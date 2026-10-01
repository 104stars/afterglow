#include "MasterStrip.h"
#include "Textures.h"
#include "../Parameters.h"

namespace afterglow::ui
{
MasterStrip::MasterStrip (APVTS& s, dsp::EngineTelemetry& t)
    : telemetry (t),
      inGain (s, ParamIDs::inGain, "GAIN", StyleId::bipolarKnob, Colours::silkscreen, "Input level. Drive it harder to push the Distort and Digital modules."),
      tone (s, ParamIDs::tone, "TONE", StyleId::bipolarKnob, Colours::silkscreen, "Tilt: clockwise is brighter, counter-clockwise is darker. Mid: boost or cut the midrange."),
      width (s, ParamIDs::width, "WIDTH", StyleId::smallKnob, Colours::silkscreen, "Stereo width: 0 % mono, 100 % original, 200 % extra wide."),
      outGain (s, ParamIDs::outGain, "GAIN", StyleId::bipolarKnob, Colours::silkscreen, "Output level, for level-matched comparisons."),
      mix (s, ParamIDs::mix, "MIX", StyleId::smallKnob, Colours::silkscreen, "Global dry/wet. The dry signal is latency-compensated."),
      eqOn (s, ParamIDs::eqOn, {}, Colours::ledGreen, LedButton::Look::power, "Switches the master EQ (cut filters and tone) on or off."),
      lowHard (s, ParamIDs::lowCutHard, "HARD", Colours::amber, LedButton::Look::keycap, "Low cut slope: soft (12 dB/oct) or hard (24 dB/oct)."),
      highHard (s, ParamIDs::highCutHard, "HARD", Colours::amber, LedButton::Look::keycap, "High cut slope: soft (12 dB/oct) or hard (24 dB/oct)."),
      toneMode (s, ParamIDs::toneMode, "MID", Colours::amber, LedButton::Look::keycap, "Tone mode: off = Tilt, on = Mid (smiley or frowny curve)."),
      limiter (s, ParamIDs::limiter, "LIMIT", Colours::ledRed, LedButton::Look::keycap, "Soft safety limiter on the output, transparent below -3 dBFS."),
      cut (s, ParamIDs::lowCut, ParamIDs::highCut, false, Colours::amber,
           "Cut filters: drag the left handle for the low cut and the right handle for the high cut. The far ends mean off.")
{
    cut.setOffAtExtremes (true);
    toneMode.setButtonText ("MID");
    toneMode.setOffText ("TILT");

    for (auto* c : std::initializer_list<juce::Component*> { &inMeter, &outMeter, &inGain, &tone, &width, &outGain, &mix,
                                                             &eqOn, &lowHard, &highHard, &toneMode, &limiter, &cut })
        addAndMakeVisible (c);

    setOpaque (true);
    setBufferedToImage (true);
}

void MasterStrip::resized()
{
    // Everything in the strip is centred on one line (y = 62): knob dials, meters, cut slider and keycaps.
    constexpr int centreY = 62;
    constexpr int knobTop = centreY - 28;                 // dial centre of a 70 px knob box
    const auto keycap = [] (int x, int w) { return juce::Rectangle<int> (x, centreY - 11, w, 22); };

    inSection = { 8, 0, 190, getHeight() };
    eqSection = { 204, 0, 520, getHeight() };
    outSection = { 730, 0, 250, getHeight() };
    globalSection = { 986, 0, 74, getHeight() };

    inMeter.setBounds (16, centreY - 32, 112, 64);
    inGain.setBounds (132, knobTop, 60, 70);

    eqOn.setBounds (214, centreY - 17, 30, 34);
    lowHard.setBounds (keycap (254, 44));
    cut.setBounds (304, centreY - 17, 262, 34);
    highHard.setBounds (keycap (572, 44));
    tone.setBounds (622, knobTop, 60, 70);
    toneMode.setBounds (keycap (682, 40));

    width.setBounds (736, knobTop, 60, 70);
    outGain.setBounds (798, knobTop, 60, 70);
    outMeter.setBounds (864, centreY - 32, 112, 64);

    mix.setBounds (globalSection.getCentreX() - 30, knobTop, 60, 70);
    limiter.setBounds (globalSection.getCentreX() - 28, knobTop + 72, 56, Layout::keycapHeight - 2); // 10 px above the strip edge
}

void MasterStrip::refresh (double seconds)
{
    auto read = [] (std::atomic<float>& a) { return a.load (std::memory_order_relaxed); };
    auto takePeak = [] (std::atomic<float>& a) { return a.exchange (0.0f, std::memory_order_relaxed); };

    const auto inRms = std::max (read (telemetry.inputRms[0]), read (telemetry.inputRms[1]));
    const auto outRms = std::max (read (telemetry.outputRms[0]), read (telemetry.outputRms[1]));
    const auto inPeak = std::max (takePeak (telemetry.inputPeak[0]), takePeak (telemetry.inputPeak[1]));
    const auto outPeak = std::max (takePeak (telemetry.outputPeak[0]), takePeak (telemetry.outputPeak[1]));
    inMeter.setLevels (inRms, inPeak, seconds);
    outMeter.setLevels (outRms, outPeak, seconds);
}

void MasterStrip::drawSectionCaption (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area) const
{
    const auto caption = area.toFloat().withHeight (22.0f).withTrimmedTop (6.0f);
    drawEngravedText (g, text, caption, Fonts::get().labelBold (13.0f), Colours::silkscreen.withAlpha (0.85f), juce::Justification::centred, true);

    // Thin rule either side of the caption.
    const auto textWidth = juce::GlyphArrangement::getStringWidth (Fonts::get().labelBold (13.0f), text) + 16.0f;
    g.setColour (Colours::silkscreen.withAlpha (0.2f));
    g.drawHorizontalLine (juce::roundToInt (caption.getCentreY()), caption.getX() + 8.0f, caption.getCentreX() - textWidth * 0.5f);
    g.drawHorizontalLine (juce::roundToInt (caption.getCentreY()), caption.getCentreX() + textWidth * 0.5f, caption.getRight() - 8.0f);
}

void MasterStrip::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const auto scale = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());

    // Black tolex covering.
    const auto tolex = Textures::get (Textures::Kind::tolex, juce::roundToInt (area.getWidth() * scale), juce::roundToInt (area.getHeight() * scale), 5u);
    Textures::drawFitted (g, tolex, area);
    juce::ColourGradient shade (juce::Colours::black.withAlpha (0.45f), area.getX(), area.getY(), juce::Colours::transparentBlack, area.getX(), area.getY() + 18.0f, false);
    g.setGradientFill (shade);
    g.fillRect (area);

    drawSectionCaption (g, "INPUT", inSection);
    drawSectionCaption (g, "EQ", eqSection);
    drawSectionCaption (g, "OUTPUT", outSection);
    drawSectionCaption (g, "GLOBAL", globalSection);

    for (auto x : { eqSection.getX() - 3, outSection.getX() - 3, globalSection.getX() - 3 })
    {
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawVerticalLine (x, 10.0f, area.getBottom() - 10.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawVerticalLine (x + 1, 10.0f, area.getBottom() - 10.0f);
    }

    // Captions: one baseline above the keycaps (keycap top - 13), micro size, same print colour.
    const auto micro = Fonts::get().labelMedium (10.5f);
    const auto print = Colours::silkscreen.withAlpha (0.75f);
    auto captionAbove = [&] (juce::Component& c, const juce::String& text, int captionWidth)
    {
        const auto b = c.getBounds();
        drawEngravedText (g, text, juce::Rectangle<float> (static_cast<float> (b.getCentreX() - captionWidth / 2), static_cast<float> (b.getY() - 13), static_cast<float> (captionWidth), 12.0f),
                          micro, print, juce::Justification::centred, true);
    };
    captionAbove (lowHard, "LOW CUT", 60);
    captionAbove (highHard, "HIGH CUT", 60);
    captionAbove (toneMode, "MODE", 50);
    drawEngravedText (g, "ON", eqOn.getBounds().toFloat().withY (static_cast<float> (eqOn.getBottom()) + 2.0f).withHeight (12.0f), micro, print, juce::Justification::centredTop, true);

    // Frequency scale under the cut slider (shared 10 Hz - 22 kHz log axis).
    const auto track = cut.getBounds().toFloat().reduced (8.0f, 0.0f);
    const auto axisPos = [&] (float hz) { return track.getX() + track.getWidth() * std::log (hz / 10.0f) / std::log (2200.0f); };
    for (auto [hz, text] : { std::pair { 20.0f, "20" }, std::pair { 100.0f, "100" }, std::pair { 1000.0f, "1k" }, std::pair { 10000.0f, "10k" } })
    {
        const auto x = axisPos (hz);
        g.setColour (Colours::silkscreen.withAlpha (0.4f));
        g.drawVerticalLine (juce::roundToInt (x), track.getBottom() + 1.0f, track.getBottom() + 5.0f);
        drawEngravedText (g, text, juce::Rectangle<float> (30.0f, 12.0f).withCentre ({ x, track.getBottom() + 12.0f }), micro, Colours::silkscreen.withAlpha (0.65f), juce::Justification::centred, true);
    }

}

} // namespace afterglow::ui
