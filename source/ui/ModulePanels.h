#pragma once

#include "Controls.h"
#include "Visualisers.h"

namespace afterglow::ui
{
/** Perforated steel cover shown over a module that is switched off. Clicking it switches the module on. */
class Hatch final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Hatch (APVTS& state, const juce::String& onParamId, const juce::String& title);
    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent& e) override;

private:
    APVTS& state;
    juce::String paramId, title;
    juce::Image cache;
    float cacheScale = 0.0f;
};

//======================================================================================================================
/** One of the six module cartridges: display window, enamel faceplate with controls, Flux slider and hatch. */
class ModulePanel : public juce::Component
{
public:
    ModulePanel (APVTS& state, dsp::EngineTelemetry& telemetry, int moduleIndex, const juce::String& title,
                 const juce::String& onParamId, const juce::String& fluxParamId);
    ~ModulePanel() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Animation and state polling, called by the editor timer. */
    virtual void refresh (double seconds);

    ModuleDisplay* getDisplay() const noexcept { return display.get(); }

protected:
    /** Lays out the module-specific controls inside the faceplate area (below the display, above Flux). */
    virtual void layoutControls (juce::Rectangle<int> area) = 0;
    /** Draws module-specific silkscreen text (slider captions). */
    virtual void paintSilkscreen (juce::Graphics&) {}

    void drawCaption (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area) const;
    void drawBalanceCaption (juce::Graphics& g, const juce::String& left, const juce::String& right, juce::Rectangle<int> area) const;

    APVTS& state;
    dsp::EngineTelemetry& telemetry;
    const int moduleIndex;
    const juce::String title;
    juce::Colour enamel, phosphor, ink { Colours::silkscreen };
    std::unique_ptr<ModuleDisplay> display;
    ParamSlider flux;
    juce::Rectangle<int> fluxCaptionArea;

private:
    void updateHatch();
    void paintFaceplate (juce::Graphics& g, float scale);

    // The faceplate artwork is cached per scale by hand (not with setBufferedToImage, which would also cache the
    // display and controls and resample them every frame at fractional scales).
    juce::Image faceplate;
    float faceplateScale = 0.0f;

    Hatch hatch;
    std::atomic<float>* onParam = nullptr;
    bool lastOn = true;
    bool hatchInitialised = false;
};

//======================================================================================================================
class NoisePanel final : public ModulePanel
{
public:
    NoisePanel (APVTS&, dsp::EngineTelemetry&);

private:
    void layoutControls (juce::Rectangle<int> area) override;
    TypeSelector type;
    LabelledKnob tone, follow, duck;
    LedButton post;
};

/** Shared helper for modules whose rate knob can switch to tempo-synced note values. */
class SyncedRateKnob
{
public:
    SyncedRateKnob (APVTS& state, LabelledKnob& knob, const juce::String& rateId, const juce::String& divisionId, const juce::String& syncId);
    void refresh();

private:
    APVTS& state;
    LabelledKnob& knob;
    juce::String rateId, divisionId;
    std::atomic<float>* sync = nullptr;
    int lastState = -1;
};

class WobblePanel final : public ModulePanel
{
public:
    WobblePanel (APVTS&, dsp::EngineTelemetry&);
    void refresh (double seconds) override;

private:
    void layoutControls (juce::Rectangle<int> area) override;
    void paintSilkscreen (juce::Graphics& g) override;
    ParamSlider balance;
    LabelledKnob wowRate, flutterRate, mix;
    LedButton sync, stereo;
    SyncedRateKnob syncedRate;
    juce::Rectangle<int> balanceCaption;
};

class DistortPanel final : public ModulePanel
{
public:
    DistortPanel (APVTS&, dsp::EngineTelemetry&);

private:
    void layoutControls (juce::Rectangle<int> area) override;
    void paintSilkscreen (juce::Graphics& g) override;
    TypeSelector type;
    RangeSlider focus;
    LabelledKnob tone, mix;
    juce::Rectangle<int> focusCaption;
};

class DigitalPanel final : public ModulePanel
{
public:
    DigitalPanel (APVTS&, dsp::EngineTelemetry&);

private:
    void layoutControls (juce::Rectangle<int> area) override;
    void paintSilkscreen (juce::Graphics& g) override;
    ParamSlider balance;
    RangeSlider focus;
    LedButton cut, compand;
    LabelledKnob smooth, mix;
    juce::Rectangle<int> balanceCaption, focusCaption;
};

class SpacePanel final : public ModulePanel
{
public:
    SpacePanel (APVTS&, dsp::EngineTelemetry&);

private:
    void layoutControls (juce::Rectangle<int> area) override;
    void paintSilkscreen (juce::Graphics& g) override;
    TypeSelector type;
    RangeSlider focus;
    LabelledKnob decay, preDelay;
    LedButton stereo;
    juce::Rectangle<int> focusCaption;
};

class MagneticPanel final : public ModulePanel
{
public:
    MagneticPanel (APVTS&, dsp::EngineTelemetry&);
    void refresh (double seconds) override;

private:
    void layoutControls (juce::Rectangle<int> area) override;
    void paintSilkscreen (juce::Graphics& g) override;
    ParamSlider balance;
    LabelledKnob rate, dropouts;
    LedButton sync, stereo;
    SyncedRateKnob syncedRate;
    juce::Rectangle<int> balanceCaption;
};

} // namespace afterglow::ui
