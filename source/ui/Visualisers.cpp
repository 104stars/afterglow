#include "Visualisers.h"
#include "../Parameters.h"

namespace afterglow::ui
{
namespace
{
    // One display language for all six screens:
    //  - primary traces: 1.2 px with glow (strokeGlow)
    //  - secondary/reference traces: 0.9 px, 55 % alpha, no outer halo (strokeSecondary)
    //  - artwork outlines: 1.0 px without glow
    //  - trace screens share one graticule; artwork screens (Distort, Magnetic) stay clean
    //  - every screen has one small readout chip; off screens keep their static layer at 30 %
    constexpr float primaryWidth = 1.2f;
    constexpr float secondaryWidth = 0.9f;

    void strokeGlow (juce::Graphics& g, const juce::Path& path, juce::Colour colour, float intensity, float width = primaryWidth)
    {
        g.setColour (colour.withAlpha (0.09f * intensity));
        g.strokePath (path, juce::PathStrokeType (width * 4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour.withAlpha (0.26f * intensity));
        g.strokePath (path, juce::PathStrokeType (width * 2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour.withAlpha (0.95f * intensity));
        g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void strokeSecondary (juce::Graphics& g, const juce::Path& path, juce::Colour colour, float intensity)
    {
        g.setColour (colour.withAlpha (0.18f * intensity));
        g.strokePath (path, juce::PathStrokeType (secondaryWidth * 2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour.withAlpha (0.55f * intensity));
        g.strokePath (path, juce::PathStrokeType (secondaryWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void strokeOutline (juce::Graphics& g, const juce::Path& path, juce::Colour colour, float alpha)
    {
        g.setColour (colour.withAlpha (alpha));
        g.strokePath (path, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawGraticule (juce::Graphics& g, juce::Rectangle<float> screen, juce::Colour colour, float strength)
    {
        g.setColour (colour.withAlpha (0.07f * strength));
        for (int i = 1; i < 6; ++i)
            g.drawVerticalLine (juce::roundToInt (screen.getX() + screen.getWidth() * static_cast<float> (i) / 6.0f), screen.getY(), screen.getBottom());
        for (int i = 1; i < 4; ++i)
            g.drawHorizontalLine (juce::roundToInt (screen.getY() + screen.getHeight() * static_cast<float> (i) / 4.0f), screen.getX(), screen.getRight());

        // Centre line with minor ticks, like an oscilloscope graticule.
        const auto cy = screen.getCentreY();
        g.setColour (colour.withAlpha (0.12f * strength));
        g.drawHorizontalLine (juce::roundToInt (cy), screen.getX(), screen.getRight());
        g.setColour (colour.withAlpha (0.10f * strength));
        for (int i = 1; i < 24; ++i)
        {
            const auto x = screen.getX() + screen.getWidth() * static_cast<float> (i) / 24.0f;
            g.drawLine (x, cy - 1.0f, x, cy + 1.0f, 1.0f);
        }
    }

    float approach (float current, float target, float seconds, float timeConstant)
    {
        return target + (current - target) * std::exp (-seconds / std::max (0.001f, timeConstant));
    }

    juce::String rateText (float hz)
    {
        return juce::String (hz, hz < 1.0f ? 2 : 1) + "Hz";
    }
} // namespace

//======================================================================================================================
ModuleDisplay::ModuleDisplay (juce::AudioProcessorValueTreeState& s, dsp::EngineTelemetry& t, int moduleIndex, const juce::String& onParamId)
    : state (s), telemetry (t), phosphor (Colours::modulePhosphor (moduleIndex)), onParam (s.getRawParameterValue (onParamId))
{
    setInterceptsMouseClicks (false, false);
    setOpaque (false);
}

float ModuleDisplay::param (const char* id) const
{
    if (auto* v = state.getRawParameterValue (id))
        return v->load (std::memory_order_relaxed);
    return 0.0f;
}

bool ModuleDisplay::isActive() const { return onParam == nullptr || onParam->load (std::memory_order_relaxed) > 0.5f; }

float ModuleDisplay::staticAlpha() const noexcept { return 0.3f + 0.7f * activity; }

void ModuleDisplay::tick (double seconds)
{
    time += seconds;
    activity = approach (activity, isActive() ? 1.0f : 0.0f, static_cast<float> (seconds), 0.12f);
    repaint();
}

void ModuleDisplay::drawReadout (juce::Graphics& g, juce::Rectangle<float> screen)
{
    const auto text = readoutText();
    if (text.isEmpty() || activity < 0.02f)
        return;

    const auto font = Fonts::get().display (9.5f);
    const auto w = juce::GlyphArrangement::getStringWidth (font, text) + 6.0f;
    const auto inner = screen.reduced (4.0f, 3.0f);
    auto chip = juce::Rectangle<float> (w, 12.0f);
    switch (readoutPlace())
    {
        case ReadoutPlace::centre:   chip = chip.withCentre ({ screen.getCentreX(), screen.getY() + screen.getHeight() * 0.42f }); break;
        case ReadoutPlace::topRight: chip = chip.withPosition (inner.getRight() - w, inner.getY()); break;
        case ReadoutPlace::bottomRight:
        default:                     chip = chip.withPosition (inner.getRight() - w, inner.getBottom() - 12.0f); break;
    }

    g.setColour (juce::Colours::black.withAlpha (0.6f * activity));
    g.fillRoundedRectangle (chip, 2.0f);
    g.setColour (readoutColour().withAlpha (0.85f * activity));
    g.setFont (font);
    g.drawText (text, chip, juce::Justification::centred, false);
}

void ModuleDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();

    // Bezel.
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (area.translated (0.0f, 1.5f), 6.0f);
    juce::ColourGradient bezel (juce::Colour (0xff45474b), area.getX(), area.getY(), juce::Colour (0xff111214), area.getX(), area.getBottom(), false);
    g.setGradientFill (bezel);
    g.fillRoundedRectangle (area, 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawLine (area.getX() + 6.0f, area.getY() + 0.8f, area.getRight() - 6.0f, area.getY() + 0.8f, 1.0f);

    const auto screen = area.reduced (4.0f);
    g.setColour (juce::Colour (0xff050706));
    g.fillRoundedRectangle (screen, 4.0f);

    // Faint phosphor bloom behind the content.
    juce::ColourGradient bloom (phosphor.withAlpha (0.10f * activity), screen.getCentreX(), screen.getCentreY(),
                                phosphor.withAlpha (0.0f), screen.getX(), screen.getY(), true);
    g.setGradientFill (bloom);
    g.fillRoundedRectangle (screen, 4.0f);

    juce::Path clip;
    clip.addRoundedRectangle (screen, 4.0f);

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (clip);
        drawContent (g, screen);

        // Scanlines.
        g.setColour (juce::Colours::black.withAlpha (0.16f));
        for (auto y = screen.getY() + 1.0f; y < screen.getBottom(); y += 2.0f)
            g.drawHorizontalLine (juce::roundToInt (y), screen.getX(), screen.getRight());

        // The readout sits above the scanlines so it stays crisp.
        drawReadout (g, screen);
    }

    // Glass: inner shadow and a diagonal reflection.
    juce::ColourGradient innerShadow (juce::Colours::black.withAlpha (0.7f), screen.getX(), screen.getY(), juce::Colours::transparentBlack, screen.getX(), screen.getY() + 6.0f, false);
    g.setGradientFill (innerShadow);
    g.fillRoundedRectangle (screen, 4.0f);

    juce::Path reflection;
    reflection.startNewSubPath (screen.getX(), screen.getY());
    reflection.lineTo (screen.getX() + screen.getWidth() * 0.62f, screen.getY());
    reflection.lineTo (screen.getX() + screen.getWidth() * 0.38f, screen.getBottom());
    reflection.lineTo (screen.getX(), screen.getBottom());
    reflection.closeSubPath();
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (clip);
    juce::ColourGradient glass (juce::Colours::white.withAlpha (0.07f), screen.getX(), screen.getY(), juce::Colours::white.withAlpha (0.0f),
                                screen.getX() + screen.getWidth() * 0.5f, screen.getBottom(), false);
    g.setGradientFill (glass);
    g.fillPath (reflection);
}

//======================================================================================================================
NoiseDisplay::NoiseDisplay (juce::AudioProcessorValueTreeState& s, dsp::EngineTelemetry& t)
    : ModuleDisplay (s, t, 0, ParamIDs::noiseOn) {}

void NoiseDisplay::tick (double seconds)
{
    const auto write = telemetry.noiseScopeWrite.load (std::memory_order_relaxed);
    for (size_t i = 0; i < trace.size(); ++i)
    {
        const auto idx = (write - static_cast<int> (trace.size()) + static_cast<int> (i) + dsp::EngineTelemetry::scopeSize) % dsp::EngineTelemetry::scopeSize;
        trace[i] = telemetry.noiseScope[static_cast<size_t> (idx)].load (std::memory_order_relaxed);
    }

    // Display high-pass: subtract a short moving average. Vinyl rumble and hum are far slower than the 20 ms window
    // and would otherwise drag the trace off the centre line and swallow the hiss and crackle in the normalisation.
    constexpr int halfWindow = 8;
    const auto n = static_cast<int> (trace.size());
    decltype (trace) smooth {};
    for (int i = 0; i < n; ++i)
    {
        const auto lo = std::max (0, i - halfWindow), hi = std::min (n - 1, i + halfWindow);
        auto acc = 0.0f;
        for (int j = lo; j <= hi; ++j)
            acc += trace[static_cast<size_t> (j)];
        smooth[static_cast<size_t> (i)] = acc / static_cast<float> (hi - lo + 1);
    }
    auto sumSquares = 0.0f;
    for (size_t i = 0; i < trace.size(); ++i)
    {
        trace[i] -= smooth[i];
        sumSquares += trace[i] * trace[i];
    }

    // Normalise to RMS (not peak) so sparse crackle still shows its body; the knob sets how big it looks.
    const auto rms = std::sqrt (sumSquares / static_cast<float> (trace.size()));
    const auto amount = param (ParamIDs::noiseAmount) * 0.01f;
    const auto target = rms > 1.0e-6f ? juce::jlimit (0.0f, 2000.0f, (0.08f + 0.12f * amount) / rms) : 0.0f;
    gain = approach (gain, target, static_cast<float> (seconds), target < gain ? 0.08f : 0.3f);

    const auto level = telemetry.noiseLevel.load (std::memory_order_relaxed);
    levelDb = approach (levelDb, juce::Decibels::gainToDecibels (level, -90.0f), static_cast<float> (seconds), 0.25f);
    ModuleDisplay::tick (seconds);
}

juce::String NoiseDisplay::readoutText() const
{
    return levelDb <= -89.0f ? juce::String ("--dB") : juce::String (juce::roundToInt (levelDb)) + "dB";
}

void NoiseDisplay::drawContent (juce::Graphics& g, juce::Rectangle<float> screen)
{
    drawGraticule (g, screen, phosphor, staticAlpha());
    if (activity < 0.01f)
        return;

    juce::Path path;
    const auto n = static_cast<int> (trace.size());
    for (int i = 0; i < n; ++i)
    {
        const auto x = screen.getX() + screen.getWidth() * static_cast<float> (i) / static_cast<float> (n - 1);
        // Soft limiter instead of a hard clip, so peaks round off inside the glass.
        const auto v = 0.40f * std::tanh (trace[static_cast<size_t> (i)] * gain / 0.40f);
        const auto y = screen.getCentreY() - v * screen.getHeight();
        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }

    strokeGlow (g, path, phosphor, activity);
}

//======================================================================================================================
WobbleDisplay::WobbleDisplay (juce::AudioProcessorValueTreeState& s, dsp::EngineTelemetry& t)
    : ModuleDisplay (s, t, 1, ParamIDs::wobbleOn) {}

void WobbleDisplay::tick (double seconds)
{
    const auto synced = param (ParamIDs::wobbleSync) > 0.5f;
    const auto wowHz = synced ? 2.0 / syncDivisionInBeats (juce::roundToInt (param (ParamIDs::wobbleDivision))) : static_cast<double> (param (ParamIDs::wobbleWowRate));
    wowPhase += seconds * std::min (wowHz, 4.0) * 0.6;
    flutterPhase += seconds * std::min (static_cast<double> (param (ParamIDs::wobbleFlutterRate)), 30.0) * 0.12;
    ModuleDisplay::tick (seconds);
}

juce::String WobbleDisplay::readoutText() const
{
    if (param (ParamIDs::wobbleSync) > 0.5f)
        return syncDivisionNames()[juce::roundToInt (param (ParamIDs::wobbleDivision))];
    return rateText (param (ParamIDs::wobbleWowRate));
}

void WobbleDisplay::drawContent (juce::Graphics& g, juce::Rectangle<float> screen)
{
    drawGraticule (g, screen, phosphor, staticAlpha());
    if (activity < 0.01f)
        return;

    const auto amount = param (ParamIDs::wobbleAmount) * 0.01f;
    const auto balance = param (ParamIDs::wobbleBalance) * 0.01f;
    const auto flux = param (ParamIDs::wobbleFlux) * 0.01f;
    const auto stereo = param (ParamIDs::wobbleStereo) > 0.5f;
    const auto wow = std::min (1.0f, 2.0f * (1.0f - balance)) * amount;
    const auto flutter = std::min (1.0f, 2.0f * balance) * amount;
    const auto live = telemetry.wobbleMod.load (std::memory_order_relaxed);

    auto makeTrace = [&] (double offset)
    {
        juce::Path path;
        constexpr int points = 90;
        for (int i = 0; i <= points; ++i)
        {
            const auto t = static_cast<float> (i) / points;
            const auto x = screen.getX() + t * screen.getWidth();
            const auto w = std::sin (juce::MathConstants<double>::twoPi * (t * 1.15 + wowPhase + offset));
            const auto f = std::sin (juce::MathConstants<double>::twoPi * (t * 7.0 + flutterPhase * 3.0 + offset * 2.0));
            const auto drift = flux * 0.25f * std::sin (static_cast<float> (time) * 0.7f + t * 5.0f);
            const auto y = 0.34f * wow * static_cast<float> (w) + 0.12f * flutter * static_cast<float> (f) + drift * amount + 0.08f * live * (1.0f - t);
            const auto py = screen.getCentreY() - juce::jlimit (-0.40f, 0.40f, y) * screen.getHeight();
            if (i == 0)
                path.startNewSubPath (x, py);
            else
                path.lineTo (x, py);
        }
        return path;
    };

    // Stereo: the right channel is a quieter secondary trace, separated by phase only.
    if (stereo)
        strokeSecondary (g, makeTrace (0.25), phosphor, activity);
    strokeGlow (g, makeTrace (0.0), phosphor, activity);
}

//======================================================================================================================
DistortDisplay::DistortDisplay (juce::AudioProcessorValueTreeState& s, dsp::EngineTelemetry& t)
    : ModuleDisplay (s, t, 2, ParamIDs::distortOn) {}

void DistortDisplay::tick (double seconds)
{
    const auto drive = telemetry.distortDrive.load (std::memory_order_relaxed);
    const auto target = isActive() ? 0.22f + 0.78f * drive : 0.0f;
    glow = approach (glow, target, static_cast<float> (seconds), target > glow ? 0.05f : 0.35f);
    flicker = approach (flicker, random.nextFloat() * 0.08f, static_cast<float> (seconds), 0.04f);
    ModuleDisplay::tick (seconds);
}

juce::String DistortDisplay::readoutText() const
{
    const auto drive = param (ParamIDs::distortAmount) * param (ParamIDs::magnitude) * 0.01f;
    return "DRV " + juce::String (juce::roundToInt (drive)) + "%";
}

void DistortDisplay::drawTube (juce::Graphics& g, juce::Rectangle<float> area, float heat, float flick, float alpha) const
{
    const auto glass = area.withTrimmedBottom (area.getHeight() * 0.16f);
    const auto base = area.withTop (glass.getBottom() - 1.0f);

    // Warm glow, falling off radially (no hard edges).
    const auto glowColour = juce::Colour (0xffff7a2a);
    const auto h = juce::jlimit (0.0f, 1.0f, heat + flick * heat);
    const auto haloCentre = glass.getCentre().translated (0.0f, glass.getHeight() * 0.12f);
    const auto haloRadius = glass.getHeight() * 0.72f;
    juce::ColourGradient halo (glowColour.withAlpha (0.55f * h), haloCentre.x, haloCentre.y,
                               glowColour.withAlpha (0.0f), haloCentre.x + haloRadius, haloCentre.y, true);
    g.setGradientFill (halo);
    g.fillEllipse (juce::Rectangle<float> (haloRadius * 2.0f, haloRadius * 2.0f).withCentre (haloCentre));

    // Bakelite base and pins.
    g.setColour (juce::Colour (0xff1c1915).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (base, 2.0f);
    g.setColour (juce::Colour (0xff6b5a46).withAlpha (0.6f * alpha));
    g.drawRoundedRectangle (base, 2.0f, 0.8f);

    // Glass envelope.
    juce::Path envelope;
    envelope.addRoundedRectangle (glass.getX(), glass.getY(), glass.getWidth(), glass.getHeight(), glass.getWidth() * 0.45f, glass.getWidth() * 0.45f,
                                  true, true, false, false);
    g.setColour (juce::Colour (0x18ffffff).withMultipliedAlpha (alpha));
    g.fillPath (envelope);

    // Getter flash at the top.
    juce::ColourGradient getter (juce::Colour (0xff9aa0a6).withAlpha (0.55f * alpha), glass.getCentreX(), glass.getY(),
                                 juce::Colours::transparentBlack, glass.getCentreX(), glass.getY() + glass.getHeight() * 0.3f, false);
    g.setGradientFill (getter);
    g.fillPath (envelope);

    // Plate assembly.
    const auto plate = glass.reduced (glass.getWidth() * 0.22f, glass.getHeight() * 0.22f).withTrimmedBottom (glass.getHeight() * 0.05f);
    g.setColour (juce::Colour (0xff3b3d40).withMultipliedAlpha (alpha));
    g.fillRect (plate);
    g.setColour (juce::Colour (0xff6a6d70).withMultipliedAlpha (alpha));
    g.drawRect (plate, 0.8f);

    // Heater filament.
    const auto filament = juce::Rectangle<float> (plate.getWidth() * 0.32f, plate.getHeight() * 0.7f).withCentre (plate.getCentre());
    juce::ColourGradient heater (juce::Colour (0xffffd28a).withAlpha (h), filament.getCentreX(), filament.getCentreY(),
                                 glowColour.withAlpha (0.0f), filament.getCentreX(), filament.getY() - filament.getHeight() * 0.3f, true);
    g.setGradientFill (heater);
    g.fillEllipse (filament.expanded (filament.getWidth() * 0.8f, filament.getHeight() * 0.15f));
    g.setColour (juce::Colour (0xffffe0a0).withAlpha (std::min (1.0f, h * 1.3f)));
    g.fillRect (filament.withSizeKeepingCentre (1.4f, filament.getHeight()));

    // Glass outline and reflection.
    g.setColour (juce::Colours::white.withAlpha (0.28f * alpha));
    g.strokePath (envelope, juce::PathStrokeType (0.9f));
    g.setColour (juce::Colours::white.withAlpha (0.22f * alpha));
    g.fillRoundedRectangle (glass.getX() + glass.getWidth() * 0.16f, glass.getY() + glass.getHeight() * 0.18f, glass.getWidth() * 0.09f, glass.getHeight() * 0.55f, 1.5f);
}

void DistortDisplay::drawContent (juce::Graphics& g, juce::Rectangle<float> screen)
{
    const auto tubeH = screen.getHeight() * 0.86f;
    const auto tubeW = tubeH * 0.42f;
    const auto centre = screen.getCentre().translated (0.0f, screen.getHeight() * 0.05f);
    const auto alpha = staticAlpha();
    const auto tubeOffset = screen.getWidth() * 0.12f; // keep the pair clear of the DRV readout chip
    drawTube (g, juce::Rectangle<float> (tubeW, tubeH).withCentre (centre.translated (-tubeW * 0.8f - tubeOffset, 0.0f)), glow * activity, flicker, alpha);
    drawTube (g, juce::Rectangle<float> (tubeW, tubeH).withCentre (centre.translated (tubeW * 0.8f - tubeOffset, 0.0f)), glow * activity * 0.96f, flicker * 0.8f, alpha);
}

//======================================================================================================================
DigitalDisplay::DigitalDisplay (juce::AudioProcessorValueTreeState& s, dsp::EngineTelemetry& t)
    : ModuleDisplay (s, t, 3, ParamIDs::digitalOn) {}

void DigitalDisplay::tick (double seconds)
{
    rate = approach (rate, telemetry.digitalRate.load (std::memory_order_relaxed), static_cast<float> (seconds), 0.08f);
    bits = approach (bits, telemetry.digitalBits.load (std::memory_order_relaxed), static_cast<float> (seconds), 0.08f);
    ModuleDisplay::tick (seconds);
}

juce::String DigitalDisplay::readoutText() const
{
    // Bits are shown as a whole number; the trace below still uses the continuous value.
    const auto rateStr = rate >= 999.0f ? juce::String (rate / 1000.0f, 1) + "k" : juce::String (juce::roundToInt (rate));
    return rateStr + " " + juce::String (juce::jlimit (1, 16, juce::roundToInt (bits))) + "b";
}

void DigitalDisplay::drawContent (juce::Graphics& g, juce::Rectangle<float> screen)
{
    drawGraticule (g, screen, phosphor, staticAlpha());
    if (activity < 0.01f)
        return;

    const auto steps = juce::jlimit (5.0f, 160.0f, 160.0f * rate / 48000.0f);
    const auto levels = juce::jlimit (1.0f, 64.0f, std::pow (2.0f, bits - 1.0f));
    const auto phase = static_cast<float> (time) * 0.35f;
    auto wave = [&] (float t) { return 0.34f * std::sin (juce::MathConstants<float>::twoPi * (t * 1.35f + phase)); };

    juce::Path smooth, stepped;
    constexpr int points = 120;
    for (int i = 0; i <= points; ++i)
    {
        const auto t = static_cast<float> (i) / points;
        const auto p = juce::Point<float> (screen.getX() + t * screen.getWidth(), screen.getCentreY() - wave (t) * screen.getHeight());
        if (i == 0) smooth.startNewSubPath (p); else smooth.lineTo (p);
    }

    for (int s = 0; s < static_cast<int> (std::ceil (steps)); ++s)
    {
        const auto t0 = static_cast<float> (s) / steps;
        const auto t1 = std::min (1.0f, static_cast<float> (s + 1) / steps);
        auto v = wave (t0) / 0.34f;
        v = std::round (v * levels) / levels * 0.34f;
        const auto y = screen.getCentreY() - v * screen.getHeight();
        const auto x0 = screen.getX() + t0 * screen.getWidth();
        const auto x1 = screen.getX() + t1 * screen.getWidth();
        if (s == 0) stepped.startNewSubPath (x0, y); else stepped.lineTo (x0, y);
        stepped.lineTo (x1, y);
    }

    // The original signal is the reference line, the held samples are the primary trace.
    strokeSecondary (g, smooth, phosphor, activity * 0.6f);
    strokeGlow (g, stepped, phosphor, activity);
}

//======================================================================================================================
SpaceDisplay::SpaceDisplay (juce::AudioProcessorValueTreeState& s, dsp::EngineTelemetry& t)
    : ModuleDisplay (s, t, 4, ParamIDs::spaceOn) {}

void SpaceDisplay::tick (double seconds)
{
    energy = approach (energy, telemetry.spaceEnergy.load (std::memory_order_relaxed), static_cast<float> (seconds), 0.2f);
    ModuleDisplay::tick (seconds);
}

float SpaceDisplay::decaySeconds() const
{
    // Same mapping as the engine's RT60 range per type (see SpaceModule::spec).
    static constexpr float minRt[] { 0.12f, 0.25f, 0.5f, 0.8f, 0.6f, 0.3f };
    static constexpr float maxRt[] { 1.2f, 3.0f, 8.0f, 14.0f, 5.0f, 6.0f };
    const auto type = juce::jlimit (0, 5, juce::roundToInt (param (ParamIDs::spaceType)));
    const auto d = param (ParamIDs::spaceDecay) * 0.01f;
    return minRt[type] * std::pow (maxRt[type] / minRt[type], d);
}

juce::String SpaceDisplay::readoutText() const
{
    const auto rt = decaySeconds();
    return juce::String (rt, rt < 10.0f ? 1 : 0) + "s " + juce::String (juce::roundToInt (param (ParamIDs::spacePreDelay))) + "ms";
}

void SpaceDisplay::drawContent (juce::Graphics& g, juce::Rectangle<float> screen)
{
    // Echogram: direct sound, early reflections after the pre-delay, then the decaying tail.
    // The time axis is fixed (3.2 s across the screen, square-root scaled so short rooms still get room to read)
    // and the tail is drawn in dB, as decay plots are, so it falls in a straight line to -60 dB at the decay time.
    const auto alpha = staticAlpha();
    drawGraticule (g, screen, phosphor, alpha);

    const auto plot = screen.reduced (6.0f, 6.0f).withTrimmedTop (2.0f);
    const auto axisSeconds = 3.2f;
    const auto baseline = plot.getBottom();
    auto xAt = [&] (float seconds) { return plot.getX() + plot.getWidth() * juce::jlimit (0.0f, 1.0f, std::sqrt (juce::jmax (0.0f, seconds) / axisSeconds)); };

    const auto type = juce::jlimit (0, 5, juce::roundToInt (param (ParamIDs::spaceType)));
    const auto preDelay = param (ParamIDs::spacePreDelay) * 0.001f;
    const auto rt = decaySeconds();
    const auto stereo = param (ParamIDs::spaceStereo) > 0.5f;
    const auto amount = param (ParamIDs::spaceAmount) * 0.01f;
    const auto breathe = 0.6f + 0.4f * juce::jlimit (0.0f, 1.0f, energy * 6.0f);
    const auto intensity = juce::jlimit (0.3f, 1.0f, alpha * (0.45f + 0.55f * std::sqrt (amount)));

    // Early reflection pattern per type (fixed, so it never shimmers).
    struct Pattern { int count; float spanSeconds; };
    static constexpr Pattern patterns[] { { 6, 0.04f }, { 10, 0.08f }, { 18, 0.03f }, { 8, 0.15f }, { 5, 0.15f }, { 12, 0.144f } };
    const auto& pattern = patterns[type];

    auto drawChannel = [&] (float tailScale, float jitter, bool primary)
    {
        juce::Path spikes;
        juce::Random r (static_cast<juce::int64> (type * 97 + 11));
        const auto direct = xAt (0.0f);
        spikes.startNewSubPath (direct, baseline);
        spikes.lineTo (direct, baseline - 0.80f * plot.getHeight());

        float lastEarly = preDelay;
        for (int i = 0; i < pattern.count; ++i)
        {
            const auto frac = type == 5 || type == 4 ? static_cast<float> (i) / pattern.count
                                                     : std::pow (r.nextFloat(), 0.8f);
            const auto t = preDelay + frac * pattern.spanSeconds;
            lastEarly = std::max (lastEarly, t);
            const auto height = juce::jmap (static_cast<float> (i) / std::max (1, pattern.count - 1), 0.65f, 0.30f) * plot.getHeight()
                              * (type == 5 ? 0.85f : 0.7f + 0.3f * r.nextFloat());
            const auto x = xAt (t) + jitter * (r.nextFloat() - 0.5f);
            spikes.startNewSubPath (x, baseline);
            spikes.lineTo (x, baseline - height);
        }

        // Exponential tail (-60 dB over the decay time), breathing with the live wet level.
        juce::Path tail;
        const auto h0 = 0.55f * plot.getHeight() * breathe;
        const auto start = lastEarly;
        const auto tailSeconds = rt * tailScale;
        tail.startNewSubPath (xAt (start), baseline);
        constexpr int points = 64;
        for (int i = 0; i <= points; ++i)
        {
            // Uniform steps along the (square-root) screen axis, mapped back to seconds.
            const auto u0 = std::sqrt (start / axisSeconds);
            const auto u = u0 + (1.0f - u0) * static_cast<float> (i) / points;
            const auto t = u * u * axisSeconds;
            auto env = h0 * juce::jmax (0.0f, 1.0f - (t - start) / std::max (0.05f, tailSeconds));
            if (type == 4) // spring: a little ripple riding on the tail
                env *= 1.0f + 0.15f * std::sin ((t - start) * 60.0f);
            tail.lineTo (xAt (t), baseline - env);
        }
        auto fill = tail;
        fill.lineTo (xAt (axisSeconds), baseline);
        fill.closeSubPath();

        if (primary)
        {
            g.setColour (phosphor.withAlpha (0.10f * intensity));
            g.fillPath (fill);
            strokeGlow (g, tail, phosphor, intensity);
            strokeOutline (g, spikes, phosphor, 0.75f * intensity);
        }
        else
        {
            strokeSecondary (g, tail, phosphor, intensity);
        }
    };

    if (stereo)
        drawChannel (0.93f, 2.0f, false);
    drawChannel (1.0f, 0.0f, true);

    // Baseline.
    g.setColour (phosphor.withAlpha (0.25f * alpha));
    g.drawHorizontalLine (juce::roundToInt (baseline), plot.getX(), plot.getRight());
}

//======================================================================================================================
MagneticDisplay::MagneticDisplay (juce::AudioProcessorValueTreeState& s, dsp::EngineTelemetry& t)
    : ModuleDisplay (s, t, 5, ParamIDs::magneticOn) {}

void MagneticDisplay::tick (double seconds)
{
    gain = approach (gain, telemetry.magneticGain.load (std::memory_order_relaxed), static_cast<float> (seconds), 0.04f);
    dropout = approach (dropout, telemetry.magneticDropout.load (std::memory_order_relaxed), static_cast<float> (seconds), 0.05f);

    // Tape transport: the pack moves from the left reel to the right one over about 90 s, then rewinds.
    if (isActive())
    {
        transport += seconds / 90.0;
        transport -= std::floor (transport);
    }

    const auto speed = isActive() ? 1.4 * (0.75 + 0.25 * gain) : 0.0;
    const auto p = static_cast<float> (transport);
    leftAngle += seconds * speed / (0.42 + 0.50 * (1.0 - p));
    rightAngle += seconds * speed / (0.42 + 0.50 * p);
    ModuleDisplay::tick (seconds);
}

juce::String MagneticDisplay::readoutText() const
{
    if (dropout > 0.05f)
        return "DROP";
    if (param (ParamIDs::magneticSync) > 0.5f)
        return syncDivisionNames()[juce::roundToInt (param (ParamIDs::magneticDivision))];
    return rateText (param (ParamIDs::magneticRate));
}

juce::Colour MagneticDisplay::readoutColour() const
{
    return dropout > 0.05f ? Colours::ledRed : phosphor;
}

void MagneticDisplay::drawReel (juce::Graphics& g, juce::Point<float> centre, float flange, float pack, float angle, float alpha, float live) const
{
    const auto circle = [] (juce::Point<float> c, float r) { return juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c); };

    // Flange (outer reel edge).
    juce::Path flangePath;
    flangePath.addEllipse (circle (centre, flange));
    strokeOutline (g, flangePath, phosphor, 0.55f * alpha);

    // Tape pack: a solid disc with a defined edge.
    g.setColour (phosphor.withAlpha (0.24f * alpha));
    g.fillEllipse (circle (centre, pack));
    juce::Path packEdge;
    packEdge.addEllipse (circle (centre, pack));
    strokeOutline (g, packEdge, phosphor, 0.5f * alpha);

    // NAB-style hub: dark centre with three spokes reaching into the pack.
    const auto hubR = flange * 0.22f;
    g.setColour (juce::Colour (0xff050706));
    g.fillEllipse (circle (centre, hubR));
    juce::Path hub;
    hub.addEllipse (circle (centre, hubR));
    for (int k = 0; k < 3; ++k)
    {
        const auto a = angle + juce::MathConstants<float>::twoPi * static_cast<float> (k) / 3.0f;
        hub.startNewSubPath (centre.x + std::cos (a) * hubR, centre.y + std::sin (a) * hubR);
        hub.lineTo (centre.x + std::cos (a) * flange * 0.40f, centre.y + std::sin (a) * flange * 0.40f);
    }
    if (live > 0.01f)
        strokeGlow (g, hub, phosphor, live, 1.2f);
    else
        strokeOutline (g, hub, phosphor, 0.55f * alpha);
}

void MagneticDisplay::drawContent (juce::Graphics& g, juce::Rectangle<float> screen)
{
    const auto alpha = staticAlpha();
    const auto w = screen.getWidth();
    const auto h = screen.getHeight();
    const auto flange = h * 0.33f;
    const auto leftC = juce::Point<float> (screen.getX() + w * 0.25f, screen.getY() + h * 0.42f);
    const auto rightC = juce::Point<float> (screen.getX() + w * 0.75f, leftC.y);
    const auto p = static_cast<float> (transport);
    const auto leftPack = flange * (0.42f + 0.50f * (1.0f - p));
    const auto rightPack = flange * (0.42f + 0.50f * p);

    // Tape path: from the outside of each pack, round two guide rollers, across the head.
    const auto rollerY = screen.getY() + h * 0.82f;
    const auto rollerL = juce::Point<float> (screen.getX() + w * 0.38f, rollerY);
    const auto rollerR = juce::Point<float> (screen.getX() + w * 0.62f, rollerY);
    const auto rollerR0 = 2.2f;
    const auto tapeAlpha = juce::jlimit (0.2f, 1.0f, gain) * activity;

    juce::Path leftRun, rightRun, headRun;
    leftRun.startNewSubPath (leftC.x - leftPack * 0.35f, leftC.y + leftPack * 0.94f);
    leftRun.lineTo (rollerL.x - rollerR0 * 0.5f, rollerL.y + rollerR0);
    rightRun.startNewSubPath (rollerR.x + rollerR0 * 0.5f, rollerR.y + rollerR0);
    rightRun.lineTo (rightC.x + rightPack * 0.35f, rightC.y + rightPack * 0.94f);
    headRun.startNewSubPath (rollerL.x, rollerL.y + rollerR0);
    headRun.lineTo (rollerR.x, rollerR.y + rollerR0);

    for (auto* run : { &leftRun, &rightRun })
        if (tapeAlpha > 0.01f)
            strokeGlow (g, *run, phosphor, tapeAlpha, 1.0f);
        else
            strokeOutline (g, *run, phosphor, 0.5f * alpha);

    // During a dropout the tape over the head fades out.
    const auto headAlpha = tapeAlpha * (1.0f - 0.75f * juce::jlimit (0.0f, 1.0f, dropout * 3.0f));
    if (headAlpha > 0.01f)
        strokeGlow (g, headRun, phosphor, headAlpha, 1.0f);
    else
        strokeOutline (g, headRun, phosphor, 0.5f * alpha);

    // Guide rollers and the head.
    for (auto c : { rollerL, rollerR })
    {
        juce::Path roller;
        roller.addEllipse (juce::Rectangle<float> (rollerR0 * 2.0f, rollerR0 * 2.0f).withCentre (c));
        strokeOutline (g, roller, phosphor, 0.7f * alpha);
    }

    const auto head = juce::Rectangle<float> (10.0f, 4.5f).withCentre ({ screen.getCentreX(), rollerY + rollerR0 + 3.0f });
    g.setColour (phosphor.withAlpha (0.55f * alpha));
    g.fillRoundedRectangle (head, 1.5f);
    g.setColour (juce::Colour (0xff050706));
    g.drawVerticalLine (juce::roundToInt (head.getCentreX()), head.getY(), head.getBottom());

    drawReel (g, leftC, flange, leftPack, static_cast<float> (leftAngle), alpha, activity);
    drawReel (g, rightC, flange, rightPack, static_cast<float> (rightAngle), alpha, activity);
}

//======================================================================================================================
VuMeter::VuMeter (const juce::String& c) : caption (c)
{
    setInterceptsMouseClicks (false, false);
}

void VuMeter::setLevels (float rms, float peak, double seconds)
{
    // The movement has mechanical stops just outside the printed scale.
    const auto vu = juce::jlimit (-21.5f, 3.6f, juce::Decibels::gainToDecibels (rms, -100.0f) + 18.0f);

    // Critically damped needle (about 300 ms to settle, like a real VU movement).
    const auto dt = static_cast<float> (seconds) / 4.0f;
    constexpr float omega = 13.0f, zeta = 0.85f;
    for (int i = 0; i < 4; ++i)
    {
        const auto accel = omega * omega * (vu - needle) - 2.0f * zeta * omega * velocity;
        velocity += accel * dt;
        needle += velocity * dt;
        if (needle > 3.6f || needle < -21.5f)
        {
            needle = juce::jlimit (-21.5f, 3.6f, needle);
            velocity *= -0.2f; // bounce off the stop pin
        }
    }

    peakHold = peak > 0.89f ? 1.0f : std::max (0.0f, peakHold - static_cast<float> (seconds) * 1.5f);
    repaint();
}

void VuMeter::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const auto scale = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());

    auto vuToAngle = [] (float vu)
    {
        const auto lo = std::pow (10.0f, -20.0f / 20.0f);
        const auto hi = std::pow (10.0f, 3.0f / 20.0f);
        const auto pos = (std::pow (10.0f, juce::jlimit (-22.0f, 4.0f, vu) / 20.0f) - lo) / (hi - lo);
        return juce::degreesToRadians (-34.0f + 68.0f * pos);
    };

    const auto faceArea = area.reduced (3.0f);
    const auto pivot = juce::Point<float> (faceArea.getCentreX(), faceArea.getBottom() + faceArea.getHeight() * 0.5f);
    const auto arcRadius = faceArea.getHeight() * 1.05f;

    if (! face.isValid() || ! juce::approximatelyEqual (faceScale, scale) || face.getWidth() != juce::roundToInt (area.getWidth() * scale))
    {
        faceScale = scale;
        face = juce::Image (juce::Image::ARGB, juce::roundToInt (area.getWidth() * scale), juce::roundToInt (area.getHeight() * scale), true);
        juce::Graphics fg (face);
        fg.addTransform (juce::AffineTransform::scale (scale));

        // Bezel.
        fg.setColour (juce::Colour (0xff0d0d0e));
        fg.fillRoundedRectangle (area, 4.0f);

        // Backlit ivory face.
        juce::ColourGradient paper (juce::Colour (0xfff6e7bd), faceArea.getCentreX(), faceArea.getBottom(),
                                    juce::Colour (0xffcfae6c), faceArea.getCentreX(), faceArea.getY() - faceArea.getHeight() * 0.3f, true);
        fg.setGradientFill (paper);
        fg.fillRoundedRectangle (faceArea, 3.0f);

        // Scale arc.
        juce::Path arc;
        arc.addCentredArc (pivot.x, pivot.y, arcRadius, arcRadius, 0.0f, vuToAngle (-20.0f), vuToAngle (0.0f), true);
        fg.setColour (Colours::ink);
        fg.strokePath (arc, juce::PathStrokeType (1.0f));
        juce::Path red;
        red.addCentredArc (pivot.x, pivot.y, arcRadius, arcRadius, 0.0f, vuToAngle (0.0f), vuToAngle (3.0f), true);
        fg.setColour (juce::Colour (0xffc0392b));
        fg.strokePath (red, juce::PathStrokeType (3.0f));

        const float marks[] { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f };
        fg.setFont (Fonts::get().labelMedium (std::max (7.0f, faceArea.getHeight() * 0.16f)));
        for (auto m : marks)
        {
            const auto a = vuToAngle (m);
            const auto p1 = juce::Point<float> (pivot.x + std::sin (a) * arcRadius, pivot.y - std::cos (a) * arcRadius);
            const auto p2 = juce::Point<float> (pivot.x + std::sin (a) * (arcRadius + faceArea.getHeight() * 0.08f), pivot.y - std::cos (a) * (arcRadius + faceArea.getHeight() * 0.08f));
            fg.setColour (m > 0.0f ? juce::Colour (0xffc0392b) : Colours::ink);
            fg.drawLine ({ p1, p2 }, 1.0f);

            const auto mi = juce::roundToInt (m);
            if (mi == -20 || mi == -10 || mi == -5 || mi == -3 || mi == 0 || mi == 3)
            {
                const auto lp = juce::Point<float> (pivot.x + std::sin (a) * (arcRadius + faceArea.getHeight() * 0.16f), pivot.y - std::cos (a) * (arcRadius + faceArea.getHeight() * 0.16f));
                const auto txt = m > 0.0f ? "+" + juce::String (juce::roundToInt (m)) : juce::String (juce::roundToInt (std::abs (m)));
                fg.drawText (txt, juce::Rectangle<float> (20.0f, 10.0f).withCentre (lp), juce::Justification::centred, false);
            }
        }

        fg.setColour (Colours::ink.withAlpha (0.85f));
        fg.setFont (Fonts::get().labelBold (std::max (8.0f, faceArea.getHeight() * 0.2f)));
        fg.drawText ("VU", faceArea.withTrimmedTop (faceArea.getHeight() * 0.5f), juce::Justification::centred, false);
        fg.setFont (Fonts::get().label (std::max (7.0f, faceArea.getHeight() * 0.14f)));
        fg.drawText (caption, faceArea.reduced (5.0f, 3.0f), juce::Justification::bottomLeft, false);
    }

    g.drawImage (face, area, juce::RectanglePlacement::stretchToFit);

    // Needle.
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (faceArea.toNearestInt());
        const auto a = vuToAngle (needle);
        const auto tip = juce::Point<float> (pivot.x + std::sin (a) * arcRadius * 1.04f, pivot.y - std::cos (a) * arcRadius * 1.04f);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.drawLine ({ pivot.translated (1.5f, 1.0f), tip.translated (1.5f, 1.0f) }, 1.4f);
        g.setColour (juce::Colour (0xff141414));
        g.drawLine ({ pivot, tip }, 1.2f);
    }

    // Peak LED.
    const auto led = juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ faceArea.getRight() - 6.0f, faceArea.getY() + 6.0f });
    g.setColour (peakHold > 0.0f ? Colours::ledRed.withAlpha (0.4f + 0.6f * peakHold) : juce::Colour (0xff5a1a12));
    g.fillEllipse (led);

    // Glass.
    juce::ColourGradient glass (juce::Colours::white.withAlpha (0.25f), faceArea.getX(), faceArea.getY(), juce::Colours::transparentWhite,
                                faceArea.getX(), faceArea.getCentreY(), false);
    g.setGradientFill (glass);
    g.fillRoundedRectangle (faceArea.withHeight (faceArea.getHeight() * 0.45f), 3.0f);
}

} // namespace afterglow::ui
