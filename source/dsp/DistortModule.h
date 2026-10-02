#pragma once

#include "DspUtils.h"
#include "EngineParams.h"
#include <juce_dsp/juce_dsp.h>
#include <memory>

namespace afterglow::dsp
{
/** DISTORT: eight saturation characters, processed with oversampling inside a selectable focus band.
    Output level is matched to the input at a reference level, so drive changes colour rather than loudness. */
class DistortModule
{
public:
    enum Type { tube = 0, transformer, speaker, tape, fuzz, clip, fold, rectify, numTypes };

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Selects the oversampling order (0 = off, 1 = 2x, 2 = 4x, 3 = 8x). Safe to call from the audio thread. */
    void setOversamplingOrder (int order) noexcept;
    int getOversamplingOrder() const noexcept { return currentOrder; }

    /** Latency in samples introduced by the current oversampling filters (always an integer). */
    int getLatencySamples() const noexcept { return latency; }
    static int latencyForOrder (int order, double sampleRate, int maxBlockSize);

    void process (float* left, float* right, int n, const DistortParams& params) noexcept;
    void publish (EngineTelemetry& telemetry) const noexcept;
    void setTelemetry (EngineTelemetry* t) noexcept { telemetry = t; }

    /** The memoryless part of each type's transfer curve (what the display draws), and the type's resting bias. */
    static float staticCurve (int type, float x, float bias) noexcept;
    static float baseBias (int type) noexcept;

    /** Transformer only: the transfer for low frequencies, where the iron saturates first. */
    static float transformerBassCurve (float x, float bias) noexcept;

private:
    struct ShaperState
    {
        EnvelopeFollower sag, gate;
        OnePole transformerLow, speakerHighPass, fuzzLowPass;
        Svf speakerBell, speakerLowPass, rattleBand, tapePre, tapeDe;
        FastRandom rng;
        float tapIn = 0.0f, tapOut = 0.0f; // input and output of the non-linear core, for the display
    };

    static constexpr int controlInterval = 16;
    static float maxDriveDb (int type) noexcept;
    float shape (int type, float x, ShaperState& s, float bias) noexcept;
    void publishTransfer (int step) noexcept;
    float computeMakeup (int type, float drive, float bias) const noexcept;
    void configureShapers (double oversampledRate);
    void updateFocus (float lowHz, float highHz) noexcept;

    double fs = 44100.0;
    int maxBlock = 512;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversamplers[4];
    int currentOrder = 0;
    int latency = 0;

    juce::AudioBuffer<float> bandBuffer;
    DelayBuffer dryDelay[2], bandDelay[2];
    Svf focusHigh[2], focusLow[2];
    TiltFilter tilt[2];
    DcBlocker dc[2];
    ShaperState shapers[2];

    Smoother driveSm, mixSm, toneSm, lowSm, highSm, makeupSm, engageSm;
    FluxSource driveFlux, biasFlux, focusFlux;
    LinearRamp typeFade;
    int currentType = 0, pendingType = -1;
    bool primed = false;
    float lastDrive = 1.0f;
    float lastBias = 0.0f;

    // Display history: shaper input (after drive) and output pairs.
    EngineTelemetry* telemetry = nullptr;
    int transferStep = 4, transferCounter = 0;
};

} // namespace afterglow::dsp
