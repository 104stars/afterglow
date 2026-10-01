#include "Visualisers.h"
#include "../Parameters.h"
#include "../dsp/DistortModule.h"
#include "../dsp/MagneticModule.h"
#include "../dsp/SpaceModule.h"

namespace afterglow::ui
{
namespace
{
    float approach (float current, float target, float seconds, float timeConstant)
    {
        return target + (current - target) * std::exp (-seconds / std::max (0.001f, timeConstant));
    }

    float smoothstep (float edge0, float edge1, float x)
    {
        const auto t = juce::jlimit (0.0f, 1.0f, (x - edge0) / (edge1 - edge0));
        return t * t * (3.0f - 2.0f * t);
    }

    /** How many of the newest entries of a telemetry ring to read (never more than half the ring, so the
        writer cannot overwrite them while they are read), and the running index of the oldest of them. */
    template <typename Ring>
    int newestEntries (const Ring& ring, int maxCount, uint32_t& first) noexcept
    {
        const auto written = ring.written.load (std::memory_order_acquire);
        const auto limit = static_cast<uint32_t> (std::min (maxCount, Ring::size / 2));
        const auto count = std::min (written, limit);
        first = written - count;
        return static_cast<int> (count);
    }

    /** A closed band between an upper and a lower edge (both left to right). */
    juce::Path bandPath (const std::vector<juce::Point<float>>& upper, const std::vector<juce::Point<float>>& lower)
    {
        juce::Path band;
        if (upper.size() < 2 || upper.size() != lower.size())
            return band;

        band.startNewSubPath (upper.front());
        for (size_t i = 1; i < upper.size(); ++i)
            band.lineTo (upper[i]);
        for (size_t i = lower.size(); i-- > 0;)
            band.lineTo (lower[i]);
        band.closeSubPath();
        return band;
    }

    float gainToDb (float gain) { return juce::Decibels::gainToDecibels (gain, -120.0f); }
} // namespace

//======================================================================================================================
void ModuleDisplay::Canvas::hLine (float x0, float x1, float y, juce::Colour colour) const
{
    g.setColour (colour);
    g.fillRect (juce::Rectangle<float> (x0 * scale, std::floor (y * scale), (x1 - x0) * scale, 1.0f));
}

void ModuleDisplay::Canvas::vLine (float x, float y0, float y1, juce::Colour colour) const
{
    g.setColour (colour);
    g.fillRect (juce::Rectangle<float> (std::floor (x * scale), y0 * scale, 1.0f, (y1 - y0) * scale));
}

void ModuleDisplay::Canvas::stroke (const juce::Path& path, float width, juce::Colour colour) const
{
    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (width * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::scale (scale));
}

void ModuleDisplay::Canvas::dashed (const juce::Path& path, float width, float dash, juce::Colour colour) const
{
    juce::Path dashes;
    const float pattern[] { dash * scale, dash * scale };
    juce::PathStrokeType (width * scale).createDashedStroke (dashes, path, pattern, 2, juce::AffineTransform::scale (scale));
    g.setColour (colour);
    g.fillPath (dashes);
}

void ModuleDisplay::Canvas::fill (const juce::Path& path, juce::Colour colour) const
{
    g.setColour (colour);
    g.fillPath (path, juce::AffineTransform::scale (scale));
}

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

void ModuleDisplay::watchData (uint32_t written) noexcept
{
    if (written != lastWritten)
    {
        lastWritten = written;
        staleSeconds = 0.0;
    }
}

float ModuleDisplay::liveIntensity() const noexcept
{
    // When the host stops calling the audio engine, the last picture fades instead of freezing.
    const auto fresh = staleSeconds < 0.1 ? 1.0f : static_cast<float> (std::exp (-(staleSeconds - 0.1) / 0.25));
    return activity * fresh;
}

void ModuleDisplay::tick (double seconds)
{
    staleSeconds += seconds;
    activity = approach (activity, isActive() ? 1.0f : 0.0f, static_cast<float> (seconds), 0.12f);
    repaint();
}

void ModuleDisplay::resized()
{
    housingScale = 0.0f;
    staticValid = false;
}

void ModuleDisplay::rebuildHousing (float scale)
{
    housingScale = scale;
    const auto area = getLocalBounds().toFloat();
    const auto screen = area.reduced (4.0f);
    const auto w = std::max (1, juce::roundToInt (area.getWidth() * scale));
    const auto h = std::max (1, juce::roundToInt (area.getHeight() * scale));

    // Under the content: the bezel and the unlit filter glass, tinted by the module's own filter colour.
    housingUnder = juce::Image (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g (housingUnder);
        g.addTransform (juce::AffineTransform::scale (scale));

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (area.withTrimmedBottom (1.5f).translated (0.0f, 1.5f), 6.0f);
        juce::ColourGradient bezel (juce::Colour (0xff1d1e21), area.getX(), area.getY(), juce::Colour (0xff0c0c0d), area.getX(), area.getBottom(), false);
        g.setGradientFill (bezel);
        g.fillRoundedRectangle (area.withTrimmedBottom (1.0f), 6.0f);

        // Light catching the lower lip of the bezel.
        g.setColour (juce::Colours::white.withAlpha (0.09f));
        g.fillRect (juce::Rectangle<float> (area.getX() + 6.0f, area.getBottom() - 1.0f - 1.0f / scale, area.getWidth() - 12.0f, 1.0f / scale));

        g.setColour (juce::Colour (0xff0b0b0a).interpolatedWith (phosphor, 0.055f));
        g.fillRoundedRectangle (screen, 3.0f);
    }

    // Over the content: the glass itself, a short shadow under the top edge of the bezel and one soft sheen.
    housingOver = juce::Image (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g (housingOver);
        g.addTransform (juce::AffineTransform::scale (scale));
        juce::Path glass;
        glass.addRoundedRectangle (screen, 3.0f);
        g.reduceClipRegion (glass);

        juce::ColourGradient shadow (juce::Colours::black.withAlpha (0.55f), 0.0f, screen.getY(), juce::Colours::transparentBlack, 0.0f, screen.getY() + 4.0f, false);
        shadow.addColour (0.35, juce::Colours::black.withAlpha (0.2f));
        g.setGradientFill (shadow);
        g.fillRect (screen.withHeight (4.0f));

        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.045f), 0.0f, screen.getY(), juce::Colours::white.withAlpha (0.0f), 0.0f, screen.getY() + screen.getHeight() * 0.45f, false);
        g.setGradientFill (sheen);
        g.fillRect (screen);
    }
}

