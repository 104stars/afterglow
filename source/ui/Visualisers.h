#pragma once

#include "Theme.h"
#include "../dsp/EngineParams.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace afterglow::ui
{
/** Base class for the animated glass display at the top of each module. */
class ModuleDisplay : public juce::Component
{
public:
    ModuleDisplay (juce::AudioProcessorValueTreeState& state, dsp::EngineTelemetry& telemetry, int moduleIndex, const juce::String& onParamId);

    void paint (juce::Graphics& g) override;

    /** Called by the editor's animation timer (about 30 times per second). */
    virtual void tick (double seconds);

protected:
    virtual void drawContent (juce::Graphics& g, juce::Rectangle<float> screen) = 0;

    /** Small numeric readout shown in a chip on the glass (empty for none). */
    virtual juce::String readoutText() const { return {}; }
    virtual juce::Colour readoutColour() const { return phosphor; }
    virtual bool readoutAtTop() const { return false; }

    float param (const char* id) const;
    bool isActive() const;

    /** Opacity of a screen's static layer (graticule, artwork): 30 % when off, full when on. */
    float staticAlpha() const noexcept;

    juce::AudioProcessorValueTreeState& state;
    dsp::EngineTelemetry& telemetry;
    juce::Colour phosphor;
    double time = 0.0;
    float activity = 0.0f; // smoothed "on" amount used for fading the content in and out

private:
    void drawReadout (juce::Graphics& g, juce::Rectangle<float> screen);

    std::atomic<float>* onParam = nullptr;
};

class NoiseDisplay final : public ModuleDisplay
{
public:
    NoiseDisplay (juce::AudioProcessorValueTreeState&, dsp::EngineTelemetry&);
    void tick (double seconds) override;

private:
    void drawContent (juce::Graphics& g, juce::Rectangle<float> screen) override;
    juce::String readoutText() const override;
    std::array<float, 160> trace {};
    float gain = 1.0f, levelDb = -90.0f;
};

class WobbleDisplay final : public ModuleDisplay
{
public:
    WobbleDisplay (juce::AudioProcessorValueTreeState&, dsp::EngineTelemetry&);
    void tick (double seconds) override;

private:
    void drawContent (juce::Graphics& g, juce::Rectangle<float> screen) override;
    juce::String readoutText() const override;
    double wowPhase = 0.0, flutterPhase = 0.0;
};

class DistortDisplay final : public ModuleDisplay
{
public:
    DistortDisplay (juce::AudioProcessorValueTreeState&, dsp::EngineTelemetry&);
    void tick (double seconds) override;

private:
    void drawContent (juce::Graphics& g, juce::Rectangle<float> screen) override;
    juce::String readoutText() const override;
    void drawTube (juce::Graphics& g, juce::Rectangle<float> area, float glow, float flicker, float alpha) const;
    float glow = 0.0f, flicker = 0.0f;
    juce::Random random;
};

class DigitalDisplay final : public ModuleDisplay
{
public:
    DigitalDisplay (juce::AudioProcessorValueTreeState&, dsp::EngineTelemetry&);
    void tick (double seconds) override;

private:
    void drawContent (juce::Graphics& g, juce::Rectangle<float> screen) override;
    juce::String readoutText() const override;
    float rate = 44100.0f, bits = 24.0f;
};

class SpaceDisplay final : public ModuleDisplay
{
public:
    SpaceDisplay (juce::AudioProcessorValueTreeState&, dsp::EngineTelemetry&);
    void tick (double seconds) override;

private:
    void drawContent (juce::Graphics& g, juce::Rectangle<float> screen) override;
    juce::String readoutText() const override;
    float decaySeconds() const;
    float energy = 0.0f;
};

class MagneticDisplay final : public ModuleDisplay
{
public:
    MagneticDisplay (juce::AudioProcessorValueTreeState&, dsp::EngineTelemetry&);
    void tick (double seconds) override;

private:
    void drawContent (juce::Graphics& g, juce::Rectangle<float> screen) override;
    juce::String readoutText() const override;
    juce::Colour readoutColour() const override;
    bool readoutAtTop() const override { return true; } // sits between the reels, like a tape counter
    void drawReel (juce::Graphics& g, juce::Point<float> centre, float flange, float pack, float angle, float alpha, float live) const;
    double leftAngle = 0.0, rightAngle = 0.0, transport = 0.15;
    float gain = 1.0f, dropout = 0.0f;
};

//======================================================================================================================
/** Classic moving-coil VU meter with a warm backlight and a peak LED. 0 VU = -18 dBFS RMS. */
class VuMeter final : public juce::Component
{
public:
    VuMeter (const juce::String& caption);
    void setLevels (float rms, float peak, double seconds);
    void paint (juce::Graphics& g) override;

private:
    juce::String caption;
    float needle = 0.0f, velocity = 0.0f; // needle position in VU (-20..+3)
    float peakHold = 0.0f;
    juce::Image face;
    float faceScale = 0.0f;
};

} // namespace afterglow::ui
