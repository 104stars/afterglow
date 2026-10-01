#pragma once

#include "PluginProcessor.h"
#include "ui/AboutPanel.h"
#include "ui/AfterglowLookAndFeel.h"
#include "ui/BigKnobRow.h"
#include "ui/HeaderBar.h"
#include "ui/MasterStrip.h"
#include "ui/ModulePanels.h"
#include "ui/PresetBrowser.h"

namespace afterglow
{
/** The whole interface at its design size (1120 x 720). The editor scales this component as a single unit. */
class MainPanel final : public juce::Component
{
public:
    explicit MainPanel (AfterglowProcessor& processor);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void refresh (double seconds);

    std::function<void (float)> onScaleChosen;

    /** Opens an overlay by name ("browser", "about", "save"); used by the snapshot tool. */
    void showOverlay (const juce::String& name);

private:
    void openBrowser();
    void openSaveDialog();
    void openAbout();
    void closeOverlays();

    AfterglowProcessor& processor;
    ui::HeaderBar header;
    std::array<std::unique_ptr<ui::ModulePanel>, 6> modules;
    ui::BigKnobRow bigKnobs;
    ui::MasterStrip master;
    ui::PresetBrowser browser;
    ui::AboutPanel about;
    ui::SavePresetDialog saveDialog;
    juce::Rectangle<int> bayArea;
};

class AfterglowEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AfterglowEditor (AfterglowProcessor& processor);
    ~AfterglowEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void mouseDown (const juce::MouseEvent& e) override;

    /** Renders the interface into an image (used for documentation screenshots and tests). */
    juce::Image renderSnapshot (float scale, const juce::String& overlay = {});

private:
    void timerCallback() override;
    void applyScale (float scale);

    AfterglowProcessor& processor;
    ui::AfterglowLookAndFeel lookAndFeel;
    MainPanel content;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    double lastTick = 0.0;
    bool constructed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfterglowEditor)
};

} // namespace afterglow
