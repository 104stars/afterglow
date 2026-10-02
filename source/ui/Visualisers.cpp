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

    float gainToDb (float gain) { return juce::Decibels::gainToDecibels (gain, -120.0f); }

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

    juce::Path polyline (const std::vector<juce::Point<float>>& points)
    {
        juce::Path path;
        for (size_t i = 0; i < points.size(); ++i)
        {
            if (i == 0)
                path.startNewSubPath (points[i]);
            else
                path.lineTo (points[i]);
        }
        return path;
    }

    /** Running index range of the entries a recorder shows: those already passed by the scroll head. */
    template <typename Ring>
    int visibleEntries (const Ring& ring, double head, int maxCount, uint32_t& first) noexcept
    {
        const auto written = ring.written.load (std::memory_order_acquire);
        const auto last = std::min (written, static_cast<uint32_t> (std::max (0.0, std::floor (head))));
        const auto count = std::min (last, static_cast<uint32_t> (std::min (maxCount, Ring::size / 2 - 4)));
        first = last - count;
        return static_cast<int> (count);
    }
} // namespace

//======================================================================================================================
void ModuleDisplay::Canvas::hLine (float x0, float x1, float y, juce::Colour colour) const
{
    g.setColour (colour);
    g.fillRect (juce::Rectangle<float> (std::round (x0 * scale), std::floor (y * scale), std::round ((x1 - x0) * scale), 1.0f));
}

void ModuleDisplay::Canvas::vLine (float x, float y0, float y1, juce::Colour colour) const
{
    g.setColour (colour);
    g.fillRect (juce::Rectangle<float> (std::floor (x * scale), std::round (y0 * scale), 1.0f, std::round ((y1 - y0) * scale)));
}

void ModuleDisplay::Canvas::stroke (const juce::Path& path, float width, juce::Colour colour) const
{
    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (std::max (1.0f, width * scale), juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::scale (scale));
}

void ModuleDisplay::Canvas::dashed (const juce::Path& path, float width, float dash, juce::Colour colour) const
{
    juce::Path dashes;
    const auto d = std::max (2.5f, dash * scale);
    const float pattern[] { d, d };
    juce::Path scaled (path);
    scaled.applyTransform (juce::AffineTransform::scale (scale));
    juce::PathStrokeType (std::max (1.0f, width * scale)).createDashedStroke (dashes, scaled, pattern, 2);
    g.setColour (colour);
    g.fillPath (dashes);
}

void ModuleDisplay::Canvas::fill (const juce::Path& path, juce::Colour colour) const
{
    g.setColour (colour);
    g.fillPath (path, juce::AffineTransform::scale (scale));
}

void ModuleDisplay::Canvas::edgeTick (float y, bool major, juce::Colour colour) const
{
    const auto length = std::max (major ? 3.0f * scale : 2.0f * scale, 2.0f);
    g.setColour (colour);
    g.fillRect (juce::Rectangle<float> (0.0f, std::floor (y * scale), std::round (length), 1.0f));
}

void ModuleDisplay::Canvas::bottomTick (float x, bool major, juce::Colour colour) const
{
    const auto length = std::max (major ? 3.0f * scale : 2.0f * scale, 2.0f);
    const auto bottom = std::round (screenHeight * scale);
    g.setColour (colour);
    g.fillRect (juce::Rectangle<float> (std::floor (x * scale), bottom - std::round (length), 1.0f, std::round (length)));
}

//======================================================================================================================
void ModuleDisplay::ScrollHead::advance (uint32_t written, double seconds, double entriesPerSecond) noexcept
{
    // Move at the data rate, but never ahead of the data and never more than two entries behind it.
    const auto w = static_cast<double> (written);
    head = juce::jlimit (w - 2.0, w, head + seconds * entriesPerSecond);
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
    const auto w = std::max (1, static_cast<int> (std::ceil (area.getWidth() * scale)));
    const auto h = std::max (1, static_cast<int> (std::ceil (area.getHeight() * scale)));

    // Under the content: a window cut into the faceplate (lit lower lip, shaded upper wall), like the type
    // selector below it, and the unlit filter glass tinted by the module's own filter colour.
    housingUnder = juce::Image (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g (housingUnder);
        g.addTransform (juce::AffineTransform::scale (scale));
        const auto cut = area.withTrimmedBottom (1.0f);

        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRoundedRectangle (cut.translated (0.0f, 1.0f), 4.0f);
        juce::ColourGradient wall (juce::Colour (0xff0c0c0d), 0.0f, cut.getY(), juce::Colour (0xff1a1b1e), 0.0f, cut.getBottom(), false);
        g.setGradientFill (wall);
        g.fillRoundedRectangle (cut, 4.0f);

        g.setColour (juce::Colour (0xff0b0b0a).interpolatedWith (phosphor, 0.055f));
        g.fillRoundedRectangle (screen, 3.0f);
    }

    // Over the content: a short shadow under the top of the cut-out and one soft sheen on the glass.
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

void ModuleDisplay::drawLayer (juce::Graphics& g, const juce::Image& image, juce::Point<float> origin) const
{
    // Cached layers are rendered at device resolution, so they are drawn one image pixel per device pixel.
    g.drawImageTransformed (image, juce::AffineTransform::scale (1.0f / pixelScale).translated (origin));
}

void ModuleDisplay::paint (juce::Graphics& g)
{
    const auto scale = juce::jlimit (0.25f, 8.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    pixelScale = scale;
    const auto area = getLocalBounds().toFloat();
    const auto screen = area.reduced (4.0f);

    // Align the component to whole device pixels, so the 1-pixel scale marks and the cached layers stay sharp at
    // any interface size.
    if (auto* top = getTopLevelComponent())
    {
        const auto inTop = top->getLocalPoint (this, juce::Point<float>());
        const auto deviceScale = scale / std::max (0.01f, juce::Component::getApproximateScaleFactorForComponent (this));
        const auto device = inTop * deviceScale;
        const auto snapped = juce::Point<float> (std::round (device.x), std::round (device.y));
        g.addTransform (juce::AffineTransform::translation ((snapped - device) / scale));
    }

    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);

    if (! housingUnder.isValid() || ! juce::approximatelyEqual (scale, housingScale))
        rebuildHousing (scale);

    drawLayer (g, housingUnder, area.getTopLeft());

    const auto key = staticKey();
    if (! staticValid || key != cachedStaticKey || ! juce::approximatelyEqual (scale, staticScale))
    {
        staticLayer = juce::Image (juce::Image::ARGB, std::max (1, static_cast<int> (std::ceil (screen.getWidth() * scale))),
                                   std::max (1, static_cast<int> (std::ceil (screen.getHeight() * scale))), true);
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
        drawLayer (g, staticLayer, screen.getTopLeft());
        g.setOpacity (1.0f);
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);

        g.addTransform (juce::AffineTransform::translation (screen.getX(), screen.getY()));
        drawLive (g);

        // The one readout slot: top-right, set on the emitter itself. Below 75 % only its first value is kept and
        // the type is enlarged so it never drops under about 9 device pixels; below 60 % it is hidden.
        auto text = readoutText();
        if (text.isNotEmpty() && scale >= 0.6f && activity > 0.02f)
        {
            if (scale < 0.75f)
                text = text.upToFirstOccurrenceOf (" ", false, false);
            const auto font = Fonts::get().display (std::max (10.5f, 9.0f / scale));
            const auto width = juce::GlyphArrangement::getStringWidth (font, text);
            const auto snap = [scale] (float v) { return std::round (v * scale) / scale; };
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText (font, text, snap (screenWidth - 4.0f - width), snap (1.5f + font.getAscent()));
            g.setColour (phosphor.withAlpha (0.78f * activity));
            glyphs.draw (g);
        }
    }

    drawLayer (g, housingOver, area.getTopLeft());
}