void ModuleDisplay::paint (juce::Graphics& g)
{
    const auto scale = juce::jlimit (0.25f, 8.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto area = getLocalBounds().toFloat();
    const auto screen = area.reduced (4.0f);

    if (! housingUnder.isValid() || ! juce::approximatelyEqual (scale, housingScale))
        rebuildHousing (scale);

    g.drawImage (housingUnder, area, juce::RectanglePlacement::stretchToFit);

    const auto key = staticKey();
    if (! staticValid || key != cachedStaticKey || ! juce::approximatelyEqual (scale, staticScale))
    {
        staticLayer = juce::Image (juce::Image::ARGB, std::max (1, juce::roundToInt (screen.getWidth() * scale)),
                                   std::max (1, juce::roundToInt (screen.getHeight() * scale)), true);
        juce::Graphics sg (staticLayer);
        drawStatic (Canvas { sg, scale });
        cachedStaticKey = key;
        staticScale = scale;
        staticValid = true;
    }

    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path glass;
        glass.addRoundedRectangle (screen, 3.0f);
        g.reduceClipRegion (glass);

        g.setOpacity (0.3f + 0.7f * activity);
        g.drawImage (staticLayer, screen, juce::RectanglePlacement::stretchToFit);
        g.setOpacity (1.0f);

        g.addTransform (juce::AffineTransform::translation (screen.getX(), screen.getY()));
        drawLive (g);

        // The one readout slot: top-right, set on the emitter itself. Hidden where it would be too small to read.
        const auto text = readoutText();
        if (text.isNotEmpty() && scale >= 0.75f && activity > 0.02f)
        {
            g.setFont (Fonts::get().display (10.5f));
            g.setColour (phosphor.withAlpha (0.78f * activity));
            g.drawText (text, juce::Rectangle<float> (4.0f, 1.5f, screenWidth - 8.0f, 11.0f), juce::Justification::centredRight, false);
        }
    }

    g.drawImage (housingOver, area, juce::RectanglePlacement::stretchToFit);
}

void ModuleDisplay::setAgedFill (juce::Graphics& g, juce::Colour colour, float alpha, float xOld, float xNew) const
{
    if (xNew - xOld < 1.0f)
    {
        g.setColour (colour.withAlpha (juce::jlimit (0.0f, 1.0f, alpha)));
        return;
    }

    // Afterglow: the oldest data has faded to 40 % of the newest.
    g.setGradientFill (juce::ColourGradient (colour.withAlpha (juce::jlimit (0.0f, 1.0f, alpha * 0.4f)), xOld, 0.0f,
                                             colour.withAlpha (juce::jlimit (0.0f, 1.0f, alpha)), xNew, 0.0f, false));
}

