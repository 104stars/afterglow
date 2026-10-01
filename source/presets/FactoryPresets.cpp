#include "PresetManager.h"

namespace afterglow
{
namespace
{
    // Parameter values are in their natural units: percent (0..100), bipolar (-100..100), Hz, ms, dB,
    // choice indices and 0/1 for switches. Anything not listed falls back to the default.
    //
    // Noise types:   0 Vinyl, 1 Shellac, 2 Tape, 3 Cassette, 4 VHS, 5 Hum 50, 6 Hum 60, 7 Buzz,
    //                8 Fuzz, 9 Room, 10 Radio, 11 Transmission, 12 8-Bit, 13 White, 14 Pink, 15 Brown
    // Distort types: 0 Tube, 1 Transformer, 2 Speaker, 3 Tape, 4 Fuzz, 5 Clip, 6 Fold, 7 Rectify
    // Space types:   0 Ambience, 1 Room, 2 Plate, 3 Hall, 4 Spring, 5 Resonator
    using Values = std::vector<std::pair<juce::String, float>>;

    PresetManager::Preset make (const char* name, const char* category, const char* description, Values values)
    {
        PresetManager::Preset p;
        p.name = name;
        p.category = category;
        p.author = "Afterglow Factory";
        p.description = description;
        p.isFactory = true;
        p.values = std::move (values);
        return p;
    }

    const Values allOff {
        { "noise_on", 0 }, { "wobble_on", 0 }, { "distort_on", 0 }, { "digital_on", 0 }, { "space_on", 0 }, { "magnetic_on", 0 }
    };

