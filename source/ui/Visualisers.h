#pragma once

#include "Theme.h"
#include "../dsp/EngineParams.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace afterglow::ui
{
/** Base class for the glass display at the top of each module.

    Every display is a small instrument drawn by one shared "beam" on tinted filter glass, in a 132 x 50 unit
    screen (origin at the top-left of the glass). The housing and each display's scale marks are cached as images
    per physical scale; only the live marks are drawn every frame. Live marks come from engine telemetry and fade
    out when the engine stops writing it, so nothing moves unless the audio engine moved it. */
class ModuleDisplay : public juce::Component
{
public:
    ModuleDisplay (juce::AudioProcessorValueTreeState& state, dsp::EngineTelemetry& telemetry, int moduleIndex, const juce::String& onParamId);

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Called by the editor's animation timer (about 30 times per second). */
    virtual void tick (double seconds);

protected:
    static constexpr float screenWidth = 132.0f;
    static constexpr float screenHeight = 50.0f;

    /** Draws into the cached static layer, in physical pixels with helpers that take screen units. */
    struct Canvas
    {
        juce::Graphics& g;
        float scale;

        void hLine (float x0, float x1, float y, juce::Colour colour) const;   // exactly one physical pixel tall
        void vLine (float x, float y0, float y1, juce::Colour colour) const;   // exactly one physical pixel wide
        void stroke (const juce::Path& path, float width, juce::Colour colour) const;   // never thinner than 1 pixel
        void dashed (const juce::Path& path, float width, float dash, juce::Colour colour) const;
        void fill (const juce::Path& path, juce::Colour colour) const;

        /** Scale ticks at the left edge: minor ones 2 units long, major ones 3 (never shorter than 2 pixels). */
        void edgeTick (float y, bool major, juce::Colour colour) const;
        /** Scale ticks rising from the bottom edge of the glass. */
        void bottomTick (float x, bool major, juce::Colour colour) const;
    };

    virtual void drawStatic (const Canvas&) {}
    /** Changes whenever the static layer must be redrawn (type, mode and so on). */
    virtual juce::String staticKey() const { return {}; }
    /** Live layer, in screen units, clipped to the glass. */
    virtual void drawLive (juce::Graphics& g) = 0;
    /** Optional instrument readout in the fixed top-right slot (only for values no control shows). */
    virtual juce::String readoutText() const { return {}; }

    float param (const char* id) const;
    bool isActive() const;

    /** Notes the write counter of the telemetry this display reads; the live layer fades if it stalls. */
    void watchData (uint32_t written) noexcept;
    /** Strength of the live layer: module on, and data still arriving. */
    float liveIntensity() const noexcept;
    bool isDataFresh() const noexcept { return staleSeconds < 0.25; }

    /** Afterglow along a time axis: from xOld (at oldRatio of the brightness) up to xFull (full brightness),
        flat after that. Equal values mean no afterglow. */
    struct Afterglow
    {
        float xOld = 0.0f, xFull = 0.0f, oldRatio = 0.4f;
    };

    /** The beam: one continuous stroke whose brightness follows dwell (bright where it moves slowly, dim on fast
        edges), with an optional halo hugging the core and an optional afterglow. */
    struct Beam
    {
        float width = 1.4f;
        float reference = 2.0f;   // segment length (screen units) that still draws at full brightness
        float floor = 0.35f;      // dimmest brightness, for the fastest segments
        bool halo = true;
        Afterglow afterglow;
    };
    void drawBeam (juce::Graphics& g, const juce::Point<float>* points, int count, juce::Colour colour, float intensity, const Beam& beam) const;
    void setAgedFill (juce::Graphics& g, juce::Colour colour, float alpha, const Afterglow& afterglow) const;
    void drawEdgePen (juce::Graphics& g, float y, float alpha) const;

    /** A time-based scroll position for the recorders: advances smoothly at the data rate between frames and
        follows the ring's write counter, so the chart neither judders nor runs ahead of the data. */
    struct ScrollHead
    {
        double head = 0.0;
        void advance (uint32_t written, double seconds, double entriesPerSecond) noexcept;
    };

    /** Smallest size, in screen units, that is still one device pixel at the current scale. */
    float pixel() const noexcept { return 1.0f / pixelScale; }

    juce::AudioProcessorValueTreeState& state;
    dsp::EngineTelemetry& telemetry;
    juce::Colour phosphor;
    float activity = 0.0f; // smoothed "on" amount
    float pixelScale = 1.0f;

private:
    void rebuildHousing (float scale);
    void drawLayer (juce::Graphics& g, const juce::Image& image, juce::Point<float> origin) const;

    std::atomic<float>* onParam = nullptr;
    juce::Image housingUnder, housingOver, staticLayer;
    float housingScale = 0.0f, staticScale = 0.0f;
    juce::String cachedStaticKey;
    bool staticValid = false;
    uint32_t lastWritten = 0;
    double staleSeconds = 10.0;
};

/** NOISE: a noise-floor strip. The real noise output scrolls by as a filled envelope on a fixed amplitude law,
    so crackle, hiss, hum and bursts are told apart by their shape and the level by the band's height. */
class NoiseDisplay final : public ModuleDisplay
{
public:
    NoiseDisplay (juce::AudioProcessorValueTreeState&, dsp::EngineTelemetry&);
    void tick (double seconds) override;

private:
    void drawStatic (const Canvas&) override;
    void drawLive (juce::Graphics& g) override;
    ScrollHead scroll;
};

/** WOBBLE: a pitch recorder. The pitch deviation the module applies, in cents, against time: the line is the wow,
    the band around it the flutter, and a second line shows the right channel in stereo. */
class WobbleDisplay final : public ModuleDisplay
{
public:
    WobbleDisplay (juce::AudioProcessorValueTreeState&, dsp::EngineTelemetry&);
    void tick (double seconds) override;

private:
    void drawStatic (const Canvas&) override;
    void drawLive (juce::Graphics& g) override;
    ScrollHead scroll; // in columns (pairs of entries)
};

/** DISTORT: a curve tracer. The selected type's transfer curve is drawn faintly; the beam traces what the shaper
    is actually doing (input after drive against output), so drive shows as how far into the bend it travels. */
class DistortDisplay final : public ModuleDisplay
{
public:
    DistortDisplay (juce::AudioProcessorValueTreeState&, dsp::EngineTelemetry&);
    void tick (double seconds) override;

private:
    void drawStatic (const Canvas&) override;
    juce::String staticKey() const override;
    void drawLive (juce::Graphics& g) override;
    int currentType() const;
    float bias = 0.0f;
    float holdPositive = 0.0f, holdNegative = 0.0f; // how far into the curve the signal reached lately
};

/** DIGITAL: a converter sweep. A test chirp from 20 Hz to 20 kHz runs through the module's actual rate and bits,
    so the steps, the aliasing past Nyquist (marked) and the coarse levels show where the damage starts. */
class DigitalDisplay final : public ModuleDisplay
{
public:
    DigitalDisplay (juce::AudioProcessorValueTreeState&, dsp::EngineTelemetry&);
    void tick (double seconds) override;

private:
    void drawStatic (const Canvas&) override;
    void drawLive (juce::Graphics& g) override;
    juce::String readoutText() const override;
    void rebuildSweep();

    float rate = 48000.0f, bits = 24.0f, jitter = 0.0f;
    juce::String sweepKey;
    std::vector<juce::Point<float>> sweep;
    float nyquistX = -1.0f;
    bool reduced = false;
};

/** SPACE: a decay recorder. A faint guide shows the expected decay (pre-delay gap, build-up, then a straight fall
    of 60 dB over the decay time, plus the faster treble decay); after each note the beam writes the measured wet
    level onto it. The Resonator type shows its twelve chromatic combs instead. */
class SpaceDisplay final : public ModuleDisplay
{
public:
    SpaceDisplay (juce::AudioProcessorValueTreeState&, dsp::EngineTelemetry&);
    void tick (double seconds) override;

private:
    void drawStatic (const Canvas&) override;
    juce::String staticKey() const override;
    void drawLive (juce::Graphics& g) override;
    juce::String readoutText() const override;
    bool isResonator() const;
    float decayAt (float hz) const;

    float windowSeconds() const;

    float decaySeconds = 1.0f, preDelayMs = 0.0f;
    std::array<float, 12> noteDb {};
};

/** MAGNETIC: a two-tone tape level recorder. A 1 kHz and a 10 kHz tone recorded on the worn tape are plotted in dB
    against time: wear wanders, flutter scallops at the rate, dropouts punch notches (deeper on the treble), and the
    ribbon between the two lines is the treble being lost. Stereo shows two lanes. */
class MagneticDisplay final : public ModuleDisplay
{
public:
    MagneticDisplay (juce::AudioProcessorValueTreeState&, dsp::EngineTelemetry&);
    void tick (double seconds) override;

private:
    void drawStatic (const Canvas&) override;
    juce::String staticKey() const override;
    void drawLive (juce::Graphics& g) override;
    bool isStereo() const;
    ScrollHead scroll;
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
