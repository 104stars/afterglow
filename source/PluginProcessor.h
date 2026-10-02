#pragma once

#include "Parameters.h"
#include "dsp/AfterglowEngine.h"
#include "presets/PresetManager.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace afterglow
{
class AfterglowProcessor final : public juce::AudioProcessor,
                                 private juce::AudioProcessorValueTreeState::Listener,
                                 private juce::AsyncUpdater
{
public:
    AfterglowProcessor();
    ~AfterglowProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Afterglow"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 14.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
    juce::UndoManager& getUndoManager() noexcept { return undoManager; }
    PresetManager& getPresetManager() noexcept { return *presetManager; }
    dsp::EngineTelemetry& getTelemetry() noexcept { return engine.getTelemetry(); }

    /** Editor scale factor, stored with the session. */
    float getUiScale() const;
    void setUiScale (float scale);

    static constexpr const char* uiScaleProperty = "uiScale";
    static constexpr const char* stateType = "AfterglowState"; // root tag of the saved state

private:
    /** Direct pointers to every parameter value, resolved once so the audio thread never searches by name. */
    struct ParamPointers
    {
        std::atomic<float>* magnitude {}; std::atomic<float>* mix {}; std::atomic<float>* limiter {}; std::atomic<float>* quality {};
        std::atomic<float>* noiseOn {}; std::atomic<float>* noiseAmount {}; std::atomic<float>* noiseType {}; std::atomic<float>* noiseTone {};
        std::atomic<float>* noiseFollow {}; std::atomic<float>* noiseDuck {}; std::atomic<float>* noisePost {}; std::atomic<float>* noiseFlux {};
        std::atomic<float>* wobbleOn {}; std::atomic<float>* wobbleAmount {}; std::atomic<float>* wobbleBalance {}; std::atomic<float>* wobbleWowRate {};
        std::atomic<float>* wobbleFlutterRate {}; std::atomic<float>* wobbleSync {}; std::atomic<float>* wobbleDivision {}; std::atomic<float>* wobbleStereo {};
        std::atomic<float>* wobbleMix {}; std::atomic<float>* wobbleFlux {};
        std::atomic<float>* distortOn {}; std::atomic<float>* distortAmount {}; std::atomic<float>* distortType {}; std::atomic<float>* distortFocusLow {};
        std::atomic<float>* distortFocusHigh {}; std::atomic<float>* distortTone {}; std::atomic<float>* distortMix {}; std::atomic<float>* distortFlux {};
        std::atomic<float>* digitalOn {}; std::atomic<float>* digitalAmount {}; std::atomic<float>* digitalBalance {}; std::atomic<float>* digitalSmooth {};
        std::atomic<float>* digitalFocusLow {}; std::atomic<float>* digitalFocusHigh {}; std::atomic<float>* digitalCut {}; std::atomic<float>* digitalCompand {};
        std::atomic<float>* digitalMix {}; std::atomic<float>* digitalFlux {};
        std::atomic<float>* spaceOn {}; std::atomic<float>* spaceAmount {}; std::atomic<float>* spaceType {}; std::atomic<float>* spaceDecay {};
        std::atomic<float>* spacePreDelay {}; std::atomic<float>* spaceFocusLow {}; std::atomic<float>* spaceFocusHigh {}; std::atomic<float>* spaceStereo {};
        std::atomic<float>* spaceFlux {};
        std::atomic<float>* magneticOn {}; std::atomic<float>* magneticAmount {}; std::atomic<float>* magneticBalance {}; std::atomic<float>* magneticRate {};
        std::atomic<float>* magneticSync {}; std::atomic<float>* magneticDivision {}; std::atomic<float>* magneticDropouts {}; std::atomic<float>* magneticStereo {};
        std::atomic<float>* magneticFlux {};
        std::atomic<float>* inGain {}; std::atomic<float>* eqOn {}; std::atomic<float>* lowCut {}; std::atomic<float>* lowCutHard {};
        std::atomic<float>* highCut {}; std::atomic<float>* highCutHard {}; std::atomic<float>* tone {}; std::atomic<float>* toneMode {};
        std::atomic<float>* width {}; std::atomic<float>* outGain {};
    };

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void applyStateOnMessageThread (const juce::ValueTree& tree);
    void cacheParameterPointers();
    dsp::EngineParams snapshotParameters (const dsp::TransportInfo& transport) const noexcept;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState state;
    std::unique_ptr<PresetManager> presetManager;

    dsp::AfterglowEngine engine;
    ParamPointers p;
    std::array<int, 4> latencyForOrder {};
    std::atomic<int> requestedOrder { 2 };
    juce::AudioBuffer<float> monoScratch;
    std::array<dsp::DelayBuffer, 2> bypassDelay;

    // Audio passes through untouched until prepareToPlay has sized everything (some hosts process first).
    std::atomic<bool> prepared { false };

    // Work that must happen on the message thread, requested from wherever the host called us.
    std::atomic<bool> latencyChangePending { false };
    juce::CriticalSection pendingStateLock;
    juce::ValueTree pendingState; // a session restored on another thread, waiting for the message thread

    // Kept outside the parameter tree, so the editor never writes to the tree while the host saves it.
    std::atomic<float> uiScale { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfterglowProcessor)
};

} // namespace afterglow
