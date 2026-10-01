#pragma once

#include "Theme.h"

namespace afterglow::ui
{
/** Slider styles, selected per slider through the "style" property. */
namespace StyleId
{
    inline const juce::Identifier property { "agStyle" };
    inline const juce::String smallKnob { "small" };
    inline const juce::String bipolarKnob { "bipolar" };
    inline const juce::String bigKnob { "big" };
    inline const juce::String masterKnob { "master" };
    inline const juce::String magnitudeFader { "magnitude" };
    inline const juce::String miniSlider { "mini" };
    inline const juce::String fluxSlider { "flux" };
} // namespace StyleId

class AfterglowLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    AfterglowLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;

    juce::Font getSliderPopupFont (juce::Slider&) override;
    int getSliderPopupPlacement (juce::Slider&) override;
    void drawBubble (juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip, const juce::Rectangle<float>& body) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& idealWidth, int& idealHeight) override;

    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour, bool isMouseOverButton, bool isButtonDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;

    /** Shared knob renderers, also used by components that draw knobs themselves. */
    static void drawSmallKnob (juce::Graphics&, juce::Rectangle<float> area, float angle, bool bipolar, juce::Colour tickColour, float startAngle, float endAngle);
    static void drawBigKnob (juce::Graphics&, juce::Rectangle<float> area, float angle, float startAngle, float endAngle, float effectiveAngle, bool showEffective);
    static void drawFaderCap (juce::Graphics&, juce::Rectangle<float> cap, bool vertical);
    static void drawSlot (juce::Graphics&, juce::Rectangle<float> slot);

private:
    static void drawBrushedDisc (juce::Graphics&, juce::Point<float> centre, float radius, float brightness);
    static void drawKnurledRing (juce::Graphics&, juce::Point<float> centre, float outerRadius, float innerRadius, float angle, int ridges, juce::Colour base, float contrast);
};

} // namespace afterglow::ui
