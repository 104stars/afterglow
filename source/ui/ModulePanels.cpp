#include "ModulePanels.h"
#include "Textures.h"
#include "../Parameters.h"

namespace afterglow::ui
{
namespace
{
    using namespace Layout;

    // Every panel uses the same grid (see Layout in Theme.h): a header row (selector or balance slider),
    // two knob rows on a 74 px pitch, one keycap row, then Flux. Columns sit at 30 % / 50 % / 66 % / 70 %.
    int columnX (juce::Rectangle<int> area, float proportion)
    {
        return area.getX() + juce::roundToInt (static_cast<float> (area.getWidth()) * proportion);
    }

    juce::Rectangle<int> knobAt (juce::Rectangle<int> area, float centreXProportion, int top)
    {
        return { columnX (area, centreXProportion) - knobWidth / 2, top, knobWidth, knobHeight };
    }

    /** Centre y of a knob's dial (the knob box minus its 13 px label). */
    constexpr int dialCentre (int knobTop) { return knobTop + (knobHeight - 13) / 2; }

    juce::Rectangle<int> keycapAt (int centreX, int centreY, int width = keycapWidth)
    {
        return juce::Rectangle<int> (width, keycapHeight).withCentre ({ centreX, centreY });
    }

    juce::Rectangle<int> selectorBounds (juce::Rectangle<int> area)
    {
        return { panelMargin, headerRowTop + 4, area.getRight() + area.getX() - 2 * panelMargin, 24 };
    }

    void layoutBalance (juce::Rectangle<int>& caption, juce::Slider& slider, juce::Rectangle<int> area)
    {
        const auto width = area.getRight() + area.getX();
        caption = { 6, headerRowTop, width - 12, 14 };
        slider.setBounds (panelMargin, headerRowTop + 14, width - 2 * panelMargin, 22);
    }

