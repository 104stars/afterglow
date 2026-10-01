// Renders the Afterglow interface to PNG files (for documentation and visual checks).
// Usage: AfterglowSnapshot <output-dir> [scale] [preset name] [overlay: browser | about | save]

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "snapshots");
    const auto scale = argc > 2 ? juce::String (argv[2]).getFloatValue() : 1.0f;
    const auto presetName = argc > 3 ? juce::String (argv[3]) : juce::String ("Lo-Fi Study Beats");
    const auto overlay = argc > 4 ? juce::String (argv[4]) : juce::String();
    outDir.createDirectory();

    afterglow::AfterglowProcessor processor;
    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    processor.getPresetManager().loadPresetByName (presetName);

    // Run some audio so the meters and displays have something to show.
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    juce::Random random (3);
    for (int block = 0; block < 120; ++block)
    {
        for (int i = 0; i < 512; ++i)
        {
            const auto t = static_cast<double> (block * 512 + i) / 48000.0;
            const auto v = 0.3f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)) + 0.05f * (random.nextFloat() - 0.5f);
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }
        processor.processBlock (buffer, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    auto* afterglowEditor = dynamic_cast<afterglow::AfterglowEditor*> (editor.get());
    if (afterglowEditor == nullptr)
        return 1;

    const auto image = afterglowEditor->renderSnapshot (scale, overlay);
    const auto file = outDir.getChildFile ("afterglow-" + presetName.replaceCharacter (' ', '-').toLowerCase() + (overlay.isNotEmpty() ? "-" + overlay : juce::String())
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
