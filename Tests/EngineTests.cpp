// SIX engine / integration tests. Drives the real SIXAudioProcessor offline, hosting real VST3s.
// Usage: SIXTests <dir containing SIXTestComp.vst3, SIXTestLatency.vst3, SIXTestWidener.vst3>
#include <juce_audio_utils/juce_audio_utils.h>
#include <thread>
#include "PluginProcessor.h"
#include "Core/Routing.h"
#include "UI/PluginEditor.h"
#include "UI/DeviceChain.h"
#include "UI/PresetBar.h"
#include "UI/PluginWindows.h"

using namespace juce;
using namespace six;

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { ++checks; if (! (cond)) { ++failures; std::cout << "  FAIL: " << msg << "  [" #cond "]  line " << __LINE__ << std::endl; } } while (0)
#define CHECK_NEAR(a, b, tol, msg) do { const double _a = (a), _b = (b); ++checks; if (std::abs (_a - _b) > (tol)) { ++failures; std::cout << "  FAIL: " << msg << " got " << _a << " expected " << _b << "  line " << __LINE__ << std::endl; } } while (0)

static File pluginDir;
static File bundle (const String& name) { return pluginDir.getChildFile (name + "_artefacts/Release/VST3/" + name + ".vst3"); }

struct TestPlayHead : public AudioPlayHead
{
    double bpm = 120.0; int64 samplePos = 0; bool playing = true;
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setTimeSignature (TimeSignature { 4, 4 });
        p.setTimeInSamples (samplePos);
        p.setIsPlaying (playing);
        return p;
    }
};

struct Rig
{
    std::unique_ptr<SIXAudioProcessor> p;
    TestPlayHead ph;
    double sr = 48000.0;
    int bs = 256;
    AudioBuffer<float> last;
    int64 t = 0;

    explicit Rig (int blockSize = 256, bool offline = false) : bs (blockSize)
    {
        p = std::make_unique<SIXAudioProcessor>();
        p->setPlayHead (&ph);
        p->setNonRealtime (offline);
        p->setPlayConfigDetails (2, 2, sr, bs);
        p->prepareToPlay (sr, bs);
    }

    RangedAudioParameter* param (const String& id) { return p->apvts.getParameter (id); }
    void set (const String& id, float plain) { auto* q = param (id); q->setValueNotifyingHost (q->convertTo0to1 (plain)); }
    float get (const String& id) { return p->apvts.getRawParameterValue (id)->load(); }
    float eff (int control) { return p->engine.effective[(size_t) control].load(); }

    void pump() { p->host->pump(); MessageManager::getInstance()->runDispatchLoopUntil (1); }

    // Feeds `blocks` blocks of a signal (fn(sampleIndex, channel)), returns the last block.
    AudioBuffer<float>& run (int blocks, std::function<float (int64, int)> fn = {})
    {
        last.setSize (2, bs);
        MidiBuffer m;
        for (int b = 0; b < blocks; ++b)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < bs; ++i)
                    last.setSample (c, i, fn ? fn (t + i, c) : 0.0f);
            ph.samplePos = t;
            p->processBlock (last, m);
            t += bs;
            pump();
        }
        return last;
    }
    static float dc (int64, int) { return 0.5f; }
};

static float sine (int64 n, int) { return 0.5f * (float) std::sin (2.0 * MathConstants<double>::pi * 441.0 * (double) n / 48000.0); }
static float peak (const AudioBuffer<float>& b, int ch) { return b.getMagnitude (ch, 0, b.getNumSamples()); }

static PluginDescription describe (Rig& r, const String& name)
{
    String err;
    auto types = r.p->host->catalog.findTypesForFile (bundle (name), err);
    if (types.isEmpty()) { std::cout << "  cannot describe " << name << ": " << err << std::endl; return {}; }
    return types[0];
}

static AudioProcessorParameter* hostedParam (HostedInstance* h, const String& name)
{
    for (auto* p : h->plugin().getParameters()) if (p->getName (64) == name) return p;
    return nullptr;
}

// -------------------------------------------------------------------------------------------
static void testDefaults()
{
    std::cout << "[defaults: Live stereo in -> CH 01 -> Master, unity]" << std::endl;
    Rig r;
    auto& out = r.run (20, [] (int64 n, int c) { return c == 0 ? sine (n, c) : 0.25f * sine (n, c); });
    AudioBuffer<float> in (2, r.bs);
    for (int i = 0; i < r.bs; ++i) { in.setSample (0, i, sine (r.t - r.bs + i, 0)); in.setSample (1, i, 0.25f * sine (r.t - r.bs + i, 1)); }
    float maxErr = 0;
    for (int c = 0; c < 2; ++c) for (int i = 0; i < r.bs; ++i) maxErr = jmax (maxErr, std::abs (out.getSample (c, i) - in.getSample (c, i)));
    CHECK (maxErr < 1.0e-5f, "empty chain must pass the input unchanged (err " << maxErr << ")");
    CHECK (r.p->getLatencySamples() == 0, "latency 0 with empty chains");
    CHECK (r.get ("ch01_livein") == 1.0f && r.get ("ch02_livein") == 0.0f, "only CH 01 receives Live input by default");
    for (int ch = 0; ch < kNumChannels; ++ch)
        for (int d = 0; d < kNumChannels; ++d)
            if (d != ch) CHECK (r.eff (sendLevelControl (ch, d)) <= kMinDb, "all sends closed");
    for (int i = 0; i < kNumPresets; ++i)
        CHECK (! r.p->host->bank.get (i).used && r.p->host->bank.get (i).name.isEmpty(), "preset " << i + 1 << " empty, no name");
    CHECK (r.get (gid::presetTarget) == 0.0f, "preset target starts at none");

    // send list built from channel numbers: CH 02 -> 01,03,04,05,06
    StringArray t;
    for (int k = 0; k < kNumSends; ++k) t.add (twoDigits (sendTarget (1, k) + 1));
    CHECK (t.joinIntoString (",") == "01,03,04,05,06", "CH 02 send targets " << t.joinIntoString (","));
    CHECK (controlInfo (channelControl (1, ccPan)).name == "CH 02 Pan", "param name CH 02 Pan");
    CHECK (controlInfo (sendLevelControl (1, 3)).name == String (CharPointer_UTF8 ("CH 02 \xe2\x86\x92 04 Send Level")), "param name send level");
    CHECK (controlInfo (sendModeControl (1, 3)).name == String (CharPointer_UTF8 ("CH 02 \xe2\x86\x92 04 Pre/Post")), "param name pre/post");
    CHECK (r.p->getParameters().size() == kNumControls + 4, "parameter count " << r.p->getParameters().size());
}