void ModuleDisplay::drawBeam (juce::Graphics& g, const juce::Point<float>* points, int count, juce::Colour colour, float intensity, const Beam& beam) const
{
    if (count < 2 || intensity < 0.004f)
        return;

    // Brightness follows dwell: a segment drawn slowly (short) is bright, a fast edge (long) is dim, like a real
    // beam. Four brightness steps keep it to a handful of paths per frame.
    static constexpr float levels[] { 0.22f, 0.45f, 0.75f, 1.0f };
    juce::Path paths[4], whole;
    auto current = -1;
    whole.startNewSubPath (points[0]);

    for (int i = 1; i < count; ++i)
    {
        const auto length = points[i].getDistanceFrom (points[i - 1]);
        const auto dwell = juce::jlimit (beam.floor, 1.0f, beam.reference / std::max (0.001f, length));
        const auto bucket = dwell < 0.33f ? 0 : (dwell < 0.6f ? 1 : (dwell < 0.88f ? 2 : 3));
        if (bucket != current)
        {
            paths[bucket].startNewSubPath (points[i - 1]);
            current = bucket;
        }
        paths[bucket].lineTo (points[i]);
        whole.lineTo (points[i]);
    }

    if (beam.halo)
    {
        setAgedFill (g, colour, 0.10f * intensity, beam.xOld, beam.xNew);
        g.strokePath (whole, juce::PathStrokeType (beam.width * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    for (int b = 0; b < 4; ++b)
    {
        if (paths[b].isEmpty())
            continue;
        setAgedFill (g, colour, levels[b] * intensity, beam.xOld, beam.xNew);
        g.strokePath (paths[b], juce::PathStrokeType (beam.width, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }
}

void ModuleDisplay::drawEdgePen (juce::Graphics& g, float y, float alpha) const
{
    // A small solid triangle at the right edge, pointing at the newest value, like a chart recorder's pen.
    juce::Path pen;
    pen.addTriangle (screenWidth, y - 2.3f, screenWidth - 3.2f, y, screenWidth, y + 2.3f);
    g.setColour (phosphor.withAlpha (juce::jlimit (0.0f, 1.0f, alpha)));
    g.fillPath (pen);
}

//======================================================================================================================
namespace
{
    // NOISE geometry: 120 columns (3 s), newest at the right. The envelope (each window's peaks) is drawn on a fixed
    // decibel law from -72 dBFS (the centre line) to -6 dBFS (the edge), so a -50 dBFS bed is visible and louder noise
    // is taller. Inside it, the body (+-1 standard deviation) is drawn in proportion to the peak, so the crest factor
    // shows: hiss is a thin bright core with dim fringes, hum a full band, crackle a thin core under tall spikes.
    constexpr float noiseLeft = 4.0f, noiseRight = 128.0f, noiseCentre = 25.0f, noiseHalf = 21.5f;
    constexpr float noiseFloorDb = -72.0f, noiseFullDb = -6.0f;
    constexpr int noiseColumns = 120;

    float noiseHeight (float magnitude)
    {
        const auto db = juce::Decibels::gainToDecibels (magnitude, noiseFloorDb);
        return noiseHalf * juce::jlimit (0.0f, 1.0f, (db - noiseFloorDb) / (noiseFullDb - noiseFloorDb));
    }
} // namespace

NoiseDisplay::NoiseDisplay (juce::AudioProcessorValueTreeState& s, dsp::EngineTelemetry& t)
    : ModuleDisplay (s, t, 0, ParamIDs::noiseOn) {}

void NoiseDisplay::tick (double seconds)
{
    watchData (telemetry.noiseEnvelope.written.load (std::memory_order_relaxed));
    ModuleDisplay::tick (seconds);
}

void NoiseDisplay::drawStatic (const Canvas& c)
{
    c.hLine (0.0f, screenWidth, noiseCentre, phosphor.withAlpha (0.10f));
}

void NoiseDisplay::drawLive (juce::Graphics& g)
{
    const auto intensity = liveIntensity();
    uint32_t first = 0;
    const auto count = newestEntries (telemetry.noiseEnvelope, noiseColumns, first);
    if (count < 2 || intensity < 0.004f)
        return;

    const auto& ring = telemetry.noiseEnvelope;
    const auto pitch = (noiseRight - noiseLeft) / static_cast<float> (noiseColumns - 1);
    std::vector<juce::Point<float>> outerTop, outerBottom, bodyTop, bodyBottom;

    for (int k = 0; k < count; ++k)
    {
        const auto entry = first + static_cast<uint32_t> (k);
        const auto x = noiseRight - static_cast<float> (count - 1 - k) * pitch;
        const auto lo = ring.get (entry, 0), hi = ring.get (entry, 1), mean = ring.get (entry, 2), rms = ring.get (entry, 3);
        const auto sd = std::sqrt (std::max (0.0f, rms * rms - mean * mean));
        const auto peak = std::max ({ std::abs (lo), std::abs (hi), 1.0e-9f });
        const auto up = noiseHeight (std::max (0.0f, hi));
        const auto down = noiseHeight (std::max (0.0f, -lo));
        const auto body = noiseHeight (peak) * std::min (1.0f, sd / peak);

        outerTop.push_back ({ x, noiseCentre - up });
        outerBottom.push_back ({ x, noiseCentre + down });
        bodyTop.push_back ({ x, noiseCentre - body });
        bodyBottom.push_back ({ x, noiseCentre + body });
    }

    // A slow-sweep scope draws noise as a dense, bright body with dim, spiky fringes.
    setAgedFill (g, phosphor, 0.24f * intensity, noiseLeft, noiseRight);
    g.fillPath (bandPath (outerTop, outerBottom));
    setAgedFill (g, phosphor, 0.50f * intensity, noiseLeft, noiseRight);
    g.fillPath (bandPath (bodyTop, bodyBottom));
}

//======================================================================================================================
namespace
{
    // WOBBLE geometry: 4 s of pitch deviation, newest at the right. The cents scale is fixed and linear for small
    // deviations, then gently compresses (20 tanh(c / 24) px), so a subtle 5 cent wow and a 50 cent warp both read.
    constexpr float wobbleLeft = 4.0f, wobbleRight = 126.0f, wobbleCentre = 25.0f;
    constexpr int wobbleColumns = 128;

    float wobbleY (float cents) { return wobbleCentre - 20.0f * std::tanh (cents / 24.0f); }
} // namespace

WobbleDisplay::WobbleDisplay (juce::AudioProcessorValueTreeState& s, dsp::EngineTelemetry& t)
    : ModuleDisplay (s, t, 1, ParamIDs::wobbleOn) {}

void WobbleDisplay::tick (double seconds)
{
    watchData (telemetry.wobblePitch.written.load (std::memory_order_relaxed));
    ModuleDisplay::tick (seconds);
}

void WobbleDisplay::drawStatic (const Canvas& c)
{
    c.hLine (0.0f, screenWidth, wobbleCentre, phosphor.withAlpha (0.12f));

    // Scale: +-10 cents (short) and +-50 cents (a quarter-tone, long) at both edges.
    for (auto cents : { 10.0f, -10.0f, 50.0f, -50.0f })
    {
        const auto major = std::abs (cents) > 20.0f;
        const auto len = major ? 3.0f : 2.0f;
        const auto colour = phosphor.withAlpha (major ? 0.40f : 0.28f);
        c.hLine (0.0f, len, wobbleY (cents), colour);
        c.hLine (screenWidth - len, screenWidth, wobbleY (cents), colour);
    }
}

void WobbleDisplay::drawLive (juce::Graphics& g)
{
    const auto mix = param (ParamIDs::wobbleMix) * 0.01f;
    const auto intensity = liveIntensity() * (0.45f + 0.55f * mix);
    uint32_t first = 0;
    const auto count = newestEntries (telemetry.wobblePitch, wobbleColumns * 2, first);
    const auto columns = count / 2;
    if (columns < 2 || intensity < 0.004f)
        return;

    const auto& ring = telemetry.wobblePitch;
    const auto stereo = param (ParamIDs::wobbleStereo) > 0.5f;
    const auto pitch = (wobbleRight - wobbleLeft) / static_cast<float> (wobbleColumns - 1);
    const auto start = first + static_cast<uint32_t> (count - columns * 2);

    struct Lane { std::vector<juce::Point<float>> line, upper, lower; };
    Lane lanes[2];

    for (int j = 0; j < columns; ++j)
    {
        const auto a = start + static_cast<uint32_t> (2 * j);
        const auto x = wobbleRight - static_cast<float> (columns - 1 - j) * pitch;
        for (int c = 0; c < 2; ++c)
        {
            const auto base = 3 * c;
            const auto mean = 0.5f * (ring.get (a, base) + ring.get (a + 1, base));
            const auto lo = std::min (ring.get (a, base + 1), ring.get (a + 1, base + 1));
            const auto hi = std::max (ring.get (a, base + 2), ring.get (a + 1, base + 2));
            lanes[c].line.push_back ({ x, wobbleY (mean) });
            lanes[c].upper.push_back ({ x, wobbleY (hi) });
            lanes[c].lower.push_back ({ x, wobbleY (lo) });
        }
    }

    Beam beam;
    beam.xOld = wobbleLeft;
    beam.xNew = wobbleRight;

    if (stereo)
    {
        // The right channel sits underneath: dimmer, thinner, no halo.
        setAgedFill (g, phosphor, 0.12f * intensity, wobbleLeft, wobbleRight);
        g.fillPath (bandPath (lanes[1].upper, lanes[1].lower));
        Beam right = beam;
        right.width = 1.2f;
        right.halo = false;
        right.reference = 6.0f;
        right.floor = 0.5f;
        drawBeam (g, lanes[1].line.data(), static_cast<int> (lanes[1].line.size()), phosphor, 0.55f * intensity, right);
    }

    // Flutter is the band around the line; it is drawn as an envelope, never as aliased per-frame wiggles.
    setAgedFill (g, phosphor, 0.20f * intensity, wobbleLeft, wobbleRight);
    g.fillPath (bandPath (lanes[0].upper, lanes[0].lower));
    beam.reference = 6.0f;
    beam.floor = 0.5f;
    drawBeam (g, lanes[0].line.data(), static_cast<int> (lanes[0].line.size()), phosphor, intensity, beam);

    drawEdgePen (g, lanes[0].line.back().y, 0.9f * intensity);
    if (stereo)
        drawEdgePen (g, lanes[1].line.back().y, 0.5f * intensity);
}

//======================================================================================================================
namespace
{
    // DISTORT geometry: origin on the shared baseline. The input axis spans each type's own working range (the
    // shaper's input after drive), the output axis +-1.5.
    constexpr float distortOriginX = 66.0f, distortOriginY = 25.0f, distortHalfX = 61.0f, distortHalfY = 21.0f;
    constexpr float distortRange[] { 3.0f, 4.0f, 3.0f, 3.0f, 2.5f, 2.5f, 5.0f, 3.0f };
    constexpr int foldType = 6;

    juce::Point<float> distortPoint (float x, float y, float range)
    {
        return { distortOriginX + distortHalfX * x / range, distortOriginY - distortHalfY * juce::jlimit (-1.7f, 1.7f, y) / 1.5f };
    }
} // namespace

DistortDisplay::DistortDisplay (juce::AudioProcessorValueTreeState& s, dsp::EngineTelemetry& t)
    : ModuleDisplay (s, t, 2, ParamIDs::distortOn) {}

int DistortDisplay::currentType() const { return juce::jlimit (0, 7, juce::roundToInt (param (ParamIDs::distortType))); }

void DistortDisplay::tick (double seconds)
{
    watchData (telemetry.distortTransfer.written.load (std::memory_order_relaxed));
    bias = telemetry.distortBias.load (std::memory_order_relaxed);
    ModuleDisplay::tick (seconds);
}

juce::String DistortDisplay::staticKey() const
{
    return juce::String (currentType()) + ":" + juce::String (juce::roundToInt (bias * 100.0f));
}

void DistortDisplay::drawStatic (const Canvas& c)
{
    const auto type = currentType();
    const auto range = distortRange[type];

    c.hLine (5.0f, 127.0f, distortOriginY, phosphor.withAlpha (0.09f));
    c.vLine (distortOriginX, 4.0f, 46.0f, phosphor.withAlpha (0.09f));

    // The clean wire (output = input), dashed, for comparison.
    juce::Path wire;
    wire.startNewSubPath (distortPoint (-1.5f, -1.5f, range));
    wire.lineTo (distortPoint (1.5f, 1.5f, range));
    c.dashed (wire, 1.0f, 2.0f, phosphor.withAlpha (0.14f));

    // The type's transfer curve, at the bias it is running with.
    juce::Path curve;
    constexpr int points = 160;
    for (int i = 0; i <= points; ++i)
    {
        const auto x = range * (2.0f * static_cast<float> (i) / points - 1.0f);
        const auto p = distortPoint (x, dsp::DistortModule::staticCurve (type, x, bias), range);
        if (i == 0)
            curve.startNewSubPath (p);
        else
            curve.lineTo (p);
    }
    c.stroke (curve, 1.2f, phosphor.withAlpha (0.32f));
}

void DistortDisplay::drawLive (juce::Graphics& g)
{
    const auto intensity = liveIntensity();
    uint32_t first = 0;
    const auto count = newestEntries (telemetry.distortTransfer, 512, first);
    if (count < 2 || intensity < 0.004f)
        return;

    const auto type = currentType();
    const auto range = distortRange[type];
    const auto& ring = telemetry.distortTransfer;
    std::vector<juce::Point<float>> trace;
    trace.reserve (static_cast<size_t> (count));
    auto peak = 0.0f;

    for (int k = 0; k < count; ++k)
    {
        const auto entry = first + static_cast<uint32_t> (k);
        auto x = ring.get (entry, 0);
        peak = std::max (peak, std::abs (x));
        // Monotonic curves pin at the end of the axis (the output is on its rail there anyway); Fold keeps folding.
        if (type != foldType)
            x = juce::jlimit (-1.04f * range, 1.04f * range, x);
        trace.push_back (distortPoint (x, ring.get (entry, 1), range));
    }

    // Silence shows the bare curve rather than a hot dot parked at the origin.
    Beam beam;
    beam.width = 1.5f;
    beam.reference = 0.9f;
    beam.floor = 0.15f;
    drawBeam (g, trace.data(), count, phosphor, intensity * smoothstep (0.02f, 0.08f, peak), beam);
}

//======================================================================================================================
namespace
{
    // DIGITAL geometry: a test chirp from 20 Hz to 20 kHz on a log axis, 12 visible cycles, amplitude tapering to
    // the right (as on a sweep generator's output), below the readout slot.
    constexpr float sweepLeft = 5.0f, sweepWidth = 122.0f, sweepCentre = 28.0f;
    constexpr int sweepPoints = 1000;

    struct ChirpTable
    {
        std::array<float, sweepPoints> u {}, hz {}, phase {}, seconds {};

        ChirpTable()
        {
            // Phase advances with f^0.55 per unit of width, so the chirp reads as one smooth sweep; the true time
            // between points follows from the phase step at that frequency.
            std::array<float, sweepPoints> step {};
            auto total = 0.0f;
            for (int i = 0; i < sweepPoints; ++i)
            {
                u[static_cast<size_t> (i)] = static_cast<float> (i) / static_cast<float> (sweepPoints - 1);
                hz[static_cast<size_t> (i)] = 20.0f * std::pow (1000.0f, u[static_cast<size_t> (i)]);
                step[static_cast<size_t> (i)] = i == 0 ? 0.0f : std::pow (hz[static_cast<size_t> (i)], 0.55f);
                total += step[static_cast<size_t> (i)];
            }

            const auto scale = 12.0f * juce::MathConstants<float>::twoPi / total;
            for (size_t i = 1; i < static_cast<size_t> (sweepPoints); ++i)
            {
                const auto dPhase = step[i] * scale;
                phase[i] = phase[i - 1] + dPhase;
                seconds[i] = seconds[i - 1] + dPhase / (juce::MathConstants<float>::twoPi * 0.5f * (hz[i] + hz[i - 1]));
            }
        }

        /** Phase and frequency of the chirp at a given true time. */
        void at (float t, float& outPhase, float& outHz) const
        {
            const auto it = std::upper_bound (seconds.begin(), seconds.end(), t);
            const auto i = static_cast<size_t> (juce::jlimit<long> (1, sweepPoints - 1, static_cast<long> (std::distance (seconds.begin(), it))));
            const auto span = seconds[i] - seconds[i - 1];
            const auto f = span > 0.0f ? juce::jlimit (0.0f, 1.0f, (t - seconds[i - 1]) / span) : 0.0f;
            outPhase = phase[i - 1] + f * (phase[i] - phase[i - 1]);
            outHz = hz[i - 1] + f * (hz[i] - hz[i - 1]);
        }
    };

    const ChirpTable& chirp()
    {
        static const ChirpTable table;
        return table;
    }

    float sweepAmplitude (float u) { return 17.5f * (1.0f - 0.55f * u); }
    float sweepX (float hz) { return sweepLeft + sweepWidth * std::log (hz / 20.0f) / std::log (1000.0f); }

    float quantiseLevel (float v, float levels) { return std::round (v * levels) / levels; }
} // namespace

DigitalDisplay::DigitalDisplay (juce::AudioProcessorValueTreeState& s, dsp::EngineTelemetry& t)
    : ModuleDisplay (s, t, 3, ParamIDs::digitalOn) {}

void DigitalDisplay::tick (double seconds)
{
    rate = approach (rate, telemetry.digitalRate.load (std::memory_order_relaxed), static_cast<float> (seconds), 0.08f);
    bits = approach (bits, telemetry.digitalBits.load (std::memory_order_relaxed), static_cast<float> (seconds), 0.08f);
    jitter = telemetry.digitalJitter.load (std::memory_order_relaxed);
    rebuildSweep();
    ModuleDisplay::tick (seconds);
}

juce::String DigitalDisplay::readoutText() const
{
    const auto rateText = rate >= 999.5f ? juce::String (rate / 1000.0f, 1) + "k" : juce::String (juce::roundToInt (rate));
    return bits < 15.5f ? rateText + "  " + juce::String (juce::jlimit (1, 16, juce::roundToInt (bits))) + "b" : rateText;
}

void DigitalDisplay::drawStatic (const Canvas& c)
{
    // The clean chirp, faint: the reference the processed sweep departs from.
    const auto& table = chirp();
    juce::Path clean;
    for (size_t i = 0; i < static_cast<size_t> (sweepPoints); i += 2)
    {
        const juce::Point<float> p (sweepLeft + sweepWidth * table.u[i], sweepCentre - std::sin (table.phase[i]) * sweepAmplitude (table.u[i]));
        if (i == 0)
            clean.startNewSubPath (p);
        else
            clean.lineTo (p);
    }
    c.stroke (clean, 1.1f, phosphor.withAlpha (0.16f));
}

void DigitalDisplay::rebuildSweep()
{
    const auto hostRate = telemetry.sampleRate.load (std::memory_order_relaxed);
    const auto smooth = param (ParamIDs::digitalSmooth) * 0.01f;
    const auto focusLow = param (ParamIDs::digitalFocusLow);
    const auto focusHigh = param (ParamIDs::digitalFocusHigh);
    const auto cut = param (ParamIDs::digitalCut) > 0.5f;
    const auto compand = param (ParamIDs::digitalCompand) > 0.5f;
    const auto mix = param (ParamIDs::digitalMix) * 0.01f;

    const auto key = juce::String (juce::roundToInt (rate)) + "/" + juce::String (juce::roundToInt (bits * 20.0f)) + "/" + juce::String (juce::roundToInt (jitter * 100.0f))
                   + "/" + juce::String (juce::roundToInt (smooth * 100.0f)) + "/" + juce::String (juce::roundToInt (focusLow)) + "/" + juce::String (juce::roundToInt (focusHigh))
                   + "/" + juce::String (static_cast<int> (cut)) + juce::String (static_cast<int> (compand)) + "/" + juce::String (juce::roundToInt (mix * 100.0f));
    if (key == sweepKey && ! sweep.empty())
        return;
    sweepKey = key;

    const auto& table = chirp();
    const auto rateReduced = rate < hostRate * 0.985f;
    const auto bitsReduced = bits < 15.5f;
    const auto levels = std::pow (2.0f, bits - 1.0f);
    const auto antiAliasHz = std::exp (juce::jmap (smooth, std::log (0.45f * hostRate), std::log (std::min (0.45f * hostRate, 0.46f * rate))));

    // One sample of the module's converter, as in DigitalModule: anti-alias filter (Smooth), hold, quantise.
    auto converted = [&] (long k)
    {
        // Clock jitter moves the sampling instants by a fixed pseudo-random amount per sample.
        const auto wobble = jitter * 0.5f * (std::fmod (std::sin (static_cast<float> (k) * 12.9898f) * 43758.547f, 1.0f));
        float ph = 0.0f, hz = 20.0f;
        table.at ((static_cast<float> (k) + wobble) / rate, ph, hz);
        const auto antiAlias = 1.0f / std::sqrt (1.0f + std::pow (hz / antiAliasHz, 4.0f));
        auto v = 0.5f * antiAlias * std::sin (ph);

        if (bitsReduced)
        {
            const auto linear = quantiseLevel (v, levels);
            if (compand)
            {
                constexpr float mu = 255.0f;
                const auto encoded = std::copysign (std::log1p (mu * std::abs (v)) / std::log1p (mu), v);
                const auto q = quantiseLevel (encoded, levels);
                v = std::copysign ((std::exp (std::abs (q) * std::log1p (mu)) - 1.0f) / mu, q);
            }
            else
            {
                v = linear;
            }
        }
        return v / 0.5f;
    };

    sweep.clear();
    sweep.reserve (static_cast<size_t> (sweepPoints));

    for (size_t i = 0; i < static_cast<size_t> (sweepPoints); ++i)
    {
        const auto clean = std::sin (table.phase[i]);
        auto processed = clean;
        const auto inBand = table.hz[i] >= focusLow && table.hz[i] <= focusHigh;

        if (! inBand)
        {
            processed = cut ? 0.0f : clean;
        }
        else if (rateReduced || bitsReduced)
        {
            if (rateReduced)
            {
                const auto position = table.seconds[i] * rate;
                const auto k = static_cast<long> (std::floor (position));
                processed = converted (k);
                if (smooth > 0.0f) // reconstruction filter, shown as a blend towards the joined samples
                    processed = juce::jmap (smooth, processed, juce::jmap (position - static_cast<float> (k), processed, converted (k + 1)));
            }
            else
            {
                processed = converted (static_cast<long> (std::floor (table.seconds[i] * hostRate)));
                processed = bitsReduced ? processed : clean;
            }
        }

        const auto value = clean + mix * (processed - clean);
        sweep.push_back ({ sweepLeft + sweepWidth * table.u[i], sweepCentre - value * sweepAmplitude (table.u[i]) });
    }

    nyquistX = rateReduced && rate * 0.5f < 20000.0f ? sweepX (rate * 0.5f) : -1.0f;
}

void DigitalDisplay::drawLive (juce::Graphics& g)
{
    const auto intensity = activity;
    if (sweep.empty() || intensity < 0.004f)
        return;

    Beam beam;
    beam.reference = 2.2f;
    beam.floor = 0.35f;
    drawBeam (g, sweep.data(), static_cast<int> (sweep.size()), phosphor, intensity, beam);

    // Nyquist: where the sweep passes half the sample rate and aliasing begins.
    if (nyquistX > 0.0f)
    {
        juce::Path mark;
        mark.addTriangle (nyquistX - 2.3f, screenHeight, nyquistX, screenHeight - 3.2f, nyquistX + 2.3f, screenHeight);
        g.setColour (phosphor.withAlpha (0.8f * intensity));
        g.fillPath (mark);
    }

    // Focus band edges, when the band does not cover the whole sweep.
    g.setColour (phosphor.withAlpha (0.45f * intensity));
    for (auto hz : { param (ParamIDs::digitalFocusLow), param (ParamIDs::digitalFocusHigh) })
    {
        const auto u = std::log (std::max (1.0f, hz) / 20.0f) / std::log (1000.0f);
        if (u > 0.01f && u < 0.99f)
            g.fillRect (juce::Rectangle<float> (sweepLeft + sweepWidth * u - 0.5f, screenHeight - 4.0f, 1.0f, 4.0f));
    }
}

//======================================================================================================================
namespace
{
    // SPACE geometry: a linear time axis (1.5 s across) and a dB axis from 0 dB (just under the readout slot)
    // down to -60 dB on the floor line.
    constexpr float spaceLeft = 5.0f, spaceRight = 128.0f, spaceSeconds = 1.5f;
    constexpr float spaceTop = 11.5f, spaceFloor = 44.5f;
    constexpr int resonatorType = 5;

    float spaceX (float seconds) { return spaceLeft + (spaceRight - spaceLeft) * seconds / spaceSeconds; }
    float spaceY (float db) { return spaceTop + (spaceFloor - spaceTop) * juce::jlimit (0.0f, 60.0f, -db) / 60.0f; }

    // Resonator: twelve chromatic cells from C, with a keyboard strip under them.
    constexpr float cellLeft = 5.0f, cellPitch = 122.0f / 12.0f, cellTop = 13.0f, cellFloor = 40.5f;
    bool isBlackKey (int note) { return note == 1 || note == 3 || note == 6 || note == 8 || note == 10; }
} // namespace

SpaceDisplay::SpaceDisplay (juce::AudioProcessorValueTreeState& s, dsp::EngineTelemetry& t)
    : ModuleDisplay (s, t, 4, ParamIDs::spaceOn) {}

bool SpaceDisplay::isResonator() const { return juce::roundToInt (param (ParamIDs::spaceType)) == resonatorType; }

float SpaceDisplay::decayAt (float hz) const
{
    return dsp::SpaceModule::decaySecondsAt (juce::jlimit (0, 5, juce::roundToInt (param (ParamIDs::spaceType))), decaySeconds,
                                             param (ParamIDs::spaceFocusLow), param (ParamIDs::spaceFocusHigh), hz);
}

void SpaceDisplay::tick (double seconds)
{
    watchData (telemetry.spaceWet.written.load (std::memory_order_relaxed));
    decaySeconds = telemetry.spaceDecaySeconds.load (std::memory_order_relaxed);
    preDelayMs = telemetry.spacePreDelayMs.load (std::memory_order_relaxed);

    for (size_t k = 0; k < 12; ++k)
    {
        // The combs ring down by themselves; only a short release keeps the cells from flickering frame to frame.
        const auto db = gainToDb (telemetry.spaceNotes[k].load (std::memory_order_relaxed));
        noteDb[k] = db > noteDb[k] ? db : approach (noteDb[k], db, static_cast<float> (seconds), 0.05f);
    }

    ModuleDisplay::tick (seconds);
}

juce::String SpaceDisplay::staticKey() const { return isResonator() ? "resonator" : "decay"; }

juce::String SpaceDisplay::readoutText() const
{
    const auto rt = decayAt (1000.0f);
    return juce::String (rt, rt < 10.0f ? 1 : 0) + " s";
}

void SpaceDisplay::drawStatic (const Canvas& c)
{
    if (isResonator())
    {
        for (int k = 0; k < 12; ++k)
        {
            const auto x = cellLeft + cellPitch * static_cast<float> (k);
            juce::Path slot;
            slot.addRectangle (x + 1.3f, cellTop, cellPitch - 2.6f, cellFloor - cellTop);
            c.fill (slot, phosphor.withAlpha (0.07f));

            juce::Path key;
            const auto black = isBlackKey (k);
            key.addRectangle (x + 1.3f, 42.5f, cellPitch - 2.6f, black ? 2.0f : 5.0f);
            c.fill (key, phosphor.withAlpha (black ? 0.22f : 0.42f));
        }
        return;
    }

    c.hLine (spaceLeft, spaceRight, spaceFloor, phosphor.withAlpha (0.14f));
    for (int i = 1; i <= 6; ++i)
    {
        const auto t = 0.25f * static_cast<float> (i);
        const auto major = i % 2 == 0;
        c.vLine (spaceX (t), spaceFloor + 1.0f, spaceFloor + (major ? 4.0f : 2.2f), phosphor.withAlpha (major ? 0.40f : 0.25f));
    }
}

void SpaceDisplay::drawLive (juce::Graphics& g)
{
    if (activity < 0.004f)
        return;

    if (isResonator())
    {
        for (size_t k = 0; k < 12; ++k)
        {
            const auto level = juce::jlimit (0.0f, 1.0f, (noteDb[k] + 66.0f) / 60.0f);
            if (level <= 0.0f)
                continue;
            const auto x = cellLeft + cellPitch * static_cast<float> (k);
            const auto top = cellFloor - level * (cellFloor - cellTop);
            g.setColour (phosphor.withAlpha (0.78f * liveIntensity()));
            g.fillRect (juce::Rectangle<float> (x + 1.3f, top, cellPitch - 2.6f, cellFloor - top));
        }
        return;
    }

    // The model: what this type, Decay, Pre-delay, Focus and Amount should do to a single hit.
    const auto type = juce::jlimit (0, 5, juce::roundToInt (param (ParamIDs::spaceType)));
    const auto amount = juce::jlimit (0.0f, 1.0f, param (ParamIDs::spaceAmount) * param (ParamIDs::magnitude) * 1.0e-4f);
    const auto dryDb = std::max (-60.0f, gainToDb (std::min (1.0f, 2.0f * (1.0f - amount))));
    const auto wetDb = std::max (-60.0f, gainToDb (std::pow (std::min (1.0f, 2.0f * amount), 1.2f)));
    float buildMinMs = 0.0f, buildMaxMs = 0.0f;
    dsp::SpaceModule::buildUpMs (type, buildMinMs, buildMaxMs);
    const auto pre = preDelayMs * 0.001f;
    const auto riseStart = pre + buildMinMs * 0.001f;
    const auto peakTime = pre + buildMaxMs * 0.001f;

    auto slope = [&] (float rt)
    {
        juce::Path p;
        p.startNewSubPath (spaceX (peakTime), spaceY (wetDb));
        const auto end = peakTime + rt * (60.0f + wetDb) / 60.0f;
        if (end <= spaceSeconds)
            p.lineTo (spaceX (end), spaceFloor);
        else
            p.lineTo (spaceRight, spaceY (wetDb - 60.0f * (spaceSeconds - peakTime) / std::max (0.01f, rt)));
        return p;
    };

    const auto modelAlpha = activity;
    juce::Path guide;
    guide.startNewSubPath (spaceX (riseStart), spaceFloor);
    guide.lineTo (spaceX (peakTime), spaceY (wetDb));
    guide.addPath (slope (decayAt (1000.0f)));

    juce::Path body (guide);
    body.lineTo (body.getCurrentPosition().x, spaceFloor);
    body.closeSubPath();
    g.setColour (phosphor.withAlpha (0.07f * modelAlpha));
    g.fillPath (body);
    g.setColour (phosphor.withAlpha (0.38f * modelAlpha));
    g.strokePath (guide, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Treble decays faster (the damping); dashed.
    juce::Path treble;
    const float dash[] { 2.0f, 2.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (treble, slope (decayAt (5000.0f)), dash, 2);
    g.setColour (phosphor.withAlpha (0.26f * modelAlpha));
    g.fillPath (treble);

    // The dry hit at time zero.
    if (dryDb > -59.0f)
    {
        g.setColour (phosphor.withAlpha (0.55f * modelAlpha));
        g.drawLine (spaceLeft + 0.6f, spaceFloor, spaceLeft + 0.6f, spaceY (dryDb), 1.4f);
    }

    // The measured wet level since the latest note, written onto the guide (normalised to its own peak).
    const auto& ring = telemetry.spaceWet;
    const auto written = ring.written.load (std::memory_order_acquire);
    const auto onset = telemetry.spaceOnsetEntry.load (std::memory_order_relaxed);
    const auto since = static_cast<int> (written - onset);
    const auto steps = static_cast<int> (spaceSeconds / 0.01f);
    const auto fadeSteps = 30;
    if (since < 2 || since > steps + fadeSteps || since > ring.size / 2)
        return;

    const auto count = std::min (since, steps);
    auto peak = -120.0f;
    for (int i = 0; i < count; ++i)
        peak = std::max (peak, ring.get (onset + static_cast<uint32_t> (i), 0));

    std::vector<juce::Point<float>> comet;
    comet.reserve (static_cast<size_t> (count));
    for (int i = 0; i < count; ++i)
        comet.push_back ({ spaceX (0.01f * static_cast<float> (i)), spaceY (ring.get (onset + static_cast<uint32_t> (i), 0) - peak + wetDb) });

    const auto ending = since > steps ? 1.0f - static_cast<float> (since - steps) / static_cast<float> (fadeSteps) : 1.0f;
    const auto intensity = liveIntensity() * ending;
    Beam beam;
    beam.reference = 1.6f;
    beam.floor = 0.25f;
    beam.xNew = comet.back().x;
    beam.xOld = beam.xNew - (spaceRight - spaceLeft) * 0.8f / spaceSeconds;
    drawBeam (g, comet.data(), count, phosphor, intensity, beam);

    g.setColour (phosphor.withAlpha (intensity));
    g.fillEllipse (juce::Rectangle<float> (3.6f, 3.6f).withCentre (comet.back()));
}

//======================================================================================================================
namespace
{
    // MAGNETIC geometry: 2 s of tape, newest at the right, level in dB hanging from a 0 dB ceiling (-24 dB at the
    // bottom of the lane). Stereo splits the screen into two lanes.
    constexpr float tapeLeft = 4.0f, tapeRight = 126.0f;
    constexpr int tapeColumns = 128;

    struct TapeLane { float top, bottom; };
    constexpr TapeLane monoLane { 8.0f, 46.0f };
    constexpr TapeLane stereoLanes[] { { 4.5f, 24.0f }, { 28.0f, 48.5f } };

    float tapeY (const TapeLane& lane, float db) { return lane.top + (lane.bottom - lane.top) * juce::jlimit (0.0f, 24.0f, -db) / 24.0f; }
} // namespace

MagneticDisplay::MagneticDisplay (juce::AudioProcessorValueTreeState& s, dsp::EngineTelemetry& t)
    : ModuleDisplay (s, t, 5, ParamIDs::magneticOn) {}

bool MagneticDisplay::isStereo() const { return param (ParamIDs::magneticStereo) > 0.5f; }

void MagneticDisplay::tick (double seconds)
{
    watchData (telemetry.magneticTape.written.load (std::memory_order_relaxed));
    ModuleDisplay::tick (seconds);
}

juce::String MagneticDisplay::staticKey() const { return isStereo() ? "stereo" : "mono"; }

void MagneticDisplay::drawStatic (const Canvas& c)
{
    const auto drawLane = [&] (const TapeLane& lane)
    {
        c.hLine (tapeLeft, tapeRight, lane.top, phosphor.withAlpha (0.14f));
        for (auto db : { -12.0f, -24.0f })
            c.hLine (0.0f, 3.0f, tapeY (lane, db), phosphor.withAlpha (0.35f));
    };

    if (isStereo())
        for (const auto& lane : stereoLanes)
            drawLane (lane);
    else
        drawLane (monoLane);
}

void MagneticDisplay::drawLive (juce::Graphics& g)
{
    const auto intensity = liveIntensity();
    uint32_t first = 0;
    const auto count = newestEntries (telemetry.magneticTape, tapeColumns, first);
    if (count < 2 || intensity < 0.004f)
        return;

    const auto& ring = telemetry.magneticTape;
    const auto stereo = isStereo();
    const auto pitch = (tapeRight - tapeLeft) / static_cast<float> (tapeColumns - 1);

    for (int c = 0; c < (stereo ? 2 : 1); ++c)
    {
        const auto& lane = stereo ? stereoLanes[c] : monoLane;
        std::vector<juce::Point<float>> low, high;
        low.reserve (static_cast<size_t> (count));
        high.reserve (static_cast<size_t> (count));

        for (int k = 0; k < count; ++k)
        {
            const auto entry = first + static_cast<uint32_t> (k);
            const auto gainDb = ring.get (entry, 2 * c);
            const auto loss = ring.get (entry, 2 * c + 1);
            const auto x = tapeRight - static_cast<float> (count - 1 - k) * pitch;
            low.push_back ({ x, tapeY (lane, dsp::MagneticModule::responseDb (gainDb, loss, 1000.0f)) });
            high.push_back ({ x, tapeY (lane, dsp::MagneticModule::responseDb (gainDb, loss, 10000.0f)) });
        }

        // The ribbon between the two tones is the treble being lost.
        setAgedFill (g, phosphor, 0.18f * intensity, tapeLeft, tapeRight);
        g.fillPath (bandPath (low, high));

        Beam treble;
        treble.width = 1.1f;
        treble.halo = false;
        treble.floor = 1.0f;
        treble.xOld = tapeLeft;
        treble.xNew = tapeRight;
        drawBeam (g, high.data(), count, phosphor, 0.5f * intensity, treble);

        Beam tone;
        tone.reference = 1.5f;
        tone.floor = 0.35f;
        tone.xOld = tapeLeft;
        tone.xNew = tapeRight;
        drawBeam (g, low.data(), count, phosphor, intensity, tone);
        drawEdgePen (g, low.back().y, 0.85f * intensity);
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
