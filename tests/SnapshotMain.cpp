// Renders the Afterglow interface to PNG files (for documentation and visual checks).
// Usage: AfterglowSnapshot <output-dir> [scale] [preset name] [overlay: browser | about | save | -] [paramId=value ...]
// Parameter values are in the parameter's own units (for example distortType=5 or spaceDecay=60); name=<suffix>
// adds a suffix to the file name and silence=1 renders without the test signal.

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "snapshots");
    const auto scale = argc > 2 ? juce::String (argv[2]).getFloatValue() : 1.0f;
    const auto presetName = argc > 3 ? juce::String (argv[3]) : juce::String ("Lo-Fi Study Beats");
    const auto overlay = argc > 4 && juce::String (argv[4]) != "-" ? juce::String (argv[4]) : juce::String();
    outDir.createDirectory();

    afterglow::AfterglowProcessor processor;
    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    processor.getPresetManager().loadPresetByName (presetName);

    juce::String suffix;
    auto silence = false;
    for (int i = 5; i < argc; ++i)
    {
        const auto arg = juce::String (argv[i]);
        const auto key = arg.upToFirstOccurrenceOf ("=", false, false);
        const auto value = arg.fromFirstOccurrenceOf ("=", false, false);
        if (key == "name")
            suffix = "-" + value;
        else if (key == "silence")
            silence = value.getIntValue() != 0;
        else if (auto* param = processor.getState().getParameter (key))
            param->setValueNotifyingHost (param->convertTo0to1 (value.getFloatValue()));
        else
            std::fprintf (stderr, "Unknown parameter %s\n", key.toRawUTF8());
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    auto* afterglowEditor = dynamic_cast<afterglow::AfterglowEditor*> (editor.get());
    if (afterglowEditor == nullptr)
        return 1;

    // Play short plucked notes over a quiet sustained tone for about four and a half seconds (enough to fill every
    // display's history), advancing the animation 30 times per second in between,
    // as a host would. The picture is taken 0.45 s after the last note.
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    juce::Random random (3);
    constexpr double fs = 48000.0, noteEvery = 0.6, duration = 4.65, frame = 1.0 / 30.0;
    const double pitches[] { 110.0, 165.0, 220.0, 146.83 };
    double untilFrame = frame;

    for (int64_t done = 0; done < static_cast<int64_t> (duration * fs); done += 512)
    {
        for (int i = 0; i < 512; ++i)
        {
            const auto t = static_cast<double> (done + i) / fs;
            const auto note = static_cast<int> (t / noteEvery);
            const auto age = t - note * noteEvery;
            const auto f = pitches[note % 4];
            const auto w = juce::MathConstants<double>::twoPi * f * age;
            const auto pluck = (std::sin (w) + 0.5 * std::sin (2.0 * w) + 0.25 * std::sin (3.0 * w)) * 0.32 * std::exp (-age / 0.09);
            const auto pad = 0.015 * std::sin (juce::MathConstants<double>::twoPi * 110.0 * t);
            const auto v = silence ? 0.0f : static_cast<float> (pluck + pad) + 0.004f * (random.nextFloat() - 0.5f);
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }
        processor.processBlock (buffer, midi);

        untilFrame -= 512.0 / fs;
        if (untilFrame <= 0.0)
        {
            afterglowEditor->advanceAnimation (frame);
            untilFrame += frame;
        }
    }

    const auto image = afterglowEditor->renderSnapshot (scale, overlay);
    const auto file = outDir.getChildFile ("afterglow-" + presetName.replaceCharacter (' ', '-').toLowerCase() + (overlay.isNotEmpty() ? "-" + overlay : juce::String()) + suffix
                                           + "@" + juce::String (scale, 1) + "x.png");
    file.deleteFile();
    juce::FileOutputStream stream (file);
    juce::PNGImageFormat png;
    if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
        return 2;

    std::printf ("Wrote %s (%d x %d)\n", file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    editor.reset();
    return 0;
}
