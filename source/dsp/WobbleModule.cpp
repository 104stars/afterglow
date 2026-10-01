#include "WobbleModule.h"

namespace afterglow::dsp
{
void WobbleModule::prepare (double sampleRate, int)
{
    fs = sampleRate;
    pitchWindow = std::max (1, static_cast<int> (std::lround (sampleRate / 64.0)));

    for (auto& d : delay)
        d.allocate (static_cast<int> (sampleRate * 0.5) + SincTable::taps + 8);

    for (int c = 0; c < 2; ++c)
    {
        wowDrift[c].prepare (sampleRate, 0xB0B0u + static_cast<uint32_t> (c) * 1913u);
        flutterNoise[c].prepare (sampleRate, 0xF1F1u + static_cast<uint32_t> (c) * 7177u);
    }

    rateFlux.prepare (sampleRate, 0x5151u, 0.3f, 2.2f);
    depthFlux.prepare (sampleRate, 0x6161u, 0.25f, 1.8f);

    depthSm.prepare (sampleRate, 0.08f);
    balanceSm.prepare (sampleRate, 0.08f);
    wowRateSm.prepare (sampleRate, 0.25f);
    flutterRateSm.prepare (sampleRate, 0.25f);
    mixSm.prepare (sampleRate, 0.05f);
    stereoSm.prepare (sampleRate, 0.08f);
    centreSm.prepare (sampleRate, 0.6f);

    SincTable::get(); // build the table outside the audio thread
    reset();
}

void WobbleModule::reset()
{
    for (auto& d : delay)
        d.clear();

    wowPhase = flutterPhase = flutterPhase2 = 0.0;
    primed = false;
    prevDelay[0] = prevDelay[1] = -1.0f;
    prevWowDelay[0] = prevWowDelay[1] = -1.0f;
    pitchBlocks = pitchSamples = 0;
}

void WobbleModule::process (float* left, float* right, int n, const WobbleParams& p, const TransportInfo& transport) noexcept
{
    const auto& sinc = SincTable::get();
    const auto minDelay = static_cast<float> (SincTable::half + 1);
    const auto depthTarget = p.on ? p.amount : 0.0f;

    depthSm.setTarget (depthTarget);
    balanceSm.setTarget (p.balance);
    wowRateSm.setTarget (p.wowRate);
    flutterRateSm.setTarget (p.flutterRate);
    mixSm.setTarget (p.mix);
    stereoSm.setTarget (p.stereo ? 1.0f : 0.0f);

    if (! primed)
    {
        depthSm.snapTo (depthTarget);
        balanceSm.snapTo (p.balance);
        wowRateSm.snapTo (p.wowRate);
        flutterRateSm.snapTo (p.flutterRate);
        mixSm.snapTo (p.mix);
        stereoSm.snapTo (p.stereo ? 1.0f : 0.0f);
        centreSm.snapTo (minDelay + 2.0f);
        prevCentre = minDelay + 2.0f;
        prevAw = prevAf = prevAr = 0.0f;
        prevEngage = engageCurve (depthTarget, 0.03f);
        primed = true;
    }

    // Tempo sync: gently pull the wow phase towards the host's musical position.
    if (p.syncBeats > 0.0 && transport.hasPpq && transport.isPlaying)
    {
        const auto target = transport.ppqPosition / p.syncBeats;
        auto error = (target - std::floor (target)) - wowPhase;
        error -= std::round (error);
        wowPhase += 0.1 * error;
        wowPhase -= std::floor (wowPhase);
    }

    rateFlux.setAmount (p.flux);
    depthFlux.setAmount (p.flux);

    const auto bypassed = depthSm.getCurrent() <= 0.0f && depthTarget <= 0.0f;

    for (int start = 0; start < n; start += controlInterval)
    {
        const auto len = std::min (controlInterval, n - start);

        const auto a = depthSm.skip (len);
        const auto bal = balanceSm.skip (len);
        const auto fluxRate = rateFlux.advance (len);
        const auto fluxDepth = depthFlux.advance (len);
        const auto wowHz = std::max (0.02f, wowRateSm.skip (len) * (1.0f + 0.45f * fluxRate));
        const auto flutterHz = std::max (1.0f, flutterRateSm.skip (len) * (1.0f + 0.25f * fluxRate));
        const auto stereo = stereoSm.skip (len);
        const auto engage = engageCurve (a, 0.03f);

        const auto wowGain = std::min (1.0f, 2.0f * (1.0f - bal));
        const auto flutterGain = std::min (1.0f, 2.0f * bal);
        const auto depthCurve = a * a;

        const auto devWow = maxWowDeviation * depthCurve * wowGain * std::max (0.0f, 1.0f + 0.6f * fluxDepth);
        const auto devFlutter = maxFlutterDeviation * depthCurve * flutterGain * std::max (0.0f, 1.0f + 0.5f * fluxDepth);
        const auto driftHz = 0.05f + 0.8f * wowHz;
        const auto devDrift = devWow * (0.25f + 1.2f * p.flux) + 0.004f * depthCurve * p.flux * p.flux;

        const auto sr = static_cast<float> (fs);
        const auto aw = std::min (devWow / (twoPi * wowHz), maxWowSeconds) * sr;
        const auto af = devFlutter / (twoPi * flutterHz) * sr;
        const auto ar = std::min (devDrift / (twoPi * driftHz), maxWowSeconds * 0.5f) * sr;

        // The centre delay follows the needed modulation range; rising quickly, settling slowly (like a tape slowing down).
        const auto needed = aw * 1.15f + af * 1.4f + ar * 1.3f + minDelay + 2.0f;
        centreSm.prepare (fs, needed > centreSm.getCurrent() ? 0.12f : 1.5f);
        centreSm.setTarget (needed);
        const auto centre = centreSm.skip (len);

        for (int c = 0; c < 2; ++c)
        {
            wowDrift[c].setRate (driftHz);
            flutterNoise[c].setRate (flutterHz * 1.7f);
        }

        const auto wowInc = static_cast<double> (wowHz) / fs;
        const auto flutterInc = static_cast<double> (flutterHz) / fs;
        const auto invLen = 1.0f / static_cast<float> (len);
        auto endL = prevDelay[0], endR = prevDelay[1];
        auto wowEndL = prevWowDelay[0], wowEndR = prevWowDelay[1];

        for (int i = 0; i < len; ++i)
        {
            const auto idx = start + i;
            // Interpolate the control values per sample: any step in a delay time would be audible as a click.
            const auto t = static_cast<float> (i + 1) * invLen;
            const auto cCentre = lerp (prevCentre, centre, t);
            const auto cAw = lerp (prevAw, aw, t);
            const auto cAf = lerp (prevAf, af, t);
            const auto cAr = lerp (prevAr, ar, t);
            const auto cEngage = lerp (prevEngage, engage, t);

            wowPhase += wowInc;
            if (wowPhase >= 1.0)
                wowPhase -= 1.0;

            flutterPhase += flutterInc;
            if (flutterPhase >= 1.0)
                flutterPhase -= 1.0;

            flutterPhase2 += flutterInc * 2.37;
            if (flutterPhase2 >= 1.0)
                flutterPhase2 -= 1.0;

            const auto wp = static_cast<float> (wowPhase) * twoPi;
            const auto wowL = std::sin (wp) + 0.12f * std::sin (2.0f * wp + 0.5f);
            const auto wowR = lerp (wowL, std::sin (wp + 0.5f * pi) + 0.12f * std::sin (2.0f * wp + 2.1f), stereo);

            const auto flutterCore = 0.7f * std::sin (static_cast<float> (flutterPhase) * twoPi)
                                   + 0.3f * std::sin (static_cast<float> (flutterPhase2) * twoPi);
            const auto flutterL = flutterCore + 0.35f * flutterNoise[0].next();
            const auto flutterR = lerp (flutterL, flutterCore + 0.35f * flutterNoise[1].next(), stereo);

            const auto driftL = wowDrift[0].next();
            const auto driftR = lerp (driftL, wowDrift[1].next(), stereo);

            const auto dL = std::max (minDelay, cCentre + cAw * wowL + cAf * flutterL + cAr * driftL);
            const auto dR = std::max (minDelay, cCentre + cAw * wowR + cAf * flutterR + cAr * driftR);

            endL = dL;
            endR = dR;
            wowEndL = cCentre + cAw * wowL + cAr * driftL; // the slow part alone, for the display's wow line
            wowEndR = cCentre + cAw * wowR + cAr * driftR;

            const auto xL = left[idx];
            const auto xR = right[idx];
            delay[0].push (xL);
            delay[1].push (xR);

            if (bypassed)
                continue;

            const auto wetL = sinc.read (delay[0], dL);
            const auto wetR = sinc.read (delay[1], dR);
            const auto m = mixSm.next() * cEngage;
            left[idx] = xL + m * (wetL - xL);
            right[idx] = xR + m * (wetR - xR);
        }

        prevCentre = centre;
        prevAw = aw;
        prevAf = af;
        prevAr = ar;
        prevEngage = engage;

        // Display: the pitch deviation this block, from how fast the delay time changed (a delay that grows
        // by d samples over n samples plays back at (1 - d / n) of the original speed).
        if (telemetry != nullptr)
        {
            auto cents = [len] (float from, float to)
            {
                if (from < 0.0f)
                    return 0.0f;
                return 1200.0f * std::log2 (std::max (0.05f, 1.0f - (to - from) / static_cast<float> (len)));
            };
            const float full[2] { bypassed ? 0.0f : cents (prevDelay[0], endL), bypassed ? 0.0f : cents (prevDelay[1], endR) };
            const float slow[2] { bypassed ? 0.0f : cents (prevWowDelay[0], wowEndL), bypassed ? 0.0f : cents (prevWowDelay[1], wowEndR) };
            pushPitch (full, slow, len);
        }

        prevDelay[0] = endL;
        prevDelay[1] = endR;
        prevWowDelay[0] = wowEndL;
        prevWowDelay[1] = wowEndR;
    }
}

void WobbleModule::pushPitch (const float* cents, const float* wowCents, int len) noexcept
{
    // Mean of the wow alone (the line), and the extremes of the whole deviation including flutter (the band).
    for (int c = 0; c < 2; ++c)
    {
        wowSum[c] = pitchBlocks == 0 ? wowCents[c] : wowSum[c] + wowCents[c];
        pitchMin[c] = pitchBlocks == 0 ? cents[c] : std::min (pitchMin[c], cents[c]);
        pitchMax[c] = pitchBlocks == 0 ? cents[c] : std::max (pitchMax[c], cents[c]);
    }

    ++pitchBlocks;
    pitchSamples += len;

    if (pitchSamples >= pitchWindow)
    {
        const auto count = static_cast<float> (pitchBlocks);
        telemetry->wobblePitch.push ({ wowSum[0] / count, pitchMin[0], pitchMax[0], wowSum[1] / count, pitchMin[1], pitchMax[1] });
        pitchBlocks = pitchSamples = 0;
    }
}

} // namespace afterglow::dsp
