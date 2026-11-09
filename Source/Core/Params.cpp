#include "Params.h"

namespace six
{
using namespace juce;

MapParameter::MapParameter (const String& id, const String& base)
    : AudioParameterFloat (ParameterID { id, 1 }, base, NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                           AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                           { return String (roundToInt (v * 100.0f)) + " %"; })),
      baseName (base)
{
}

String MapParameter::getName (int maximumStringLength) const
{
    String n;
    {
        const SpinLock::ScopedLockType sl (lock);
        n = mapLabel.isEmpty() ? baseName : baseName + " " + mapLabel;
    }
    return maximumStringLength > 0 ? n.substring (0, maximumStringLength) : n;
}

void MapParameter::setLabelText (const String& s)
{
    const SpinLock::ScopedLockType sl (lock);
    mapLabel = s;
}

String MapParameter::getLabelText() const
{
    const SpinLock::ScopedLockType sl (lock);
    return mapLabel;
}

namespace
{
    // Fader-style taper: norm = (gain / maxGain)^(1/3)
    NormalisableRange<float> faderRange (float maxDb)
    {
        const float maxGain = Decibels::decibelsToGain (maxDb);
        return NormalisableRange<float> (kMinDb, maxDb,
            [maxGain] (float, float, float x)      // 0..1 -> dB
            {
                const float g = x * x * x * maxGain;
                return gainToDb (g);
            },
            [maxGain] (float, float, float db)     // dB -> 0..1
            {
                const float g = dbToGain (db) / maxGain;
                return jlimit (0.0f, 1.0f, std::cbrt (g));
            },
            [] (float, float, float db) { return db <= kMinDb + 0.05f ? kMinDb : db; });
    }

    float parseDb (const String& t)
    {
        auto s = t.trim().toLowerCase();
        if (s.startsWith ("-inf") || s.startsWith (String (CharPointer_UTF8 ("-\xe2\x88\x9e")))) return kMinDb;
        return s.retainCharacters ("+-.0123456789").getFloatValue();
    }

    float parsePan (const String& t)
    {
        auto s = t.trim().toUpperCase();
        if (s.isEmpty() || s == "C") return 0.0f;
        const float n = s.retainCharacters ("0123456789.").getFloatValue();
        if (s.startsWith ("L")) return -n;
        if (s.startsWith ("R")) return n;
        return s.getFloatValue();
    }
}

AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;

    // Order matters for Live (it lists the first 128 parameters without Configure):
    // mixer-level controls first, then the Map lanes, then channels, sends and bypasses.
    // IDs are independent of this order.
    StringArray targets { String (CharPointer_UTF8 ("\xe2\x80\x94")) };
    for (int i = 0; i < kNumPresets; ++i) targets.add (twoDigits (i + 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { gid::presetTarget, 1 }, "Preset Target", targets, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { gid::morphMode, 1 }, "Morph Sync/ms", StringArray { "Sync", "ms" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { gid::morphMs, 1 }, "Morph Time ms",
        NormalisableRange<float> (0.0f, 10000.0f, 1.0f, 0.3f), 350.0f,
        AudioParameterFloatAttributes().withLabel ("ms")
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })
            .withValueFromStringFunction ([] (const String& t) { return t.retainCharacters ("0123456789.").getFloatValue(); })));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { gid::morphSync, 1 }, "Morph Musical Time", syncChoiceNames(), 4));


    std::vector<int> order;
    for (int k = 0; k < kNumMaps; ++k) order.push_back (mapControl (k));
    for (int i = 0; i < kMapBase; ++i) order.push_back (i);

    for (int i : order)
    {
        const auto& c = controlInfo (i);
        const ParameterID pid { c.paramID, 1 };

        switch (c.kind)
        {
            case ControlKind::gainDb:
                layout.add (std::make_unique<AudioParameterFloat> (pid, c.name, faderRange (c.maxValue), c.defaultValue,
                    AudioParameterFloatAttributes().withLabel ("dB")
                        .withStringFromValueFunction ([] (float v, int) { return formatDb (v).upToLastOccurrenceOf (" dB", false, false); })
                        .withValueFromStringFunction (parseDb)));
                break;

            case ControlKind::sendInDb:
                layout.add (std::make_unique<AudioParameterFloat> (pid, c.name, NormalisableRange<float> (c.minValue, c.maxValue, 0.01f),
                    c.defaultValue,
                    AudioParameterFloatAttributes().withLabel ("dB")
                        .withStringFromValueFunction ([] (float v, int) { return formatDb (v).upToLastOccurrenceOf (" dB", false, false); })
                        .withValueFromStringFunction (parseDb)));
                break;

            case ControlKind::pan:
                layout.add (std::make_unique<AudioParameterFloat> (pid, c.name, NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
                    AudioParameterFloatAttributes()
                        .withStringFromValueFunction ([] (float v, int) { return formatPan (v); })
                        .withValueFromStringFunction (parsePan)));
                break;

            case ControlKind::toggle:
            {
                StringArray names;
                if (c.paramID.endsWith ("_mono"))         names = { "Stereo", "Mono" };
                else if (c.paramID.endsWith ("_prepost")) names = { "Pre", "Post" };
                else if (c.paramID.endsWith ("_bypass"))  names = { "Active", "Bypassed" };
                else                                      names = { "Off", "On" };
                layout.add (std::make_unique<AudioParameterChoice> (pid, c.name, names, roundToInt (c.defaultValue)));
                break;
            }

            case ControlKind::out:
            {
                StringArray names { "Master" };
                for (int ch = 0; ch < kNumChannels; ++ch) names.add ("Ch " + twoDigits (ch + 1));
                layout.add (std::make_unique<AudioParameterChoice> (pid, c.name, names, 0));
                break;
            }

            case ControlKind::map:
                layout.add (std::make_unique<MapParameter> (c.paramID, c.name));
                break;
        }
    }

    return layout;
}

void ParamRefs::bind (AudioProcessorValueTreeState& s)
{
    for (int i = 0; i < kNumControls; ++i)
    {
        const auto& id = controlInfo (i).paramID;
        controls[(size_t) i] = s.getParameter (id);
        raw[(size_t) i] = s.getRawParameterValue (id);
        jassert (controls[(size_t) i] != nullptr);
    }
    for (int k = 0; k < kNumMaps; ++k)
        maps[(size_t) k] = dynamic_cast<MapParameter*> (controls[(size_t) mapControl (k)]);

    presetTarget = s.getParameter (gid::presetTarget);
    morphMode = s.getParameter (gid::morphMode);
    morphMs = s.getParameter (gid::morphMs);
    morphSync = s.getParameter (gid::morphSync);
    rawPresetTarget = s.getRawParameterValue (gid::presetTarget);
    rawMorphMode = s.getRawParameterValue (gid::morphMode);
    rawMorphMs = s.getRawParameterValue (gid::morphMs);
    rawMorphSync = s.getRawParameterValue (gid::morphSync);
}

void ParamRefs::setWithGesture (RangedAudioParameter* p, float plainValue)
{
    if (p == nullptr) return;
    const float norm = p->convertTo0to1 (plainValue);
    p->beginChangeGesture();
    p->setValueNotifyingHost (norm);
    p->endChangeGesture();
}
}
