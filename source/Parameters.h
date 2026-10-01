#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace afterglow
{
/** Every automatable parameter in the plugin. IDs are stable: never rename them, presets and sessions depend on them. */
namespace ParamIDs
{
    // Global
    inline constexpr const char* magnitude  = "magnitude";
    inline constexpr const char* mix        = "mix";
    inline constexpr const char* limiter    = "limiter";
    inline constexpr const char* quality    = "quality";

    // Noise
    inline constexpr const char* noiseOn     = "noise_on";
    inline constexpr const char* noiseAmount = "noise_amount";
    inline constexpr const char* noiseType   = "noise_type";
    inline constexpr const char* noiseTone   = "noise_tone";
    inline constexpr const char* noiseFollow = "noise_follow";
    inline constexpr const char* noiseDuck   = "noise_duck";
    inline constexpr const char* noisePost   = "noise_post";
    inline constexpr const char* noiseFlux   = "noise_flux";

    // Wobble
    inline constexpr const char* wobbleOn          = "wobble_on";
    inline constexpr const char* wobbleAmount      = "wobble_amount";
    inline constexpr const char* wobbleBalance     = "wobble_balance";
    inline constexpr const char* wobbleWowRate     = "wobble_wow_rate";
    inline constexpr const char* wobbleFlutterRate = "wobble_flutter_rate";
    inline constexpr const char* wobbleSync        = "wobble_sync";
    inline constexpr const char* wobbleDivision    = "wobble_division";
    inline constexpr const char* wobbleStereo      = "wobble_stereo";
    inline constexpr const char* wobbleMix         = "wobble_mix";
    inline constexpr const char* wobbleFlux        = "wobble_flux";

    // Distort
    inline constexpr const char* distortOn        = "distort_on";
    inline constexpr const char* distortAmount    = "distort_amount";
    inline constexpr const char* distortType      = "distort_type";
    inline constexpr const char* distortFocusLow  = "distort_focus_low";
    inline constexpr const char* distortFocusHigh = "distort_focus_high";
    inline constexpr const char* distortTone      = "distort_tone";
    inline constexpr const char* distortMix       = "distort_mix";
    inline constexpr const char* distortFlux      = "distort_flux";

    // Digital
    inline constexpr const char* digitalOn        = "digital_on";
    inline constexpr const char* digitalAmount    = "digital_amount";
    inline constexpr const char* digitalBalance   = "digital_balance";
    inline constexpr const char* digitalSmooth    = "digital_smooth";
    inline constexpr const char* digitalFocusLow  = "digital_focus_low";
    inline constexpr const char* digitalFocusHigh = "digital_focus_high";
    inline constexpr const char* digitalCut       = "digital_cut";
    inline constexpr const char* digitalCompand   = "digital_compand";
    inline constexpr const char* digitalMix       = "digital_mix";
    inline constexpr const char* digitalFlux      = "digital_flux";

    // Space
    inline constexpr const char* spaceOn        = "space_on";
    inline constexpr const char* spaceAmount    = "space_amount";
    inline constexpr const char* spaceType      = "space_type";
    inline constexpr const char* spaceDecay     = "space_decay";
    inline constexpr const char* spacePreDelay  = "space_predelay";
    inline constexpr const char* spaceFocusLow  = "space_focus_low";
    inline constexpr const char* spaceFocusHigh = "space_focus_high";
    inline constexpr const char* spaceStereo    = "space_stereo";
    inline constexpr const char* spaceFlux      = "space_flux";

    // Magnetic
    inline constexpr const char* magneticOn       = "magnetic_on";
    inline constexpr const char* magneticAmount   = "magnetic_amount";
    inline constexpr const char* magneticBalance  = "magnetic_balance";
    inline constexpr const char* magneticRate     = "magnetic_rate";
    inline constexpr const char* magneticSync     = "magnetic_sync";
    inline constexpr const char* magneticDivision = "magnetic_division";
    inline constexpr const char* magneticDropouts = "magnetic_dropouts";
    inline constexpr const char* magneticStereo   = "magnetic_stereo";
    inline constexpr const char* magneticFlux     = "magnetic_flux";

    // Master
    inline constexpr const char* inGain      = "in_gain";
    inline constexpr const char* eqOn        = "eq_on";
    inline constexpr const char* lowCut      = "low_cut";
    inline constexpr const char* lowCutHard  = "low_cut_hard";
    inline constexpr const char* highCut     = "high_cut";
    inline constexpr const char* highCutHard = "high_cut_hard";
    inline constexpr const char* tone        = "tone";
    inline constexpr const char* toneMode    = "tone_mode";
    inline constexpr const char* width       = "width";
    inline constexpr const char* outGain     = "out_gain";
} // namespace ParamIDs

namespace Ranges
{
    inline constexpr float lowCutOff = 10.0f;      // the lowest low-cut position means "off"
    inline constexpr float lowCutMax = 2000.0f;
    inline constexpr float highCutMin = 1000.0f;
    inline constexpr float highCutOff = 22000.0f;  // the highest high-cut position means "off"
    inline constexpr float focusMin = 20.0f;
    inline constexpr float focusMax = 20000.0f;
} // namespace Ranges

enum class ModuleId { noise = 0, wobble, distort, digital, space, magnetic };
inline constexpr int numModules = 6;

const juce::StringArray& noiseTypeNames();
const juce::StringArray& distortTypeNames();
const juce::StringArray& spaceTypeNames();
const juce::StringArray& syncDivisionNames();
const juce::StringArray& qualityNames();
const juce::StringArray& toneModeNames();

/** Length of a tempo-synced division, in quarter notes. */
double syncDivisionInBeats (int index);

/** Oversampling factor exponent (0 = 1x, 1 = 2x, 2 = 4x, 3 = 8x) for a quality index. */
int qualityToOversamplingOrder (int qualityIndex);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Returns every parameter ID, in a stable order. */
const juce::StringArray& allParameterIds();

} // namespace afterglow
