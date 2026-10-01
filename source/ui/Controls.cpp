#include "Controls.h"

namespace afterglow::ui
{
namespace
{
    constexpr float rotaryStart = juce::MathConstants<float>::pi * 1.25f;
    constexpr float rotaryEnd = juce::MathConstants<float>::pi * 2.75f;

    void configurePopup (juce::Slider& s)
    {
        // Value bubbles live inside the plugin window (separate desktop windows misbehave in some hosts).
        s.setPopupDisplayEnabled (true, false, s.findParentComponentOfClass<juce::AudioProcessorEditor>(), 1200);
    }

    double defaultValueOf (APVTS& state, const juce::String& id)
    {
        if (auto* p = state.getParameter (id))
            return p->convertFrom0to1 (p->getDefaultValue());
        return 0.0;
    }

    void drawGlowText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, const juce::Font& font, juce::Colour colour)
    {
        g.setFont (font);
        g.setColour (colour.withAlpha (0.18f));
        for (auto offset : { juce::Point<float> (-1.0f, 0.0f), juce::Point<float> (1.0f, 0.0f), juce::Point<float> (0.0f, -1.0f), juce::Point<float> (0.0f, 1.0f) })
            g.drawText (text, area.translated (offset.x, offset.y), juce::Justification::centred, false);
        g.setColour (colour);
        g.drawText (text, area, juce::Justification::centred, false);
    }
} // namespace

//======================================================================================================================
LabelledKnob::LabelledKnob (APVTS& s, const juce::String& paramId, const juce::String& labelText, const juce::String& style,
                            juce::Colour inkColour, const juce::String& tooltip)
    : state (s), label (labelText), ink (inkColour)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters (rotaryStart, rotaryEnd, true);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.getProperties().set (StyleId::property, style);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, inkColour);
    slider.setMouseDragSensitivity (style == StyleId::bigKnob ? 360 : 240);
    slider.setVelocityModeParameters (0.6, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    slider.setTooltip (tooltip);
    configurePopup (slider);
    addAndMakeVisible (slider);
    attachTo (paramId);

    if (style == StyleId::bigKnob)
        labelHeight = 0.0f;
}

void LabelledKnob::attachTo (const juce::String& paramId)
{
    attachment.reset();
    attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);
    slider.setDoubleClickReturnValue (true, defaultValueOf (state, paramId));
}

void LabelledKnob::parentHierarchyChanged()
{
    configurePopup (slider);
}

void LabelledKnob::resized()
{
    auto area = getLocalBounds();
    area.removeFromBottom (juce::roundToInt (labelHeight));
    slider.setBounds (area);
}

void LabelledKnob::paint (juce::Graphics& g)
{
    if (labelHeight <= 0.0f || label.isEmpty())
        return;

    const auto area = getLocalBounds().toFloat().removeFromBottom (labelHeight + 2.0f);
    drawEngravedText (g, label, area, Fonts::get().label (labelHeight), ink, juce::Justification::centredTop, ink.getPerceivedBrightness() > 0.5f);
}

//======================================================================================================================
ParamSlider::ParamSlider (APVTS& state, const juce::String& paramId, const juce::String& style, bool vertical, const juce::String& tooltip)
{
    setSliderStyle (vertical ? juce::Slider::LinearVertical : juce::Slider::LinearHorizontal);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    getProperties().set (StyleId::property, style);
    setVelocityModeParameters (0.6, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    setTooltip (tooltip);
    configurePopup (*this);
    attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, *this);
    setDoubleClickReturnValue (true, defaultValueOf (state, paramId));
}

void ParamSlider::parentHierarchyChanged()
{
    juce::Slider::parentHierarchyChanged();
    configurePopup (*this);
}

//======================================================================================================================
LedButton::LedButton (APVTS& state, const juce::String& paramId, const juce::String& buttonText, juce::Colour ledColour, Look l,
                      const juce::String& tooltip)
    : juce::Button (buttonText), led (ledColour), look (l)
{
    setClickingTogglesState (true);
    setTooltip (tooltip);
    attachment = std::make_unique<APVTS::ButtonAttachment> (state, paramId, *this);
}

int LedButton::getNameplateWidth() const
{
    const auto textWidth = juce::GlyphArrangement::getStringWidth (Fonts::get().labelBold (16.0f), getButtonText());
    return juce::roundToInt (8.0f + 7.0f + textWidth + 2.0f * 11.0f);
}

