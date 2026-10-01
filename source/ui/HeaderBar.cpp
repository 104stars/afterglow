#include "HeaderBar.h"
#include "Textures.h"
#include "../Parameters.h"

namespace afterglow::ui
{
IconButton::IconButton (Icon i, const juce::String& tooltip) : juce::Button ({}), icon (i)
{
    setTooltip (tooltip);
}

void IconButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    auto area = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (area.translated (0.0f, 1.5f), 3.0f);
    if (isButtonDown)
        area.translate (0.0f, 1.0f);

    juce::ColourGradient body (juce::Colour (0xff4a4b4f), area.getX(), area.getY(), juce::Colour (0xff1f2023), area.getX(), area.getBottom(), false);
    g.setGradientFill (body);
    g.fillRoundedRectangle (area, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (isMouseOver ? 0.2f : 0.12f));
    g.drawLine (area.getX() + 3.0f, area.getY() + 0.8f, area.getRight() - 3.0f, area.getY() + 0.8f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (area, 3.0f, 1.0f);

    const auto c = area.getCentre();
    const auto s = std::min (area.getWidth(), area.getHeight()) * 0.22f;
    const auto colour = isEnabled() ? Colours::silkscreen.withAlpha (isMouseOver ? 1.0f : 0.85f) : Colours::silkscreen.withAlpha (0.3f);
    g.setColour (colour);
    juce::Path p;

    switch (icon)
    {
        case Icon::left:  p.addTriangle (c.x + s, c.y - s * 1.3f, c.x + s, c.y + s * 1.3f, c.x - s * 1.1f, c.y); g.fillPath (p); break;
        case Icon::right: p.addTriangle (c.x - s, c.y - s * 1.3f, c.x - s, c.y + s * 1.3f, c.x + s * 1.1f, c.y); g.fillPath (p); break;
        case Icon::undo:
        case Icon::redo:
        {
            // A hooked arrow: a U-turn curve with an arrow head pointing back (undo) or forward (redo).
            const auto dir = icon == Icon::undo ? 1.0f : -1.0f;
            const auto w = s * 1.6f;
            juce::Path curve;
            curve.startNewSubPath (c.x + dir * w * 0.9f, c.y + s * 1.1f);
            curve.cubicTo (c.x + dir * w * 1.25f, c.y - s * 0.9f, c.x - dir * w * 0.2f, c.y - s * 1.2f, c.x - dir * w * 0.55f, c.y - s * 0.35f);
            g.strokePath (curve, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path head;
            const auto tip = juce::Point<float> (c.x - dir * w * 0.95f, c.y + s * 0.15f);
            head.addTriangle (tip.x, tip.y, tip.x + dir * s * 0.25f, tip.y - s * 1.05f, tip.x + dir * s * 1.05f, tip.y - s * 0.2f);
            g.fillPath (head);
            break;
        }
        case Icon::menu:
            for (int i = -1; i <= 1; ++i)
                g.fillRoundedRectangle (juce::Rectangle<float> (s * 2.6f, 1.6f).withCentre ({ c.x, c.y + static_cast<float> (i) * s * 0.9f }), 0.8f);
            break;
    }
}

//======================================================================================================================
HeaderBar::HeaderBar (APVTS& state, PresetManager& p, juce::UndoManager& u)
    : presets (p),
      undo (u),
      magnitude (state, ParamIDs::magnitude, StyleId::magnitudeFader, false,
                 "Magnitude: one control for the whole processor. It scales every big knob and the master section, from clean to full effect.")
{
    for (auto* c : std::initializer_list<juce::Component*> { &prev, &next, &undoButton, &redoButton, &browse, &save, &magnitude })
        addAndMakeVisible (c);

    browse.setTooltip ("Open the preset browser.");
    save.setTooltip ("Save the current settings as a user preset.");

    prev.onClick = [this] { presets.loadPrevious(); refresh(); };
    next.onClick = [this] { presets.loadNext(); refresh(); };
    undoButton.onClick = [this] { undo.undo(); };
    redoButton.onClick = [this] { undo.redo(); };
    browse.onClick = [this] { if (onBrowse) onBrowse(); };
    save.onClick = [this] { if (onSave) onSave(); };
    magnitude.onValueChange = [this] { repaint (magnitudeCaption); };

    setBufferedToImage (true);
    refresh();
}

void HeaderBar::resized()
{
    logoArea = { 16, 4, 270, 76 };
    prev.setBounds (300, 27, 26, 30);
    displayArea = { 330, 14, 306, 56 };
    next.setBounds (640, 27, 26, 30);
    browse.setBounds (676, 18, 72, 22);
    save.setBounds (676, 44, 72, 22);
    undoButton.setBounds (754, 18, 28, 22);
    redoButton.setBounds (754, 44, 28, 22);
    // Ends 20 px short of the corner screws; the group is centred on the header's middle like the display.
    magnitudeCaption = { 800, 10, 236, 16 };
    magnitude.setBounds (800, 28, 236, 30);
}

void HeaderBar::refresh()
{
    const auto name = presets.getCurrentPresetName();
    const auto dirty = presets.isDirty();
    const auto idx = presets.getCurrentIndex();
    const auto category = juce::isPositiveAndBelow (idx, static_cast<int> (presets.getPresets().size()))
                            ? presets.getPresets()[static_cast<size_t> (idx)].category : juce::String();

    if (name != shownName || dirty != shownDirty || category != shownCategory)
    {
        shownName = name;
        shownDirty = dirty;
        shownCategory = category;
        repaint (displayArea);
    }

    undoButton.setEnabled (undo.canUndo());
    redoButton.setEnabled (undo.canRedo());
}

void HeaderBar::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked())
        return;

