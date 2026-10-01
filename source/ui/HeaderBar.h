#pragma once

#include "Controls.h"
#include "../presets/PresetManager.h"

namespace afterglow::ui
{
/** Small key-cap button that draws a vector icon. */
class IconButton final : public juce::Button
{
public:
    enum class Icon { left, right, undo, redo, menu };

    IconButton (Icon icon, const juce::String& tooltip);
    void paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown) override;

private:
    Icon icon;
};

/** Top bar: logo, preset display and navigation, undo/redo, and the Magnitude fader. */
class HeaderBar final : public juce::Component
{
public:
    HeaderBar (APVTS& state, PresetManager& presets, juce::UndoManager& undo);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& e) override;

    /** Polls preset state (name, modified flag), called by the editor timer. */
    void refresh();

    std::function<void()> onBrowse, onSave, onAbout;

private:
    juce::Rectangle<int> logoArea, displayArea, magnitudeCaption;

    PresetManager& presets;
    juce::UndoManager& undo;
    IconButton prev { IconButton::Icon::left, "Previous preset" };
    IconButton next { IconButton::Icon::right, "Next preset" };
    IconButton undoButton { IconButton::Icon::undo, "Undo (Ctrl+Z)" };
    IconButton redoButton { IconButton::Icon::redo, "Redo (Ctrl+Shift+Z)" };
    juce::TextButton browse { "BROWSE" }, save { "SAVE" };
    ParamSlider magnitude;

    juce::String shownName, shownCategory;
    bool shownDirty = false;
};

} // namespace afterglow::ui
