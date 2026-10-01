#pragma once

#include "PluginProcessor.h"

namespace afterglow
{
class AfterglowEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AfterglowEditor (AfterglowProcessor& p) : AudioProcessorEditor (p) { setSize (400, 300); }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
};
} // namespace afterglow