static void testMonoPan()
{
    std::cout << "[mono / stereo / pan laws]" << std::endl;
    {
        Rig r;
        r.set ("ch01_mono", 1.0f);
        auto& o = r.run (40, [] (int64, int c) { return c == 0 ? 0.8f : 0.0f; });
        // (L+R)/2 = 0.4, equal-power centre -3 dB -> 0.2828
        CHECK_NEAR (o.getSample (0, 100), 0.4 * std::cos (MathConstants<double>::pi / 4), 1e-3, "mono sum + equal-power centre L");
        CHECK_NEAR (o.getSample (1, 100), 0.4 * std::sin (MathConstants<double>::pi / 4), 1e-3, "mono sum + equal-power centre R");
        r.set ("ch01_pan", 100.0f);
        auto& o2 = r.run (20, [] (int64, int c) { return c == 0 ? 0.8f : 0.0f; });
        CHECK_NEAR (o2.getSample (0, 100), 0.0, 1e-3, "mono hard right L");
        CHECK_NEAR (o2.getSample (1, 100), 0.4, 1e-3, "mono hard right R");
    }
    {
        Rig r;
        r.set ("ch01_pan", 50.0f);   // stereo balance
        CHECK (r.param ("ch01_pan")->getCurrentValueAsText() == "R50", "pan text R50");
        r.set ("ch01_pan", -20.0f);
        CHECK (r.param ("ch01_pan")->getCurrentValueAsText() == "L20", "pan text L20");
        r.set ("ch01_pan", 0.0f);
        CHECK (r.param ("ch01_pan")->getCurrentValueAsText() == "C", "pan text C");
        auto& c0 = r.run (20, Rig::dc);
        CHECK_NEAR (c0.getSample (0, 10), 0.5, 1e-4, "balance centre unity L");
        CHECK_NEAR (c0.getSample (1, 10), 0.5, 1e-4, "balance centre unity R");
        r.set ("ch01_pan", 50.0f);
        auto& o = r.run (20, Rig::dc);
        CHECK_NEAR (o.getSample (0, 10), 0.5 * std::cos (0.25 * MathConstants<double>::pi), 1e-3, "balance R50 attenuates L");
        CHECK_NEAR (o.getSample (1, 10), 0.5, 1e-4, "balance R50 keeps R at unity");
    }
}

static void testRouting()
{
    std::cout << "[serial Out, Pre/Post sends, feedback-loop protection]" << std::endl;
    Rig r;
    r.set ("ch01_out", 2.0f);   // Ch 02
    auto& o = r.run (20, Rig::dc);
    CHECK_NEAR (o.getSample (0, 10), 0.5, 1e-4, "01 -> 02 -> Master passes signal");
    r.set ("ch02_fader", -6.0f);
    auto& o2 = r.run (20, Rig::dc);
    CHECK_NEAR (o2.getSample (0, 10), 0.5 * Decibels::decibelsToGain (-6.0), 1e-3, "CH 02 fader acts on the serial path");

    // loop: 02 -> 01 while 01 -> 02
    CHECK (validateRoutingChange (r.p->host->currentValues(), channelControl (1, ccOut), 1.0f).isNotEmpty(), "UI validation rejects loop");
    const auto before = r.p->engine.routingRejects.load();
    r.set ("ch02_out", 1.0f);
    auto& o3 = r.run (20, Rig::dc);
    CHECK (r.p->engine.routingRejects.load() == before + 1, "engine rejected loop from automation");
    CHECK_NEAR (o3.getSample (0, 10), 0.5 * Decibels::decibelsToGain (-6.0), 1e-3, "last valid routing kept");
    r.set ("ch02_out", 0.0f);
    r.run (5, Rig::dc);
    // loop through a send: 01 -> 02 (out), 02 -> 01 (send)
    r.set ("ch02_to01_level", 0.0f);
    r.run (5, Rig::dc);
    CHECK (r.p->engine.routingRejects.load() == before + 2, "send that closes a loop is rejected");
    r.set ("ch02_to01_level", kMinDb);
    r.run (5, Rig::dc);

    // Pre / Post: 01 -> Master, 01 send -> 04 (Post), 04 -> Master
    Rig s;
    s.set ("ch01_to04_level", 0.0f);
    auto& a = s.run (20, Rig::dc);
    CHECK_NEAR (a.getSample (0, 10), 1.0, 1e-3, "dry + post send = 2x");
    s.set ("ch01_fader", -6.0f);
    auto& b = s.run (20, Rig::dc);
    CHECK_NEAR (b.getSample (0, 10), 2 * 0.5 * Decibels::decibelsToGain (-6.0), 1e-3, "source fader affects Post send");
    s.set ("ch01_fader", kMinDb);
    auto& c = s.run (20, Rig::dc);
    CHECK_NEAR (c.getSample (0, 10), 0.0, 1e-4, "fader down silences Post send");
    s.set ("ch01_to04_prepost", 0.0f);   // Pre
    auto& d = s.run (20, Rig::dc);
    CHECK_NEAR (d.getSample (0, 10), 0.5, 1e-3, "Pre send keeps working with fader down");
    s.set ("ch04_sendin", -6.0f);
    auto& e = s.run (20, Rig::dc);
    CHECK_NEAR (e.getSample (0, 10), 0.5 * Decibels::decibelsToGain (-6.0), 1e-3, "Send In scales incoming sends");
}

