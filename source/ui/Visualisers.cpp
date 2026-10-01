#include "Visualisers.h"
#include "../Parameters.h"

namespace afterglow::ui
{
namespace
{
    void strokeGlow (juce::Graphics& g, const juce::Path& path, juce::Colour colour, float intensity, float width = 1.4f)
    {
        g.setColour (colour.withAlpha (0.10f * intensity));
        g.strokePath (path, juce::PathStrokeType (width * 4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour.withAlpha (0.28f * intensity));
        g.strokePath (path, juce::PathStrokeType (width * 2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour.withAlpha (0.95f * intensity));
        g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawGraticule (juce::Graphics& g, juce::Rectangle<float> screen, juce::Colour colour)
    {
        g.setColour (colour.withAlpha (0.07f));
        for (int i = 1; i < 6; ++i)
            g.drawVerticalLine (juce::roundToInt (screen.getX() + screen.getWidth() * static_cast<float> (i) / 6.0f), screen.getY(), screen.getBottom());
        for (int i = 1; i < 4; ++i)
            g.drawHorizontalLine (juce::roundToInt (screen.getY() + screen.getHeight() * static_cast<float> (i) / 4.0f), screen.getX(), screen.getRight());
        g.setColour (colour.withAlpha (0.12f));
        g.drawHorizontalLine (juce::roundToInt (screen.getCentreY()), screen.getX(), screen.getRight());
    }

    float approach (float current, float target, float seconds, float timeConstant)
    {
        return target + (current - target) * std::exp (-seconds / std::max (0.001f, timeConstant));
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

void ModuleDisplay::tick (double seconds)
{
    time += seconds;
    activity = approach (activity, isActive() ? 1.0f : 0.0f, static_cast<float> (seconds), 0.12f);
    repaint();
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

    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (screen, 4.0f);
        g.reduceClipRegion (clip);
        drawContent (g, screen);

        // Scanlines.
        g.setColour (juce::Colours::black.withAlpha (0.16f));
        for (auto y = screen.getY() + 1.0f; y < screen.getBottom(); y += 2.0f)
            g.drawHorizontalLine (juce::roundToInt (y), screen.getX(), screen.getRight());
    }

    // Glass: inner shadow and a diagonal reflection.
    juce::ColourGradient innerShadow (juce::Colours::black.withAlpha (0.7f), screen.getX(), screen.getY(), juce::Colours::transparentBlack, screen.getX(), screen.getY() + 7.0f, false);
    g.setGradientFill (innerShadow);
    g.fillRoundedRectangle (screen, 4.0f);

    juce::Path reflection;
    reflection.startNewSubPath (screen.getX(), screen.getY());
    reflection.lineTo (screen.getX() + screen.getWidth() * 0.62f, screen.getY());
    reflection.lineTo (screen.getX() + screen.getWidth() * 0.38f, screen.getBottom());
    reflection.lineTo (screen.getX(), screen.getBottom());
    reflection.closeSubPath();
    juce::Graphics::ScopedSaveState save (g);
    juce::Path clip;
    clip.addRoundedRectangle (screen, 4.0f);
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
    auto peak = 1.0e-4f;

    for (size_t i = 0; i < trace.size(); ++i)
    {
        const auto idx = (write - static_cast<int> (trace.size()) + static_cast<int> (i) + dsp::EngineTelemetry::scopeSize) % dsp::EngineTelemetry::scopeSize;
        trace[i] = telemetry.noiseScope[static_cast<size_t> (idx)].load (std::memory_order_relaxed);
        peak = std::max (peak, std::abs (trace[i]));
    }

    // Show the shape of the noise; the knob position sets how big it looks.
    const auto amount = param (ParamIDs::noiseAmount) * 0.01f;
    const auto target = juce::jlimit (0.0f, 400.0f, (0.25f + 0.75f * amount) / peak);
    gain = approach (gain, target, static_cast<float> (seconds), target < gain ? 0.05f : 0.25f);
    if (telemetry.noiseLevel.load (std::memory_order_relaxed) < 1.0e-6f)
        gain = approach (gain, 0.0f, static_cast<float> (seconds), 0.15f);

    ModuleDisplay::tick (seconds);
}

void NoiseDisplay::drawContent (juce::Graphics& g, juce::Rectangle<float> screen)
{
    drawGraticule (g, screen, phosphor);
    if (activity < 0.01f)
        return;

    juce::Path path;
    const auto n = static_cast<int> (trace.size());
    for (int i = 0; i < n; ++i)
    {
        const auto x = screen.getX() + screen.getWidth() * static_cast<float> (i) / static_cast<float> (n - 1);
        const auto y = screen.getCentreY() - juce::jlimit (-0.48f, 0.48f, trace[static_cast<size_t> (i)] * gain * 0.42f) * screen.getHeight();
        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }

    strokeGlow (g, path, phosphor, activity, 1.1f);
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

void WobbleDisplay::drawContent (juce::Graphics& g, juce::Rectangle<float> screen)
{
    drawGraticule (g, screen, phosphor);
    if (activity < 0.01f)
        return;

    const auto amount = param (ParamIDs::wobbleAmount) * 0.01f;
    const auto balance = param (ParamIDs::wobbleBalance) * 0.01f;
    const auto flux = param (ParamIDs::wobbleFlux) * 0.01f;
    const auto stereo = param (ParamIDs::wobbleStereo) > 0.5f;
    const auto wow = std::min (1.0f, 2.0f * (1.0f - balance)) * amount;
    const auto flutter = std::min (1.0f, 2.0f * balance) * amount;
    const auto live = telemetry.wobbleMod.load (std::memory_order_relaxed);

    for (int line = 0; line < (stereo ? 2 : 1); ++line)
    {
        juce::Path path;
        const auto offset = line == 0 ? 0.0 : 0.25;
        constexpr int points = 90;

        for (int i = 0; i <= points; ++i)
        {
            const auto t = static_cast<float> (i) / points;
            const auto x = screen.getX() + t * screen.getWidth();
            const auto w = std::sin (juce::MathConstants<double>::twoPi * (t * 1.15 + wowPhase + offset));
            const auto f = std::sin (juce::MathConstants<double>::twoPi * (t * 7.0 + flutterPhase * 3.0 + offset * 2.0));
            const auto drift = flux * 0.25f * std::sin (static_cast<float> (time) * 0.7f + t * 5.0f);
            const auto y = 0.34f * wow * static_cast<float> (w) + 0.12f * flutter * static_cast<float> (f) + drift * amount + 0.08f * live * (1.0f - t);
            const auto py = screen.getCentreY() + (line == 0 ? 0.0f : 2.5f) - juce::jlimit (-0.46f, 0.46f, y) * screen.getHeight();
            if (i == 0)
                path.startNewSubPath (x, py);
            else
                path.lineTo (x, py);
        }

        strokeGlow (g, path, phosphor.withMultipliedBrightness (line == 0 ? 1.0f : 0.7f), activity * (line == 0 ? 1.0f : 0.7f), 1.3f);
    }
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

void DistortDisplay::drawTube (juce::Graphics& g, juce::Rectangle<float> area, float heat, float flick) const
{
    const auto glass = area.withTrimmedBottom (area.getHeight() * 0.16f);
    const auto base = area.withTop (glass.getBottom() - 1.0f);

    // Bakelite base and pins.
    g.setColour (juce::Colour (0xff1c1915));
    g.fillRoundedRectangle (base, 2.0f);
    g.setColour (juce::Colour (0xff6b5a46).withAlpha (0.6f));
    g.drawRoundedRectangle (base, 2.0f, 0.8f);

    // Glass envelope.
    juce::Path envelope;
    envelope.addRoundedRectangle (glass.getX(), glass.getY(), glass.getWidth(), glass.getHeight(), glass.getWidth() * 0.45f, glass.getWidth() * 0.45f,
                                  true, true, false, false);

    // Warm glow through the glass.
    const auto glowColour = juce::Colour (0xffff7a2a);
    const auto h = juce::jlimit (0.0f, 1.0f, heat + flick * heat);
    juce::ColourGradient halo (glowColour.withAlpha (0.55f * h), glass.getCentreX(), glass.getY() + glass.getHeight() * 0.62f,
                               glowColour.withAlpha (0.0f), glass.getCentreX(), glass.getY() - glass.getHeight() * 0.1f, true);
    g.setGradientFill (halo);
    g.fillRect (area.expanded (area.getWidth() * 0.6f, 0.0f));

    g.setColour (juce::Colour (0x18ffffff));
    g.fillPath (envelope);

    // Getter flash at the top.
    juce::ColourGradient getter (juce::Colour (0xff9aa0a6).withAlpha (0.55f), glass.getCentreX(), glass.getY(),
                                 juce::Colours::transparentBlack, glass.getCentreX(), glass.getY() + glass.getHeight() * 0.3f, false);
    g.setGradientFill (getter);
    g.fillPath (envelope);

    // Plate assembly.
    const auto plate = glass.reduced (glass.getWidth() * 0.22f, glass.getHeight() * 0.22f).withTrimmedBottom (glass.getHeight() * 0.05f);
    g.setColour (juce::Colour (0xff3b3d40));
    g.fillRect (plate);
    g.setColour (juce::Colour (0xff6a6d70));
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
    g.setColour (juce::Colours::white.withAlpha (0.28f));
    g.strokePath (envelope, juce::PathStrokeType (0.9f));
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.fillRoundedRectangle (glass.getX() + glass.getWidth() * 0.16f, glass.getY() + glass.getHeight() * 0.18f, glass.getWidth() * 0.09f, glass.getHeight() * 0.55f, 1.5f);
}

void DistortDisplay::drawContent (juce::Graphics& g, juce::Rectangle<float> screen)
{
    const auto tubeH = screen.getHeight() * 0.9f;
    const auto tubeW = tubeH * 0.42f;
    const auto centre = screen.getCentre().translated (0.0f, screen.getHeight() * 0.04f);
    drawTube (g, juce::Rectangle<float> (tubeW, tubeH).withCentre (centre.translated (-tubeW * 0.72f, 0.0f)), glow * activity, flicker);
    drawTube (g, juce::Rectangle<float> (tubeW, tubeH).withCentre (centre.translated (tubeW * 0.72f, 0.0f)), glow * activity * 0.96f, flicker * 0.8f);
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

void DigitalDisplay::drawContent (juce::Graphics& g, juce::Rectangle<float> screen)
{
    drawGraticule (g, screen, phosphor);
    if (activity < 0.01f)
        return;

    const auto steps = juce::jlimit (5.0f, 160.0f, 160.0f * rate / 48000.0f);
    const auto levels = juce::jlimit (1.0f, 64.0f, std::pow (2.0f, bits - 1.0f));
    const auto phase = static_cast<float> (time) * 0.35f;
    auto wave = [&] (float t) { return 0.36f * std::sin (juce::MathConstants<float>::twoPi * (t * 1.35f + phase)); };

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
        auto v = wave (t0) / 0.36f;
        v = std::round (v * levels) / levels * 0.36f;
        const auto y = screen.getCentreY() - v * screen.getHeight();
        const auto x0 = screen.getX() + t0 * screen.getWidth();
        const auto x1 = screen.getX() + t1 * screen.getWidth();
        if (s == 0) stepped.startNewSubPath (x0, y); else stepped.lineTo (x0, y);
        stepped.lineTo (x1, y);
    }

    g.setColour (phosphor.withAlpha (0.18f * activity));
    g.strokePath (smooth, juce::PathStrokeType (1.0f));
    strokeGlow (g, stepped, phosphor, activity, 1.2f);

    // Read-out of the current resolution.
    const auto info = (rate >= 999.0f ? juce::String (rate / 1000.0f, 1) + "k" : juce::String (juce::roundToInt (rate))) + "  "
                    + (bits >= 15.9f ? juce::String ("16") : juce::String (bits, 1)) + "b";
    g.setFont (Fonts::get().display (10.5f));
    g.setColour (phosphor.withAlpha (0.75f * activity));
    g.drawText (info, screen.reduced (5.0f, 3.0f), juce::Justification::bottomRight, false);
}

//======================================================================================================================
SpaceDisplay::SpaceDisplay (juce::AudioProcessorValueTreeState& s, dsp::EngineTelemetry& t)
    : ModuleDisplay (s, t, 4, ParamIDs::spaceOn) {}

void SpaceDisplay::tick (double seconds)
{
    const auto amount = param (ParamIDs::spaceAmount) * 0.01f;
    const auto stereo = param (ParamIDs::spaceStereo) > 0.5f;
    energy = approach (energy, telemetry.spaceEnergy.load (std::memory_order_relaxed), static_cast<float> (seconds), 0.15f);

    spawnTimer += seconds;
    const auto interval = 0.55 - 0.25 * amount;
    if (spawnTimer > interval && isActive() && amount > 0.0f)
    {
        spawnTimer = 0.0;
        const auto strength = juce::jlimit (0.15f, 1.0f, 0.35f + 4.0f * energy) * (0.4f + 0.6f * amount);
        if (stereo)
        {
            rings.push_back ({ 0.0f, strength, -1 });
            rings.push_back ({ 0.0f, strength * 0.85f, 1 });
        }
        else
        {
            rings.push_back ({ 0.0f, strength, 0 });
        }
    }

    for (auto& r : rings)
        r.age += static_cast<float> (seconds);

    const auto decay = 0.4f + 2.6f * param (ParamIDs::spaceDecay) * 0.01f;
    rings.erase (std::remove_if (rings.begin(), rings.end(), [decay] (const Ring& r) { return r.age > decay * 2.2f + 0.3f; }), rings.end());
    ModuleDisplay::tick (seconds);
}

void SpaceDisplay::drawContent (juce::Graphics& g, juce::Rectangle<float> screen)
{
    if (activity < 0.01f)
        return;

    const auto decay = 0.4f + 2.6f * param (ParamIDs::spaceDecay) * 0.01f;
    const auto maxRadius = screen.getWidth() * 0.55f;

    for (const auto& r : rings)
    {
        const auto centre = screen.getCentre().translated (static_cast<float> (r.side) * screen.getWidth() * 0.18f, 0.0f);
        const auto radius = 3.0f + maxRadius * (1.0f - std::exp (-r.age * 1.6f / decay));
        const auto alpha = r.strength * std::exp (-r.age / decay) * activity;
        if (alpha < 0.01f)
            continue;

        juce::Path ring;
        ring.addEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 1.3f).withCentre (centre));
        strokeGlow (g, ring, phosphor, juce::jlimit (0.0f, 1.0f, alpha), 1.1f);
    }

    // Two speaker-like source dots.
    const auto dotAlpha = 0.5f + 0.5f * juce::jlimit (0.0f, 1.0f, energy * 6.0f);
    g.setColour (phosphor.withAlpha (dotAlpha * activity));
    g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (screen.getCentre()));
}

//======================================================================================================================
MagneticDisplay::MagneticDisplay (juce::AudioProcessorValueTreeState& s, dsp::EngineTelemetry& t)
    : ModuleDisplay (s, t, 5, ParamIDs::magneticOn) {}

void MagneticDisplay::tick (double seconds)
{
    gain = approach (gain, telemetry.magneticGain.load (std::memory_order_relaxed), static_cast<float> (seconds), 0.04f);
    dropout = approach (dropout, telemetry.magneticDropout.load (std::memory_order_relaxed), static_cast<float> (seconds), 0.05f);
    const auto speed = isActive() ? 1.6 * (0.75 + 0.25 * gain) : 0.0;
    reelAngle += seconds * speed;
    ModuleDisplay::tick (seconds);
}

void MagneticDisplay::drawReel (juce::Graphics& g, juce::Point<float> centre, float radius, float tapeRadius, float angle) const
{
    // Tape pack.
    g.setColour (phosphor.withAlpha (0.16f * activity));
    g.fillEllipse (juce::Rectangle<float> (tapeRadius * 2.0f, tapeRadius * 2.0f).withCentre (centre));

    juce::Path outline;
    outline.addEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    strokeGlow (g, outline, phosphor, activity * 0.6f, 0.9f);

    juce::Path hub;
    hub.addEllipse (juce::Rectangle<float> (radius * 0.7f, radius * 0.7f).withCentre (centre));
    for (int k = 0; k < 6; ++k)
    {
        const auto a = angle + juce::MathConstants<float>::twoPi * static_cast<float> (k) / 6.0f;
        hub.startNewSubPath (centre.x + std::cos (a) * radius * 0.35f, centre.y + std::sin (a) * radius * 0.35f);
        hub.lineTo (centre.x + std::cos (a) * radius * 0.55f, centre.y + std::sin (a) * radius * 0.55f);
    }
    strokeGlow (g, hub, phosphor, activity, 1.1f);
}

void MagneticDisplay::drawContent (juce::Graphics& g, juce::Rectangle<float> screen)
{
    if (activity < 0.01f)
        return;

    const auto radius = screen.getHeight() * 0.34f;
    const auto leftC = juce::Point<float> (screen.getX() + screen.getWidth() * 0.27f, screen.getCentreY() - screen.getHeight() * 0.06f);
    const auto rightC = juce::Point<float> (screen.getX() + screen.getWidth() * 0.73f, leftC.y);
    const auto angle = static_cast<float> (reelAngle);

    // Tape path past the head.
    const auto tapeAlpha = juce::jlimit (0.15f, 1.0f, gain) * (1.0f - 0.7f * dropout);
    juce::Path tape;
    tape.startNewSubPath (leftC.x - radius * 0.2f, leftC.y + radius * 0.98f);
    tape.lineTo (screen.getCentreX() - radius * 0.6f, screen.getBottom() - 6.0f);
    tape.lineTo (screen.getCentreX() + radius * 0.6f, screen.getBottom() - 6.0f);
    tape.lineTo (rightC.x + radius * 0.2f, rightC.y + radius * 0.98f);
    strokeGlow (g, tape, phosphor, activity * tapeAlpha, 1.0f);

    // Head.
    g.setColour (phosphor.withAlpha (0.6f * activity));
    g.fillRoundedRectangle (juce::Rectangle<float> (radius * 0.55f, 4.0f).withCentre ({ screen.getCentreX(), screen.getBottom() - 3.0f }), 1.5f);

    drawReel (g, leftC, radius, radius * 0.92f, angle);
    drawReel (g, rightC, radius, radius * 0.6f, angle * 1.35f);

    // Dropout indicator.
    if (dropout > 0.02f)
    {
        g.setColour (Colours::ledRed.withAlpha (juce::jlimit (0.0f, 1.0f, dropout * 3.0f) * activity));
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ screen.getRight() - 8.0f, screen.getY() + 8.0f }));
    }
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
        return juce::degreesToRadians (-40.0f + 80.0f * pos);
    };

    const auto faceArea = area.reduced (3.0f);
    const auto pivot = juce::Point<float> (faceArea.getCentreX(), faceArea.getBottom() + faceArea.getHeight() * 0.5f);
    const auto arcRadius = faceArea.getHeight() * 1.12f;

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
                const auto lp = juce::Point<float> (pivot.x + std::sin (a) * (arcRadius + faceArea.getHeight() * 0.19f), pivot.y - std::cos (a) * (arcRadius + faceArea.getHeight() * 0.19f));
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
