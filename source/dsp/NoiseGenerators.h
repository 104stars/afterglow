#pragma once

#include "DspUtils.h"

namespace afterglow::dsp
{
/** Random impulsive events (vinyl crackle, static, pops). Each event is a short, decaying noise burst. */
class CrackleGenerator
{
public:
    struct Settings
    {
        float eventsPerSecond = 10.0f;
        float sizePower = 3.0f;     // higher = more tiny ticks, fewer big ones
        float minDecayMs = 0.05f;
        float maxDecayMs = 0.6f;
        float popsPerSecond = 0.2f;
        float popDecayMs = 2.5f;
        float highPassHz = 700.0f;
        float lowPassHz = 12000.0f;
        float stereoSpread = 0.8f;  // 0 = mono, 1 = fully independent placement
        float popLevel = 1.0f;
    };

    void prepare (double sampleRate, uint32_t seed);
    void setSettings (const Settings& s);
    void reset();

    /** Renders one stereo sample. @param densityScale multiplies the event rate (used by Flux). */
    void process (float& outL, float& outR, float densityScale) noexcept;

private:
    struct Channel
    {
        float env = 0.0f, decay = 0.0f, impulse = 0.0f;
        float popEnv = 0.0f, popImpulse = 0.0f;
        OnePole hp, lp, popLp, thump;
    };

    void trigger (bool pop) noexcept;

    double fs = 44100.0;
    Settings settings;
    FastRandom rng;
    Channel ch[2];
    float popDecay = 0.0f;
};

//======================================================================================================================
/** Procedural noise sources for the Noise module. Every type is synthesised in real time (no samples). */
class NoiseSynth
{
public:
    enum Type
    {
        vinyl = 0, shellac, tape, cassette, vhs, hum50, hum60, buzz,
        fuzz, room, radio, transmission, bit8, white, pink, brown, numTypes
    };

    void prepare (double sampleRate, uint32_t seed);
    void reset();

    /** Renders n samples of the given type (overwrites L and R). @param activity flux-driven modifier in -1..1. */
    void render (int type, float* left, float* right, int n, float activity) noexcept;

    /** Loudness normalisation applied to each type so that all types sit at a comparable level. */
    static float typeGain (int type) noexcept;

private:
    struct Pink
    {
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        float process (float white) noexcept
        {
            b0 = 0.99886f * b0 + white * 0.0555179f;
            b1 = 0.99332f * b1 + white * 0.0750759f;
            b2 = 0.96900f * b2 + white * 0.1538520f;
            b3 = 0.86650f * b3 + white * 0.3104856f;
            b4 = 0.55000f * b4 + white * 0.5329522f;
            b5 = -0.7616f * b5 - white * 0.0168980f;
            const auto out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
            b6 = white * 0.115926f;
            return out * 0.11f;
        }
    };

    void renderVinyl (float* l, float* r, int n, float activity, bool shellacMode) noexcept;
    void renderTape (float* l, float* r, int n, float activity, bool cassetteMode) noexcept;
    void renderVhs (float* l, float* r, int n, float activity) noexcept;
    void renderHum (float* l, float* r, int n, float activity, float mainsHz) noexcept;
    void renderBuzz (float* l, float* r, int n, float activity) noexcept;
    void renderFuzz (float* l, float* r, int n, float activity) noexcept;
    void renderRoom (float* l, float* r, int n, float activity) noexcept;
    void renderRadio (float* l, float* r, int n, float activity) noexcept;
    void renderTransmission (float* l, float* r, int n, float activity) noexcept;
    void renderBit8 (float* l, float* r, int n, float activity) noexcept;
    void renderColoured (float* l, float* r, int n, int type) noexcept;

    void configureFilters();

    double fs = 44100.0;
    float invFs = 1.0f / 44100.0f;
    FastRandom rng;
    SmoothRandom wander[4];

    CrackleGenerator vinylCrackle, shellacCrackle, radioCrackle;

    // Shared filter banks (re-used by several types; reset when the type changes).
    Svf bandA[2], bandB[2], bandC[2], bandD[2];
    OnePole lowA[2], lowB[2];
    Pink pinkFilters[2];
    float brownState[2] {};
    DcBlocker dc[2];

    // Oscillator / event state.
    double phaseA = 0.0, phaseB = 0.0, phaseC = 0.0;
    float burstEnv = 0.0f, burstDecay = 0.0f;
    float gateEnv = 0.0f, gateTarget = 0.0f;
    int gateCounter = 0;
    float beepEnv = 0.0f;
    int beepCounter = 0;
    double beepPhase = 0.0;
    float heldL = 0.0f, heldR = 0.0f, clockPhase = 0.0f;
    uint32_t lfsr = 1u;
    int lastType = -1;
};

} // namespace afterglow::dsp
