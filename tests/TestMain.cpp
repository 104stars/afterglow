// Afterglow test runner: null tests, latency, stability fuzzing, presets, calibration and a CPU benchmark.
// Exit code is non-zero if any check fails.

#include "PluginProcessor.h"
#include "Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <chrono>
#include <cstdio>
#include <random>

using namespace afterglow;

namespace
{
int failures = 0;
int checks = 0;

void check (bool condition, const juce::String& what)
{
    ++checks;
    if (! condition)
    {
        ++failures;
        std::printf ("  FAIL: %s\n", what.toRawUTF8());
    }
}

void section (const char* name) { std::printf ("\n== %s\n", name); }

void setParam (AfterglowProcessor& proc, const juce::String& id, float value)
{
    auto* param = proc.getState().getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

void setAllModules (AfterglowProcessor& proc, bool on)
{
    for (auto id : { ParamIDs::noiseOn, ParamIDs::wobbleOn, ParamIDs::distortOn, ParamIDs::digitalOn, ParamIDs::spaceOn, ParamIDs::magneticOn })
        setParam (proc, id, on ? 1.0f : 0.0f);
}

/** Deterministic music-like test signal: decaying noise hits, a bass line and a chord. */
juce::AudioBuffer<float> makeSignal (double sampleRate, double seconds, uint32_t seed = 1)
{
    const auto n = static_cast<int> (sampleRate * seconds);
    juce::AudioBuffer<float> buffer (2, n);
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> uni (-1.0f, 1.0f);
    auto env = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const auto t = static_cast<double> (i) / sampleRate;
        if (i % static_cast<int> (sampleRate * 0.25) == 0)
            env = 1.0f;
        env *= 0.9995f;

        const auto hit = env * uni (rng) * 0.35f;
        const auto bass = 0.25f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * t));
        const auto chord = 0.08f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * t)
                                                     + std::sin (2.0 * juce::MathConstants<double>::pi * 554.37 * t)
                                                     + std::sin (2.0 * juce::MathConstants<double>::pi * 659.25 * t));
        buffer.setSample (0, i, hit + bass + chord);
        buffer.setSample (1, i, hit * 0.8f + bass + chord * 0.9f);
    }

    return buffer;
}

/** Runs the whole buffer through the processor in blocks (optionally random block sizes). */
void runProcessor (AfterglowProcessor& proc, juce::AudioBuffer<float>& buffer, int maxBlock, bool randomBlocks, uint32_t seed = 7)
{
    std::mt19937 rng (seed);
    std::uniform_int_distribution<int> sizeDist (1, maxBlock);
    juce::MidiBuffer midi;
    int pos = 0;

    while (pos < buffer.getNumSamples())
    {
        const auto len = std::min (randomBlocks ? sizeDist (rng) : maxBlock, buffer.getNumSamples() - pos);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), pos, len);
        proc.processBlock (view, midi);
        pos += len;
    }
}

struct Stats
{
    float peak = 0.0f;
    double rms = 0.0;
    bool finite = true;
};

Stats analyse (const juce::AudioBuffer<float>& b, int startSample = 0)
{
    Stats s;
    double sum = 0.0;
    int count = 0;

    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        for (int i = startSample; i < b.getNumSamples(); ++i)
        {
            const auto v = b.getSample (c, i);
            if (! std::isfinite (v))
                s.finite = false;
            s.peak = std::max (s.peak, std::abs (v));
            sum += static_cast<double> (v) * v;
            ++count;
        }
    }

    s.rms = count > 0 ? std::sqrt (sum / count) : 0.0;
    return s;
}

double toDb (double v) { return 20.0 * std::log10 (std::max (v, 1.0e-12)); }

float maxDifferenceDelayed (const juce::AudioBuffer<float>& input, const juce::AudioBuffer<float>& output, int latency, int skip = 0)
{
    auto maxDiff = 0.0f;
    for (int c = 0; c < input.getNumChannels(); ++c)
        for (int i = std::max (skip, latency); i < input.getNumSamples(); ++i)
            maxDiff = std::max (maxDiff, std::abs (output.getSample (c, i) - input.getSample (c, i - latency)));
    return maxDiff;
}

