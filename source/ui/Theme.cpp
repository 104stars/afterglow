#include "Theme.h"
#include "AfterglowBinaryData.h"

namespace afterglow::ui
{
Fonts& Fonts::get()
{
    static Fonts instance;
    return instance;
}

Fonts::Fonts()
{
    using namespace AfterglowBinaryData;
    medium = juce::Typeface::createSystemTypefaceFor (BarlowCondensedMedium_ttf, static_cast<size_t> (BarlowCondensedMedium_ttfSize));
    semiBold = juce::Typeface::createSystemTypefaceFor (BarlowCondensedSemiBold_ttf, static_cast<size_t> (BarlowCondensedSemiBold_ttfSize));
    bold = juce::Typeface::createSystemTypefaceFor (BarlowCondensedBold_ttf, static_cast<size_t> (BarlowCondensedBold_ttfSize));
    mono = juce::Typeface::createSystemTypefaceFor (ShareTechMonoRegular_ttf, static_cast<size_t> (ShareTechMonoRegular_ttfSize));
    scriptFace = juce::Typeface::createSystemTypefaceFor (YellowtailRegular_ttf, static_cast<size_t> (YellowtailRegular_ttfSize));
}

namespace
{
    juce::Font makeFont (const juce::Typeface::Ptr& face, float height, float kerning = 0.0f)
    {
        if (face == nullptr)
            return juce::Font (juce::FontOptions (height));
        return juce::Font (juce::FontOptions (face).withHeight (height).withKerningFactor (kerning));
    }
} // namespace

juce::Font Fonts::label (float height) const { return makeFont (semiBold, height, 0.06f); }
juce::Font Fonts::labelBold (float height) const { return makeFont (bold, height, 0.08f); }
juce::Font Fonts::labelMedium (float height) const { return makeFont (medium, height, 0.03f); }
juce::Font Fonts::display (float height) const { return makeFont (mono, height, 0.02f); }
juce::Font Fonts::script (float height) const { return makeFont (scriptFace, height); }

void drawEngravedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, const juce::Font& font,
                       juce::Colour colour, juce::Justification justification, bool darkShadow)
{
    g.setFont (font);

    if (darkShadow)
    {
        // Silkscreen print on a painted panel: a soft shadow just below the ink.
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawText (text, area.translated (0.0f, 1.0f), justification, false);
    }
    else
    {
        // Engraved into a light panel: a highlight on the lower edge.
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawText (text, area.translated (0.0f, 0.8f), justification, false);
    }

    g.setColour (colour);
    g.drawText (text, area, justification, false);
}

void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius, float angle)
{
    const auto r = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

    // Countersink shadow.
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (r.expanded (radius * 0.18f).translated (0.0f, radius * 0.12f));

    juce::ColourGradient metal (juce::Colour (0xffe6e6e2), r.getX(), r.getY(), juce::Colour (0xff5a5c5e), r.getRight(), r.getBottom(), false);
    metal.addColour (0.45, juce::Colour (0xffb9bab6));
    g.setGradientFill (metal);
    g.fillEllipse (r);

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawEllipse (r, radius * 0.1f);

    // Phillips slot.
    juce::Path slot;
    const auto len = radius * 0.62f;
    const auto width = radius * 0.22f;
    slot.addRectangle (-len, -width * 0.5f, len * 2.0f, width);
    slot.addRectangle (-width * 0.5f, -len, width, len * 2.0f);
    slot.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));
    g.setColour (juce::Colour (0xff2a2a2a));
    g.fillPath (slot);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.strokePath (slot, juce::PathStrokeType (radius * 0.05f), juce::AffineTransform::translation (0.0f, radius * 0.06f));
}

} // namespace afterglow::ui
