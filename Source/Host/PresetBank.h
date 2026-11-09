#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Core/Controls.h"

namespace six
{
struct SlotSnapshot
{
    bool hasPlugin = false;
    juce::PluginDescription desc;
    juce::MemoryBlock state;
    juce::uint64 hash = 0;
    juce::uint64 uid = 0;

    std::unique_ptr<juce::XmlElement> toXml (const juce::String& tag) const;
    static SlotSnapshot fromXml (const juce::XmlElement& e);
};

struct Preset
{
    bool used = false;
    juce::String name;                           // optional; empty by default
    ControlValues values {};
    std::array<SlotSnapshot, (size_t) (kNumChannels * kNumSlots)> slots;
};

// 64 presets with fixed addresses (01..64). Names are optional; deleting a preset never
// renumbers the others, so automation of "Preset Target" keeps pointing at the same slot.
class PresetBank
{
public:
    Preset& get (int i) { return presets[(size_t) i]; }
    const Preset& get (int i) const { return presets[(size_t) i]; }
    int nextFree() const;
    int numUsed() const;

    std::unique_ptr<juce::XmlElement> toXml() const;
    void fromXml (const juce::XmlElement& e);
    void clearAll() { for (auto& p : presets) p = Preset(); }

    static juce::String encodeValues (const ControlValues& v);
    static ControlValues decodeValues (const juce::String& s);

private:
    std::array<Preset, (size_t) kNumPresets> presets;
};
}
