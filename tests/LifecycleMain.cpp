// Afterglow lifecycle test: creates and destroys the plugin and its editor the way hosts do, including the case
// that used to crash (removing the last instance, which shuts JUCE down, then adding a new one or quitting).
// Run it under AddressSanitizer to catch use-after-free; a plain run catches crashes and assertions.
// Exit code is non-zero if any check fails.

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cstdio>
#include <thread>

namespace
{
int failures = 0;

void check (bool condition, const char* what)
{
    if (! condition)
    {
        ++failures;
        std::printf ("  FAIL: %s\n", what);
    }
}

/** Plays a few blocks so the engine and its display telemetry are running. */
void playSomeAudio (afterglow::AfterglowProcessor& processor)
{
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    for (int block = 0; block < 40; ++block)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto v = 0.3f * std::sin (0.05f * static_cast<float> (block * 256 + i));
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }
        processor.processBlock (buffer, midi);
    }
}

/** Opens the editor, paints it (which builds the fonts and texture caches) and animates it. */
std::unique_ptr<juce::AudioProcessorEditor> openEditor (afterglow::AfterglowProcessor& processor)
{
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    if (auto* e = dynamic_cast<afterglow::AfterglowEditor*> (editor.get()))
    {
        for (int i = 0; i < 10; ++i)
            e->advanceAnimation (1.0 / 30.0);
        const auto image = e->renderSnapshot (1.0f);
        check (image.isValid(), "editor renders");
    }
    else
    {
        check (false, "editor has the expected type");
    }
    return editor;
}

/** Closes an editor the way plugin wrappers do: tell the processor first, then delete it. */
void closeEditor (afterglow::AfterglowProcessor& processor, std::unique_ptr<juce::AudioProcessorEditor>& editor)
{
    if (editor == nullptr)
        return;
    processor.editorBeingDeleted (editor.get());
    editor.reset();
}

std::unique_ptr<afterglow::AfterglowProcessor> makeProcessor()
{
    auto processor = std::make_unique<afterglow::AfterglowProcessor>();
    processor->setRateAndBufferSizeDetails (48000.0, 256);
    processor->prepareToPlay (48000.0, 256);
    return processor;
}

/** One host session: JUCE is initialised while at least one instance exists, as in a plugin wrapper. */
void session (const char* name, int instances, bool reverseOrder)
{
    std::printf ("== %s\n", name);
    juce::ScopedJuceInitialiser_GUI juce;

    std::vector<std::unique_ptr<afterglow::AfterglowProcessor>> processors;
    std::vector<std::unique_ptr<juce::AudioProcessorEditor>> editors;

    check (afterglow::ui::UiResources::current() == nullptr, "no UI resources exist before an editor is opened");

    for (int i = 0; i < instances; ++i)
    {
        processors.push_back (makeProcessor());
        playSomeAudio (*processors.back());
        editors.push_back (openEditor (*processors.back()));
        check (afterglow::ui::UiResources::current() != nullptr, "UI resources exist while an editor is open");
    }

    // The host saves and restores the session from a background thread while the UI changes presets.
    {
        auto& processor = *processors.front();
        std::atomic<bool> done { false };
        std::thread host ([&]
        {
            for (int i = 0; i < 200; ++i)
            {
                juce::MemoryBlock block;
                processor.getStateInformation (block);
                processor.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
            }
            done = true;
        });

        for (int i = 0; ! done; ++i)
            processor.getPresetManager().loadPreset (i % 8);

        host.join();
    }

    // Change the oversampling quality from the audio thread's point of view (automation), which must not call
    // back into the host from there.
    if (auto* quality = processors.front()->getState().getParameter ("quality"))
        quality->setValueNotifyingHost (0.0f);
    playSomeAudio (*processors.front());

    // Reopen one editor (closing and opening the plugin window).
    closeEditor (*processors.front(), editors.front());
    editors.front() = openEditor (*processors.front());

    // Remove the instances one by one (editor first, then the processor, as hosts do), in either order.
    for (size_t n = 0; n < processors.size(); ++n)
    {
        const auto i = reverseOrder ? processors.size() - 1 - n : n;
        closeEditor (*processors[i], editors[i]);
        processors[i]->releaseResources();
        processors[i].reset();
    }

    check (afterglow::ui::UiResources::current() == nullptr, "UI resources are released with the last editor");

}
} // namespace

int main()
{
    {
        std::printf ("== processing before prepareToPlay\n");
        juce::ScopedJuceInitialiser_GUI juce;
        afterglow::AfterglowProcessor processor;
        juce::AudioBuffer<float> buffer (2, 1024);
        buffer.clear();
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);
        check (buffer.getMagnitude (0, buffer.getNumSamples()) < 1.0e-9f, "unprepared processor passes silence through");
    }

    // Each session ends with JUCE shut down, as when the last instance is removed from a project; the next session
    // starts it again, as when a new instance is added. Quitting the host is the end of main (static destructors).
    session ("single instance", 1, false);
    session ("re-added after the last instance was removed", 1, false);
    session ("several instances", 3, false);
    session ("several instances, removed newest first", 2, true);
    session ("again after shutdown", 1, false);

    std::printf ("\n%s\n", failures == 0 ? "lifecycle: all checks passed" : "lifecycle: FAILED");
    return failures == 0 ? 0 : 1;
}
