#include "AfterglowLookAndFeel.h"
#include <map>

namespace afterglow::ui
{
namespace
{
    juce::Point<float> polar (juce::Point<float> centre, float radius, float angle)
    {
        // JUCE rotary angles: 0 is 12 o'clock, increasing clockwise.
        return { centre.x + radius * std::sin (angle), centre.y - radius * std::cos (angle) };
    }

    float physicalScale (juce::Graphics& g)
    {
        return std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    }

    /** Brushed (conical) metal disc, cached per pixel size because it is made of many wedges. */
    juce::Image brushedDiscImage (int pixelDiameter, float brightness)
    {
        static std::map<std::pair<int, int>, juce::Image> cache;
        const auto key = std::make_pair (pixelDiameter, juce::roundToInt (brightness * 100.0f));
        if (auto it = cache.find (key); it != cache.end())
            return it->second;

        juce::Image image (juce::Image::ARGB, pixelDiameter, pixelDiameter, true);
        {
            juce::Graphics g (image);
            const auto d = static_cast<float> (pixelDiameter);
            const auto c = juce::Point<float> (d * 0.5f, d * 0.5f);
            constexpr int wedges = 96;

            for (int k = 0; k < wedges; ++k)
            {
                const auto a0 = juce::MathConstants<float>::twoPi * static_cast<float> (k) / wedges;
                const auto a1 = a0 + juce::MathConstants<float>::twoPi / wedges + 0.01f;
                const auto mid = 0.5f * (a0 + a1);
                auto v = 0.6f + 0.27f * std::cos (2.0f * (mid + 0.7f)) + 0.05f * std::cos (6.0f * mid + 1.3f);
                v *= brightness;
                juce::Path wedge;
                wedge.addPieSegment (0.0f, 0.0f, d, d, a0, a1, 0.0f);
                g.setColour (juce::Colour::fromFloatRGBA (v * 0.97f, v * 0.98f, v, 1.0f));
                g.fillPath (wedge);
            }

            // Fine concentric machining rings.
            for (float r = d * 0.06f; r < d * 0.5f; r += std::max (1.0f, d * 0.035f))
            {
                g.setColour (juce::Colours::black.withAlpha (0.05f));
                g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 0.6f);
            }

            juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.22f), c.x - d * 0.3f, c.y - d * 0.35f,
                                        juce::Colours::transparentWhite, c.x, c.y, true);
            g.setGradientFill (sheen);
            g.fillEllipse (0.0f, 0.0f, d, d);
        }

        if (cache.size() > 64)
            cache.clear();
        cache[key] = image;
        return image;
    }
} // namespace

AfterglowLookAndFeel::AfterglowLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1c1b1a));
    setColour (juce::PopupMenu::textColourId, Colours::silkscreen);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::amber.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, Colours::amber);
    setColour (juce::TextEditor::textColourId, Colours::amber);
    setColour (juce::TextEditor::highlightColourId, Colours::amber.withAlpha (0.3f));
    setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);
    setColour (juce::CaretComponent::caretColourId, Colours::amber);
    setColour (juce::ComboBox::textColourId, Colours::amber);
    setColour (juce::ComboBox::arrowColourId, Colours::amber);
    setColour (juce::TextButton::textColourOffId, Colours::silkscreen);
    setColour (juce::TextButton::textColourOnId, Colours::amber);
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, Colours::amber.withAlpha (0.5f));
    setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff1c1b1a));
    setColour (juce::AlertWindow::textColourId, Colours::silkscreen);
    setColour (juce::AlertWindow::outlineColourId, Colours::amber.withAlpha (0.4f));
    setColour (juce::Label::textColourId, Colours::silkscreen);
    setColour (juce::TooltipWindow::textColourId, Colours::ink);
    setColour (juce::Slider::rotarySliderOutlineColourId, Colours::silkscreen);
}

