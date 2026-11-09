#include "Routing.h"

namespace six
{
bool RouteGraph::topologicalOrder (std::array<int, kNumChannels>& order) const noexcept
{
    int indeg[kNumChannels] {};
    for (int s = 0; s < kNumChannels; ++s)
        for (int d = 0; d < kNumChannels; ++d)
            if (has (s, d))
            {
                if (s == d) return false;
                ++indeg[d];
            }

    bool done[kNumChannels] {};
    int n = 0;
    while (n < kNumChannels)
    {
        int pick = -1;
        for (int i = 0; i < kNumChannels; ++i)
            if (! done[i] && indeg[i] == 0) { pick = i; break; }   // lowest index first: deterministic
        if (pick < 0) return false;
        done[pick] = true;
        order[(size_t) n++] = pick;
        for (int d = 0; d < kNumChannels; ++d)
            if (has (pick, d)) --indeg[d];
    }
    return true;
}

juce::String RouteGraph::describeCycle() const
{
    // DFS for a back edge, then print the path.
    int state[kNumChannels] {};   // 0 new, 1 on stack, 2 done
    int parent[kNumChannels];
    std::fill (std::begin (parent), std::end (parent), -1);

    std::function<int (int)> dfs = [&] (int u) -> int
    {
        state[u] = 1;
        for (int v = 0; v < kNumChannels; ++v)
        {
            if (! has (u, v)) continue;
            if (state[v] == 1) { parent[v] = parent[v]; return u * 16 + v; }
            if (state[v] == 0) { parent[v] = u; if (auto r = dfs (v); r >= 0) return r; }
        }
        state[u] = 2;
        return -1;
    };

    for (int i = 0; i < kNumChannels; ++i)
    {
        if (state[i] != 0) continue;
        const int r = dfs (i);
        if (r < 0) continue;
        const int from = r / 16, to = r % 16;
        juce::StringArray path;
        for (int x = from; x != to && x >= 0; x = parent[x]) path.insert (0, twoDigits (x + 1));
        path.insert (0, twoDigits (to + 1));
        path.add (twoDigits (to + 1));
        return path.joinIntoString (juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")));
    }
    return {};
}

RouteGraph graphFromValues (const ControlValues& v)
{
    RouteGraph g;
    for (int s = 0; s < kNumChannels; ++s)
    {
        const int out = juce::roundToInt (v[(size_t) channelControl (s, ccOut)]);
        if (out >= 1 && out <= kNumChannels) g.add (s, out - 1);
        for (int d = 0; d < kNumChannels; ++d)
            if (d != s && v[(size_t) sendLevelControl (s, d)] > kMinDb + 0.001f)
                g.add (s, d);
    }
    return g;
}

juce::String validateRoutingChange (const ControlValues& current, int controlIndex, float newValue)
{
    auto v = current;
    v[(size_t) controlIndex] = newValue;
    const auto g = graphFromValues (v);
    if (g.isAcyclic()) return {};
    const auto& info = controlInfo (controlIndex);
    if (info.kind == ControlKind::out && juce::roundToInt (newValue) == info.channel + 1)
        return "A channel can't output into itself.";
    return "Blocked: this route would create a feedback loop (" + g.describeCycle() + ").";
}
}
