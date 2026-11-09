#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Core/Params.h"
#include "Engine/AudioEngine.h"
#include "Host/HostManager.h"

class SIXAudioProcessor : public juce::AudioProcessor
{
public:
    SIXAudioProcessor();
    ~SIXAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SIX"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    six::ParamRefs params;
    six::AudioEngine engine { params };
    std::unique_ptr<six::HostManager> host;

    // UI state saved with the set (not automatable)
    int selectedChannel = 0;
    int presetPage = 0;

private:
    double lastBpm = 120.0;
    int lastNum = 4, lastDen = 4;
    void applyState (const juce::XmlElement& xml);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SIXAudioProcessor)
};
