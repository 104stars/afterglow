#include "Parameters.h"

namespace afterglow
{
namespace
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    using Float = juce::AudioParameterFloat;
    using Bool = juce::AudioParameterBool;
    using Choice = juce::AudioParameterChoice;
    using FloatAttributes = juce::AudioParameterFloatAttributes;

    constexpr int parameterVersion = 1;

    juce::ParameterID pid (const char* id) { return { id, parameterVersion }; }

    juce::NormalisableRange<float> logRange (float minValue, float maxValue)
    {
        const auto ratio = std::log (maxValue / minValue);
        return { minValue, maxValue,
                 [=] (float, float, float t) { return minValue * std::exp (ratio * t); },
                 [=] (float, float, float v) { return std::log (std::max (v, minValue) / minValue) / ratio; },
                 [=] (float, float, float v) { return juce::jlimit (minValue, maxValue, v); } };
    }

    juce::String formatHz (float hz)
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";
        return juce::String (juce::roundToInt (hz)) + " Hz";
    }

    float parseNumber (const juce::String& text)
    {
        auto t = text.trim().toLowerCase();
        auto value = t.retainCharacters ("0123456789.-+").getFloatValue();
        if (t.containsChar ('k'))
            value *= 1000.0f;
        return value;
    }

    FloatAttributes percentAttributes()
    {
        return FloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; })
            .withValueFromStringFunction ([] (const juce::String& s) { return parseNumber (s); });
    }

    FloatAttributes bipolarAttributes()
    {
        return FloatAttributes()
            .withStringFromValueFunction ([] (float v, int)
            {
                const auto r = juce::roundToInt (v);
                return r > 0 ? "+" + juce::String (r) : juce::String (r);
            })
            .withValueFromStringFunction ([] (const juce::String& s) { return parseNumber (s); });
    }

    FloatAttributes hzAttributes()
    {
        return FloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return formatHz (v); })
            .withValueFromStringFunction ([] (const juce::String& s) { return parseNumber (s); });
    }

    FloatAttributes rateAttributes()
    {
        return FloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 1.0f ? 2 : 1) + " Hz"; })
            .withValueFromStringFunction ([] (const juce::String& s) { return parseNumber (s); });
    }

    FloatAttributes dbAttributes()
    {
        return FloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int)
            {
                return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB";
            })
            .withValueFromStringFunction ([] (const juce::String& s) { return parseNumber (s); });
    }

    void addPercent (Layout& layout, const char* id, const juce::String& name, float def)
    {
        layout.add (std::make_unique<Float> (pid (id), name, juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), def, percentAttributes()));
    }

    void addBipolar (Layout& layout, const char* id, const juce::String& name, float def = 0.0f)
    {
        layout.add (std::make_unique<Float> (pid (id), name, juce::NormalisableRange<float> (-100.0f, 100.0f, 0.01f), def, bipolarAttributes()));
    }

    void addBool (Layout& layout, const char* id, const juce::String& name, bool def)
    {
        layout.add (std::make_unique<Bool> (pid (id), name, def));
    }

    void addFocus (Layout& layout, const char* lowId, const char* highId, const juce::String& prefix)
    {
        layout.add (std::make_unique<Float> (pid (lowId), prefix + " Focus Low", logRange (Ranges::focusMin, Ranges::focusMax), Ranges::focusMin, hzAttributes()));
        layout.add (std::make_unique<Float> (pid (highId), prefix + " Focus High", logRange (Ranges::focusMin, Ranges::focusMax), Ranges::focusMax, hzAttributes()));
    }
} // namespace

const juce::StringArray& noiseTypeNames()
{
    static const juce::StringArray names { "Vinyl", "Shellac", "Tape", "Cassette", "VHS", "Hum 50", "Hum 60", "Buzz",
                                           "Fuzz", "Room", "Radio", "Transmission", "8-Bit", "White", "Pink", "Brown" };
    return names;
}

const juce::StringArray& distortTypeNames()
{
    static const juce::StringArray names { "Tube", "Transformer", "Speaker", "Tape", "Fuzz", "Clip", "Fold", "Rectify" };
    return names;
}

const juce::StringArray& spaceTypeNames()
{
    static const juce::StringArray names { "Ambience", "Room", "Plate", "Hall", "Spring", "Resonator" };
    return names;
}

const juce::StringArray& syncDivisionNames()
{
    static const juce::StringArray names { "4 Bars", "2 Bars", "1 Bar", "1/2", "1/2 T", "1/4 D", "1/4", "1/4 T",
                                           "1/8 D", "1/8", "1/8 T", "1/16", "1/16 T", "1/32" };
    return names;
}

