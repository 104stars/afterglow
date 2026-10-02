#pragma once

#include "Theme.h"

namespace afterglow::ui
{
/** Procedurally generated material textures (no bitmap assets), rendered at the exact pixel size needed
    so they stay sharp at every interface scale and on high-DPI displays. Results are cached. */
namespace Textures
{
    enum class Kind { walnut, brushedMetal, tolex, grainTile, creamTile, darkGrainTile };

    /** Returns a cached texture of the given kind at the given pixel size. Call from the message thread only. */
    juce::Image get (Kind kind, int pixelWidth, int pixelHeight, uint32_t seed = 1);

    /** Tiles an image over an area, at the given device scale (texture pixels per logical unit). */
    void fillTiled (juce::Graphics& g, const juce::Image& tile, juce::Rectangle<float> area, float pixelsPerUnit, float opacity = 1.0f);

    /** Draws a texture that was generated for exactly this area. */
    void drawFitted (juce::Graphics& g, const juce::Image& image, juce::Rectangle<float> area, float opacity = 1.0f);
} // namespace Textures

} // namespace afterglow::ui