//======================================================================================================================
void AfterglowLookAndFeel::drawBrushedDisc (juce::Graphics& g, juce::Point<float> centre, float radius, float brightness)
{
    const auto scale = physicalScale (g);
    const auto pixels = std::max (4, juce::roundToInt (radius * 2.0f * scale));
    const auto image = brushedDiscImage (pixels, brightness);
    const auto area = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

    juce::Graphics::ScopedSaveState save (g);
    juce::Path clip;
    clip.addEllipse (area);
    g.reduceClipRegion (clip);
    g.drawImage (image, area, juce::RectanglePlacement::stretchToFit);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (area.reduced (0.3f), std::max (0.6f, radius * 0.04f));
}

void AfterglowLookAndFeel::drawKnurledRing (juce::Graphics& g, juce::Point<float> centre, float outerRadius, float innerRadius,
                                            float angle, int ridges, juce::Colour base, float contrast)
{
    const auto outer = juce::Rectangle<float> (outerRadius * 2.0f, outerRadius * 2.0f).withCentre (centre);

    juce::ColourGradient body (base.brighter (0.5f), outer.getX(), outer.getY(), base.darker (0.7f), outer.getRight(), outer.getBottom(), false);
    g.setGradientFill (body);
    g.fillEllipse (outer);

    // Ridges rotate with the knob; their highlights follow a light source at the top left.
    const auto lightAngle = -0.8f;
    const auto ridgeWidth = juce::MathConstants<float>::twoPi * outerRadius / static_cast<float> (ridges) * 0.42f;

    for (int k = 0; k < ridges; ++k)
    {
        const auto a = angle + juce::MathConstants<float>::twoPi * static_cast<float> (k) / static_cast<float> (ridges);
        const auto facing = std::cos (a - lightAngle);
        const auto p1 = polar (centre, innerRadius + (outerRadius - innerRadius) * 0.45f, a);
        const auto p2 = polar (centre, outerRadius - 0.4f, a);
        g.setColour (facing > 0.0f ? juce::Colours::white.withAlpha (contrast * facing * 0.7f)
                                   : juce::Colours::black.withAlpha (contrast * -facing));
        g.drawLine ({ p1, p2 }, ridgeWidth);
    }

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (outer, std::max (0.8f, outerRadius * 0.03f));
}

void AfterglowLookAndFeel::drawSmallKnob (juce::Graphics& g, juce::Rectangle<float> area, float angle, bool bipolar,
                                          juce::Colour tickColour, float startAngle, float endAngle)
{
    const auto size = std::min (area.getWidth(), area.getHeight());
    const auto centre = area.getCentre();
    const auto r = size * 0.36f;

    // Printed scale.
    for (int i = 0; i <= 10; ++i)
    {
        const auto a = startAngle + (endAngle - startAngle) * static_cast<float> (i) / 10.0f;
        const auto major = i == 0 || i == 10 || (bipolar && i == 5);
        const auto inner = r * 1.17f;
        const auto outer = r * (major ? 1.38f : 1.29f);
        g.setColour (tickColour.withAlpha (major ? 0.95f : 0.6f));
        g.drawLine ({ polar (centre, inner, a), polar (centre, outer, a) }, major ? std::max (1.0f, size * 0.025f) : std::max (0.7f, size * 0.016f));
    }

    // Contact shadow.
    juce::ColourGradient shadow (juce::Colours::black.withAlpha (0.65f), centre.x, centre.y + r * 0.2f,
                                 juce::Colours::transparentBlack, centre.x, centre.y + r * 1.35f, true);
    g.setGradientFill (shadow);
    g.fillEllipse (juce::Rectangle<float> (r * 2.7f, r * 2.7f).withCentre (centre.translated (0.0f, r * 0.2f)));

    // Bakelite skirt with knurling.
    drawKnurledRing (g, centre, r, r * 0.8f, angle, 32, juce::Colour (0xff1d1c1b), 0.32f);

    // Concave top face.
    const auto face = juce::Rectangle<float> (r * 1.62f, r * 1.62f).withCentre (centre);
    juce::ColourGradient faceGrad (juce::Colour (0xff121212), face.getX(), face.getY(), juce::Colour (0xff3a3937), face.getX(), face.getBottom(), false);
    g.setGradientFill (faceGrad);
    g.fillEllipse (face);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (face.reduced (0.5f), 0.8f);

    // Brushed aluminium cap.
    drawBrushedDisc (g, centre, r * 0.5f, 0.95f);

    // Pointer line.
    const auto p1 = polar (centre, r * 0.56f, angle);
    const auto p2 = polar (centre, r * 0.93f, angle);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawLine (juce::Line<float> (p1, p2).withShortenedStart (-0.5f).withShortenedEnd (-0.5f), r * 0.17f);
    g.setColour (Colours::silkscreen);
    g.drawLine ({ p1, p2 }, r * 0.11f);
}

