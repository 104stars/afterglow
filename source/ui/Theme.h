#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace afterglow::ui
{
/** Fixed design size of the interface; everything is laid out in these units and scaled as a whole. */
inline constexpr int designWidth = 1120;
inline constexpr int designHeight = 720;

/** Shared layout grid. The module bay and the big-knob row use the same columns so every big knob sits
    exactly under its module. */
namespace Layout
{
    inline constexpr int cheekWidth = 26;
    inline constexpr int innerWidth = designWidth - 2 * cheekWidth; // 1068
    inline constexpr int headerHeight = 84;
    inline constexpr int bayHeight = 346;
    inline constexpr int bigKnobHeight = 154;
    inline constexpr int bayMargin = 10;   // from the inner edge to the first module
    inline constexpr int moduleGap = 8;
    inline constexpr int moduleWidth = (innerWidth - 2 * bayMargin - 5 * moduleGap) / 6; // 168
    inline constexpr int modulePitch = moduleWidth + moduleGap;                          // 176

    /** Centre x of module column i, relative to the inner area (between the walnut cheeks). */
    inline constexpr int columnCentre (int i) { return bayMargin + moduleWidth / 2 + i * modulePitch; }

    // Module panel grid (panel coordinates), identical for all six modules.
    inline constexpr int panelMargin = 14;
    inline constexpr int displayTop = 14;
    inline constexpr int displayHeight = 58;
    inline constexpr int headerRowTop = 84;     // selector or balance caption
    inline constexpr int rowATop = 124;         // first knob row (knob + label = 70)
    inline constexpr int rowBTop = 198;         // second knob row
    inline constexpr int knobWidth = 60;
    inline constexpr int knobHeight = 70;
    inline constexpr int buttonRowTop = 272;
    inline constexpr int keycapHeight = 22;
    inline constexpr int keycapWidth = 54;
    inline constexpr int focusTop = 124;
    inline constexpr int focusHeight = 120;
} // namespace Layout

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
            juce::Colour (0xff96691d), // wobble: mustard (darkened slightly so cream print keeps contrast)
            juce::Colour (0xff5f6e33), // distort: olive
            juce::Colour (0xff2c676b), // digital: teal
            juce::Colour (0xff3a5880), // space: slate blue
            juce::Colour (0xff634574), // magnetic: plum
        };
        return colours[juce::jlimit (0, 5, module)];
    }

    /** Emitter colour of each module's display: one warm phosphor seen through a per-module colour filter,
        kept about a quarter less saturated than the enamel so the screens read as instruments, not neon. */
    inline juce::Colour modulePhosphor (int module)
    {
        static const juce::Colour colours[] {
            juce::Colour (0xfff49e7c), // noise
            juce::Colour (0xfff1c878), // wobble
            juce::Colour (0xffd2d98c), // distort
            juce::Colour (0xff92d4c4), // digital
            juce::Colour (0xffa4c2e8), // space
            juce::Colour (0xffd2aee0), // magnetic
        };
        return colours[juce::jlimit (0, 5, module)];
    }
} // namespace Colours

/** Embedded typefaces (all under open licences, see resources/fonts). */
class Fonts
{
public:
    /** The fonts of the open editors (see UiResources). Outside an editor's lifetime this returns plain fallback
        fonts, so nothing graphical is ever created or kept alive by a static object. */
    static const Fonts& get();

    /** Loads the embedded typefaces, or creates the empty fallback set (default system font). */
    explicit Fonts (bool loadEmbedded);

    juce::Font label (float height) const;      // Barlow Condensed SemiBold, engraved labels
    juce::Font labelBold (float height) const;  // Barlow Condensed Bold
    juce::Font labelMedium (float height) const;
    juce::Font display (float height) const;    // Share Tech Mono, displays
    juce::Font script (float height) const;     // Yellowtail, logo

private:
    juce::Typeface::Ptr medium, semiBold, bold, mono, scriptFace;
};

/** Fonts and image caches shared by every open editor of this plugin.

    Each editor holds a juce::SharedResourcePointer to this, so it is created with the first editor and destroyed
    with the last one. That keeps typefaces and images (which on Windows hold DirectWrite and Direct2D objects, and
    on macOS CoreText and CoreGraphics ones) strictly inside the time JUCE and the platform graphics are running.
    They are never destroyed at library unload, when the host removes the plugin or quits, which is where static
    caches crash. It is only used on the message thread. */
class UiResources
{
public:
    UiResources();
    ~UiResources();

    /** The live instance, or nullptr when no editor is open. */
    static UiResources* current() noexcept;

    const Fonts& getFonts() const noexcept { return fonts; }

    /** A small image cache for generated artwork (textures, knob caps). */
    juce::Image findImage (const juce::String& key) const;
    void storeImage (const juce::String& key, const juce::Image& image);

private:
    Fonts fonts { true };
    std::map<juce::String, juce::Image> images;

    JUCE_DECLARE_NON_COPYABLE (UiResources)
};

/** Draws text with a subtle engraved/silkscreened look. */
void drawEngravedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, const juce::Font& font,
                       juce::Colour colour, juce::Justification justification = juce::Justification::centred, bool darkShadow = true);

/** A Phillips-head screw. */
void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius, float angle = 0.6f);

} // namespace afterglow::ui
