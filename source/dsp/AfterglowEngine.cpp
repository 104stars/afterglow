#include "AfterglowEngine.h"

namespace afterglow::dsp
{
void AfterglowEngine::prepare (double sampleRate, int maxBlockSize, int oversamplingOrder)
{
    fs = sampleRate;
    maxBlock = std::max (1, maxBlockSize);

    noise.prepare (sampleRate, maxBlock);
    wobble.prepare (sampleRate, maxBlock);
    distort.prepare (sampleRate, maxBlock);
    distort.setOversamplingOrder (oversamplingOrder);
    digital.prepare (sampleRate, maxBlock);
    space.prepare (sampleRate, maxBlock);
    magnetic.prepare (sampleRate, maxBlock);
    master.prepare (sampleRate, maxBlock);

    for (auto& d : dryDelay)
        d.allocate (maxBlock + 1024);

    dryL.assign (static_cast<size_t> (maxBlock), 0.0f);
    dryR.assign (static_cast<size_t> (maxBlock), 0.0f);
    mixSm.prepare (sampleRate, 0.03f);
    reset();
}

void AfterglowEngine::reset()
{
    noise.reset();
    wobble.reset();
    distort.reset();
    digital.reset();
    space.reset();
    magnetic.reset();
    master.reset();

    for (auto& d : dryDelay)
        d.clear();

    mixPrimed = false;
}

int AfterglowEngine::setOversamplingOrder (int order) noexcept
{
    distort.setOversamplingOrder (order);
    return distort.getLatencySamples();
}

void AfterglowEngine::process (float* left, float* right, int n, const EngineParams& params, const TransportInfo& transport) noexcept
{
    for (int start = 0; start < n; start += maxBlock)
    {
        const auto len = std::min (maxBlock, n - start);
        processChunk (left + start, right + start, len, params, transport);
    }
}

void AfterglowEngine::processChunk (float* left, float* right, int n, const EngineParams& in, const TransportInfo& transport) noexcept
{
    publishMeters (left, right, n, true);

    // Magnitude scales every big knob and the master section at once: 0 % is a clean signal.
    const auto mag = clamp01 (in.master.magnitude);
    auto p = in;
    p.noise.amount *= mag;
    p.wobble.amount *= mag;
    p.distort.amount *= mag;
    p.digital.amount *= mag;
    p.space.amount *= mag;
    p.magnetic.amount *= mag;

    MasterSection::Settings ms;
    ms.inGainDb = in.master.inGainDb * mag;
    ms.outGainDb = in.master.outGainDb * mag;
    ms.eqOn = in.master.eqOn;
    ms.lowCutHz = logLerp (10.0f, std::max (10.0f, in.master.lowCut), mag);
    ms.highCutHz = logLerp (22000.0f, std::min (22000.0f, in.master.highCut), mag);
    ms.lowCutHard = in.master.lowCutHard;
    ms.highCutHard = in.master.highCutHard;
    ms.tone = in.master.tone * mag;
    ms.toneMode = in.master.toneMode;
    ms.width = lerp (1.0f, in.master.width, mag);
    ms.limiter = in.master.limiter;
    master.setSettings (ms);

    // Keep the untouched input for the global Mix control.
    std::copy_n (left, n, dryL.data());
    std::copy_n (right, n, dryR.data());

    master.processInput (left, right, n);

    noise.analyseInput (left, right, n);
    noise.render (n, p.noise, transport);
    if (! p.noise.post)
        noise.addTo (left, right, n);

    wobble.process (left, right, n, p.wobble, transport);
    distort.process (left, right, n, p.distort);
    digital.process (left, right, n, p.digital);
    space.process (left, right, n, p.space);
    magnetic.process (left, right, n, p.magnetic, transport);

    master.processEq (left, right, n);

    if (p.noise.post)
        noise.addTo (left, right, n);

    master.processOutput (left, right, n);

    // Global dry/wet against a dry signal delayed by the plugin's latency.
    const auto latency = distort.getLatencySamples();
    mixSm.setTarget (clamp01 (in.master.mix));
    if (! mixPrimed)
    {
        mixSm.snapTo (mixSm.getTarget());
        mixPrimed = true;
    }

    for (int i = 0; i < n; ++i)
    {
        dryDelay[0].push (dryL[static_cast<size_t> (i)]);
        dryDelay[1].push (dryR[static_cast<size_t> (i)]);
    }

    if (! (mixSm.isSettled() && mixSm.getCurrent() >= 1.0f))
    {
        for (int i = 0; i < n; ++i)
        {
            const auto back = latency + 1 + (n - 1 - i);
            const auto g = mixSm.next();
            left[i] = lerp (dryDelay[0].read (back), left[i], g);
            right[i] = lerp (dryDelay[1].read (back), right[i], g);
        }
    }

    master.processLimiter (left, right, n);

    publishMeters (left, right, n, false);
    noise.publish (telemetry);
    wobble.publish (telemetry);
    distort.publish (telemetry);
    digital.publish (telemetry);
    space.publish (telemetry);
    magnetic.publish (telemetry);

    // Feed the noise oscilloscope (decimated).
    const auto* rendered = noise.getRenderedLeft();
    auto write = telemetry.noiseScopeWrite.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        if (++scopeDecimator >= 6)
        {
            scopeDecimator = 0;
            telemetry.noiseScope[static_cast<size_t> (write)].store (rendered[i], std::memory_order_relaxed);
            write = (write + 1) % EngineTelemetry::scopeSize;
        }
    }
    telemetry.noiseScopeWrite.store (write, std::memory_order_relaxed);
}

void AfterglowEngine::publishMeters (const float* left, const float* right, int n, bool input) noexcept
{
    const float* ch[2] { left, right };

    for (int c = 0; c < 2; ++c)
    {
        auto peak = 0.0f, sum = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const auto v = ch[c][i];
            peak = std::max (peak, std::abs (v));
            sum += v * v;
        }

        const auto rms = n > 0 ? std::sqrt (sum / static_cast<float> (n)) : 0.0f;
        auto& peakSlot = input ? telemetry.inputPeak[c] : telemetry.outputPeak[c];
        auto& rmsSlot = input ? telemetry.inputRms[c] : telemetry.outputRms[c];

        // Hold the maximum until the UI reads (and resets) it.
        if (peak > peakSlot.load (std::memory_order_relaxed))
            peakSlot.store (peak, std::memory_order_relaxed);
        rmsSlot.store (rms, std::memory_order_relaxed);
    }
}

} // namespace afterglow::dsp