std::unique_ptr<AfterglowProcessor> makeProcessor (double sampleRate, int block, int channels = 2)
{
    auto proc = std::make_unique<AfterglowProcessor>();
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (set);
    layout.outputBuses.add (set);
    proc->setBusesLayout (layout);
    proc->setRateAndBufferSizeDetails (sampleRate, block);
    proc->prepareToPlay (sampleRate, block);
    return proc;
}

//======================================================================================================================
void testNullAndLatency()
{
    section ("Null tests and latency");

    for (auto quality : { 0, 1, 2, 3 })
    {
        for (auto sr : { 44100.0, 48000.0, 96000.0 })
        {
            auto proc = makeProcessor (sr, 512);
            setParam (*proc, ParamIDs::quality, static_cast<float> (quality));
            proc->prepareToPlay (sr, 512);
            const auto latency = proc->getLatencySamples();
            const auto input = makeSignal (sr, 1.0);

            // 1) Everything switched off.
            setAllModules (*proc, false);
            auto out = input;
            runProcessor (*proc, out, 512, true);
            const auto diffOff = maxDifferenceDelayed (input, out, latency);
            check (diffOff < 1.0e-5f, juce::String::formatted ("modules off should null (q=%d sr=%.0f diff=%g)", quality, sr, diffOff));

            // 2) Default patch with Magnitude at zero.
            proc = makeProcessor (sr, 512);
            setParam (*proc, ParamIDs::quality, static_cast<float> (quality));
            proc->prepareToPlay (sr, 512);
            setParam (*proc, ParamIDs::magnitude, 0.0f);
            setParam (*proc, ParamIDs::inGain, 6.0f);
            setParam (*proc, ParamIDs::lowCut, 300.0f);
            setParam (*proc, ParamIDs::tone, 50.0f);
            proc->prepareToPlay (sr, 512);
            out = input;
            runProcessor (*proc, out, 512, true);
            const auto diffMag = maxDifferenceDelayed (input, out, latency);
            check (diffMag < 1.0e-5f, juce::String::formatted ("magnitude 0 should null (q=%d sr=%.0f diff=%g)", quality, sr, diffMag));

            // 3) Global mix at zero with everything cranked.
            proc = makeProcessor (sr, 512);
            setParam (*proc, ParamIDs::quality, static_cast<float> (quality));
            for (auto id : { ParamIDs::noiseAmount, ParamIDs::wobbleAmount, ParamIDs::distortAmount, ParamIDs::digitalAmount, ParamIDs::spaceAmount, ParamIDs::magneticAmount })
                setParam (*proc, id, 80.0f);
            setParam (*proc, ParamIDs::mix, 0.0f);
            proc->prepareToPlay (sr, 512);
            out = input;
            runProcessor (*proc, out, 512, true);
            const auto diffMix = maxDifferenceDelayed (input, out, latency);
            check (diffMix < 1.0e-5f, juce::String::formatted ("mix 0 should null (q=%d sr=%.0f diff=%g)", quality, sr, diffMix));

            if (static_cast<int> (sr) == 48000)
                std::printf ("  quality %d: latency %d samples, null diffs off=%.2g mag=%.2g mix=%.2g\n", quality, latency, diffOff, diffMag, diffMix);
        }
    }

    // Each module enabled with amount 0 must be transparent too.
    for (auto id : { ParamIDs::noiseOn, ParamIDs::wobbleOn, ParamIDs::distortOn, ParamIDs::digitalOn, ParamIDs::spaceOn, ParamIDs::magneticOn })
    {
        auto proc = makeProcessor (48000.0, 256);
        setAllModules (*proc, false);
        setParam (*proc, id, 1.0f);
        for (auto a : { ParamIDs::noiseAmount, ParamIDs::wobbleAmount, ParamIDs::distortAmount, ParamIDs::digitalAmount, ParamIDs::spaceAmount, ParamIDs::magneticAmount })
            setParam (*proc, a, 0.0f);
        proc->prepareToPlay (48000.0, 256);
        const auto input = makeSignal (48000.0, 0.5);
        auto out = input;
        runProcessor (*proc, out, 256, false);
        const auto diff = maxDifferenceDelayed (input, out, proc->getLatencySamples());
        check (diff < 1.0e-5f, juce::String ("module at 0 % should be transparent: ") + id + " diff=" + juce::String (diff));
    }

    // Impulse should appear exactly at the reported latency.
    {
        auto proc = makeProcessor (48000.0, 128);
        setAllModules (*proc, false);
        proc->prepareToPlay (48000.0, 128);
        juce::AudioBuffer<float> b (2, 4096);
        b.clear();
        b.setSample (0, 100, 1.0f);
        b.setSample (1, 100, 1.0f);
        runProcessor (*proc, b, 128, false);
        int peakIndex = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (std::abs (b.getSample (0, i)) > std::abs (b.getSample (0, peakIndex)))
                peakIndex = i;
        check (peakIndex - 100 == proc->getLatencySamples(),
               juce::String::formatted ("impulse delay %d should equal latency %d", peakIndex - 100, proc->getLatencySamples()));
    }
}