void ModuleDisplay::setAgedFill (juce::Graphics& g, juce::Colour colour, float alpha, const Afterglow& afterglow) const
{
    alpha = juce::jlimit (0.0f, 1.0f, alpha);
    if (afterglow.xFull - afterglow.xOld < 1.0f)
    {
        g.setColour (colour.withAlpha (alpha));
        return;
    }

    g.setGradientFill (juce::ColourGradient (colour.withAlpha (alpha * afterglow.oldRatio), afterglow.xOld, 0.0f,
                                             colour.withAlpha (alpha), afterglow.xFull, 0.0f, false));
}

void ModuleDisplay::drawBeam (juce::Graphics& g, const juce::Point<float>* points, int count, juce::Colour colour, float intensity, const Beam& beam) const
{
    if (count < 2 || intensity < 0.004f)
        return;

    // Brightness follows dwell: a slowly drawn stretch is bright, a fast edge dim, like a real beam. Dwell is taken
    // over three neighbouring segments with a little hysteresis, so brightness changes over runs, not per segment.
    static constexpr float levels[] { 0.22f, 0.45f, 0.75f, 1.0f };
    static constexpr float thresholds[] { 0.33f, 0.6f, 0.88f };
    const auto segments = count - 1;
    std::vector<float> lengths (static_cast<size_t> (segments));
    for (int i = 0; i < segments; ++i)
        lengths[static_cast<size_t> (i)] = points[i + 1].getDistanceFrom (points[i]);

    std::vector<int> buckets (static_cast<size_t> (segments));
    auto current = -1;
    for (int i = 0; i < segments; ++i)
    {
        const auto a = lengths[static_cast<size_t> (std::max (0, i - 1))];
        const auto b = lengths[static_cast<size_t> (i)];
        const auto c = lengths[static_cast<size_t> (std::min (segments - 1, i + 1))];
        const auto dwell = juce::jlimit (beam.floor, 1.0f, beam.reference / std::max (0.001f, (a + b + c) / 3.0f));

        auto bucket = dwell < thresholds[0] ? 0 : (dwell < thresholds[1] ? 1 : (dwell < thresholds[2] ? 2 : 3));
        if (current >= 0 && bucket != current)
        {
            // Only change level when the dwell is clearly past the boundary.
            const auto boundary = thresholds[std::min (bucket, current)];
            if (std::abs (dwell - boundary) < 0.08f)
                bucket = current;
        }
        buckets[static_cast<size_t> (i)] = bucket;
        current = bucket;
    }

    const auto lowest = *std::min_element (buckets.begin(), buckets.end());
    juce::Path whole;
    whole.startNewSubPath (points[0]);
    for (int i = 1; i < count; ++i)
        whole.lineTo (points[i]);

    if (beam.halo)
    {
        // The halo hugs the core: it softens the edge rather than drawing a second outline.
        setAgedFill (g, colour, 0.16f * intensity, beam.afterglow);
        g.strokePath (whole, juce::PathStrokeType (beam.width + std::max (0.6f, 1.4f * pixel()), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // One continuous stroke at the lowest level, then the brighter runs on top: joins can never open.
    const auto width = std::max (beam.width, pixel());
    const auto base = levels[lowest] * intensity;
    setAgedFill (g, colour, base, beam.afterglow);
    g.strokePath (whole, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    for (int level = lowest + 1; level < 4; ++level)
    {
        juce::Path run;
        auto open = false;
        for (int i = 0; i < segments; ++i)
        {
            if (buckets[static_cast<size_t> (i)] == level)
            {
                if (! open)
                    run.startNewSubPath (points[i]);
                run.lineTo (points[i + 1]);
                open = true;
            }
            else
            {
                open = false;
            }
        }

        if (run.isEmpty())
            continue;

        const auto target = levels[level] * intensity;
        setAgedFill (g, colour, (target - base) / std::max (0.01f, 1.0f - base), beam.afterglow);
        g.strokePath (run, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void ModuleDisplay::drawEdgePen (juce::Graphics& g, float y, float alpha) const
{
    // A small solid triangle at the right edge, pointing at the newest value, like a chart recorder's pen.
    const auto half = std::max (2.3f, 2.5f * pixel());
    const auto depth = std::max (3.2f, 3.5f * pixel());
    juce::Path pen;
    pen.addTriangle (screenWidth, y - half, screenWidth - depth, y, screenWidth, y + half);
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
    constexpr double noiseRate = 40.0; // entries per second

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
    const auto written = telemetry.noiseEnvelope.written.load (std::memory_order_relaxed);
    watchData (written);
    scroll.advance (written, seconds, noiseRate);
    ModuleDisplay::tick (seconds);
}

void NoiseDisplay::drawStatic (const Canvas& c)
{
    c.hLine (noiseLeft, noiseRight, noiseCentre, phosphor.withAlpha (0.12f));

    // Level scale: -48 and -24 dBFS, above and below the centre line.
    for (auto db : { -48.0f, -24.0f })
    {
        const auto h = noiseHeight (juce::Decibels::decibelsToGain (db));
        const auto major = db > -30.0f;
        const auto colour = phosphor.withAlpha (major ? 0.40f : 0.28f);
        c.edgeTick (noiseCentre - h, major, colour);
        c.edgeTick (noiseCentre + h, major, colour);
    }
}

void NoiseDisplay::drawLive (juce::Graphics& g)
{
    const auto intensity = liveIntensity();
    uint32_t first = 0;
    const auto count = visibleEntries (telemetry.noiseEnvelope, scroll.head, noiseColumns + 1, first);
    if (count < 2 || intensity < 0.004f)
        return;

    const auto& ring = telemetry.noiseEnvelope;
    const auto pitch = (noiseRight - noiseLeft) / static_cast<float> (noiseColumns - 1);
    std::vector<juce::Point<float>> outerTop, outerBottom, bodyTop, bodyBottom;

    for (int k = 0; k < count; ++k)
    {
        const auto entry = first + static_cast<uint32_t> (k);
        const auto x = noiseRight - static_cast<float> (scroll.head - 1.0 - static_cast<double> (entry)) * pitch;
        const auto lo = ring.get (entry, 0), hi = ring.get (entry, 1), mean = ring.get (entry, 2), rms = ring.get (entry, 3);
        const auto sd = std::sqrt (std::max (0.0f, rms * rms - mean * mean));
        const auto peak = std::max ({ std::abs (lo), std::abs (hi), 1.0e-9f });
        const auto body = noiseHeight (peak) * std::min (1.0f, sd / peak);

        outerTop.push_back ({ x, noiseCentre - noiseHeight (std::max (0.0f, hi)) });
        outerBottom.push_back ({ x, noiseCentre + noiseHeight (std::max (0.0f, -lo)) });
        bodyTop.push_back ({ x, noiseCentre - body });
        bodyBottom.push_back ({ x, noiseCentre + body });
    }

    // A slow-sweep scope draws noise as a bright body with dim, spiky fringes; the fringe's edge is traced so a
    // flat envelope (hiss, hum) and a spiky one (crackle) separate at a glance. Only the oldest part fades.
    const Afterglow fade { noiseLeft, noiseLeft + 30.0f, 0.4f };
    setAgedFill (g, phosphor, 0.16f * intensity, fade);
    g.fillPath (bandPath (outerTop, outerBottom));
    setAgedFill (g, phosphor, 0.62f * intensity, fade);
    g.fillPath (bandPath (bodyTop, bodyBottom));
    setAgedFill (g, phosphor, 0.40f * intensity, fade);
    const juce::PathStrokeType edge (std::max (1.0f, pixel()), juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.strokePath (polyline (outerTop), edge);
    g.strokePath (polyline (outerBottom), edge);
}

//======================================================================================================================
namespace
{
    // WOBBLE geometry: 4 s of pitch deviation, newest at the right. The cents scale is fixed: close to linear for
    // small deviations and gently compressed for large ones (asinh), so a subtle 2 cent wow is visible and a
    // 50 cent warp still fits.
    constexpr float wobbleLeft = 4.0f, wobbleRight = 128.0f, wobbleCentre = 25.0f;
    constexpr int wobbleColumns = 128;
    constexpr double wobbleColumnRate = 32.0; // two 1/64 s entries per column

    float wobbleY (float cents) { return wobbleCentre - 20.0f * std::asinh (cents / 6.0f) / std::asinh (11.0f); }

    /** Smooths a band edge: the extreme over three columns, then a three-tap average. */
    std::vector<float> smoothEdge (const std::vector<float>& values, bool upper)
    {
        const auto n = values.size();
        std::vector<float> extreme (n), out (n);
        for (size_t i = 0; i < n; ++i)
        {
            const auto a = values[i > 0 ? i - 1 : i], b = values[i], c = values[i + 1 < n ? i + 1 : i];
            extreme[i] = upper ? std::min ({ a, b, c }) : std::max ({ a, b, c }); // screen y: up is smaller
        }
        for (size_t i = 0; i < n; ++i)
            out[i] = (extreme[i > 0 ? i - 1 : i] + extreme[i] + extreme[i + 1 < n ? i + 1 : i]) / 3.0f;
        return out;
    }
} // namespace

WobbleDisplay::WobbleDisplay (juce::AudioProcessorValueTreeState& s, dsp::EngineTelemetry& t)
    : ModuleDisplay (s, t, 1, ParamIDs::wobbleOn) {}

void WobbleDisplay::tick (double seconds)
{
    const auto written = telemetry.wobblePitch.written.load (std::memory_order_relaxed);
    watchData (written);
    scroll.advance (written / 2, seconds, wobbleColumnRate);
    ModuleDisplay::tick (seconds);
}

void WobbleDisplay::drawStatic (const Canvas& c)
{
    c.hLine (wobbleLeft, wobbleRight, wobbleCentre, phosphor.withAlpha (0.12f));

    // Scale on the left edge: +-10 cents (minor) and +-50 cents, a quarter-tone (major). The right edge is the pen's.
    for (auto cents : { 10.0f, -10.0f, 50.0f, -50.0f })
    {
        const auto major = std::abs (cents) > 20.0f;
        c.edgeTick (wobbleY (cents), major, phosphor.withAlpha (major ? 0.40f : 0.28f));
    }
}

void WobbleDisplay::drawLive (juce::Graphics& g)
{
    const auto mix = param (ParamIDs::wobbleMix) * 0.01f;
    const auto intensity = liveIntensity() * (0.45f + 0.55f * mix);
    const auto& ring = telemetry.wobblePitch;

    // Columns are whole, even-aligned pairs of entries, so the history never re-pairs from frame to frame.
    const auto writtenColumns = ring.written.load (std::memory_order_acquire) / 2;
    const auto lastColumn = std::min (writtenColumns, static_cast<uint32_t> (std::max (0.0, std::floor (scroll.head))));
    const auto columns = static_cast<int> (std::min (lastColumn, static_cast<uint32_t> (std::min (wobbleColumns + 1, ring.size / 4 - 2))));
    if (columns < 2 || intensity < 0.004f)
        return;

    const auto firstColumn = lastColumn - static_cast<uint32_t> (columns);
    const auto stereo = param (ParamIDs::wobbleStereo) > 0.5f;
    const auto pitch = (wobbleRight - wobbleLeft) / static_cast<float> (wobbleColumns - 1);

    struct Lane { std::vector<juce::Point<float>> line, upper, lower; };
    Lane lanes[2];

    for (int c = 0; c < (stereo ? 2 : 1); ++c)
    {
        const auto base = 3 * c;
        std::vector<float> xs, mean, hi, lo;
        for (int j = 0; j < columns; ++j)
        {
            const auto column = firstColumn + static_cast<uint32_t> (j);
            const auto a = column * 2;
            xs.push_back (wobbleRight - static_cast<float> (scroll.head - 1.0 - static_cast<double> (column)) * pitch);
            mean.push_back (wobbleY (0.5f * (ring.get (a, base) + ring.get (a + 1, base))));
            hi.push_back (wobbleY (std::max (ring.get (a, base + 2), ring.get (a + 1, base + 2))));
            lo.push_back (wobbleY (std::min (ring.get (a, base + 1), ring.get (a + 1, base + 1))));
        }

        const auto upper = smoothEdge (hi, true);
        const auto lower = smoothEdge (lo, false);
        for (size_t j = 0; j < xs.size(); ++j)
        {
            // Where the flutter band is thinner than the line itself, draw only the line.
            const auto thick = lower[j] - upper[j] >= 0.8f;
            lanes[c].line.push_back ({ xs[j], mean[j] });
            lanes[c].upper.push_back ({ xs[j], thick ? std::min (upper[j], mean[j]) : mean[j] });
            lanes[c].lower.push_back ({ xs[j], thick ? std::max (lower[j], mean[j]) : mean[j] });
        }
    }

    Beam beam;
    beam.reference = 6.0f;
    beam.floor = 0.5f;
    beam.afterglow = { wobbleLeft, wobbleLeft + 30.0f, 0.4f };

    if (stereo)
    {
        // The right channel sits underneath: dimmer, thinner, no halo.
        setAgedFill (g, phosphor, 0.12f * intensity, beam.afterglow);
        g.fillPath (bandPath (lanes[1].upper, lanes[1].lower));
        Beam right = beam;
        right.width = 1.0f;
        right.halo = false;
        drawBeam (g, lanes[1].line.data(), static_cast<int> (lanes[1].line.size()), phosphor, 0.55f * intensity, right);
    }

    // The line is the wow (the slow part of the pitch); the band around it is the full deviation including flutter.
    setAgedFill (g, phosphor, 0.20f * intensity, beam.afterglow);
    g.fillPath (bandPath (lanes[0].upper, lanes[0].lower));
    drawBeam (g, lanes[0].line.data(), static_cast<int> (lanes[0].line.size()), phosphor, intensity, beam);

    drawEdgePen (g, lanes[0].line.back().y, 0.9f * intensity);
    if (stereo)
        drawEdgePen (g, lanes[1].line.back().y, 0.5f * intensity);
}

//======================================================================================================================
namespace
{
    // DISTORT geometry: origin on the shared baseline. The input axis spans each type's working range (the core's
    // input after drive), the output axis +-1.5.
    constexpr float distortOriginX = 66.0f, distortOriginY = 25.0f, distortHalfX = 61.0f, distortHalfY = 21.0f;
    constexpr float distortRange[] { 2.5f, 2.5f, 2.5f, 2.5f, 2.0f, 2.0f, 4.0f, 2.5f };
    constexpr int transformerType = 1, foldType = 6;

    juce::Point<float> distortPoint (float x, float y, float range)
    {
        return { distortOriginX + distortHalfX * x / range, distortOriginY - distortHalfY * juce::jlimit (-1.7f, 1.7f, y) / 1.5f };
    }

    /** The curve the display draws for a type: Transformer shows its bass transfer, where the iron saturates first. */
    float displayCurve (int type, float x, float bias)
    {
        return type == transformerType ? dsp::DistortModule::transformerBassCurve (x, bias) : dsp::DistortModule::staticCurve (type, x, bias);
    }

    juce::Path curvePath (int type, float bias, float range, float from, float to, bool treble = false)
    {
        juce::Path curve;
        constexpr int points = 160;
        for (int i = 0; i <= points; ++i)
        {
            const auto x = from + (to - from) * static_cast<float> (i) / points;
            const auto y = treble ? dsp::DistortModule::staticCurve (type, x, bias) : displayCurve (type, x, bias);
            if (i == 0)
                curve.startNewSubPath (distortPoint (x, y, range));
            else
                curve.lineTo (distortPoint (x, y, range));
        }
        return curve;
    }
} // namespace

DistortDisplay::DistortDisplay (juce::AudioProcessorValueTreeState& s, dsp::EngineTelemetry& t)
    : ModuleDisplay (s, t, 2, ParamIDs::distortOn) {}

int DistortDisplay::currentType() const { return juce::jlimit (0, 7, juce::roundToInt (param (ParamIDs::distortType))); }

void DistortDisplay::tick (double seconds)
{
    const auto& ring = telemetry.distortTransfer;
    const auto written = ring.written.load (std::memory_order_acquire);
    watchData (written);
    bias = telemetry.distortBias.load (std::memory_order_relaxed);

    // Hold how far into the curve the signal has reached in the last moments (separately for each side, the curves
    // are asymmetric), and let it fall back over about a second.
    auto top = 0.0f, bottom = 0.0f;
    const auto count = std::min<uint32_t> (written, 200);
    for (uint32_t k = 0; k < count; ++k)
    {
        const auto x = ring.get (written - 1 - k, 0);
        top = std::max (top, x);
        bottom = std::max (bottom, -x);
    }
    const auto fall = std::exp (-static_cast<float> (seconds) / 1.2f);
    const auto live = isActive() && isDataFresh();
    holdPositive = std::max (live ? top : 0.0f, holdPositive * fall);
    holdNegative = std::max (live ? bottom : 0.0f, holdNegative * fall);

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

    c.hLine (5.0f, 127.0f, distortOriginY, phosphor.withAlpha (0.08f));
    c.vLine (distortOriginX, 4.0f, 46.0f, phosphor.withAlpha (0.08f));

    // The clean wire (output = input), dashed, for comparison.
    juce::Path wire;
    wire.startNewSubPath (distortPoint (-1.5f, -1.5f, range));
    wire.lineTo (distortPoint (1.5f, 1.5f, range));
    c.dashed (wire, 1.0f, 2.0f, phosphor.withAlpha (0.14f));

    // Transformer: the treble transfer (less saturated) as a dashed line; treble is dashed on every screen.
    if (type == transformerType)
        c.dashed (curvePath (type, bias, range, -range, range, true), 1.0f, 2.0f, phosphor.withAlpha (0.18f));

    c.stroke (curvePath (type, bias, range, -range, range), 1.2f, phosphor.withAlpha (0.42f));
}

void DistortDisplay::drawLive (juce::Graphics& g)
{
    const auto type = currentType();
    const auto range = distortRange[type];

    // How far the drive has pushed into the curve lately: the reached stretch of the curve stays lit, with a short
    // tick at each end.
    const auto reachTop = std::min (holdPositive, 1.04f * range);
    const auto reachBottom = std::min (holdNegative, 1.04f * range);
    if (activity > 0.004f && reachTop + reachBottom > 0.02f)
    {
        g.setColour (phosphor.withAlpha (0.40f * activity));
        g.strokePath (curvePath (type, bias, range, -reachBottom, reachTop),
                      juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (phosphor.withAlpha (0.6f * activity));
        for (auto x : { -reachBottom, reachTop })
        {
            const auto p = distortPoint (x, displayCurve (type, x, bias), range);
            g.fillRect (juce::Rectangle<float> (std::max (1.0f, pixel()), 3.0f).withCentre (p));
        }
    }

    const auto intensity = liveIntensity() * (0.25f + 0.75f * param (ParamIDs::distortMix) * 0.01f);
    const auto& ring = telemetry.distortTransfer;
    const auto written = ring.written.load (std::memory_order_acquire);
    const auto count = static_cast<int> (std::min<uint32_t> (written, 256)); // about 21 ms of the core's history
    if (count < 2 || intensity < 0.004f)
        return;

    std::vector<juce::Point<float>> trace;
    trace.reserve (static_cast<size_t> (count));
    auto peak = 0.0f;
    for (int k = 0; k < count; ++k)
    {
        const auto entry = written - static_cast<uint32_t> (count - k);
        auto x = ring.get (entry, 0);
        peak = std::max (peak, std::abs (x));
        // Monotonic curves pin at the end of the axis (the output is on its rail there anyway); Fold keeps folding.
        if (type != foldType)
            x = juce::jlimit (-1.04f * range, 1.04f * range, x);
        trace.push_back (distortPoint (x, ring.get (entry, 1), range));
    }

    // The live beam: what the core is doing right now. Silence leaves only the curve and the held stretch.
    Beam beam;
    beam.width = 1.4f;
    beam.reference = 0.9f;
    beam.floor = 0.15f;
    drawBeam (g, trace.data(), count, phosphor, intensity * smoothstep (0.02f, 0.08f, peak), beam);
}

//======================================================================================================================
namespace
{
    // DIGITAL geometry: a test chirp from 20 Hz to 20 kHz on a log axis, 10 visible cycles, amplitude tapering to
    // the right (as on a sweep generator's output), kept below the readout slot.
    constexpr float sweepLeft = 5.0f, sweepWidth = 122.0f, sweepCentre = 28.0f;
    constexpr int sweepPoints = 1000;

    struct ChirpTable
    {
        std::array<float, sweepPoints> u {}, hz {}, phase {}, seconds {};

        ChirpTable()
        {
            // Phase advances with f^0.3 per unit of width, so the chirp reads as one sweep while its fastest cycles
            // stay open; the true time between points follows from the phase step at that frequency.
            std::array<float, sweepPoints> step {};
            auto total = 0.0f;
            for (int i = 0; i < sweepPoints; ++i)
            {
                u[static_cast<size_t> (i)] = static_cast<float> (i) / static_cast<float> (sweepPoints - 1);
                hz[static_cast<size_t> (i)] = 20.0f * std::pow (1000.0f, u[static_cast<size_t> (i)]);
                step[static_cast<size_t> (i)] = i == 0 ? 0.0f : std::pow (hz[static_cast<size_t> (i)], 0.3f);
                total += step[static_cast<size_t> (i)];
            }

            const auto scale = 10.0f * juce::MathConstants<float>::twoPi / total;
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
    // Only what the converter is actually doing; an untouched converter shows nothing.
    const auto hostRate = telemetry.sampleRate.load (std::memory_order_relaxed);
    juce::StringArray parts;
    if (rate < hostRate * 0.985f)
        parts.add (rate >= 999.5f ? juce::String (rate / 1000.0f, 1) + "k" : juce::String (juce::roundToInt (rate)));
    if (bits < 15.5f)
        parts.add (juce::String (juce::jlimit (1, 16, juce::roundToInt (bits))) + "b");
    return parts.joinIntoString (" ");
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
    c.stroke (clean, 1.0f, phosphor.withAlpha (0.16f));

    // Frequency scale along the bottom: 100 Hz, 1 kHz (major), 10 kHz.
    for (auto hz : { 100.0f, 1000.0f, 10000.0f })
        c.bottomTick (sweepX (hz), hz > 500.0f && hz < 5000.0f, phosphor.withAlpha (hz > 500.0f && hz < 5000.0f ? 0.40f : 0.28f));
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
    reduced = rateReduced || bitsReduced;
    const auto levels = std::pow (2.0f, bits - 1.0f);
    const auto antiAliasHz = std::exp (juce::jmap (smooth, std::log (0.45f * hostRate), std::log (std::min (0.45f * hostRate, 0.46f * rate))));

    // One sample of the module's converter, as in DigitalModule: anti-alias filter (Smooth), hold, quantise.
    auto converted = [&] (long k)
    {
        // Clock jitter moves the sampling instants by a fixed pseudo-random amount per sample.
        const auto offset = jitter * 0.5f * (std::fmod (std::sin (static_cast<float> (k) * 12.9898f) * 43758.547f, 1.0f));
        float ph = 0.0f, hz = 20.0f;
        table.at ((static_cast<float> (k) + offset) / rate, ph, hz);
        const auto antiAlias = 1.0f / std::sqrt (1.0f + std::pow (hz / antiAliasHz, 4.0f));
        auto v = 0.5f * antiAlias * std::sin (ph);

        if (bitsReduced)
        {
            if (compand)
            {
                constexpr float mu = 255.0f;
                const auto encoded = std::copysign (std::log1p (mu * std::abs (v)) / std::log1p (mu), v);
                const auto q = quantiseLevel (encoded, levels);
                v = std::copysign ((std::exp (std::abs (q) * std::log1p (mu)) - 1.0f) / mu, q);
            }
            else
            {
                v = quantiseLevel (v, levels);
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
        else if (rateReduced)
        {
            const auto position = table.seconds[i] * rate;
            const auto k = static_cast<long> (std::floor (position));
            processed = converted (k);
            if (smooth > 0.0f) // reconstruction filter, shown as a blend towards the joined samples
                processed = juce::jmap (smooth, processed, juce::jmap (position - static_cast<float> (k), processed, converted (k + 1)));
        }
        else if (bitsReduced)
        {
            processed = converted (static_cast<long> (std::floor (table.seconds[i] * hostRate)));
        }

        const auto value = clean + mix * (processed - clean);
        sweep.push_back ({ sweepLeft + sweepWidth * table.u[i], sweepCentre - value * sweepAmplitude (table.u[i]) });
    }

    nyquistX = rateReduced && rate * 0.5f < 20000.0f ? sweepX (rate * 0.5f) : -1.0f;
}

void DigitalDisplay::drawLive (juce::Graphics& g)
{
    // An untouched converter sits quieter than a working one.
    const auto intensity = activity * (reduced ? 1.0f : 0.6f);
    if (sweep.empty() || intensity < 0.004f)
        return;

    // The sweep only changes with the converter settings, so it is rendered once into an image at device resolution
    // and drawn with the current intensity.
    const auto imageKey = sweepKey + "@" + juce::String (pixelScale);
    if (imageKey != sweepImageKey || ! sweepImage.isValid())
    {
        sweepImageKey = imageKey;
        sweepImage = juce::Image (juce::Image::ARGB, static_cast<int> (std::ceil (screenWidth * pixelScale)), static_cast<int> (std::ceil (screenHeight * pixelScale)), true);
        juce::Graphics ig (sweepImage);
        ig.addTransform (juce::AffineTransform::scale (pixelScale));
        Beam beam;
        beam.reference = 2.2f;
        beam.floor = 0.35f;
        beam.halo = false;
        drawBeam (ig, sweep.data(), static_cast<int> (sweep.size()), phosphor, 1.0f, beam);
    }

    {
        juce::Graphics::ScopedSaveState save (g);
        g.setOpacity (juce::jlimit (0.0f, 1.0f, intensity));
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImageTransformed (sweepImage, juce::AffineTransform::scale (1.0f / pixelScale));
    }

    // Nyquist: where the sweep passes half the sample rate and aliasing begins, on the frequency scale.
    if (nyquistX > 0.0f)
    {
        const auto half = std::max (2.3f, 2.5f * pixel());
        const auto depth = std::max (3.2f, 3.5f * pixel());
        juce::Path mark;
        mark.addTriangle (nyquistX - half, screenHeight, nyquistX, screenHeight - depth, nyquistX + half, screenHeight);
        g.setColour (phosphor.withAlpha (0.8f * intensity));
        g.fillPath (mark);
    }

    // Focus band edges, when the band does not cover the whole sweep.
    g.setColour (phosphor.withAlpha (0.45f * intensity));
    for (auto hz : { param (ParamIDs::digitalFocusLow), param (ParamIDs::digitalFocusHigh) })
    {
        const auto u = std::log (std::max (1.0f, hz) / 20.0f) / std::log (1000.0f);
        if (u > 0.01f && u < 0.99f)
        {
            const auto x = std::round ((sweepLeft + sweepWidth * u) * pixelScale) / pixelScale;
            g.fillRect (juce::Rectangle<float> (x, screenHeight - 5.0f, pixel(), 5.0f));
        }
    }
}

//======================================================================================================================
namespace
{
    // SPACE geometry: a linear time axis whose length depends on the type (so the slope still moves with Decay on
    // the long types), and a dB axis from 0 dB (just under the readout slot) down to -60 dB on the floor line.
    constexpr float spaceLeft = 10.0f, spaceRight = 128.0f, spaceDry = 6.0f;
    constexpr float spaceTop = 11.5f, spaceFloor = 44.5f;
    constexpr int plateType = 2, hallType = 3, springType = 4, resonatorType = 5;

    float spaceY (float db) { return spaceTop + (spaceFloor - spaceTop) * juce::jlimit (0.0f, 60.0f, -db) / 60.0f; }

    // Resonator: twelve chromatic cells from C, with a piano colouring strip under them.
    constexpr float cellLeft = 6.0f, cellPitch = 10.0f, cellInset = 1.0f, cellTop = 13.0f, cellFloor = 40.5f;
    bool isBlackKey (int note) { return note == 1 || note == 3 || note == 6 || note == 8 || note == 10; }
} // namespace

SpaceDisplay::SpaceDisplay (juce::AudioProcessorValueTreeState& s, dsp::EngineTelemetry& t)
    : ModuleDisplay (s, t, 4, ParamIDs::spaceOn) {}

bool SpaceDisplay::isResonator() const { return juce::roundToInt (param (ParamIDs::spaceType)) == resonatorType; }

float SpaceDisplay::windowSeconds() const
{
    const auto type = juce::roundToInt (param (ParamIDs::spaceType));
    return type == hallType ? 6.0f : (type == plateType ? 3.0f : 1.5f);
}

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
        const auto db = gainToDb (telemetry.spaceNotes[k].load (std::memory_order_relaxed));
        noteDb[k] = approach (noteDb[k], db, static_cast<float> (seconds), db > noteDb[k] ? 0.03f : 0.15f);
    }

    ModuleDisplay::tick (seconds);
}

juce::String SpaceDisplay::staticKey() const { return isResonator() ? "resonator" : "decay" + juce::String (windowSeconds()); }

juce::String SpaceDisplay::readoutText() const
{
    const auto rt = decayAt (1000.0f);
    return juce::String (rt, rt < 10.0f ? 1 : 0) + "s";
}

void SpaceDisplay::drawStatic (const Canvas& c)
{
    if (isResonator())
    {
        for (int k = 0; k < 12; ++k)
        {
            const auto x = cellLeft + cellPitch * static_cast<float> (k);
            juce::Path slot;
            slot.addRectangle (x + cellInset, cellTop, cellPitch - 2.0f * cellInset, cellFloor - cellTop);
            c.fill (slot, phosphor.withAlpha (0.06f));

            juce::Path key;
            key.addRectangle (x + cellInset, 43.0f, cellPitch - 2.0f * cellInset, 4.0f);
            c.fill (key, phosphor.withAlpha (isBlackKey (k) ? 0.10f : 0.30f));
        }
        return;
    }

    const auto window = windowSeconds();
    const auto minor = window / 6.0f;
    c.hLine (spaceLeft, spaceRight, spaceFloor, phosphor.withAlpha (0.14f));
    for (int i = 1; i <= 6; ++i)
    {
        const auto x = spaceLeft + (spaceRight - spaceLeft) * minor * static_cast<float> (i) / window;
        const auto major = i % 2 == 0;
        c.vLine (x, spaceFloor + 1.0f, spaceFloor + (major ? 4.0f : 2.2f), phosphor.withAlpha (major ? 0.40f : 0.25f));
    }
}

void SpaceDisplay::drawLive (juce::Graphics& g)
{
    if (activity < 0.004f)
        return;

    if (isResonator())
    {
        // Each comb's level relative to a fixed scale from -54 to -6 dB: a dim column with a bright cap.
        const auto intensity = liveIntensity();
        for (size_t k = 0; k < 12; ++k)
        {
            const auto level = juce::jlimit (0.0f, 1.0f, (noteDb[k] + 54.0f) / 48.0f);
            if (level <= 0.0f)
                continue;
            const auto x = cellLeft + cellPitch * static_cast<float> (k) + cellInset;
            const auto w = cellPitch - 2.0f * cellInset;
            const auto top = cellFloor - level * (cellFloor - cellTop);
            g.setColour (phosphor.withAlpha (0.25f * intensity));
            g.fillRect (juce::Rectangle<float> (x, top, w, cellFloor - top));
            g.setColour (phosphor.withAlpha (0.9f * intensity));
            g.fillRect (juce::Rectangle<float> (x, top, w, 1.6f));
        }
        return;
    }

    const auto window = windowSeconds();
    auto spaceX = [window] (float seconds) { return spaceLeft + (spaceRight - spaceLeft) * seconds / window; };

    // The model: what this type, Decay, Pre-delay, Focus and Amount should do to a single hit.
    const auto type = juce::jlimit (0, 5, juce::roundToInt (param (ParamIDs::spaceType)));
    const auto amount = juce::jlimit (0.0f, 1.0f, param (ParamIDs::spaceAmount) * param (ParamIDs::magnitude) * 1.0e-4f);
    const auto dryDb = std::max (-60.0f, gainToDb (std::min (1.0f, 2.0f * (1.0f - amount))));
    const auto wetDb = std::max (-60.0f, gainToDb (std::pow (std::min (1.0f, 2.0f * amount), 1.2f)));
    float buildMinMs = 0.0f, buildMaxMs = 0.0f;
    dsp::SpaceModule::buildUpMs (type, buildMinMs, buildMaxMs);
    const auto pre = preDelayMs * 0.001f;
    const auto firstArrival = pre + (type == plateType || type == springType ? buildMinMs : 3.0f) * 0.001f;
    const auto peakTime = pre + buildMaxMs * 0.001f;

    auto slope = [&] (float rt, juce::Point<float>& end)
    {
        juce::Path p;
        p.startNewSubPath (spaceX (peakTime), spaceY (wetDb));
        const auto finish = peakTime + rt * (60.0f + wetDb) / 60.0f;
        end = finish <= window ? juce::Point<float> (spaceX (finish), spaceFloor)
                               : juce::Point<float> (spaceRight, spaceY (wetDb - 60.0f * (window - peakTime) / std::max (0.01f, rt)));
        p.lineTo (end);
        return p;
    };

    // Guide: nothing until the first reflections arrive after the pre-delay, a build-up to the peak, then a straight
    // fall of 60 dB over the decay time at 1 kHz; the treble (5 kHz) falls faster and is dashed.
    const auto modelAlpha = activity;
    juce::Point<float> end;
    juce::Path guide;
    guide.startNewSubPath (spaceX (firstArrival), spaceFloor);
    guide.lineTo (spaceX (firstArrival), spaceY (wetDb - 10.0f));
    guide.lineTo (spaceX (peakTime), spaceY (wetDb));
    guide.addPath (slope (decayAt (1000.0f), end));
    g.setColour (phosphor.withAlpha (0.38f * modelAlpha));
    g.strokePath (guide, juce::PathStrokeType (std::max (1.2f, pixel()), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path treble;
    const auto dashLength = std::max (2.0f, 2.5f * pixel());
    const float dash[] { dashLength, dashLength };
    juce::PathStrokeType (std::max (1.0f, pixel())).createDashedStroke (treble, slope (decayAt (5000.0f), end), dash, 2);
    g.setColour (phosphor.withAlpha (0.26f * modelAlpha));
    g.fillPath (treble);

    // The dry hit at time zero, in its own column: an impulse with a small cap.
    if (dryDb > -59.0f)
    {
        const auto x = std::round (spaceDry * pixelScale) / pixelScale;
        g.setColour (phosphor.withAlpha (0.45f * modelAlpha));
        g.fillRect (juce::Rectangle<float> (x, spaceY (dryDb), pixel(), spaceFloor - spaceY (dryDb)));
        g.fillRect (juce::Rectangle<float> (x - 1.0f, spaceY (dryDb), 2.0f + pixel(), pixel()));
    }

    // The measured wet level after the latest note, written onto the guide. It starts where the reflections arrive
    // and is normalised to its own build-up peak (held once the build-up is over). When the previous tail is still
    // within 10 dB of the new peak, the note did not restart the decay, and nothing is drawn.
    const auto& ring = telemetry.spaceWet;
    const auto written = ring.written.load (std::memory_order_acquire);
    const auto onset = telemetry.spaceOnsetEntry.load (std::memory_order_relaxed);
    const auto since = static_cast<int> (written - onset);
    const auto start = static_cast<int> (std::round (firstArrival / 0.01f));
    const auto steps = std::min (static_cast<int> (window / 0.01f), ring.size / 2 - 8);
    const auto fadeSteps = 30;
    if (since < start + 2 || since > steps + fadeSteps || since > ring.size / 2)
        return;

    auto level = [&] (int i)
    {
        // A 30 ms power average, so the trace reads as a decay rather than as jitter.
        auto sum = 0.0f;
        for (int k = std::max (0, i - 1); k <= std::min (since - 1, i + 1); ++k)
            sum += std::pow (10.0f, ring.get (onset + static_cast<uint32_t> (k), 0) * 0.1f);
        return 10.0f * std::log10 (sum / static_cast<float> (std::min (since - 1, i + 1) - std::max (0, i - 1) + 1) + 1.0e-12f);
    };

    const auto count = std::min (since, steps);
    const auto peakEnd = std::min (count, static_cast<int> (std::round ((peakTime + 0.05f) / 0.01f)) + 1);
    auto peak = -120.0f;
    for (int i = start; i < std::max (start + 1, peakEnd); ++i)
        peak = std::max (peak, level (i));

    if (ring.get (onset, 0) > peak - 10.0f)
        return;

    std::vector<juce::Point<float>> comet;
    for (int i = start; i < count; ++i)
        comet.push_back ({ spaceX (0.01f * static_cast<float> (i)), spaceY (level (i) - peak + wetDb) });
    if (comet.size() < 2)
        return;

    const auto ending = since > steps ? 1.0f - static_cast<float> (since - steps) / static_cast<float> (fadeSteps) : 1.0f;
    const auto intensity = liveIntensity() * ending;
    Beam beam;
    beam.reference = 1.6f;
    beam.floor = 0.25f;
    beam.afterglow = { comet.back().x - (spaceRight - spaceLeft) * 0.5f, comet.back().x, 0.0f };
    drawBeam (g, comet.data(), static_cast<int> (comet.size()), phosphor, intensity, beam);

    g.setColour (phosphor.withAlpha (0.8f * intensity));
    g.fillEllipse (juce::Rectangle<float> (2.0f, 2.0f).withCentre (comet.back()));
}

//======================================================================================================================
namespace
{
    // MAGNETIC geometry: 2 s of tape, newest at the right, level in dB hanging from a 0 dB ceiling on a compressive
    // law (1 - e^(dB / 8)), so the small dips of wear and flutter are readable and deep dropouts still fit
    // (-1 dB is an eighth of the way down, -12 dB three quarters, -40 dB the floor). Stereo shows two lanes.
    constexpr float tapeLeft = 4.0f, tapeRight = 128.0f;
    constexpr int tapeColumns = 128;
    constexpr double tapeRate = 64.0;

    struct TapeLane { float top, bottom; };
    constexpr TapeLane monoLane { 8.0f, 46.0f };
    constexpr TapeLane stereoLanes[] { { 6.5f, 24.5f }, { 28.5f, 46.5f } };

    float tapeY (const TapeLane& lane, float db)
    {
        const auto depth = juce::jlimit (0.0f, 40.0f, -db);
        return lane.top + (lane.bottom - lane.top) * (1.0f - std::exp (-depth / 8.0f)) / (1.0f - std::exp (-5.0f));
    }
} // namespace

MagneticDisplay::MagneticDisplay (juce::AudioProcessorValueTreeState& s, dsp::EngineTelemetry& t)
    : ModuleDisplay (s, t, 5, ParamIDs::magneticOn) {}

bool MagneticDisplay::isStereo() const { return param (ParamIDs::magneticStereo) > 0.5f; }

void MagneticDisplay::tick (double seconds)
{
    const auto written = telemetry.magneticTape.written.load (std::memory_order_relaxed);
    watchData (written);
    scroll.advance (written, seconds, tapeRate);
    ModuleDisplay::tick (seconds);
}

juce::String MagneticDisplay::staticKey() const { return isStereo() ? "stereo" : "mono"; }

void MagneticDisplay::drawStatic (const Canvas& c)
{
    const auto drawLane = [&] (const TapeLane& lane)
    {
        c.hLine (tapeLeft, tapeRight, lane.top, phosphor.withAlpha (0.08f));
        c.edgeTick (tapeY (lane, -3.0f), false, phosphor.withAlpha (0.28f));
        c.edgeTick (tapeY (lane, -12.0f), true, phosphor.withAlpha (0.40f));
        c.edgeTick (tapeY (lane, -24.0f), false, phosphor.withAlpha (0.28f));
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
    const auto count = visibleEntries (telemetry.magneticTape, scroll.head, tapeColumns + 1, first);
    if (count < 2 || intensity < 0.004f)
        return;

    const auto& ring = telemetry.magneticTape;
    const auto stereo = isStereo();
    const auto pitch = (tapeRight - tapeLeft) / static_cast<float> (tapeColumns - 1);
    const Afterglow fade { tapeLeft, tapeLeft + 30.0f, 0.4f };

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
            const auto x = tapeRight - static_cast<float> (scroll.head - 1.0 - static_cast<double> (entry)) * pitch;
            low.push_back ({ x, tapeY (lane, dsp::MagneticModule::responseDb (gainDb, loss, 1000.0f)) });
            high.push_back ({ x, tapeY (lane, dsp::MagneticModule::responseDb (gainDb, loss, 10000.0f)) });
        }

        // The treble being lost: a ribbon down to the 10 kHz level, edged with a dashed hairline (treble is dashed
        // on every screen). Where the loss is too small to see, the ribbon closes onto the 1 kHz line.
        std::vector<juce::Point<float>> ribbon (high);
        for (size_t k = 0; k < ribbon.size(); ++k)
            if (ribbon[k].y - low[k].y < 0.75f)
                ribbon[k].y = low[k].y;

        setAgedFill (g, phosphor, 0.22f * intensity, fade);
        g.fillPath (bandPath (low, ribbon));

        juce::Path trebleEdge;
        auto open = false;
        for (size_t k = 0; k < high.size(); ++k)
        {
            const auto visible = high[k].y - low[k].y >= 0.75f;
            if (visible && ! open)
                trebleEdge.startNewSubPath (high[k]);
            else if (visible)
                trebleEdge.lineTo (high[k]);
            open = visible;
        }
        juce::Path dashes;
        const auto dashLength = std::max (2.0f, 2.5f * pixel());
        const float dash[] { dashLength, dashLength };
        juce::PathStrokeType (std::max (0.9f, pixel())).createDashedStroke (dashes, trebleEdge, dash, 2);
        setAgedFill (g, phosphor, 0.45f * intensity, fade);
        g.fillPath (dashes);

        // The 1 kHz level: the recorder's pen line.
        Beam tone;
        tone.reference = 4.0f;
        tone.floor = 0.6f;
        tone.afterglow = fade;
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