static void testHostingChain()
{
    std::cout << "[hosting real VST3s: EQ->Comp->Sat serial + Reverb send, latency, mono->stereo]" << std::endl;
    Rig r;
    auto comp = describe (r, "SIXTestComp");
    auto lat = describe (r, "SIXTestLatency");
    auto wid = describe (r, "SIXTestWidener");
    CHECK (comp.name == "SIXTestComp", "describe comp from .vst3 bundle (Finder drop path)");
    String err;
    CHECK (r.p->host->loadIntoSlot (0, 0, comp, err), "load comp into CH 01 FX1: " << err);
    r.run (4, Rig::dc);
    auto* h = r.p->host->slot (0, 0).inst;
    CHECK (h != nullptr && h->latency() == 0, "slot holds instance");
    auto& o = r.run (10, Rig::dc);
    CHECK_NEAR (o.getSample (0, 10), 0.5, 1e-4, "comp at 0 dB threshold passes 0.5");
    auto* thr = hostedParam (h, "Threshold");
    thr->setValueNotifyingHost (thr->getValueForText ("-30"));
    auto& o2 = r.run (10, Rig::dc);
    CHECK (o2.getSample (0, 10) < 0.3f, "lowering Threshold compresses (" << o2.getSample (0, 10) << ")");

    // Learn/Map: Threshold -> Map 01
    const int k = r.p->host->assignMap (h, thr->getParameterIndex());
    CHECK (k == 0, "Threshold mapped to Map 01");
    CHECK (r.p->params.maps[0]->getName (100).contains ("Threshold"), "Map 01 name shows Threshold: " << r.p->params.maps[0]->getName (100));
    r.set ("map_01", 1.0f);   // automation lane drawn in Live -> plug-in
    r.run (4, Rig::dc);
    CHECK_NEAR (thr->getValue(), 1.0, 1e-4, "Map 01 automation moves the hosted Threshold");
    auto& o3 = r.run (4, Rig::dc);
    CHECK_NEAR (o3.getSample (0, 10), 0.5, 1e-4, "threshold 0 dB again -> unity");
    // touching the plug-in's own control is recorded on Map 01
    thr->beginChangeGesture();
    thr->setValueNotifyingHost (0.25f);
    thr->endChangeGesture();
    r.run (2, Rig::dc);
    CHECK_NEAR (r.get ("map_01"), 0.25, 1e-3, "plug-in control change flows to Map 01 (recordable)");

    // latency compensation: CH 01 = comp + latency(100); CH 02 gets Live input dry; both to master
    CHECK (r.p->host->loadIntoSlot (0, 1, lat, err), "load latency plug-in");
    r.set ("ch02_livein", 1.0f);
    r.set ("map_01", 1.0f);   // threshold 0 dB: no compression of the impulse
    r.run (20);
    CHECK (r.p->getLatencySamples() == 100, "SIX reports total latency 100 to the host (got " << r.p->getLatencySamples() << ")");
    // impulse test
    const int64 imp = r.t + 1000;
    AudioBuffer<float> acc (2, 4000);
    acc.clear();
    for (int b = 0; b < 16; ++b)
    {
        auto& blk = r.run (1, [imp] (int64 n, int) { return n == imp ? 0.1f : 0.0f; });
        const int64 start = r.t - r.bs - imp + 500;
        for (int i = 0; i < r.bs; ++i) if (start + i >= 0 && start + i < 4000) acc.setSample (0, (int) (start + i), blk.getSample (0, i));
    }
    int peakAt = -1; float pk = 0;
    for (int i = 0; i < 4000; ++i) if (std::abs (acc.getSample (0, i)) > pk) { pk = std::abs (acc.getSample (0, i)); peakAt = i - 500; }
    CHECK (peakAt == 100, "parallel paths aligned: impulse arrives once at +100 samples (got " << peakAt << ")");
    CHECK_NEAR (pk, 0.2, 1e-3, "both paths sum coherently");

    // mono -> stereo: CH 03 Mono with a mono-in/stereo-out device keeps the stereo tail
    CHECK (r.p->host->loadIntoSlot (2, 0, wid, err), "load mono->stereo widener: " << err);
    CHECK (r.p->host->slot (2, 0).inst->layoutName().startsWith ("Mono"), "widener configured mono in: " << r.p->host->slot (2, 0).inst->layoutName());
    r.set ("ch02_livein", 0.0f);
    r.set ("ch01_out", 3.0f);   // 01 -> 03
    r.set ("ch01_fx2_bypass", 1.0f);
    r.set ("ch03_mono", 1.0f);
    auto& w = r.run (60, Rig::dc);
    // comp threshold 0.25*-60 = -45 dB... input mono 0.5: check stereo opposite polarity and balance law (unity centre)
    CHECK (w.getSample (0, 10) > 0.0f && w.getSample (1, 10) < 0.0f, "stereo output of mono-in reverb preserved");
    CHECK_NEAR (std::abs (w.getSample (0, 10)), std::abs (w.getSample (1, 10)), 1e-4, "balance law on widened signal (equal sides at C)");
    CHECK (r.p->getLatencySamples() == 100, "bypassed latency device keeps latency constant");

    // reorder keeps instance + mapping, bypass follows the device
    auto* compInst = r.p->host->slot (0, 0).inst;
    auto* latInst = r.p->host->slot (0, 1).inst;
    r.p->host->moveSlot (0, 1, 3);
    r.run (6, Rig::dc);
    CHECK (r.p->host->slot (0, 3).inst == latInst && r.p->host->slot (0, 0).inst == compInst, "latency device moved to FX4");
    CHECK (r.get ("ch01_fx4_bypass") == 1.0f && r.get ("ch01_fx2_bypass") == 0.0f, "bypass moved with the device");
    r.p->host->moveSlot (0, 0, 2);
    r.run (6, Rig::dc);
    CHECK (r.p->host->slot (0, 2).inst == compInst, "comp moved to FX3 (insert-move)");
    r.set ("map_01", 0.5f);
    r.run (3, Rig::dc);
    CHECK_NEAR (thr->getValue(), 0.5, 1e-4, "mapping follows the moved instance");
    CHECK (r.p->params.maps[0]->getName (100).contains ("FX3"), "Map label follows location: " << r.p->params.maps[0]->getName (100));

    // replacing the device must not re-target the old automation
    r.p->host->loadIntoSlot (0, 2, comp, err);
    r.run (4, Rig::dc);
    auto* newComp = r.p->host->slot (0, 2).inst;
    auto* thr2 = hostedParam (newComp, "Threshold");
    const float before = thr2->getValue();
    r.set ("map_01", 0.9f);
    r.run (3, Rig::dc);
    CHECK_NEAR (thr2->getValue(), before, 1e-6, "Map 01 does not move a replacement device");
    CHECK (r.p->host->mapLocation (0) == "not loaded", "map shows not loaded");
    CHECK (r.p->host->poolSize() <= 4, "old instance freed (pool " << r.p->host->poolSize() << ")");

    // self-hosting is refused
    PluginDescription self; self.name = "SIX"; self.manufacturerName = "SIX Audio"; self.pluginFormatName = "VST3";
    CHECK (! r.p->host->loadIntoSlot (5, 0, self, err) && err.contains ("itself"), "SIX inside itself refused");
}