void LedButton::paintNameplate (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    // The module name engraved on the cream panel doubles as its on/off switch (like RC-20):
    // an indicator lamp, the title, and a faint engraved plate that strengthens on hover.
    const auto on = getToggleState();
    auto area = getLocalBounds().toFloat().reduced (0.5f);
    if (isButtonDown)
        area.translate (0.0f, 0.5f);

    const auto outline = isMouseOver ? 0.24f : 0.12f;
    g.setColour (juce::Colours::black.withAlpha (outline));
    g.drawRoundedRectangle (area, 4.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (isMouseOver ? 0.7f : 0.5f));
    g.drawLine (area.getX() + 4.0f, area.getBottom() + 0.5f, area.getRight() - 4.0f, area.getBottom() + 0.5f, 1.0f);
    if (isMouseOver)
    {
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillRoundedRectangle (area.reduced (1.0f), 3.5f);
    }

    const auto font = Fonts::get().labelBold (16.0f);
    const auto textWidth = juce::GlyphArrangement::getStringWidth (font, getButtonText());
    const auto groupWidth = 8.0f + 7.0f + textWidth;
    const auto x0 = area.getCentreX() - groupWidth * 0.5f;
    const auto lamp = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ x0 + 4.0f, area.getCentreY() });

    // Recessed indicator lamp.
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.fillEllipse (lamp.expanded (1.2f).translated (0.0f, 0.6f));
    g.setColour (juce::Colour (0xff4a463a));
    g.fillEllipse (lamp.expanded (1.0f));
    if (on)
    {
        g.setColour (led.withAlpha (0.35f));
        g.fillEllipse (lamp.expanded (3.5f));
        juce::ColourGradient lit (led.brighter (0.5f), lamp.getCentreX(), lamp.getY(), led.darker (0.3f), lamp.getCentreX(), lamp.getBottom(), false);
        g.setGradientFill (lit);
    }
    else
    {
        juce::ColourGradient dark (juce::Colour (0xff2a2922), lamp.getCentreX(), lamp.getY(), juce::Colour (0xff3b3a2c), lamp.getCentreX(), lamp.getBottom(), false);
        g.setGradientFill (dark);
    }
    g.fillEllipse (lamp);
    g.setColour (juce::Colours::white.withAlpha (on ? 0.6f : 0.15f));
    g.fillEllipse (lamp.withSizeKeepingCentre (3.0f, 2.0f).translated (-1.0f, -1.5f));

    const auto textArea = juce::Rectangle<float> (x0 + 15.0f, area.getY(), textWidth + 2.0f, area.getHeight());
    drawEngravedText (g, getButtonText(), textArea, font, Colours::ink.withAlpha (on ? 0.85f : 0.4f), juce::Justification::centredLeft, false);
}

void LedButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    if (look == Look::nameplate)
    {
        paintNameplate (g, isMouseOver, isButtonDown);
        return;
    }

    const auto on = getToggleState();
    auto area = getLocalBounds().toFloat().reduced (1.0f);

    if (look == Look::roundLed)
    {
        const auto d = std::min (area.getWidth(), area.getHeight() * 0.62f);
        auto cap = juce::Rectangle<float> (d, d).withCentre ({ area.getCentreX(), area.getY() + d * 0.5f + 1.0f });
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (cap.expanded (1.5f).translated (0.0f, 1.0f));
        juce::ColourGradient bezel (juce::Colour (0xffcfd0cf), cap.getX(), cap.getY(), juce::Colour (0xff4a4b4d), cap.getX(), cap.getBottom(), false);
        g.setGradientFill (bezel);
        g.fillEllipse (cap);

        const auto lens = cap.reduced (d * 0.2f).translated (0.0f, isButtonDown ? 0.6f : 0.0f);
        if (on)
        {
            juce::ColourGradient halo (led.withAlpha (0.55f), lens.getCentreX(), lens.getCentreY(), led.withAlpha (0.0f), lens.getCentreX(), lens.getY() - d * 0.6f, true);
            g.setGradientFill (halo);
            g.fillEllipse (lens.expanded (d * 0.55f));
        }
        juce::ColourGradient lensGrad (on ? led.brighter (0.6f) : led.darker (0.85f).withMultipliedSaturation (0.5f), lens.getCentreX(), lens.getY(),
                                       on ? led.darker (0.2f) : juce::Colour (0xff1a1a1a), lens.getCentreX(), lens.getBottom(), false);
        g.setGradientFill (lensGrad);
        g.fillEllipse (lens);
        g.setColour (juce::Colours::white.withAlpha (on ? 0.55f : 0.18f));
        g.fillEllipse (lens.withSizeKeepingCentre (lens.getWidth() * 0.4f, lens.getHeight() * 0.3f).translated (-lens.getWidth() * 0.12f, -lens.getHeight() * 0.2f));

        const auto textArea = area.withTop (cap.getBottom() + 1.0f);
        drawEngravedText (g, getButtonText(), textArea, Fonts::get().label (std::min (13.0f, textArea.getHeight())), ink.withAlpha (isMouseOver ? 1.0f : 0.92f),
                          juce::Justification::centredTop, ink.getPerceivedBrightness() > 0.5f);
        return;
    }

    // Key-cap and power buttons.
    const auto corner = 3.0f;
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (area.translated (0.0f, 1.5f), corner);

    if (isButtonDown)
        area.translate (0.0f, 1.0f);

    juce::ColourGradient body (juce::Colour (0xff4a4b4f), area.getX(), area.getY(), juce::Colour (0xff1f2023), area.getX(), area.getBottom(), false);
    g.setGradientFill (body);
    g.fillRoundedRectangle (area, corner);
    g.setColour (juce::Colours::white.withAlpha (isMouseOver ? 0.2f : 0.12f));
    g.drawLine (area.getX() + corner, area.getY() + 0.8f, area.getRight() - corner, area.getY() + 0.8f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (area, corner, 1.0f);

    if (look == Look::power)
    {
        // LED window at the top and a power glyph.
        auto ledArea = juce::Rectangle<float> (area.getWidth() * 0.5f, 3.5f).withCentre ({ area.getCentreX(), area.getY() + 5.0f });
        if (on)
        {
            g.setColour (led.withAlpha (0.3f));
            g.fillRoundedRectangle (ledArea.expanded (3.0f, 2.5f), 3.0f);
        }
        g.setColour (on ? led : led.darker (0.9f).withAlpha (0.6f));
        g.fillRoundedRectangle (ledArea, 1.5f);

        const auto iconArea = area.withTrimmedTop (9.0f).reduced (area.getWidth() * 0.28f, 3.0f);
        const auto r = std::min (iconArea.getWidth(), iconArea.getHeight()) * 0.5f;
        const auto c = iconArea.getCentre();
        juce::Path power;
        power.addCentredArc (c.x, c.y, r, r, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
        g.setColour (on ? Colours::silkscreen : Colours::silkscreen.withAlpha (0.55f));
        g.strokePath (power, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.drawLine (c.x, c.y - r * 1.15f, c.x, c.y - r * 0.15f, 1.4f);
        return;
    }

    auto ledArea = juce::Rectangle<float> (area.getWidth() * 0.42f, 2.8f).withCentre ({ area.getCentreX(), area.getY() + 4.5f });
    if (on)
    {
        g.setColour (led.withAlpha (0.28f));
        g.fillRoundedRectangle (ledArea.expanded (3.0f, 2.5f), 3.0f);
    }
    g.setColour (on ? led : led.darker (0.9f).withAlpha (0.55f));
    g.fillRoundedRectangle (ledArea, 1.4f);

    const auto textArea = area.withTrimmedTop (7.0f);
    const auto legend = (! on && offText.isNotEmpty()) ? offText : getButtonText();
    g.setFont (Fonts::get().label (13.0f));
    g.setColour (on ? Colours::silkscreen : Colours::silkscreen.withAlpha (0.7f));
    g.drawText (legend, textArea, juce::Justification::centred, false);
}

//======================================================================================================================
TypeSelector::TypeSelector (APVTS& state, const juce::String& paramId, juce::Colour phosphor, const juce::String& tooltip)
    : param (*state.getParameter (paramId)),
      attachment (param, [this] (float v) { index = juce::roundToInt (v); repaint(); }, state.undoManager),
      glow (phosphor)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&param))
        choices = choice->choices;

    setTooltip (tooltip);
    attachment.sendInitialUpdate();
}

juce::Rectangle<float> TypeSelector::leftArrow() const { return getLocalBounds().toFloat().removeFromLeft (18.0f); }
juce::Rectangle<float> TypeSelector::rightArrow() const { return getLocalBounds().toFloat().removeFromRight (18.0f); }