    void layoutFocus (RangeSlider& focus, juce::Rectangle<int>& caption)
    {
        focus.setBounds (18, focusTop, 28, focusHeight);
        caption = { 8, rowBTop + knobHeight - 15, 48, 13 }; // same baseline as the knob labels beside it
    }
} // namespace

//======================================================================================================================
Hatch::Hatch (APVTS& s, const juce::String& onParamId, const juce::String& t)
    : state (s), paramId (onParamId), title (t)
{
    setTooltip ("This module is switched off. Click to switch it on.");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Hatch::paint (juce::Graphics& g)
{
    const auto scale = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto area = getLocalBounds().toFloat();

    if (! cache.isValid() || ! juce::approximatelyEqual (cacheScale, scale) || cache.getWidth() != juce::roundToInt (area.getWidth() * scale))
    {
        cacheScale = scale;
        cache = juce::Image (juce::Image::ARGB, std::max (1, juce::roundToInt (area.getWidth() * scale)), std::max (1, juce::roundToInt (area.getHeight() * scale)), true);
        juce::Graphics hg (cache);
        hg.addTransform (juce::AffineTransform::scale (scale));

        // Dark steel plate.
        juce::ColourGradient steel (juce::Colour (0xff3d3f43), area.getX(), area.getY(), juce::Colour (0xff1b1c1f), area.getX(), area.getBottom(), false);
        hg.setGradientFill (steel);
        hg.fillRoundedRectangle (area, 6.0f);

        // Perforations.
        const auto pitch = 7.0f;
        const auto hole = 3.6f;
        auto row = 0;
        for (auto y = area.getY() + 12.0f; y < area.getBottom() - 10.0f; y += pitch * 0.866f, ++row)
        {
            for (auto x = area.getX() + 10.0f + (row % 2 == 0 ? 0.0f : pitch * 0.5f); x < area.getRight() - 8.0f; x += pitch)
            {
                const auto r = juce::Rectangle<float> (hole, hole).withCentre ({ x, y });
                hg.setColour (juce::Colours::black.withAlpha (0.9f));
                hg.fillEllipse (r);
                hg.setColour (juce::Colours::white.withAlpha (0.12f));
                hg.drawEllipse (r.translated (0.0f, 0.5f), 0.5f);
            }
        }

        // Name plate.
        const auto plate = juce::Rectangle<float> (area.getWidth() - 36.0f, 46.0f).withCentre (area.getCentre());
        hg.setColour (juce::Colours::black.withAlpha (0.5f));
        hg.fillRoundedRectangle (plate.translated (0.0f, 2.0f), 4.0f);
        juce::ColourGradient brass (juce::Colour (0xffd8c49a), plate.getX(), plate.getY(), juce::Colour (0xff8f7a52), plate.getX(), plate.getBottom(), false);
        hg.setGradientFill (brass);
        hg.fillRoundedRectangle (plate, 4.0f);
        hg.setColour (juce::Colours::black.withAlpha (0.45f));
        hg.drawRoundedRectangle (plate, 4.0f, 1.0f);
        drawEngravedText (hg, title.toUpperCase(), plate.withTrimmedBottom (18.0f).translated (0.0f, 4.0f), Fonts::get().labelBold (17.0f), juce::Colour (0xff2c2416), juce::Justification::centred, false);
        drawEngravedText (hg, juce::String (juce::CharPointer_UTF8 ("OFF \xc2\xb7 CLICK TO ENABLE")), plate.withTrimmedTop (26.0f), Fonts::get().label (10.5f), juce::Colour (0xff3e3320), juce::Justification::centred, false);

        // Frame and screws.
        hg.setColour (juce::Colours::black.withAlpha (0.8f));
        hg.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);
        hg.setColour (juce::Colours::white.withAlpha (0.12f));
        hg.drawLine (area.getX() + 6.0f, area.getY() + 1.2f, area.getRight() - 6.0f, area.getY() + 1.2f, 1.0f);
        for (auto p : { juce::Point<float> (7.0f, 7.0f), juce::Point<float> (area.getRight() - 7.0f, 7.0f),
                        juce::Point<float> (7.0f, area.getBottom() - 7.0f), juce::Point<float> (area.getRight() - 7.0f, area.getBottom() - 7.0f) })
            drawScrew (hg, p, 3.2f, 0.3f + p.x * 0.05f);
    }

    g.drawImage (cache, area, juce::RectanglePlacement::stretchToFit);
}

void Hatch::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked())
        return;

    if (auto* p = state.getParameter (paramId))
    {
        if (state.undoManager != nullptr)
            state.undoManager->beginNewTransaction ("Enable module");
        p->beginChangeGesture();
        p->setValueNotifyingHost (1.0f);
        p->endChangeGesture();
    }
}

//======================================================================================================================
ModulePanel::ModulePanel (APVTS& s, dsp::EngineTelemetry& t, int index, const juce::String& moduleTitle,
                          const juce::String& onParamId, const juce::String& fluxParamId)
    : state (s),
      telemetry (t),
      moduleIndex (index),
      title (moduleTitle),
      enamel (Colours::moduleEnamel (index)),
      phosphor (Colours::modulePhosphor (index)),
      flux (s, fluxParamId, StyleId::fluxSlider, false,
            "Flux: organic, random drift of this module's key parameters. A little adds life, a lot gets wild."),
      hatch (s, onParamId, moduleTitle),
      onParam (s.getRawParameterValue (onParamId))
{
    flux.setColour (juce::Slider::trackColourId, phosphor);
    addAndMakeVisible (flux);
    addChildComponent (hatch);
    setOpaque (false);
}

void ModulePanel::resized()
{
    const auto bounds = getLocalBounds();
    faceplate = {};

    // One 14 px content margin all round: display, selectors, divider and Flux share the same edges.
    if (display != nullptr)
        display->setBounds (Layout::panelMargin, Layout::displayTop, bounds.getWidth() - 2 * Layout::panelMargin, Layout::displayHeight);

    const auto fluxArea = juce::Rectangle<int> (Layout::panelMargin, bounds.getHeight() - 31, bounds.getWidth() - 2 * Layout::panelMargin, 22);
    fluxCaptionArea = fluxArea.withWidth (36);
    flux.setBounds (fluxArea.withTrimmedLeft (38));

    layoutControls (juce::Rectangle<int> (6, 0, bounds.getWidth() - 12, bounds.getHeight()));
    hatch.setBounds (bounds);
    updateHatch();
}

