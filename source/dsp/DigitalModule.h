#pragma once

#include "DspUtils.h"
#include "EngineParams.h"

namespace afterglow::dsp
{
/** DIGITAL: early sampler grit. Sample-rate reduction (with clock jitter from Flux) and bit-depth reduction,
    optional mu-law companding like vintage 8-bit samplers, Smooth anti-alias/reconstruction filters,
    and a focus band with an optional CUT mode that discards everything outside the band. */
class DigitalModule
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void process (float* left, float* right, int n, const DigitalParams& params) noexcept;
    void publish (EngineTelemetry& telemetry) const noexcept;

private:
    static constexpr int controlInterval = 16;

    float crush (int channel, float x, float bitsAmount, float levels, bool compand) noexcept;
    void updateFilters (float lowHz, float highHz, float smoothCutoff) noexcept;

    double fs = 44100.0;
    Svf focusHigh[2], focusLow[2];
    Svf preFilter[2], postFilterA[2], postFilterB[2];
    float held[2] {}, holdPhase = 0.0f;
    FastRandom rng;
    FluxSource rateFlux, bitsFlux, jitterFlux;
    Smoother amountSm, balanceSm, smoothSm, lowSm, highSm, cutSm, mixSm, compandSm;
    bool primed = false;
    float currentRate = 44100.0f, currentBits = 24.0f;
    float prevEngage = 0.0f;
};

} // namespace afterglow::dsp
