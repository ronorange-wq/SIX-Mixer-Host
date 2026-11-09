#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Controls.h"

namespace six
{
// Map NN parameter: a stable automation lane whose display name reflects the external
// plug-in parameter it is bound to. The parameter ID never changes.
class MapParameter : public juce::AudioParameterFloat
{
public:
    MapParameter (const juce::String& id, const juce::String& baseName);
    juce::String getName (int maximumStringLength) const override;
    void setLabelText (const juce::String& s);   // e.g. "CH 02 FX1 Threshold"
    juce::String getLabelText() const;
private:
    juce::String baseName;
    juce::String mapLabel;
    mutable juce::SpinLock lock;
};

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

struct ParamRefs
{
    std::array<juce::RangedAudioParameter*, (size_t) kNumControls> controls {};
    std::array<std::atomic<float>*, (size_t) kNumControls> raw {};
    std::array<MapParameter*, (size_t) kNumMaps> maps {};
    juce::RangedAudioParameter* presetTarget = nullptr;
    juce::RangedAudioParameter* morphMode = nullptr;
    juce::RangedAudioParameter* morphMs = nullptr;
    juce::RangedAudioParameter* morphSync = nullptr;
    std::atomic<float>* rawPresetTarget = nullptr;
    std::atomic<float>* rawMorphMode = nullptr;
    std::atomic<float>* rawMorphMs = nullptr;
    std::atomic<float>* rawMorphSync = nullptr;

    void bind (juce::AudioProcessorValueTreeState& s);

    // Set a parameter from the UI with a full begin/perform/end gesture so Live can record it.
    static void setWithGesture (juce::RangedAudioParameter* p, float plainValue);
};
}