void AfterglowLookAndFeel::drawBigKnob (juce::Graphics& g, juce::Rectangle<float> area, float angle, float startAngle, float endAngle,
                                        float effectiveAngle, bool showEffective)
{
    const auto size = std::min (area.getWidth(), area.getHeight());
    const auto centre = area.getCentre();
    const auto r = size * 0.33f;

    // Printed 0-10 scale on the cream panel.
    for (int i = 0; i <= 20; ++i)
    {
        const auto a = startAngle + (endAngle - startAngle) * static_cast<float> (i) / 20.0f;
        const auto major = i % 2 == 0;
        g.setColour (Colours::ink.withAlpha (major ? 0.85f : 0.45f));
        g.drawLine ({ polar (centre, r * 1.12f, a), polar (centre, r * (major ? 1.27f : 1.2f), a) }, major ? std::max (1.0f, size * 0.014f) : std::max (0.7f, size * 0.009f));

        if (major)
        {
            const auto label = juce::String (i / 2);
            const auto p = polar (centre, r * 1.43f, a);
            g.setColour (Colours::ink.withAlpha (0.8f));
            g.setFont (Fonts::get().labelMedium (size * 0.085f));
            g.drawText (label, juce::Rectangle<float> (size * 0.14f, size * 0.1f).withCentre (p), juce::Justification::centred, false);
        }
    }

    // Shadow cast on the panel.
    juce::ColourGradient shadow (juce::Colours::black.withAlpha (0.55f), centre.x, centre.y + r * 0.25f,
                                 juce::Colours::transparentBlack, centre.x, centre.y + r * 1.4f, true);
    g.setGradientFill (shadow);
    g.fillEllipse (juce::Rectangle<float> (r * 2.8f, r * 2.8f).withCentre (centre.translated (0.0f, r * 0.25f)));

    // Machined aluminium skirt.
    drawKnurledRing (g, centre, r, r * 0.84f, angle, 64, juce::Colour (0xff9fa2a5), 0.28f);
    const auto skirtTop = juce::Rectangle<float> (r * 1.78f, r * 1.78f).withCentre (centre);
    juce::ColourGradient skirtGrad (juce::Colour (0xffd9dbdc), skirtTop.getX(), skirtTop.getY(), juce::Colour (0xff6b6e71), skirtTop.getRight(), skirtTop.getBottom(), false);
    g.setGradientFill (skirtGrad);
    g.fillEllipse (skirtTop);

    // Skirt index mark.
    g.setColour (juce::Colour (0xff1a1a1a));
    g.drawLine ({ polar (centre, r * 0.8f, angle), polar (centre, r * 0.97f, angle) }, std::max (1.2f, r * 0.06f));

    // Black glossy body.
    const auto body = juce::Rectangle<float> (r * 1.5f, r * 1.5f).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (body.expanded (r * 0.04f).translated (0.0f, r * 0.04f));
    juce::ColourGradient bodyGrad (juce::Colour (0xff3b3b3d), body.getX(), body.getY(), juce::Colour (0xff060606), body.getRight(), body.getBottom(), false);
    g.setGradientFill (bodyGrad);
    g.fillEllipse (body);
    juce::ColourGradient gloss (juce::Colours::white.withAlpha (0.28f), body.getX() + body.getWidth() * 0.3f, body.getY() + body.getHeight() * 0.12f,
                                juce::Colours::transparentWhite, body.getX() + body.getWidth() * 0.45f, body.getY() + body.getHeight() * 0.55f, true);
    g.setGradientFill (gloss);
    g.fillEllipse (body.reduced (r * 0.06f));

    // Chrome centre cap.
    drawBrushedDisc (g, centre, r * 0.36f, 1.05f);

    // Indicator.
    const auto p1 = polar (centre, r * 0.42f, angle);
    const auto p2 = polar (centre, r * 0.72f, angle);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawLine ({ p1.translated (0.0f, 0.8f), p2.translated (0.0f, 0.8f) }, r * 0.1f);
    g.setColour (juce::Colour (0xfffaf3e0));
    g.drawLine ({ p1, p2 }, r * 0.075f);

    // Amber marker showing the value after Magnitude scaling.
    if (showEffective)
    {
        juce::Path marker;
        const auto tip = polar (centre, r * 1.06f, effectiveAngle);
        const auto baseL = polar (centre, r * 1.2f, effectiveAngle - 0.07f);
        const auto baseR = polar (centre, r * 1.2f, effectiveAngle + 0.07f);
        marker.addTriangle (tip, baseL, baseR);
        g.setColour (juce::Colour (0xffd9771f));
        g.fillPath (marker);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.strokePath (marker, juce::PathStrokeType (0.6f));
    }
}

