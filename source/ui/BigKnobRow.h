#pragma once

#include "Controls.h"

namespace afterglow::ui
{
/** The cream "big knob" panel: one main amount knob and power switch per module. */
class BigKnobRow final : public juce::Component
{
public:
    explicit BigKnobRow (APVTS& state);

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Updates the amber "effective value" markers that show the Magnitude scaling. */
    void refresh();

private:
    juce::Rectangle<int> cellBounds (size_t index) const;

    struct Cell
    {
        std::unique_ptr<LabelledKnob> knob;
        std::unique_ptr<LedButton> power;
        juce::String title;
        juce::String onParamId;
        std::atomic<float>* amount = nullptr;
        float shownEffective = -1.0f;
    };

    APVTS& state;
    std::array<Cell, 6> cells;
    std::atomic<float>* magnitude = nullptr;
};

} // namespace afterglow::ui
