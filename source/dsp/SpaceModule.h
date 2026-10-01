#pragma once

#include "DspUtils.h"
#include "EngineParams.h"

namespace afterglow::dsp
{
/** SPACE: a 16-line feedback delay network reverb with six characters.
    Ambience/Room add early reflections, Plate is dense and bright, Hall is long and modulated,
    Spring uses in-loop dispersion for the classic chirp, and Resonator is a bank of 12 combs tuned to
    the chromatic scale. The Focus band sets the in-loop damping (resonance) of the tail. */
class SpaceModule
{
public:
    enum Type { ambience = 0, room, plate, hall, spring, resonator, numTypes };

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void process (float* left, float* right, int n, const SpaceParams& params) noexcept;
    void publish (EngineTelemetry& telemetry) const noexcept;
    void setTelemetry (EngineTelemetry* t) noexcept { telemetry = t; }

    /** Decay time (RT60, seconds) at the given Decay setting (0..1), without Flux. */
    static float decaySeconds (int type, float decay) noexcept;

    /** Range of the network's delay lines in ms: the time over which the tail builds up after the pre-delay. */
    static void buildUpMs (int type, float& minMs, float& maxMs) noexcept;

    /** Decay time at one frequency, after the in-loop damping that the type and the Focus band apply. */
    static float decaySecondsAt (int type, float rt, float focusLowHz, float focusHighHz, float hz) noexcept;

private:
    static constexpr int numLines = 16;
    static constexpr int numDiffusers = 4;
    static constexpr int dispersionStages = 6;
    static constexpr int numEarlyTaps = 8;
    static constexpr int controlInterval = 16;

    struct TypeSpec
    {
        float minDelayMs, maxDelayMs;
        float minRt, maxRt;
        float diffusionScaleMs, diffusionGain;
        float dampHz, lowHz;
        float modDepthMs, modRateHz;
        float earlyLevel;
        float outputGain;
        float normExponent; // how strongly longer decays are compensated for their energy build-up
    };

    struct Allpass
    {
        DelayBuffer buffer;
        int length = 1;
        float gain = 0.5f;

        float process (float x) noexcept
        {
            const auto delayed = buffer.read (length);
            const auto v = x + gain * delayed;
            buffer.push (v);
            return delayed - gain * v;
        }
    };

    struct FirstOrderAllpass
    {
        float a = 0.0f, x1 = 0.0f, y1 = 0.0f;
        float process (float x) noexcept
        {
            const auto y = a * x + x1 - a * y1;
            x1 = x;
            y1 = y;
            return y;
        }
    };

    static const TypeSpec& spec (int type) noexcept;
    void configureType (int type);

    double fs = 44100.0;
    int currentType = -1;

    DelayBuffer lines[numLines];
    float lineLength[numLines] {};
    float modPhase[numLines] {}, modInc[numLines] {};
    OnePole damping[numLines], lowCut[numLines];
    FirstOrderAllpass dispersion[numLines][dispersionStages];
    float lineOut[numLines] {};
    float lineMod[numLines] {};

    Allpass diffusers[2][numDiffusers];
    DelayBuffer preDelay[2];
    DelayBuffer early;
    int earlyTap[numEarlyTaps] {};
    float earlyGainL[numEarlyTaps] {}, earlyGainR[numEarlyTaps] {};
    Svf inputHigh[2], inputLow[2];

    FluxSource modFlux, decayFlux, predelayFlux;
    SmoothRandom lineWander[numLines];
    Smoother amountSm, decaySm, preDelaySm, lowSm, highSm, stereoSm;
    LinearRamp typeFade;
    int pendingType = -1;
    bool primed = false;

    // Display: wet level history, input onsets, the values in use and the resonator's level per note.
    EngineTelemetry* telemetry = nullptr;
    int wetWindow = 441, wetCount = 0;
    float wetSum = 0.0f;
    float onsetFast = 0.0f, onsetSlow = 0.0f;
    int samplesSinceOnset = 0;
    float lastRt = 1.0f, lastPreMs = 0.0f;
    float noteSum[12] {};
    int noteCount = 0;
    float notes[12] {}, noteEnergy[12] {};
};

} // namespace afterglow::dsp
