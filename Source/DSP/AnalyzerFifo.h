#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace SMB
{
// single-producer (audio) / single-consumer (GUI) mono sample FIFO
class AnalyzerFifo
{
public:
    AnalyzerFifo() : fifo (1 << 15) { data.resize (1 << 15); }

    void push (const float* x, int n)
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) data[(size_t) (s1 + i)] = x[i];
        for (int i = 0; i < n2; ++i) data[(size_t) (s2 + i)] = x[n1 + i];
        fifo.finishedWrite (n1 + n2);
    }

    template <typename Fn> void pull (Fn&& fn)
    {
        const int n = fifo.getNumReady();
        int s1, n1, s2, n2;
        fifo.prepareToRead (n, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) fn (data[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) fn (data[(size_t) (s2 + i)]);
        fifo.finishedRead (n1 + n2);
    }

private:
    juce::AbstractFifo fifo;
    std::vector<float> data;
};
}
