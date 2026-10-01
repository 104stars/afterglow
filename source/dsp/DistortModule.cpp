#include "DistortModule.h"

namespace afterglow::dsp
{
namespace
{
    float softClipKnee (float u) noexcept
    {
        // Smooth "tape-like" saturator with a gentle knee: u / (1 + |u|^2.5)^(1/2.5)
        const auto a = std::abs (u);
        return u / std::pow (1.0f + std::pow (a, 2.5f), 0.4f);
    }

    float kneeClip (float x) noexcept
    {
        constexpr float threshold = 0.8f;
        const auto a = std::abs (x);
        if (a <= threshold)
            return x;
        const auto over = (a - threshold) / (1.0f - threshold);
        return std::copysign (threshold + (1.0f - threshold) * std::tanh (over), x);
    }

    std::unique_ptr<juce::dsp::Oversampling<float>> makeOversampler (int order)
    {
        return std::make_unique<juce::dsp::Oversampling<float>> (2, static_cast<size_t> (order),
                                                                 juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                                                                 true, true);
    }
} // namespace

float DistortModule::maxDriveDb (int type) noexcept
{
    static constexpr float drive[numTypes] { 36.0f, 30.0f, 34.0f, 30.0f, 42.0f, 34.0f, 22.0f, 30.0f };
    return drive[juce::jlimit (0, numTypes - 1, type)];
}

float DistortModule::staticCurve (int type, float x, float bias) noexcept
{
    switch (type)
    {
        case tube:        return std::tanh (x + bias) - std::tanh (bias);
        case transformer: return std::tanh (0.7f * x) / 0.7f;
        case speaker:     return std::clamp (x > 0.0f ? 0.6f * std::tanh (x / 0.6f) : std::tanh (x), -1.2f, 0.9f);
        case tape:        return softClipKnee (x + bias) - softClipKnee (bias);
        case fuzz:
        {
            const auto u = x + 0.3f * bias;
            return u > 0.0f ? std::tanh (3.0f * u) : 0.75f * std::tanh (1.4f * u);
        }
        case clip:        return kneeClip (x);
        case fold:        return std::sin (0.5f * pi * (x + bias)) - std::sin (0.5f * pi * bias);
        case rectify:
        {
            const auto t = std::tanh (x);
            return 0.45f * t + 0.9f * std::abs (t);
        }
        default:          return x;
    }
}

int DistortModule::latencyForOrder (int order, double, int maxBlockSize)
{
    if (order <= 0)
        return 0;

    auto os = makeOversampler (order);
    os->initProcessing (static_cast<size_t> (maxBlockSize));
    return static_cast<int> (std::lround (os->getLatencyInSamples()));
}

void DistortModule::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate;
    maxBlock = maxBlockSize;

    for (int order = 1; order <= 3; ++order)
    {
        oversamplers[order] = makeOversampler (order);
        oversamplers[order]->initProcessing (static_cast<size_t> (maxBlockSize));
    }

    bandBuffer.setSize (2, maxBlockSize);

    // Room for a whole block plus the 8x filters' latency.
    for (int c = 0; c < 2; ++c)
    {
        dryDelay[c].allocate (maxBlockSize + 512);
        bandDelay[c].allocate (maxBlockSize + 512);
        tilt[c].prepare (sampleRate, 1500.0f);
        dc[c].prepare (sampleRate, 10.0f);
        shapers[c].rng.setSeed (0xD157u + static_cast<uint32_t> (c) * 3331u);
    }

    driveSm.prepare (sampleRate, 0.05f);
    mixSm.prepare (sampleRate, 0.05f);
    toneSm.prepare (sampleRate, 0.05f);
    lowSm.prepare (sampleRate, 0.06f);
    highSm.prepare (sampleRate, 0.06f);
    makeupSm.prepare (sampleRate, 0.08f);
    engageSm.prepare (sampleRate, 0.03f);
    typeFade.prepare (sampleRate, 0.02f);

    driveFlux.prepare (sampleRate, 0xD1D1u, 0.25f, 2.5f);
    biasFlux.prepare (sampleRate, 0xB1A5u, 0.15f, 1.2f);
    focusFlux.prepare (sampleRate, 0xF0C5u, 0.2f, 1.5f);

    setOversamplingOrder (currentOrder);
    reset();
}

