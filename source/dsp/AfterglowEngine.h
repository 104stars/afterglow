#pragma once

#include "DigitalModule.h"
#include "DistortModule.h"
#include "EngineParams.h"
#include "MagneticModule.h"
#include "MasterSection.h"
#include "NoiseModule.h"
#include "SpaceModule.h"
#include "WobbleModule.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace afterglow::dsp
{
/** The complete signal chain:
    Input -> Noise (pre) -> Wobble -> Distort -> Digital -> Space -> Magnetic -> EQ -> Noise (post) -> Width/Output
          -> global Mix against the latency-aligned dry signal -> optional safety limiter. */
class AfterglowEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int oversamplingOrder);
    void reset();

    /** Switches oversampling for the Distort module. Returns the new total latency. Audio-thread safe. */
    int setOversamplingOrder (int order) noexcept;
    int getOversamplingOrder() const noexcept { return distort.getOversamplingOrder(); }
    int getLatencySamples() const noexcept { return distort.getLatencySamples(); }

    /** Processes up to maxBlockSize samples in place. Mono buffers are processed through the left channel. */
    void process (float* left, float* right, int n, const EngineParams& params, const TransportInfo& transport) noexcept;

    EngineTelemetry& getTelemetry() noexcept { return telemetry; }

private:
    void processChunk (float* left, float* right, int n, const EngineParams& params, const TransportInfo& transport) noexcept;
    void publishMeters (const float* left, const float* right, int n, bool input) noexcept;

    double fs = 44100.0;
    int maxBlock = 512;

    NoiseModule noise;
    WobbleModule wobble;
    DistortModule distort;
    DigitalModule digital;
    SpaceModule space;
    MagneticModule magnetic;
    MasterSection master;

    DelayBuffer dryDelay[2];
    std::vector<float> dryL, dryR;
    Smoother mixSm;
    bool mixPrimed = false;
    int scopeDecimator = 0;

    EngineTelemetry telemetry;
};

} // namespace afterglow::dsp
