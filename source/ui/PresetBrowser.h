#pragma once

#include "Controls.h"
#include "../presets/PresetManager.h"

namespace afterglow::ui
{
/** Preset browser overlay. It covers only the module bay, so the big knobs, Magnitude and the master section stay
    live while browsing. OK keeps the current state, Cancel restores what was there before the browser opened. */
class PresetBrowser final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit PresetBrowser (PresetManager& presets);

    void open();
    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    std::function<void()> onClose;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;

    void rebuildList();
    void selectFilter (int index);
    void updateInfo();
    void close (bool keep);
    int selectedPresetIndex() const;

    PresetManager& presets;
    PresetManager::Snapshot snapshot;

    juce::StringArray filters;
    int filterIndex = 0;
    std::vector<int> visible; // indices into presets.getPresets()
    juce::ListBox list;
    juce::OwnedArray<juce::TextButton> filterButtons;
    juce::TextButton okButton { "OK" }, cancelButton { "CANCEL" }, renameButton { "RENAME" }, deleteButton { "DELETE" },
        startupButton { "SET AS STARTUP" }, folderButton { "OPEN FOLDER" };
    juce::Rectangle<int> infoArea;
    bool suppressLoad = false;

    // Dialogs are owned here (not left to the desktop), so closing the plugin window or removing the plugin while
    // one is open closes it too, instead of leaving it pointing at a deleted editor and look-and-feel.
    std::unique_ptr<juce::AlertWindow> renameWindow;
    juce::ScopedMessageBox messageBox;
};

//======================================================================================================================
/** Modal dialog for naming and categorising a new user preset. */
class SavePresetDialog final : public juce::Component
{
public:
    explicit SavePresetDialog (PresetManager& presets);

    void open();
    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    std::function<void()> onClose;

private:
    void save();

    PresetManager& presets;
    juce::TextEditor name, description;
    juce::ComboBox category;
    juce::TextButton saveButton { "SAVE" }, cancelButton { "CANCEL" };
    juce::String error;
    juce::Rectangle<int> panel;
};

} // namespace afterglow::ui