void DistortModule::reset()
{
    for (int order = 1; order <= 3; ++order)
        if (oversamplers[order] != nullptr)
            oversamplers[order]->reset();

    for (int c = 0; c < 2; ++c)
    {
        dryDelay[c].clear();
        bandDelay[c].clear();
        focusHigh[c].reset();
        focusLow[c].reset();
        tilt[c].reset();
        dc[c].reset();
        auto& s = shapers[c];
        s.sag.reset();
        s.gate.reset();
        s.transformerLow.reset();
        s.speakerHighPass.reset();
        s.fuzzLowPass.reset();
        s.speakerBell.reset();
        s.speakerLowPass.reset();
        s.rattleBand.reset();
        s.tapePre.reset();
        s.tapeDe.reset();
    }

    typeFade.snapTo (1.0f);
    pendingType = -1;
    primed = false;
    glow = 0.0f;
}

void DistortModule::setOversamplingOrder (int order) noexcept
{
    order = juce::jlimit (0, 3, order);

    if (order != currentOrder || latency == 0)
    {
        currentOrder = order;

        if (order > 0 && oversamplers[order] != nullptr)
        {
            oversamplers[order]->reset();
            latency = static_cast<int> (std::lround (oversamplers[order]->getLatencyInSamples()));
        }
        else
        {
            latency = 0;
        }

        configureShapers (fs * static_cast<double> (1 << order));
    }
}

void DistortModule::configureShapers (double rate)
{
    for (auto& s : shapers)
    {
        s.sag.prepare (rate, 8.0f, 120.0f);
        s.gate.prepare (rate, 0.5f, 40.0f);
        s.transformerLow.setCutoff (110.0f, rate);
        s.speakerHighPass.setCutoff (95.0f, rate);
        s.fuzzLowPass.setCutoff (6500.0f, rate);
        s.speakerBell.setup (Svf::Type::bell, 2400.0f, 1.1f, 6.0f, rate);
        s.speakerLowPass.setup (Svf::Type::lowPass, 5200.0f, 0.8f, 0.0f, rate);
        s.rattleBand.setup (Svf::Type::bandPass, 3200.0f, 2.0f, 0.0f, rate);
        s.tapePre.setup (Svf::Type::highShelf, 2500.0f, 0.6f, 6.0f, rate);
        s.tapeDe.setup (Svf::Type::highShelf, 2500.0f, 0.6f, -6.0f, rate);
    }
}

void DistortModule::updateFocus (float lowHz, float highHz) noexcept
{
    highHz = std::max (highHz, lowHz * 1.15f);

    for (int c = 0; c < 2; ++c)
    {
        focusHigh[c].setup (Svf::Type::highPass, lowHz, 0.7071f, 0.0f, fs);
        focusLow[c].setup (Svf::Type::lowPass, std::min (highHz, 0.47f * static_cast<float> (fs)), 0.7071f, 0.0f, fs);
    }
}

float DistortModule::shape (int type, float x, ShaperState& s, float bias) noexcept
{
    switch (type)
    {
        case tube:
        {
            // Push-pull triode pair: soft, mostly odd, with power-supply sag and a little bias imbalance.
            const auto env = s.sag.process (x);
            const auto xs = x / (1.0f + 0.25f * env);
            return fastTanh (xs + bias) - fastTanh (bias);
        }
        case transformer:
        {
            // Iron saturates first at low frequencies (core flux follows the integral of the voltage).
            const auto low = s.transformerLow.processLP (x);
            const auto high = x - low;
            const auto satLow = (fastTanh (2.0f * low + bias) - fastTanh (bias)) * 0.5f;
            return fastTanh (0.7f * (high + satLow)) / 0.7f;
        }
        case speaker:
        {
            // A torn cone: asymmetric excursion limit, rattle on loud peaks and a narrow, honky response.
            auto v = s.speakerHighPass.processHP (x);
            v = v > 0.0f ? 0.6f * fastTanh (v / 0.6f) : fastTanh (v);
            const auto excess = std::max (0.0f, std::abs (v) - 0.35f);
            v += s.rattleBand.process (s.rng.nextBipolar()) * excess * 1.8f;
            v = s.speakerLowPass.process (s.speakerBell.process (v));
            return std::clamp (v, -1.2f, 0.9f);
        }
        case tape:
        {
            // Pre-emphasis, soft saturation, de-emphasis: highs compress earlier than lows.
            const auto pre = s.tapePre.process (x);
            const auto sat = softClipKnee (pre + bias) - softClipKnee (bias);
            return s.tapeDe.process (sat);
        }
        case fuzz:
        {
            // Starved germanium fuzz: asymmetric, gated at low levels, fizzy on top.
            const auto env = s.gate.process (x);
            const auto gate = std::clamp ((env - 0.03f) / 0.12f, 0.0f, 1.0f);
            const auto u = x + 0.3f * bias;
            const auto y = u > 0.0f ? fastTanh (3.0f * u) : 0.75f * fastTanh (1.4f * u);
            return s.fuzzLowPass.processLP (y) * (0.25f + 0.75f * gate);
        }
        case clip:    return kneeClip (x);
        case fold:    return std::sin (0.5f * pi * (x + bias)) - std::sin (0.5f * pi * bias);
        case rectify:
        {
            const auto t = fastTanh (x);
            return 0.45f * t + 0.9f * std::abs (t);
        }
        default: return x;
    }
}

