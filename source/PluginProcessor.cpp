#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace afterglow
{
AfterglowProcessor::AfterglowProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, &undoManager, "AfterglowState", createParameterLayout())
{
    cacheParameterPointers();

    // The oversampling latency does not depend on the sample rate, so it can be reported before prepareToPlay.
    for (int order = 0; order < 4; ++order)
        latencyForOrder[static_cast<size_t> (order)] = dsp::DistortModule::latencyForOrder (order, 44100.0, 512);

    state.addParameterListener (ParamIDs::quality, this);
    requestedOrder.store (qualityToOversamplingOrder (static_cast<int> (p.quality->load())));
    setLatencySamples (latencyForOrder[static_cast<size_t> (requestedOrder.load())]);

    presetManager = std::make_unique<PresetManager> (state, &undoManager);
    presetManager->loadStartupPresetIfAny();
    undoManager.clearUndoHistory();
}

AfterglowProcessor::~AfterglowProcessor()
{
    state.removeParameterListener (ParamIDs::quality, this);
}

void AfterglowProcessor::cacheParameterPointers()
{
    using namespace ParamIDs;
    auto get = [this] (const char* id)
    {
        auto* v = state.getRawParameterValue (id);
        jassert (v != nullptr);
        return v;
    };

    p.magnitude = get (magnitude); p.mix = get (mix); p.limiter = get (limiter); p.quality = get (quality);
    p.noiseOn = get (noiseOn); p.noiseAmount = get (noiseAmount); p.noiseType = get (noiseType); p.noiseTone = get (noiseTone);
    p.noiseFollow = get (noiseFollow); p.noiseDuck = get (noiseDuck); p.noisePost = get (noisePost); p.noiseFlux = get (noiseFlux);
    p.wobbleOn = get (wobbleOn); p.wobbleAmount = get (wobbleAmount); p.wobbleBalance = get (wobbleBalance); p.wobbleWowRate = get (wobbleWowRate);
    p.wobbleFlutterRate = get (wobbleFlutterRate); p.wobbleSync = get (wobbleSync); p.wobbleDivision = get (wobbleDivision); p.wobbleStereo = get (wobbleStereo);
    p.wobbleMix = get (wobbleMix); p.wobbleFlux = get (wobbleFlux);
    p.distortOn = get (distortOn); p.distortAmount = get (distortAmount); p.distortType = get (distortType); p.distortFocusLow = get (distortFocusLow);
    p.distortFocusHigh = get (distortFocusHigh); p.distortTone = get (distortTone); p.distortMix = get (distortMix); p.distortFlux = get (distortFlux);
    p.digitalOn = get (digitalOn); p.digitalAmount = get (digitalAmount); p.digitalBalance = get (digitalBalance); p.digitalSmooth = get (digitalSmooth);
    p.digitalFocusLow = get (digitalFocusLow); p.digitalFocusHigh = get (digitalFocusHigh); p.digitalCut = get (digitalCut); p.digitalCompand = get (digitalCompand);
    p.digitalMix = get (digitalMix); p.digitalFlux = get (digitalFlux);
    p.spaceOn = get (spaceOn); p.spaceAmount = get (spaceAmount); p.spaceType = get (spaceType); p.spaceDecay = get (spaceDecay);
    p.spacePreDelay = get (spacePreDelay); p.spaceFocusLow = get (spaceFocusLow); p.spaceFocusHigh = get (spaceFocusHigh); p.spaceStereo = get (spaceStereo);
    p.spaceFlux = get (spaceFlux);
    p.magneticOn = get (magneticOn); p.magneticAmount = get (magneticAmount); p.magneticBalance = get (magneticBalance); p.magneticRate = get (magneticRate);
    p.magneticSync = get (magneticSync); p.magneticDivision = get (magneticDivision); p.magneticDropouts = get (magneticDropouts); p.magneticStereo = get (magneticStereo);
    p.magneticFlux = get (magneticFlux);
    p.inGain = get (inGain); p.eqOn = get (eqOn); p.lowCut = get (lowCut); p.lowCutHard = get (lowCutHard);
    p.highCut = get (highCut); p.highCutHard = get (highCutHard); p.tone = get (tone); p.toneMode = get (toneMode);
    p.width = get (width); p.outGain = get (outGain);
}

void AfterglowProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (parameterID == ParamIDs::quality)
    {
        const auto order = qualityToOversamplingOrder (juce::roundToInt (newValue));
        requestedOrder.store (order);
        setLatencySamples (latencyForOrder[static_cast<size_t> (order)]);
    }
}

void AfterglowProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    samplesPerBlock = std::max (1, samplesPerBlock);

    for (int order = 0; order < 4; ++order)
        latencyForOrder[static_cast<size_t> (order)] = dsp::DistortModule::latencyForOrder (order, sampleRate, samplesPerBlock);

    const auto order = qualityToOversamplingOrder (static_cast<int> (p.quality->load()));
    requestedOrder.store (order);
    engine.prepare (sampleRate, samplesPerBlock, order);
    setLatencySamples (latencyForOrder[static_cast<size_t> (order)]);
    monoScratch.setSize (1, samplesPerBlock);
}

void AfterglowProcessor::releaseResources() {}

bool AfterglowProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    // Mono in / stereo out is fine (the stereo effects spread the signal); stereo in / mono out is not.
    return ! (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono());
}

dsp::EngineParams AfterglowProcessor::snapshotParameters (const dsp::TransportInfo& transport) const noexcept
{
    auto pct = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed) * 0.01f; };
    auto val = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };
    auto flag = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed) > 0.5f; };
    auto index = [] (const std::atomic<float>* a) { return static_cast<int> (std::lround (a->load (std::memory_order_relaxed))); };

    const auto beatsPerSecond = std::max (20.0, transport.bpm) / 60.0;
    dsp::EngineParams e;

    e.noise.on = flag (p.noiseOn);
    e.noise.amount = pct (p.noiseAmount);
    e.noise.type = index (p.noiseType);
    e.noise.tone = pct (p.noiseTone);
    e.noise.follow = pct (p.noiseFollow);
    e.noise.duck = pct (p.noiseDuck);
    e.noise.post = flag (p.noisePost);
    e.noise.flux = pct (p.noiseFlux);

    e.wobble.on = flag (p.wobbleOn);
    e.wobble.amount = pct (p.wobbleAmount);
    e.wobble.balance = pct (p.wobbleBalance);
    e.wobble.wowRate = val (p.wobbleWowRate);
    e.wobble.flutterRate = val (p.wobbleFlutterRate);
    if (flag (p.wobbleSync))
    {
        const auto beats = syncDivisionInBeats (index (p.wobbleDivision));
        e.wobble.syncBeats = beats;
        e.wobble.wowRate = static_cast<float> (beatsPerSecond / beats);
    }
    e.wobble.stereo = flag (p.wobbleStereo);
    e.wobble.mix = pct (p.wobbleMix);
    e.wobble.flux = pct (p.wobbleFlux);

    e.distort.on = flag (p.distortOn);
    e.distort.amount = pct (p.distortAmount);
    e.distort.type = index (p.distortType);
    e.distort.focusLow = val (p.distortFocusLow);
    e.distort.focusHigh = val (p.distortFocusHigh);
    e.distort.tone = pct (p.distortTone);
    e.distort.mix = pct (p.distortMix);
    e.distort.flux = pct (p.distortFlux);

    e.digital.on = flag (p.digitalOn);
    e.digital.amount = pct (p.digitalAmount);
    e.digital.balance = pct (p.digitalBalance);
    e.digital.smooth = pct (p.digitalSmooth);
    e.digital.focusLow = val (p.digitalFocusLow);
    e.digital.focusHigh = val (p.digitalFocusHigh);
    e.digital.cut = flag (p.digitalCut);
    e.digital.compand = flag (p.digitalCompand);
    e.digital.mix = pct (p.digitalMix);
    e.digital.flux = pct (p.digitalFlux);

    e.space.on = flag (p.spaceOn);
    e.space.amount = pct (p.spaceAmount);
    e.space.type = index (p.spaceType);
    e.space.decay = pct (p.spaceDecay);
    e.space.preDelayMs = val (p.spacePreDelay);
    e.space.focusLow = val (p.spaceFocusLow);
    e.space.focusHigh = val (p.spaceFocusHigh);
    e.space.stereo = flag (p.spaceStereo);
    e.space.flux = pct (p.spaceFlux);

    e.magnetic.on = flag (p.magneticOn);
    e.magnetic.amount = pct (p.magneticAmount);
    e.magnetic.balance = pct (p.magneticBalance);
    e.magnetic.rate = val (p.magneticRate);
    if (flag (p.magneticSync))
    {
        const auto beats = syncDivisionInBeats (index (p.magneticDivision));
        e.magnetic.syncBeats = beats;
        e.magnetic.rate = static_cast<float> (beatsPerSecond / beats);
    }
    e.magnetic.dropouts = pct (p.magneticDropouts);
    e.magnetic.stereo = flag (p.magneticStereo);
    e.magnetic.flux = pct (p.magneticFlux);

    e.master.magnitude = pct (p.magnitude);
    e.master.inGainDb = val (p.inGain);
    e.master.eqOn = flag (p.eqOn);
    e.master.lowCut = val (p.lowCut);
    e.master.lowCutHard = flag (p.lowCutHard);
    e.master.highCut = val (p.highCut);
    e.master.highCutHard = flag (p.highCutHard);
    e.master.tone = pct (p.tone);
    e.master.toneMode = index (p.toneMode);
    e.master.width = pct (p.width);
    e.master.outGainDb = val (p.outGain);
    e.master.mix = pct (p.mix);
    e.master.limiter = flag (p.limiter);
    e.master.quality = index (p.quality);
    return e;
}

void AfterglowProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numIn = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();
    const auto n = buffer.getNumSamples();

    for (auto ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, n);

    if (n == 0 || buffer.getNumChannels() == 0)
        return;

    dsp::TransportInfo transport;
    if (auto* host = getPlayHead())
    {
        if (const auto position = host->getPosition())
        {
            transport.isPlaying = position->getIsPlaying();
            if (const auto bpm = position->getBpm())
                transport.bpm = *bpm;
            if (const auto ppq = position->getPpqPosition())
            {
                transport.hasPpq = true;
                transport.ppqPosition = *ppq;
            }
        }
    }

    const auto order = requestedOrder.load();
    if (order != engine.getOversamplingOrder())
        engine.setOversamplingOrder (order);

    const auto params = snapshotParameters (transport);
    auto* left = buffer.getWritePointer (0);

    if (buffer.getNumChannels() >= 2 && numOut >= 2)
    {
        auto* right = buffer.getWritePointer (1);
        if (numIn == 1)
            std::copy_n (left, n, right);
        engine.process (left, right, n, params, transport);
        return;
    }

    // Mono: run the stereo engine with a scratch right channel and keep the left output.
    const auto chunk = std::max (1, monoScratch.getNumSamples());
    for (int start = 0; start < n; start += chunk)
    {
        const auto len = std::min (chunk, n - start);
        auto* scratch = monoScratch.getWritePointer (0);
        std::copy_n (left + start, len, scratch);
        engine.process (left + start, scratch, len, params, transport);
    }
}

juce::AudioProcessorEditor* AfterglowProcessor::createEditor()
{
    return new AfterglowEditor (*this);
}

float AfterglowProcessor::getUiScale() const
{
    return static_cast<float> (state.state.getProperty (uiScaleProperty, 1.0f));
}

void AfterglowProcessor::setUiScale (float scale)
{
    state.state.setProperty (uiScaleProperty, scale, nullptr);
}

void AfterglowProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto copy = state.copyState();
    copy.setProperty ("presetName", presetManager->getCurrentPresetName(), nullptr);
    copy.setProperty ("presetDirty", presetManager->isDirty(), nullptr);
    copy.setProperty ("version", AFTERGLOW_VERSION_STRING, nullptr);

    if (auto xml = copy.createXml())
        copyXmlToBinary (*xml, destData);
}

void AfterglowProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    const auto presetName = tree.getProperty ("presetName").toString();
    const auto dirty = static_cast<bool> (tree.getProperty ("presetDirty", false));

    presetManager->runWhileLoading ([&] { state.replaceState (tree); });
    presetManager->setCurrentPresetName (presetName, dirty);
    undoManager.clearUndoHistory();
}

} // namespace afterglow

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new afterglow::AfterglowProcessor();
}
