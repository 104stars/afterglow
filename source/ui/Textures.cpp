#include "Textures.h"
#include <cmath>

namespace afterglow::ui::Textures
{
namespace
{
    inline uint32_t hash (int x, int y, uint32_t seed) noexcept
    {
        auto h = static_cast<uint32_t> (x) * 374761393u + static_cast<uint32_t> (y) * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }

    inline float hash01 (int x, int y, uint32_t seed) noexcept
    {
        return static_cast<float> (hash (x, y, seed) & 0xffffffu) / 16777215.0f;
    }

    inline float smooth (float t) noexcept { return t * t * (3.0f - 2.0f * t); }

    float valueNoise (float x, float y, uint32_t seed) noexcept
    {
        const auto xi = static_cast<int> (std::floor (x));
        const auto yi = static_cast<int> (std::floor (y));
        const auto tx = smooth (x - static_cast<float> (xi));
        const auto ty = smooth (y - static_cast<float> (yi));
        const auto a = hash01 (xi, yi, seed);
        const auto b = hash01 (xi + 1, yi, seed);
        const auto c = hash01 (xi, yi + 1, seed);
        const auto d = hash01 (xi + 1, yi + 1, seed);
        return juce::jmap (ty, juce::jmap (tx, a, b), juce::jmap (tx, c, d));
    }

    float fbm (float x, float y, uint32_t seed, int octaves) noexcept
    {
        auto sum = 0.0f, amp = 0.5f, norm = 0.0f;
        for (int o = 0; o < octaves; ++o)
        {
            sum += amp * valueNoise (x, y, seed + static_cast<uint32_t> (o) * 101u);
            norm += amp;
            x *= 2.03f;
            y *= 2.03f;
            amp *= 0.5f;
        }
        return sum / norm;
    }

    template <typename Fn>
    juce::Image render (int w, int h, bool withAlpha, Fn&& fn)
    {
        juce::Image image (withAlpha ? juce::Image::ARGB : juce::Image::RGB, std::max (1, w), std::max (1, h), true);
        juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                data.setPixelColour (x, y, fn (x, y));

        return image;
    }

    juce::Colour toColour (float r, float g, float b, float a = 1.0f)
    {
        return juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, r), juce::jlimit (0.0f, 1.0f, g), juce::jlimit (0.0f, 1.0f, b), a);
    }

    juce::Image makeWalnut (int w, int h, uint32_t seed)
    {
        // Grain runs along the long axis. Domain-warped stripes give figured walnut.
        const auto alongX = w > h;
        return render (w, h, false, [=] (int px, int py)
        {
            const auto u = static_cast<float> (alongX ? py : px);
            const auto v = static_cast<float> (alongX ? px : py);
            const auto warp = fbm (u * 0.018f, v * 0.0035f, seed, 4) * 18.0f;
            const auto stripes = 0.5f + 0.5f * std::sin ((u * 0.42f + warp) * 0.9f);
            const auto fine = fbm (u * 0.9f, v * 0.012f, seed + 7u, 3);
            const auto pores = hash01 (px, py, seed + 3u) > 0.985f ? 0.55f : 1.0f;
            auto t = 0.25f + 0.45f * std::pow (stripes, 2.2f) + 0.3f * fine;
            t *= pores;
            const auto r = juce::jmap (t, 0.10f, 0.42f);
            const auto g = juce::jmap (t, 0.06f, 0.26f);
            const auto b = juce::jmap (t, 0.035f, 0.15f);
            return toColour (r, g, b);
        });
    }

    juce::Image makeBrushedMetal (int w, int h, uint32_t seed)
    {
        std::vector<float> rows (static_cast<size_t> (h));
        for (int y = 0; y < h; ++y)
            rows[static_cast<size_t> (y)] = hash01 (0, y, seed);

        return render (w, h, false, [&rows, seed] (int x, int y)
        {
            const auto streak = fbm (static_cast<float> (x) * 0.004f, static_cast<float> (y) * 0.7f, seed, 3);
            const auto row = rows[static_cast<size_t> (y)];
            const auto grain = hash01 (x, y, seed + 9u);
            const auto v = 0.30f + 0.10f * streak + 0.035f * row + 0.025f * grain;
            return toColour (v * 0.97f, v * 0.985f, v);
        });
    }

    juce::Image makeTolex (int w, int h, uint32_t seed)
    {
        // Black vinyl covering: a pebbled height map lit from the top-left.
        auto height = [seed] (int x, int y)
        {
            return fbm (static_cast<float> (x) * 0.32f, static_cast<float> (y) * 0.32f, seed, 3);
        };

        return render (w, h, false, [&] (int x, int y)
        {
            const auto h0 = height (x, y);
            const auto shade = (h0 - height (x + 1, y + 1)) * 2.2f;
            const auto v = 0.075f + 0.05f * h0 + 0.05f * shade;
            return toColour (v, v * 0.98f, v * 0.96f);
        });
    }

    juce::Image makeGrainTile (int size, uint32_t seed, bool light, bool dark)
    {
        return render (size, size, true, [=] (int x, int y)
        {
            const auto n = hash01 (x, y, seed);
            const auto speck = hash01 (x, y, seed + 17u) > 0.992f;
            if (light && n > 0.5f)
                return juce::Colours::white.withAlpha ((n - 0.5f) * 0.10f + (speck ? 0.08f : 0.0f));
            if (dark)
                return juce::Colours::black.withAlpha ((0.5f - std::min (n, 0.5f)) * 0.16f + (speck ? 0.12f : 0.0f));
            return juce::Colours::transparentBlack;
        });
    }

} // namespace