float DistortModule::computeMakeup (int type, float drive, float bias) const noexcept
{
    // Level-match at a reference sine of about -9 dBFS so the drive knob changes colour, not volume.
    constexpr int points = 64;
    constexpr float reference = 0.35f;
    float values[points];
    auto mean = 0.0f;

    for (int i = 0; i < points; ++i)
    {
        const auto x = reference * std::sin (twoPi * (static_cast<float> (i) + 0.5f) / points);
        values[i] = staticCurve (type, x * drive, bias);
        mean += values[i];
    }

    mean /= points;
    auto sum = 0.0f;
    for (auto v : values)
        sum += (v - mean) * (v - mean);

    // Per-type trims account for the filters and dynamics that the static curve leaves out.
    static constexpr float trim[numTypes] { 0.92f, 0.90f, 0.75f, 0.90f, 0.80f, 0.85f, 0.75f, 0.85f };
    const auto rmsOut = std::sqrt (sum / points);
    const auto rmsIn = reference * 0.70710678f;
    return rmsOut > 1.0e-6f ? std::clamp (rmsIn / rmsOut, 0.01f, 4.0f) * trim[juce::jlimit (0, numTypes - 1, type)] : 1.0f;
}

void DistortModule::process (float* left, float* right, int n, const DistortParams& p) noexcept
{
    const auto amountTarget = p.on ? p.amount : 0.0f;
    const auto engageTarget = engageCurve (amountTarget, 0.04f);

    if (! primed)
    {
        currentType = p.type;
        driveSm.snapTo (maxDriveDb (p.type) * std::pow (amountTarget, 1.15f));
        mixSm.snapTo (p.mix);
        toneSm.snapTo (p.tone);
        lowSm.snapTo (std::log (p.focusLow));
        highSm.snapTo (std::log (p.focusHigh));
        engageSm.snapTo (engageTarget);
        makeupSm.snapTo (computeMakeup (p.type, dbToGain (driveSm.getCurrent()), 0.0f));
        lastDrive = dbToGain (driveSm.getCurrent());
        primed = true;
    }

    if (p.type != currentType && pendingType != p.type)
    {
        pendingType = p.type;
        typeFade.setTarget (0.0f);
    }

    driveSm.setTarget (maxDriveDb (currentType) * std::pow (amountTarget, 1.15f));
    mixSm.setTarget (p.mix);
    toneSm.setTarget (p.tone);
    lowSm.setTarget (std::log (std::max (p.focusLow, 10.0f)));
    highSm.setTarget (std::log (std::max (p.focusHigh, 20.0f)));
    engageSm.setTarget (engageTarget);

    driveFlux.setAmount (p.flux);
    biasFlux.setAmount (p.flux);
    focusFlux.setAmount (p.flux);

    const auto delayRead = latency + 1;

    // Fully disengaged: keep the latency-matched dry path running and skip the expensive part.
    if (engageSm.getCurrent() <= 0.0f && engageTarget <= 0.0f)
    {
        for (int i = 0; i < n; ++i)
        {
            dryDelay[0].push (left[i]);
            dryDelay[1].push (right[i]);
            bandDelay[0].push (0.0f);
            bandDelay[1].push (0.0f);
            left[i] = dryDelay[0].read (delayRead);
            right[i] = dryDelay[1].read (delayRead);
        }

        glow = 0.0f;
        return;
    }

    auto* bandL = bandBuffer.getWritePointer (0);
    auto* bandR = bandBuffer.getWritePointer (1);

    // 1) Isolate the focus band, remember dry and band for the latency-matched recombination, apply drive.
    auto levelSum = 0.0f;

    for (int start = 0; start < n; start += controlInterval)
    {
        const auto len = std::min (controlInterval, n - start);
        const auto focusShift = 0.35f * focusFlux.advance (len) * 2.3f; // up to about +-1/3 octave of drift
        updateFocus (std::exp (lowSm.skip (len) + focusShift), std::exp (highSm.skip (len) + focusShift));

        // Drive is computed at control rate and interpolated, the dB conversion is too costly per sample.
        const auto driveTarget = dbToGain (driveSm.skip (len) + 6.0f * driveFlux.advance (len));
        const auto invLen = 1.0f / static_cast<float> (len);

        for (int i = start; i < start + len; ++i)
        {
            const auto mL = focusLow[0].process (focusHigh[0].process (left[i]));
            const auto mR = focusLow[1].process (focusHigh[1].process (right[i]));
            dryDelay[0].push (left[i]);
            dryDelay[1].push (right[i]);
            bandDelay[0].push (mL);
            bandDelay[1].push (mR);

            const auto drive = lerp (lastDrive, driveTarget, static_cast<float> (i - start + 1) * invLen);
            bandL[i] = mL * drive;
            bandR[i] = mR * drive;
            levelSum += mL * mL + mR * mR;
        }

        lastDrive = driveTarget;
    }

    // A slowly drifting bias adds even harmonics, like a warming-up circuit.
    const auto baseBias = currentType == tube ? 0.08f : (currentType == fuzz ? 0.25f : (currentType == tape ? 0.04f : 0.0f));
    const auto bias = baseBias + 0.3f * biasFlux.advance (n);

    // 2) Non-linear stage, oversampled.
    juce::dsp::AudioBlock<float> block (bandBuffer.getArrayOfWritePointers(), 2, static_cast<size_t> (n));
    const auto type = currentType;

    if (currentOrder > 0)
    {
        auto& os = *oversamplers[currentOrder];
        auto up = os.processSamplesUp (block);

        for (size_t c = 0; c < 2; ++c)
        {
            auto* d = up.getChannelPointer (c);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
                d[i] = shape (type, d[i], shapers[c], bias);
        }

        os.processSamplesDown (block);
    }
    else
    {
        for (int i = 0; i < n; ++i)
        {
            bandL[i] = shape (type, bandL[i], shapers[0], bias);
            bandR[i] = shape (type, bandR[i], shapers[1], bias);
        }
    }

    // 3) Make-up gain, tone, and recombination with the untouched part of the spectrum.
    makeupSm.setTarget (computeMakeup (type, dbToGain (driveSm.getTarget()), bias));

    for (int start = 0; start < n; start += controlInterval)
    {
        const auto len = std::min (controlInterval, n - start);
        const auto tone = toneSm.skip (len);
        for (auto& t : tilt)
            t.setTilt (tone, 9.0f);

        for (int i = start; i < start + len; ++i)
        {
            if (pendingType >= 0 && typeFade.get() <= 0.0f)
            {
                currentType = pendingType;
                pendingType = -1;
                typeFade.setTarget (1.0f);
                for (auto& s : shapers)
                {
                    s.sag.reset();
                    s.gate.reset();
                }
            }

            const auto makeup = makeupSm.next();
            const auto g = mixSm.next() * engageSm.next() * typeFade.next();
            const auto yL = tilt[0].process (dc[0].process (bandL[i]) * makeup);
            const auto yR = tilt[1].process (dc[1].process (bandR[i]) * makeup);
            // Every input sample of this block has been pushed already, so sample i sits (n - 1 - i) further back.
            const auto back = delayRead + (n - 1 - i);
            const auto bL = bandDelay[0].read (back);
            const auto bR = bandDelay[1].read (back);
            left[i] = dryDelay[0].read (back) + g * (yL - bL);
            right[i] = dryDelay[1].read (back) + g * (yR - bR);
        }
    }

    const auto rms = std::sqrt (levelSum / static_cast<float> (std::max (1, 2 * n)));
    glow = engageSm.getCurrent() * clamp01 (driveSm.getCurrent() / maxDriveDb (currentType) * 1.4f)
         * clamp01 (0.35f + 2.5f * rms);
}

void DistortModule::publish (EngineTelemetry& telemetry) const noexcept
{
    telemetry.distortDrive.store (glow, std::memory_order_relaxed);
}

} // namespace afterglow::dsp
