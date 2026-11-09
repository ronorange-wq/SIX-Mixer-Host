#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Core/Controls.h"
#include "../Core/Params.h"
#include "../Core/Routing.h"
#include "DelayRing.h"
#include "HostedInstance.h"

namespace six
{
// ---------------------------------------------------------------------------------
// Messages between the message thread (MT) and the audio thread (AT).
// The AT owns the live slot assignment; the MT owns all HostedInstance objects and only
// deletes one after the AT reports that it released it.
// ---------------------------------------------------------------------------------
struct PreparedSlot
{
    enum Action : uint8_t { keep, toEmpty, toInstance };
    Action action = keep;
    HostedInstance* inst = nullptr;
    juce::uint64 uid = 0;
};

// Plug-in instances prepared ahead of time (on the MT) for a morph to one preset, so a
// timeline-triggered morph can start in the very block of the automation event.
struct PreparedSet
{
    int preset = -1;
    std::array<PreparedSlot, (size_t) (kNumChannels * kNumSlots)> slots {};
    bool complete = true;
};

struct EngineCommand
{
    enum Type : uint8_t { setSlot, moveSlot, setPresetMix, clearPresetMix, setPrepared, retrigger, setMap, restoreHolds, releaseAllHolds, clearAllSlots };
    Type type = setSlot;
    int a = 0, b = 0, c = 0;                 // channel/slot/... (see users)
    HostedInstance* inst = nullptr;
    juce::uint64 uid = 0;
    PreparedSet* set = nullptr;
    ControlValues values {};
    std::array<bool, (size_t) kNumControls> mask {};
};

struct EngineNotice
{
    enum Type : uint8_t { instanceReleased, slotChanged, preparedReleased, morphStarted, morphFinished, morphNeedsPlugins, routingRejected };
    Type type = instanceReleased;
    int a = 0, b = 0;
    HostedInstance* inst = nullptr;
    juce::uint64 uid = 0;
    PreparedSet* set = nullptr;
    bool flag = false;
};

struct BlockContext
{
    double bpm = 120.0;
    int timeSigNum = 4, timeSigDen = 4;
    bool offline = false;
};

class AudioEngine
{
public:
    explicit AudioEngine (ParamRefs& p);
    ~AudioEngine();

    void prepare (double sampleRate, int maxBlock);
    void process (juce::AudioBuffer<float>& io, int numInputChannels, const BlockContext& ctx) noexcept;

    // ---- MT side
    bool post (const EngineCommand& c);
    bool popNotice (EngineNotice& n);
    double getSampleRate() const noexcept { return sampleRate; }
    int getMaxBlock() const noexcept { return maxBlock; }

    // ---- published to MT/UI (lock-free)
    std::array<std::atomic<float>, (size_t) kNumControls> effective;        // audible control values
    std::array<std::atomic<bool>,  (size_t) kNumControls> held;             // value held by a morph (param not in charge)
    std::array<std::atomic<float>, (size_t) kNumControls> seenParam;        // param value the engine last processed
    std::array<std::atomic<float>, (size_t) kNumChannels> peakL, peakR;
    std::array<std::atomic<float>, (size_t) kNumMaps> lastPushedMap;        // echo guard for Learn/Map
    std::atomic<int> activePreset { -1 };
    std::atomic<int> morphTarget { -1 };
    std::atomic<float> morphProgress { 1.0f };
    std::atomic<int> neededLatency { 0 };
    std::atomic<juce::uint32> routingRejects { 0 };
    std::atomic<int> processedBlocks { 0 };

    // For tests / diagnostics (only valid while the audio thread is stopped)
    HostedInstance* debugSlot (int ch, int s) const { auto& sl = chans[(size_t) ch].slots[(size_t) s]; return sl.fading ? sl.next : sl.cur; }
    std::atomic<int> slotLatency[kNumChannels][kNumSlots] {};

private:
    struct Ramp
    {
        float cur = 0.0f, tgt = 0.0f, step = 0.001f;
        void snap() noexcept { cur = tgt; }
        void advance (int n, float& s, float& e) noexcept
        {
            s = cur;
            const float d = tgt - cur, mx = step * (float) n;
            cur = std::abs (d) <= mx ? tgt : cur + (d > 0 ? mx : -mx);
            e = cur;
        }
        bool live() const noexcept { return cur != 0.0f || tgt != 0.0f; }
    };

