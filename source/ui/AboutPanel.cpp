#include "AboutPanel.h"
#include "../Parameters.h"

namespace afterglow::ui
{
namespace
{
    constexpr float scales[] { 0.75f, 0.9f, 1.0f, 1.25f, 1.5f, 2.0f };
}

AboutPanel::AboutPanel (APVTS& state)
{
    quality.addItemList (qualityNames(), 1);
    quality.setTooltip ("Oversampling used by the Distort module. Higher settings reduce aliasing and add a little latency and CPU load.");
    qualityAttachment = std::make_unique<APVTS::ComboBoxAttachment> (state, ParamIDs::quality, quality);
    addAndMakeVisible (quality);

    for (auto s : scales)
    {
        auto* b = scaleButtons.add (new juce::TextButton (juce::String (juce::roundToInt (s * 100.0f)) + "%"));
        b->onClick = [this, s] { if (onScaleChosen) onScaleChosen (s); };
        addAndMakeVisible (b);
    }

    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);
    setWantsKeyboardFocus (true);
}

void AboutPanel::resized()
{
    const auto right = getWidth() - 360;
    quality.setBounds (right, 66, 320, 28);

    auto x = right;
    for (auto* b : scaleButtons)
    {
        b->setBounds (x, 146, 48, 26);
        x += 54;
    }

    closeButton.setBounds (getWidth() - 140, getHeight() - 50, 110, 32);
}

void AboutPanel::mouseUp (const juce::MouseEvent&) {}

bool AboutPanel::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onClose)
            onClose();
        return true;
    }
    return false;
}

void AboutPanel::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (area.translated (0.0f, 3.0f), 8.0f);
    juce::ColourGradient bg (juce::Colour (0xff262422), area.getX(), area.getY(), juce::Colour (0xff141312), area.getX(), area.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (area, 8.0f);
    g.setColour (Colours::amber.withAlpha (0.25f));
    g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

    // Left column: identity and help.
    auto left = juce::Rectangle<float> (28.0f, 18.0f, area.getWidth() - 440.0f, area.getHeight() - 36.0f);

    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (Fonts::get().script (46.0f), "afterglow", left.getX(), left.getY() + 40.0f);
    juce::Path logo;
    glyphs.createPath (logo);
    const auto b = logo.getBounds();
    juce::ColourGradient sunset (juce::Colour (0xffffe08a), b.getX(), b.getY(), juce::Colour (0xffd8325e), b.getX(), b.getBottom(), false);
    sunset.addColour (0.5, juce::Colour (0xffff8c3a));
    g.setGradientFill (sunset);
    g.fillPath (logo);

    g.setFont (Fonts::get().display (13.0f));
    g.setColour (Colours::amber.withAlpha (0.8f));
    g.drawText ("VERSION " + juce::String (AFTERGLOW_VERSION_STRING) + "   " + juce::SystemStats::getOperatingSystemName().toUpperCase(),
                juce::Rectangle<float> (left.getX() + 230.0f, left.getY() + 26.0f, 400.0f, 18.0f), juce::Justification::centredLeft, false);

    left.removeFromTop (62.0f);
    const juce::String help =
        "Six vintage colour modules, from left to right in the signal path: Noise, Wobble, Distort, Digital, Space and Magnetic. "
        "Each big knob sets how much of a module you hear (0 % bypasses it), and Magnitude scales all of them together with the "
        "master section. Flux adds organic, never-repeating drift to each module.\n\n"
        "Tips:  drag knobs up or down, hold Shift for fine control, double-click to reset, use the mouse wheel for small steps. "
        "Drag the middle of a Focus band to move it. Click a switched-off module's cover (or grab its big knob) to switch it on. "
        "Ctrl+Z / Ctrl+Shift+Z undo and redo.\n\n"
        "User presets are saved to Documents/Afterglow/Presets and can be copied between computers.\n\n"
        "Fonts: Barlow Condensed and Share Tech Mono (SIL Open Font License), Yellowtail (Apache License 2.0). "
        "Built with JUCE.";

    juce::AttributedString text;
    text.setLineSpacing (3.0f);
    text.append (help, Fonts::get().labelMedium (16.0f), Colours::silkscreen.withAlpha (0.85f));
    juce::TextLayout layout;
    layout.createLayout (text, left.getWidth());
    layout.draw (g, left);

    // Right column: settings.
    const auto right = static_cast<float> (getWidth() - 360);
    g.setFont (Fonts::get().labelBold (14.0f));
    g.setColour (Colours::amber.withAlpha (0.8f));
    g.drawText ("SETTINGS", juce::Rectangle<float> (right, 18.0f, 320.0f, 18.0f), juce::Justification::centredLeft, false);
    g.setFont (Fonts::get().label (13.0f));
    g.setColour (Colours::silkscreen.withAlpha (0.75f));
    g.drawText ("OVERSAMPLING QUALITY (DISTORT)", juce::Rectangle<float> (right, 46.0f, 320.0f, 16.0f), juce::Justification::centredLeft, false);
    g.drawText ("INTERFACE SIZE", juce::Rectangle<float> (right, 126.0f, 320.0f, 16.0f), juce::Justification::centredLeft, false);

    g.setFont (Fonts::get().labelMedium (14.0f));
    g.setColour (Colours::silkscreen.withAlpha (0.5f));
    g.drawFittedText ("The window can also be resized by dragging its lower-right corner. Quality is a global setting and is not stored in presets.",
                      juce::Rectangle<int> (static_cast<int> (right), 184, 320, 60), juce::Justification::topLeft, 3);
}

} // namespace afterglow::ui