void TypeSelector::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const auto window = area.reduced (17.0f, 1.0f);

    // Recessed glass window.
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.fillRoundedRectangle (window.translated (0.0f, 1.0f), 3.0f);
    g.setColour (Colours::glassDark);
    g.fillRoundedRectangle (window, 3.0f);
    juce::ColourGradient inner (juce::Colours::black, window.getX(), window.getY(), juce::Colours::transparentBlack, window.getX(), window.getY() + 6.0f, false);
    g.setGradientFill (inner);
    g.fillRoundedRectangle (window, 3.0f);

    const auto text = choices.isEmpty() ? param.getCurrentValueAsText() : choices[index];
    drawGlowText (g, text.toUpperCase(), window.reduced (2.0f, 0.0f), Fonts::get().display (std::min (15.0f, window.getHeight() * 0.78f)), glow);

    // Glass reflection.
    juce::ColourGradient reflection (juce::Colours::white.withAlpha (0.1f), window.getX(), window.getY(), juce::Colours::transparentWhite, window.getX(), window.getCentreY(), false);
    g.setGradientFill (reflection);
    g.fillRoundedRectangle (window.withHeight (window.getHeight() * 0.5f), 3.0f);

    // Arrow keys.
    for (auto [r, dir] : { std::pair { leftArrow().reduced (2.0f, 3.0f), -1.0f }, std::pair { rightArrow().reduced (2.0f, 3.0f), 1.0f } })
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.0f), 2.5f);
        juce::ColourGradient key (juce::Colour (0xff47484c), r.getX(), r.getY(), juce::Colour (0xff1e1f22), r.getX(), r.getBottom(), false);
        g.setGradientFill (key);
        g.fillRoundedRectangle (r, 2.5f);
        juce::Path tri;
        const auto c = r.getCentre();
        const auto s = std::min (r.getWidth(), r.getHeight()) * 0.22f;
        tri.addTriangle (c.x - dir * s, c.y - s * 1.2f, c.x - dir * s, c.y + s * 1.2f, c.x + dir * s * 1.1f, c.y);
        g.setColour (Colours::silkscreen.withAlpha (0.85f));
        g.fillPath (tri);
    }
}

void TypeSelector::step (int delta)
{
    const auto count = std::max (1, choices.size());
    const auto next = (index + delta + count) % count;
    attachment.setValueAsCompleteGesture (static_cast<float> (next));
}

void TypeSelector::mouseDown (const juce::MouseEvent& e)
{
    if (leftArrow().contains (e.position))
    {
        step (-1);
        return;
    }

    if (rightArrow().contains (e.position))
    {
        step (1);
        return;
    }

    juce::PopupMenu menu;
    for (int i = 0; i < choices.size(); ++i)
        menu.addItem (i + 1, choices[i], true, i == index);

    juce::Component::SafePointer<TypeSelector> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                        [safe] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                safe->attachment.setValueAsCompleteGesture (static_cast<float> (result - 1));
                        });
}

void TypeSelector::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (std::abs (wheel.deltaY) > 0.0f)
        step (wheel.deltaY > 0.0f ? -1 : 1);
}

//======================================================================================================================
RangeSlider::RangeSlider (APVTS& state, const juce::String& lowId, const juce::String& highId, bool isVertical, juce::Colour glow,
                          const juce::String& tooltip)
    : lowParam (*state.getParameter (lowId)),
      highParam (*state.getParameter (highId)),
      lowAttachment (lowParam, [this] (float v) { lowValue = frequencyToAxis (v); repaint(); }, state.undoManager),
      highAttachment (highParam, [this] (float v) { highValue = frequencyToAxis (v); repaint(); }, state.undoManager),
      vertical (isVertical),
      glowColour (glow)
{
    // Both thumbs share one logarithmic frequency axis spanning both parameters' ranges.
    axisMin = std::max (1.0f, lowParam.getNormalisableRange().start);
    axisMax = std::max (axisMin * 2.0f, highParam.getNormalisableRange().end);
    setTooltip (tooltip);
    lowAttachment.sendInitialUpdate();
    highAttachment.sendInitialUpdate();
}

float RangeSlider::frequencyToAxis (float hz) const
{
    return juce::jlimit (0.0f, 1.0f, std::log (std::max (hz, axisMin) / axisMin) / std::log (axisMax / axisMin));
}

float RangeSlider::axisToFrequency (float position) const
{
    return axisMin * std::pow (axisMax / axisMin, juce::jlimit (0.0f, 1.0f, position));
}

juce::Rectangle<float> RangeSlider::track() const
{
    auto area = getLocalBounds().toFloat();
    return vertical ? area.reduced (0.0f, 7.0f) : area.reduced (8.0f, 0.0f);
}

