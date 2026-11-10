#include "HostManager.h"
#include "../UI/PluginWindows.h"

namespace six
{
using namespace juce;

namespace
{
    struct InstanceListener : public AudioProcessorListener
    {
        InstanceListener (HostManager& m, HostedInstance& i) : hm (m), inst (i) {}
        void audioProcessorParameterChanged (AudioProcessor*, int index, float v) override   { hm.onHostedParamChanged (inst, index, v); }
        void audioProcessorChanged (AudioProcessor*, const ChangeDetails& d) override        { hm.onHostedChanged (inst, d); }
        void audioProcessorParameterChangeGestureBegin (AudioProcessor*, int index) override { hm.onHostedGesture (inst, index, true); }
        void audioProcessorParameterChangeGestureEnd (AudioProcessor*, int index) override   { hm.onHostedGesture (inst, index, false); }
        HostManager& hm;
        HostedInstance& inst;
    };

    bool sameIdentity (const PluginDescription& a, const PluginDescription& b)
    {
        return a.pluginFormatName == b.pluginFormatName && a.uniqueId == b.uniqueId && a.name == b.name
            && a.manufacturerName == b.manufacturerName;
    }

    String hostedParamID (AudioPluginInstance& p, int index)
    {
        if (auto* hp = dynamic_cast<HostedAudioProcessorParameter*> (p.getParameters()[index]))
            return hp->getParameterID();
        return String (index);
    }
}

String HostManager::SlotModel::displayName() const
{
    if (inst != nullptr) return inst->description().name;
    if (missing) return missingDesc.name;
    return {};
}

HostManager::HostManager (AudioProcessor& o, AudioEngine& e, ParamRefs& p)
    : owner (o), engine (e), params (p)
{
    addDefaultFormatsToManager (formats);
    for (int k = 0; k < kNumMaps; ++k) { mapUidA[(size_t) k].store (0); mapIndexA[(size_t) k].store (-1); }
    windowManager = std::make_unique<PluginWindowManager>();
    startTimerHz (30);
}

HostManager::~HostManager()
{
    stopTimer();
    windowManager->closeAll();
    // The audio engine is stopped by now (processor destruction); delete everything.
    for (auto& p : prep) { delete p.sent; p.sent = nullptr; }
    const ScopedLock sl (poolLock);
    pool.clear();
}

uint64 HostManager::newUid()
{
    uint64 u = 0;
    while (u == 0) u = (uint64) Random::getSystemRandom().nextInt64();
    return u;
}

// ------------------------------------------------------------------------------ instances
HostedInstance* HostManager::createInstance (const PluginDescription& d, const MemoryBlock* state, uint64 uid, String& error)
{
    if (PluginCatalog::isSelf (d)) { error = "SIX can't be loaded inside itself."; return nullptr; }
    if (d.isInstrument) { error = d.name + " is an instrument - SIX hosts effects."; return nullptr; }

    String err;
    for (auto* fmt : formats.getFormats())
        if (fmt->getName() == d.pluginFormatName && fmt->requiresUnblockedMessageThreadDuringCreation (d))
        {
            error = "async";   // AUv3-style: must be created asynchronously (see loadIntoSlot)
            return nullptr;
        }
    auto p = formats.createPluginInstance (d, sampleRate, blockSize, err);
    if (p == nullptr) { error = "Couldn't load " + d.name + (err.isNotEmpty() ? ": " + err : String()); return nullptr; }
    return wrapInstance (std::move (p), d, state, uid);
}

HostedInstance* HostManager::wrapInstance (std::unique_ptr<AudioPluginInstance> p, const PluginDescription& d, const MemoryBlock* state, uint64 uid)
{
    auto h = std::make_unique<HostedInstance> (std::move (p), d, uid != 0 ? uid : newUid());
    h->configureLayout();
    if (state != nullptr && state->getSize() > 0)
        h->plugin().setStateInformation (state->getData(), (int) state->getSize());
    h->prepare (sampleRate, blockSize, offline);
    h->cleanHash = HostedInstance::hashState (h->getState());
    if (state != nullptr && state->getSize() > 0) h->cleanHash = HostedInstance::hashState (*state);
    h->id = nextInstanceId++;
    h->listener = std::make_unique<InstanceListener> (*this, *h);
    h->plugin().addListener (h->listener.get());

    auto* raw = h.get();
    const ScopedLock sl (poolLock);
    pool.push_back (std::move (h));
    return raw;
}

void HostManager::destroyInstance (HostedInstance* h)
{
    windowManager->closeFor (h);
    const ScopedLock sl (poolLock);
    pool.erase (std::remove_if (pool.begin(), pool.end(), [h] (auto& u) { return u.get() == h; }), pool.end());
}

void HostManager::postSlot (int ch, int s, HostedInstance* inst, uint64 uid, int fadeSamples)
{
    EngineCommand c;
    c.type = EngineCommand::setSlot;
    c.a = ch; c.b = s; c.c = fadeSamples;
    c.inst = inst; c.uid = uid;
    if (inst != nullptr) ++inst->audioRefs;
    if (! engine.post (c) && inst != nullptr) --inst->audioRefs;
}

bool HostManager::loadIntoSlot (int ch, int s, const PluginDescription& d, String& error, const MemoryBlock* state, uint64 uid, int fadeMs)
{
    if (! isPositiveAndBelow (ch, kNumChannels) || ! isPositiveAndBelow (s, kNumSlots)) return false;
    auto* h = createInstance (d, state, uid, error);
    auto& m = model[(size_t) ch][(size_t) s];
    if (h == nullptr && error == "async")
    {
        error = {};
        WeakReference<HostManager> weak (this);
        auto stateCopy = std::make_shared<MemoryBlock> (state != nullptr ? *state : MemoryBlock());
        const bool hasState = state != nullptr;
        formats.createPluginInstanceAsync (d, sampleRate, blockSize,
            [weak, ch, s, d, stateCopy, hasState, uid, fadeMs] (std::unique_ptr<AudioPluginInstance> inst, const String& e2)
            {
                auto* hm = weak.get();
                if (hm == nullptr) return;
                if (inst == nullptr) { hm->toast ("Couldn't load " + d.name + ": " + e2); return; }
                auto* hi = hm->wrapInstance (std::move (inst), d, hasState ? stateCopy.get() : nullptr, uid);
                hm->postSlot (ch, s, hi, hi->placement(), (int) (fadeMs * 0.001 * hm->sampleRate));
                auto& mm = hm->model[(size_t) ch][(size_t) s];
                mm.inst = hi; mm.uid = hi->placement(); mm.missing = false; mm.missingState.reset();
                hm->updateMapLabels();
                hm->sendChangeMessage();
            });
        return true;
    }
    if (h == nullptr)
    {
        if (state != nullptr)   // restoring: keep name + state for a later reload
        {
            m.missing = true;
            m.missingDesc = d;
            m.missingState = *state;
            m.uid = uid;
        }
        sendChangeMessage();
        return false;
    }

    // Maps bound to a replaced device stay bound to the old placement and are never re-targeted.
    postSlot (ch, s, h, h->placement(), (int) (fadeMs * 0.001 * sampleRate));
    m.inst = h;
    m.uid = h->placement();
    m.missing = false;
    m.missingState.reset();
    updateMapLabels();
    sendChangeMessage();
    return true;
}

void HostManager::removeSlot (int ch, int s)
{
    auto& m = model[(size_t) ch][(size_t) s];
    if (m.inst != nullptr)
    {
        windowManager->closeFor (m.inst);
        postSlot (ch, s, nullptr, 0, (int) (0.03 * sampleRate));
    }
    m = SlotModel();
    updateMapLabels();
    sendChangeMessage();
}

void HostManager::moveSlot (int ch, int from, int to)
{
    if (from == to || ! isPositiveAndBelow (from, kNumSlots) || ! isPositiveAndBelow (to, kNumSlots)) return;
    EngineCommand c;
    c.type = EngineCommand::moveSlot;
    c.a = ch; c.b = from; c.c = to;
    engine.post (c);

    auto& row = model[(size_t) ch];
    auto b = row.begin();
    if (from < to) std::rotate (b + from, b + from + 1, b + to + 1);
    else           std::rotate (b + to, b + from, b + from + 1);

    // Bypass follows the device (its parameter is per position, so move the values).
    std::array<float, (size_t) kNumSlots> byp {};
    for (int i = 0; i < kNumSlots; ++i) byp[(size_t) i] = params.raw[(size_t) bypassControl (ch, i)]->load();
    std::array<int, (size_t) kNumSlots> perm { 0, 1, 2, 3 };
    auto pb = perm.begin();
    if (from < to) std::rotate (pb + from, pb + from + 1, pb + to + 1);
    else           std::rotate (pb + to, pb + from, pb + from + 1);
    for (int i = 0; i < kNumSlots; ++i)
        if (byp[(size_t) perm[(size_t) i]] != byp[(size_t) i])
            ParamRefs::setWithGesture (params.controls[(size_t) bypassControl (ch, i)], byp[(size_t) perm[(size_t) i]]);

    updateMapLabels();
    sendChangeMessage();
}

int HostManager::firstEmptySlot (int ch) const
{
    for (int s = 0; s < kNumSlots; ++s)
        if (! model[(size_t) ch][(size_t) s].occupied()) return s;
    return -1;
}

bool HostManager::isReferenced (HostedInstance* h) const
{
    for (auto& row : model) for (auto& m : row) if (m.inst == h) return true;
    for (auto& p : prep) for (auto* c : p.cache) if (c == h) return true;
    return false;
}

void HostManager::collectGarbage()
{
    std::vector<HostedInstance*> dead;
    {
        const ScopedLock sl (poolLock);
        for (auto& u : pool)
            if (u->audioRefs <= 0 && ! isReferenced (u.get())) dead.push_back (u.get());
    }
    for (auto* h : dead) destroyInstance (h);
}

void HostManager::logDrag (const String& what)
{
    verifiedDragInfo = what;
    auto dir = File::getSpecialLocation (File::userApplicationDataDirectory)
#if JUCE_MAC
                   .getChildFile ("Application Support")
#endif
                   .getChildFile ("SIX Mixer Host");
    dir.createDirectory();
    dir.getChildFile ("DragLog.txt").appendText (Time::getCurrentTime().toString (true, true) + "  " + what + "\n");
}

String HostManager::debugPool() const
{
    String r;
    for (auto& u : pool)
    {
        bool inModel = false, inPrep = false;
        for (auto& row : model) for (auto& m : row) inModel |= m.inst == u.get();
        for (auto& p : prep) for (auto* c : p.cache) inPrep |= c == u.get();
        r << u->description().name << " refs=" << u->audioRefs << " model=" << (int) inModel << " prep=" << (int) inPrep << "\n";
    }
    return r;
}

int HostManager::preparedInstanceCount() const
{
    int n = 0;
    for (auto& p : prep) for (auto* c : p.cache) n += c != nullptr ? 1 : 0;
    return n;
}

void HostManager::prepareAll (double sr, int bs, bool off)
{
    sampleRate = sr;
    blockSize = bs;
    offline = off;
    const ScopedLock sl (poolLock);
    for (auto& u : pool) u->prepare (sr, bs, off);
}

ControlValues HostManager::currentValues() const
{
    ControlValues v {};
    const bool running = engine.processedBlocks.load() > 0;
    for (int i = 0; i < kNumControls; ++i)
        v[(size_t) i] = running ? engine.effective[(size_t) i].load() : params.raw[(size_t) i]->load();
    return v;
}

// ------------------------------------------------------------------------------ presets
bool HostManager::updatePreset (int i)
{
    if (! isPositiveAndBelow (i, kNumPresets)) return false;
    auto& p = bank.get (i);
    p.used = true;
    p.values = currentValues();
    for (int c = 0; c < kNumControls; ++c)   // store discrete controls as clean values
    {
        const auto k = controlInfo (c).kind;
        if (k == ControlKind::toggle || k == ControlKind::out) p.values[(size_t) c] = (float) roundToInt (p.values[(size_t) c]);
        if (isSoloControl (c)) p.values[(size_t) c] = 0.0f;
    }

    for (int ch = 0; ch < kNumChannels; ++ch)
        for (int s = 0; s < kNumSlots; ++s)
        {
            auto& snap = p.slots[(size_t) (ch * kNumSlots + s)];
            const auto& m = model[(size_t) ch][(size_t) s];
            snap = SlotSnapshot();
            if (m.inst != nullptr)
            {
                snap.hasPlugin = true;
                snap.desc = m.inst->description();
                snap.state = m.inst->getState();
                snap.hash = HostedInstance::hashState (snap.state);
                snap.uid = m.uid;
                m.inst->cleanHash = snap.hash;
                m.inst->dirty = false;
            }
            else if (m.missing)
            {
                snap.hasPlugin = true;
                snap.desc = m.missingDesc;
                snap.state = m.missingState;
                snap.hash = HostedInstance::hashState (snap.state);
                snap.uid = m.uid;
            }
        }

    EngineCommand c;
    c.type = EngineCommand::setPresetMix;
    c.a = i;
    c.values = p.values;
    engine.post (c);
    for (int q = 0; q < kNumPresets; ++q) prep[(size_t) q].signature = 0;   // re-evaluate all
    sendChangeMessage();
    return true;
}

int HostManager::saveNewPreset()
{
    const int i = bank.nextFree();
    if (i < 0) { toast ("All 64 preset slots are used."); return -1; }
    bank.get (i).name = {};
    updatePreset (i);
    return i;
}

void HostManager::clearPreset (int i)
{
    bank.get (i) = Preset();
    EngineCommand c;
    c.type = EngineCommand::clearPresetMix;
    c.a = i;
    engine.post (c);
    invalidatePrepared (i);
    sendChangeMessage();
}

void HostManager::renamePreset (int i, const String& name)
{
    bank.get (i).name = name.trim();
    sendChangeMessage();
}

void HostManager::clickPreset (int i)
{
    if (! isPositiveAndBelow (i, kNumPresets) || ! bank.get (i).used) return;   // empty: no sound change
    const int current = roundToInt (params.rawPresetTarget->load());
    if (current == i + 1)
    {
        EngineCommand c;
        c.type = EngineCommand::retrigger;
        c.a = i;
        engine.post (c);
    }
    else
        ParamRefs::setWithGesture (params.presetTarget, (float) (i + 1));
}

// ------------------------------------------------------------------------------ prepared morph targets
std::array<HostManager::DesiredSlot, (size_t) (kNumChannels * kNumSlots)> HostManager::computeDesired (int p, uint64& sig) const
{
    std::array<DesiredSlot, (size_t) (kNumChannels * kNumSlots)> r;
    const auto& pr = bank.get (p);
    sig = 1469598103934665603ull;
    auto mix = [&sig] (uint64 v) { sig ^= v; sig *= 1099511628211ull; };

    for (int i = 0; i < kNumChannels * kNumSlots; ++i)
    {
        const auto& snap = pr.slots[(size_t) i];
        const auto& m = model[(size_t) (i / kNumSlots)][(size_t) (i % kNumSlots)];
        auto& d = r[(size_t) i];
        d.snap = &snap;
        if (! snap.hasPlugin)
            d.action = m.inst != nullptr ? PreparedSlot::toEmpty : PreparedSlot::keep;
        else if (m.inst != nullptr && sameIdentity (m.inst->description(), snap.desc) && ! m.inst->dirty.load() && m.inst->cleanHash == snap.hash)
            d.action = PreparedSlot::keep;
        else if (failedIdentifiers.contains (snap.desc.createIdentifierString()))
        {
            d.missing = true;
            d.action = m.inst != nullptr ? PreparedSlot::toEmpty : PreparedSlot::keep;
        }
        else
            d.action = PreparedSlot::toInstance;

        mix ((uint64) d.action + 7);
        if (d.action == PreparedSlot::toInstance) { mix (snap.hash); mix (snap.uid); mix ((uint64) snap.desc.uniqueId); }
        if (d.action == PreparedSlot::keep && m.inst != nullptr) mix ((uint64) (pointer_sized_int) m.inst);
    }
    return r;
}

void HostManager::invalidatePrepared (int p)
{
    auto& e = prep[(size_t) p];
    if (e.sent != nullptr)
    {
        EngineCommand c;
        c.type = EngineCommand::setPrepared;
        c.a = p;
        c.set = nullptr;
        engine.post (c);   // engine returns the old set via preparedReleased
        e.sent = nullptr;
    }
    e.signature = 0;
    e.cache.fill (nullptr);
}

void HostManager::maintainPrepared()
{
    int budget = 2;                       // instance creations per call (keeps the UI responsive)
    constexpr int maxPrepared = 48;

    for (int p = 0; p < kNumPresets; ++p)
    {
        auto& e = prep[(size_t) p];
        if (! bank.get (p).used)
        {
            if (e.sent != nullptr || e.signature != 0) invalidatePrepared (p);
            continue;
        }

        uint64 sig = 0;
        const auto desired = computeDesired (p, sig);
        if (sig == e.signature && e.sent != nullptr) continue;

        if (sig != e.signature && e.sent != nullptr)
        {
            auto keepCache = e.cache;
            invalidatePrepared (p);
            e.cache = keepCache;          // reuse matching instances below
        }

        // Create the instances this preset needs (reusing cached ones that still match).
        std::array<HostedInstance*, (size_t) (kNumChannels * kNumSlots)> next {};
        bool complete = true;
        for (int i = 0; i < kNumChannels * kNumSlots; ++i)
        {
            const auto& d = desired[(size_t) i];
            if (d.action != PreparedSlot::toInstance) continue;
            auto* cached = e.cache[(size_t) i];
            if (cached != nullptr && sameIdentity (cached->description(), d.snap->desc) && cached->cleanHash == d.snap->hash
                && ! cached->dirty.load() && cached->placement() == d.snap->uid)
            {
                next[(size_t) i] = cached;
                continue;
            }
            if (budget <= 0 || preparedInstanceCount() >= maxPrepared) { complete = false; continue; }
            --budget;
            String err;
            auto* h = createInstance (d.snap->desc, &d.snap->state, d.snap->uid, err);
            if (h == nullptr) { failedIdentifiers.addIfNotAlreadyThere (d.snap->desc.createIdentifierString()); complete = false; continue; }
            next[(size_t) i] = h;
            e.cache[(size_t) i] = h;      // keep referenced while incomplete
        }
        if (! complete) continue;

        auto* set = new PreparedSet();
        set->preset = p;
        set->complete = true;
        for (int i = 0; i < kNumChannels * kNumSlots; ++i)
        {
            const auto& d = desired[(size_t) i];
            auto& s = set->slots[(size_t) i];
            s.action = d.action;
            if (d.action == PreparedSlot::toInstance)
            {
                s.inst = next[(size_t) i];
                s.uid = d.snap->uid;
                ++s.inst->audioRefs;
            }
        }
        e.cache = next;
        EngineCommand c;
        c.type = EngineCommand::setPrepared;
        c.a = p;
        c.set = set;
        if (engine.post (c))
        {
            e.sent = set;
            e.signature = sig;
        }
        else
        {
            for (auto& s : set->slots) if (s.inst != nullptr) --s.inst->audioRefs;
            delete set;
        }
    }
}

// ------------------------------------------------------------------------------ notices
void HostManager::handleNotice (const EngineNotice& n)
{
    switch (n.type)
    {
        case EngineNotice::instanceReleased:
            if (n.inst != nullptr) --n.inst->audioRefs;
            break;

        case EngineNotice::slotChanged:
        {
            auto& m = model[(size_t) n.a][(size_t) n.b];
            if (n.flag)   // re-order: the MT model was already rotated; only sync pointers
            {
                m.inst = n.inst;
                if (n.inst != nullptr) m.uid = n.uid;
            }
            else
            {
                if (m.inst != n.inst && m.inst != nullptr) windowManager->closeFor (m.inst);
                m.inst = n.inst;
                if (n.inst != nullptr) { m.uid = n.uid; m.missing = false; m.missingState.reset(); }
                else if (! m.missing) m.uid = 0;
            }
            updateMapLabels();
            sendChangeMessage();
            break;
        }

        case EngineNotice::preparedReleased:
        {
            auto* set = n.set;
            if (set == nullptr) break;
            if (! n.flag)
                for (auto& s : set->slots) if (s.inst != nullptr) --s.inst->audioRefs;
            auto& e = prep[(size_t) set->preset];
            if (e.sent == set || n.flag)
            {
                if (e.sent == set) e.sent = nullptr;
                e.signature = 0;
                if (n.flag) e.cache.fill (nullptr);   // instances now live in slots
            }
            delete set;
            break;
        }

        case EngineNotice::morphStarted:
        {
            const auto& pr = bank.get (n.a);
            for (int i = 0; i < kNumChannels * kNumSlots; ++i)
            {
                auto& m = model[(size_t) (i / kNumSlots)][(size_t) (i % kNumSlots)];
                const auto& snap = pr.slots[(size_t) i];
                if (! snap.hasPlugin) { if (m.missing) m = SlotModel(); continue; }
                if (failedIdentifiers.contains (snap.desc.createIdentifierString()))
                {
                    m.missing = true;
                    m.missingDesc = snap.desc;
                    m.missingState = snap.state;
                    m.uid = snap.uid;
                }
            }
            for (auto& p : prep) p.signature = 0;
            sendChangeMessage();
            break;
        }

        case EngineNotice::morphNeedsPlugins:
        {
            uint64 sig;
            const auto desired = computeDesired (n.a, sig);
            for (int i = 0; i < kNumChannels * kNumSlots; ++i)
            {
                const int ch = i / kNumSlots, s = i % kNumSlots;
                const auto& d = desired[(size_t) i];
                if (d.action == PreparedSlot::toInstance)
                {
                    String err;
                    if (auto* h = createInstance (d.snap->desc, &d.snap->state, d.snap->uid, err))
                    {
                        postSlot (ch, s, h, h->placement(), n.b);
                        model[(size_t) ch][(size_t) s].inst = h;
                        model[(size_t) ch][(size_t) s].uid = h->placement();
                        model[(size_t) ch][(size_t) s].missing = false;
                    }
                    else
                    {
                        failedIdentifiers.addIfNotAlreadyThere (d.snap->desc.createIdentifierString());
                        auto& m = model[(size_t) ch][(size_t) s];
                        if (m.inst != nullptr) postSlot (ch, s, nullptr, 0, n.b);
                        m = SlotModel();
                        m.missing = true; m.missingDesc = d.snap->desc; m.missingState = d.snap->state; m.uid = d.snap->uid;
                    }
                }
                else if (d.action == PreparedSlot::toEmpty)
                {
                    postSlot (ch, s, nullptr, 0, n.b);
                    model[(size_t) ch][(size_t) s].inst = nullptr;
                }
            }
            updateMapLabels();
            sendChangeMessage();
            break;
        }

        case EngineNotice::morphFinished:
            sendChangeMessage();
            break;

        case EngineNotice::routingRejected:
        {
            const auto g = graphFromValues (currentValues());
            toast ("Routing change blocked: it would create a feedback loop. The last valid routing is kept.");
            ignoreUnused (g);
            break;
        }
    }
}

void HostManager::pump()
{
    EngineNotice n;
    int count = 0;
    while (engine.popNotice (n) && count++ < 4096) handleNotice (n);

    if (++prepTick >= 6) { prepTick = 0; maintainPrepared(); }
    collectGarbage();

    const int needed = engine.neededLatency.load();
    if (needed != owner.getLatencySamples())
        owner.setLatencySamples (needed);
}

// ------------------------------------------------------------------------------ Learn / Map
int HostManager::findMap (uint64 uid, int idx) const
{
    for (int k = 0; k < kNumMaps; ++k)
        if (maps[(size_t) k].uid == uid && maps[(size_t) k].paramIndex == idx) return k;
    return -1;
}

int HostManager::assignMap (HostedInstance* inst, int paramIndex)
{
    if (inst == nullptr) return -1;
    auto& ps = inst->plugin().getParameters();
    if (! isPositiveAndBelow (paramIndex, ps.size())) return -1;
    if (const int existing = findMap (inst->placement(), paramIndex); existing >= 0) return existing;

    int k = -1;
    for (int i = 0; i < kNumMaps; ++i) if (! maps[(size_t) i].assigned()) { k = i; break; }
    if (k < 0) { toast ("All 32 Map lanes are in use."); return -1; }

    auto& m = maps[(size_t) k];
    m.uid = inst->placement();
    m.paramIndex = paramIndex;
    m.paramID = hostedParamID (inst->plugin(), paramIndex);
    m.paramName = ps[paramIndex]->getName (64);
    m.pluginName = inst->description().name;

    // Start the lane at the plug-in's current value, so nothing jumps.
    const float v = ps[paramIndex]->getValue();
    engine.lastPushedMap[(size_t) k].store (v);
    ParamRefs::setWithGesture (params.controls[(size_t) mapControl (k)], v);

    mapUidA[(size_t) k].store (m.uid);
    mapIndexA[(size_t) k].store (paramIndex);
    EngineCommand c;
    c.type = EngineCommand::setMap;
    c.a = k; c.b = paramIndex; c.uid = m.uid;
    engine.post (c);
    updateMapLabels();
    sendChangeMessage();
    return k;
}

void HostManager::clearMap (int k)
{
    maps[(size_t) k] = MapModel();
    mapUidA[(size_t) k].store (0);
    mapIndexA[(size_t) k].store (-1);
    EngineCommand c;
    c.type = EngineCommand::setMap;
    c.a = k; c.b = -1; c.uid = 0;
    engine.post (c);
    updateMapLabels();
    sendChangeMessage();
}

void HostManager::clearMapsForUid (uint64 uid)
{
    if (uid == 0) return;
    for (int k = 0; k < kNumMaps; ++k) if (maps[(size_t) k].uid == uid) clearMap (k);
}

String HostManager::mapLocation (int k) const
{
    const auto& m = maps[(size_t) k];
    if (! m.assigned()) return {};
    for (int ch = 0; ch < kNumChannels; ++ch)
        for (int s = 0; s < kNumSlots; ++s)
            if (model[(size_t) ch][(size_t) s].inst != nullptr && model[(size_t) ch][(size_t) s].uid == m.uid)
                return "CH " + twoDigits (ch + 1) + " FX" + String (s + 1);
    return "not loaded";
}

void HostManager::updateMapLabels()
{
    bool changed = false;
    for (int k = 0; k < kNumMaps; ++k)
    {
        auto* p = params.maps[(size_t) k];
        if (p == nullptr) continue;
        const auto& m = maps[(size_t) k];
        const auto label = m.assigned() ? mapLocation (k) + " " + m.paramName : String();
        if (label != p->getLabelText()) { p->setLabelText (label); changed = true; }
    }
    if (changed)
        owner.updateHostDisplay (AudioProcessorListener::ChangeDetails().withParameterInfoChanged (true));
}

void HostManager::onHostedParamChanged (HostedInstance& inst, int index, float value)
{
    const uint64 uid = inst.placement();
    for (int k = 0; k < kNumMaps; ++k)
    {
        if (mapUidA[(size_t) k].load() != uid || mapIndexA[(size_t) k].load() != index) continue;
        if (std::abs (value - engine.lastPushedMap[(size_t) k].load()) < 1.0e-4f) return;   // our own echo
        engine.lastPushedMap[(size_t) k].store (value);
        if (auto* p = params.controls[(size_t) mapControl (k)])
            p->setValueNotifyingHost (value);   // recordable in Live (inside the plug-in's gesture)
        return;
    }

    if (learnArmed.load() && (learnUid.load() == 0 || learnUid.load() == uid))
    {
        learnArmed = false;
        WeakReference<HostManager> weak (this);
        auto* instPtr = &inst;
        MessageManager::callAsync ([weak, instPtr, index]
        {
            if (auto* hm = weak.get())
            {
                const ScopedLock sl (hm->poolLock);
                for (auto& u : hm->pool)
                    if (u.get() == instPtr)
                    {
                        const int k = hm->assignMap (instPtr, index);
                        if (k >= 0) hm->toast ("Learned: Map " + twoDigits (k + 1) + " = " + hm->maps[(size_t) k].paramName);
                        break;
                    }
            }
        });
        return;
    }
    inst.dirty = true;
}

void HostManager::onHostedGesture (HostedInstance& inst, int index, bool begin)
{
    const uint64 uid = inst.placement();
    for (int k = 0; k < kNumMaps; ++k)
        if (mapUidA[(size_t) k].load() == uid && mapIndexA[(size_t) k].load() == index)
        {
            if (auto* p = params.controls[(size_t) mapControl (k)])
            {
                if (begin) p->beginChangeGesture();
                else       p->endChangeGesture();
            }
            return;
        }
}

void HostManager::onHostedChanged (HostedInstance& inst, const AudioProcessorListener::ChangeDetails& d)
{
    if (d.programChanged || d.nonParameterStateChanged) inst.dirty = true;
}

// ------------------------------------------------------------------------------ state
std::unique_ptr<XmlElement> HostManager::saveState()
{
    auto e = std::make_unique<XmlElement> ("HOST");

    auto* names = e->createNewChildElement ("CHANNELS");
    for (int ch = 0; ch < kNumChannels; ++ch)
        names->createNewChildElement ("CH")->setAttribute ("name", channelNames[(size_t) ch]);

    auto* slots = e->createNewChildElement ("SLOTS");
    for (int ch = 0; ch < kNumChannels; ++ch)
        for (int s = 0; s < kNumSlots; ++s)
        {
            const auto& m = model[(size_t) ch][(size_t) s];
            SlotSnapshot snap;
            if (m.inst != nullptr)
            {
                snap.hasPlugin = true;
                snap.desc = m.inst->description();
                snap.state = m.inst->getState();
                snap.hash = m.inst->dirty.load() ? HostedInstance::hashState (snap.state) : m.inst->cleanHash;
                snap.uid = m.uid;
            }
            else if (m.missing)
            {
                snap.hasPlugin = true;
                snap.desc = m.missingDesc;
                snap.state = m.missingState;
                snap.hash = HostedInstance::hashState (snap.state);
                snap.uid = m.uid;
            }
            if (! snap.hasPlugin) continue;
            auto se = snap.toXml ("SLOT");
            se->setAttribute ("ch", ch);
            se->setAttribute ("s", s);
            se->setAttribute ("dirty", m.inst != nullptr && m.inst->dirty.load());
            slots->addChildElement (se.release());
        }

    e->addChildElement (bank.toXml().release());

    auto* me = e->createNewChildElement ("MAPS");
    for (int k = 0; k < kNumMaps; ++k)
    {
        const auto& m = maps[(size_t) k];
        if (! m.assigned()) continue;
        auto* x = me->createNewChildElement ("MAP");
        x->setAttribute ("k", k);
        x->setAttribute ("uid", String::toHexString ((int64) m.uid));
        x->setAttribute ("paramID", m.paramID);
        x->setAttribute ("paramIndex", m.paramIndex);
        x->setAttribute ("paramName", m.paramName);
        x->setAttribute ("pluginName", m.pluginName);
    }

    // Values currently held by a morph (the parameter isn't in charge of them).
    auto* h = e->createNewChildElement ("HOLDS");
    h->setAttribute ("activePreset", engine.activePreset.load());
    StringArray mask;
    ControlValues hv {};
    for (int i = 0; i < kNumControls; ++i)
    {
        // a parameter changed after the engine's last block releases the hold
        const bool held = engine.held[(size_t) i].load() && engine.seenParam[(size_t) i].load() == params.raw[(size_t) i]->load();
        mask.add (held ? "1" : "0");
        float v = engine.effective[(size_t) i].load();
        const auto k = controlInfo (i).kind;
        if (k == ControlKind::toggle || k == ControlKind::out) v = (float) roundToInt (v);
        hv[(size_t) i] = v;
    }
    h->setAttribute ("mask", mask.joinIntoString (""));
    h->addTextElement (PresetBank::encodeValues (hv));
    return e;
}

void HostManager::restoreState (const XmlElement& e)
{
    // 1. Clear current slots, maps and prepared sets.
    for (int ch = 0; ch < kNumChannels; ++ch)
        for (int s = 0; s < kNumSlots; ++s)
        {
            auto& m = model[(size_t) ch][(size_t) s];
            if (m.inst != nullptr) windowManager->closeFor (m.inst);
            m = SlotModel();
        }
    {
        EngineCommand c;
        c.type = EngineCommand::clearAllSlots;   // releases whatever the engine holds
        engine.post (c);
    }
    for (int p = 0; p < kNumPresets; ++p) invalidatePrepared (p);
    for (int k = 0; k < kNumMaps; ++k) if (maps[(size_t) k].assigned()) clearMap (k);
    failedIdentifiers.clear();

    // 2. Names
    if (auto* names = e.getChildByName ("CHANNELS"))
    {
        int ch = 0;
        for (auto* c : names->getChildWithTagNameIterator ("CH"))
            if (ch < kNumChannels) channelNames[(size_t) ch++] = c->getStringAttribute ("name");
    }

    // 3. Presets
    if (auto* pe = e.getChildByName ("PRESETS")) bank.fromXml (*pe);
    else bank.clearAll();
    for (int p = 0; p < kNumPresets; ++p)
    {
        EngineCommand c;
        c.a = p;
        if (bank.get (p).used) { c.type = EngineCommand::setPresetMix; c.values = bank.get (p).values; }
        else c.type = EngineCommand::clearPresetMix;
        engine.post (c);
    }

    // 4. Plug-ins (loaded here on the message thread, never on the audio thread)
    if (auto* slots = e.getChildByName ("SLOTS"))
        for (auto* se : slots->getChildWithTagNameIterator ("SLOT"))
        {
            const int ch = se->getIntAttribute ("ch", -1), s = se->getIntAttribute ("s", -1);
            if (! isPositiveAndBelow (ch, kNumChannels) || ! isPositiveAndBelow (s, kNumSlots)) continue;
            const auto snap = SlotSnapshot::fromXml (*se);
            if (! snap.hasPlugin) continue;
            String err;
            if (loadIntoSlot (ch, s, snap.desc, err, &snap.state, snap.uid, 0))
            {
                auto* h = model[(size_t) ch][(size_t) s].inst;
                h->cleanHash = snap.hash;
                h->dirty = se->getBoolAttribute ("dirty");
            }
            else
                failedIdentifiers.addIfNotAlreadyThere (snap.desc.createIdentifierString());
        }

    // 5. Maps: re-resolve the parameter index from its ID on the same device instance.
    if (auto* me = e.getChildByName ("MAPS"))
        for (auto* x : me->getChildWithTagNameIterator ("MAP"))
        {
            const int k = x->getIntAttribute ("k", -1);
            if (! isPositiveAndBelow (k, kNumMaps)) continue;
            auto& m = maps[(size_t) k];
            m.uid = (uint64) x->getStringAttribute ("uid").getHexValue64();
            m.paramID = x->getStringAttribute ("paramID");
            m.paramIndex = x->getIntAttribute ("paramIndex", -1);
            m.paramName = x->getStringAttribute ("paramName");
            m.pluginName = x->getStringAttribute ("pluginName");
            for (auto& row : model)
                for (auto& sm : row)
                    if (sm.inst != nullptr && sm.uid == m.uid)
                    {
                        auto& ps = sm.inst->plugin().getParameters();
                        for (int i = 0; i < ps.size(); ++i)
                            if (hostedParamID (sm.inst->plugin(), i) == m.paramID) { m.paramIndex = i; break; }
                    }
            mapUidA[(size_t) k].store (m.uid);
            mapIndexA[(size_t) k].store (m.paramIndex);
            engine.lastPushedMap[(size_t) k].store (params.raw[(size_t) mapControl (k)]->load());
            EngineCommand c;
            c.type = EngineCommand::setMap;
            c.a = k; c.b = m.paramIndex; c.uid = m.uid;
            engine.post (c);
        }
    updateMapLabels();

    // 6. Morph holds (after parameters were restored by the processor)
    EngineCommand hc;
    hc.type = EngineCommand::restoreHolds;
    hc.a = -1;
    if (auto* h = e.getChildByName ("HOLDS"))
    {
        hc.a = h->getIntAttribute ("activePreset", -1);
        const auto mask = h->getStringAttribute ("mask");
        hc.values = PresetBank::decodeValues (h->getAllSubText());
        for (int i = 0; i < kNumControls; ++i) hc.mask[(size_t) i] = mask[i] == '1';
    }
    engine.post (hc);
    sendChangeMessage();
}
}
