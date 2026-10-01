#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace afterglow::ui
{
/** Fixed design size of the interface; everything is laid out in these units and scaled as a whole. */
inline constexpr int designWidth = 1120;
inline constexpr int designHeight = 720;

namespace Colours
{
    // Hardware materials
    inline const juce::Colour walnutDark   { 0xff2a1a10 };
    inline const juce::Colour walnutLight  { 0xff6b4329 };
    inline const juce::Colour chassis      { 0xff17181a };
    inline const juce::Colour chassisEdge  { 0xff2b2d31 };
    inline const juce::Colour brushedLight { 0xff9a9ea3 };
    inline const juce::Colour brushedDark  { 0xff5d6166 };
    inline const juce::Colour cream        { 0xffe9dfc8 };
    inline const juce::Colour creamShadow  { 0xffc9bc9e };
    inline const juce::Colour ink          { 0xff2b2622 };
    inline const juce::Colour silkscreen   { 0xfff4ead3 };
    inline const juce::Colour glassDark    { 0xff0b0d0c };

    // Indicator light colours
    inline const juce::Colour amber        { 0xffffb347 };
    inline const juce::Colour amberDim     { 0xff5c3a12 };
    inline const juce::Colour ledGreen     { 0xff9cff6a };
    inline const juce::Colour ledRed       { 0xffff5a3c };

    /** Enamel colour of each module's faceplate. */
    inline juce::Colour moduleEnamel (int module)
    {
        static const juce::Colour colours[] {
            juce::Colour (0xff8e3b2e), // noise: oxblood
            juce::Colour (0xffa87a26), // wobble: mustard
            juce::Colour (0xff5f6e33), // distort: olive
            juce::Colour (0xff2c676b), // digital: teal
            juce::Colour (0xff3a5880), // space: slate blue
            juce::Colour (0xff634574), // magnetic: plum
        };
        return colours[juce::jlimit (0, 5, module)];
    }

    /** Phosphor colour used by each module's display window. */
    inline juce::Colour modulePhosphor (int module)
    {
        static const juce::Colour colours[] {
            juce::Colour (0xffff8a5c),
            juce::Colour (0xffffd166),
            juce::Colour (0xffd4f08a),
            juce::Colour (0xff7ef0e6),
            juce::Colour (0xff9cc8ff),
            juce::Colour (0xffe0a8ff),
        };
        return colours[juce::jlimit (0, 5, module)];
    }
} // namespace Colours

/** Embedded typefaces (all under open licences, see resources/fonts). */
class Fonts
{
public:
    static Fonts& get();

    juce::Font label (float height) const;      // Barlow Condensed SemiBold, engraved labels
    juce::Font labelBold (float height) const;  // Barlow Condensed Bold
    juce::Font labelMedium (float height) const;
    juce::Font display (float height) const;    // Share Tech Mono, displays
    juce::Font script (float height) const;     // Yellowtail, logo

private:
    Fonts();
    juce::Typeface::Ptr medium, semiBold, bold, mono, scriptFace;
};

/** Draws text with a subtle engraved/silkscreened look. */
void drawEngravedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, const juce::Font& font,
                       juce::Colour colour, juce::Justification justification = juce::Justification::centred, bool darkShadow = true);

/** A Phillips-head screw. */
void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius, float angle = 0.6f);

} // namespace afterglow::ui
