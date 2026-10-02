#pragma once

#include "AfterglowLookAndFeel.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace afterglow::ui
{
using APVTS = juce::AudioProcessorValueTreeState;

//======================================================================================================================
/** A rotary knob with a silkscreened label underneath, attached to a parameter. */
class LabelledKnob : public juce::Component
{
public:
    LabelledKnob (APVTS& state, const juce::String& paramId, const juce::String& labelText, const juce::String& style,
                  juce::Colour inkColour, const juce::String& tooltip = {});

    void resized() override;
    void paint (juce::Graphics& g) override;
    void parentHierarchyChanged() override;

    juce::Slider& getSlider() noexcept { return slider; }
    void setLabelHeight (float h) { labelHeight = h; resized(); }

    /** Re-targets the knob to another parameter (used by Sync toggles that swap Hz for note values). */
    void attachTo (const juce::String& paramId);

private:
    APVTS& state;
    juce::Slider slider;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
    juce::String label;
    juce::Colour ink;
    float labelHeight = 13.0f;
};

//======================================================================================================================
/** A linear slider (Magnitude, Flux, balance sliders) attached to a parameter. */
class ParamSlider : public juce::Slider
{
public:
    ParamSlider (APVTS& state, const juce::String& paramId, const juce::String& style, bool vertical = false, const juce::String& tooltip = {});
    void parentHierarchyChanged() override;

private:
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

//======================================================================================================================
/** Illuminated hardware push button for on/off parameters. */
class LedButton : public juce::Button
{
public:
    enum class Look { keycap, power, roundLed, nameplate };

    LedButton (APVTS& state, const juce::String& paramId, const juce::String& text, juce::Colour ledColour, Look look,
               const juce::String& tooltip = {});

    void paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown) override;
    void setInkColour (juce::Colour c) { ink = c; repaint(); }

    /** Legend shown while the button is off (for two-state keys such as TILT / MID). */
    void setOffText (const juce::String& legend) { offText = legend; repaint(); }

    /** Width needed by the nameplate look for its lamp and title. */
    int getNameplateWidth() const;

private:
    void paintNameplate (juce::Graphics& g, bool isMouseOver, bool isButtonDown);

    juce::String offText;
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
    juce::Colour led, ink { Colours::silkscreen };
    Look look;
};

//======================================================================================================================
/** Small display window showing a choice parameter, with arrow buttons and a click-to-open list. */
class TypeSelector : public juce::Component, public juce::SettableTooltipClient
{
public:
    TypeSelector (APVTS& state, const juce::String& paramId, juce::Colour phosphor, const juce::String& tooltip = {});

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;

private:
    void step (int delta);
    juce::Rectangle<float> leftArrow() const;
    juce::Rectangle<float> rightArrow() const;

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::StringArray choices;
    int index = 0;
    juce::Colour glow;
};

//======================================================================================================================
/** Small paper label showing a value while a custom control is dragged. Hosted by the nearest parent that has
    the "popupHost" property, so it is drawn above neighbouring components and scaled with the interface. */
class ValuePopup : public juce::Component
{
public:
    ValuePopup() { setInterceptsMouseClicks (false, false); }
    void show (juce::Component& owner, juce::Rectangle<float> anchorInOwner, const juce::String& text, bool preferSide);
    void hide();
    void paint (juce::Graphics& g) override;

private:
    juce::String text;
};

//======================================================================================================================
/** Two-thumb slider for a frequency range (Focus band, master Cut filters). */
class RangeSlider : public juce::Component, public juce::SettableTooltipClient
{
public:
    RangeSlider (APVTS& state, const juce::String& lowId, const juce::String& highId, bool vertical, juce::Colour glow,
                 const juce::String& tooltip = {});

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    /** Optional: positions treated as "off" are drawn without the lit band (used by the master Cut). */
    void setOffAtExtremes (bool shouldShowOff) { offAtExtremes = shouldShowOff; repaint(); }

private:
    enum class Thumb { none, low, high, both };

    float frequencyToAxis (float hz) const;
    float axisToFrequency (float position) const;

    juce::Rectangle<float> track() const;
    float valueToPosition (float normalised) const;
    float positionToValue (juce::Point<float> p) const;
    juce::Rectangle<float> thumbBounds (float normalised) const;
    Thumb thumbAt (juce::Point<float> p) const;

    juce::RangedAudioParameter& lowParam;
    juce::RangedAudioParameter& highParam;
    juce::ParameterAttachment lowAttachment, highAttachment;
    float lowValue = 0.0f, highValue = 1.0f; // positions on the shared log-frequency axis (0..1)
    float axisMin = 20.0f, axisMax = 20000.0f;
    bool vertical;
    juce::Colour glowColour;
    void updatePopup();

    ValuePopup popup;
    Thumb dragging = Thumb::none, hover = Thumb::none;
    float dragStartLow = 0.0f, dragStartHigh = 0.0f, dragStartValue = 0.0f;
    bool offAtExtremes = false;
};

} // namespace afterglow::ui
