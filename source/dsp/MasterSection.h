#pragma once

#include "DspUtils.h"
#include "EngineParams.h"

namespace afterglow::dsp
{
/** Master section: input trim, cut filters (soft 12 dB/oct or hard 24 dB/oct), Tilt/Mid tone,
    stereo width, output trim and an optional soft-knee safety limiter. */
class MasterSection
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Effective values after Magnitude scaling. */
    struct Settings
    {
        float inGainDb = 0.0f;
        bool eqOn = true;
        float lowCutHz = 10.0f;
        bool lowCutHard = false;
        float highCutHz = 22000.0f;
        bool highCutHard = false;
        float tone = 0.0f;
        int toneMode = 0;
        float width = 1.0f;
        float outGainDb = 0.0f;
        bool limiter = false;
    };

    void setSettings (const Settings& s) noexcept;

    void processInput (float* left, float* right, int n) noexcept;
    void processEq (float* left, float* right, int n) noexcept;
    void processOutput (float* left, float* right, int n) noexcept;
    void processLimiter (float* left, float* right, int n) noexcept;

private:
    static constexpr int controlInterval = 32;

    void updateFilters (float lowHz, float highHz, float tone) noexcept;

    double fs = 44100.0;
    Settings settings;
    bool primed = false;

    Smoother inGainSm, outGainSm, widthSm, eqOnSm, lowSm, highSm, lowHardSm, highHardSm, toneSm, toneModeSm, limiterSm;

    // Low cut: a 2nd order (soft) and a 4th order (hard) chain run in parallel and are crossfaded.
    Svf lowSoft[2], lowHardA[2], lowHardB[2];
    Svf highSoft[2], highHardA[2], highHardB[2];
    Svf tiltLow[2], tiltHigh[2], midBell[2];
};

} // namespace afterglow::dsp
