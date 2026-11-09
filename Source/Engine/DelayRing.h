#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace six
{
// Stereo ring buffer used for latency alignment and send/out taps.
// All methods after allocate() are allocation-free.
class DelayRing
{
public:
    void allocate (int minSize)
    {
        int s = 1;
        while (s < minSize) s <<= 1;
        size = s;
        mask = s - 1;
        buf.setSize (2, s, false, true, false);
        buf.clear();
        writePos = 0;
    }

    int capacity() const noexcept { return size; }
    void clear() noexcept { buf.clear(); writePos = 0; }

    void write (const float* l, const float* r, int n) noexcept
    {
        auto* dl = buf.getWritePointer (0);
        auto* dr = buf.getWritePointer (1);
        int p = writePos;
        for (int i = 0; i < n; ++i)
        {
            dl[p] = l[i];
            dr[p] = r[i];
            p = (p + 1) & mask;
        }
        writePos = p;
    }

    // Reads the last n written samples, delayed by `delay` (0 = what was just written).
    // `gainStart..gainEnd` is a linear ramp; result is ADDED to the destination.
    void addTo (float* l, float* r, int n, int delay, float gainStart, float gainEnd) const noexcept
    {
        if (gainStart == 0.0f && gainEnd == 0.0f) return;
        delay = juce::jlimit (0, size - n - 1, delay);
        const auto* sl = buf.getReadPointer (0);
        const auto* sr = buf.getReadPointer (1);
        int p = (writePos - n - delay) & mask;
        const float step = (gainEnd - gainStart) / (float) juce::jmax (1, n);
        float g = gainStart;
        for (int i = 0; i < n; ++i)
        {
            g += step;
            l[i] += sl[p] * g;
            r[i] += sr[p] * g;
            p = (p + 1) & mask;
        }
    }

    void readTo (float* l, float* r, int n, int delay) const noexcept
    {
        juce::FloatVectorOperations::clear (l, n);
        juce::FloatVectorOperations::clear (r, n);
        addTo (l, r, n, delay, 1.0f, 1.0f);
    }

private:
    juce::AudioBuffer<float> buf;
    int size = 0, mask = 0, writePos = 0;
};
}
