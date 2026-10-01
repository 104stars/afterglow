#pragma once

#include "Controls.h"
#include "Visualisers.h"

namespace afterglow::ui
{
/** Bottom strip: input, EQ (cut filters and tone), output, global mix and limiter, with VU meters. */
class MasterStrip final : public juce::Component
{
public:
    MasterStrip (APVTS& state, dsp::EngineTelemetry& telemetry);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void refresh (double seconds);

private:
    void drawSectionCaption (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area) const;

    dsp::EngineTelemetry& telemetry;
    VuMeter inMeter { "IN" }, outMeter { "OUT" };
    LabelledKnob inGain, tone, width, outGain, mix;
    LedButton eqOn, lowHard, highHard, toneMode, limiter;
    RangeSlider cut;

    juce::Rectangle<int> inSection, eqSection, outSection, globalSection;
};

} // namespace afterglow::ui