juce::Image get (Kind kind, int pixelWidth, int pixelHeight, uint32_t seed)
{
    pixelWidth = std::clamp (pixelWidth, 1, 8192);
    pixelHeight = std::clamp (pixelHeight, 1, 8192);
    // Cached in the editors' shared resources (never in a static, see UiResources).
    auto* resources = UiResources::current();
    const auto key = "texture/" + juce::String (static_cast<int> (kind)) + "/" + juce::String (pixelWidth) + "x" + juce::String (pixelHeight)
                   + "/" + juce::String (static_cast<juce::int64> (seed));

    if (resources != nullptr)
        if (auto cached = resources->findImage (key); cached.isValid())
            return cached;

    juce::Image image;
    switch (kind)
    {
        case Kind::walnut:        image = makeWalnut (pixelWidth, pixelHeight, seed); break;
        case Kind::brushedMetal:  image = makeBrushedMetal (pixelWidth, pixelHeight, seed); break;
        case Kind::tolex:         image = makeTolex (pixelWidth, pixelHeight, seed); break;
        case Kind::grainTile:     image = makeGrainTile (pixelWidth, seed, true, true); break;
        case Kind::creamTile:     image = makeGrainTile (pixelWidth, seed, false, true); break;
        case Kind::darkGrainTile: image = makeGrainTile (pixelWidth, seed, true, false); break;
    }

    if (resources != nullptr)
        resources->storeImage (key, image);
    return image;
}

void fillTiled (juce::Graphics& g, const juce::Image& tile, juce::Rectangle<float> area, float pixelsPerUnit, float opacity)
{
    if (! tile.isValid())
        return;

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area.toNearestInt());
    g.setOpacity (opacity);
    const auto scale = 1.0f / std::max (0.1f, pixelsPerUnit);
    g.setTiledImageFill (tile, 0, 0, 1.0f);
    g.addTransform (juce::AffineTransform::scale (scale));
    g.fillRect (area.getX() / scale, area.getY() / scale, area.getWidth() / scale, area.getHeight() / scale);
}

void drawFitted (juce::Graphics& g, const juce::Image& image, juce::Rectangle<float> area, float opacity)
{
    if (! image.isValid())
        return;

    g.setOpacity (opacity);
    g.drawImage (image, area, juce::RectanglePlacement::stretchToFit);
    g.setOpacity (1.0f);
}


} // namespace afterglow::ui::Textures
