#pragma once

#include "DspUtils.h"
#include "EngineParams.h"

namespace afterglow::dsp
{
/** WOBBLE: pitch modulation like a badly aligned tape deck or a warped record.
    Depth is specified as pitch deviation, so the perceived amount stays constant across rates.
    A windowed-sinc interpolated delay keeps the top end clean even at heavy settings. */
class WobbleModule
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void process (float* left, float* right, int n, const WobbleParams& params, const TransportInfo& transport) noexcept;
    void setTelemetry (EngineTelemetry* t) noexcept { telemetry = t; }

private:
    static constexpr int controlInterval = 16;
    static constexpr float maxWowDeviation = 0.03f;      // 3 % pitch swing at full depth (a badly warped record)
    static constexpr float maxFlutterDeviation = 0.007f; // 0.7 % fast flutter at full depth
    static constexpr float maxWowSeconds = 0.12f;        // cap for very slow, very deep settings

    double fs = 44100.0;
    DelayBuffer delay[2];
    double wowPhase = 0.0, flutterPhase = 0.0, flutterPhase2 = 0.0;
    SmoothRandom wowDrift[2], flutterNoise[2];
    FluxSource rateFlux, depthFlux;
    Smoother depthSm, balanceSm, wowRateSm, flutterRateSm, mixSm, stereoSm, centreSm;
    bool primed = false;

    // Display history: pitch deviation per control block, gathered into 1/64 s windows.
    void pushPitch (float centsL, float centsR, int len) noexcept;
    EngineTelemetry* telemetry = nullptr;
    float prevDelay[2] { -1.0f, -1.0f };
    float pitchSum[2] {}, pitchMin[2] {}, pitchMax[2] {};
    int pitchBlocks = 0, pitchSamples = 0, pitchWindow = 750;
    float prevCentre = 0.0f, prevAw = 0.0f, prevAf = 0.0f, prevAr = 0.0f, prevEngage = 0.0f;
};

} // namespace afterglow::dsp
