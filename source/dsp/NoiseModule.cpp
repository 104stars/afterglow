#include "NoiseModule.h"

namespace afterglow::dsp
{
namespace
{
    /** Perceptual volume curve for the big knob: 0 % is silent, 100 % is the calibrated reference level. */
    float amountToGain (float a) noexcept
    {
        if (a <= 0.0f)
            return 0.0f;

        const auto db = -54.0f * std::pow (1.0f - std::min (a, 1.0f), 1.6f);
        return dbToGain (db) * engageCurve (a, 0.03f);
    }
} // namespace

void NoiseModule::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate;
    synth.prepare (sampleRate, 0xA5F1u);

    for (auto& t : tilt)
        t.prepare (sampleRate, 1200.0f);

    levelFlux.prepare (sampleRate, 0x1234u, 0.35f, 3.0f);
    toneFlux.prepare (sampleRate, 0x4321u, 0.2f, 1.5f);
    activityFlux.prepare (sampleRate, 0x7777u, 0.25f, 2.0f);

    followEnv.prepare (sampleRate, 3.0f, 90.0f);
    duckEnv.prepare (sampleRate, 35.0f, 280.0f);

    amountSm.prepare (sampleRate, 0.05f);
    toneSm.prepare (sampleRate, 0.05f);
    followSm.prepare (sampleRate, 0.05f);
    duckSm.prepare (sampleRate, 0.05f);
    transportSm.prepare (sampleRate, 0.03f);
    typeFade.prepare (sampleRate, 0.025f);

    const auto size = static_cast<size_t> (maxBlockSize);
    envFollow.assign (size, 0.0f);
    envDuck.assign (size, 0.0f);
    bufL.assign (size, 0.0f);
    bufR.assign (size, 0.0f);

    reset();
}

void NoiseModule::reset()
{
    synth.reset();
    for (auto& t : tilt)
        t.reset();
    followEnv.reset();
    duckEnv.reset();
    typeFade.snapTo (1.0f);
    pendingType = -1;
    primed = false;
    lastBlockRms = 0.0f;
    lastGain = 0.0f;
}

void NoiseModule::analyseInput (const float* left, const float* right, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const auto x = std::max (std::abs (left[i]), std::abs (right[i]));
        envFollow[static_cast<size_t> (i)] = followEnv.process (x);
        envDuck[static_cast<size_t> (i)] = duckEnv.process (x);
    }
}

void NoiseModule::render (int n, const NoiseParams& p, const TransportInfo& transport) noexcept
{
    const auto amountTarget = p.on ? p.amount : 0.0f;
    amountSm.setTarget (amountTarget);
    toneSm.setTarget (p.tone);
    followSm.setTarget (p.follow);
    duckSm.setTarget (p.duck);

    // Like a real noise floor that stops with the tape: fade out when the host transport stops.
    const auto transportTarget = transport.isPlaying ? 1.0f : 0.0f;
    if (transport.isPlaying != wasPlaying)
    {
        transportSm.prepare (fs, transport.isPlaying ? 0.03f : 0.3f);
        wasPlaying = transport.isPlaying;
    }
    transportSm.setTarget (transportTarget);

    if (! primed)
    {
        amountSm.snapTo (amountTarget);
        toneSm.snapTo (p.tone);
        followSm.snapTo (p.follow);
        duckSm.snapTo (p.duck);
        transportSm.snapTo (transportTarget);
        currentType = p.type;
        primed = true;
    }

    if (p.type != currentType && pendingType != p.type)
    {
        pendingType = p.type;
        typeFade.setTarget (0.0f);
    }

    levelFlux.setAmount (p.flux);
    toneFlux.setAmount (p.flux);
    activityFlux.setAmount (p.flux);

    // Nothing audible and nothing fading: skip the synthesis entirely.
    if (amountSm.getCurrent() <= 0.0f && amountTarget <= 0.0f)
    {
        std::fill_n (bufL.data(), n, 0.0f);
        std::fill_n (bufR.data(), n, 0.0f);
        lastBlockRms = 0.0f;
        lastGain = 0.0f;
        return;
    }

    auto previousGain = lastGain;

    for (int start = 0; start < n; start += controlInterval)
    {
        const auto len = std::min (controlInterval, n - start);

        if (pendingType >= 0 && ! typeFade.isRamping() && typeFade.get() <= 0.0f)
        {
            currentType = pendingType;
            pendingType = -1;
            typeFade.setTarget (1.0f);
        }

        const auto activity = std::clamp (activityFlux.advance (len) * 1.5f, -1.0f, 1.0f);
        synth.render (currentType, bufL.data() + start, bufR.data() + start, len, activity);

        const auto a = amountSm.skip (len);
        const auto tone = std::clamp (toneSm.skip (len) + 0.6f * toneFlux.advance (len), -1.0f, 1.0f);
        const auto fl = followSm.skip (len);
        const auto dk = duckSm.skip (len);
        const auto tr = transportSm.skip (len);
        const auto fluxDb = 8.0f * levelFlux.advance (len);

        for (auto& t : tilt)
            t.setTilt (tone, 12.0f);

        const auto last = static_cast<size_t> (start + len - 1);
        const auto followDb = gainToDb (envFollow[last]);
        auto followShape = std::clamp ((followDb + 54.0f) / 48.0f, 0.0f, 1.0f);
        followShape = followShape * followShape * (3.0f - 2.0f * followShape);
        const auto followGain = 1.0f - fl + fl * followShape;

        const auto duckDb = gainToDb (envDuck[last]);
        const auto duckDepth = std::clamp ((duckDb + 42.0f) / 30.0f, 0.0f, 1.0f);
        const auto duckGain = dbToGain (-30.0f * dk * duckDepth);

        const auto target = amountToGain (a) * dbToGain (fluxDb) * followGain * duckGain * tr;

        for (int i = 0; i < len; ++i)
        {
            const auto idx = static_cast<size_t> (start + i);
            const auto g = previousGain + (target - previousGain) * static_cast<float> (i + 1) / static_cast<float> (len);
            const auto fade = typeFade.next();
            bufL[idx] = tilt[0].process (bufL[idx]) * g * fade;
            bufR[idx] = tilt[1].process (bufR[idx]) * g * fade;
        }

        previousGain = target;
    }

    lastGain = previousGain;

    auto sum = 0.0f;
    for (int i = 0; i < n; ++i)
        sum += bufL[static_cast<size_t> (i)] * bufL[static_cast<size_t> (i)];
    lastBlockRms = n > 0 ? std::sqrt (sum / static_cast<float> (n)) : 0.0f;
}

void NoiseModule::addTo (float* left, float* right, int n) const noexcept
{
    for (int i = 0; i < n; ++i)
    {
        left[i] += bufL[static_cast<size_t> (i)];
        right[i] += bufR[static_cast<size_t> (i)];
    }
}

} // namespace afterglow::dsp