    Values with (Values base, const Values& extra)
    {
        for (const auto& kv : extra)
        {
            auto it = std::find_if (base.begin(), base.end(), [&] (const auto& b) { return b.first == kv.first; });
            if (it != base.end())
                it->second = kv.second;
            else
                base.push_back (kv);
        }
        return base;
    }
} // namespace

const std::vector<PresetManager::Preset>& getFactoryPresets()
{
    static const std::vector<PresetManager::Preset> presets {
        // Mix Bus -----------------------------------------------------------------------------------------------------
        make ("Afterglow", "Mix Bus", "A gentle, all-round vintage tint. A good place to start.", {}),

        make ("Clean Slate", "Mix Bus", "Every module switched off. Build your own colour from scratch.", allOff),

        make ("Glue & Tape", "Mix Bus", "Subtle tape saturation and transport movement that pulls a mix together.", {
            { "noise_type", 2 }, { "noise_amount", 8 }, { "noise_follow", 30 },
            { "wobble_amount", 8 }, { "wobble_flux", 10 },
            { "distort_type", 3 }, { "distort_amount", 25 }, { "distort_flux", 5 },
            { "space_on", 0 },
            { "magnetic_amount", 12 }, { "magnetic_balance", 30 }, { "magnetic_dropouts", 0 } }),

        make ("Analog Sheen", "Mix Bus", "Transformer weight and a little air. Barely there, but you miss it when it is off.", {
            { "noise_on", 0 },
            { "wobble_amount", 5 },
            { "distort_type", 1 }, { "distort_amount", 20 },
            { "space_on", 0 },
            { "magnetic_amount", 6 }, { "magnetic_dropouts", 0 },
            { "tone", 12 } }),

        make ("Late Night Vinyl", "Mix Bus", "A record spinning at 33 1/3, crackle tucked under the music.", {
            { "noise_type", 0 }, { "noise_amount", 35 }, { "noise_follow", 10 }, { "noise_duck", 20 },
            { "wobble_amount", 18 }, { "wobble_wow_rate", 0.5556f }, { "wobble_flux", 20 },
            { "distort_type", 0 }, { "distort_amount", 15 },
            { "space_on", 0 }, { "magnetic_on", 0 },
            { "low_cut", 35 }, { "high_cut", 13000 }, { "tone", -10 } }),

        // Drums -------------------------------------------------------------------------------------------------------
        make ("Dusty Breaks", "Drums", "Sampled-from-wax drum break: crunchy, narrow and dusty.", {
            { "noise_type", 0 }, { "noise_amount", 32 }, { "noise_follow", 40 }, { "noise_duck", 20 },
            { "wobble_amount", 12 },
            { "distort_type", 3 }, { "distort_amount", 38 }, { "distort_focus_low", 120 },
            { "digital_amount", 35 }, { "digital_balance", 40 }, { "digital_smooth", 20 }, { "digital_focus_low", 800 },
            { "space_type", 1 }, { "space_amount", 10 }, { "space_decay", 25 },
            { "magnetic_amount", 22 }, { "magnetic_dropouts", 8 },
            { "low_cut", 40 }, { "high_cut", 12000 }, { "tone", -15 } }),

        make ("Kick Crunch", "Drums", "Crushes only the click of the kick and leaves the sub untouched.", with (allOff, {
            { "distort_on", 1 }, { "distort_type", 0 }, { "distort_amount", 30 }, { "distort_focus_low", 60 },
            { "digital_on", 1 }, { "digital_amount", 45 }, { "digital_balance", 55 }, { "digital_focus_low", 1500 } })),

        make ("Snare Smash", "Drums", "Clipped, splashy snare with a bright plate behind it.", {
            { "noise_type", 9 }, { "noise_amount", 10 }, { "noise_follow", 60 },
            { "wobble_on", 0 },
            { "distort_type", 5 }, { "distort_amount", 55 }, { "distort_mix", 70 },
            { "digital_amount", 20 }, { "digital_balance", 80 },
            { "space_type", 2 }, { "space_amount", 22 }, { "space_decay", 45 },
            { "magnetic_amount", 10 } }),

        make ("Tape Drums", "Drums", "Drums printed to a hot tape machine: rounded transients and gentle flutter.", {
            { "noise_type", 2 }, { "noise_amount", 20 }, { "noise_follow", 60 },
            { "wobble_amount", 10 },
            { "distort_type", 3 }, { "distort_amount", 40 },
            { "space_on", 0 },
            { "magnetic_amount", 30 }, { "magnetic_balance", 60 }, { "magnetic_rate", 11 },
            { "high_cut", 14000 } }),

        make ("Boom Bap Bus", "Drums", "Companded 12-bit grit, dusty vinyl and a firm low end.", {
            { "noise_type", 0 }, { "noise_amount", 25 }, { "noise_follow", 30 },
            { "wobble_amount", 8 },
            { "distort_type", 1 }, { "distort_amount", 30 },
            { "digital_amount", 40 }, { "digital_balance", 30 }, { "digital_compand", 1 }, { "digital_smooth", 30 },
            { "space_on", 0 },
            { "magnetic_amount", 10 },
            { "low_cut", 30 }, { "low_cut_hard", 1 }, { "tone", -20 } }),

        // Bass --------------------------------------------------------------------------------------------------------
        make ("Warm Bass", "Bass", "Iron and valves: thicker, rounder bass that still sits in the mix.", with (allOff, {
            { "distort_on", 1 }, { "distort_type", 1 }, { "distort_amount", 35 },
            { "tone", -10 } })),

        make ("Fuzz Bass", "Bass", "Fuzz on the mids and highs only, the sub stays clean.", with (allOff, {
            { "noise_on", 1 }, { "noise_type", 8 }, { "noise_amount", 8 }, { "noise_follow", 80 },
            { "distort_on", 1 }, { "distort_type", 4 }, { "distort_amount", 55 }, { "distort_focus_low", 300 }, { "distort_mix", 60 } })),

        make ("Sub Saturator", "Bass", "Valve saturation focused below 250 Hz for weight on small speakers.", with (allOff, {
            { "distort_on", 1 }, { "distort_type", 0 }, { "distort_amount", 40 }, { "distort_focus_high", 250 } })),

        // Keys --------------------------------------------------------------------------------------------------------
        make ("Old Upright", "Keys", "A slightly detuned piano recorded long ago in a small room.", {
            { "noise_type", 0 }, { "noise_amount", 28 },
            { "wobble_amount", 30 }, { "wobble_balance", 25 }, { "wobble_wow_rate", 0.6f }, { "wobble_flux", 30 },
            { "distort_type", 0 }, { "distort_amount", 18 },
            { "space_type", 1 }, { "space_amount", 18 }, { "space_decay", 40 },
            { "magnetic_amount", 20 }, { "magnetic_dropouts", 6 },
            { "low_cut", 60 }, { "high_cut", 9000 } }),

        make ("Cassette Rhodes", "Keys", "Electric piano bounced to a well-loved cassette.", {
            { "noise_type", 3 }, { "noise_amount", 30 }, { "noise_follow", 20 },
            { "wobble_amount", 42 }, { "wobble_balance", 40 }, { "wobble_stereo", 1 }, { "wobble_flux", 25 },
            { "distort_type", 3 }, { "distort_amount", 25 },
            { "space_type", 2 }, { "space_amount", 10 },
            { "magnetic_amount", 28 }, { "magnetic_balance", 45 },
            { "high_cut", 11000 } }),

        make ("Warped Wurli", "Keys", "Slow, seasick wow from a record left in the sun.", {
            { "noise_type", 0 }, { "noise_amount", 20 },
            { "wobble_amount", 60 }, { "wobble_balance", 15 }, { "wobble_wow_rate", 0.35f }, { "wobble_flux", 50 },
            { "distort_type", 0 }, { "distort_amount", 30 },
            { "space_type", 1 }, { "space_amount", 15 } }),

        make ("Lush Chorus Keys", "Keys", "Stereo wow at 50 % mix turns into a wide, warm chorus.", with (allOff, {
            { "wobble_on", 1 }, { "wobble_amount", 45 }, { "wobble_balance", 30 }, { "wobble_wow_rate", 1.2f },
            { "wobble_stereo", 1 }, { "wobble_mix", 50 }, { "wobble_flux", 15 },
            { "space_on", 1 }, { "space_type", 3 }, { "space_amount", 18 }, { "space_decay", 50 } })),

        make ("Music Box Memory", "Keys", "Bit-reduced, wobbly and far away, like a half-remembered melody.", {
            { "noise_type", 1 }, { "noise_amount", 15 },
            { "wobble_amount", 35 }, { "wobble_flux", 40 },
            { "distort_on", 0 },
            { "digital_amount", 25 }, { "digital_balance", 70 }, { "digital_smooth", 40 },
            { "space_type", 2 }, { "space_amount", 30 }, { "space_decay", 60 },
            { "high_cut", 8000 } }),

        // Guitar ------------------------------------------------------------------------------------------------------
        make ("Broken Amp", "Guitar", "A torn speaker cone, a spring tank and some mains hum.", {
            { "noise_type", 6 }, { "noise_amount", 12 }, { "noise_follow", 50 },
            { "wobble_amount", 8 },
            { "distort_type", 2 }, { "distort_amount", 55 },
            { "space_type", 4 }, { "space_amount", 25 }, { "space_decay", 45 },
            { "magnetic_on", 0 } }),

        make ("Surf Spring", "Guitar", "Drippy spring reverb and a touch of valve warmth.", {
            { "noise_on", 0 },
            { "wobble_amount", 15 }, { "wobble_balance", 70 }, { "wobble_flutter_rate", 7 },
            { "distort_type", 0 }, { "distort_amount", 25 },
            { "space_type", 4 }, { "space_amount", 40 }, { "space_decay", 55 }, { "space_predelay", 5 },
            { "magnetic_on", 0 } }),

        make ("Garage Fuzz", "Guitar", "Gated, sputtering germanium fuzz in a small room.", {
            { "noise_type", 8 }, { "noise_amount", 15 }, { "noise_follow", 60 },
            { "wobble_on", 0 },
            { "distort_type", 4 }, { "distort_amount", 60 }, { "distort_focus_low", 120 }, { "distort_focus_high", 8000 },
            { "space_type", 1 }, { "space_amount", 12 },
            { "magnetic_on", 0 } }),

        // Vocals ------------------------------------------------------------------------------------------------------
        make ("Telephone", "Vocals", "A narrow, crunchy phone line.", with (allOff, {
            { "distort_on", 1 }, { "distort_type", 5 }, { "distort_amount", 30 }, { "distort_focus_low", 400 }, { "distort_focus_high", 3000 },
            { "digital_on", 1 }, { "digital_amount", 30 }, { "digital_balance", 60 }, { "digital_smooth", 20 },
            { "digital_focus_low", 300 }, { "digital_focus_high", 3400 }, { "digital_cut", 1 },
            { "low_cut", 300 }, { "low_cut_hard", 1 }, { "high_cut", 3400 }, { "high_cut_hard", 1 },
            { "tone_mode", 1 }, { "tone", 40 } })),

        make ("AM Radio Voice", "Vocals", "An old AM broadcast with static and fading.", {
            { "noise_type", 10 }, { "noise_amount", 25 }, { "noise_follow", 40 },
            { "wobble_on", 0 },
            { "distort_type", 1 }, { "distort_amount", 35 }, { "distort_focus_low", 250 }, { "distort_focus_high", 4500 },
            { "space_type", 1 }, { "space_amount", 8 },
            { "magnetic_amount", 20 }, { "magnetic_dropouts", 10 },
            { "low_cut", 250 }, { "low_cut_hard", 1 }, { "high_cut", 4500 }, { "high_cut_hard", 1 },
            { "tone_mode", 1 }, { "tone", 30 } }),

        make ("Vintage Vocal Plate", "Vocals", "A classic plate with pre-delay and a gentle valve front end.", {
            { "noise_type", 2 }, { "noise_amount", 10 },
            { "wobble_amount", 6 },
            { "distort_type", 0 }, { "distort_amount", 15 },
            { "space_type", 2 }, { "space_amount", 28 }, { "space_decay", 50 }, { "space_predelay", 30 },
            { "magnetic_amount", 8 }, { "magnetic_dropouts", 0 },
            { "tone", 10 } }),

        make ("Megaphone", "Vocals", "A shouty, honky horn speaker.", with (allOff, {
            { "distort_on", 1 }, { "distort_type", 2 }, { "distort_amount", 60 }, { "distort_focus_low", 500 }, { "distort_focus_high", 4000 },
            { "space_on", 1 }, { "space_type", 0 }, { "space_amount", 10 },
            { "low_cut", 450 }, { "low_cut_hard", 1 }, { "high_cut", 4200 }, { "high_cut_hard", 1 } })),

        // Lo-Fi -------------------------------------------------------------------------------------------------------
        make ("Lo-Fi Study Beats", "Lo-Fi", "Everything at once: vinyl, wobble, tape and a crushed sampler.", {
            { "noise_type", 0 }, { "noise_amount", 38 }, { "noise_follow", 30 },
            { "wobble_amount", 35 }, { "wobble_balance", 30 }, { "wobble_wow_rate", 0.5f }, { "wobble_flux", 30 },
            { "distort_type", 3 }, { "distort_amount", 30 },
            { "digital_amount", 30 }, { "digital_balance", 45 }, { "digital_smooth", 30 }, { "digital_compand", 1 },
            { "space_type", 1 }, { "space_amount", 15 }, { "space_decay", 40 },
            { "magnetic_amount", 25 }, { "magnetic_dropouts", 10 },
            { "low_cut", 50 }, { "high_cut", 9000 }, { "tone", -20 } }),

        make ("VHS Memories", "Lo-Fi", "Home video soundtrack: tracking wobble, hiss and dropouts.", {
            { "noise_type", 4 }, { "noise_amount", 30 }, { "noise_follow", 20 },
            { "wobble_amount", 40 }, { "wobble_balance", 45 }, { "wobble_wow_rate", 0.25f }, { "wobble_stereo", 1 }, { "wobble_flux", 35 },
            { "distort_type", 3 }, { "distort_amount", 25 },
            { "digital_amount", 15 }, { "digital_balance", 10 },
            { "magnetic_amount", 30 }, { "magnetic_balance", 35 }, { "magnetic_dropouts", 20 },
            { "high_cut", 10000 }, { "tone", -15 }, { "width", 80 } }),

        make ("Walkman", "Lo-Fi", "A pocket cassette player with tired batteries.", {
            { "noise_type", 3 }, { "noise_amount", 35 },
            { "wobble_amount", 45 }, { "wobble_balance", 55 }, { "wobble_wow_rate", 1.4f }, { "wobble_flutter_rate", 9 }, { "wobble_flux", 25 },
            { "distort_type", 3 }, { "distort_amount", 30 },
            { "space_on", 0 },
            { "magnetic_amount", 35 }, { "magnetic_balance", 50 }, { "magnetic_dropouts", 15 },
            { "low_cut", 70 }, { "high_cut", 10000 }, { "width", 90 } }),

        make ("8-Bit Console", "Lo-Fi", "Home computer sound chip: few bits, hard edges.", with (allOff, {
            { "noise_on", 1 }, { "noise_type", 12 }, { "noise_amount", 15 }, { "noise_follow", 70 },
            { "distort_on", 1 }, { "distort_type", 5 }, { "distort_amount", 20 },
            { "digital_on", 1 }, { "digital_amount", 70 }, { "digital_balance", 75 } })),

        make ("Sampler 1987", "Lo-Fi", "A companded 12-bit sampler with its reconstruction filters engaged.", with (allOff, {
            { "distort_on", 1 }, { "distort_type", 1 }, { "distort_amount", 20 },
            { "digital_on", 1 }, { "digital_amount", 45 }, { "digital_balance", 35 }, { "digital_compand", 1 }, { "digital_smooth", 50 } })),

        make ("Shellac 78", "Lo-Fi", "A mono 78 rpm disc: band-limited, crackly and wavering.", {
            { "noise_type", 1 }, { "noise_amount", 45 }, { "noise_follow", 10 },
            { "wobble_amount", 25 }, { "wobble_balance", 30 }, { "wobble_wow_rate", 1.3f }, { "wobble_flux", 25 },
            { "distort_type", 0 }, { "distort_amount", 20 },
            { "space_on", 0 },
            { "magnetic_amount", 15 },
            { "low_cut", 250 }, { "low_cut_hard", 1 }, { "high_cut", 5500 }, { "high_cut_hard", 1 },
            { "tone_mode", 1 }, { "tone", 30 }, { "width", 0 } }),

        make ("Worn Out Tape", "Lo-Fi", "The tape has been played a thousand times and it shows.", {
            { "noise_type", 2 }, { "noise_amount", 25 },
            { "wobble_amount", 35 }, { "wobble_balance", 40 }, { "wobble_flux", 30 },
            { "distort_type", 3 }, { "distort_amount", 30 },
            { "space_on", 0 },
            { "magnetic_amount", 65 }, { "magnetic_balance", 35 }, { "magnetic_dropouts", 45 }, { "magnetic_flux", 40 },
            { "high_cut", 9000 } }),

        // Sound Design ------------------------------------------------------------------------------------------------
        make ("Cosmic Flux", "Sound Design", "Maximum Flux everywhere: nothing ever repeats.", {
            { "noise_type", 11 }, { "noise_amount", 20 }, { "noise_flux", 50 },
            { "wobble_amount", 50 }, { "wobble_stereo", 1 }, { "wobble_flux", 100 },
            { "distort_type", 6 }, { "distort_amount", 35 }, { "distort_flux", 60 },
            { "space_type", 3 }, { "space_amount", 40 }, { "space_decay", 75 }, { "space_flux", 70 },
            { "magnetic_amount", 30 }, { "magnetic_flux", 80 } }),

        make ("Resonant Dream", "Sound Design", "A chromatic resonator bank rings with every note.", {
            { "noise_type", 14 }, { "noise_amount", 6 },
            { "wobble_amount", 25 }, { "wobble_stereo", 1 }, { "wobble_flux", 40 },
            { "distort_on", 0 },
            { "space_type", 5 }, { "space_amount", 45 }, { "space_decay", 60 },
            { "magnetic_on", 0 } }),

        make ("Ghost Transmission", "Sound Design", "A distant signal from somewhere it should not be.", {
            { "noise_type", 11 }, { "noise_amount", 45 }, { "noise_follow", 0 },
            { "wobble_amount", 15 },
            { "distort_type", 7 }, { "distort_amount", 25 },
            { "digital_amount", 35 }, { "digital_focus_low", 500 }, { "digital_focus_high", 3000 }, { "digital_cut", 1 },
            { "space_type", 3 }, { "space_amount", 30 }, { "space_decay", 70 },
            { "magnetic_amount", 40 }, { "magnetic_dropouts", 40 } }),

        make ("Destroyer", "Sound Design", "Folded, crushed and falling apart. Handle with care.", {
            { "noise_type", 7 }, { "noise_amount", 15 },
            { "wobble_on", 0 },
            { "distort_type", 6 }, { "distort_amount", 70 },
            { "digital_amount", 60 },
            { "space_type", 1 }, { "space_amount", 15 },
            { "magnetic_amount", 40 }, { "magnetic_dropouts", 30 },
            { "limiter", 1 } }),

        // Post --------------------------------------------------------------------------------------------------------
        make ("Old Newsreel", "Post", "Cinema sound from the 1930s.", {
            { "noise_type", 1 }, { "noise_amount", 30 },
            { "wobble_amount", 20 },
            { "distort_type", 2 }, { "distort_amount", 30 },
            { "space_on", 0 },
            { "magnetic_amount", 30 }, { "magnetic_dropouts", 25 },
            { "low_cut", 300 }, { "low_cut_hard", 1 }, { "high_cut", 4000 }, { "high_cut_hard", 1 },
            { "width", 0 } }),

        make ("Next Door", "Post", "Muffled through a wall, in the room next door.", with (allOff, {
            { "noise_on", 1 }, { "noise_type", 9 }, { "noise_amount", 15 }, { "noise_post", 1 },
            { "space_on", 1 }, { "space_type", 1 }, { "space_amount", 25 }, { "space_decay", 30 },
            { "low_cut", 60 }, { "high_cut", 1000 }, { "high_cut_hard", 1 }, { "tone", -30 } })),

        make ("Walkie-Talkie", "Post", "Clipped, narrow and squelchy.", with (allOff, {
            { "noise_on", 1 }, { "noise_type", 11 }, { "noise_amount", 25 }, { "noise_follow", 70 },
            { "distort_on", 1 }, { "distort_type", 5 }, { "distort_amount", 50 }, { "distort_focus_low", 400 }, { "distort_focus_high", 3000 },
            { "digital_on", 1 }, { "digital_amount", 25 }, { "digital_balance", 70 },
            { "low_cut", 450 }, { "low_cut_hard", 1 }, { "high_cut", 3000 }, { "high_cut_hard", 1 } })),

        make ("Haunted Hum", "Post", "A humming, breathing room for horror cues.", {
            { "noise_type", 5 }, { "noise_amount", 25 }, { "noise_follow", 0 }, { "noise_flux", 40 },
            { "wobble_amount", 20 },
            { "distort_on", 0 },
            { "space_type", 3 }, { "space_amount", 20 }, { "space_decay", 60 },
            { "magnetic_amount", 20 } }),
    };

    return presets;
}

} // namespace afterglow