void testStabilityFuzz()
{
    section ("Stability fuzzing (random parameters, sample rates, block sizes)");
    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> uni (0.0f, 1.0f);
    int runs = 0;

    for (auto sr : { 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        for (int r = 0; r < 12; ++r)
        {
            const auto maxBlock = std::array<int, 5> { 1, 32, 333, 512, 2048 }[static_cast<size_t> (r % 5)];
            auto proc = makeProcessor (sr, maxBlock);

            for (auto* param : proc->getParameters())
                param->setValueNotifyingHost (uni (rng));

            setParam (*proc, ParamIDs::magnitude, 100.0f);
            setParam (*proc, ParamIDs::inGain, 0.0f);
            setParam (*proc, ParamIDs::outGain, 0.0f);
            proc->prepareToPlay (sr, maxBlock);
            auto buffer = makeSignal (sr, 1.5, static_cast<uint32_t> (r + 1));

            // Change parameters mid-stream too.
            runProcessor (*proc, buffer, maxBlock, true, static_cast<uint32_t> (r));
            for (auto* param : proc->getParameters())
                if (uni (rng) < 0.3f)
                    param->setValueNotifyingHost (uni (rng));
            auto buffer2 = makeSignal (sr, 0.5, static_cast<uint32_t> (r + 100));
            runProcessor (*proc, buffer2, maxBlock, true, static_cast<uint32_t> (r + 50));

            const auto s1 = analyse (buffer);
            const auto s2 = analyse (buffer2);
            check (s1.finite && s2.finite, juce::String::formatted ("output must be finite (sr=%.0f run=%d)", sr, r));
            check (s1.peak < 16.0f && s2.peak < 16.0f, juce::String::formatted ("output peak bounded (sr=%.0f run=%d peak=%.2f/%.2f)", sr, r, s1.peak, s2.peak));
            ++runs;
        }
    }

    std::printf ("  %d randomised runs completed\n", runs);
}

void testPresets()
{
    section ("Factory presets");
    auto proc = makeProcessor (48000.0, 512);
    auto& pm = proc->getPresetManager();
    const auto count = static_cast<int> (pm.getPresets().size());
    const auto input = makeSignal (48000.0, 3.0);
    const auto inputRms = analyse (input).rms;

    for (int i = 0; i < count; ++i)
    {
        if (! pm.getPresets()[static_cast<size_t> (i)].isFactory)
            continue;

        check (pm.loadPreset (i), "preset loads");
        proc->prepareToPlay (48000.0, 512);
        auto out = input;
        runProcessor (*proc, out, 512, false);
        const auto s = analyse (out, 4800);
        check (s.finite, "preset output finite: " + pm.getPresets()[static_cast<size_t> (i)].name);
        check (s.peak < 6.0f, "preset peak reasonable: " + pm.getPresets()[static_cast<size_t> (i)].name + " peak=" + juce::String (s.peak));
        std::printf ("  %-22s level %+5.1f dB (peak %5.2f)\n", pm.getPresets()[static_cast<size_t> (i)].name.toRawUTF8(), toDb (s.rms) - toDb (inputRms), s.peak);
    }

    check (count >= 30, "at least 30 factory presets");
}

void testStateRoundTrip()
{
    section ("State save / restore");
    std::mt19937 rng (99);
    std::uniform_real_distribution<float> uni (0.0f, 1.0f);
    auto a = makeProcessor (44100.0, 256);

    for (auto* param : a->getParameters())
        param->setValueNotifyingHost (uni (rng));

    juce::MemoryBlock block;
    a->getStateInformation (block);
    auto b = makeProcessor (44100.0, 256);
    b->setStateInformation (block.getData(), static_cast<int> (block.getSize()));

    int mismatches = 0;
    for (int i = 0; i < a->getParameters().size(); ++i)
    {
        const auto ta = a->getParameters()[i]->getCurrentValueAsText();
        const auto tb = b->getParameters()[i]->getCurrentValueAsText();
        if (ta != tb)
        {
            ++mismatches;
            std::printf ("  %s: %s vs %s\n", a->getParameters()[i]->getName (64).toRawUTF8(), ta.toRawUTF8(), tb.toRawUTF8());
        }
    }

    check (mismatches == 0, "all parameters restored (" + juce::String (mismatches) + " mismatches)");
    std::printf ("  %d parameters round-tripped\n", a->getParameters().size());
}

void testNoiseCalibration()
{
    section ("Noise calibration (amount 100 %, silent input)");
    const auto names = noiseTypeNames();

    for (int t = 0; t < names.size(); ++t)
    {
        auto proc = makeProcessor (48000.0, 512);
        setAllModules (*proc, false);
        setParam (*proc, ParamIDs::noiseOn, 1.0f);
        setParam (*proc, ParamIDs::noiseType, static_cast<float> (t));
        setParam (*proc, ParamIDs::noiseAmount, 100.0f);
        setParam (*proc, ParamIDs::noiseFollow, 0.0f);
        setParam (*proc, ParamIDs::noiseDuck, 0.0f);
        proc->prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> b (2, 48000 * 4);
        b.clear();
        runProcessor (*proc, b, 512, false);
        const auto s = analyse (b, 4800);
        std::printf ("  %-13s rms %6.1f dBFS  peak %6.1f dBFS\n", names[t].toRawUTF8(), toDb (s.rms), toDb (s.peak));
        check (s.finite, "noise finite: " + names[t]);
        check (toDb (s.rms) > -28.0 && toDb (s.rms) < -14.0, "noise level in calibrated range: " + names[t]);
    }
}

void testDistortLevels()
{
    section ("Distort level matching (1 kHz sine at -12 dBFS)");
    const auto names = distortTypeNames();

    for (int t = 0; t < names.size(); ++t)
    {
        juce::String line = "  " + names[t].paddedRight (' ', 12);
        for (auto amount : { 25.0f, 50.0f, 75.0f, 100.0f })
        {
            auto proc = makeProcessor (48000.0, 512);
            setAllModules (*proc, false);
            setParam (*proc, ParamIDs::distortOn, 1.0f);
            setParam (*proc, ParamIDs::distortType, static_cast<float> (t));
            setParam (*proc, ParamIDs::distortAmount, amount);
            setParam (*proc, ParamIDs::distortFlux, 0.0f);
            proc->prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> b (2, 48000);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto v = 0.251f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / 48000.0));
                b.setSample (0, i, v);
                b.setSample (1, i, v);
            }
            const auto inRms = analyse (b).rms;
            runProcessor (*proc, b, 512, false);
            const auto s = analyse (b, 9600);
            const auto deltaDb = toDb (s.rms) - toDb (inRms);
            line << juce::String::formatted (" %3.0f%%:%+5.1fdB", amount, deltaDb);
            check (s.finite && std::abs (deltaDb) < 7.5, "distort level within 7.5 dB: " + names[t] + " " + juce::String (amount));
        }
        std::printf ("%s\n", line.toRawUTF8());
    }
}