float RangeSlider::valueToPosition (float n) const
{
    const auto t = track();
    return vertical ? t.getBottom() - n * t.getHeight() : t.getX() + n * t.getWidth();
}

float RangeSlider::positionToValue (juce::Point<float> p) const
{
    const auto t = track();
    const auto v = vertical ? (t.getBottom() - p.y) / t.getHeight() : (p.x - t.getX()) / t.getWidth();
    return juce::jlimit (0.0f, 1.0f, v);
}

juce::Rectangle<float> RangeSlider::thumbBounds (float n) const
{
    const auto pos = valueToPosition (n);
    const auto area = getLocalBounds().toFloat();
    return vertical ? juce::Rectangle<float> (std::min (area.getWidth() - 2.0f, 20.0f), 10.0f).withCentre ({ area.getCentreX(), pos })
                    : juce::Rectangle<float> (10.0f, std::min (area.getHeight() - 2.0f, 20.0f)).withCentre ({ pos, area.getCentreY() });
}

RangeSlider::Thumb RangeSlider::thumbAt (juce::Point<float> p) const
{
    const auto lowHit = thumbBounds (lowValue).expanded (3.0f).contains (p);
    const auto highHit = thumbBounds (highValue).expanded (3.0f).contains (p);

    if (lowHit && highHit)
        return positionToValue (p) < 0.5f * (lowValue + highValue) ? Thumb::low : Thumb::high;
    if (lowHit)
        return Thumb::low;
    if (highHit)
        return Thumb::high;

    const auto v = positionToValue (p);
    if (v > lowValue && v < highValue)
        return Thumb::both;
    return Thumb::none;
}

void RangeSlider::paint (juce::Graphics& g)
{
    const auto t = track();
    const auto slot = vertical ? juce::Rectangle<float> (5.0f, t.getHeight()).withCentre (t.getCentre())
                               : juce::Rectangle<float> (t.getWidth(), 5.0f).withCentre (t.getCentre());
    AfterglowLookAndFeel::drawSlot (g, slot);

    // The selected band glows.
    const auto p1 = valueToPosition (lowValue);
    const auto p2 = valueToPosition (highValue);
    auto band = vertical ? juce::Rectangle<float>::leftTopRightBottom (slot.getX(), p2, slot.getRight(), p1)
                         : juce::Rectangle<float>::leftTopRightBottom (p1, slot.getY(), p2, slot.getBottom());
    band = band.reduced (1.0f);

    const auto fullyOpen = offAtExtremes && lowValue <= 0.001f && highValue >= 0.999f;
    if (! band.isEmpty() && ! fullyOpen)
    {
        g.setColour (glowColour.withAlpha (0.18f));
        g.fillRoundedRectangle (band.expanded (vertical ? 3.0f : 1.0f, vertical ? 1.0f : 3.0f), 3.0f);
        g.setColour (glowColour.withAlpha (0.8f));
        g.fillRoundedRectangle (band, 1.5f);
    }

    AfterglowLookAndFeel::drawFaderCap (g, thumbBounds (lowValue), ! vertical);
    AfterglowLookAndFeel::drawFaderCap (g, thumbBounds (highValue), ! vertical);

}

void RangeSlider::updatePopup()
{
    const auto active = dragging != Thumb::none ? dragging : hover;
    if (active == Thumb::none)
    {
        popup.hide();
        return;
    }

    juce::String text;
    juce::Rectangle<float> anchor;

    if (active == Thumb::low)
    {
        text = lowParam.getCurrentValueAsText();
        anchor = thumbBounds (lowValue);
    }
    else if (active == Thumb::high)
    {
        text = highParam.getCurrentValueAsText();
        anchor = thumbBounds (highValue);
    }
    else
    {
        text = lowParam.getCurrentValueAsText() + " - " + highParam.getCurrentValueAsText();
        anchor = thumbBounds (0.5f * (lowValue + highValue));
    }

    popup.show (*this, anchor, text, vertical);
}

void RangeSlider::mouseDown (const juce::MouseEvent& e)
{
    dragging = thumbAt (e.position);

    if (dragging == Thumb::none)
    {
        // Clicking on the track grabs the nearest thumb and moves it there.
        const auto v = positionToValue (e.position);
        dragging = std::abs (v - lowValue) < std::abs (v - highValue) ? Thumb::low : Thumb::high;
    }

    dragStartLow = lowValue;
    dragStartHigh = highValue;
    dragStartValue = positionToValue (e.position);

    if (dragging == Thumb::low || dragging == Thumb::both)
        lowAttachment.beginGesture();
    if (dragging == Thumb::high || dragging == Thumb::both)
        highAttachment.beginGesture();

    mouseDrag (e);
}