static void testMorph()
{
    std::cout << "[presets & morph: ms, sync, retrigger, automation priority]" << std::endl;
    Rig r;
    auto& hm = *r.p->host;
    r.run (2, Rig::dc);
    CHECK (hm.saveNewPreset() == 0, "+ saves into 01");
    r.set ("ch01_fader", -12.0f);
    r.set ("ch01_pan", -40.0f);
    r.run (4, Rig::dc);
    CHECK (hm.saveNewPreset() == 1, "+ saves into 02");
    hm.renamePreset (1, "Chorus");
    CHECK (hm.bank.get (0).name.isEmpty() && hm.bank.get (1).name == "Chorus", "only one named");

    // morph 01 over 350 ms (triggered like timeline automation: parameter change)
    r.set (gid::morphMode, 1.0f);
    r.set (gid::morphMs, 350.0f);
    r.set (gid::presetTarget, 1.0f);
    r.run (1, Rig::dc);
    CHECK (r.p->engine.morphProgress.load() > 0.0f && r.p->engine.morphProgress.load() < 0.02f, "morph started in the event block");
    r.run ((int) (0.175 * 48000 / 256) - 1, Rig::dc);
    const float mid = r.eff (channelControl (0, ccFader));
    CHECK (mid > -12.0f && mid < -1.0f, "halfway fader " << mid);
    CHECK_NEAR (r.get ("ch01_fader"), -12.0, 0.05, "morph does not write the fader parameter (no automation spam)");
    r.run ((int) (0.2 * 48000 / 256), Rig::dc);
    CHECK_NEAR (r.eff (channelControl (0, ccFader)), 0.0, 1e-3, "reached preset 01 fader 0 dB");
    CHECK_NEAR (r.eff (channelControl (0, ccPan)), 0.0, 1e-3, "reached preset 01 pan C");
    auto& o = r.run (4, Rig::dc);
    CHECK_NEAR (o.getSample (0, 10), 0.5, 1e-3, "audio follows morph result");

    // same value repeated -> no retrigger
    int startedBefore = r.p->engine.activePreset.load();
    r.set (gid::presetTarget, 1.0f);
    r.run (2, Rig::dc);
    CHECK (r.p->engine.morphProgress.load() >= 1.0f && startedBefore == 0, "same value does not restart");

    // Sync 1 Bar at 120 bpm = 2 s
    r.set (gid::morphMode, 0.0f);
    r.set (gid::morphSync, 4.0f);
    r.set (gid::presetTarget, 2.0f);
    r.run ((int) (1.0 * 48000 / 256), Rig::dc);
    const float p1 = r.p->engine.morphProgress.load();
    CHECK_NEAR (p1, 0.5, 0.02, "1 Bar @120 = 2 s (progress at 1 s)");
    // re-target mid-way: new morph starts from the audible values
    const float audible = r.eff (channelControl (0, ccFader));
    r.set (gid::morphMode, 1.0f);
    r.set (gid::presetTarget, 1.0f);
    r.run (1, Rig::dc);
    CHECK_NEAR (r.eff (channelControl (0, ccFader)), audible, 0.5, "new morph starts from current audible value");
    r.run (100, Rig::dc);
    CHECK_NEAR (r.eff (channelControl (0, ccFader)), 0.0, 1e-3, "arrived at 01");

    // automation of a single control wins over morph/hold for that control
    r.set (gid::presetTarget, 2.0f);
    r.run (5, Rig::dc);
    r.set ("ch01_fader", -3.0f);   // fader automation arriving during the morph
    r.run (80, Rig::dc);
    CHECK_NEAR (r.eff (channelControl (0, ccFader)), -3.0, 1e-3, "explicit fader automation takes precedence");
    CHECK_NEAR (r.eff (channelControl (0, ccPan)), -40.0, 0.01, "other controls still reached preset 02");

    // fader automation and preset change in the same block: fader follows automation
    r.set ("ch01_fader", -20.0f);
    r.set (gid::presetTarget, 1.0f);
    r.run (80, Rig::dc);
    CHECK_NEAR (r.eff (channelControl (0, ccFader)), -20.0, 1e-3, "same-block automation keeps the fader");
    CHECK_NEAR (r.eff (channelControl (0, ccPan)), 0.0, 1e-3, "pan morphed to 01");

    // clicking an empty slot never changes the sound
    const float fBefore = r.eff (channelControl (0, ccFader));
    hm.clickPreset (5);
    r.run (10, Rig::dc);
    CHECK_NEAR (r.eff (channelControl (0, ccFader)), fBefore, 1e-6, "empty preset click: no change");
    CHECK (r.get (gid::presetTarget) == 1.0f, "empty click does not touch Preset Target");

    // delete 01: 02 keeps its address
    hm.clearPreset (0);
    CHECK (! hm.bank.get (0).used && hm.bank.get (1).used && hm.bank.get (1).name == "Chorus", "addresses stable after delete");
    CHECK (hm.saveNewPreset() == 0, "+ refills the free address 01");

    // routing morph that would loop transiently: A: 01->02, B: 02->01 (two-phase, never a loop)
    Rig q;
    q.set ("ch02_livein", 1.0f);
    q.set ("ch01_out", 2.0f);
    q.run (4, Rig::dc);
    q.p->host->saveNewPreset();
    q.set ("ch01_out", 0.0f);
    q.run (4, Rig::dc);
    q.set ("ch02_out", 1.0f);
    q.run (4, Rig::dc);
    q.p->host->saveNewPreset();
    const auto rej = q.p->engine.routingRejects.load();
    q.set (gid::morphMode, 1.0f);
    q.set (gid::morphMs, 200.0f);
    q.set (gid::presetTarget, 1.0f);
    float mx = 0;
    for (int i = 0; i < 60; ++i) { auto& b = q.run (1, Rig::dc); mx = jmax (mx, peak (b, 0)); }
    CHECK (q.p->engine.routingRejects.load() == rej, "morph between opposite routings never forms a loop");
    CHECK (mx < 1.2f, "no feedback build-up (" << mx << ")");
}

