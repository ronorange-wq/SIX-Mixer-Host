#pragma once
#include <juce_core/juce_core.h>
#include <array>

// ---------------------------------------------------------------------------------
// SIX control layout.
//
// Every sound-affecting control of the mixer lives in one flat array ("ControlValues").
// The same index is used by: the host parameter, the audio engine, the morph engine and
// the preset snapshots. Parameter IDs are derived from the index and never change.
// ---------------------------------------------------------------------------------
namespace six
{
constexpr int kNumChannels   = 6;
constexpr int kNumSlots      = 4;                      // device-chain slots per channel
constexpr int kNumSends      = kNumChannels - 1;       // one send to each other channel
constexpr int kNumPresets    = 64;                     // 8 pages x 8
constexpr int kPresetsPerPage = 8;
constexpr int kNumMaps       = 32;                     // Learn/Map automation lanes
constexpr int kNumOutChoices = kNumChannels + 1;       // Master + Ch 01..06

constexpr float kMinDb = -70.0f;                       // treated as -inf
constexpr float kFaderMaxDb  = 6.0f;
constexpr float kSendMaxDb   = 6.0f;
constexpr float kSendInMinDb = -24.0f;
constexpr float kSendInMaxDb = 12.0f;

enum class ControlKind { gainDb, sendInDb, pan, toggle, out, map };

// Per-channel controls
enum ChannelControl { ccFader = 0, ccPan, ccSendIn, ccMono, ccLiveIn, ccOut, ccNumPerChannel };

constexpr int kChannelBase = 0;
constexpr int kSendBase    = kChannelBase + kNumChannels * ccNumPerChannel;     // 36
constexpr int kNumSendPairs = kNumChannels * kNumSends;                          // 30
constexpr int kBypassBase  = kSendBase + kNumSendPairs * 2;                      // 96
constexpr int kMapBase     = kBypassBase + kNumChannels * kNumSlots;             // 120
constexpr int kNumControls = kMapBase + kNumMaps;                                // 152

using ControlValues = std::array<float, (size_t) kNumControls>;

// channel index ch is 0..5 everywhere in code; displayed as 01..06
inline int channelControl (int ch, ChannelControl c) noexcept { return kChannelBase + ch * ccNumPerChannel + (int) c; }

// Send from channel `src` to channel `dst` (dst != src). The k-th send of a channel is
// computed from the channel numbers, so the list of targets can never contain duplicates.
inline int sendSlotIndex (int src, int dst) noexcept { return dst < src ? dst : dst - 1; }   // 0..4
inline int sendTarget (int src, int sendSlot) noexcept { return sendSlot < src ? sendSlot : sendSlot + 1; }
inline int sendLevelControl (int src, int dst) noexcept { return kSendBase + (src * kNumSends + sendSlotIndex (src, dst)) * 2; }
inline int sendModeControl  (int src, int dst) noexcept { return sendLevelControl (src, dst) + 1; }   // 0 = Pre, 1 = Post
inline int bypassControl (int ch, int slot) noexcept { return kBypassBase + ch * kNumSlots + slot; }
inline int mapControl (int k) noexcept { return kMapBase + k; }

struct ControlInfo
{
    ControlKind kind;
    juce::String paramID;   // stable automation id
    juce::String name;      // readable name shown in Live
    float minValue, maxValue, defaultValue;
    int channel = -1, other = -1, slot = -1;
};

const ControlInfo& controlInfo (int index);
ControlValues defaultControlValues();

// Mixer-level (global, not part of presets) parameter ids
namespace gid
{
    static const juce::String presetTarget = "preset_target";
    static const juce::String morphMode    = "morph_mode";     // 0 Sync, 1 ms
    static const juce::String morphMs      = "morph_ms";
    static const juce::String morphSync    = "morph_sync";
}

constexpr int kNumSyncChoices = 8;
const juce::StringArray& syncChoiceNames();
double syncChoiceQuarterNotes (int choice, int timeSigNum, int timeSigDen);

inline juce::String twoDigits (int n) { return juce::String (n).paddedLeft ('0', 2); }

inline float dbToGain (float db) noexcept { return db <= kMinDb + 0.001f ? 0.0f : std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept  { return g <= 0.000316f ? kMinDb : juce::jmax (kMinDb, 20.0f * std::log10 (g)); }

juce::String formatDb (float db, int decimals = 1);
juce::String formatPan (float pan);   // pan -100..100 -> L20 / C / R15
}