void RangeSlider::mouseDrag (const juce::MouseEvent& e)
{
    constexpr float minGap = 0.04f;
    const auto v = positionToValue (e.position);
    const auto& lowRange = lowParam.getNormalisableRange();
    const auto& highRange = highParam.getNormalisableRange();
    auto setLow = [&] (float position)
    {
        const auto hz = juce::jlimit (lowRange.start, lowRange.end, axisToFrequency (position));
        lowValue = frequencyToAxis (hz);
        lowAttachment.setValueAsPartOfGesture (hz);
    };
    auto setHigh = [&] (float position)
    {
        const auto hz = juce::jlimit (highRange.start, highRange.end, axisToFrequency (position));
        highValue = frequencyToAxis (hz);
        highAttachment.setValueAsPartOfGesture (hz);
    };

    if (dragging == Thumb::low)
    {
        setLow (std::min (v, highValue - minGap));
    }
    else if (dragging == Thumb::high)
    {
        setHigh (std::max (v, lowValue + minGap));
    }
    else if (dragging == Thumb::both)
    {
        const auto lowLimit = frequencyToAxis (lowRange.start), lowTop = frequencyToAxis (lowRange.end);
        const auto highLimit = frequencyToAxis (highRange.end), highBottom = frequencyToAxis (highRange.start);
        auto delta = v - dragStartValue;
        delta = juce::jlimit (std::max (lowLimit - dragStartLow, highBottom - dragStartHigh),
                              std::min (highLimit - dragStartHigh, lowTop - dragStartLow), delta);
        setLow (dragStartLow + delta);
        setHigh (dragStartHigh + delta);
    }

    repaint();
    updatePopup();
}

void RangeSlider::mouseUp (const juce::MouseEvent&)
{
    if (dragging == Thumb::low || dragging == Thumb::both)
        lowAttachment.endGesture();
    if (dragging == Thumb::high || dragging == Thumb::both)
        highAttachment.endGesture();

    dragging = Thumb::none;
    repaint();
    updatePopup();
}

void RangeSlider::mouseDoubleClick (const juce::MouseEvent&)
{
    lowAttachment.setValueAsCompleteGesture (lowParam.convertFrom0to1 (lowParam.getDefaultValue()));
    highAttachment.setValueAsCompleteGesture (highParam.convertFrom0to1 (highParam.getDefaultValue()));
    dragging = Thumb::none;
}

void RangeSlider::mouseMove (const juce::MouseEvent& e)
{
    const auto h = thumbAt (e.position);
    if (h != hover)
    {
        hover = h;
        repaint();
        updatePopup();
    }
}

void RangeSlider::mouseExit (const juce::MouseEvent&)
{
    hover = Thumb::none;
    repaint();
    if (dragging == Thumb::none)
        popup.hide();
}

//======================================================================================================================
void ValuePopup::show (juce::Component& owner, juce::Rectangle<float> anchorInOwner, const juce::String& newText, bool preferSide)
{
    auto* host = owner.getParentComponent();
    while (host != nullptr && ! static_cast<bool> (host->getProperties()["popupHost"]))
        host = host->getParentComponent();

    if (host == nullptr)
        return;

    if (getParentComponent() != host)
        host->addChildComponent (this);

    text = newText;
    const auto font = Fonts::get().display (13.0f);
    const auto w = juce::GlyphArrangement::getStringWidth (font, text) + 12.0f;
    const auto anchor = host->getLocalArea (&owner, anchorInOwner);
    auto box = juce::Rectangle<float> (w, 18.0f);
    box = preferSide ? box.withCentre ({ anchor.getRight() + w * 0.5f + 6.0f, anchor.getCentreY() })
                     : box.withCentre ({ anchor.getCentreX(), anchor.getY() - 13.0f });
    setBounds (box.toNearestInt().constrainedWithin (host->getLocalBounds()));
    toFront (false);
    setVisible (true);
    repaint();
}

void ValuePopup::hide()
{
    setVisible (false);
}

void ValuePopup::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (area.reduced (0.5f).translated (0.0f, 1.0f), 3.0f);
    g.setColour (Colours::cream);
    g.fillRoundedRectangle (area.reduced (0.5f, 1.0f), 3.0f);
    g.setColour (Colours::ink);
    g.setFont (Fonts::get().display (13.0f));
    g.drawText (text, area, juce::Justification::centred, false);
}

} // namespace afterglow::ui