void testSpaceLevels()
{
    section ("Space wet level (amount 100 %, signal input)");
    const auto names = spaceTypeNames();
    const auto input = makeSignal (48000.0, 4.0);
    const auto inRms = analyse (input).rms;

    for (int t = 0; t < names.size(); ++t)
    {
        juce::String line = "  " + names[t].paddedRight (' ', 11);
        for (auto decay : { 0.0f, 50.0f, 100.0f })
        {
            auto proc = makeProcessor (48000.0, 512);
            setAllModules (*proc, false);
            setParam (*proc, ParamIDs::spaceOn, 1.0f);
            setParam (*proc, ParamIDs::spaceType, static_cast<float> (t));
            setParam (*proc, ParamIDs::spaceAmount, 100.0f);
            setParam (*proc, ParamIDs::spaceDecay, decay);
            setParam (*proc, ParamIDs::spaceFlux, 0.0f);
            proc->prepareToPlay (48000.0, 512);
            auto out = input;
            runProcessor (*proc, out, 512, false);
            const auto s = analyse (out, 48000);
            const auto deltaDb = toDb (s.rms) - toDb (inRms);
            line << juce::String::formatted (" decay %3.0f%%:%+5.1fdB", decay, deltaDb);
            check (s.finite && deltaDb > -18.0 && deltaDb < 9.0, "space wet level reasonable: " + names[t] + " decay " + juce::String (decay));
        }
        std::printf ("%s\n", line.toRawUTF8());
    }
}

