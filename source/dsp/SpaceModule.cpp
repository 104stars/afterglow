#include "SpaceModule.h"

namespace afterglow::dsp
{
namespace
{
    bool isPrime (int v) noexcept
    {
        if (v < 2)
            return false;
        if (v % 2 == 0)
            return v == 2;
        for (int d = 3; d * d <= v; d += 2)
            if (v % d == 0)
                return false;
        return true;
    }

    int nextPrime (int v) noexcept
    {
        while (! isPrime (v))
            ++v;
        return v;
    }

    /** In-place fast Walsh-Hadamard transform, normalised to be orthonormal (energy preserving). */
    template <int N>
    void hadamard (float* v) noexcept
    {
        for (int h = 1; h < N; h <<= 1)
            for (int i = 0; i < N; i += 2 * h)
                for (int j = i; j < i + h; ++j)
                {
                    const auto a = v[j];
                    const auto b = v[j + h];
                    v[j] = a + b;
                    v[j + h] = a - b;
                }

        const auto scale = 1.0f / std::sqrt (static_cast<float> (N));
        for (int i = 0; i < N; ++i)
            v[i] *= scale;
    }
} // namespace

const SpaceModule::TypeSpec& SpaceModule::spec (int type) noexcept
{
    //                                   delays ms      RT60 s        diffusion     damp    low   mod ms rate   early  gain   norm
    static const TypeSpec specs[numTypes] {
        /* ambience  */ {   3.0f,  16.0f,  0.12f,  1.2f,  1.5f, 0.60f,  9000.0f,  60.0f, 0.08f, 0.60f, 0.80f, 0.46f, 0.50f },
        /* room      */ {   7.0f,  38.0f,  0.25f,  3.0f,  3.0f, 0.65f,  7000.0f,  50.0f, 0.20f, 0.50f, 0.55f, 0.51f, 0.34f },
        /* plate     */ {   4.0f,  48.0f,  0.50f,  8.0f,  4.0f, 0.72f, 12000.0f,  70.0f, 0.25f, 0.80f, 0.00f, 0.75f, 0.52f },
        /* hall      */ {  25.0f, 115.0f,  0.80f, 14.0f,  6.0f, 0.70f,  6000.0f,  40.0f, 0.60f, 0.35f, 0.25f, 0.44f, 0.25f },
        /* spring    */ {  28.0f,  46.0f,  0.60f,  5.0f,  0.0f, 0.00f,  4500.0f, 180.0f, 0.15f, 1.10f, 0.00f, 1.10f, 0.27f },
        /* resonator */ {   0.0f,   0.0f,  0.30f,  6.0f,  0.0f, 0.00f, 10000.0f,  60.0f, 0.00f, 0.00f, 0.00f, 0.28f, 0.25f },
    };
    return specs[juce::jlimit (0, numTypes - 1, type)];
}

void SpaceModule::prepare (double sampleRate, int)
{
    fs = sampleRate;

    for (int i = 0; i < numLines; ++i)
    {
        lines[i].allocate (static_cast<int> (sampleRate * 0.2));
        lineWander[i].prepare (sampleRate, 0x5ACEu + static_cast<uint32_t> (i) * 101u);
        lineWander[i].setRate (0.15f + 0.05f * static_cast<float> (i % 5));
    }

    for (int c = 0; c < 2; ++c)
    {
        preDelay[c].allocate (static_cast<int> (sampleRate * 0.3));
        for (auto& d : diffusers[c])
            d.buffer.allocate (static_cast<int> (sampleRate * 0.03));
    }

    early.allocate (static_cast<int> (sampleRate * 0.06));

    modFlux.prepare (sampleRate, 0x3D3Du, 0.2f, 1.5f);
    decayFlux.prepare (sampleRate, 0x4E4Eu, 0.12f, 0.9f);
    predelayFlux.prepare (sampleRate, 0x5F5Fu, 0.15f, 1.1f);

    amountSm.prepare (sampleRate, 0.05f);
    decaySm.prepare (sampleRate, 0.15f);
    preDelaySm.prepare (sampleRate, 0.25f);
    lowSm.prepare (sampleRate, 0.08f);
    highSm.prepare (sampleRate, 0.08f);
    stereoSm.prepare (sampleRate, 0.08f);
    typeFade.prepare (sampleRate, 0.04f);
    wetWindow = std::max (1, static_cast<int> (std::lround (sampleRate * 0.01)));

    currentType = -1;
    reset();
}

void SpaceModule::reset()
{
    for (int i = 0; i < numLines; ++i)
    {
        lines[i].clear();
        damping[i].reset();
        lowCut[i].reset();
        lineOut[i] = 0.0f;
        lineMod[i] = 0.0f;
        for (auto& d : dispersion[i])
            d.x1 = d.y1 = 0.0f;
    }

    for (int c = 0; c < 2; ++c)
    {
        preDelay[c].clear();
        inputHigh[c].reset();
        inputLow[c].reset();
        for (auto& d : diffusers[c])
            d.buffer.clear();
    }

    early.clear();
    typeFade.snapTo (1.0f);
    pendingType = -1;
    primed = false;
    wetCount = 0;
    wetSum = 0.0f;
    onsetFast = onsetSlow = 0.0f;
    samplesSinceOnset = 1 << 30;
    std::fill (std::begin (notes), std::end (notes), 0.0f);
}

float SpaceModule::decaySeconds (int type, float decay) noexcept
{
    const auto& s = spec (type);
    return s.minRt * std::pow (s.maxRt / s.minRt, std::clamp (decay, 0.0f, 1.0f));
}

void SpaceModule::buildUpMs (int type, float& minMs, float& maxMs) noexcept
{
    const auto& s = spec (type);
    minMs = s.minDelayMs;
    maxMs = s.maxDelayMs;
}

float SpaceModule::decaySecondsAt (int type, float rt, float focusLowHz, float focusHighHz, float hz) noexcept
{
    // Each pass through a line of mean length L loses 60 L / rt dB, plus whatever the loop filters take at
    // this frequency, so the decay rate in dB per second is 60 / rt - filterDb / L.
    const auto& s = spec (type);
    const auto dampHz = std::min (focusHighHz, s.dampHz);
    const auto lowHz = std::max (focusLowHz, s.lowHz);
    const auto lp = 1.0f / std::sqrt (1.0f + (hz / dampHz) * (hz / dampHz));
    const auto hp = (hz / lowHz) / std::sqrt (1.0f + (hz / lowHz) * (hz / lowHz));
    const auto filterDb = 20.0f * std::log10 (std::max (1.0e-6f, lp * hp));
    const auto lineSeconds = type == resonator ? 0.005f : 0.001f * std::sqrt (s.minDelayMs * s.maxDelayMs);
    const auto rate = 60.0f / rt - filterDb / lineSeconds;
    return rate > 1.0e-3f ? 60.0f / rate : 60.0f;
}

void SpaceModule::configureType (int type)
{
    currentType = type;
    const auto& s = spec (type);
    const auto sr = static_cast<float> (fs);

    if (type == resonator)
    {
        // Twelve chromatic notes from C3, then four more: a resonator that rings with any key.
        for (int i = 0; i < numLines; ++i)
        {
            const auto hz = 130.8128f * std::pow (2.0f, static_cast<float> (i) / 12.0f);
            lineLength[i] = sr / hz;
        }
    }
    else
    {
        for (int i = 0; i < numLines; ++i)
        {
            const auto t = static_cast<float> (i) / static_cast<float> (numLines - 1);
            const auto ms = s.minDelayMs * std::pow (s.maxDelayMs / s.minDelayMs, t) * (1.0f + 0.05f * std::sin (static_cast<float> (i) * 2.3f));
            lineLength[i] = static_cast<float> (nextPrime (static_cast<int> (ms * 0.001f * sr)));
        }
    }

    for (int i = 0; i < numLines; ++i)
    {
        const auto golden = std::fmod (static_cast<float> (i) * 0.618034f, 1.0f);
        modInc[i] = s.modRateHz * (0.7f + 0.6f * golden) / sr;
        modPhase[i] = golden;

        for (auto& d : dispersion[i])
        {
            d.a = type == spring ? 0.62f : 0.0f;
            d.x1 = d.y1 = 0.0f;
        }

        lines[i].clear();
        damping[i].reset();
        lowCut[i].reset();
        lineOut[i] = 0.0f;
        lineMod[i] = 0.0f;
    }

    static constexpr float diffusionRatios[numDiffusers] { 1.0f, 1.37f, 2.13f, 2.87f };

    for (int c = 0; c < 2; ++c)
    {
        for (int k = 0; k < numDiffusers; ++k)
        {
            auto& d = diffusers[c][k];
            const auto ms = std::max (0.1f, s.diffusionScaleMs) * diffusionRatios[k] * (c == 0 ? 1.0f : 1.07f);
            d.length = std::max (1, nextPrime (static_cast<int> (ms * 0.001f * sr)));
            d.gain = s.diffusionGain;
            d.buffer.clear();
        }
    }

    // Early reflections: a sparse, decaying pattern, alternating sides.
    static constexpr float tapMs[numEarlyTaps] { 3.1f, 5.3f, 7.9f, 11.2f, 14.6f, 18.9f, 23.4f, 29.7f };
    const auto earlyScale = type == ambience ? 0.6f : 1.0f;

    for (int k = 0; k < numEarlyTaps; ++k)
    {
        earlyTap[k] = std::max (1, static_cast<int> (tapMs[k] * earlyScale * 0.001f * sr));
        const auto g = std::pow (0.78f, static_cast<float> (k)) * 0.55f;
        const auto side = (k % 2 == 0) ? 0.85f : 0.45f;
        earlyGainL[k] = g * side;
        earlyGainR[k] = g * (1.3f - side);
    }

    early.clear();
}

void SpaceModule::process (float* left, float* right, int n, const SpaceParams& p) noexcept
{
    const auto amountTarget = p.on ? p.amount : 0.0f;

    amountSm.setTarget (amountTarget);
    decaySm.setTarget (p.decay);
    preDelaySm.setTarget (p.preDelayMs);
    lowSm.setTarget (std::log (std::max (p.focusLow, 10.0f)));
    highSm.setTarget (std::log (std::max (p.focusHigh, 20.0f)));
    stereoSm.setTarget (p.stereo ? 1.0f : 0.0f);

    if (! primed)
    {
        amountSm.snapTo (amountTarget);
        decaySm.snapTo (p.decay);
        preDelaySm.snapTo (p.preDelayMs);
        lowSm.snapTo (lowSm.getTarget());
        highSm.snapTo (highSm.getTarget());
        stereoSm.snapTo (p.stereo ? 1.0f : 0.0f);
        configureType (p.type);
        primed = true;
    }

    if (p.type != currentType && pendingType != p.type)
    {
        pendingType = p.type;
        typeFade.setTarget (0.0f);
    }

    modFlux.setAmount (p.flux);
    decayFlux.setAmount (p.flux);
    predelayFlux.setAmount (p.flux);

    if (amountSm.getCurrent() <= 0.0f && amountTarget <= 0.0f)
        return;

    const auto sr = static_cast<float> (fs);
    const auto onsetFastRelease = std::exp (-static_cast<float> (controlInterval) / (0.04f * sr));
    const auto onsetSlowAttack = std::exp (-static_cast<float> (controlInterval) / (0.06f * sr));
    const auto onsetSlowRelease = std::exp (-static_cast<float> (controlInterval) / (0.12f * sr));
    noteCount = 0;
    std::fill (std::begin (noteSum), std::end (noteSum), 0.0f);

    for (int start = 0; start < n; start += controlInterval)
    {
        const auto len = std::min (controlInterval, n - start);

        if (pendingType >= 0 && typeFade.get() <= 0.0f)
        {
            configureType (pendingType);
            pendingType = -1;
            typeFade.setTarget (1.0f);
        }

        const auto& s = spec (currentType);
        const auto decay = decaySm.skip (len);
        const auto fluxDecay = decayFlux.advance (len);
        const auto rt = s.minRt * std::pow (s.maxRt / s.minRt, decay) * std::max (0.3f, 1.0f + 0.35f * fluxDecay);
        const auto lowHz = std::exp (lowSm.skip (len));
        const auto highHz = std::exp (highSm.skip (len));
        const auto preMs = std::max (0.0f, preDelaySm.skip (len) + 25.0f * std::abs (predelayFlux.advance (len)));
        lastRt = rt;
        lastPreMs = preMs;

        // Display: detect note onsets on the input, so the decay recorder can restart at each hit. The slow
        // envelope follows a decaying note closely (120 ms release), so a new note stands out against it.
        if (telemetry != nullptr)
        {
            auto peak = 0.0f;
            for (int i = start; i < start + len; ++i)
                peak = std::max (peak, std::max (std::abs (left[i]), std::abs (right[i])));

            onsetFast = std::max (peak, onsetFast * onsetFastRelease);
            const auto isOnset = onsetFast > 2.0f * onsetSlow && onsetFast > 0.003f && samplesSinceOnset > static_cast<int> (0.12f * sr);
            onsetSlow = onsetFast > onsetSlow ? onsetFast + (onsetSlow - onsetFast) * onsetSlowAttack
                                              : onsetFast + (onsetSlow - onsetFast) * onsetSlowRelease;
            samplesSinceOnset = std::min (samplesSinceOnset + len, 1 << 30);

            if (isOnset)
            {
                samplesSinceOnset = 0;
                telemetry->spaceOnsetEntry.store (telemetry->spaceWet.written.load (std::memory_order_relaxed), std::memory_order_relaxed);
                telemetry->spaceOnsetCount.fetch_add (1, std::memory_order_release);
            }
        }
        const auto modDepth = (s.modDepthMs + 1.2f * p.flux * (currentType == resonator ? 0.1f : 1.0f)) * 0.001f * sr;
        const auto fluxMod = 1.0f + 2.0f * std::abs (modFlux.advance (len));

        // In-loop damping: the type's natural damping, narrowed further by the Focus band.
        const auto dampHz = std::min (highHz, s.dampHz);
        const auto loopLowHz = std::max (lowHz, s.lowHz);
        auto gainSum = 0.0f;
        float lineGain[numLines];

        for (int i = 0; i < numLines; ++i)
        {
            damping[i].setCutoff (dampHz, fs);
            lowCut[i].setCutoff (loopLowHz, fs);
            lineGain[i] = std::min (0.9995f, std::pow (10.0f, -3.0f * lineLength[i] / (rt * sr)));
            gainSum += lineGain[i] * lineGain[i];
        }

        for (int c = 0; c < 2; ++c)
        {
            inputHigh[c].setup (Svf::Type::highPass, lowHz, 0.6f, 0.0f, fs);
            inputLow[c].setup (Svf::Type::lowPass, std::min (highHz, 0.47f * sr), 0.6f, 0.0f, fs);
        }

        // Energy normalisation: longer tails build up more energy, so scale the output down accordingly.
        const auto meanGain2 = gainSum / static_cast<float> (numLines);
        const auto norm = s.outputGain * std::pow (std::max (0.002f, 1.0f - meanGain2), s.normExponent) * 2.2f;
        const auto preSamples = std::clamp (preMs * 0.001f * sr, 0.0f, static_cast<float> (preDelay[0].capacity() - 4));

        // Delay-line modulation is slow, so it is evaluated at control rate and interpolated per sample.
        float modStart[numLines], modStep[numLines];
        const auto invLen = 1.0f / static_cast<float> (len);
        for (int l = 0; l < numLines; ++l)
        {
            modPhase[l] += modInc[l] * static_cast<float> (len);
            modPhase[l] -= std::floor (modPhase[l]);
            const auto wander = p.flux > 0.0f ? 0.5f * p.flux * lineWander[l].advance (len) : 0.0f;
            const auto target = modDepth * fluxMod * (std::sin (modPhase[l] * twoPi) + wander);
            modStart[l] = lineMod[l];
            modStep[l] = (target - lineMod[l]) * invLen;
            lineMod[l] = target;
        }

        for (int i = start; i < start + len; ++i)
        {
            const auto amount = amountSm.next();
            const auto stereo = stereoSm.next();
            const auto fade = typeFade.next();
            const auto dryGain = std::min (1.0f, 2.0f * (1.0f - amount));
            const auto wetGain = std::pow (std::min (1.0f, 2.0f * amount), 1.2f) * fade;

            const auto xL = left[i];
            const auto xR = right[i];
            const auto mono = 0.5f * (xL + xR);
            const float in[2] { lerp (mono, xL, stereo), lerp (mono, xR, stereo) };
            float diffused[2];

            for (int c = 0; c < 2; ++c)
            {
                const auto band = inputLow[c].process (inputHigh[c].process (in[c]));
                preDelay[c].push (band);
                auto v = preDelay[c].readLinear (preSamples + 1.0f);

                if (s.diffusionGain > 0.0f)
                    for (auto& d : diffusers[c])
                        v = d.process (v);

                diffused[c] = v;
            }

            // Early reflections.
            early.push (0.5f * (diffused[0] + diffused[1]));
            auto erL = 0.0f, erR = 0.0f;
            if (s.earlyLevel > 0.0f)
            {
                for (int k = 0; k < numEarlyTaps; ++k)
                {
                    const auto e = early.read (earlyTap[k]);
                    erL += e * earlyGainL[k];
                    erR += e * earlyGainR[k];
                }
            }

            // Feedback delay network.
            float v[numLines];
            const auto step = static_cast<float> (i - start + 1);
            for (int l = 0; l < numLines; ++l)
            {
                const auto d = std::max (2.0f, lineLength[l] + modStart[l] + modStep[l] * step);
                auto o = lines[l].readLinear (d);
                o = lowCut[l].processHP (damping[l].processLP (o));

                if (currentType == spring)
                    for (auto& ap : dispersion[l])
                        o = ap.process (o);

                lineOut[l] = o;
                v[l] = o * lineGain[l];
            }

            if (currentType == resonator)
            {
                for (int l = 0; l < numLines; ++l)
                    noteSum[l % 12] += lineOut[l] * lineOut[l] * norm * norm;
                ++noteCount;
            }

            if (currentType != resonator)
                hadamard<numLines> (v);

            for (int l = 0; l < numLines; ++l)
            {
                const auto inject = (l % 2 == 0 ? diffused[0] : diffused[1]) * 0.35f;
                lines[l].push (v[l] + inject);
            }

            auto outL = 0.0f, outR = 0.0f;
            for (int l = 0; l < numLines; l += 2)
            {
                const auto sign = (l / 2) % 2 == 0 ? 1.0f : -1.0f;
                outL += lineOut[l] * sign;
                outR += lineOut[l + 1] * sign;
            }

            outL = outL * norm + erL * s.earlyLevel;
            outR = outR * norm + erR * s.earlyLevel;

            const auto monoOut = 0.70710678f * (outL + outR);
            outL = lerp (monoOut, outL, stereo);
            outR = lerp (monoOut, outR, stereo);

            // Display: wet level (before the Amount gain) in 10 ms windows.
            wetSum += outL * outL + outR * outR;
            if (++wetCount >= wetWindow)
            {
                if (telemetry != nullptr)
                    telemetry->spaceWet.push ({ 10.0f * std::log10 (wetSum / static_cast<float> (2 * wetCount) + 1.0e-12f) });
                wetCount = 0;
                wetSum = 0.0f;
            }

            left[i] = xL * dryGain + outL * wetGain;
            right[i] = xR * dryGain + outR * wetGain;
        }
    }

    for (int k = 0; k < 12; ++k)
        notes[k] = noteCount > 0 ? std::sqrt (noteSum[k] / static_cast<float> (noteCount)) : 0.0f;
}

void SpaceModule::publish (EngineTelemetry& t) const noexcept
{
    t.spaceDecaySeconds.store (lastRt, std::memory_order_relaxed);
    t.spacePreDelayMs.store (lastPreMs, std::memory_order_relaxed);
    for (size_t k = 0; k < 12; ++k)
        t.spaceNotes[k].store (notes[k], std::memory_order_relaxed);
}

} // namespace afterglow::dsp
