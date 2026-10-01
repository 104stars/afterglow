#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace afterglow::dsp
{
inline constexpr float pi = 3.14159265358979323846f;
inline constexpr float twoPi = 2.0f * pi;

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

template <typename T>
inline T lerp (T a, T b, T t) noexcept { return a + (b - a) * t; }

inline float clamp01 (float x) noexcept { return std::clamp (x, 0.0f, 1.0f); }

/** Interpolates between two frequencies on a logarithmic scale. */
inline float logLerp (float a, float b, float t) noexcept
{
    return std::exp (lerp (std::log (a), std::log (b), t));
}

/** Smooth 0..1 ramp used to fade a module in as its amount leaves zero, so that 0 % is a true bypass. */
inline float engageCurve (float amount01, float knee = 0.04f) noexcept
{
    const auto t = clamp01 (amount01 / knee);
    return t * t * (3.0f - 2.0f * t);
}

//======================================================================================================================
/** Small, fast, deterministic random generator (xorshift32). Safe to use on the audio thread. */
class FastRandom
{
public:
    explicit FastRandom (uint32_t seed = 0x9e3779b9u) noexcept { setSeed (seed); }

    void setSeed (uint32_t seed) noexcept { state = seed != 0 ? seed : 0x12345678u; }

    uint32_t nextUInt() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /** Uniform in [0, 1). */
    float nextFloat() noexcept { return static_cast<float> (nextUInt() >> 8) * (1.0f / 16777216.0f); }

    /** Uniform in [-1, 1). */
    float nextBipolar() noexcept { return nextFloat() * 2.0f - 1.0f; }

    /** Approximately Gaussian (sum of four uniforms), unit variance. */
    float nextGaussian() noexcept
    {
        const auto s = nextFloat() + nextFloat() + nextFloat() + nextFloat();
        return (s - 2.0f) * 1.7320508f;
    }

private:
    uint32_t state = 0x12345678u;
};

//======================================================================================================================
/** One-pole exponential parameter smoother. */
class Smoother
{
public:
    void prepare (double sampleRate, float timeSeconds) noexcept
    {
        coeff = timeSeconds <= 0.0f ? 0.0f : std::exp (-1.0f / (static_cast<float> (sampleRate) * timeSeconds));
    }

    void setTarget (float t) noexcept { target = t; }
    void snapTo (float v) noexcept { current = target = v; }
    float getTarget() const noexcept { return target; }
    float getCurrent() const noexcept { return current; }

    float next() noexcept
    {
        current = target + coeff * (current - target);
        if (std::abs (current - target) < 1.0e-6f)
            current = target; // land exactly, so "is it zero?" bypass checks work and no denormals linger
        return current;
    }

    /** Advances the smoother by n samples at once (for control-rate use). */
    float skip (int n) noexcept
    {
        current = target + std::pow (coeff, static_cast<float> (n)) * (current - target);
        if (std::abs (current - target) < 1.0e-6f)
            current = target;
        return current;
    }

    bool isSettled (float tolerance = 1.0e-5f) const noexcept { return std::abs (current - target) < tolerance; }

private:
    float coeff = 0.0f, current = 0.0f, target = 0.0f;
};

//======================================================================================================================
/** Topology-preserving one-pole filter (Zavalishin). Provides low-pass and high-pass outputs. */
class OnePole
{
public:
    void setCutoff (float hz, double sampleRate) noexcept
    {
        const auto wc = std::tan (pi * std::min (hz, 0.49f * static_cast<float> (sampleRate)) / static_cast<float> (sampleRate));
        g = wc / (1.0f + wc);
    }

    void reset (float value = 0.0f) noexcept { s = value; }

    float processLP (float x) noexcept
    {
        const auto v = (x - s) * g;
        const auto lp = v + s;
        s = lp + v;
        return lp;
    }

    float processHP (float x) noexcept { return x - processLP (x); }

private:
    float g = 0.0f, s = 0.0f;
};

//======================================================================================================================
/** Linear trapezoidal state-variable filter (Simper). Handles LP/BP/HP plus shelves and bells. */
class Svf
{
public:
    enum class Type { lowPass, bandPass, highPass, bell, lowShelf, highShelf, allPass, notch };

    void setup (Type type, float hz, float q, float gainDb, double sampleRate) noexcept
    {
        const auto fs = static_cast<float> (sampleRate);
        hz = std::clamp (hz, 5.0f, 0.49f * fs);
        auto w = std::tan (pi * hz / fs);
        const auto A = std::pow (10.0f, gainDb / 40.0f);
        auto k = 1.0f / q;

        switch (type)
        {
            case Type::lowPass:   m0 = 0.0f; m1 = 0.0f; m2 = 1.0f; break;
            case Type::bandPass:  m0 = 0.0f; m1 = 1.0f; m2 = 0.0f; break;
            case Type::highPass:  m0 = 1.0f; m1 = -k;   m2 = -1.0f; break;
            case Type::allPass:   m0 = 1.0f; m1 = -2.0f * k; m2 = 0.0f; break;
            case Type::notch:     m0 = 1.0f; m1 = -k;   m2 = 0.0f; break;
            case Type::bell:
                k = 1.0f / (q * A);
                m0 = 1.0f; m1 = k * (A * A - 1.0f); m2 = 0.0f;
                break;
            case Type::lowShelf:
                w /= std::sqrt (A);
                m0 = 1.0f; m1 = k * (A - 1.0f); m2 = A * A - 1.0f;
                break;
            case Type::highShelf:
                w *= std::sqrt (A);
                m0 = A * A; m1 = k * (1.0f - A) * A; m2 = 1.0f - A * A;
                break;
        }

        a1 = 1.0f / (1.0f + w * (w + k));
        a2 = w * a1;
        a3 = w * a2;
    }

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    float process (float v0) noexcept
    {
        const auto v3 = v0 - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return m0 * v0 + m1 * v1 + m2 * v2;
    }

private:
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

//======================================================================================================================
/** DC blocking high-pass (first order). */
class DcBlocker
{
public:
    void prepare (double sampleRate, float hz = 8.0f) noexcept
    {
        r = std::exp (-twoPi * hz / static_cast<float> (sampleRate));
    }

    void reset() noexcept { x1 = y1 = 0.0f; }

    float process (float x) noexcept
    {
        const auto y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }

private:
    float r = 0.999f, x1 = 0.0f, y1 = 0.0f;
};

//======================================================================================================================
/** Peak/RMS style envelope follower with independent attack and release. */
class EnvelopeFollower
{
public:
    void prepare (double sampleRate, float attackMs, float releaseMs) noexcept
    {
        const auto fs = static_cast<float> (sampleRate);
        att = std::exp (-1.0f / (fs * attackMs * 0.001f));
        rel = std::exp (-1.0f / (fs * releaseMs * 0.001f));
    }

    void reset() noexcept { env = 0.0f; }

    float process (float x) noexcept
    {
        const auto a = std::abs (x);
        const auto c = a > env ? att : rel;
        env = a + c * (env - a);
        return env;
    }

    float get() const noexcept { return env; }

private:
    float att = 0.0f, rel = 0.0f, env = 0.0f;
};

//======================================================================================================================
/** Tilt equaliser built from a one-pole split: positive tilt brightens, negative darkens. */
class TiltFilter
{
public:
    void prepare (double sampleRate, float pivotHz) noexcept
    {
        fs = sampleRate;
        lp.setCutoff (pivotHz, sampleRate);
        lp.reset();
    }

    void reset() noexcept { lp.reset(); }

    /** @param tilt -1..1, @param maxDb gain applied at the spectrum extremes. */
    void setTilt (float tilt, float maxDb) noexcept
    {
        lowGain = dbToGain (-tilt * maxDb);
        highGain = dbToGain (tilt * maxDb);
    }

    float process (float x) noexcept
    {
        const auto low = lp.processLP (x);
        return low * lowGain + (x - low) * highGain;
    }

private:
    double fs = 44100.0;
    OnePole lp;
    float lowGain = 1.0f, highGain = 1.0f;
};

//======================================================================================================================
/** Smooth random signal in [-1, 1] built from Catmull-Rom interpolated random points with jittered spacing.
    This is the core of the "Flux" engine: organic, non-repeating drift without zipper noise. */
class SmoothRandom
{
public:
    void prepare (double sampleRate, uint32_t seed) noexcept
    {
        fs = static_cast<float> (sampleRate);
        rng.setSeed (seed);
        p0 = rng.nextBipolar();
        p1 = rng.nextBipolar();
        p2 = rng.nextBipolar();
        p3 = rng.nextBipolar();
        phase = rng.nextFloat();
        setRate (rateHz);
    }

    void setRate (float hz) noexcept
    {
        rateHz = std::max (hz, 0.001f);
        baseInc = rateHz / fs;
        if (inc <= 0.0f)
            inc = baseInc;
    }

    float next() noexcept
    {
        phase += inc;

        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            p0 = p1;
            p1 = p2;
            p2 = p3;
            p3 = rng.nextBipolar();
            // Jitter the segment length so the motion never becomes periodic.
            inc = baseInc * (0.6f + 0.8f * rng.nextFloat());
        }

        const auto t = phase;
        const auto t2 = t * t;
        const auto t3 = t2 * t;
        const auto v = 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                               + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
        return std::clamp (v, -1.25f, 1.25f) * 0.8f;
    }

    /** Advances by n samples and returns the final value (control-rate use). */
    float advance (int n) noexcept
    {
        auto v = 0.0f;
        for (int i = 0; i < n; ++i)
            v = next();
        return v;
    }

private:
    FastRandom rng;
    float fs = 44100.0f, rateHz = 1.0f, baseInc = 0.0f, inc = 0.0f, phase = 0.0f;
    float p0 = 0.0f, p1 = 0.0f, p2 = 0.0f, p3 = 0.0f;
};

//======================================================================================================================
/** Two-speed flux source: a slow wander plus a faster, rougher component that only appears at high flux settings. */
class FluxSource
{
public:
    void prepare (double sampleRate, uint32_t seed, float slowHz, float fastHz) noexcept
    {
        slow.prepare (sampleRate, seed);
        fast.prepare (sampleRate, seed * 7919u + 17u);
        slowBase = slowHz;
        fastBase = fastHz;
        slow.setRate (slowHz);
        fast.setRate (fastHz);
    }

    /** @param amount 0..1 flux amount. */
    void setAmount (float amount) noexcept
    {
        depthSlow = std::pow (amount, 1.3f);
        depthFast = amount * amount * amount;
        // More flux also means livelier motion.
        slow.setRate (slowBase * (0.6f + 1.4f * amount));
        fast.setRate (fastBase * (0.7f + 1.8f * amount));
    }

    /** Returns a value roughly in [-1, 1] scaled by the current flux amount. */
    float next() noexcept { return depthSlow * slow.next() + 0.6f * depthFast * fast.next(); }

    /** Advances by n samples and returns the final value (control-rate use). */
    float advance (int n) noexcept
    {
        auto v = 0.0f;
        for (int i = 0; i < n; ++i)
            v = next();
        return v;
    }

private:
    SmoothRandom slow, fast;
    float slowBase = 0.5f, fastBase = 4.0f, depthSlow = 0.0f, depthFast = 0.0f;
};

//======================================================================================================================
/** Circular delay buffer with power-of-two size and several fractional read modes. */
class DelayBuffer
{
public:
    void allocate (int maxDelaySamples)
    {
        int size = 1;
        while (size < maxDelaySamples + 64)
            size <<= 1;
        buffer.assign (static_cast<size_t> (size), 0.0f);
        mask = size - 1;
        writeIndex = 0;
    }

    void clear() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    int capacity() const noexcept { return mask + 1 - 64; }

    void push (float x) noexcept
    {
        buffer[static_cast<size_t> (writeIndex)] = x;
        writeIndex = (writeIndex + 1) & mask;
    }

    /** Integer delay. A delay of 1 returns the most recently pushed sample. */
    float read (int delay) const noexcept
    {
        return buffer[static_cast<size_t> ((writeIndex - delay) & mask)];
    }

    float readLinear (float delay) const noexcept
    {
        const auto d = static_cast<int> (delay);
        const auto f = delay - static_cast<float> (d);
        const auto a = read (d);
        const auto b = read (d + 1);
        return a + f * (b - a);
    }

    /** Cubic Hermite (4-point, 3rd order) interpolation. Requires delay >= 2. */
    float readHermite (float delay) const noexcept
    {
        const auto d = static_cast<int> (delay);
        const auto f = delay - static_cast<float> (d);
        const auto xm1 = read (d - 1);
        const auto x0 = read (d);
        const auto x1 = read (d + 1);
        const auto x2 = read (d + 2);
        const auto c1 = 0.5f * (x1 - xm1);
        const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    const float* data() const noexcept { return buffer.data(); }
    int getMask() const noexcept { return mask; }
    int getWriteIndex() const noexcept { return writeIndex; }

private:
    std::vector<float> buffer;
    int mask = 0, writeIndex = 0;
};

//======================================================================================================================
/** Kaiser-windowed sinc interpolator for very clean modulated delays (pitch wobble). */
class SincTable
{
public:
    static constexpr int taps = 16;
    static constexpr int half = taps / 2;
    static constexpr int phases = 512;

    static const SincTable& get()
    {
        static const SincTable table;
        return table;
    }

    /** Reads with a fractional delay (in samples). Requires delay >= half + 1. */
    float read (const DelayBuffer& buf, float delay) const noexcept
    {
        // Position of the sample we want, measured backwards from the newest sample.
        const auto d = static_cast<int> (delay);
        const auto frac = delay - static_cast<float> (d);
        const auto p = frac * static_cast<float> (phases);
        const auto pi0 = std::min (static_cast<int> (p), phases - 1);
        const auto pf = p - static_cast<float> (pi0);
        const auto* c0 = coeffs.data() + static_cast<size_t> (pi0) * taps;
        const auto* c1 = c0 + taps;

        const auto* data = buf.data();
        const auto mask = buf.getMask();
        // Tap t corresponds to delay (d - half + 1 + t).
        auto idx = buf.getWriteIndex() - (d - half + 1);
        auto acc = 0.0f;

        for (int t = 0; t < taps; ++t)
        {
            const auto c = c0[t] + pf * (c1[t] - c0[t]);
            acc += c * data[static_cast<size_t> ((idx - t) & mask)];
        }

        return acc;
    }

private:
    SincTable()
    {
        coeffs.resize (static_cast<size_t> ((phases + 1) * taps));
        const auto beta = 7.5;
        const auto cutoff = 0.94; // slightly below Nyquist to tame aliasing when pitch rises
        const auto i0Beta = besselI0 (beta);

        for (int p = 0; p <= phases; ++p)
        {
            const auto frac = static_cast<double> (p) / phases;
            double sum = 0.0;

            for (int t = 0; t < taps; ++t)
            {
                // Distance from the desired (fractional) position to this tap.
                const auto x = static_cast<double> (t - (half - 1)) - frac;
                const auto sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * cutoff * x) / (juce::MathConstants<double>::pi * cutoff * x);
                const auto r = x / static_cast<double> (half);
                const auto w = std::abs (r) >= 1.0 ? 0.0 : besselI0 (beta * std::sqrt (1.0 - r * r)) / i0Beta;
                const auto v = sinc * w;
                coeffs[static_cast<size_t> (p * taps + t)] = static_cast<float> (v);
                sum += v;
            }

            for (int t = 0; t < taps; ++t)
                coeffs[static_cast<size_t> (p * taps + t)] = static_cast<float> (coeffs[static_cast<size_t> (p * taps + t)] / sum);
        }
    }

    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 32; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    std::vector<float> coeffs;
};

//======================================================================================================================
/** First-order antiderivative anti-aliasing helper for static waveshapers (Parker et al.). */
template <typename Fn, typename AntiFn>
class Adaa1
{
public:
    Adaa1 (Fn f, AntiFn F) : fn (f), anti (F) {}

    void reset() noexcept { x1 = 0.0f; F1 = anti (0.0f); }

    float process (float x) noexcept
    {
        const auto Fx = anti (x);
        const auto diff = x - x1;
        float y;

        if (std::abs (diff) < 1.0e-4f)
            y = fn (0.5f * (x + x1));
        else
            y = (Fx - F1) / diff;

        x1 = x;
        F1 = Fx;
        return y;
    }

private:
    Fn fn;
    AntiFn anti;
    float x1 = 0.0f, F1 = 0.0f;
};

/** Accurate rational tanh approximation (error < 1e-5 on [-5, 5]), clamped outside that range. */
inline float fastTanh (float x) noexcept
{
    if (x > 5.0f) return 1.0f;
    if (x < -5.0f) return -1.0f;
    const auto x2 = x * x;
    const auto num = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const auto den = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2));
    return std::clamp (num / den, -1.0f, 1.0f);
}

/** Mathematically safe log(cosh(x)) for large |x|. */
inline float logCosh (float x) noexcept
{
    const auto ax = std::abs (x);
    if (ax > 12.0f)
        return ax - 0.69314718f;
    return std::log (std::cosh (x));
}

/** Simple bounded linear ramp used for crossfades (e.g. switching types without clicks). */
class LinearRamp
{
public:
    void prepare (double sampleRate, float seconds) noexcept { step = 1.0f / std::max (1.0f, static_cast<float> (sampleRate) * seconds); }
    void setTarget (float t) noexcept { target = t; }
    void snapTo (float v) noexcept { value = target = v; }
    float next() noexcept
    {
        if (value < target) value = std::min (target, value + step);
        else if (value > target) value = std::max (target, value - step);
        return value;
    }
    float get() const noexcept { return value; }
    bool isRamping() const noexcept { return std::abs (value - target) > 0.0f; }

private:
    float step = 0.001f, value = 0.0f, target = 0.0f;
};

} // namespace afterglow::dsp