void AfterglowLookAndFeel::drawSlot (juce::Graphics& g, juce::Rectangle<float> slot)
{
    const auto corner = std::min (slot.getWidth(), slot.getHeight()) * 0.5f;
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.fillRoundedRectangle (slot.translated (0.0f, 1.0f), corner);
    g.setColour (juce::Colour (0xff050505));
    g.fillRoundedRectangle (slot, corner);
    juce::ColourGradient inner (juce::Colours::black, slot.getX(), slot.getY(), juce::Colour (0xff2a2a2a), slot.getX(), slot.getBottom(), false);
    g.setGradientFill (inner);
    g.fillRoundedRectangle (slot.reduced (0.8f), corner);
}

void AfterglowLookAndFeel::drawFaderCap (juce::Graphics& g, juce::Rectangle<float> cap, bool vertical)
{
    const auto corner = std::min (cap.getWidth(), cap.getHeight()) * 0.18f;

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (cap.translated (0.0f, cap.getHeight() * 0.12f).expanded (1.0f), corner);

    juce::ColourGradient body (juce::Colour (0xffeeeeec), cap.getX(), cap.getY(), juce::Colour (0xff6c6f72), cap.getX(), cap.getBottom(), false);
    body.addColour (0.48, juce::Colour (0xffb7b9bb));
    body.addColour (0.52, juce::Colour (0xff8d9093));
    g.setGradientFill (body);
    g.fillRoundedRectangle (cap, corner);

    // Grip grooves across the cap.
    const auto grooves = 5;
    for (int i = 0; i < grooves; ++i)
    {
        const auto t = (static_cast<float> (i) + 1.0f) / (grooves + 1.0f);
        if (vertical)
        {
            const auto y = cap.getY() + cap.getHeight() * t;
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.drawHorizontalLine (juce::roundToInt (y), cap.getX() + 2.0f, cap.getRight() - 2.0f);
        }
        else
        {
            const auto x = cap.getX() + cap.getWidth() * t;
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.drawLine (x, cap.getY() + 2.5f, x, cap.getBottom() - 2.5f, 0.8f);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawLine (x + 0.8f, cap.getY() + 2.5f, x + 0.8f, cap.getBottom() - 2.5f, 0.6f);
        }
    }

    // Centre index line.
    g.setColour (juce::Colour (0xff1e1e1e));
    if (vertical)
        g.fillRect (juce::Rectangle<float> (cap.getWidth() - 4.0f, 1.6f).withCentre (cap.getCentre()));
    else
        g.fillRect (juce::Rectangle<float> (1.8f, cap.getHeight() - 5.0f).withCentre (cap.getCentre()));

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (cap, corner, 0.8f);
}

