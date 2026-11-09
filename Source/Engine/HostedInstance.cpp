#include "HostedInstance.h"

namespace six
{
using namespace juce;

HostedInstance::HostedInstance (std::unique_ptr<AudioPluginInstance> p, const PluginDescription& d, uint64 uid)
    : instance (std::move (p)), desc (d), placementUid (uid)
{
    midi.ensureSize (256);
}

HostedInstance::~HostedInstance()
{
    if (instance != nullptr)
    {
        if (listener != nullptr) instance->removeListener (listener.get());
        if (auto* ed = instance->getActiveEditor())
            delete ed;
        if (preparedFlag) instance->releaseResources();
    }
}

void HostedInstance::configureLayout()
{
    auto& p = *instance;
    const int nInBuses = p.getBusCount (true);
    const int nOutBuses = p.getBusCount (false);
    if (nOutBuses == 0) { numIn = p.getTotalNumInputChannels(); numOut = 0; return; }

    auto attempt = [&] (const AudioChannelSet& in, const AudioChannelSet& out) -> bool
    {
        auto l = p.getBusesLayout();
        for (int i = 0; i < l.inputBuses.size(); ++i)  l.inputBuses.getReference (i)  = (i == 0 ? in : AudioChannelSet::disabled());
        for (int i = 0; i < l.outputBuses.size(); ++i) l.outputBuses.getReference (i) = (i == 0 ? out : AudioChannelSet::disabled());
        if (nInBuses == 0 && ! in.isDisabled()) return false;
        if (! p.checkBusesLayoutSupported (l)) return false;
        return p.setBusesLayout (l);
    };

    const bool ok = attempt (AudioChannelSet::stereo(), AudioChannelSet::stereo())
                 || attempt (AudioChannelSet::mono(), AudioChannelSet::stereo())
                 || attempt (AudioChannelSet::mono(), AudioChannelSet::mono())
                 || attempt (AudioChannelSet::stereo(), AudioChannelSet::mono());
    ignoreUnused (ok);

    numIn  = nInBuses > 0 ? p.getChannelCountOfBus (true, 0) : 0;
    numOut = p.getChannelCountOfBus (false, 0);
}

String HostedInstance::layoutName() const
{
    auto name = [] (int n) { return n == 1 ? String ("Mono") : n == 2 ? String ("Stereo") : String (n) + "ch"; };
    return name (numIn) + String (CharPointer_UTF8 (" \xe2\x86\x92 ")) + name (numOut);
}

void HostedInstance::prepare (double sampleRate, int maxBlock, bool offline)
{
    if (preparedFlag) instance->releaseResources();
    maxBlockSize = jmax (1, maxBlock);
    instance->setRateAndBufferSizeDetails (sampleRate, maxBlockSize);
    instance->setNonRealtime (offline);
    instance->prepareToPlay (sampleRate, maxBlockSize);
    numBufferChannels = jmax (2, instance->getTotalNumInputChannels(), instance->getTotalNumOutputChannels());
    scratch.setSize (numBufferChannels, maxBlockSize, false, true, false);
    align.allocate (65536 + maxBlockSize);
    preparedFlag = true;
}

void HostedInstance::process (const float* inL, const float* inR, float* outL, float* outR, int n, int extraDelay) noexcept
{
    jassert (n <= maxBlockSize);
    scratch.clear (0, n);

    if (numIn == 1)
    {
        auto* d = scratch.getWritePointer (0);
        for (int i = 0; i < n; ++i) d[i] = 0.5f * (inL[i] + inR[i]);
    }
    else if (numIn >= 2)
    {
        scratch.copyFrom (0, 0, inL, n);
        scratch.copyFrom (1, 0, inR, n);
    }

    AudioBuffer<float> view (scratch.getArrayOfWritePointers(), numBufferChannels, n);
    midi.clear();
    instance->processBlock (view, midi);

    const float* oL = view.getReadPointer (0);
    const float* oR = numOut >= 2 ? view.getReadPointer (1) : oL;
    if (numOut == 0) { oL = inL; oR = inR; }

    if (extraDelay > 0)
    {
        align.write (oL, oR, n);
        align.readTo (outL, outR, n, extraDelay);
    }
    else
    {
        align.write (oL, oR, n);   // keep history so a later alignment change has data
        if (outL != oL) FloatVectorOperations::copy (outL, oL, n);
        if (outR != oR) FloatVectorOperations::copy (outR, oR, n);
    }
}

MemoryBlock HostedInstance::getState()
{
    MemoryBlock m;
    instance->getStateInformation (m);
    return m;
}

uint64 HostedInstance::hashState (const MemoryBlock& m)
{
    uint64 h = 1469598103934665603ull;
    auto* d = static_cast<const uint8*> (m.getData());
    for (size_t i = 0; i < m.getSize(); ++i) { h ^= d[i]; h *= 1099511628211ull; }
    return h ^ (uint64) m.getSize();
}
}