void ModulePanel::refresh (double seconds)
{
    if (display != nullptr)
        display->tick (seconds);
    updateHatch();
}

void ModulePanel::updateHatch()
{
    const auto on = onParam == nullptr || onParam->load (std::memory_order_relaxed) > 0.5f;

    if (! hatchInitialised)
    {
        // First time: show the current state without animating.
        hatchInitialised = true;
        lastOn = on;
        hatch.setAlpha (1.0f);
        hatch.setVisible (! on);
        if (! on)
            hatch.toFront (false);
        return;
    }

    if (on == lastOn)
        return;

    lastOn = on;
    if (on)
        juce::Desktop::getInstance().getAnimator().fadeOut (&hatch, 140);
    else
    {
        hatch.toFront (false);
        juce::Desktop::getInstance().getAnimator().fadeIn (&hatch, 140);
    }
}

void ModulePanel::drawCaption (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area) const
{
    drawEngravedText (g, text, area.toFloat(), Fonts::get().label (13.0f), ink, juce::Justification::centred, true);
}

void ModulePanel::drawBalanceCaption (juce::Graphics& g, const juce::String& left, const juce::String& right, juce::Rectangle<int> area) const
{
    const auto f = area.toFloat();
    const auto font = Fonts::get().label (13.0f);
    drawEngravedText (g, left, f.withWidth (f.getWidth() * 0.42f), font, ink, juce::Justification::centredRight, true);
    drawEngravedText (g, right, f.withTrimmedLeft (f.getWidth() * 0.58f), font, ink, juce::Justification::centredLeft, true);

    // Double-headed arrow between the two words.
    const auto c = f.getCentre();
    juce::Path arrow;
    arrow.addArrow ({ c.x - 1.0f, c.y, c.x + 8.0f, c.y }, 1.1f, 5.0f, 3.5f);
    arrow.addArrow ({ c.x + 1.0f, c.y, c.x - 8.0f, c.y }, 1.1f, 5.0f, 3.5f);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillPath (arrow, juce::AffineTransform::translation (0.0f, 1.0f));
    g.setColour (ink);
    g.fillPath (arrow);
}