//======================================================================================================================
void AfterglowLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                             float startAngle, float endAngle, juce::Slider& slider)
{
    const auto style = slider.getProperties()[StyleId::property].toString();
    const auto area = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y), static_cast<float> (width), static_cast<float> (height));
    const auto angle = startAngle + pos * (endAngle - startAngle);

    if (style == StyleId::bigKnob)
    {
        const auto effective = static_cast<float> (static_cast<double> (slider.getProperties().getWithDefault ("effective", pos)));
        const auto effAngle = startAngle + effective * (endAngle - startAngle);
        drawBigKnob (g, area, angle, startAngle, endAngle, effAngle, std::abs (effective - pos) > 0.004f);
        return;
    }

    drawSmallKnob (g, area, angle, style == StyleId::bipolarKnob, slider.findColour (juce::Slider::rotarySliderOutlineColourId), startAngle, endAngle);
}

void AfterglowLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float,
                                             float, juce::Slider::SliderStyle sliderStyle, juce::Slider& slider)
{
    const auto style = slider.getProperties()[StyleId::property].toString();
    const auto area = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y), static_cast<float> (width), static_cast<float> (height));
    const auto vertical = sliderStyle == juce::Slider::LinearVertical;

    if (style == StyleId::magnitudeFader)
    {
        const auto slot = juce::Rectangle<float> (area.getWidth(), 7.0f).withCentre (area.getCentre());
        drawSlot (g, slot);

        // Lit portion of the groove.
        const auto lit = slot.withRight (sliderPos).reduced (1.5f);
        if (lit.getWidth() > 0.0f)
        {
            g.setColour (Colours::amber.withAlpha (0.55f));
            g.fillRoundedRectangle (lit, lit.getHeight() * 0.5f);
            g.setColour (Colours::amber.withAlpha (0.12f));
            g.fillRoundedRectangle (lit.expanded (2.0f, 3.0f), 3.0f);
        }

        const auto cap = juce::Rectangle<float> (22.0f, area.getHeight() - 4.0f).withCentre ({ sliderPos, area.getCentreY() });
        drawFaderCap (g, cap, false);
        return;
    }

    if (style == StyleId::fluxSlider || style == StyleId::miniSlider)
    {
        const auto isFlux = style == StyleId::fluxSlider;
        const auto thickness = isFlux ? 4.0f : 5.0f;
        const auto slot = vertical ? juce::Rectangle<float> (thickness, area.getHeight() - 8.0f).withCentre (area.getCentre())
                                   : juce::Rectangle<float> (area.getWidth() - 8.0f, thickness).withCentre (area.getCentre());
        drawSlot (g, slot);

        if (isFlux && ! vertical)
        {
            const auto lit = slot.withRight (sliderPos).reduced (1.0f);
            if (lit.getWidth() > 1.0f)
            {
                const auto glow = slider.findColour (juce::Slider::trackColourId);
                g.setColour (glow.withAlpha (0.75f));
                g.fillRoundedRectangle (lit, lit.getHeight() * 0.5f);
            }
        }

        if (! isFlux && ! vertical)
        {
            // Centre detent ticks above and below the slot (balance sliders).
            g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId).withAlpha (0.75f));
            const auto cx = area.getCentreX();
            g.fillRect (juce::Rectangle<float> (cx - 0.6f, area.getY(), 1.2f, slot.getY() - area.getY() - 2.0f));
            g.fillRect (juce::Rectangle<float> (cx - 0.6f, slot.getBottom() + 2.0f, 1.2f, area.getBottom() - slot.getBottom() - 2.0f));
        }

        const auto capW = isFlux ? 11.0f : 13.0f;
        const auto capH = std::min (area.getHeight() - 2.0f, isFlux ? 15.0f : 17.0f);
        const auto cap = vertical ? juce::Rectangle<float> (capH, capW).withCentre ({ area.getCentreX(), sliderPos })
                                  : juce::Rectangle<float> (capW, capH).withCentre ({ sliderPos, area.getCentreY() });
        drawFaderCap (g, cap, vertical);
        return;
    }

    LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0, 0, sliderStyle, slider);
}

