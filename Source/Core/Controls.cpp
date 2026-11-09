#include "Controls.h"

namespace six
{
namespace
{
    std::vector<ControlInfo> buildTable()
    {
        std::vector<ControlInfo> t ((size_t) kNumControls);

        for (int ch = 0; ch < kNumChannels; ++ch)
        {
            const auto n = twoDigits (ch + 1);
            const auto p = "ch" + n + "_";
            const auto nm = "CH " + n + " ";

            auto set = [&] (ChannelControl c, ControlKind k, juce::String id, juce::String name, float mn, float mx, float def)
            {
                auto& i = t[(size_t) channelControl (ch, c)];
                i = { k, p + id, nm + name, mn, mx, def, ch, -1, -1 };
            };

            set (ccFader,  ControlKind::gainDb,   "fader",  "Fader",       kMinDb, kFaderMaxDb, 0.0f);
            set (ccPan,    ControlKind::pan,      "pan",    "Pan",         -100.0f, 100.0f, 0.0f);
            set (ccSendIn, ControlKind::sendInDb, "sendin", "Send In",     kSendInMinDb, kSendInMaxDb, 0.0f);
            set (ccMono,   ControlKind::toggle,   "mono",   "Mono/Stereo", 0.0f, 1.0f, 0.0f);
            set (ccLiveIn, ControlKind::toggle,   "livein", "Live In",     0.0f, 1.0f, ch == 0 ? 1.0f : 0.0f);
            set (ccOut,    ControlKind::out,      "out",    "Out",         0.0f, (float) kNumChannels, 0.0f);

            for (int k = 0; k < kNumSends; ++k)
            {
                const int dst = sendTarget (ch, k);
                const auto dn = twoDigits (dst + 1);
                auto& lv = t[(size_t) sendLevelControl (ch, dst)];
                lv = { ControlKind::gainDb, p + "to" + dn + "_level",
                       nm + juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92 ")) + dn + " Send Level",
                       kMinDb, kSendMaxDb, kMinDb, ch, dst, -1 };
                auto& md = t[(size_t) sendModeControl (ch, dst)];
                md = { ControlKind::toggle, p + "to" + dn + "_prepost",
                       nm + juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92 ")) + dn + " Pre/Post",
                       0.0f, 1.0f, 1.0f, ch, dst, -1 };
            }

            for (int s = 0; s < kNumSlots; ++s)
            {
                auto& b = t[(size_t) bypassControl (ch, s)];
                b = { ControlKind::toggle, p + "fx" + juce::String (s + 1) + "_bypass",
                      nm + "FX " + juce::String (s + 1) + " Bypass", 0.0f, 1.0f, 0.0f, ch, -1, s };
            }
        }

        for (int k = 0; k < kNumMaps; ++k)
            t[(size_t) mapControl (k)] = { ControlKind::map, "map_" + twoDigits (k + 1), "Map " + twoDigits (k + 1),
                                           0.0f, 1.0f, 0.0f, -1, -1, k };
        return t;
    }

    const std::vector<ControlInfo>& table()
    {
        static const auto t = buildTable();
        return t;
    }
}

const ControlInfo& controlInfo (int index) { return table()[(size_t) index]; }

ControlValues defaultControlValues()
{
    ControlValues v {};
    for (int i = 0; i < kNumControls; ++i)
        v[(size_t) i] = controlInfo (i).defaultValue;
    return v;
}

const juce::StringArray& syncChoiceNames()
{
    static const juce::StringArray names { "1/16", "1/8", "1/4", "1/2", "1 Bar", "2 Bars", "4 Bars", "8 Bars" };
    return names;
}

double syncChoiceQuarterNotes (int choice, int num, int den)
{
    if (num <= 0) num = 4;
    if (den <= 0) den = 4;
    const double bar = num * 4.0 / den;
    switch (choice)
    {
        case 0: return 0.25;
        case 1: return 0.5;
        case 2: return 1.0;
        case 3: return 2.0;
        case 4: return bar;
        case 5: return bar * 2.0;
        case 6: return bar * 4.0;
        case 7: return bar * 8.0;
        default: return bar;
    }
}

juce::String formatDb (float db, int decimals)
{
    if (db <= kMinDb + 0.001f)
        return juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e dB"));
    auto s = juce::String (db, decimals);
    if (db > 0.0f) s = "+" + s;
    if (s == "-0.0" || s == "+0.0") s = "0.0";
    return s + " dB";
}

juce::String formatPan (float pan)
{
    const int p = juce::roundToInt (pan);
    if (p == 0) return "C";
    return p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p);
}
}