static void testPluginMorphAndState()
{
    std::cout << "[prepared plug-in swap on morph, state save/restore, missing plug-in]" << std::endl;
    MemoryBlock saved;
    float refOut = 0;
    {
        Rig r;
        auto comp = describe (r, "SIXTestComp");
        auto& hm = *r.p->host;
        r.run (2, Rig::dc);
        hm.saveNewPreset();                        // 01: empty chain
        String err;
        hm.loadIntoSlot (1, 0, comp, err);         // 02: comp on CH 02 with -30 dB threshold
        r.run (2, Rig::dc);
        auto* thr = hostedParam (hm.slot (1, 0).inst, "Threshold");
        thr->setValueNotifyingHost (thr->getValueForText ("-30"));
        r.set ("ch01_out", 2.0f);
        r.run (4, Rig::dc);
        hm.saveNewPreset();
        hm.renamePreset (1, "Comp");
        hm.channelNames[1] = "Punch";
        hm.removeSlot (1, 0);
        r.set ("ch01_out", 0.0f);
        for (int i = 0; i < 20; ++i) r.run (1, Rig::dc);   // preparer runs on the timer ticks
        CHECK (hm.presetPrepared (1), "preset 02 prepared ahead (plug-in instance ready)");
        CHECK (hm.preparedInstanceCount() >= 1, "prepared instance exists");

        r.set (gid::morphMode, 1.0f);
        r.set (gid::morphMs, 100.0f);
        r.set (gid::presetTarget, 2.0f);
        r.run (1, Rig::dc);
        CHECK (hm.slot (1, 0).inst != nullptr, "plug-in swapped in the event block (no load on audio thread)");
        auto& o = r.run (40, Rig::dc);
        refOut = o.getSample (0, 10);
        CHECK (refOut < 0.3f, "morph target sound includes compressor (" << refOut << ")");

        // map + state
        hm.assignMap (hm.slot (1, 0).inst, hostedParam (hm.slot (1, 0).inst, "Threshold")->getParameterIndex());
        r.p->getStateInformation (saved);
    }
    {
        Rig r;
        r.p->setStateInformation (saved.getData(), (int) saved.getSize());
        auto& o = r.run (40, Rig::dc);
        auto& hm = *r.p->host;
        CHECK_NEAR (o.getSample (0, 10), refOut, 1e-4, "restored set sounds identical");
        CHECK (hm.slot (1, 0).inst != nullptr && hm.slot (1, 0).inst->description().name == "SIXTestComp", "plug-in restored");
        CHECK (hm.bank.get (0).used && hm.bank.get (0).name.isEmpty() && hm.bank.get (1).name == "Comp", "presets + optional names restored");
        CHECK (hm.channelNames[1] == "Punch", "channel name restored");
        CHECK (hm.map (0).assigned() && hm.mapLocation (0) == "CH 02 FX1", "map restored and bound");
        CHECK (r.get ("ch01_out") == 0.0f && r.eff (channelControl (0, ccOut)) == 2.0f, "morph-held routing restored (param 0, audible Ch 02)");
        r.set ("map_01", 1.0f);
        auto& o2 = r.run (6, Rig::dc);
        CHECK_NEAR (o2.getSample (0, 10), 0.5, 1e-3, "restored Map 01 still drives Threshold");
    }
    {
        // missing plug-in: corrupt the path / id
        auto xml = AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        for (auto* host : xml->getChildWithTagNameIterator ("HOST"))
            if (auto* slots = host->getChildByName ("SLOTS"))
                for (auto* s : slots->getChildWithTagNameIterator ("SLOT"))
                    if (auto* pl = s->getChildByName ("PLUGIN"))
                    {
                        pl->setAttribute ("file", "/nonexistent/Gone.vst3");
                        pl->setAttribute ("uniqueId", "deadbeef");
                    }
        MemoryBlock broken;
        AudioProcessor::copyXmlToBinary (*xml, broken);
        Rig r;
        r.p->setStateInformation (broken.getData(), (int) broken.getSize());
        r.run (4, Rig::dc);
        auto& m = r.p->host->slot (1, 0);
        CHECK (m.missing && m.displayName() == "SIXTestComp" && m.missingState.getSize() > 0, "missing plug-in keeps name + state");
        MemoryBlock again;
        r.p->getStateInformation (again);
        CHECK (again.toString().length() > 0 && AudioProcessor::getXmlFromBinary (again.getData(), (int) again.getSize())->toString().contains ("Gone.vst3"),
               "missing plug-in state is saved again for later reload");
    }
}

