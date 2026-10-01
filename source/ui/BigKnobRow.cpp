#include "BigKnobRow.h"
#include "Textures.h"
#include "../Parameters.h"

namespace afterglow::ui
{
BigKnobRow::BigKnobRow (APVTS& s) : state (s), magnitude (s.getRawParameterValue (ParamIDs::magnitude))
{
    struct Spec { const char* amount; const char* on; const char* title; const char* tip; };
    static const Spec specs[] {
        { ParamIDs::noiseAmount, ParamIDs::noiseOn, "NOISE", "Noise level." },
        { ParamIDs::wobbleAmount, ParamIDs::wobbleOn, "WOBBLE", "Wow and flutter depth (pitch)." },
        { ParamIDs::distortAmount, ParamIDs::distortOn, "DISTORT", "Distortion drive. Output level is matched automatically." },
        { ParamIDs::digitalAmount, ParamIDs::digitalOn, "DIGITAL", "Sample-rate and bit-depth reduction amount." },
        { ParamIDs::spaceAmount, ParamIDs::spaceOn, "SPACE", "Reverb dry/wet balance." },
        { ParamIDs::magneticAmount, ParamIDs::magneticOn, "MAGNETIC", "Wear, flutter and dropout depth." },
    };

    for (size_t i = 0; i < cells.size(); ++i)
    {
        auto& c = cells[i];
        const auto& spec = specs[i];
        c.title = spec.title;
        c.onParamId = spec.on;
        c.amount = s.getRawParameterValue (spec.amount);
        c.knob = std::make_unique<LabelledKnob> (s, spec.amount, juce::String(), StyleId::bigKnob, Colours::ink,
                                                 juce::String (spec.tip) + " At 0 % the module is bypassed.");
        c.power = std::make_unique<LedButton> (s, spec.on, juce::String(), Colours::ledGreen, LedButton::Look::power,
                                               juce::String ("Switches the ") + juce::String (spec.title).toLowerCase() + " module on or off.");

        // Grabbing the big knob of a switched-off module switches it on, like the hardware it imitates.
        auto* knobSlider = &c.knob->getSlider();
        knobSlider->onDragStart = [this, i]
        {
            if (auto* p = state.getParameter (cells[i].onParamId); p != nullptr && p->getValue() < 0.5f)
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (1.0f);
                p->endChangeGesture();
            }
        };

        addAndMakeVisible (*c.knob);
        addAndMakeVisible (*c.power);
    }

    setOpaque (true);
}

void BigKnobRow::resized()
{
    const auto cellWidth = static_cast<float> (getWidth() - 20) / 6.0f;

    for (size_t i = 0; i < cells.size(); ++i)
    {
        const auto x = 10.0f + cellWidth * static_cast<float> (i);
        const auto cell = juce::Rectangle<float> (x, 0.0f, cellWidth, static_cast<float> (getHeight())).toNearestInt();
        cells[i].knob->setBounds (juce::Rectangle<int> (118, 118).withCentre ({ cell.getCentreX(), cell.getY() + 66 }));
        cells[i].power->setBounds (cell.getX() + 10, cell.getY() + 12, 30, 34);
    }
}

void BigKnobRow::refresh()
{
    const auto mag = magnitude != nullptr ? magnitude->load (std::memory_order_relaxed) * 0.01f : 1.0f;

    for (auto& c : cells)
    {
        const auto value = c.amount->load (std::memory_order_relaxed) * 0.01f;
        const auto effective = value * mag;
        if (std::abs (effective - c.shownEffective) > 0.001f)
        {
            c.shownEffective = effective;
            c.knob->getSlider().getProperties().set ("effective", effective);
            c.knob->getSlider().repaint();
        }
    }
}

void BigKnobRow::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const auto scale = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());

    // Cream enamel panel with a fine texture.
    juce::ColourGradient cream (Colours::cream.brighter (0.08f), area.getX(), area.getY(), Colours::creamShadow, area.getX(), area.getBottom(), false);
    g.setGradientFill (cream);
    g.fillRect (area);
    const auto tile = juce::roundToInt (128.0f * scale);
    Textures::fillTiled (g, Textures::get (Textures::Kind::creamTile, tile, tile, 77u), area, scale, 0.8f);

    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawHorizontalLine (0, area.getX(), area.getRight());
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawHorizontalLine (1, area.getX(), area.getRight());
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawHorizontalLine (juce::roundToInt (area.getBottom()) - 1, area.getX(), area.getRight());

    const auto cellWidth = (area.getWidth() - 20.0f) / 6.0f;
    for (size_t i = 0; i < cells.size(); ++i)
    {
        const auto x = 10.0f + cellWidth * static_cast<float> (i);

        // Engraved separators between modules.
        if (i > 0)
        {
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.drawVerticalLine (juce::roundToInt (x), area.getY() + 12.0f, area.getBottom() - 12.0f);
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.drawVerticalLine (juce::roundToInt (x) + 1, area.getY() + 12.0f, area.getBottom() - 12.0f);
        }

        const auto label = juce::Rectangle<float> (x, area.getBottom() - 26.0f, cellWidth, 18.0f);
        drawEngravedText (g, cells[i].title, label, Fonts::get().labelBold (16.0f), Colours::ink.withAlpha (0.85f), juce::Justification::centred, false);
    }
}

} // namespace afterglow::ui