void ModulePanel::paint (juce::Graphics& g)
{
    const auto scale = juce::jlimit (0.25f, 8.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (! faceplate.isValid() || ! juce::approximatelyEqual (scale, faceplateScale))
    {
        faceplateScale = scale;
        faceplate = juce::Image (juce::Image::ARGB, std::max (1, juce::roundToInt (static_cast<float> (getWidth()) * scale)),
                                 std::max (1, juce::roundToInt (static_cast<float> (getHeight()) * scale)), true);
        juce::Graphics fg (faceplate);
        fg.addTransform (juce::AffineTransform::scale (scale));
        paintFaceplate (fg, std::max (1.0f, scale));
    }

    g.drawImage (faceplate, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
}

void ModulePanel::paintFaceplate (juce::Graphics& g, float scale)
{
    const auto area = getLocalBounds().toFloat();

    // Drop shadow into the bay.
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (area.translated (0.0f, 2.0f), 7.0f);

    // Enamel faceplate.
    juce::ColourGradient body (enamel.brighter (0.18f), area.getX(), area.getY(), enamel.darker (0.45f), area.getX(), area.getBottom(), false);
    body.addColour (0.35, enamel);
    g.setGradientFill (body);
    g.fillRoundedRectangle (area, 6.0f);

    const auto tilePixels = juce::roundToInt (96.0f * scale);
    Textures::fillTiled (g, Textures::get (Textures::Kind::grainTile, tilePixels, tilePixels, static_cast<uint32_t> (moduleIndex + 11)), area, scale, 0.9f);

    // Paint sheen and bevel.
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.10f), area.getX(), area.getY(), juce::Colours::transparentWhite, area.getX(), area.getY() + 60.0f, false);
    g.setGradientFill (sheen);
    g.fillRoundedRectangle (area, 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.drawLine (area.getX() + 6.0f, area.getY() + 0.8f, area.getRight() - 6.0f, area.getY() + 0.8f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);

    for (auto p : { juce::Point<float> (7.0f, 7.0f), juce::Point<float> (area.getRight() - 7.0f, 7.0f),
                    juce::Point<float> (7.0f, area.getBottom() - 7.0f), juce::Point<float> (area.getRight() - 7.0f, area.getBottom() - 7.0f) })
        drawScrew (g, p, 3.2f, 0.4f + 0.3f * static_cast<float> (moduleIndex) + p.y * 0.01f);

    // Engraved divider under the display.
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawHorizontalLine (78, 14.0f, area.getRight() - 14.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawHorizontalLine (79, 14.0f, area.getRight() - 14.0f);

    // Flux caption with a small random-walk glyph.
    const auto fc = fluxCaptionArea.toFloat();
    drawEngravedText (g, "FLUX", fc.withTrimmedBottom (8.0f), Fonts::get().labelBold (13.0f), ink, juce::Justification::centredLeft, true);
    juce::Path squiggle;
    squiggle.startNewSubPath (fc.getX(), fc.getBottom() - 5.0f);
    const float pts[] { 0.0f, -3.0f, 1.5f, -2.0f, 2.5f, -0.5f, 1.0f };
    for (int i = 0; i < 7; ++i)
        squiggle.lineTo (fc.getX() + 4.0f * static_cast<float> (i + 1), fc.getBottom() - 5.0f + pts[i]);
    g.setColour (ink.withAlpha (0.85f));
    g.strokePath (squiggle, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    paintSilkscreen (g);
}

//======================================================================================================================
NoisePanel::NoisePanel (APVTS& s, dsp::EngineTelemetry& t)
    : ModulePanel (s, t, 0, "Noise", ParamIDs::noiseOn, ParamIDs::noiseFlux),
      type (s, ParamIDs::noiseType, Colours::modulePhosphor (0), "Noise character: vinyl, tape, hum, radio and more. All generated in real time."),
      tone (s, ParamIDs::noiseTone, "TONE", StyleId::bipolarKnob, Colours::silkscreen, "Tilts the noise darker (left) or brighter (right)."),
      follow (s, ParamIDs::noiseFollow, "FOLLOW", StyleId::smallKnob, Colours::silkscreen, "Makes the noise level follow the input level. Great on drums."),
      duck (s, ParamIDs::noiseDuck, "DUCK", StyleId::smallKnob, Colours::silkscreen, "Pushes the noise down when the input gets loud, like a slow compressor."),
      post (s, ParamIDs::noisePost, "POST", Colours::amber, LedButton::Look::keycap,
            "Routing: off = noise enters at the start of the chain and is processed by every module. On = noise is added after the EQ.")
{
    display = std::make_unique<NoiseDisplay> (s, t);
    addAndMakeVisible (*display);
    for (auto* c : std::initializer_list<juce::Component*> { &type, &tone, &follow, &duck, &post })
        addAndMakeVisible (c);
}

void NoisePanel::layoutControls (juce::Rectangle<int> area)
{
    type.setBounds (selectorBounds (area));
    tone.setBounds (knobAt (area, 0.3f, rowATop));
    post.setBounds (keycapAt (columnX (area, 0.7f), dialCentre (rowATop)));
    follow.setBounds (knobAt (area, 0.3f, rowBTop));
    duck.setBounds (knobAt (area, 0.7f, rowBTop));
}

//======================================================================================================================
SyncedRateKnob::SyncedRateKnob (APVTS& s, LabelledKnob& k, const juce::String& rate, const juce::String& division, const juce::String& syncId)
    : state (s), knob (k), rateId (rate), divisionId (division), sync (s.getRawParameterValue (syncId))
{
    refresh();
}

void SyncedRateKnob::refresh()
{
    const auto synced = sync != nullptr && sync->load (std::memory_order_relaxed) > 0.5f ? 1 : 0;
    if (synced == lastState)
        return;

    lastState = synced;
    knob.attachTo (synced == 1 ? divisionId : rateId);
    knob.getSlider().setTooltip (synced == 1 ? "Rate in note values, locked to the host tempo." : "Rate in Hz. Switch SYNC on to lock it to the host tempo.");
}

//======================================================================================================================
WobblePanel::WobblePanel (APVTS& s, dsp::EngineTelemetry& t)
    : ModulePanel (s, t, 1, "Wobble", ParamIDs::wobbleOn, ParamIDs::wobbleFlux),
      balance (s, ParamIDs::wobbleBalance, StyleId::miniSlider, false, "Balance between slow wow and fast flutter."),
      wowRate (s, ParamIDs::wobbleWowRate, "WOW RATE", StyleId::smallKnob, Colours::silkscreen),
      flutterRate (s, ParamIDs::wobbleFlutterRate, "FLUTTER", StyleId::smallKnob, Colours::silkscreen, "Speed of the fast flutter component."),
      mix (s, ParamIDs::wobbleMix, "MIX", StyleId::smallKnob, Colours::silkscreen, "Blend with the dry signal. Around 50 % with STEREO on gives a lush chorus."),
      sync (s, ParamIDs::wobbleSync, "SYNC", Colours::amber, LedButton::Look::keycap, "Locks the wow rate to the host tempo."),
      stereo (s, ParamIDs::wobbleStereo, "STEREO", Colours::amber, LedButton::Look::keycap, "Offsets the wow between left and right, turning it into a chorus."),
      syncedRate (s, wowRate, ParamIDs::wobbleWowRate, ParamIDs::wobbleDivision, ParamIDs::wobbleSync)
{
    display = std::make_unique<WobbleDisplay> (s, t);
    addAndMakeVisible (*display);
    balance.setColour (juce::Slider::rotarySliderOutlineColourId, ink);
    for (auto* c : std::initializer_list<juce::Component*> { &balance, &wowRate, &flutterRate, &mix, &sync, &stereo })
        addAndMakeVisible (c);
}

void WobblePanel::refresh (double seconds)
{
    syncedRate.refresh();
    ModulePanel::refresh (seconds);
}

void WobblePanel::layoutControls (juce::Rectangle<int> area)
{
    layoutBalance (balanceCaption, balance, area);
    wowRate.setBounds (knobAt (area, 0.3f, rowATop));
    flutterRate.setBounds (knobAt (area, 0.7f, rowATop));
    // SYNC and STEREO stacked on a 30 px pitch, centred on the MIX dial beside them.
    sync.setBounds (keycapAt (columnX (area, 0.3f), dialCentre (rowBTop) - 15));
    stereo.setBounds (keycapAt (columnX (area, 0.3f), dialCentre (rowBTop) + 15));
    mix.setBounds (knobAt (area, 0.7f, rowBTop));
}

void WobblePanel::paintSilkscreen (juce::Graphics& g)
{
    drawBalanceCaption (g, "WOW", "FLUTTER", balanceCaption);
}

//======================================================================================================================
DistortPanel::DistortPanel (APVTS& s, dsp::EngineTelemetry& t)
    : ModulePanel (s, t, 2, "Distort", ParamIDs::distortOn, ParamIDs::distortFlux),
      type (s, ParamIDs::distortType, Colours::modulePhosphor (2), "Saturation character: tube, transformer, broken speaker, tape, fuzz and waveshapers."),
      focus (s, ParamIDs::distortFocusLow, ParamIDs::distortFocusHigh, true, Colours::modulePhosphor (2),
             "Focus: the frequency band that gets distorted. Everything outside it passes through clean. Drag the middle to move the band."),
      tone (s, ParamIDs::distortTone, "TONE", StyleId::bipolarKnob, Colours::silkscreen, "Darkens or brightens the distorted signal."),
      mix (s, ParamIDs::distortMix, "MIX", StyleId::smallKnob, Colours::silkscreen, "Parallel blend of distorted and clean signal.")
{
    display = std::make_unique<DistortDisplay> (s, t);
    addAndMakeVisible (*display);
    for (auto* c : std::initializer_list<juce::Component*> { &type, &focus, &tone, &mix })
        addAndMakeVisible (c);
}

void DistortPanel::layoutControls (juce::Rectangle<int> area)
{
    type.setBounds (selectorBounds (area));
    layoutFocus (focus, focusCaption);
    tone.setBounds (knobAt (area, 0.66f, rowATop));
    mix.setBounds (knobAt (area, 0.66f, rowBTop));
}

void DistortPanel::paintSilkscreen (juce::Graphics& g)
{
    drawCaption (g, "FOCUS", focusCaption);
}

//======================================================================================================================
DigitalPanel::DigitalPanel (APVTS& s, dsp::EngineTelemetry& t)
    : ModulePanel (s, t, 3, "Digital", ParamIDs::digitalOn, ParamIDs::digitalFlux),
      balance (s, ParamIDs::digitalBalance, StyleId::miniSlider, false, "Balance between sample-rate reduction and bit-depth reduction."),
      focus (s, ParamIDs::digitalFocusLow, ParamIDs::digitalFocusHigh, true, Colours::modulePhosphor (3),
             "Focus: the frequency band that gets crushed. Try it on the top of a kick while the low end stays clean."),
      cut (s, ParamIDs::digitalCut, "CUT", Colours::amber, LedButton::Look::keycap, "Removes everything outside the focus band from the processed signal."),
      compand (s, ParamIDs::digitalCompand, "COMPAND", Colours::amber, LedButton::Look::keycap,
               "Mu-law companding like classic 8-bit samplers: quiet details survive, loud parts get grainy."),
      smooth (s, ParamIDs::digitalSmooth, "SMOOTH", StyleId::smallKnob, Colours::silkscreen, "Anti-alias and reconstruction filters that polish the harsh digital edges."),
      mix (s, ParamIDs::digitalMix, "MIX", StyleId::smallKnob, Colours::silkscreen, "Blend between crushed and clean signal.")
{
    display = std::make_unique<DigitalDisplay> (s, t);
    addAndMakeVisible (*display);
    balance.setColour (juce::Slider::rotarySliderOutlineColourId, ink);
    for (auto* c : std::initializer_list<juce::Component*> { &balance, &focus, &cut, &compand, &smooth, &mix })
        addAndMakeVisible (c);
}

void DigitalPanel::layoutControls (juce::Rectangle<int> area)
{
    layoutBalance (balanceCaption, balance, area);
    layoutFocus (focus, focusCaption);
    smooth.setBounds (knobAt (area, 0.66f, rowATop));
    mix.setBounds (knobAt (area, 0.66f, rowBTop));
    cut.setBounds (keycapAt (32, buttonRowTop + keycapHeight / 2, 44));
    compand.setBounds (keycapAt (columnX (area, 0.66f), buttonRowTop + keycapHeight / 2, 72));
}

void DigitalPanel::paintSilkscreen (juce::Graphics& g)
{
    drawBalanceCaption (g, "RATE", "BITS", balanceCaption);
    drawCaption (g, "FOCUS", focusCaption);
}

//======================================================================================================================
SpacePanel::SpacePanel (APVTS& s, dsp::EngineTelemetry& t)
    : ModulePanel (s, t, 4, "Space", ParamIDs::spaceOn, ParamIDs::spaceFlux),
      type (s, ParamIDs::spaceType, Colours::modulePhosphor (4), "Reverb character: ambience, room, plate, hall, spring, or a chromatic resonator."),
      focus (s, ParamIDs::spaceFocusLow, ParamIDs::spaceFocusHigh, true, Colours::modulePhosphor (4),
             "Focus: damping and resonance of the reverb itself. Narrow it for dark, boxy or ringing spaces."),
      decay (s, ParamIDs::spaceDecay, "DECAY", StyleId::smallKnob, Colours::silkscreen, "Length of the reverb tail."),
      preDelay (s, ParamIDs::spacePreDelay, "PRE-DELAY", StyleId::smallKnob, Colours::silkscreen, "Delays the reverb a little so it does not smear the attack."),
      stereo (s, ParamIDs::spaceStereo, "STEREO", Colours::amber, LedButton::Look::keycap, "Stereo reverb. Turns mono sources into a wide space.")
{
    display = std::make_unique<SpaceDisplay> (s, t);
    addAndMakeVisible (*display);
    for (auto* c : std::initializer_list<juce::Component*> { &type, &focus, &decay, &preDelay, &stereo })
        addAndMakeVisible (c);
}

void SpacePanel::layoutControls (juce::Rectangle<int> area)
{
    type.setBounds (selectorBounds (area));
    layoutFocus (focus, focusCaption);
    decay.setBounds (knobAt (area, 0.66f, rowATop));
    preDelay.setBounds (knobAt (area, 0.66f, rowBTop));
    stereo.setBounds (keycapAt (columnX (area, 0.66f), buttonRowTop + keycapHeight / 2));
}

void SpacePanel::paintSilkscreen (juce::Graphics& g)
{
    drawCaption (g, "FOCUS", focusCaption);
}

//======================================================================================================================
MagneticPanel::MagneticPanel (APVTS& s, dsp::EngineTelemetry& t)
    : ModulePanel (s, t, 5, "Magnetic", ParamIDs::magneticOn, ParamIDs::magneticFlux),
      balance (s, ParamIDs::magneticBalance, StyleId::miniSlider, false, "Balance between slow tape wear and fast flutter."),
      rate (s, ParamIDs::magneticRate, "RATE", StyleId::smallKnob, Colours::silkscreen),
      dropouts (s, ParamIDs::magneticDropouts, "DROPOUTS", StyleId::smallKnob, Colours::silkscreen, "How often the tape suddenly loses signal."),
      sync (s, ParamIDs::magneticSync, "SYNC", Colours::amber, LedButton::Look::keycap, "Locks the flutter rate to the host tempo."),
      stereo (s, ParamIDs::magneticStereo, "STEREO", Colours::amber, LedButton::Look::keycap, "Independent wear and dropouts on the left and right channels."),
      syncedRate (s, rate, ParamIDs::magneticRate, ParamIDs::magneticDivision, ParamIDs::magneticSync)
{
    display = std::make_unique<MagneticDisplay> (s, t);
    addAndMakeVisible (*display);
    balance.setColour (juce::Slider::rotarySliderOutlineColourId, ink);
    for (auto* c : std::initializer_list<juce::Component*> { &balance, &rate, &dropouts, &sync, &stereo })
        addAndMakeVisible (c);
}

void MagneticPanel::refresh (double seconds)
{
    syncedRate.refresh();
    ModulePanel::refresh (seconds);
}

void MagneticPanel::layoutControls (juce::Rectangle<int> area)
{
    layoutBalance (balanceCaption, balance, area);
    // SYNC and STEREO stacked on a 30 px pitch, centred on the RATE dial they relate to.
    sync.setBounds (keycapAt (columnX (area, 0.3f), dialCentre (rowATop) - 15));
    stereo.setBounds (keycapAt (columnX (area, 0.3f), dialCentre (rowATop) + 15));
    rate.setBounds (knobAt (area, 0.7f, rowATop));
    dropouts.setBounds (knobAt (area, 0.5f, rowBTop));
}

void MagneticPanel::paintSilkscreen (juce::Graphics& g)
{
    drawBalanceCaption (g, "WEAR", "FLUTTER", balanceCaption);
}

} // namespace afterglow::ui
