#pragma once

#include "DspUtils.h"
#include "EngineParams.h"

namespace afterglow::dsp
{
/** MAGNETIC: the level and high-frequency artefacts of worn magnetic tape.
    Wear is slow, irregular level and treble loss; Flutter is fast amplitude scrape; Dropouts are sudden,
    random losses of signal (mostly treble first, like oxide shedding). */
class MagneticModule
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void process (float* left, float* right, int n, const MagneticParams& params, const TransportInfo& transport) noexcept;
    void setTelemetry (EngineTelemetry* t) noexcept { telemetry = t; }

    /** Level change in dB at one frequency for a given gain and treble loss (0..1): the module's own
        low-pass blend, so the display shows exactly what the audio gets. */
    static float responseDb (float gainDb, float loss, float hz) noexcept;

private:
    static constexpr int controlInterval = 8;

    struct Dropout
    {
        float env = 0.0f;       // 0..1 current dropout depth
        float target = 0.0f;
        int holdSamples = 0;
        float attack = 0.0f, release = 0.0f;
        float depthDb = 0.0f;
    };

    struct ChannelState
    {
        SmoothRandom wearSlow, wearFast, scrape;
        Dropout dropout;
        Svf lowPass;
        float gain = 1.0f, prevGain = 1.0f;
        float lossAmount = 0.0f, prevLoss = 0.0f;
    };

    void updateDropout (Dropout& d, float eventsPerSecond, float depthScale, float flux, int len) noexcept;

    double fs = 44100.0;
    ChannelState channels[2];
    double flutterPhase = 0.0;
    FastRandom rng;
    FluxSource rateFlux, depthFlux;
    Smoother amountSm, balanceSm, rateSm, dropoutSm, stereoSm;
    bool primed = false;

    // Display history: lowest gain and highest loss per channel in 1/64 s windows.
    void collectTape (const float* gainsDb, const float* losses, int len) noexcept;
    EngineTelemetry* telemetry = nullptr;
    int tapeWindow = 750, tapeSamples = 0;
    float tapeGainDb[2] {}, tapeLoss[2] {};
};

} // namespace afterglow::dsp
