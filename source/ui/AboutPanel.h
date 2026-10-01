#pragma once

#include "Controls.h"

namespace afterglow::ui
{
/** Credits, help and settings (oversampling quality and interface size). Opened from the logo. */
class AboutPanel final : public juce::Component
{
public:
    explicit AboutPanel (APVTS& state);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress& key) override;

    std::function<void (float)> onScaleChosen;
    std::function<void()> onClose;

private:
    juce::ComboBox quality;
    std::unique_ptr<APVTS::ComboBoxAttachment> qualityAttachment;
    juce::OwnedArray<juce::TextButton> scaleButtons;
    juce::TextButton closeButton { "CLOSE" };
};

} // namespace afterglow::ui
