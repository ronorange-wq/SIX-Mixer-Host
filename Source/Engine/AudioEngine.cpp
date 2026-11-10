#include "AudioEngine.h"

namespace six
{
using namespace juce;

namespace
{
    constexpr int kChannelRingSize = 1 << 17;   // ~2.7 s at 48 kHz of latency-compensation headroom
    constexpr int kSlotRingSize    = 1 << 16;

    inline float smoothstep (float t) noexcept { return t * t * (3.0f - 2.0f * t); }
    inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

    inline void panGains (float p, float law, float& gl, float& gr) noexcept
    {
        // Stereo balance: unity on both sides at centre, the opposite side fades out.
        const float bl = p > 0.0f ? std::cos (p * MathConstants<float>::halfPi) : 1.0f;
        const float br = p < 0.0f ? std::cos (-p * MathConstants<float>::halfPi) : 1.0f;
        // Equal-power pan for a mono signal (-3 dB per side at centre).
        const float th = (p + 1.0f) * MathConstants<float>::pi * 0.25f;
        const float el = std::cos (th), er = std::sin (th);
        gl = lerp (bl, el, law);
        gr = lerp (br, er, law);
    }

    inline void applyRamp (float* d, int n, float a, float b) noexcept
    {
        if (a == b) { if (a != 1.0f) FloatVectorOperations::multiply (d, a, n); return; }
        const float st = (b - a) / (float) n;
        float g = a;
        for (int i = 0; i < n; ++i) { g += st; d[i] *= g; }
    }
}

AudioEngine::AudioEngine (ParamRefs& p) : params (p)
{
    cmdBuf.resize ((size_t) cmdFifo.getTotalSize());
    noticeBuf.resize ((size_t) noticeFifo.getTotalSize());
    for (auto& e : effective) e.store (0.0f);
    for (auto& h : held) h.store (false);
    for (auto& v : seenParam) v.store (0.0f);
    for (int i = 0; i < kNumChannels; ++i) { peakL[(size_t) i].store (0.0f); peakR[(size_t) i].store (0.0f); }
    for (auto& m : lastPushedMap) m.store (-1.0f);
    eff = defaultControlValues();
    lastParam = eff;
    holdVal = eff;
    for (int s = 0; s < kNumChannels; ++s)
        for (int k = 0; k < kNumOutChoices; ++k)
            lastValidOut[s][k] = k == 0 ? 1.0f : 0.0f;
}

AudioEngine::~AudioEngine() = default;

void AudioEngine::prepare (double sr, int mb)
{
    sampleRate = sr > 0 ? sr : 44100.0;
    maxBlock = jmax (16, mb);
    const float fsr = (float) sampleRate;

    for (auto& c : chans)
    {
        c.preRing.allocate (kChannelRingSize);
        c.postRing.allocate (kChannelRingSize);
        c.work.setSize (2, maxBlock, false, true, false);
        c.tmpA.setSize (2, maxBlock, false, true, false);
        c.tmpB.setSize (2, maxBlock, false, true, false);
        c.sendBuf.setSize (2, maxBlock, false, true, false);
        for (auto& s : c.slots)
        {
            s.dry.allocate (kSlotRingSize + maxBlock);
            s.wet.step = 1.0f / (0.02f * fsr);
            s.wet.cur = s.wet.tgt = 1.0f;
        }
        c.fader.step = 1.0f / (0.02f * fsr);
        c.sendIn.step = 1.0f / (0.02f * fsr);
        c.mono.step = 1.0f / (0.03f * fsr);
        c.liveIn.step = 1.0f / (0.03f * fsr);
        c.pan.step = 2.0f / (0.03f * fsr);
        c.law.step = 1.0f / (0.05f * fsr);
        c.duck.step = 1.0f / (0.005f * fsr);
        c.soloGate.step = 1.0f / (0.01f * fsr);
        c.soloGate.cur = c.soloGate.tgt = 1.0f;
        c.duck.cur = c.duck.tgt = 1.0f;
        for (auto& o : c.out) o.step = 1.0f / (0.03f * fsr);
    }
    for (auto& row : sendGain) for (auto& r : row) r.step = 1.0f / (0.02f * fsr);
    for (auto& row : sendPost) for (auto& r : row) r.step = 1.0f / (0.03f * fsr);
    liveRing.allocate (kChannelRingSize);
    firstBlock = true;
    prepared = true;
}

// ------------------------------------------------------------------------------ FIFOs
bool AudioEngine::post (const EngineCommand& c)
{
    const ScopedLock sl (producerLock);
    const auto scope = cmdFifo.write (1);
    if (scope.blockSize1 + scope.blockSize2 < 1) return false;
    cmdBuf[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = c;
    return true;
}

bool AudioEngine::popNotice (EngineNotice& n)
{
    const auto scope = noticeFifo.read (1);
    if (scope.blockSize1 + scope.blockSize2 < 1) return false;
    n = noticeBuf[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
    return true;
}

void AudioEngine::notify (const EngineNotice& n) noexcept
{
    if (numPendingNotices == 0)
    {
        const auto scope = noticeFifo.write (1);
        if (scope.blockSize1 + scope.blockSize2 >= 1)
        {
            noticeBuf[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = n;
            return;
        }
    }
    if (numPendingNotices < (int) pendingNotices.size())
        pendingNotices[(size_t) numPendingNotices++] = n;
    else
        jassertfalse;   // MT is not draining notices
}

void AudioEngine::flushPendingNotices() noexcept
{
    int i = 0;
    for (; i < numPendingNotices; ++i)
    {
        const auto scope = noticeFifo.write (1);
        if (scope.blockSize1 + scope.blockSize2 < 1) break;
        noticeBuf[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = pendingNotices[(size_t) i];
    }
    if (i > 0)
    {
        for (int j = i; j < numPendingNotices; ++j) pendingNotices[(size_t) (j - i)] = pendingNotices[(size_t) j];
        numPendingNotices -= i;
    }
}

// ------------------------------------------------------------------------------ commands
void AudioEngine::handleCommands() noexcept
{
    EngineCommand c;
    for (;;)
    {
        // Commands are applied in order: while a re-order waits for its 5 ms duck, later
        // commands (which assume the new order) stay queued.
        bool movePending = false;
        for (auto& ch : chans) movePending |= ch.pendingMoveFrom >= 0;
        if (movePending && prepared) break;
        {
            const auto scope = cmdFifo.read (1);
            if (scope.blockSize1 + scope.blockSize2 < 1) break;
            c = cmdBuf[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
        }

        switch (c.type)
        {
            case EngineCommand::setSlot:
                startSlotFade (c.a, c.b, c.inst, c.uid, c.c);
                break;

            case EngineCommand::moveSlot:
            {
                auto& ch = chans[(size_t) c.a];
                if (ch.pendingMoveFrom >= 0) applyPendingMove (ch, c.a);
                ch.pendingMoveFrom = c.b;
                ch.pendingMoveTo = c.c;
                if (! prepared) applyPendingMove (ch, c.a);
                break;
            }

            case EngineCommand::setPresetMix:
                presetMix[(size_t) c.a] = c.values;
                presetValid[(size_t) c.a] = true;
                break;

            case EngineCommand::clearPresetMix:
                presetValid[(size_t) c.a] = false;
                break;

            case EngineCommand::setPrepared:
            {
                auto* old = prepared_[(size_t) c.a];
                prepared_[(size_t) c.a] = c.set;
                if (old != nullptr)
                {
                    EngineNotice n; n.type = EngineNotice::preparedReleased; n.set = old; n.flag = false;
                    notify (n);
                }
                break;
            }

            case EngineCommand::retrigger:
                startMorph (c.a, lastCtx);
                break;

            case EngineCommand::setMap:
                maps[(size_t) c.a] = MapBinding { c.uid, c.b, nullptr, -1.0f };
                break;

            case EngineCommand::restoreHolds:
                for (int i = 0; i < kNumControls; ++i)
                {
                    hold[(size_t) i] = c.mask[(size_t) i];
                    holdVal[(size_t) i] = c.values[(size_t) i];
                    morphing[(size_t) i] = false;
                    lastParam[(size_t) i] = params.raw[(size_t) i]->load();
                }
                morphActive = false;
                lastPresetTarget = roundToInt (params.rawPresetTarget->load());
                activePreset.store (c.a);
                morphTarget.store (c.a);
                morphProgress.store (1.0f);
                break;

            case EngineCommand::clearAllSlots:
                for (int ch = 0; ch < kNumChannels; ++ch)
                {
                    if (chans[(size_t) ch].pendingMoveFrom >= 0) applyPendingMove (chans[(size_t) ch], ch);
                    for (int s = 0; s < kNumSlots; ++s) startSlotFade (ch, s, nullptr, 0, 0);
                }
                break;

            case EngineCommand::releaseAllHolds:
                for (auto& h : hold) h = false;
                for (auto& m : morphing) m = false;
                morphActive = false;
                break;
        }
    }
}

void AudioEngine::startSlotFade (int ch, int s, HostedInstance* inst, uint64 uid, int fadeSamples) noexcept
{
    if (! isPositiveAndBelow (ch, kNumChannels) || ! isPositiveAndBelow (s, kNumSlots)) return;
    auto& sl = chans[(size_t) ch].slots[(size_t) s];

    auto release = [this] (HostedInstance* h)
    {
        if (h == nullptr) return;
        EngineNotice n; n.type = EngineNotice::instanceReleased; n.inst = h;
        notify (n);
    };

    if (sl.fading)   // collapse a running cross-fade onto its destination
    {
        release (sl.cur);
        sl.cur = sl.next; sl.curUid = sl.nextUid;
        sl.next = nullptr; sl.nextUid = 0;
        sl.fading = false;
    }

    if (inst != nullptr && inst == sl.cur) { release (inst); return; }   // duplicate reference
    if (inst == nullptr && sl.cur == nullptr) return;

    sl.next = inst;
    sl.nextUid = uid;
    sl.fadePos = 0.0;
    sl.fadeInc = 1.0 / (double) jmax (1, fadeSamples);
    sl.fading = true;
    if (! prepared || fadeSamples <= 0)   // not running: switch immediately
    {
        release (sl.cur);
        sl.cur = sl.next; sl.curUid = sl.nextUid;
        sl.next = nullptr; sl.nextUid = 0; sl.fading = false;
    }

    EngineNotice n; n.type = EngineNotice::slotChanged; n.a = ch; n.b = s; n.inst = inst; n.uid = uid;
    notify (n);
}

void AudioEngine::applyPendingMove (ChannelState& c, int ch) noexcept
{
    const int from = c.pendingMoveFrom, to = c.pendingMoveTo;
    c.pendingMoveFrom = c.pendingMoveTo = -1;
    if (! isPositiveAndBelow (from, kNumSlots) || ! isPositiveAndBelow (to, kNumSlots) || from == to) return;

    auto b = c.slots.begin();
    if (from < to) std::rotate (b + from, b + from + 1, b + to + 1);
    else           std::rotate (b + to, b + from, b + from + 1);

    for (int s = 0; s < kNumSlots; ++s)
    {
        auto& sl = c.slots[(size_t) s];
        EngineNotice n; n.type = EngineNotice::slotChanged; n.a = ch; n.b = s;
        n.inst = sl.fading ? sl.next : sl.cur;
        n.uid = sl.fading ? sl.nextUid : sl.curUid;
        n.flag = true;   // structural move
        notify (n);
    }
}

// ------------------------------------------------------------------------------ morph
void AudioEngine::startMorph (int p, const BlockContext& ctxIn) noexcept
{
    if (! isPositiveAndBelow (p, kNumPresets) || ! presetValid[(size_t) p]) return;
    for (int ch = 0; ch < kNumChannels; ++ch)   // prepared swaps assume the final slot order
        if (chans[(size_t) ch].pendingMoveFrom >= 0) applyPendingMove (chans[(size_t) ch], ch);
    const auto& ctx = ctxIn.bpm > 0 && ctxIn.timeSigNum > 0 ? ctxIn : BlockContext {};

    morphFrom = eff;
    morphTo = presetMix[(size_t) p];

    RouteGraph gFrom, gTo;
    for (int s = 0; s < kNumChannels; ++s)
    {
        const int oFrom = roundToInt (eff[(size_t) channelControl (s, ccOut)]);
        const int oTo = roundToInt (morphTo[(size_t) channelControl (s, ccOut)]);
        for (int k = 0; k < kNumOutChoices; ++k)
        {
            fromOut[s][k] = firstBlock ? (k == oFrom ? 1.0f : 0.0f) : outTarget[s][k];
            toOut[s][k] = k == oTo ? 1.0f : 0.0f;
            if (k > 0 && fromOut[s][k] > 0.0f) gFrom.add (s, k - 1);
            if (k > 0 && toOut[s][k] > 0.0f) gTo.add (s, k - 1);
        }
        for (int d = 0; d < kNumChannels; ++d)
        {
            if (d == s) continue;
            if (dbToGain (morphFrom[(size_t) sendLevelControl (s, d)]) > 0.0f) gFrom.add (s, d);
            if (dbToGain (morphTo[(size_t) sendLevelControl (s, d)]) > 0.0f) gTo.add (s, d);
        }
    }
    morphConflict = ! gFrom.unionWith (gTo).isAcyclic();

    // Controls whose parameter moved in this very block keep following the host.
    for (int i = 0; i < kNumControls; ++i)
    {
        const bool changedNow = params.raw[(size_t) i]->load() != lastParam[(size_t) i];
        if (changedNow || isSoloControl (i)) continue;   // solo is monitoring, never morphed
        hold[(size_t) i] = true;
        morphing[(size_t) i] = true;
        holdVal[(size_t) i] = eff[(size_t) i];
    }

    double len;
    if (roundToInt (params.rawMorphMode->load()) == 0)
    {
        const double qn = syncChoiceQuarterNotes (roundToInt (params.rawMorphSync->load()), ctx.timeSigNum, ctx.timeSigDen);
        len = qn * 60.0 / jlimit (20.0, 999.0, ctx.bpm) * sampleRate;
    }
    else
    {
        len = params.rawMorphMs->load() * 0.001 * sampleRate;
    }
    morphLen = jmax (1.0, len);
    morphPos = 0.0;
    morphActive = true;
    morphPreset = p;
    activePreset.store (p);
    morphTarget.store (p);
    morphProgress.store (0.0f);

    const int fade = (int) jmax (0.015 * sampleRate, morphLen);
    auto* set = prepared_[(size_t) p];
    bool needLazy = set == nullptr;
    if (set != nullptr)
    {
        for (int i = 0; i < kNumChannels * kNumSlots; ++i)
        {
            const auto& e = set->slots[(size_t) i];
            if (e.action == PreparedSlot::toInstance)  startSlotFade (i / kNumSlots, i % kNumSlots, e.inst, e.uid, fade);
            else if (e.action == PreparedSlot::toEmpty) startSlotFade (i / kNumSlots, i % kNumSlots, nullptr, 0, fade);
        }
        needLazy = ! set->complete;
        prepared_[(size_t) p] = nullptr;
        EngineNotice n; n.type = EngineNotice::preparedReleased; n.set = set; n.flag = true;
        notify (n);
    }

    EngineNotice n; n.type = EngineNotice::morphStarted; n.a = p; n.b = fade; n.flag = needLazy;
    notify (n);
    if (needLazy)
    {
        EngineNotice l; l.type = EngineNotice::morphNeedsPlugins; l.a = p; l.b = fade;
        notify (l);
    }
}

void AudioEngine::computeEffective (int n, const BlockContext& ctx) noexcept
{
    for (int i = 0; i < kNumControls; ++i)
    {
        const float p = params.raw[(size_t) i]->load();
        if (firstBlock) lastParam[(size_t) i] = p;
    }

    const int pt = roundToInt (params.rawPresetTarget->load());
    if (firstBlock) lastPresetTarget = pt;
    if (pt != lastPresetTarget)
    {
        lastPresetTarget = pt;
        if (pt >= 1) startMorph (pt - 1, ctx);
    }

    // Explicit host/UI changes take precedence over morph/hold for that control.
    for (int i = 0; i < kNumControls; ++i)
    {
        const float p = params.raw[(size_t) i]->load();
        if (p != lastParam[(size_t) i])
        {
            hold[(size_t) i] = false;
            morphing[(size_t) i] = false;
            lastParam[(size_t) i] = p;
        }
    }

    for (auto& v : morphOutValid) v = false;

    if (morphActive)
    {
        morphPos += n;
        const float t = (float) jmin (1.0, morphPos / morphLen);
        const float s = smoothstep (t);
        morphProgress.store (t);

        for (int i = 0; i < kNumControls; ++i)
        {
            if (! morphing[(size_t) i]) continue;
            const auto& info = controlInfo (i);
            const float a = morphFrom[(size_t) i], b = morphTo[(size_t) i];
            switch (info.kind)
            {
                case ControlKind::gainDb:
                {
                    const float ga = dbToGain (a), gb = dbToGain (b);
                    float g;
                    if (morphConflict && info.other >= 0 && (ga == 0.0f) != (gb == 0.0f))
                        g = gb == 0.0f ? ga * (1.0f - jmin (1.0f, 2.0f * s)) : gb * jmax (0.0f, 2.0f * s - 1.0f);
                    else
                        g = lerp (ga, gb, s);
                    holdVal[(size_t) i] = t >= 1.0f ? b : gainToDb (g);
                    break;
                }
                case ControlKind::sendInDb:
                    holdVal[(size_t) i] = t >= 1.0f ? b : gainToDb (lerp (dbToGain (a), dbToGain (b), s));
                    break;
                case ControlKind::out:
                {
                    const int ch = info.channel;
                    for (int k = 0; k < kNumOutChoices; ++k)
                    {
                        const float f = fromOut[ch][k], to = toOut[ch][k];
                        float v;
                        if (morphConflict && k > 0 && (f == 0.0f) != (to == 0.0f))
                            v = to == 0.0f ? f * (1.0f - jmin (1.0f, 2.0f * s)) : to * jmax (0.0f, 2.0f * s - 1.0f);
                        else
                            v = lerp (f, to, s);
                        morphOut[ch][k] = v;
                    }
                    morphOutValid[ch] = t < 1.0f;
                    holdVal[(size_t) i] = s < 0.5f ? a : b;
                    break;
                }
                case ControlKind::pan:
                case ControlKind::toggle:
                case ControlKind::map:
                    holdVal[(size_t) i] = t >= 1.0f ? b : lerp (a, b, s);
                    break;
            }
        }

        if (t >= 1.0f)
        {
            morphActive = false;
            for (auto& m : morphing) m = false;
            EngineNotice nn; nn.type = EngineNotice::morphFinished; nn.a = morphPreset;
            notify (nn);
        }
    }

    for (int i = 0; i < kNumControls; ++i)
    {
        eff[(size_t) i] = hold[(size_t) i] ? holdVal[(size_t) i] : lastParam[(size_t) i];
        effective[(size_t) i].store (eff[(size_t) i], std::memory_order_relaxed);
        held[(size_t) i].store (hold[(size_t) i], std::memory_order_relaxed);
        seenParam[(size_t) i].store (lastParam[(size_t) i], std::memory_order_relaxed);
    }
}

// ------------------------------------------------------------------------------ routing
void AudioEngine::updateRouting() noexcept
{
    // 1. Target graph from current values; reject the change if it closes a loop.
    RouteGraph T;
    for (int s = 0; s < kNumChannels; ++s)
        for (int d = 0; d < kNumChannels; ++d)
        {
            if (outTarget[s][d + 1] > 0.0f) T.add (s, d);
            if (d != s && sendGain[(size_t) s][(size_t) d].tgt > 0.0f) T.add (s, d);
        }

    if (! T.isAcyclic())
    {
        for (int s = 0; s < kNumChannels; ++s)
        {
            bool outBad = false;
            for (int d = 0; d < kNumChannels; ++d)
            {
                if (lastValid.has (s, d)) continue;
                if (outTarget[s][d + 1] > 0.0f) outBad = true;
                if (d != s) sendGain[(size_t) s][(size_t) d].tgt = 0.0f;
            }
            if (outBad)
                for (int k = 0; k < kNumOutChoices; ++k) outTarget[s][k] = lastValidOut[s][k];
        }
        if (! rejectedLastBlock)
        {
            routingRejects.fetch_add (1);
            EngineNotice n; n.type = EngineNotice::routingRejected;
            notify (n);
        }
        rejectedLastBlock = true;
        T = {};
        for (int s = 0; s < kNumChannels; ++s)
            for (int d = 0; d < kNumChannels; ++d)
            {
                if (outTarget[s][d + 1] > 0.0f) T.add (s, d);
                if (d != s && sendGain[(size_t) s][(size_t) d].tgt > 0.0f) T.add (s, d);
            }
    }
    else
    {
        lastValid = T;
        std::memcpy (lastValidOut, outTarget, sizeof (outTarget));
        rejectedLastBlock = false;
    }

    // 2. Edges still fading out stay in the graph. New edges wait until they can't form a loop.
    RouteGraph A;
    for (int s = 0; s < kNumChannels; ++s)
        for (int d = 0; d < kNumChannels; ++d)
        {
            if (chans[(size_t) s].out[(size_t) d + 1].cur > 0.0f) A.add (s, d);
            if (d != s && sendGain[(size_t) s][(size_t) d].cur > 0.0f) A.add (s, d);
        }

    RouteGraph U = A.unionWith (T);
    if (! U.isAcyclic())
    {
        U = A;
        for (int s = 0; s < kNumChannels; ++s)
            for (int d = 0; d < kNumChannels; ++d)
            {
                if (! T.has (s, d) || A.has (s, d)) continue;
                auto trial = U; trial.add (s, d);
                if (trial.isAcyclic()) { U = trial; continue; }
                outTarget[s][d + 1] = 0.0f;   // hold this new edge for now
                sendGain[(size_t) s][(size_t) d].tgt = 0.0f;
            }
    }

    if (! U.topologicalOrder (order))
        for (int i = 0; i < kNumChannels; ++i) order[(size_t) i] = i;   // cannot happen; be safe

    for (int s = 0; s < kNumChannels; ++s)
        for (int k = 0; k < kNumOutChoices; ++k)
            chans[(size_t) s].out[(size_t) k].tgt = outTarget[s][k];

    // 3. Latency compensation along the graph.
    for (int ch = 0; ch < kNumChannels; ++ch)
    {
        auto& c = chans[(size_t) ch];
        int lat = 0;
        for (auto& sl : c.slots)
        {
            const int a = sl.cur != nullptr ? sl.cur->latency() : 0;
            const int b = (sl.fading && sl.next != nullptr) ? sl.next->latency() : 0;
            lat += jmax (a, b);
        }
        c.chainLatency = lat;
    }

    int total = 0;
    for (int i = 0; i < kNumChannels; ++i)
    {
        const int ch = order[(size_t) i];
        auto& c = chans[(size_t) ch];
        int arr = 0;
        for (int s = 0; s < kNumChannels; ++s)
            if (U.has (s, ch)) arr = jmax (arr, chans[(size_t) s].outLatency);
        c.arrival = arr;
        c.outLatency = arr + c.chainLatency;
        total = jmax (total, c.outLatency);
    }
    totalLatency = jmin (total, kChannelRingSize / 2);
    neededLatency.store (totalLatency);
}

void AudioEngine::applyMaps() noexcept
{
    for (int k = 0; k < kNumMaps; ++k)
    {
        auto& m = maps[(size_t) k];
        if (m.paramIndex < 0 || m.uid == 0) continue;

        HostedInstance* targets[2] {};
        int nt = 0;
        for (auto& c : chans)
            for (auto& sl : c.slots)
            {
                if (sl.cur != nullptr && sl.curUid == m.uid && nt < 2) targets[nt++] = sl.cur;
                if (sl.fading && sl.next != nullptr && sl.nextUid == m.uid && nt < 2) targets[nt++] = sl.next;
            }
        if (nt == 0) { m.lastInst = nullptr; continue; }

        const float v = jlimit (0.0f, 1.0f, eff[(size_t) mapControl (k)]);
        const bool changed = v != m.lastValue || targets[0] != m.lastInst;
        if (! changed) continue;

        lastPushedMap[(size_t) k].store (v);
        for (int t = 0; t < nt; ++t)
        {
            auto& ps = targets[t]->plugin().getParameters();
            if (isPositiveAndBelow (m.paramIndex, ps.size()))
                if (std::abs (ps[m.paramIndex]->getValue() - v) > 1.0e-6f)
                    ps[m.paramIndex]->setValue (v);
        }
        m.lastValue = v;
        m.lastInst = targets[0];
    }
}

// ------------------------------------------------------------------------------ processing
void AudioEngine::process (AudioBuffer<float>& io, int numIn, const BlockContext& ctx) noexcept
{
    lastCtx = ctx;
    handleCommands();
    const int total = io.getNumSamples();
    if (! prepared || io.getNumChannels() < 1)
    {
        io.clear();
        return;
    }

    auto* L = io.getWritePointer (0);
    auto* R = io.getNumChannels() > 1 ? io.getWritePointer (1) : L;
    const float* inL = numIn > 0 ? io.getReadPointer (0) : nullptr;
    const float* inR = numIn > 1 ? io.getReadPointer (1) : inL;

    for (int off = 0; off < total; off += maxBlock)
    {
        const int n = jmin (maxBlock, total - off);
        processChunk (L + off, R + off, inL != nullptr ? inL + off : nullptr, inR != nullptr ? inR + off : nullptr, n, ctx);
    }
    if (io.getNumChannels() > 2)
        for (int c = 2; c < io.getNumChannels(); ++c) io.clear (c, 0, total);

    flushPendingNotices();
    processedBlocks.fetch_add (1, std::memory_order_relaxed);
}

void AudioEngine::processChunk (float* outL, float* outR, const float* inL, const float* inR, int n, const BlockContext& ctx) noexcept
{
    // 1. Input from Live
    if (inL != nullptr) liveRing.write (inL, inR, n);
    else
    {
        auto& z = chans[0].tmpA;
        z.clear (0, n);
        liveRing.write (z.getReadPointer (0), z.getReadPointer (1), n);
    }

    // 2. Control values
    computeEffective (n, ctx);

    for (int ch = 0; ch < kNumChannels; ++ch)
    {
        auto& c = chans[(size_t) ch];
        c.fader.tgt = dbToGain (eff[(size_t) channelControl (ch, ccFader)]);
        c.pan.tgt = jlimit (-1.0f, 1.0f, eff[(size_t) channelControl (ch, ccPan)] * 0.01f);
        c.sendIn.tgt = dbToGain (eff[(size_t) channelControl (ch, ccSendIn)]);
        c.mono.tgt = jlimit (0.0f, 1.0f, eff[(size_t) channelControl (ch, ccMono)]);
        c.liveIn.tgt = jlimit (0.0f, 1.0f, eff[(size_t) channelControl (ch, ccLiveIn)]);

        if (morphOutValid[ch])
            for (int k = 0; k < kNumOutChoices; ++k) outTarget[ch][k] = morphOut[ch][k];
        else
        {
            const int o = jlimit (0, kNumChannels, roundToInt (eff[(size_t) channelControl (ch, ccOut)]));
            for (int k = 0; k < kNumOutChoices; ++k) outTarget[ch][k] = k == o ? 1.0f : 0.0f;
        }
        outTarget[ch][ch + 1] = 0.0f;   // never into itself

        for (int d = 0; d < kNumChannels; ++d)
        {
            if (d == ch) continue;
            sendGain[(size_t) ch][(size_t) d].tgt = dbToGain (eff[(size_t) sendLevelControl (ch, d)]);
            sendPost[(size_t) ch][(size_t) d].tgt = jlimit (0.0f, 1.0f, eff[(size_t) sendModeControl (ch, d)]);
        }
        for (int s = 0; s < kNumSlots; ++s)
            c.slots[(size_t) s].wet.tgt = 1.0f - jlimit (0.0f, 1.0f, eff[(size_t) bypassControl (ch, s)]);
    }

    updateRouting();

    if (firstBlock)
    {
        for (auto& c : chans)
        {
            c.fader.snap(); c.pan.snap(); c.sendIn.snap(); c.mono.snap(); c.liveIn.snap();
            for (auto& o : c.out) o.snap();
            for (auto& s : c.slots) s.wet.snap();
            c.monoDetected = c.mono.tgt > 0.5f;
            c.law.tgt = c.monoDetected ? 1.0f : 0.0f;
            c.law.snap();
        }
        for (auto& row : sendGain) for (auto& r : row) r.snap();
        for (auto& row : sendPost) for (auto& r : row) r.snap();
        firstBlock = false;
    }

    applyMaps();

    // 3. Channels in topological order
    for (int oi = 0; oi < kNumChannels; ++oi)
    {
        const int ch = order[(size_t) oi];
        auto& c = chans[(size_t) ch];
        float* wl = c.work.getWritePointer (0);
        float* wr = c.work.getWritePointer (1);
        FloatVectorOperations::clear (wl, n);
        FloatVectorOperations::clear (wr, n);
        float a, b;

        // b) direct signal: Live input and Outs of other channels
        c.liveIn.advance (n, a, b);
        liveRing.addTo (wl, wr, n, c.arrival, a, b);
        for (int s = 0; s < kNumChannels; ++s)
        {
            if (s == ch) continue;
            auto& r = chans[(size_t) s].out[(size_t) ch + 1];
            r.advance (n, a, b);
            chans[(size_t) s].postRing.addTo (wl, wr, n, c.arrival - chans[(size_t) s].outLatency, a, b);
        }

        // a) incoming sends, scaled by Send In
        float* sl = c.sendBuf.getWritePointer (0);
        float* sr = c.sendBuf.getWritePointer (1);
        FloatVectorOperations::clear (sl, n);
        FloatVectorOperations::clear (sr, n);
        bool anySend = false;
        for (int s = 0; s < kNumChannels; ++s)
        {
            if (s == ch) continue;
            float g0, g1, p0, p1;
            sendGain[(size_t) s][(size_t) ch].advance (n, g0, g1);
            sendPost[(size_t) s][(size_t) ch].advance (n, p0, p1);
            if (g0 == 0.0f && g1 == 0.0f) continue;
            anySend = true;
            const int delay = c.arrival - chans[(size_t) s].outLatency;
            chans[(size_t) s].preRing.addTo (sl, sr, n, delay, g0 * (1.0f - p0), g1 * (1.0f - p1));
            chans[(size_t) s].postRing.addTo (sl, sr, n, delay, g0 * p0, g1 * p1);
        }
        if (anySend)
        {
            FloatVectorOperations::add (wl, sl, n);
            FloatVectorOperations::add (wr, sr, n);
        }
        // Send In = input level of the channel: everything that enters it
        // (Live input, Outs of other channels and sends).
        c.sendIn.advance (n, a, b);
        applyRamp (wl, n, a, b);
        applyRamp (wr, n, a, b);

        // c) Mono / Stereo
        c.mono.advance (n, a, b);
        if (a != 0.0f || b != 0.0f)
        {
            const float st = (b - a) / (float) n;
            float m = a;
            for (int i = 0; i < n; ++i)
            {
                m += st;
                const float mid = 0.5f * (wl[i] + wr[i]);
                wl[i] += m * (mid - wl[i]);
                wr[i] += m * (mid - wr[i]);
            }
        }

        // d) four effect slots, left to right
        for (int s = 0; s < kNumSlots; ++s)
            processSlot (c, ch, s, n);

        // structural changes (re-order) happen under a 5 ms duck
        if (c.pendingMoveFrom >= 0) c.duck.tgt = 0.0f;
        c.duck.advance (n, a, b);
        if (a != 1.0f || b != 1.0f) { applyRamp (wl, n, a, b); applyRamp (wr, n, a, b); }
        if (c.pendingMoveFrom >= 0 && c.duck.cur == 0.0f) { applyPendingMove (c, ch); c.duck.tgt = 1.0f; }

        // mono detection at the end of the chain (only meaningful in Mono mode)
        if (c.mono.tgt > 0.5f)
        {
            float mx = 0.0f, md = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                mx = jmax (mx, std::abs (wl[i]), std::abs (wr[i]));
                md = jmax (md, std::abs (wl[i] - wr[i]));
            }
            if (mx > 1.0e-4f)
            {
                if (md <= 1.0e-6f + 1.0e-4f * mx)
                {
                    c.monoBlocks += n;
                    if (c.monoBlocks > (int) (0.2 * sampleRate)) c.monoDetected = true;
                }
                else { c.monoDetected = false; c.monoBlocks = 0; }
            }
            c.law.tgt = c.monoDetected ? 1.0f : 0.0f;
        }
        else
        {
            c.monoDetected = true;   // entering Mono mode starts from "mono"
            c.monoBlocks = 0;
            c.law.tgt = 0.0f;
        }

        // e) Pan -> pre tap
        float p0, p1, l0, l1;
        c.pan.advance (n, p0, p1);
        c.law.advance (n, l0, l1);
        float gl0, gr0, gl1, gr1;
        panGains (p0, l0, gl0, gr0);
        panGains (p1, l1, gl1, gr1);
        applyRamp (wl, n, gl0, gl1);
        applyRamp (wr, n, gr0, gr1);
        c.preRing.write (wl, wr, n);

        // f) Fader -> post tap + main out
        c.fader.advance (n, a, b);
        applyRamp (wl, n, a, b);
        applyRamp (wr, n, a, b);
        c.postRing.write (wl, wr, n);

        const auto pl = FloatVectorOperations::findMaximum (wl, n), nl = FloatVectorOperations::findMinimum (wl, n);
        const auto pr = FloatVectorOperations::findMaximum (wr, n), nr = FloatVectorOperations::findMinimum (wr, n);
        const float peakLv = jmax (pl, -nl), peakRv = jmax (pr, -nr);
        if (peakLv > peakL[(size_t) ch].load (std::memory_order_relaxed)) peakL[(size_t) ch].store (peakLv, std::memory_order_relaxed);
        if (peakRv > peakR[(size_t) ch].load (std::memory_order_relaxed)) peakR[(size_t) ch].store (peakRv, std::memory_order_relaxed);
    }

    // 4. Solo: a channel reaches Master only if it is soloed or fed (directly or further
    //    down the chain) by a soloed channel. Channels feeding a soloed one keep working.
    {
        bool anySolo = false;
        for (int ch = 0; ch < kNumChannels; ++ch) anySolo |= eff[(size_t) soloControl (ch)] > 0.5f;
        bool pass[kNumChannels] {};
        for (int ch = 0; ch < kNumChannels; ++ch) pass[ch] = ! anySolo || eff[(size_t) soloControl (ch)] > 0.5f;
        if (anySolo)
            for (int oi = 0; oi < kNumChannels; ++oi)   // topological order: propagate downstream
            {
                const int s = order[(size_t) oi];
                if (! pass[s]) continue;
                for (int d = 0; d < kNumChannels; ++d)
                    if (d != s && (chans[(size_t) s].out[(size_t) d + 1].live() || sendGain[(size_t) s][(size_t) d].live()))
                        pass[d] = true;
            }
        for (int ch = 0; ch < kNumChannels; ++ch) chans[(size_t) ch].soloGate.tgt = pass[ch] ? 1.0f : 0.0f;
    }

    // 5. Master sum, aligned to the total latency
    FloatVectorOperations::clear (outL, n);
    if (outR != outL) FloatVectorOperations::clear (outR, n);
    for (int ch = 0; ch < kNumChannels; ++ch)
    {
        auto& c = chans[(size_t) ch];
        float a, b, ga, gb;
        c.out[0].advance (n, a, b);
        c.soloGate.advance (n, ga, gb);
        a *= ga;
        b *= gb;
        if (outR == outL)
        {
            auto& t = c.tmpB;   // mono output bus: fold to mono
            t.clear (0, n);
            c.postRing.addTo (t.getWritePointer (0), t.getWritePointer (1), n, totalLatency - c.outLatency, a, b);
            for (int i = 0; i < n; ++i) outL[i] += 0.5f * (t.getSample (0, i) + t.getSample (1, i));
        }
        else
            c.postRing.addTo (outL, outR, n, totalLatency - c.outLatency, a, b);
    }
}

void AudioEngine::processSlot (ChannelState& c, int ch, int s, int n) noexcept
{
    auto& sl = c.slots[(size_t) s];
    float* wl = c.work.getWritePointer (0);
    float* wr = c.work.getWritePointer (1);
    float w0, w1;
    sl.wet.advance (n, w0, w1);
    sl.dry.write (wl, wr, n);

    if (sl.cur == nullptr && ! sl.fading)
    {
        sl.latency = 0;
        slotLatency[ch][s].store (0, std::memory_order_relaxed);
        return;
    }

    const int latCur = sl.cur != nullptr ? sl.cur->latency() : 0;
    const int latNext = (sl.fading && sl.next != nullptr) ? sl.next->latency() : 0;
    const int lat = jmax (latCur, latNext);
    sl.latency = lat;
    slotLatency[ch][s].store (lat, std::memory_order_relaxed);

    if (w0 == 0.0f && w1 == 0.0f && ! sl.fading)
    {
        sl.dry.readTo (wl, wr, n, lat);   // fully bypassed: latency-matched dry signal
        return;
    }

    float* al = c.tmpA.getWritePointer (0);
    float* ar = c.tmpA.getWritePointer (1);
    float* bl = c.tmpB.getWritePointer (0);
    float* br = c.tmpB.getWritePointer (1);

    if (sl.cur != nullptr) sl.cur->process (wl, wr, al, ar, n, lat - latCur);
    else                   sl.dry.readTo (al, ar, n, lat);

    if (sl.fading)
    {
        if (sl.next != nullptr) sl.next->process (wl, wr, bl, br, n, lat - latNext);
        else                    sl.dry.readTo (bl, br, n, lat);

        double f = sl.fadePos;
        for (int i = 0; i < n; ++i)
        {
            f = jmin (1.0, f + sl.fadeInc);
            const float g = (float) f;
            al[i] += g * (bl[i] - al[i]);
            ar[i] += g * (br[i] - ar[i]);
        }
        sl.fadePos = f;
        if (sl.fadePos >= 1.0)
        {
            if (sl.cur != nullptr)
            {
                EngineNotice nn; nn.type = EngineNotice::instanceReleased; nn.inst = sl.cur;
                notify (nn);
            }
            sl.cur = sl.next; sl.curUid = sl.nextUid;
            sl.next = nullptr; sl.nextUid = 0;
            sl.fading = false;
        }
    }

    if (w0 != 1.0f || w1 != 1.0f)   // bypass cross-fade against the latency-matched dry signal
    {
        sl.dry.readTo (bl, br, n, lat);
        const float st = (w1 - w0) / (float) n;
        float w = w0;
        for (int i = 0; i < n; ++i)
        {
            w += st;
            al[i] = bl[i] + w * (al[i] - bl[i]);
            ar[i] = br[i] + w * (ar[i] - br[i]);
        }
    }

    FloatVectorOperations::copy (wl, al, n);
    FloatVectorOperations::copy (wr, ar, n);
}
}