    struct SlotState
    {
        HostedInstance* cur = nullptr;
        juce::uint64 curUid = 0;
        HostedInstance* next = nullptr;
        juce::uint64 nextUid = 0;
        bool fading = false;
        double fadePos = 0.0, fadeInc = 0.0;
        Ramp wet;
        DelayRing dry;
        int latency = 0;
    };

    struct ChannelState
    {
        std::array<SlotState, (size_t) kNumSlots> slots;
        DelayRing preRing, postRing;
        juce::AudioBuffer<float> work, tmpA, tmpB, sendBuf;
        Ramp fader, sendIn, mono, liveIn, pan, law, duck;
        std::array<Ramp, (size_t) kNumOutChoices> out;   // [0] master, [1+d] -> channel d
        int arrival = 0, chainLatency = 0, outLatency = 0;
        bool monoDetected = false;
        int monoBlocks = 0;
        int pendingMoveFrom = -1, pendingMoveTo = -1;
    };

    void handleCommands() noexcept;
    void notify (const EngineNotice& n) noexcept;
    void flushPendingNotices() noexcept;
    void startSlotFade (int ch, int slot, HostedInstance* inst, juce::uint64 uid, int fadeSamples) noexcept;
    void startMorph (int preset, const BlockContext& ctx) noexcept;
    void computeEffective (int n, const BlockContext& ctx) noexcept;
    void updateRouting() noexcept;
    void applyMaps() noexcept;
    void processChunk (float* outL, float* outR, const float* inL, const float* inR, int n, const BlockContext& ctx) noexcept;
    void processSlot (ChannelState& c, int ch, int s, int n) noexcept;
    void applyPendingMove (ChannelState& c, int ch) noexcept;

    ParamRefs& params;
    double sampleRate = 44100.0;
    int maxBlock = 512;
    bool prepared = false, firstBlock = true;
    BlockContext lastCtx;

    std::array<ChannelState, (size_t) kNumChannels> chans;
    std::array<std::array<Ramp, (size_t) kNumChannels>, (size_t) kNumChannels> sendGain, sendPost;
    DelayRing liveRing;
    int totalLatency = 0;
    std::array<int, (size_t) kNumChannels> order { 0, 1, 2, 3, 4, 5 };

    // control state
    ControlValues lastParam {}, holdVal {}, eff {};
    std::array<bool, (size_t) kNumControls> hold {}, morphing {};
    int lastPresetTarget = 0;

    // morph
    bool morphActive = false;
    int morphPreset = -1;
    double morphPos = 0.0, morphLen = 1.0;
    bool morphConflict = false;
    ControlValues morphFrom {}, morphTo {};
    float fromOut[kNumChannels][kNumOutChoices] {};
    float toOut[kNumChannels][kNumOutChoices] {};
    float morphOut[kNumChannels][kNumOutChoices] {};
    bool morphOutValid[kNumChannels] {};

    // routing
    float outTarget[kNumChannels][kNumOutChoices] {};
    RouteGraph lastValid;
    float lastValidOut[kNumChannels][kNumOutChoices] {};
    bool rejectedLastBlock = false;

    // presets
    std::array<ControlValues, (size_t) kNumPresets> presetMix {};
    std::array<bool, (size_t) kNumPresets> presetValid {};
    std::array<PreparedSet*, (size_t) kNumPresets> prepared_ {};

    // maps
    struct MapBinding { juce::uint64 uid = 0; int paramIndex = -1; HostedInstance* lastInst = nullptr; float lastValue = -1.0f; };
    std::array<MapBinding, (size_t) kNumMaps> maps {};

    // FIFOs
    juce::AbstractFifo cmdFifo { 1024 };
    std::vector<EngineCommand> cmdBuf;
    juce::CriticalSection producerLock;
    juce::AbstractFifo noticeFifo { 2048 };
    std::vector<EngineNotice> noticeBuf;
    std::array<EngineNotice, 256> pendingNotices {};
    int numPendingNotices = 0;

    JUCE_DECLARE_NON_COPYABLE (AudioEngine)
};
}
