#include "PresetBank.h"

namespace six
{
using namespace juce;

std::unique_ptr<XmlElement> SlotSnapshot::toXml (const String& tag) const
{
    auto e = std::make_unique<XmlElement> (tag);
    e->setAttribute ("has", hasPlugin);
    if (! hasPlugin) return e;
    e->setAttribute ("uid", String::toHexString ((int64) uid));
    e->setAttribute ("hash", String::toHexString ((int64) hash));
    if (auto d = desc.createXml()) e->addChildElement (d.release());
    auto* st = e->createNewChildElement ("STATE");
    st->addTextElement (state.toBase64Encoding());
    return e;
}

SlotSnapshot SlotSnapshot::fromXml (const XmlElement& e)
{
    SlotSnapshot s;
    s.hasPlugin = e.getBoolAttribute ("has");
    if (! s.hasPlugin) return s;
    s.uid = (uint64) e.getStringAttribute ("uid").getHexValue64();
    s.hash = (uint64) e.getStringAttribute ("hash").getHexValue64();
    if (auto* d = e.getChildByName ("PLUGIN")) s.desc.loadFromXml (*d);
    if (auto* st = e.getChildByName ("STATE")) s.state.fromBase64Encoding (st->getAllSubText().trim());
    if (s.desc.name.isEmpty()) s.hasPlugin = false;
    return s;
}

int PresetBank::nextFree() const
{
    for (int i = 0; i < kNumPresets; ++i) if (! presets[(size_t) i].used) return i;
    return -1;
}

int PresetBank::numUsed() const
{
    int n = 0;
    for (auto& p : presets) n += p.used ? 1 : 0;
    return n;
}

String PresetBank::encodeValues (const ControlValues& v)
{
    StringArray a;
    for (auto f : v) a.add (String (f, 4));
    return a.joinIntoString (",");
}

ControlValues PresetBank::decodeValues (const String& s)
{
    auto v = defaultControlValues();
    auto a = StringArray::fromTokens (s, ",", "");
    for (int i = 0; i < jmin (a.size(), kNumControls); ++i) v[(size_t) i] = a[i].getFloatValue();
    return v;
}

std::unique_ptr<XmlElement> PresetBank::toXml() const
{
    auto e = std::make_unique<XmlElement> ("PRESETS");
    for (int i = 0; i < kNumPresets; ++i)
    {
        const auto& p = presets[(size_t) i];
        if (! p.used) continue;
        auto* pe = e->createNewChildElement ("PRESET");
        pe->setAttribute ("index", i);
        pe->setAttribute ("name", p.name);
        pe->createNewChildElement ("VALUES")->addTextElement (encodeValues (p.values));
        for (int s = 0; s < kNumChannels * kNumSlots; ++s)
            if (p.slots[(size_t) s].hasPlugin)
            {
                auto se = p.slots[(size_t) s].toXml ("SLOT");
                se->setAttribute ("i", s);
                pe->addChildElement (se.release());
            }
    }
    return e;
}

void PresetBank::fromXml (const XmlElement& e)
{
    clearAll();
    for (auto* pe : e.getChildWithTagNameIterator ("PRESET"))
    {
        const int i = pe->getIntAttribute ("index", -1);
        if (! isPositiveAndBelow (i, kNumPresets)) continue;
        auto& p = presets[(size_t) i];
        p.used = true;
        p.name = pe->getStringAttribute ("name");
        if (auto* v = pe->getChildByName ("VALUES")) p.values = decodeValues (v->getAllSubText());
        for (auto* se : pe->getChildWithTagNameIterator ("SLOT"))
        {
            const int s = se->getIntAttribute ("i", -1);
            if (isPositiveAndBelow (s, kNumChannels * kNumSlots)) p.slots[(size_t) s] = SlotSnapshot::fromXml (*se);
        }
    }
}
}
