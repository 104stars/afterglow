#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <vector>

namespace afterglow
{
/** Factory and user presets. User presets are XML files in Documents/Afterglow/Presets, so they can be
    copied between machines and shared. The global Quality setting is never stored in a preset. */
class PresetManager final : private juce::AudioProcessorValueTreeState::Listener
{
public:
    struct Preset
    {
        juce::String name, category, author, description;
        bool isFactory = true;
        juce::File file;
        std::vector<std::pair<juce::String, float>> values; // real (denormalised) parameter values
    };

    PresetManager (juce::AudioProcessorValueTreeState& state, juce::UndoManager* undoManager);
    ~PresetManager() override;

    const std::vector<Preset>& getPresets() const noexcept { return presets; }
    static juce::StringArray getCategories();

    void refreshUserPresets();

    bool loadPreset (int index);
    bool loadPresetByName (const juce::String& name);
    void loadNext();
    void loadPrevious();

    /** Saves the current settings as a user preset (overwrites a user preset with the same name). */
    bool saveUserPreset (const juce::String& name, const juce::String& category, const juce::String& description, juce::String& error);
    bool deleteUserPreset (int index);
    bool renameUserPreset (int index, const juce::String& newName, juce::String& error);

    int getCurrentIndex() const noexcept { return currentIndex; }
    juce::String getCurrentPresetName() const { return currentName; }
    void setCurrentPresetName (const juce::String& name, bool dirtyFlag);
    bool isDirty() const noexcept { return dirty.load(); }

    void setStartupPreset (int index);
    juce::String getStartupPresetName() const;
    void loadStartupPresetIfAny();

    /** Snapshot and restore of all parameter values, used by the preset browser's Cancel button. */
    struct Snapshot
    {
        std::vector<std::pair<juce::String, float>> values;
        juce::String name;
        int index = -1;
        bool dirty = false;
    };
    Snapshot takeSnapshot() const;
    void restoreSnapshot (const Snapshot& snapshot);

    /** Runs a function while parameter changes are not treated as user edits. */
    void runWhileLoading (const std::function<void()>& fn);

    static juce::File getUserPresetDirectory();
    static constexpr const char* fileExtension = ".afterglowpreset";

    std::function<void()> onPresetListChanged;

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void applyValues (const std::vector<std::pair<juce::String, float>>& values);
    std::vector<std::pair<juce::String, float>> captureValues() const;
    static bool readPresetFile (const juce::File& file, Preset& preset);
    std::unique_ptr<juce::PropertiesFile> openSettings() const;

    juce::AudioProcessorValueTreeState& state;
    juce::UndoManager* undo = nullptr;
    std::vector<Preset> presets;
    int currentIndex = -1;
    juce::String currentName { "Init" };
    std::atomic<bool> dirty { false };
    std::atomic<int> loadingDepth { 0 };
};

/** Built-in presets, defined in FactoryPresets.cpp. */
const std::vector<PresetManager::Preset>& getFactoryPresets();

} // namespace afterglow
