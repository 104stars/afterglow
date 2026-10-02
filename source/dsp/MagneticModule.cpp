#include "MagneticModule.h"

#include <complex>

namespace afterglow::dsp
{
void MagneticModule::prepare (double sampleRate, int)
{
    fs = sampleRate;
    rng.setSeed (0x7A9Eu);

    for (int c = 0; c < 2; ++c)
    {
        auto& ch = channels[c];
        const auto seed = 0x1000u + static_cast<uint32_t> (c) * 0x777u;
        ch.wearSlow.prepare (sampleRate, seed + 1u);
        ch.wearFast.prepare (sampleRate, seed + 2u);
        ch.scrape.prepare (sampleRate, seed + 3u);
        ch.wearSlow.setRate (0.9f);
        ch.wearFast.setRate (3.1f);
    }

    rateFlux.prepare (sampleRate, 0x2929u, 0.25f, 2.0f);
    depthFlux.prepare (sampleRate, 0x3939u, 0.2f, 1.6f);

    amountSm.prepare (sampleRate, 0.05f);
    balanceSm.prepare (sampleRate, 0.05f);
    rateSm.prepare (sampleRate, 0.2f);
    dropoutSm.prepare (sampleRate, 0.05f);
    stereoSm.prepare (sampleRate, 0.08f);
    tapeWindow = std::max (1, static_cast<int> (std::lround (sampleRate / 64.0)));

    reset();
}

void MagneticModule::reset()
{
    for (auto& ch : channels)
    {
        ch.dropout = {};
        ch.lowPass.reset();
        ch.gain = ch.prevGain = 1.0f;
        ch.lossAmount = ch.prevLoss = 0.0f;
    }

    flutterPhase = 0.0;
    primed = false;
    tapeSamples = 0;
}

float MagneticModule::responseDb (float gainDb, float loss, float hz) noexcept
{
    // The treble loss blends a 2-pole low-pass (Q 0.6) into the signal: y = (1 - b) x + b lp(x).
    const auto cutoff = logLerp (20000.0f, 2200.0f, clamp01 (loss));
    const auto blend = clamp01 (4.0f * loss);
    const auto r = hz / cutoff;
    const std::complex<float> lowPass = 1.0f / std::complex<float> (1.0f - r * r, r / 0.6f);
    const auto h = (1.0f - blend) + blend * lowPass;
    return gainDb + 20.0f * std::log10 (std::max (1.0e-6f, std::abs (h)));
}

void MagneticModule::collectTape (const float* gainsDb, const float* losses, int len) noexcept
{
    for (int c = 0; c < 2; ++c)
    {
        tapeGainDb[c] = tapeSamples == 0 ? gainsDb[c] : std::min (tapeGainDb[c], gainsDb[c]);
        tapeLoss[c] = tapeSamples == 0 ? losses[c] : std::max (tapeLoss[c], losses[c]);
    }

    tapeSamples += len;
    if (tapeSamples >= tapeWindow)
    {
        if (telemetry != nullptr)
            telemetry->magneticTape.push ({ tapeGainDb[0], tapeLoss[0], tapeGainDb[1], tapeLoss[1] });
        tapeSamples = 0;
    }
}

void MagneticModule::updateDropout (Dropout& d, float eventsPerSecond, float depthScale, float flux, int len) noexcept
{
    const auto sr = static_cast<float> (fs);

    if (d.holdSamples > 0)
    {
        d.holdSamples -= len;
        d.target = 1.0f;
    }
    else
    {
        d.target = 0.0f;

        if (depthScale > 0.0f && rng.nextFloat() < eventsPerSecond * static_cast<float> (len) / sr)
        {
            const auto r = rng.nextFloat();
            d.holdSamples = static_cast<int> (sr * (0.012f + 0.26f * r * r * (1.0f + flux)));
            d.depthDb = depthScale * (4.0f + 36.0f * std::pow (rng.nextFloat(), 1.5f));
            d.attack = std::exp (-static_cast<float> (len) / (sr * (0.003f + 0.009f * rng.nextFloat())));
            d.release = std::exp (-static_cast<float> (len) / (sr * (0.025f + 0.12f * rng.nextFloat())));
        }
    }

    const auto coeff = d.target > d.env ? d.attack : d.release;
    d.env = d.target + (d.env - d.target) * coeff;
    if (d.env < 1.0e-5f && d.target <= 0.0f)
        d.env = 0.0f;
}

void MagneticModule::process (float* left, float* right, int n, const MagneticParams& p, const TransportInfo& transport) noexcept
{
    const auto amountTarget = p.on ? p.amount : 0.0f;

    amountSm.setTarget (amountTarget);
    balanceSm.setTarget (p.balance);
    rateSm.setTarget (p.rate);
    dropoutSm.setTarget (p.dropouts);
    stereoSm.setTarget (p.stereo ? 1.0f : 0.0f);

    if (! primed)
    {
        amountSm.snapTo (amountTarget);
        balanceSm.snapTo (p.balance);
        rateSm.snapTo (p.rate);
        dropoutSm.snapTo (p.dropouts);
        stereoSm.snapTo (p.stereo ? 1.0f : 0.0f);
        primed = true;
    }

    if (p.syncBeats > 0.0 && transport.hasPpq && transport.isPlaying)
    {
        const auto target = transport.ppqPosition / p.syncBeats;
        auto error = (target - std::floor (target)) - flutterPhase;
        error -= std::round (error);
        flutterPhase += 0.1 * error;
        flutterPhase -= std::floor (flutterPhase);
    }

    rateFlux.setAmount (p.flux);
    depthFlux.setAmount (p.flux);

    if (amountSm.getCurrent() <= 0.0f && amountTarget <= 0.0f)
    {
        // Untouched tape: keep the display history running at 0 dB and no loss.
        static constexpr float flat[2] {};
        for (int done = 0; done < n; done += controlInterval)
            collectTape (flat, flat, std::min (controlInterval, n - done));

        for (auto& ch : channels)
        {
            ch.gain = ch.prevGain = 1.0f;
            ch.lossAmount = ch.prevLoss = 0.0f;
            ch.dropout = {};
        }
        return;
    }

    float* io[2] { left, right };

    for (int start = 0; start < n; start += controlInterval)
    {
        const auto len = std::min (controlInterval, n - start);
        const auto a = amountSm.skip (len);
        const auto bal = balanceSm.skip (len);
        const auto fluxRate = rateFlux.advance (len);
        const auto rate = std::max (0.5f, rateSm.skip (len) * (1.0f + 0.3f * fluxRate));
        const auto drop = dropoutSm.skip (len);
        const auto stereo = stereoSm.skip (len);
        const auto depthMult = std::max (0.0f, 1.0f + 0.5f * depthFlux.advance (len));

        const auto wearMix = std::min (1.0f, 2.0f * (1.0f - bal));
        const auto flutterMix = std::min (1.0f, 2.0f * bal);
        const auto depthCurve = std::pow (a, 1.3f);
        const auto wearDepthDb = 10.0f * depthCurve * wearMix * depthMult;
        const auto flutterDepthDb = 6.0f * depthCurve * flutterMix * depthMult;
        const auto eventsPerSecond = drop * drop * 3.5f * (1.0f + 1.5f * p.flux * std::abs (fluxRate));
        const auto depthScale = std::pow (a, 0.6f) * depthMult;

        flutterPhase += static_cast<double> (rate) * len / fs;
        flutterPhase -= std::floor (flutterPhase);
        const auto phi = static_cast<float> (flutterPhase) * twoPi;

        float gains[2], losses[2];

        for (int c = 0; c < 2; ++c)
        {
            auto& ch = channels[c];
            ch.scrape.setRate (rate * 2.2f);
            ch.wearSlow.setRate (0.9f * (1.0f + p.flux));
            ch.wearFast.setRate (3.1f * (1.0f + p.flux));

            const auto w = 0.7f * ch.wearSlow.advance (len) + 0.3f * ch.wearFast.advance (len);
            const auto w01 = clamp01 (0.5f + 0.5f * w);
            const auto osc = 0.6f * std::sin (phi) + 0.25f * std::sin (2.7f * phi + 0.9f * static_cast<float> (c))
                           + 0.3f * ch.scrape.advance (len);
            const auto f01 = clamp01 (0.5f + 0.5f * osc);

            updateDropout (ch.dropout, eventsPerSecond, depthScale, p.flux, len);

            const auto attenuation = wearDepthDb * w01 + flutterDepthDb * f01 + ch.dropout.env * ch.dropout.depthDb;
            gains[c] = dbToGain (-attenuation);

            const auto wearLoss = 0.08f * wearDepthDb * w01;
            const auto dropLoss = ch.dropout.env * std::min (1.0f, ch.dropout.depthDb / 24.0f);
            losses[c] = clamp01 (std::max (wearLoss, dropLoss));
        }

        // Mono mode: the right channel follows the left one.
        gains[1] = lerp (gains[0], gains[1], stereo);
        losses[1] = lerp (losses[0], losses[1], stereo);

        const float gainsDb[2] { gainToDb (gains[0]), gainToDb (gains[1]) };
        collectTape (gainsDb, losses, len);

        const auto invLen = 1.0f / static_cast<float> (len);

        for (int c = 0; c < 2; ++c)
        {
            auto& ch = channels[c];
            ch.prevGain = ch.gain;
            ch.prevLoss = ch.lossAmount;
            ch.gain = gains[c];
            ch.lossAmount = losses[c];

            const auto cutoff = logLerp (20000.0f, 2200.0f, ch.lossAmount);
            ch.lowPass.setup (Svf::Type::lowPass, std::min (cutoff, 0.45f * static_cast<float> (fs)), 0.6f, 0.0f, fs);

            auto* d = io[c] + start;

            if (ch.prevLoss <= 0.0f && ch.lossAmount <= 0.0f)
            {
                for (int i = 0; i < len; ++i)
                    d[i] *= lerp (ch.prevGain, ch.gain, static_cast<float> (i + 1) * invLen);
                continue;
            }

            for (int i = 0; i < len; ++i)
            {
                const auto t = static_cast<float> (i + 1) * invLen;
                const auto g = lerp (ch.prevGain, ch.gain, t);
                const auto loss = clamp01 (4.0f * lerp (ch.prevLoss, ch.lossAmount, t));
                const auto x = d[i];
                const auto lp = ch.lowPass.process (x);
                d[i] = (x + loss * (lp - x)) * g;
            }
        }
    }
}

} // namespace afterglow::dsp