// Copies the test plug-ins where the Browser scan looks and runs the (load-free) scan.
static File testVst3Dir() { return File::getSpecialLocation (File::userHomeDirectory).getChildFile (".vst3"); }
static void installTestBundles()
{
    testVst3Dir().createDirectory();
    for (auto n : { "SIXTestComp", "SIXTestLatency", "SIXTestWidener" })
        if (! testVst3Dir().getChildFile (String (n) + ".vst3").exists())
            bundle (n).copyDirectoryTo (testVst3Dir().getChildFile (String (n) + ".vst3"));
}
static void removeTestBundles()
{
    for (auto n : { "SIXTestComp", "SIXTestLatency", "SIXTestWidener" })
        testVst3Dir().getChildFile (String (n) + ".vst3").deleteRecursively();
}
static void scanCatalog (PluginCatalog& cat)
{
    installTestBundles();
    cat.startScan (true);
    for (int i = 0; i < 300 && cat.isScanning(); ++i) MessageManager::getInstance()->runDispatchLoopUntil (20);
}
static PluginDescription lightNamed (PluginCatalog& cat, const String& name)
{
    for (auto& d : cat.getEffects()) if (d.name == name) return d;
    return {};
}

static void testFolders()
{
    std::cout << "[Browse folders: create, file, rename, persist, delete]" << std::endl;
    Rig r;
    auto& cat = r.p->host->catalog;
    while (cat.numFolders() > 0) cat.deleteFolder (0);
    scanCatalog (cat);
    auto comp = lightNamed (cat, "SIXTestComp");
    auto lat = lightNamed (cat, "SIXTestLatency");
    const int rev = cat.createFolder ("Reverb");
    const int del = cat.createFolder ("Delay");
    CHECK (cat.numFolders() == 2 && cat.folderName (rev) == "Reverb", "folders created");
    CHECK (cat.addToFolder (rev, comp) && ! cat.addToFolder (rev, comp), "add once, no duplicates");
    cat.addToFolder (del, lat);
    cat.addToFolder (del, comp);   // one effect may live in several folders
    CHECK (cat.search ({}, rev).size() == 1 && cat.search ({}, del).size() == 2, "folder contents");
    CHECK (cat.search ("Latency", del).size() == 1, "search inside a folder");
    cat.renameFolder (del, "Delays");
    {
        Rig r2;   // another SIX instance / a new project sees the same folders
        auto& c2 = r2.p->host->catalog;
        CHECK (c2.numFolders() == 2 && c2.folderName (1) == "Delays" && c2.folderSize (1) == 2, "folders persist across instances");
    }
    cat.removeFromFolder (del, comp);
    CHECK (cat.folderSize (del) == 1, "remove from folder");
    cat.deleteFolder (rev);
    CHECK (cat.numFolders() == 1 && cat.isInFolder (0, lat), "deleting a folder keeps the others");
    while (cat.numFolders() > 0) cat.deleteFolder (0);
}

static void testSafeScan()
{
    std::cout << "[Browser scan never loads plug-ins]" << std::endl;
    Rig r;
    auto& cat = r.p->host->catalog;
    scanCatalog (cat);
    auto effects = cat.getEffects();
    PluginDescription comp;
    for (auto& d : effects) if (d.name == "SIXTestComp") comp = d;
    CHECK (comp.name == "SIXTestComp" && comp.version == "six-light", "listed from moduleinfo.json without loading it");
    CHECK (r.p->host->poolSize() == 0, "nothing instantiated by the scan");
    String err;
    auto real = cat.resolveForLoad (comp, err);
    CHECK (real.size() == 1 && real[0].version != "six-light", "resolved to the real plug-in when chosen: " << err);
    CHECK (r.p->host->loadIntoSlot (0, 0, real[0], err), "loads after resolving");
}

static void testOffline()
{
    std::cout << "[offline render determinism]" << std::endl;
    auto render = []
    {
        Rig r (512, true);
        r.run (2, sine);
        r.p->host->saveNewPreset();
        r.set ("ch01_fader", -10.0f);
        r.set ("ch01_to03_level", -3.0f);
        r.set ("ch03_pan", 30.0f);
        r.run (2, sine);
        r.p->host->saveNewPreset();
        r.set (gid::morphMode, 1.0f);
        r.set (gid::morphMs, 300.0f);
        double sum = 0;
        for (int i = 0; i < 100; ++i)
        {
            if (i == 10) r.set (gid::presetTarget, 1.0f);
            if (i == 50) r.set (gid::presetTarget, 2.0f);
            auto& b = r.run (1, sine);
            for (int c = 0; c < 2; ++c) for (int s = 0; s < b.getNumSamples(); ++s) sum += b.getSample (c, s) * (s + 1);
        }
        return sum;
    };
    const double a = render(), b = render();
    CHECK (a == b, "two offline renders are bit-identical");
}

