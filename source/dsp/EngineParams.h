#pragma once

#include <atomic>
#include <array>

namespace afterglow::dsp
{
/** Plain parameter snapshot handed to the engine once per block. Percent parameters are normalised to 0..1,
    bipolar parameters to -1..1, frequencies are in Hz and gains in dB. */
struct NoiseParams
{
    bool on = true;
    float amount = 0.0f;
    int type = 0;
    float tone = 0.0f;
    float follow = 0.0f;
    float duck = 0.0f;
    bool post = false;
    float flux = 0.0f;
};

struct WobbleParams
{
    bool on = true;
    float amount = 0.0f;
    float balance = 0.5f;
    float wowRate = 0.5f;
    float flutterRate = 10.0f;
    double syncBeats = 0.0; // > 0 when tempo synced: length of one wow cycle in quarter notes (rate already converted)
    bool stereo = false;
    float mix = 1.0f;
    float flux = 0.0f;
};

struct DistortParams
{
    bool on = true;
    float amount = 0.0f;
    int type = 0;
    float focusLow = 20.0f;
    float focusHigh = 20000.0f;
    float tone = 0.0f;
    float mix = 1.0f;
    float flux = 0.0f;
};

struct DigitalParams
{
    bool on = true;
    float amount = 0.0f;
    float balance = 0.5f;
    float smooth = 0.0f;
    float focusLow = 20.0f;
    float focusHigh = 20000.0f;
    bool cut = false;
    bool compand = false;
    float mix = 1.0f;
    float flux = 0.0f;
};

struct SpaceParams
{
    bool on = true;
    float amount = 0.0f;
    int type = 1;
    float decay = 0.35f;
    float preDelayMs = 8.0f;
    float focusLow = 20.0f;
    float focusHigh = 20000.0f;
    bool stereo = true;
    float flux = 0.0f;
};

struct MagneticParams
{
    bool on = true;
    float amount = 0.0f;
    float balance = 0.5f;
    float rate = 9.0f;
    double syncBeats = 0.0; // > 0 when tempo synced (rate already converted to Hz)
    float dropouts = 0.0f;
    bool stereo = false;
    float flux = 0.0f;
};

struct MasterParams
{
    float magnitude = 1.0f;
    float inGainDb = 0.0f;
    bool eqOn = true;
    float lowCut = 10.0f;
    bool lowCutHard = false;
    float highCut = 22000.0f;
    bool highCutHard = false;
    float tone = 0.0f;
    int toneMode = 0;
    float width = 1.0f;
    float outGainDb = 0.0f;
    float mix = 1.0f;
    bool limiter = false;
    int quality = 2;
};

struct EngineParams
{
    NoiseParams noise;
    WobbleParams wobble;
    DistortParams distort;
    DigitalParams digital;
    SpaceParams space;
    MagneticParams magnetic;
    MasterParams master;
};

struct TransportInfo
{
    bool isPlaying = true;
    double bpm = 120.0;
    bool hasPpq = false;
    double ppqPosition = 0.0;
};

/** Lock-free values published by the audio thread for meters and the animated module displays. */
struct EngineTelemetry
{
    std::atomic<float> inputPeak[2] {}, inputRms[2] {};
    std::atomic<float> outputPeak[2] {}, outputRms[2] {};
    std::atomic<float> noiseLevel { 0.0f };
    std::atomic<float> wobbleMod { 0.0f };
    std::atomic<float> distortDrive { 0.0f };
    std::atomic<float> digitalRate { 44100.0f };
    std::atomic<float> digitalBits { 24.0f };
    std::atomic<float> spaceEnergy { 0.0f };
    std::atomic<float> magneticGain { 1.0f };
    std::atomic<float> magneticDropout { 0.0f };

    static constexpr int scopeSize = 512;
    std::array<std::atomic<float>, scopeSize> noiseScope {};
    std::atomic<int> noiseScopeWrite { 0 };
};

} // namespace afterglow::dsp