    if (logoArea.contains (e.getPosition()) && onAbout)
        onAbout();
    else if (displayArea.contains (e.getPosition()) && onBrowse)
        onBrowse();
}

void HeaderBar::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const auto scale = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());

    // Gunmetal brushed aluminium.
    const auto metal = Textures::get (Textures::Kind::brushedMetal, juce::roundToInt (area.getWidth() * scale), juce::roundToInt (area.getHeight() * scale), 21u);
    Textures::drawFitted (g, metal, area);
    juce::ColourGradient tint (juce::Colour (0x8a101114), area.getX(), area.getY(), juce::Colour (0xd0101114), area.getX(), area.getBottom(), false);
    g.setGradientFill (tint);
    g.fillRect (area);
    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.drawHorizontalLine (0, area.getX(), area.getRight());
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawHorizontalLine (juce::roundToInt (area.getBottom()) - 1, area.getX(), area.getRight());

    for (auto p : { juce::Point<float> (8.0f, 10.0f), juce::Point<float> (8.0f, area.getBottom() - 10.0f),
                    juce::Point<float> (area.getRight() - 8.0f, 10.0f), juce::Point<float> (area.getRight() - 8.0f, area.getBottom() - 10.0f) })
        drawScrew (g, p, 3.4f, p.x * 0.01f + p.y * 0.05f);

    // Logo: a sunset-gradient script with a soft glow.
    {
        const auto logo = logoArea.toFloat();
        const auto font = Fonts::get().script (54.0f);
        const auto text = juce::String ("afterglow");
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, text, logo.getX() + 6.0f, logo.getY() + 50.0f);
        juce::Path path;
        glyphs.createPath (path);

        g.setColour (juce::Colour (0xffff8a3d).withAlpha (0.18f));
        g.strokePath (path, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillPath (path, juce::AffineTransform::translation (0.0f, 2.0f));

        const auto b = path.getBounds();
        juce::ColourGradient sunset (juce::Colour (0xffffe08a), b.getX(), b.getY(), juce::Colour (0xffd8325e), b.getX(), b.getBottom(), false);
        sunset.addColour (0.5, juce::Colour (0xffff8c3a));
        g.setGradientFill (sunset);
        g.fillPath (path);
        g.setColour (juce::Colour (0xfffff2d0).withAlpha (0.55f));
        g.strokePath (path, juce::PathStrokeType (0.7f));

        drawEngravedText (g, "VINTAGE  COLOUR  PROCESSOR", juce::Rectangle<float> (logo.getX() + 10.0f, logo.getBottom() - 16.0f, 250.0f, 12.0f),
                          Fonts::get().label (11.0f), Colours::silkscreen.withAlpha (0.75f), juce::Justification::centredLeft, true);
    }

    // Preset display (amber vacuum fluorescent look).
    {
        const auto d = displayArea.toFloat();
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRoundedRectangle (d.translated (0.0f, 1.0f), 5.0f);
        g.setColour (juce::Colour (0xff0a0806));
        g.fillRoundedRectangle (d, 5.0f);
        juce::ColourGradient inner (juce::Colours::black, d.getX(), d.getY(), juce::Colours::transparentBlack, d.getX(), d.getY() + 9.0f, false);
        g.setGradientFill (inner);
        g.fillRoundedRectangle (d, 5.0f);
        juce::ColourGradient bloom (Colours::amber.withAlpha (0.08f), d.getCentreX(), d.getCentreY(), Colours::amber.withAlpha (0.0f), d.getX(), d.getY(), true);
        g.setGradientFill (bloom);
        g.fillRoundedRectangle (d, 5.0f);

        const auto textArea = d.reduced (12.0f, 6.0f);
        g.setFont (Fonts::get().display (10.0f));
        g.setColour (Colours::amber.withAlpha (0.55f));
        g.drawText ("PRESET", textArea, juce::Justification::topLeft, false);
        if (shownCategory.isNotEmpty())
            g.drawText (shownCategory.toUpperCase(), textArea, juce::Justification::topRight, false);

        const auto nameText = shownName + (shownDirty ? " *" : "");
        const auto nameArea = textArea.withTrimmedTop (12.0f);
        const auto nameFont = Fonts::get().display (22.0f);
        g.setFont (nameFont);
        g.setColour (Colours::amber);
        g.drawFittedText (nameText, nameArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

        juce::ColourGradient glass (juce::Colours::white.withAlpha (0.08f), d.getX(), d.getY(), juce::Colours::transparentWhite, d.getX(), d.getCentreY(), false);
        g.setGradientFill (glass);
        g.fillRoundedRectangle (d.withHeight (d.getHeight() * 0.5f), 5.0f);
    }

    // Magnitude caption and scale.
    drawEngravedText (g, "MAGNITUDE", magnitudeCaption.toFloat(), Fonts::get().labelBold (13.0f), Colours::silkscreen, juce::Justification::centredLeft, true);
    g.setFont (Fonts::get().display (11.0f));
    g.setColour (Colours::amber.withAlpha (0.8f));
    g.drawText (juce::String (juce::roundToInt (magnitude.getValue())) + "%", magnitudeCaption.toFloat(), juce::Justification::centredRight, false);
    // The scale spans the cap's travel (inset by the thumb radius) so 0, 50 and 100 sit under the cap's centre.
    const auto fader = magnitude.getBounds().toFloat();
    const auto inset = static_cast<float> (getLookAndFeel().getSliderThumbRadius (magnitude));
    for (int i = 0; i <= 10; ++i)
    {
        const auto x = fader.getX() + inset + (fader.getWidth() - 2.0f * inset) * static_cast<float> (i) / 10.0f;
        const auto major = i % 5 == 0;
        g.setColour (Colours::silkscreen.withAlpha (major ? 0.8f : 0.45f));
        g.drawLine (x, fader.getBottom() + 2.0f, x, fader.getBottom() + (major ? 8.0f : 5.0f), 1.0f);
    }
    const auto scaleRow = juce::Rectangle<float> (fader.getX() + inset - 12.0f, fader.getBottom() + 9.0f, fader.getWidth() - 2.0f * inset + 24.0f, 12.0f);
    const auto micro = Fonts::get().labelMedium (10.5f);
    const auto print = Colours::silkscreen.withAlpha (0.65f);
    for (auto [value, text] : { std::pair { 0.0f, "0" }, std::pair { 0.5f, "50" }, std::pair { 1.0f, "100" } })
    {
        const auto x = fader.getX() + inset + (fader.getWidth() - 2.0f * inset) * value;
        drawEngravedText (g, text, juce::Rectangle<float> (24.0f, 12.0f).withCentre ({ x, scaleRow.getCentreY() }), micro, print, juce::Justification::centred, true);
    }
}

} // namespace afterglow::ui