static void snapshot (const File& out, bool demo)
{
    Rig r;
    auto& hm = *r.p->host;
    if (demo)
    {
        const char* names[] = { "Dry", "Punch", "Tape", "Room", "Crunch", "Wide" };
        for (int i = 0; i < 6; ++i) hm.channelNames[(size_t) i] = names[i];
        r.set ("ch01_out", 2.0f); r.set ("ch02_out", 3.0f);
        r.set ("ch01_fader", -7.8f); r.set ("ch02_fader", -2.3f); r.set ("ch03_fader", -15.6f);
        r.set ("ch04_fader", -4.9f); r.set ("ch05_fader", -11.2f); r.set ("ch06_fader", -0.9f);
        r.set ("ch02_sendin", 0.3f); r.set ("ch03_sendin", -0.8f); r.set ("ch04_sendin", -2.1f);
        r.set ("ch01_mono", 1.0f); r.set ("ch02_mono", 1.0f); r.set ("ch05_mono", 1.0f);
        r.set ("ch03_pan", -20.0f); r.set ("ch04_pan", 15.0f);
        r.set ("ch02_to01_level", -12.0f); r.set ("ch02_to04_level", -2.3f); r.set ("ch02_to05_level", -10.5f);
        r.set ("ch02_to06_level", -4.8f); r.set ("ch02_to03_level", kMinDb);
        r.set ("ch02_to01_level", kMinDb);   // 02 -> 01 would loop with 01 -> 02 (rejected)
        r.set ("ch02_to05_prepost", 0.0f);
        r.run (4, sine);
        hm.saveNewPreset(); hm.saveNewPreset();
        hm.renamePreset (1, "Punch");
        r.set (gid::presetTarget, 2.0f);
        r.p->selectedChannel = 1;
    }
    r.run (40, sine);
    if (demo)
    {
        String err;
        if (auto d = describe (r, "SIXTestComp"); d.name.isNotEmpty()) hm.loadIntoSlot (1, 0, d, err);
        if (auto d = describe (r, "SIXTestLatency"); d.name.isNotEmpty()) hm.loadIntoSlot (1, 1, d, err);
        r.set ("ch02_fx2_bypass", 1.0f);
        hm.assignMap (hm.slot (1, 0).inst, 0);
    }
    std::unique_ptr<AudioProcessorEditor> ed (r.p->createEditor());
    if (demo && out.getFileName().contains ("browse"))
    {
        auto& cat = hm.catalog;
        while (cat.numFolders() > 0) cat.deleteFolder (0);
        const int rv = cat.createFolder ("Reverb");
        const int dl = cat.createFolder ("Delay");
        cat.createFolder ("Compressors");
        cat.addToFolder (dl, describe (r, "SIXTestLatency"));
        cat.addToFolder (rv, describe (r, "SIXTestWidener"));
        cat.addToFolder (2, describe (r, "SIXTestComp"));
        if (auto* se = dynamic_cast<SIXEditor*> (ed.get())) se->toggleBrowser();
    }
    ed->setSize (1600, 980);
    MessageManager::getInstance()->runDispatchLoopUntil (200);
    r.run (5, sine);
    MessageManager::getInstance()->runDispatchLoopUntil (100);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    out.deleteFile();
    FileOutputStream fos (out);
    PNGImageFormat().writeImageToStream (img, fos);
    std::cout << "snapshot written: " << out.getFullPathName() << std::endl;
}

template <typename T> static void findAll (Component& c, Array<T*>& out)
{
    if (auto* t = dynamic_cast<T*> (&c)) out.add (t);
    for (auto* ch : c.getChildren()) findAll (*ch, out);
}

static void testUi()
{
    std::cout << "[UI: Finder drop, Browse drag, reorder drag, preset buttons]" << std::endl;
    Rig r;
    r.run (4, Rig::dc);
    std::unique_ptr<AudioProcessorEditor> ed (r.p->createEditor());
    ed->setSize (1280, 784);
    scanCatalog (r.p->host->catalog);
    Array<DeviceSlot*> slots;
    findAll (*ed, slots);
    CHECK (slots.size() == 4, "four device slots shown");
    auto& hm = *r.p->host;

    // Finder drop of an installed .vst3 bundle onto FX1 of the selected channel
    CHECK (slots[0]->isInterestedInFileDrag ({ bundle ("SIXTestComp").getFullPathName() }), "slot accepts .vst3 drag");
    slots[0]->filesDropped ({ bundle ("SIXTestComp").getFullPathName() }, 10, 10);
    r.run (2, Rig::dc);
    CHECK (hm.slot (0, 0).inst != nullptr && hm.slot (0, 0).inst->description().name == "SIXTestComp", "Finder drop loaded the effect");
    CHECK (hm.verifiedDragInfo.startsWith ("Finder bundle drop"), "drop logged: " << hm.verifiedDragInfo);

    // Browse list drag (internal drag description) onto FX3
    auto d = describe (r, "SIXTestLatency");
    DragAndDropTarget::SourceDetails sd ("six-plugin:" + d.createIdentifierString(), nullptr, {});
    CHECK (slots[2]->isInterestedInDragSource (sd), "slot accepts Browse drag");
    slots[2]->itemDropped (sd);
    r.run (2, Rig::dc);
    CHECK (hm.slot (0, 2).inst != nullptr && hm.slot (0, 2).inst->description().name == "SIXTestLatency", "Browse drag loaded the effect");

    // text drop with the plug-in name (what a DAW browser might send)
    slots[3]->textDropped ("SIXTestWidener", 5, 5);
    r.run (2, Rig::dc);
    CHECK (hm.slot (0, 3).inst != nullptr, "text drop with a plug-in name resolves");

    // reorder by dragging FX3 onto FX2
    auto* lat = hm.slot (0, 2).inst;
    DragAndDropTarget::SourceDetails mv ("six-slot:0:2", nullptr, {});
    slots[1]->itemDropped (mv);
    r.run (6, Rig::dc);
    CHECK (hm.slot (0, 1).inst == lat, "drag re-order moved FX3 to FX2");

    // preset bar: + saves, an empty preset click changes nothing
    Array<TextButton*> buttons;
    findAll (*ed, buttons);
    TextButton* plus = nullptr;
    for (auto* b : buttons) if (b->getButtonText() == "+") plus = b;
    CHECK (plus != nullptr, "+ button present");
    if (plus != nullptr) plus->triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK (hm.bank.get (0).used && hm.bank.get (0).name.isEmpty(), "+ saved preset 01 without a name");
   #if JUCE_MAC   // native top-level windows need a window manager (not available under Xvfb)
    r.p->host->windows().open (hm.slot (0, 0).inst, "test");
    CHECK (hm.windows().isOpen (hm.slot (0, 0).inst), "native editor window opens");
    hm.windows().closeFor (hm.slot (0, 0).inst);
    CHECK (! hm.windows().isOpen (hm.slot (0, 0).inst), "and closes");
   #endif
    ed.reset();
}

