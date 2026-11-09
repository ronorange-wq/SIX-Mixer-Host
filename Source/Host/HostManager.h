#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Engine/AudioEngine.h"
#include "PluginCatalog.h"
#include "PresetBank.h"

namespace six
{
class PluginWindowManager;

// Message-thread side of SIX: owns hosted plug-in instances, the slot model shown in the UI,
// the preset bank, Learn/Map bindings, prepared morph targets and the saved state.
class HostManager : public juce::ChangeBroadcaster,
                    private juce::Timer
{
public:
    HostManager (juce::AudioProcessor& owner, AudioEngine& engine, ParamRefs& params);
    ~HostManager() override;

    struct SlotModel
    {
        HostedInstance* inst = nullptr;
        juce::uint64 uid = 0;
        bool missing = false;                 // plug-in not installed: name + state kept
        juce::PluginDescription missingDesc;
        juce::MemoryBlock missingState;

        bool occupied() const noexcept { return inst != nullptr || missing; }
        juce::String displayName() const;
    };

    struct MapModel
    {
        juce::uint64 uid = 0;
        juce::String paramID;
        int paramIndex = -1;
        juce::String paramName, pluginName;
        bool assigned() const noexcept { return uid != 0; }
    };

    juce::AudioPluginFormatManager formats;
    PluginCatalog catalog { formats };
    PresetBank bank;
    std::array<juce::String, (size_t) kNumChannels> channelNames;

    const SlotModel& slot (int ch, int s) const { return model[(size_t) ch][(size_t) s]; }

    // ---- loading (message thread). Returns false + error message on failure.
    bool loadIntoSlot (int ch, int s, const juce::PluginDescription& d, juce::String& error,
                       const juce::MemoryBlock* state = nullptr, juce::uint64 uid = 0, int fadeMs = 30);
    void removeSlot (int ch, int s);
    void moveSlot (int ch, int from, int to);
    int firstEmptySlot (int ch) const;

    // ---- presets
    int saveNewPreset();                      // "+" : next free address, empty name
    bool updatePreset (int i);
    void clearPreset (int i);
    void renamePreset (int i, const juce::String& name);
    void clickPreset (int i);                 // UI click: morph (never changes sound for an empty slot)

    // ---- Learn / Map
    const MapModel& map (int k) const { return maps[(size_t) k]; }
    int findMap (juce::uint64 uid, int paramIndex) const;
    int assignMap (HostedInstance* inst, int paramIndex);   // returns map index or -1
    void clearMap (int k);
    void clearMapsForUid (juce::uint64 uid);
    juce::String mapLocation (int k) const;  // "CH 02 FX1" or "not loaded"
    void armLearn (juce::uint64 uid) { learnUid = uid; learnArmed = true; }
    void cancelLearn() { learnArmed = false; }
    bool isLearnArmed() const noexcept { return learnArmed.load(); }

    // ---- state
    std::unique_ptr<juce::XmlElement> saveState();
    void restoreState (const juce::XmlElement& e);
    void prepareAll (double sampleRate, int blockSize, bool offline);

    // Drains engine notices, maintains prepared morph targets, latency, garbage.
    void pump();

    PluginWindowManager& windows() { return *windowManager; }

    juce::String toastText;
    juce::uint32 toastSerial = 0;
    void toast (const juce::String& s) { toastText = s; ++toastSerial; sendChangeMessage(); }

    ControlValues currentValues() const;      // audible values (incl. morph)
    juce::String verifiedDragInfo;            // last drag source (diagnostics)
    void logDrag (const juce::String& what);  // also appended to "SIX Mixer Host/DragLog.txt"

    // test hooks
    int poolSize() const { return (int) pool.size(); }
    juce::String debugPool() const;
    int preparedInstanceCount() const;
    bool presetPrepared (int p) const { return prep[(size_t) p].sent != nullptr; }

    // instance listener entry points (any thread)
    void onHostedParamChanged (HostedInstance& inst, int index, float value);
    void onHostedGesture (HostedInstance& inst, int index, bool begin);
    void onHostedChanged (HostedInstance& inst, const juce::AudioProcessorListener::ChangeDetails& d);

private:
    void timerCallback() override { pump(); }
    HostedInstance* createInstance (const juce::PluginDescription& d, const juce::MemoryBlock* state, juce::uint64 uid, juce::String& error);
    HostedInstance* wrapInstance (std::unique_ptr<juce::AudioPluginInstance> p, const juce::PluginDescription& d, const juce::MemoryBlock* state, juce::uint64 uid);
    void destroyInstance (HostedInstance* h);
    void postSlot (int ch, int s, HostedInstance* inst, juce::uint64 uid, int fadeSamples);
    void collectGarbage();
    void maintainPrepared();
    void handleNotice (const EngineNotice& n);
    void updateMapLabels();
    juce::uint64 newUid();
    bool isReferenced (HostedInstance* h) const;
    void invalidatePrepared (int p);

    struct DesiredSlot
    {
        PreparedSlot::Action action = PreparedSlot::keep;
        const SlotSnapshot* snap = nullptr;
        bool missing = false;
    };
    std::array<DesiredSlot, (size_t) (kNumChannels * kNumSlots)> computeDesired (int p, juce::uint64& signature) const;

    juce::AudioProcessor& owner;
    AudioEngine& engine;
    ParamRefs& params;

    std::vector<std::unique_ptr<HostedInstance>> pool;
    juce::CriticalSection poolLock;
    juce::uint32 nextInstanceId = 1;
    std::array<std::array<SlotModel, (size_t) kNumSlots>, (size_t) kNumChannels> model;

    std::array<MapModel, (size_t) kNumMaps> maps;
    std::array<std::atomic<juce::uint64>, (size_t) kNumMaps> mapUidA;
    std::array<std::atomic<int>, (size_t) kNumMaps> mapIndexA;
    std::atomic<bool> learnArmed { false };
    std::atomic<juce::uint64> learnUid { 0 };

    struct PrepEntry
    {
        PreparedSet* sent = nullptr;                                 // owned by the audio engine until released
        juce::uint64 signature = 0;
        std::array<HostedInstance*, (size_t) (kNumChannels * kNumSlots)> cache {};
    };
    std::array<PrepEntry, (size_t) kNumPresets> prep;
    juce::StringArray failedIdentifiers;
    int prepTick = 0;

    double sampleRate = 44100.0;
    int blockSize = 512;
    bool offline = false;

    std::unique_ptr<PluginWindowManager> windowManager;
    JUCE_DECLARE_WEAK_REFERENCEABLE (HostManager)
};
}
