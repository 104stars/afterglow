#include "DigitalModule.h"

namespace afterglow::dsp
{
namespace
{
    constexpr float muLaw = 255.0f;
    const float logOnePlusMu = std::log (1.0f + muLaw);

    float quantise (float v, float levels) noexcept { return std::round (v * levels) / levels; }
} // namespace

void DigitalModule::prepare (double sampleRate, int)
{
    fs = sampleRate;
    rng.setSeed (0xC0DEu);

    rateFlux.prepare (sampleRate, 0x0A0Au, 0.2f, 1.7f);
    bitsFlux.prepare (sampleRate, 0x0B0Bu, 0.18f, 1.4f);
    jitterFlux.prepare (sampleRate, 0x0C0Cu, 0.5f, 3.0f);

    amountSm.prepare (sampleRate, 0.05f);
    balanceSm.prepare (sampleRate, 0.05f);
    smoothSm.prepare (sampleRate, 0.05f);
    lowSm.prepare (sampleRate, 0.06f);
    highSm.prepare (sampleRate, 0.06f);
    cutSm.prepare (sampleRate, 0.03f);
    mixSm.prepare (sampleRate, 0.05f);
    compandSm.prepare (sampleRate, 0.03f);

    reset();
}

void DigitalModule::reset()
{
    for (int c = 0; c < 2; ++c)
    {
        focusHigh[c].reset();
        focusLow[c].reset();
        preFilter[c].reset();
        postFilterA[c].reset();
        postFilterB[c].reset();
        held[c] = 0.0f;
    }

    holdPhase = 0.0f;
    primed = false;
    currentRate = static_cast<float> (fs);
    currentBits = 24.0f;
}

void DigitalModule::updateFilters (float lowHz, float highHz, float smoothCutoff) noexcept
{
    highHz = std::max (highHz, lowHz * 1.15f);
    const auto nyquistSafe = 0.47f * static_cast<float> (fs);

    for (int c = 0; c < 2; ++c)
    {
        focusHigh[c].setup (Svf::Type::highPass, lowHz, 0.7071f, 0.0f, fs);
        focusLow[c].setup (Svf::Type::lowPass, std::min (highHz, nyquistSafe), 0.7071f, 0.0f, fs);
        // 2nd order anti-alias before the hold, 4th order (Butterworth pair) reconstruction after it.
        preFilter[c].setup (Svf::Type::lowPass, smoothCutoff, 0.7071f, 0.0f, fs);
        postFilterA[c].setup (Svf::Type::lowPass, smoothCutoff, 0.5412f, 0.0f, fs);
        postFilterB[c].setup (Svf::Type::lowPass, smoothCutoff, 1.3066f, 0.0f, fs);
    }
}

float DigitalModule::crush (int, float x, float bitsAmount, float levels, bool) noexcept
{
    if (bitsAmount <= 0.0f)
        return x;

    const auto v = std::clamp (x, -1.0f, 1.0f);
    const auto linear = quantise (v, levels);

    const auto compand = compandSm.getCurrent();
    auto result = linear;

    if (compand > 0.0f)
    {
        const auto encoded = std::copysign (std::log1p (muLaw * std::abs (v)) / logOnePlusMu, v);
        const auto q = quantise (encoded, levels);
        const auto decoded = std::copysign ((std::exp (std::abs (q) * logOnePlusMu) - 1.0f) / muLaw, q);
        result = lerp (linear, decoded, compand);
    }

    return lerp (x, result, bitsAmount);
}

void DigitalModule::process (float* left, float* right, int n, const DigitalParams& p) noexcept
{
    const auto amountTarget = p.on ? p.amount : 0.0f;

    amountSm.setTarget (amountTarget);
    balanceSm.setTarget (p.balance);
    smoothSm.setTarget (p.smooth);
    lowSm.setTarget (std::log (std::max (p.focusLow, 10.0f)));
    highSm.setTarget (std::log (std::max (p.focusHigh, 20.0f)));
    cutSm.setTarget (p.cut ? 1.0f : 0.0f);
    mixSm.setTarget (p.mix);
    compandSm.setTarget (p.compand ? 1.0f : 0.0f);

    if (! primed)
    {
        amountSm.snapTo (amountTarget);
        balanceSm.snapTo (p.balance);
        smoothSm.snapTo (p.smooth);
        lowSm.snapTo (lowSm.getTarget());
        highSm.snapTo (highSm.getTarget());
        cutSm.snapTo (cutSm.getTarget());
        mixSm.snapTo (p.mix);
        compandSm.snapTo (compandSm.getTarget());
        prevEngage = engageCurve (amountTarget, 0.04f);
        primed = true;
    }

    rateFlux.setAmount (p.flux);
    bitsFlux.setAmount (p.flux);
    jitterFlux.setAmount (p.flux);

    if (amountSm.getCurrent() <= 0.0f && amountTarget <= 0.0f)
    {
        currentRate = static_cast<float> (fs);
        currentBits = 24.0f;
        return;
    }

    const auto sr = static_cast<float> (fs);

    for (int start = 0; start < n; start += controlInterval)
    {
        const auto len = std::min (controlInterval, n - start);
        const auto a = amountSm.skip (len);
        const auto bal = balanceSm.skip (len);
        const auto smooth = smoothSm.skip (len);
        const auto lowHz = std::exp (lowSm.skip (len));
        const auto highHz = std::exp (highSm.skip (len));
        const auto fluxRate = rateFlux.advance (len);
        const auto fluxBits = bitsFlux.advance (len);
        const auto jitterAmount = 0.6f * std::abs (jitterFlux.advance (len)) + 0.15f * p.flux;
        const auto engage = engageCurve (a, 0.04f);

        const auto rateAmount = a * std::min (1.0f, 2.0f * (1.0f - bal));
        const auto bitAmount = a * std::min (1.0f, 2.0f * bal);
        const auto rateActive = clamp01 (rateAmount * 10.0f);
        const auto bitActive = clamp01 (bitAmount * 10.0f);

        // Rate: from the host rate down to about 650 Hz. Bits: from 16 bits down to 1.5 bits.
        const auto logRate = lerp (std::log (sr), std::log (650.0f), std::pow (rateAmount, 0.8f)) + 0.5f * fluxRate * rateActive;
        const auto targetRate = std::clamp (std::exp (logRate), 200.0f, sr);
        const auto bits = std::clamp (16.0f - 14.5f * std::pow (bitAmount, 0.65f) + 2.5f * fluxBits * bitActive, 1.5f, 16.0f);
        const auto levels = std::pow (2.0f, bits - 1.0f);
        const auto bitBlend = clamp01 (bitAmount / 0.02f);

        const auto smoothActive = clamp01 (smooth / 0.02f);
        const auto smoothCutoff = logLerp (0.45f * sr, std::min (0.45f * sr, 0.46f * targetRate), smooth);
        updateFilters (lowHz, highHz, smoothCutoff);

        currentRate = targetRate;
        currentBits = bitAmount > 0.0f ? bits : 24.0f;

        const auto increment = targetRate / sr;
        const auto jitter = jitterAmount * rateActive;
        const auto invLen = 1.0f / static_cast<float> (len);

        for (int i = start; i < start + len; ++i)
        {
            const auto t = static_cast<float> (i - start + 1) * invLen;
            const auto g = mixSm.next() * lerp (prevEngage, engage, t);
            const auto cut = cutSm.next();
            compandSm.next();

            const float x[2] { left[i], right[i] };
            float m[2], pre[2];

            for (int c = 0; c < 2; ++c)
            {
                m[c] = focusLow[c].process (focusHigh[c].process (x[c]));
                const auto filtered = preFilter[c].process (m[c]);
                pre[c] = lerp (m[c], filtered, smoothActive);
            }

            // Sample-and-hold clock, with jitter when Flux is up.
            holdPhase += increment * (1.0f + jitter * rng.nextBipolar());
            if (holdPhase >= 1.0f)
            {
                holdPhase -= std::floor (holdPhase);
                held[0] = pre[0];
                held[1] = pre[1];
            }

            float out[2];
            for (int c = 0; c < 2; ++c)
            {
                auto z = crush (c, held[c], bitBlend, levels, p.compand);
                const auto post = postFilterB[c].process (postFilterA[c].process (z));
                z = lerp (z, post, smoothActive);
                out[c] = x[c] + g * (z - m[c] - cut * (x[c] - m[c]));
            }

            left[i] = out[0];
            right[i] = out[1];
        }

        prevEngage = engage;
    }
}

void DigitalModule::publish (EngineTelemetry& telemetry) const noexcept
{
    telemetry.digitalRate.store (currentRate, std::memory_order_relaxed);
    telemetry.digitalBits.store (currentBits, std::memory_order_relaxed);
}

} // namespace afterglow::dsp
