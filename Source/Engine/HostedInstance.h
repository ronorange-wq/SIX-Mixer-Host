#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DelayRing.h"

namespace six
{
// One external effect instance hosted by SIX.
// Created, prepared and destroyed on the message thread; only process() runs on the audio thread.
class HostedInstance
{
public:
    HostedInstance (std::unique_ptr<juce::AudioPluginInstance> p, const juce::PluginDescription& d, juce::uint64 placementUid);
    ~HostedInstance();

    juce::AudioPluginInstance& plugin() noexcept { return *instance; }
    const juce::PluginDescription& description() const noexcept { return desc; }
    juce::uint64 placement() const noexcept { return placementUid; }
    void setPlacement (juce::uint64 u) noexcept { placementUid = u; }

    // Picks stereo->stereo, then mono->stereo, then mono->mono (keeps the stereo tail of
    // mono-in/stereo-out effects such as reverbs). Must be called before prepare().
    void configureLayout();
    void prepare (double sampleRate, int maxBlock, bool offline = false);
    bool isPrepared() const noexcept { return preparedFlag; }

    int mainInputs() const noexcept  { return numIn; }
    int mainOutputs() const noexcept { return numOut; }
    juce::String layoutName() const;
    int latency() const noexcept { return instance->getLatencySamples(); }

    // Audio thread. in/out may alias. `extraDelay` aligns this instance's output with a
    // higher-latency sibling during cross-fades.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, int extraDelay) noexcept;

    // ---- message-thread bookkeeping
    juce::MemoryBlock getState();
    static juce::uint64 hashState (const juce::MemoryBlock& m);

    std::atomic<bool> dirty { false };      // unmapped parameter changed since cleanHash
    juce::uint64 cleanHash = 0;             // hash of the state it was loaded/saved with
    int audioRefs = 0;                      // pointers currently held by the audio engine
    juce::uint32 id = 0;                    // registry id
    std::unique_ptr<juce::AudioProcessorListener> listener;

private:
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::PluginDescription desc;
    juce::uint64 placementUid = 0;
    int numIn = 2, numOut = 2, numBufferChannels = 2;
    bool preparedFlag = false;
    int maxBlockSize = 0;
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midi;
    DelayRing align;

    JUCE_DECLARE_NON_COPYABLE (HostedInstance)
};
}
