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
        c.power = std::make_unique<LedButton> (s, spec.on, spec.title, Colours::ledGreen, LedButton::Look::nameplate,
                                               juce::String ("Click the name to switch the ") + juce::String (spec.title).toLowerCase() + " module on or off.");

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

juce::Rectangle<int> BigKnobRow::cellBounds (size_t index) const
{
    // Cells follow the module columns above (Layout::columnCentre), so each knob sits under its module.
    const auto centre = Layout::columnCentre (static_cast<int> (index));
    return { centre - Layout::modulePitch / 2, 0, Layout::modulePitch, getHeight() };
}

void BigKnobRow::resized()
{
    for (size_t i = 0; i < cells.size(); ++i)
    {
        const auto cell = cellBounds (i);
        cells[i].knob->setBounds (juce::Rectangle<int> (116, 116).withCentre ({ cell.getCentreX(), 62 }));

        // The module name below the knob is the power switch, clear of the printed scale.
        const auto plateWidth = cells[i].power->getNameplateWidth();
        cells[i].power->setBounds (juce::Rectangle<int> (plateWidth, 26).withCentre ({ cell.getCentreX(), getHeight() - 22 }));
        cells[i].power->toFront (false);
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

    for (size_t i = 1; i < cells.size(); ++i)
    {
        // Engraved separators, exactly under the gaps between the modules.
        const auto x = cellBounds (i).getX();
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.drawVerticalLine (x, area.getY() + 12.0f, area.getBottom() - 12.0f);
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawVerticalLine (x + 1, area.getY() + 12.0f, area.getBottom() - 12.0f);
    }
}

} // namespace afterglow::ui