juce::Font AfterglowLookAndFeel::getSliderPopupFont (juce::Slider&) { return Fonts::get().display (14.0f); }

int AfterglowLookAndFeel::getSliderPopupPlacement (juce::Slider&) { return juce::BubbleComponent::above; }

void AfterglowLookAndFeel::drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&, const juce::Rectangle<float>& body)
{
    // Same paper-label look as the tooltips (the value text is drawn with the tooltip text colour).
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (body.translated (0.0f, 1.5f), 4.0f);
    g.setColour (Colours::cream);
    g.fillRoundedRectangle (body, 4.0f);
    g.setColour (Colours::ink.withAlpha (0.55f));
    g.drawRoundedRectangle (body.reduced (0.5f), 4.0f, 1.0f);
}

//======================================================================================================================
void AfterglowLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto area = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));
    juce::ColourGradient bg (juce::Colour (0xff24221f), 0.0f, 0.0f, juce::Colour (0xff151413), 0.0f, static_cast<float> (height), false);
    g.setGradientFill (bg);
    g.fillRect (area);
    g.setColour (Colours::amber.withAlpha (0.35f));
    g.drawRect (area, 1.0f);
}

void AfterglowLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                              bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                              const juce::String& shortcutKeyText, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (Colours::silkscreen.withAlpha (0.15f));
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    auto r = area.toFloat().reduced (3.0f, 1.0f);
    if (isHighlighted && isActive)
    {
        g.setColour (Colours::amber.withAlpha (0.18f));
        g.fillRoundedRectangle (r, 3.0f);
    }

    g.setColour (! isActive ? Colours::silkscreen.withAlpha (0.35f) : (isHighlighted || isTicked ? Colours::amber : Colours::silkscreen));
    g.setFont (getPopupMenuFont());

    auto textArea = r.reduced (8.0f, 0.0f);
    if (isTicked)
    {
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ textArea.getX() + 3.0f, textArea.getCentreY() }));
    }
    textArea.removeFromLeft (14.0f);
    g.drawText (text, textArea, juce::Justification::centredLeft, true);

    if (shortcutKeyText.isNotEmpty())
        g.drawText (shortcutKeyText, textArea, juce::Justification::centredRight, true);

    if (hasSubMenu)
    {
        juce::Path arrow;
        const auto c = juce::Point<float> (r.getRight() - 8.0f, r.getCentreY());
        arrow.addTriangle (c.x - 3.0f, c.y - 4.0f, c.x - 3.0f, c.y + 4.0f, c.x + 2.0f, c.y);
        g.fillPath (arrow);
    }
}

juce::Font AfterglowLookAndFeel::getPopupMenuFont() { return Fonts::get().labelMedium (17.0f); }

void AfterglowLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& idealWidth, int& idealHeight)
{
    LookAndFeel_V4::getIdealPopupMenuItemSize (text, isSeparator, standardMenuItemHeight, idealWidth, idealHeight);
    idealHeight = isSeparator ? 9 : 24;
    idealWidth += 24;
}

//======================================================================================================================
juce::Rectangle<int> AfterglowLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    const auto font = Fonts::get().labelMedium (15.0f);
    juce::AttributedString s;
    s.append (tipText, font, Colours::ink);
    juce::TextLayout layout;
    layout.createLayout (s, 260.0f);
    const auto w = juce::roundToInt (layout.getWidth()) + 20;
    const auto h = juce::roundToInt (layout.getHeight()) + 14;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}

void AfterglowLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto area = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));
    g.setColour (Colours::cream);
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour (Colours::ink.withAlpha (0.5f));
    g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 1.0f);

    juce::AttributedString s;
    s.append (text, Fonts::get().labelMedium (15.0f), Colours::ink);
    juce::TextLayout layout;
    layout.createLayout (s, static_cast<float> (width) - 20.0f);
    layout.draw (g, area.reduced (10.0f, 7.0f));
}

void AfterglowLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    const auto area = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));
    g.setColour (juce::Colour (0xff0d0e0d));
    g.fillRoundedRectangle (area, 3.0f);
    juce::ColourGradient inner (juce::Colours::black.withAlpha (0.6f), 0.0f, 0.0f, juce::Colours::transparentBlack, 0.0f, 6.0f, false);
    g.setGradientFill (inner);
    g.fillRoundedRectangle (area, 3.0f);
}

void AfterglowLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    const auto area = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height)).reduced (0.5f);
    g.setColour (editor.hasKeyboardFocus (true) ? Colours::amber.withAlpha (0.7f) : Colours::silkscreen.withAlpha (0.2f));
    g.drawRoundedRectangle (area, 3.0f, 1.0f);
}

void AfterglowLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto area = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));
    g.setColour (juce::Colour (0xff0d0e0d));
    g.fillRoundedRectangle (area, 3.0f);
    g.setColour (box.hasKeyboardFocus (true) ? Colours::amber.withAlpha (0.7f) : Colours::silkscreen.withAlpha (0.2f));
    g.drawRoundedRectangle (area.reduced (0.5f), 3.0f, 1.0f);

    juce::Path arrow;
    const auto c = juce::Point<float> (area.getRight() - 12.0f, area.getCentreY());
    arrow.addTriangle (c.x - 4.0f, c.y - 2.0f, c.x + 4.0f, c.y - 2.0f, c.x, c.y + 3.0f);
    g.setColour (Colours::amber);
    g.fillPath (arrow);
}

juce::Font AfterglowLookAndFeel::getComboBoxFont (juce::ComboBox&) { return Fonts::get().labelMedium (16.0f); }

void AfterglowLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool isMouseOverButton, bool isButtonDown)
{
    // Rubberised hardware push button.
    auto area = button.getLocalBounds().toFloat().reduced (1.0f);
    const auto corner = 3.5f;

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (area.translated (0.0f, 1.5f), corner);

    if (isButtonDown)
        area = area.translated (0.0f, 1.0f);

    juce::ColourGradient body (juce::Colour (isButtonDown ? 0xff26262a : 0xff45464b), area.getX(), area.getY(),
                               juce::Colour (isButtonDown ? 0xff1b1b1e : 0xff222326), area.getX(), area.getBottom(), false);
    g.setGradientFill (body);
    g.fillRoundedRectangle (area, corner);

    g.setColour (juce::Colours::white.withAlpha (isMouseOverButton ? 0.18f : 0.1f));
    g.drawLine (area.getX() + corner, area.getY() + 0.8f, area.getRight() - corner, area.getY() + 0.8f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawRoundedRectangle (area, corner, 1.0f);

    if (button.getToggleState())
    {
        g.setColour (Colours::amber.withAlpha (0.25f));
        g.fillRoundedRectangle (area.reduced (1.0f), corner);
    }
}

juce::Font AfterglowLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return Fonts::get().label (std::min (17.0f, static_cast<float> (buttonHeight) * 0.62f));
}

void AfterglowLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                                          int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    const auto thumb = isScrollbarVertical
        ? juce::Rectangle<float> (static_cast<float> (x) + 3.0f, static_cast<float> (thumbStartPosition), static_cast<float> (width) - 6.0f, static_cast<float> (thumbSize))
        : juce::Rectangle<float> (static_cast<float> (thumbStartPosition), static_cast<float> (y) + 3.0f, static_cast<float> (thumbSize), static_cast<float> (height) - 6.0f);
    g.setColour (Colours::amber.withAlpha (isMouseDown ? 0.7f : (isMouseOver ? 0.55f : 0.35f)));
    g.fillRoundedRectangle (thumb, std::min (thumb.getWidth(), thumb.getHeight()) * 0.5f);
}

} // namespace afterglow::ui
