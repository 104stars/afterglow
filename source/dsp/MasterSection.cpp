#include "MasterSection.h"

namespace afterglow::dsp
{
void MasterSection::prepare (double sampleRate, int)
{
    fs = sampleRate;
    inGainSm.prepare (sampleRate, 0.03f);
    outGainSm.prepare (sampleRate, 0.03f);
    widthSm.prepare (sampleRate, 0.05f);
    eqOnSm.prepare (sampleRate, 0.02f);
    lowSm.prepare (sampleRate, 0.06f);
    highSm.prepare (sampleRate, 0.06f);
    lowHardSm.prepare (sampleRate, 0.02f);
    highHardSm.prepare (sampleRate, 0.02f);
    toneSm.prepare (sampleRate, 0.05f);
    toneModeSm.prepare (sampleRate, 0.03f);
    limiterSm.prepare (sampleRate, 0.02f);
    reset();
}

void MasterSection::reset()
{
    for (int c = 0; c < 2; ++c)
    {
        lowSoft[c].reset();
        lowHardA[c].reset();
        lowHardB[c].reset();
        highSoft[c].reset();
        highHardA[c].reset();
        highHardB[c].reset();
        tiltLow[c].reset();
        tiltHigh[c].reset();
        midBell[c].reset();
    }

    primed = false;
}

void MasterSection::setSettings (const Settings& s) noexcept
{
    settings = s;
    inGainSm.setTarget (dbToGain (s.inGainDb));
    outGainSm.setTarget (dbToGain (s.outGainDb));
    widthSm.setTarget (s.width);
    eqOnSm.setTarget (s.eqOn ? 1.0f : 0.0f);
    lowSm.setTarget (std::log (std::max (s.lowCutHz, 1.0f)));
    highSm.setTarget (std::log (std::max (s.highCutHz, 1.0f)));
    lowHardSm.setTarget (s.lowCutHard ? 1.0f : 0.0f);
    highHardSm.setTarget (s.highCutHard ? 1.0f : 0.0f);
    toneSm.setTarget (s.tone);
    toneModeSm.setTarget (s.toneMode == 1 ? 1.0f : 0.0f);
    limiterSm.setTarget (s.limiter ? 1.0f : 0.0f);

    if (! primed)
    {
        for (auto* sm : { &inGainSm, &outGainSm, &widthSm, &eqOnSm, &lowSm, &highSm, &lowHardSm, &highHardSm, &toneSm, &toneModeSm, &limiterSm })
            sm->snapTo (sm->getTarget());
        primed = true;
    }
}

void MasterSection::processInput (float* left, float* right, int n) noexcept
{
    if (inGainSm.isSettled() && std::abs (inGainSm.getCurrent() - 1.0f) < 1.0e-7f)
        return;

    for (int i = 0; i < n; ++i)
    {
        const auto g = inGainSm.next();
        left[i] *= g;
        right[i] *= g;
    }
}

void MasterSection::updateFilters (float lowHz, float highHz, float tone) noexcept
{
    const auto nyquistSafe = 0.45f * static_cast<float> (fs);
    lowHz = std::min (lowHz, nyquistSafe);
    highHz = std::min (highHz, nyquistSafe);

    for (int c = 0; c < 2; ++c)
    {
        lowSoft[c].setup (Svf::Type::highPass, lowHz, 0.7071f, 0.0f, fs);
        lowHardA[c].setup (Svf::Type::highPass, lowHz, 0.5412f, 0.0f, fs);
        lowHardB[c].setup (Svf::Type::highPass, lowHz, 1.3066f, 0.0f, fs);
        highSoft[c].setup (Svf::Type::lowPass, highHz, 0.7071f, 0.0f, fs);
        highHardA[c].setup (Svf::Type::lowPass, highHz, 0.5412f, 0.0f, fs);
        highHardB[c].setup (Svf::Type::lowPass, highHz, 1.3066f, 0.0f, fs);

        // Tilt: +-6 dB shelves around the midrange. Mid: a broad +-9 dB bell at 1 kHz.
        tiltLow[c].setup (Svf::Type::lowShelf, 300.0f, 0.5f, -6.0f * tone, fs);
        tiltHigh[c].setup (Svf::Type::highShelf, 3000.0f, 0.5f, 6.0f * tone, fs);
        midBell[c].setup (Svf::Type::bell, 1000.0f, 0.55f, 9.0f * tone, fs);
    }
}

void MasterSection::processEq (float* left, float* right, int n) noexcept
{
    const auto eqIdle = eqOnSm.isSettled() && eqOnSm.getCurrent() <= 0.0f;
    if (eqIdle)
        return;

    float* io[2] { left, right };

    for (int start = 0; start < n; start += controlInterval)
    {
        const auto len = std::min (controlInterval, n - start);
        const auto lowHz = std::exp (lowSm.skip (len));
        const auto highHz = std::exp (highSm.skip (len));
        const auto tone = toneSm.skip (len);
        updateFilters (lowHz, highHz, tone);

        // The extreme positions of the cut filters mean "off": fade the filters out near them.
        const auto lowActive = clamp01 ((lowHz - 10.0f) / 4.0f);
        const auto highActive = clamp01 ((22000.0f - highHz) / 4000.0f);
        const auto toneActive = std::abs (tone) > 1.0e-4f || ! toneSm.isSettled();

        for (int i = start; i < start + len; ++i)
        {
            const auto eqOn = eqOnSm.next();
            const auto lowHard = lowHardSm.next();
            const auto highHard = highHardSm.next();
            const auto mode = toneModeSm.next();

            for (int c = 0; c < 2; ++c)
            {
                const auto x = io[c][i];
                auto y = x;

                if (lowActive > 0.0f)
                {
                    const auto soft = lowSoft[c].process (y);
                    const auto hard = lowHardB[c].process (lowHardA[c].process (y));
                    y = lerp (y, lerp (soft, hard, lowHard), lowActive);
                }

                if (highActive > 0.0f)
                {
                    const auto soft = highSoft[c].process (y);
                    const auto hard = highHardB[c].process (highHardA[c].process (y));
                    y = lerp (y, lerp (soft, hard, highHard), highActive);
                }

                if (toneActive)
                {
                    const auto tilted = tiltHigh[c].process (tiltLow[c].process (y));
                    const auto mid = midBell[c].process (y);
                    y = lerp (tilted, mid, mode);
                }

                io[c][i] = lerp (x, y, eqOn);
            }
        }
    }
}

void MasterSection::processOutput (float* left, float* right, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const auto w = widthSm.next();
        const auto g = outGainSm.next();
        const auto mid = 0.5f * (left[i] + right[i]);
        const auto side = 0.5f * (left[i] - right[i]) * w;
        left[i] = (mid + side) * g;
        right[i] = (mid - side) * g;
    }
}

void MasterSection::processLimiter (float* left, float* right, int n) noexcept
{
    if (limiterSm.isSettled() && limiterSm.getCurrent() <= 0.0f)
        return;

    // Transparent below -3 dBFS, then a smooth knee that never exceeds -0.3 dBFS.
    constexpr float threshold = 0.708f;
    constexpr float ceiling = 0.966f;

    auto softLimit = [] (float x) noexcept
    {
        const auto a = std::abs (x);
        if (a <= threshold)
            return x;
        const auto range = ceiling - threshold;
        return std::copysign (threshold + range * std::tanh ((a - threshold) / range), x);
    };

    for (int i = 0; i < n; ++i)
    {
        const auto amount = limiterSm.next();
        left[i] = lerp (left[i], softLimit (left[i]), amount);
        right[i] = lerp (right[i], softLimit (right[i]), amount);
    }
}

} // namespace afterglow::dsp