// Audio thread running continuously while the message thread hammers the host layer.
static void stress (int seconds)
{
    std::cout << "[stress: real audio thread + random UI/host operations for " << seconds << " s]" << std::endl;
    Rig r;
    std::vector<PluginDescription> descs { describe (r, "SIXTestComp"), describe (r, "SIXTestLatency"), describe (r, "SIXTestWidener") };
    std::atomic<bool> running { true };
    std::atomic<int64> blocks { 0 };
    std::thread audio ([&]
    {
        AudioBuffer<float> b (2, r.bs);
        MidiBuffer m;
        int64 t = 0;
        while (running.load())
        {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < r.bs; ++i) b.setSample (c, i, sine (t + i, c));
            r.p->processBlock (b, m);
            t += r.bs;
            ++blocks;
            for (int c = 0; c < 2; ++c) if (! std::isfinite (b.getSample (c, 0)) || std::abs (b.getSample (c, 0)) > 50.0f) { std::cout << "  bad sample" << std::endl; }
            std::this_thread::sleep_for (std::chrono::microseconds (300));
        }
    });

    Random rnd (1234);
    auto& hm = *r.p->host;
    const auto end = Time::getMillisecondCounter() + (uint32) seconds * 1000;
    int ops = 0, restores = 0;
    while (Time::getMillisecondCounter() < end)
    {
        const int ch = rnd.nextInt (kNumChannels), s = rnd.nextInt (kNumSlots);
        String err;
        switch (rnd.nextInt (12))
        {
            case 0: case 1: hm.loadIntoSlot (ch, s, descs[(size_t) rnd.nextInt (3)], err); break;
            case 2: hm.removeSlot (ch, s); break;
            case 3: hm.moveSlot (ch, s, rnd.nextInt (kNumSlots)); break;
            case 4: if (rnd.nextBool()) hm.saveNewPreset(); else hm.updatePreset (rnd.nextInt (8)); break;
            case 5: hm.clickPreset (rnd.nextInt (8)); break;
            case 6:
            {
                const int c = rnd.nextInt (kMapBase);
                auto* p = r.p->params.controls[(size_t) c];
                const float v = p->convertFrom0to1 (rnd.nextFloat());
                if (rnd.nextInt (3) == 0 || validateRoutingChange (hm.currentValues(), c, v).isEmpty())
                    ParamRefs::setWithGesture (p, v);
                break;
            }
            case 7:
                if (auto* h = hm.slot (ch, s).inst) { if (hm.assignMap (h, 0) < 0) hm.clearMap (rnd.nextInt (kNumMaps)); }
                else if (hm.map (0).assigned()) hm.clearMap (0);
                break;
            case 8:
                if (rnd.nextInt (4) == 0)
                {
                    MemoryBlock mb;
                    r.p->getStateInformation (mb);
                    r.p->setStateInformation (mb.getData(), (int) mb.getSize());
                    ++restores;
                }
                break;
            case 9: hm.clearPreset (rnd.nextInt (8)); break;
            case 10: r.set (gid::morphMs, (float) rnd.nextInt (300)); r.set (gid::morphMode, 1.0f); break;
            case 11: r.set ("map_01", rnd.nextFloat()); break;
        }
        ++ops;
        hm.pump();
        MessageManager::getInstance()->runDispatchLoopUntil (2);
    }
    running = false;
    audio.join();
    for (int i = 0; i < 20; ++i) { r.run (1, Rig::dc); }
    int occupied = 0;
    for (int ch = 0; ch < kNumChannels; ++ch) for (int s = 0; s < kNumSlots; ++s) occupied += hm.slot (ch, s).inst != nullptr ? 1 : 0;
    std::cout << "  ops " << ops << ", restores " << restores << ", audio blocks " << blocks.load() << ", pool " << hm.poolSize()
              << ", in slots " << occupied << ", prepared " << hm.preparedInstanceCount() << std::endl;
    CHECK (blocks.load() > 1000, "audio thread kept running");
    int mismatches = 0;
    for (int ch = 0; ch < kNumChannels; ++ch) for (int s = 0; s < kNumSlots; ++s) mismatches += r.p->engine.debugSlot (ch, s) != hm.slot (ch, s).inst ? 1 : 0;
    CHECK (mismatches == 0, "UI slot model matches the audio engine (" << mismatches << " mismatches)");
    CHECK (hm.poolSize() <= occupied + hm.preparedInstanceCount() + 2, "no leaked plug-in instances");
    if (hm.poolSize() > occupied + hm.preparedInstanceCount()) std::cout << hm.debugPool() << std::endl;
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    pluginDir = argc > 1 ? File (argv[1]) : File::getCurrentWorkingDirectory();
    if (argc > 2 && String (argv[2]) == "--stress") { stress (argc > 3 ? String (argv[3]).getIntValue() : 10); std::cout << checks - failures << "/" << checks << " checks passed" << std::endl; return failures == 0 ? 0 : 1; }
    if (argc > 2 && String (argv[2]) == "--ui") { testUi(); removeTestBundles(); std::cout << checks - failures << "/" << checks << " checks passed" << std::endl; return failures == 0 ? 0 : 1; }
    if (argc > 3 && String (argv[2]) == "--snapshot") { snapshot (File (argv[3]), argc > 4); return 0; }
    testDefaults();
    testMonoPan();
    testRouting();
    testHostingChain();
    testMorph();
    testPluginMorphAndState();
    testFolders();
    testSafeScan();
    testOffline();
    removeTestBundles();
    std::cout << std::endl << checks - failures << "/" << checks << " checks passed" << std::endl;
    return failures == 0 ? 0 : 1;
}