double syncDivisionInBeats (int index)
{
    static constexpr double beats[] { 16.0, 8.0, 4.0, 2.0, 4.0 / 3.0, 1.5, 1.0, 2.0 / 3.0,
                                      0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return beats[juce::jlimit (0, static_cast<int> (std::size (beats)) - 1, index)];
}

const juce::StringArray& qualityNames()
{
    static const juce::StringArray names { "Eco (1x)", "Standard (2x)", "High (4x)", "Ultra (8x)" };
    return names;
}

int qualityToOversamplingOrder (int qualityIndex) { return juce::jlimit (0, 3, qualityIndex); }

const juce::StringArray& toneModeNames()
{
    static const juce::StringArray names { "Tilt", "Mid" };
    return names;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace ParamIDs;
    Layout layout;

    // Global ----------------------------------------------------------------------------------------------------------
    addPercent (layout, magnitude, "Magnitude", 100.0f);
    addPercent (layout, mix, "Mix", 100.0f);
    addBool (layout, limiter, "Output Limiter", false);
    layout.add (std::make_unique<Choice> (pid (quality), "Quality", qualityNames(), 2,
                                          juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    // Noise -----------------------------------------------------------------------------------------------------------
    addBool (layout, noiseOn, "Noise On", true);
    addPercent (layout, noiseAmount, "Noise Amount", 18.0f);
    layout.add (std::make_unique<Choice> (pid (noiseType), "Noise Type", noiseTypeNames(), 0));
    addBipolar (layout, noiseTone, "Noise Tone");
    addPercent (layout, noiseFollow, "Noise Follow", 5.0f);
    addPercent (layout, noiseDuck, "Noise Duck", 0.0f);
    addBool (layout, noisePost, "Noise Post Routing", false);
    addPercent (layout, noiseFlux, "Noise Flux", 0.0f);

    // Wobble ----------------------------------------------------------------------------------------------------------
    addBool (layout, wobbleOn, "Wobble On", true);
    addPercent (layout, wobbleAmount, "Wobble Amount", 20.0f);
    addPercent (layout, wobbleBalance, "Wobble Wow/Flutter", 35.0f);
    layout.add (std::make_unique<Float> (pid (wobbleWowRate), "Wobble Wow Rate", logRange (0.05f, 8.0f), 0.55f, rateAttributes()));
    layout.add (std::make_unique<Float> (pid (wobbleFlutterRate), "Wobble Flutter Rate", logRange (3.0f, 30.0f), 11.0f, rateAttributes()));
    addBool (layout, wobbleSync, "Wobble Sync", false);
    layout.add (std::make_unique<Choice> (pid (wobbleDivision), "Wobble Sync Rate", syncDivisionNames(), 2));
    addBool (layout, wobbleStereo, "Wobble Stereo", false);
    addPercent (layout, wobbleMix, "Wobble Mix", 100.0f);
    addPercent (layout, wobbleFlux, "Wobble Flux", 15.0f);

    // Distort ---------------------------------------------------------------------------------------------------------
    addBool (layout, distortOn, "Distort On", true);
    addPercent (layout, distortAmount, "Distort Amount", 22.0f);
    layout.add (std::make_unique<Choice> (pid (distortType), "Distort Type", distortTypeNames(), 0));
    addFocus (layout, distortFocusLow, distortFocusHigh, "Distort");
    addBipolar (layout, distortTone, "Distort Tone");
    addPercent (layout, distortMix, "Distort Mix", 100.0f);
    addPercent (layout, distortFlux, "Distort Flux", 10.0f);

    // Digital ---------------------------------------------------------------------------------------------------------
    addBool (layout, digitalOn, "Digital On", true);
    addPercent (layout, digitalAmount, "Digital Amount", 0.0f);
    addPercent (layout, digitalBalance, "Digital Rate/Bits", 50.0f);
    addPercent (layout, digitalSmooth, "Digital Smooth", 0.0f);
    addFocus (layout, digitalFocusLow, digitalFocusHigh, "Digital");
    addBool (layout, digitalCut, "Digital Cut", false);
    addBool (layout, digitalCompand, "Digital Compand", false);
    addPercent (layout, digitalMix, "Digital Mix", 100.0f);
    addPercent (layout, digitalFlux, "Digital Flux", 0.0f);

    // Space -----------------------------------------------------------------------------------------------------------
    addBool (layout, spaceOn, "Space On", true);
    addPercent (layout, spaceAmount, "Space Amount", 12.0f);
    layout.add (std::make_unique<Choice> (pid (spaceType), "Space Type", spaceTypeNames(), 1));
    addPercent (layout, spaceDecay, "Space Decay", 35.0f);
    layout.add (std::make_unique<Float> (pid (spacePreDelay), "Space Pre-Delay",
                                         juce::NormalisableRange<float> (0.0f, 250.0f, 0.1f, 0.5f), 8.0f,
                                         FloatAttributes().withLabel ("ms")
                                             .withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; })
                                             .withValueFromStringFunction ([] (const juce::String& s) { return parseNumber (s); })));
    addFocus (layout, spaceFocusLow, spaceFocusHigh, "Space");
    addBool (layout, spaceStereo, "Space Stereo", true);
    addPercent (layout, spaceFlux, "Space Flux", 10.0f);

    // Magnetic --------------------------------------------------------------------------------------------------------
    addBool (layout, magneticOn, "Magnetic On", true);
    addPercent (layout, magneticAmount, "Magnetic Amount", 18.0f);
    addPercent (layout, magneticBalance, "Magnetic Wear/Flutter", 40.0f);
    layout.add (std::make_unique<Float> (pid (magneticRate), "Magnetic Flutter Rate", logRange (2.0f, 30.0f), 9.0f, rateAttributes()));
    addBool (layout, magneticSync, "Magnetic Sync", false);
    layout.add (std::make_unique<Choice> (pid (magneticDivision), "Magnetic Sync Rate", syncDivisionNames(), 11));
    addPercent (layout, magneticDropouts, "Magnetic Dropouts", 5.0f);
    addBool (layout, magneticStereo, "Magnetic Stereo", false);
    addPercent (layout, magneticFlux, "Magnetic Flux", 10.0f);

    // Master ----------------------------------------------------------------------------------------------------------
    layout.add (std::make_unique<Float> (pid (inGain), "Input Gain", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, dbAttributes()));
    addBool (layout, eqOn, "EQ On", true);
    layout.add (std::make_unique<Float> (pid (lowCut), "Low Cut", logRange (Ranges::lowCutOff, Ranges::lowCutMax), Ranges::lowCutOff,
                                         FloatAttributes().withLabel ("Hz")
                                             .withStringFromValueFunction ([] (float v, int) { return v <= Ranges::lowCutOff * 1.05f ? juce::String ("Off") : formatHz (v); })
                                             .withValueFromStringFunction ([] (const juce::String& s) { return s.trim().equalsIgnoreCase ("off") ? Ranges::lowCutOff : parseNumber (s); })));
    addBool (layout, lowCutHard, "Low Cut Hard", false);
    layout.add (std::make_unique<Float> (pid (highCut), "High Cut", logRange (Ranges::highCutMin, Ranges::highCutOff), Ranges::highCutOff,
                                         FloatAttributes().withLabel ("Hz")
                                             .withStringFromValueFunction ([] (float v, int) { return v >= Ranges::highCutOff * 0.995f ? juce::String ("Off") : formatHz (v); })
                                             .withValueFromStringFunction ([] (const juce::String& s) { return s.trim().equalsIgnoreCase ("off") ? Ranges::highCutOff : parseNumber (s); })));
    addBool (layout, highCutHard, "High Cut Hard", false);
    addBipolar (layout, tone, "Tone");
    layout.add (std::make_unique<Choice> (pid (toneMode), "Tone Mode", toneModeNames(), 0));
    layout.add (std::make_unique<Float> (pid (width), "Width", juce::NormalisableRange<float> (0.0f, 200.0f, 0.01f), 100.0f, percentAttributes()));
    layout.add (std::make_unique<Float> (pid (outGain), "Output Gain", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, dbAttributes()));

    return layout;
}

const juce::StringArray& allParameterIds()
{
    using namespace ParamIDs;
    static const juce::StringArray ids {
        magnitude, mix, limiter, quality,
        noiseOn, noiseAmount, noiseType, noiseTone, noiseFollow, noiseDuck, noisePost, noiseFlux,
        wobbleOn, wobbleAmount, wobbleBalance, wobbleWowRate, wobbleFlutterRate, wobbleSync, wobbleDivision, wobbleStereo, wobbleMix, wobbleFlux,
        distortOn, distortAmount, distortType, distortFocusLow, distortFocusHigh, distortTone, distortMix, distortFlux,
        digitalOn, digitalAmount, digitalBalance, digitalSmooth, digitalFocusLow, digitalFocusHigh, digitalCut, digitalCompand, digitalMix, digitalFlux,
        spaceOn, spaceAmount, spaceType, spaceDecay, spacePreDelay, spaceFocusLow, spaceFocusHigh, spaceStereo, spaceFlux,
        magneticOn, magneticAmount, magneticBalance, magneticRate, magneticSync, magneticDivision, magneticDropouts, magneticStereo, magneticFlux,
        inGain, eqOn, lowCut, lowCutHard, highCut, highCutHard, tone, toneMode, width, outGain
    };
    return ids;
}

} // namespace afterglow
