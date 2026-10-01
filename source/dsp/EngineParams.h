#pragma once

#include <array>
#include <atomic>
#include <cstdint>

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

/** Single-producer, single-consumer history of small records, written by the audio thread and read by the UI.
    Each record holds Fields floats. The reader takes the newest entries from the write counter and should read
    at most Size / 2 of them, so it never touches the slot being overwritten. */
template <int Size, int Fields>
struct TelemetryRing
{
    static constexpr int size = Size;
    static constexpr int fields = Fields;

    std::array<std::atomic<float>, static_cast<size_t> (Size * Fields)> data {};
    std::atomic<uint32_t> written { 0 };

    void push (const std::array<float, static_cast<size_t> (Fields)>& values) noexcept
    {
        const auto w = written.load (std::memory_order_relaxed);
        const auto base = static_cast<size_t> (w % static_cast<uint32_t> (Size)) * static_cast<size_t> (Fields);
        for (size_t f = 0; f < static_cast<size_t> (Fields); ++f)
            data[base + f].store (values[f], std::memory_order_relaxed);
        written.store (w + 1, std::memory_order_release);
    }

    /** Value of one field of entry number 'entry' (a running count, as returned by 'written'). */
    float get (uint32_t entry, int field) const noexcept
    {
        return data[static_cast<size_t> (entry % static_cast<uint32_t> (Size)) * static_cast<size_t> (Fields) + static_cast<size_t> (field)]
            .load (std::memory_order_relaxed);
    }
};

/** Lock-free values published by the audio thread for the meters and the module displays. */
struct EngineTelemetry
{
    std::atomic<float> sampleRate { 44100.0f };
    std::atomic<float> inputPeak[2] {}, inputRms[2] {};
    std::atomic<float> outputPeak[2] {}, outputRms[2] {};

    /** Noise output (mid), per 25 ms window: min, max, mean, rms. */
    TelemetryRing<256, 4> noiseEnvelope;

    /** Wobble pitch deviation in cents, per 1/64 s window: mean, min, max for L, then for R. */
    TelemetryRing<512, 6> wobblePitch;

    /** Distort shaper input (after drive) and output pairs, left channel, about 12 kHz. */
    TelemetryRing<1024, 2> distortTransfer;
    std::atomic<float> distortBias { 0.0f };

    std::atomic<float> digitalRate { 44100.0f };
    std::atomic<float> digitalBits { 24.0f };
    std::atomic<float> digitalJitter { 0.0f };

    /** Space wet level in dB per 10 ms, the ring entry at the latest input onset, the decay time and pre-delay
        actually in use (including Flux), and the resonator's level per pitch class. */
    TelemetryRing<512, 1> spaceWet;
    std::atomic<uint32_t> spaceOnsetEntry { 0 }, spaceOnsetCount { 0 };
    std::atomic<float> spaceDecaySeconds { 1.0f }, spacePreDelayMs { 0.0f };
    std::array<std::atomic<float>, 12> spaceNotes {};

    /** Magnetic tape per 1/64 s window: lowest gain (dB) and highest treble loss (0..1) for L, then for R. */
    TelemetryRing<256, 4> magneticTape;
};

} // namespace afterglow::dsp
