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

namespace
{
    void addGlobal (AudioProcessorValueTreeState::ParameterLayout& layout, int which)
    {
        if (which == -1)
        {
            StringArray targets { String (CharPointer_UTF8 ("\xe2\x80\x94")) };
            for (int i = 0; i < kNumPresets; ++i) targets.add (twoDigits (i + 1));
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { gid::presetTarget, 1 }, "Preset Target", targets, 0));
        }
        else if (which == -2)
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { gid::morphMode, 1 }, "Morph Sync/ms", StringArray { "Sync", "ms" }, 0));
        else if (which == -3)
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { gid::morphMs, 1 }, "Morph Time ms",
                NormalisableRange<float> (0.0f, 10000.0f, 1.0f, 0.3f), 350.0f,
                AudioParameterFloatAttributes().withLabel ("ms")
                    .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })
                    .withValueFromStringFunction ([] (const String& t) { return t.retainCharacters ("0123456789.").getFloatValue(); })));
        else if (which == -4)
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { gid::morphSync, 1 }, "Morph Musical Time", syncChoiceNames(), 4));
    }
}

AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;

    // Push 3 shows plug-in parameters in banks of 8, in this order (IDs don't depend on it):
    //  banks 1-6: one channel each = its 5 send levels (to the other channels, in channel
    //             order), then its Send In, Pan and Fader
    //  bank 7: Solo 01-06 + Preset Target + Morph ms     bank 8: Mono 01-06 + Sync/ms + Musical time
    //  bank 9: Out 01-06 + Map 01-02                     bank 10: Live In 01-06 + Map 03-04
    //  then the sends' Pre/Post, the slot Bypasses and the remaining Map lanes.
    std::vector<int> order;
    order.reserve ((size_t) kNumControls + 4);
    auto chans = [&] (ChannelControl cc) { for (int ch = 0; ch < kNumChannels; ++ch) order.push_back (channelControl (ch, cc)); };
    int nextMap = 0;
    auto maps = [&] (int n) { for (int i = 0; i < n && nextMap < kNumMaps; ++i) order.push_back (mapControl (nextMap++)); };
    for (int ch = 0; ch < kNumChannels; ++ch)
    {
        for (int k = 0; k < kNumSends; ++k) order.push_back (sendLevelControl (ch, sendTarget (ch, k)));
        order.push_back (channelControl (ch, ccSendIn));
        order.push_back (channelControl (ch, ccPan));
        order.push_back (channelControl (ch, ccFader));
    }
    for (int ch = 0; ch < kNumChannels; ++ch) order.push_back (soloControl (ch));
    order.push_back (-1); order.push_back (-3);                     // -1 preset target, -3 morph ms
    chans (ccMono);   order.push_back (-2); order.push_back (-4);   // -2 morph mode, -4 musical time
    chans (ccOut);    maps (2);
    chans (ccLiveIn); maps (2);
    for (int ch = 0; ch < kNumChannels; ++ch)
        for (int k = 0; k < kNumSends; ++k) order.push_back (sendModeControl (ch, sendTarget (ch, k)));
    for (int ch = 0; ch < kNumChannels; ++ch)
        for (int sl = 0; sl < kNumSlots; ++sl) order.push_back (bypassControl (ch, sl));
    maps (kNumMaps);
    for (int i : order)
    {
        if (i < 0) { addGlobal (layout, i); continue; }
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
                layout.add (std::make_unique<AudioParameterFloat> (pid, c.name, faderRange (c.maxValue),   // -inf .. +12 dB
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