void testMono()
{
    section ("Mono layout");
    auto proc = makeProcessor (48000.0, 512, 1);
    juce::AudioBuffer<float> b (1, 48000);
    const auto src = makeSignal (48000.0, 1.0);
    b.copyFrom (0, 0, src, 0, 0, 48000);
    runProcessor (*proc, b, 512, true);
    const auto s = analyse (b);
    check (s.finite && s.peak < 6.0f, "mono processing finite and bounded");
    check (proc->getTotalNumOutputChannels() == 1, "mono layout accepted");
}

void testBenchmark()
{
    section ("CPU benchmark (48 kHz stereo, 512-sample blocks, 20 s of audio)");

    for (auto presetName : { "Afterglow", "Lo-Fi Study Beats", "Cosmic Flux" })
    {
        for (auto quality : { 1, 2, 3 })
        {
            auto proc = makeProcessor (48000.0, 512);
            proc->getPresetManager().loadPresetByName (presetName);
            setParam (*proc, ParamIDs::quality, static_cast<float> (quality));
            proc->prepareToPlay (48000.0, 512);
            auto b = makeSignal (48000.0, 20.0);
            const auto start = std::chrono::high_resolution_clock::now();
            runProcessor (*proc, b, 512, false);
            const auto seconds = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - start).count();
            std::printf ("  %-18s quality %d: %6.2f%% of one core (%.0fx realtime)\n", presetName, quality, 100.0 * seconds / 20.0, 20.0 / seconds);
        }
    }
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf ("Afterglow tests\n");
    testNullAndLatency();
    testStabilityFuzz();
    testPresets();
    testStateRoundTrip();
    testNoiseCalibration();
    testDistortLevels();
    testSpaceLevels();
    testMono();

    if (juce::SystemStats::getEnvironmentVariable ("AFTERGLOW_BENCHMARK", "1") != "0")
        testBenchmark();

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
