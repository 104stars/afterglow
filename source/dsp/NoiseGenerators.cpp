#include "NoiseGenerators.h"

namespace afterglow::dsp
{
//======================================================================================================================
void CrackleGenerator::prepare (double sampleRate, uint32_t seed)
{
    fs = sampleRate;
    rng.setSeed (seed);
    setSettings (settings);
    reset();
}

void CrackleGenerator::setSettings (const Settings& s)
{
    settings = s;

    for (auto& c : ch)
    {
        c.hp.setCutoff (s.highPassHz, fs);
        c.lp.setCutoff (s.lowPassHz, fs);
        c.popLp.setCutoff (1600.0f, fs);
        c.thump.setCutoff (150.0f, fs);
    }

    popDecay = std::exp (-1.0f / (static_cast<float> (fs) * s.popDecayMs * 0.001f));
}

void CrackleGenerator::reset()
{
    for (auto& c : ch)
    {
        c.env = c.impulse = c.popEnv = c.popImpulse = 0.0f;
        c.decay = 0.0f;
        c.hp.reset();
        c.lp.reset();
        c.popLp.reset();
        c.thump.reset();
    }
}

void CrackleGenerator::trigger (bool pop) noexcept
{
    const auto u = rng.nextFloat();
    const auto amp = pop ? (0.45f + 0.55f * u) * settings.popLevel
                         : std::pow (u, settings.sizePower);
    const auto pan = 0.5f + 0.5f * settings.stereoSpread * rng.nextBipolar();
    const float gains[2] { std::sqrt (2.0f * (1.0f - pan)), std::sqrt (2.0f * pan) };
    const auto polarity = rng.nextFloat() < 0.5f ? -1.0f : 1.0f;

    if (pop)
    {
        for (int c = 0; c < 2; ++c)
        {
            ch[c].popEnv += amp * gains[c];
            ch[c].popImpulse += polarity * amp * gains[c];
        }
        return;
    }

    const auto r = rng.nextFloat();
    const auto decayMs = lerp (settings.minDecayMs, settings.maxDecayMs, r * r);
    const auto decay = std::exp (-1.0f / (static_cast<float> (fs) * decayMs * 0.001f));

    for (int c = 0; c < 2; ++c)
    {
        ch[c].env = std::max (ch[c].env, amp * gains[c]);
        ch[c].decay = decay;
        ch[c].impulse += polarity * amp * gains[c];
    }
}

void CrackleGenerator::process (float& outL, float& outR, float densityScale) noexcept
{
    const auto invFs = 1.0f / static_cast<float> (fs);

    if (rng.nextFloat() < settings.eventsPerSecond * densityScale * invFs)
        trigger (false);

    if (rng.nextFloat() < settings.popsPerSecond * densityScale * invFs)
        trigger (true);

    float out[2];

    for (int c = 0; c < 2; ++c)
    {
        auto& s = ch[c];
        auto click = s.impulse + s.env * rng.nextBipolar();
        s.impulse = 0.0f;
        s.env *= s.decay;
        click = s.lp.processLP (s.hp.processHP (click));

        auto pop = s.popLp.processLP (s.popEnv * rng.nextBipolar()) + 3.0f * s.thump.processLP (s.popImpulse);
        s.popImpulse = 0.0f;
        s.popEnv *= popDecay;

        out[c] = click + pop;
    }

    outL = out[0];
    outR = out[1];
}

//======================================================================================================================
void NoiseSynth::prepare (double sampleRate, uint32_t seed)
{
    fs = sampleRate;
    invFs = 1.0f / static_cast<float> (sampleRate);
    rng.setSeed (seed);

    for (int i = 0; i < 4; ++i)
        wander[i].prepare (sampleRate, seed * 31u + static_cast<uint32_t> (i) * 977u + 5u);

    wander[0].setRate (0.35f);
    wander[1].setRate (0.12f);
    wander[2].setRate (0.25f);
    wander[3].setRate (2.5f);

    CrackleGenerator::Settings v;
    v.eventsPerSecond = 14.0f;
    v.sizePower = 3.4f;
    v.minDecayMs = 0.04f;
    v.maxDecayMs = 0.45f;
    v.popsPerSecond = 0.25f;
    v.popDecayMs = 2.0f;
    v.highPassHz = 900.0f;
    v.lowPassHz = 13000.0f;
    v.stereoSpread = 0.85f;
    v.popLevel = 0.9f;
    vinylCrackle.prepare (sampleRate, seed + 101u);
    vinylCrackle.setSettings (v);

    CrackleGenerator::Settings s;
    s.eventsPerSecond = 45.0f;
    s.sizePower = 2.6f;
    s.minDecayMs = 0.08f;
    s.maxDecayMs = 0.9f;
    s.popsPerSecond = 0.9f;
    s.popDecayMs = 3.5f;
    s.highPassHz = 450.0f;
    s.lowPassHz = 6000.0f;
    s.stereoSpread = 0.25f;
    s.popLevel = 1.0f;
    shellacCrackle.prepare (sampleRate, seed + 202u);
    shellacCrackle.setSettings (s);

    CrackleGenerator::Settings r;
    r.eventsPerSecond = 7.0f;
    r.sizePower = 2.2f;
    r.minDecayMs = 0.4f;
    r.maxDecayMs = 5.0f;
    r.popsPerSecond = 0.4f;
    r.popDecayMs = 6.0f;
    r.highPassHz = 350.0f;
    r.lowPassHz = 4000.0f;
    r.stereoSpread = 0.0f;
    r.popLevel = 0.7f;
    radioCrackle.prepare (sampleRate, seed + 303u);
    radioCrackle.setSettings (r);

    for (int c = 0; c < 2; ++c)
        dc[c].prepare (sampleRate, 12.0f);

    lastType = -1;
    reset();
}

void NoiseSynth::reset()
{
    for (int c = 0; c < 2; ++c)
    {
        bandA[c].reset();
        bandB[c].reset();
        bandC[c].reset();
        bandD[c].reset();
        lowA[c].reset();
        lowB[c].reset();
        pinkFilters[c] = {};
        brownState[c] = 0.0f;
        dc[c].reset();
    }

    vinylCrackle.reset();
    shellacCrackle.reset();
    radioCrackle.reset();
    phaseA = phaseB = phaseC = 0.0;
    burstEnv = gateEnv = gateTarget = beepEnv = 0.0f;
    gateCounter = beepCounter = 0;
    beepPhase = 0.0;
    heldL = heldR = clockPhase = 0.0f;
    lfsr = 1u;
}

void NoiseSynth::configureFilters()
{
    const auto t = lastType;

    for (int c = 0; c < 2; ++c)
    {
        switch (t)
        {
            case vinyl:
                bandA[c].setup (Svf::Type::highPass, 500.0f, 0.707f, 0.0f, fs);
                bandB[c].setup (Svf::Type::lowPass, 9000.0f, 0.707f, 0.0f, fs);
                lowA[c].setCutoff (28.0f, fs);
                lowB[c].setCutoff (28.0f, fs);
                break;
            case shellac:
                bandA[c].setup (Svf::Type::highPass, 280.0f, 0.707f, 0.0f, fs);
                bandB[c].setup (Svf::Type::lowPass, 5200.0f, 0.9f, 0.0f, fs);
                bandC[c].setup (Svf::Type::bell, 1800.0f, 0.8f, 4.0f, fs);
                lowA[c].setCutoff (35.0f, fs);
                lowB[c].setCutoff (35.0f, fs);
                break;
            case tape:
                bandA[c].setup (Svf::Type::highPass, 110.0f, 0.707f, 0.0f, fs);
                bandB[c].setup (Svf::Type::highShelf, 4500.0f, 0.707f, 3.0f, fs);
                bandC[c].setup (Svf::Type::lowPass, 16000.0f, 0.707f, 0.0f, fs);
                break;
            case cassette:
                bandA[c].setup (Svf::Type::highPass, 260.0f, 0.707f, 0.0f, fs);
                bandB[c].setup (Svf::Type::bell, 5500.0f, 0.7f, 6.0f, fs);
                bandC[c].setup (Svf::Type::lowPass, 12500.0f, 0.707f, 0.0f, fs);
                break;
            case vhs:
                bandA[c].setup (Svf::Type::highPass, 150.0f, 0.707f, 0.0f, fs);
                bandB[c].setup (Svf::Type::lowPass, 8500.0f, 0.8f, 0.0f, fs);
                bandC[c].setup (Svf::Type::bandPass, 2200.0f, 1.2f, 0.0f, fs);
                break;
            case buzz:
                bandA[c].setup (Svf::Type::highPass, 1400.0f, 0.707f, 0.0f, fs);
                bandB[c].setup (Svf::Type::lowPass, 9000.0f, 0.707f, 0.0f, fs);
                break;
            case fuzz:
                bandA[c].setup (Svf::Type::highPass, 1100.0f, 0.707f, 0.0f, fs);
                bandB[c].setup (Svf::Type::lowPass, 7000.0f, 0.707f, 0.0f, fs);
                lowA[c].setCutoff (320.0f, fs);
                lowB[c].setCutoff (2400.0f, fs);
                break;
            case room:
                bandA[c].setup (Svf::Type::lowPass, 5200.0f, 0.707f, 0.0f, fs);
                bandB[c].setup (Svf::Type::highPass, 25.0f, 0.707f, 0.0f, fs);
                bandC[c].setup (Svf::Type::bandPass, 75.0f, 1.4f, 0.0f, fs);
                break;
            case radio:
                bandA[c].setup (Svf::Type::highPass, 300.0f, 0.707f, 0.0f, fs);
                bandB[c].setup (Svf::Type::lowPass, 3500.0f, 0.9f, 0.0f, fs);
                break;
            case transmission:
                bandA[c].setup (Svf::Type::highPass, 500.0f, 0.8f, 0.0f, fs);
                bandB[c].setup (Svf::Type::lowPass, 2600.0f, 1.3f, 0.0f, fs);
                bandC[c].setup (Svf::Type::bell, 1300.0f, 1.0f, 5.0f, fs);
                break;
            case bit8:
                bandA[c].setup (Svf::Type::lowPass, 15000.0f, 0.707f, 0.0f, fs);
                break;
            case brown:
                bandA[c].setup (Svf::Type::highPass, 18.0f, 0.707f, 0.0f, fs);
                break;
            default:
                break;
        }
    }
}

float NoiseSynth::typeGain (int type) noexcept
{
    // Calibrated so that every type renders at roughly -20 dBFS RMS (see tests/TestMain.cpp, "noise calibration").
    static constexpr float gains[numTypes] {
        0.65f,   // vinyl (crackle: calibrated lower, its peaks are what you hear)
        0.448f,  // shellac
        0.0943f, // tape
        0.0907f, // cassette
        0.170f,  // vhs
        0.0913f, // hum50
        0.0913f, // hum60
        0.283f,  // buzz
        0.389f,  // fuzz
        0.832f,  // room
        0.252f,  // radio
        0.271f,  // transmission
        0.1015f, // bit8
        0.100f,  // white
        0.300f,  // pink
        0.207f   // brown
    };
    return gains[juce::jlimit (0, numTypes - 1, type)];
}

void NoiseSynth::render (int type, float* left, float* right, int n, float activity) noexcept
{
    type = juce::jlimit (0, numTypes - 1, type);

    if (type != lastType)
    {
        lastType = type;
        reset();
        configureFilters();
    }

    switch (type)
    {
        case vinyl:        renderVinyl (left, right, n, activity, false); break;
        case shellac:      renderVinyl (left, right, n, activity, true); break;
        case tape:         renderTape (left, right, n, activity, false); break;
        case cassette:     renderTape (left, right, n, activity, true); break;
        case vhs:          renderVhs (left, right, n, activity); break;
        case hum50:        renderHum (left, right, n, activity, 50.0f); break;
        case hum60:        renderHum (left, right, n, activity, 60.0f); break;
        case buzz:         renderBuzz (left, right, n, activity); break;
        case fuzz:         renderFuzz (left, right, n, activity); break;
        case room:         renderRoom (left, right, n, activity); break;
        case radio:        renderRadio (left, right, n, activity); break;
        case transmission: renderTransmission (left, right, n, activity); break;
        case bit8:         renderBit8 (left, right, n, activity); break;
        default:           renderColoured (left, right, n, type); break;
    }

    const auto g = typeGain (type);

    for (int i = 0; i < n; ++i)
    {
        left[i] *= g;
        right[i] *= g;
    }
}

//======================================================================================================================
void NoiseSynth::renderVinyl (float* l, float* r, int n, float activity, bool shellacMode) noexcept
{
    auto& crackle = shellacMode ? shellacCrackle : vinylCrackle;
    const auto rpmHz = shellacMode ? 1.3f : 0.5556f;
    const auto surfaceLevel = shellacMode ? 0.09f : 0.012f;
    const auto swishDepth = shellacMode ? 0.5f : 0.35f;
    const auto rumbleLevel = shellacMode ? 0.6f : 0.4f;
    const auto density = std::max (0.15f, 1.0f + 1.6f * activity);

    for (int i = 0; i < n; ++i)
    {
        phaseA += rpmHz * invFs;
        if (phaseA >= 1.0)
            phaseA -= 1.0;

        const auto rot = static_cast<float> (phaseA) * twoPi;
        const auto swish = 1.0f + swishDepth * (0.7f * std::sin (rot) + 0.3f * std::sin (2.0f * rot + 0.7f));

        float out[2];
        crackle.process (out[0], out[1], density);

        const auto rumbleSource = rng.nextGaussian();

        for (int c = 0; c < 2; ++c)
        {
            auto surface = bandB[c].process (bandA[c].process (rng.nextGaussian()));
            if (shellacMode)
                surface = bandC[c].process (surface);

            const auto rumble = lowB[c].processLP (lowA[c].processLP (rumbleSource)) * rumbleLevel * 6.0f;
            out[c] += surface * surfaceLevel * swish + rumble;
        }

        l[i] = out[0];
        r[i] = out[1];
    }
}

void NoiseSynth::renderTape (float* l, float* r, int n, float activity, bool cassetteMode) noexcept
{
    const auto modDepth = (cassetteMode ? 0.14f : 0.08f) * (1.0f + std::max (0.0f, activity));

    for (int i = 0; i < n; ++i)
    {
        // Modulation noise: the hiss level breathes slightly with the tape transport.
        const auto mod = 1.0f + modDepth * wander[3].next();
        float out[2];

        for (int c = 0; c < 2; ++c)
            out[c] = bandC[c].process (bandB[c].process (bandA[c].process (rng.nextGaussian()))) * mod;

        l[i] = out[0];
        r[i] = out[1];
    }
}

void NoiseSynth::renderVhs (float* l, float* r, int n, float activity) noexcept
{
    const auto whineHz = 15734.26f;
    const auto hasWhine = whineHz < 0.45f * static_cast<float> (fs);
    const auto frameHz = 59.94f;
    const auto burstDecayCoeff = std::exp (-1.0f / (static_cast<float> (fs) * 0.0016f));
    const auto trackingRate = 0.12f * (1.0f + 2.0f * std::max (0.0f, activity));

    for (int i = 0; i < n; ++i)
    {
        phaseA += whineHz * invFs;
        if (phaseA >= 1.0)
            phaseA -= 1.0;

        phaseB += frameHz * invFs;
        if (phaseB >= 1.0)
        {
            phaseB -= 1.0;
            burstEnv = 1.0f; // head switching
        }

        // Occasional tracking errors: the hiss swells for a moment.
        if (gateCounter <= 0)
        {
            if (rng.nextFloat() < trackingRate * invFs)
            {
                gateTarget = 1.0f;
                gateCounter = static_cast<int> (fs * (0.12 + 0.3 * rng.nextFloat()));
            }
            else
            {
                gateTarget = 0.0f;
            }
        }
        else
        {
            --gateCounter;
        }

        gateEnv += (gateTarget - gateEnv) * 0.002f;

        const auto common = rng.nextGaussian();
        const auto whine = hasWhine ? 0.035f * std::sin (static_cast<float> (phaseA) * twoPi) : 0.0f;
        float out[2];

        for (int c = 0; c < 2; ++c)
        {
            const auto source = 0.85f * common + 0.35f * rng.nextGaussian();
            const auto hiss = bandB[c].process (bandA[c].process (source)) * (1.0f + 1.6f * gateEnv);
            const auto head = bandC[c].process (burstEnv * rng.nextBipolar()) * 0.9f;
            out[c] = hiss + head + whine;
        }

        burstEnv *= burstDecayCoeff;
        l[i] = out[0];
        r[i] = out[1];
    }
}

void NoiseSynth::renderHum (float* l, float* r, int n, float activity, float mainsHz) noexcept
{
    static constexpr float harmonics[] { 1.0f, 0.55f, 0.42f, 0.22f, 0.18f, 0.10f, 0.08f, 0.05f };
    const auto wanderDepth = 0.1f + 0.25f * std::max (0.0f, activity);

    for (int i = 0; i < n; ++i)
    {
        const auto drift = wander[1].next();
        phaseA += mainsHz * (1.0f + 0.002f * drift) * invFs;
        if (phaseA >= 1.0)
            phaseA -= 1.0;

        const auto theta = static_cast<float> (phaseA) * twoPi;
        const auto s1 = std::sin (theta);
        const auto twoCos = 2.0f * std::cos (theta);
        auto sPrev = 0.0f, sCur = s1, sum = 0.0f;

        for (auto a : harmonics)
        {
            sum += a * sCur;
            const auto sNext = twoCos * sCur - sPrev;
            sPrev = sCur;
            sCur = sNext;
        }

        const auto level = 1.0f + wanderDepth * wander[0].next();
        const auto v = sum * level;
        l[i] = v;
        r[i] = v;
    }
}

void NoiseSynth::renderBuzz (float* l, float* r, int n, float activity) noexcept
{
    const auto wanderDepth = 0.12f + 0.3f * std::max (0.0f, activity);

    for (int i = 0; i < n; ++i)
    {
        phaseA += 60.0f * invFs;
        if (phaseA >= 1.0)
            phaseA -= 1.0;

        const auto s = std::sin (static_cast<float> (phaseA) * twoPi);
        const auto square = std::tanh (5.0f * s);
        const auto rectified = std::abs (s) - 0.6366f;
        const auto edges = bandB[0].process (bandA[0].process (square));
        const auto level = 1.0f + wanderDepth * wander[0].next();
        const auto v = dc[0].process ((0.3f * square + 0.35f * rectified + 2.2f * edges) * level);
        l[i] = v;
        r[i] = v;
    }
}

void NoiseSynth::renderFuzz (float* l, float* r, int n, float activity) noexcept
{
    const auto sputterRate = 9.0f * (1.0f + 1.5f * std::max (0.0f, activity));

    for (int i = 0; i < n; ++i)
    {
        if (--gateCounter <= 0)
        {
            const auto on = rng.nextFloat() < 0.45f;
            gateTarget = on ? 1.0f : 0.0f;
            const auto meanSeconds = on ? 0.03f : 1.0f / sputterRate;
            gateCounter = 1 + static_cast<int> (-std::log (std::max (1.0e-6f, rng.nextFloat())) * meanSeconds * static_cast<float> (fs));
        }

        gateEnv += (gateTarget - gateEnv) * 0.02f;

        const auto lf = lowA[0].processLP (rng.nextGaussian()) * 8.0f;
        const auto sputter = lowB[0].processLP (std::tanh (lf)) * gateEnv;
        const auto hissL = bandB[0].process (bandA[0].process (rng.nextGaussian()));
        const auto hissR = bandB[1].process (bandA[1].process (rng.nextGaussian()));
        l[i] = 0.35f * hissL + 0.8f * sputter;
        r[i] = 0.35f * hissR + 0.8f * sputter;
    }
}

void NoiseSynth::renderRoom (float* l, float* r, int n, float activity) noexcept
{
    const auto rumbleDepth = 0.5f + 0.4f * std::max (0.0f, activity);

    for (int i = 0; i < n; ++i)
    {
        const auto p0 = pinkFilters[0].process (rng.nextBipolar());
        const auto p1 = pinkFilters[1].process (rng.nextBipolar());
        const auto hvac = (1.0f + rumbleDepth * wander[2].next());
        float out[2];
        const float src[2] { 0.8f * p0 + 0.2f * p1, 0.2f * p0 + 0.8f * p1 };

        for (int c = 0; c < 2; ++c)
        {
            const auto air = bandB[c].process (bandA[c].process (src[c]));
            const auto rumble = bandC[c].process (rng.nextGaussian()) * 0.05f * hvac;
            out[c] = air + rumble;
        }

        l[i] = out[0];
        r[i] = out[1];
    }
}

void NoiseSynth::renderRadio (float* l, float* r, int n, float activity) noexcept
{
    const auto density = std::max (0.2f, 1.0f + 1.5f * activity);

    for (int i = 0; i < n; ++i)
    {
        const auto fading = 1.0f + 0.45f * wander[0].next();
        const auto whistleHz = 1900.0f + 1300.0f * wander[1].next();
        phaseA += whistleHz * invFs;
        if (phaseA >= 1.0)
            phaseA -= 1.0;

        const auto whistleLevel = 0.12f * std::max (0.0f, wander[2].next());
        const auto staticNoise = bandB[0].process (bandA[0].process (rng.nextGaussian())) * fading;
        float cl, cr;
        radioCrackle.process (cl, cr, density);

        const auto v = staticNoise + 1.4f * cl + whistleLevel * std::sin (static_cast<float> (phaseA) * twoPi);
        l[i] = v;
        r[i] = v;
    }
}

void NoiseSynth::renderTransmission (float* l, float* r, int n, float activity) noexcept
{
    const auto burstsPerSecond = 0.12f * (1.0f + 2.0f * std::max (0.0f, activity));

    for (int i = 0; i < n; ++i)
    {
        // Squelch bursts, sometimes announced by a short two-tone beep.
        if (gateCounter > 0)
        {
            --gateCounter;
            gateTarget = 1.0f;
        }
        else
        {
            gateTarget = 0.0f;

            if (rng.nextFloat() < burstsPerSecond * invFs)
            {
                gateCounter = static_cast<int> (fs * (0.12 + 0.18 * rng.nextFloat()));

                if (rng.nextFloat() < 0.35f)
                    beepCounter = static_cast<int> (fs * 0.25);
            }
        }

        gateEnv += (gateTarget - gateEnv) * (gateTarget > gateEnv ? 0.01f : 0.0015f);

        auto beep = 0.0f;
        if (beepCounter > 0)
        {
            --beepCounter;
            beepPhase += 2525.0 / fs;
            if (beepPhase >= 1.0)
                beepPhase -= 1.0;
            beepEnv += (1.0f - beepEnv) * 0.01f;
        }
        else
        {
            beepEnv *= 0.995f;
        }

        if (beepEnv > 1.0e-4f)
            beep = 0.25f * beepEnv * std::sin (static_cast<float> (beepPhase) * twoPi);

        const auto band = bandC[0].process (bandB[0].process (bandA[0].process (rng.nextGaussian())));
        const auto staticNoise = std::tanh (2.5f * band * (1.0f + 2.5f * gateEnv)) * 0.5f;
        const auto v = staticNoise + beep;
        l[i] = v;
        r[i] = v;
    }
}

void NoiseSynth::renderBit8 (float* l, float* r, int n, float activity) noexcept
{
    const auto clockHz = 7200.0f * (1.0f + 0.35f * activity);
    const auto inc = clockHz * invFs;

    for (int i = 0; i < n; ++i)
    {
        clockPhase += inc;

        if (clockPhase >= 1.0f)
        {
            clockPhase -= 1.0f;
            const auto bit = (lfsr ^ (lfsr >> 1u)) & 1u;
            lfsr = (lfsr >> 1u) | (bit << 14u);
            heldL = (lfsr & 1u) != 0 ? 1.0f : -1.0f;
            heldR = (lfsr & 0x80u) != 0 ? 1.0f : -1.0f;
        }

        l[i] = dc[0].process (bandA[0].process (heldL));
        r[i] = dc[1].process (bandA[1].process (heldR));
    }
}

void NoiseSynth::renderColoured (float* l, float* r, int n, int type) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        float out[2];

        for (int c = 0; c < 2; ++c)
        {
            const auto w = rng.nextGaussian();

            if (type == white)
            {
                out[c] = w;
            }
            else if (type == pink)
            {
                out[c] = pinkFilters[c].process (w);
            }
            else
            {
                brownState[c] = 0.997f * brownState[c] + 0.05f * w;
                out[c] = bandA[c].process (brownState[c]);
            }
        }

        l[i] = out[0];
        r[i] = out[1];
    }
}

} // namespace afterglow::dsp
