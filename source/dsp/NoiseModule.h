#pragma once

#include "DspUtils.h"
#include "EngineParams.h"
#include "NoiseGenerators.h"

namespace afterglow::dsp
{
/** NOISE: adds procedurally generated noise beds. The incoming audio itself is never altered.
    Follow makes the noise track the input level, Duck pushes it down under loud passages. */
class NoiseModule
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Analyses the (pre-chain) input so Follow and Duck can react to it. Call before render(). */
    void analyseInput (const float* left, const float* right, int n) noexcept;

    /** Renders the noise for this block into the internal buffers. */
    void render (int n, const NoiseParams& params, const TransportInfo& transport) noexcept;

    /** Adds the rendered noise to the signal. */
    void addTo (float* left, float* right, int n) const noexcept;

    void publish (EngineTelemetry& telemetry) const noexcept;

    const float* getRenderedLeft() const noexcept { return bufL.data(); }

private:
    static constexpr int controlInterval = 16;

    double fs = 44100.0;
    NoiseSynth synth;
    TiltFilter tilt[2];
    FluxSource levelFlux, toneFlux, activityFlux;
    EnvelopeFollower followEnv, duckEnv;
    Smoother amountSm, toneSm, followSm, duckSm, transportSm, onSm;
    LinearRamp typeFade;
    int currentType = 0;
    int pendingType = -1;
    bool primed = false;
    bool wasPlaying = true;

    std::vector<float> envFollow, envDuck, bufL, bufR;
    float lastGain = 0.0f;
    float lastBlockRms = 0.0f;
};

} // namespace afterglow::dsp
