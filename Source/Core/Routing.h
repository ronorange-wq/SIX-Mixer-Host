#pragma once
#include "Controls.h"

namespace six
{
// A routing graph between the six channels. adj[src] is a bit mask of destination channels.
// Allocation-free; safe to use on the audio thread.
struct RouteGraph
{
    std::array<uint8_t, kNumChannels> adj {};

    void add (int s, int d) noexcept    { adj[(size_t) s] |= (uint8_t) (1u << d); }
    bool has (int s, int d) const noexcept { return (adj[(size_t) s] >> d) & 1u; }
    bool operator== (const RouteGraph& o) const noexcept { return adj == o.adj; }
    bool operator!= (const RouteGraph& o) const noexcept { return adj != o.adj; }

    RouteGraph unionWith (const RouteGraph& o) const noexcept
    {
        RouteGraph r;
        for (size_t i = 0; i < adj.size(); ++i) r.adj[i] = (uint8_t) (adj[i] | o.adj[i]);
        return r;
    }

    // Kahn's algorithm. Returns false if the graph has a cycle (incl. self loops).
    bool topologicalOrder (std::array<int, kNumChannels>& order) const noexcept;
    bool isAcyclic() const noexcept { std::array<int, kNumChannels> o; return topologicalOrder (o); }

    // Human readable description of one cycle, e.g. "03 -> 02 -> 03" (message thread only)
    juce::String describeCycle() const;
};

// Graph implied by a set of control values: Out routes to channels + every open send.
RouteGraph graphFromValues (const ControlValues& v);

// Message-thread check used by the UI before applying a routing change.
// Returns an empty string if OK, otherwise a short explanation.
juce::String validateRoutingChange (const ControlValues& current, int controlIndex, float newValue);
}
