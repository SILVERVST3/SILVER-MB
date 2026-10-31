#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>
#include "../Params.h"

namespace SMB
{
// Topology-preserving-transform SVF (Zavalishin) - stable under fast modulation
struct TptSvf
{
    float k = 1.414f, a1 = 0, a2 = 0, a3 = 0;
    float ic1[2] {}, ic2[2] {};

    void set (float fc, float q, double sr)
    {
        const float g = (float) std::tan (juce::MathConstants<double>::pi * fc / sr);
        k  = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0; }

    inline float process (float v0, int ch, bool highpass)
    {
        const float v3 = v0 - ic2[ch];
        const float v1 = a1 * ic1[ch] + a2 * v3;
        const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
        ic1[ch] = 2.0f * v1 - ic1[ch];
        ic2[ch] = 2.0f * v2 - ic2[ch];
        return highpass ? (v0 - k * v1 - v2) : v2;
    }
};

struct TptOnePole
{
    float G = 0; float s[2] {};
    void set (float fc, double sr)
    {
        const float g = (float) std::tan (juce::MathConstants<double>::pi * fc / sr);
        G = g / (1.0f + g);
    }
    void reset() { s[0] = s[1] = 0; }
    inline float process (float x, int ch, bool highpass)
    {
        const float v  = (x - s[ch]) * G;
        const float lp = v + s[ch];
        s[ch] = lp + v;
        return highpass ? x - lp : lp;
    }
};

// Butterworth HP or LP of order 1..8 (6..48 dB/oct), stereo, can be "open" (bypassed)
class ButterChain
{
public:
    void setup (bool isHighpass, int order, float fc, double sr, bool open)
    {
        fc = juce::jlimit (10.0f, (float) (sr * 0.47), fc);
        if (isHighpass == hp && order == ord && fc == freq && sr == rate && open == isOpen) return;

        if (order != ord || isHighpass != hp) reset();
        hp = isHighpass; ord = order; freq = fc; rate = sr; isOpen = open;

        nSvf  = order / 2;
        hasOP = (order % 2) != 0;
        for (int i = 0; i < nSvf; ++i)
        {
            const double q = 1.0 / (2.0 * std::cos ((2.0 * (i + 1) - 1.0) * juce::MathConstants<double>::pi / (2.0 * order)));
            svf[i].set (fc, (float) q, sr);
        }
        if (hasOP) op.set (fc, sr);
    }

    void reset() { for (auto& s : svf) s.reset(); op.reset(); }

    inline float process (float x, int ch)
    {
        if (isOpen) return x;
        for (int i = 0; i < nSvf; ++i) x = svf[i].process (x, ch, hp);
        if (hasOP) x = op.process (x, ch, hp);
        return x;
    }

private:
    TptSvf svf[4];
    TptOnePole op;
    int nSvf = 0; bool hasOP = false;
    bool hp = false; int ord = -1; float freq = -1; double rate = 0; bool isOpen = false;
};

// Dynamic high-shelf of Butterworth order N (1..8) at fc. Gain is set per sample; with gain 1 it is an exact identity.
// Section k: SVF tuned at fc*G^(1/2N), output = LP + k*r*BP + r^2*HP  with r = G^(1/N)
class DynShelf
{
public:
    void setup (int order, float fc, double sr)
    {
        fc = juce::jlimit (10.0f, (float) (sr * 0.47), fc);
        if (order == N && fc == freq && sr == rate) return;
        if (order != N) reset();
        N = order; freq = fc; rate = sr;
        oc = (float) std::tan (juce::MathConstants<double>::pi * fc / sr);
        nS = N / 2; odd = (N % 2) != 0;
        for (int i = 0; i < nS; ++i)
            k[i] = (float) (2.0 * std::sin ((2.0 * i + 1.0) * juce::MathConstants<double>::pi / (2.0 * N)));
        invTwoN = 1.0f / (2.0f * (float) N);
    }
    void reset() { for (auto& a : ic1) a[0] = a[1] = 0; for (auto& a : ic2) a[0] = a[1] = 0; s1[0] = s1[1] = 0; }

    // gDb: shelf gain in dB (high frequencies get gDb)
    inline float process (float x, int ch, float gDb)
    {
        const float rg = std::exp (gDb * 0.11512925f * invTwoN);   // 10^(gDb/20)^(1/2N)
        const float r  = rg * rg;
        const float g  = oc * rg;
        for (int i = 0; i < nS; ++i)
        {
            const float kk = k[i];
            const float a1 = 1.0f / (1.0f + g * (g + kk));
            const float a2 = g * a1, a3 = g * a2;
            const float v3 = x - ic2[i][ch];
            const float v1 = a1 * ic1[i][ch] + a2 * v3;
            const float v2 = ic2[i][ch] + a2 * ic1[i][ch] + a3 * v3;
            ic1[i][ch] = 2.0f * v1 - ic1[i][ch];
            ic2[i][ch] = 2.0f * v2 - ic2[i][ch];
            const float hp = x - kk * v1 - v2;
            x = v2 + kk * r * v1 + r * r * hp;
        }
        if (odd)
        {
            const float G1 = g / (1.0f + g);
            const float v  = (x - s1[ch]) * G1;
            const float lp = v + s1[ch];
            s1[ch] = lp + v;
            x = lp + r * (x - lp);
        }
        return x;
    }

private:
    int N = -1, nS = 0; bool odd = false;
    float freq = -1, oc = 0.1f, invTwoN = 0.5f; double rate = 0;
    float k[4] {};
    float ic1[4][2] {}, ic2[4][2] {}, s1[2] {};
};

struct DelayLine
{
    std::vector<float> buf; int w = 0, mask = 0;
    void prepare (int maxDelay)
    {
        int n = 1; while (n < maxDelay + 2) n <<= 1;
        buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    inline float process (float x, int d)
    {
        buf[(size_t) w] = x;
        const float y = buf[(size_t) ((w - d) & mask)];
        w = (w + 1) & mask;
        return y;
    }
};

// Linear-phase band-pass FIR (magnitude identical to the IIR band). Length 2^order, delay 2^(order-1).
inline void designBandFIR (juce::AudioBuffer<float>& ir, int fftOrder, double sr,
                           float lo, int loOrd, float hi, int hiOrd)
{
    const int M = 1 << fftOrder;
    juce::dsp::FFT fft (fftOrder);
    std::vector<float> d ((size_t) (2 * M), 0.0f);

    for (int k = 0; k <= M / 2; ++k)
    {
        const float f = (float) (k * sr / M);
        d[(size_t) (2 * k)] = bandMag (f, lo, loOrd, hi, hiOrd);
    }
    fft.performRealOnlyInverseTransform (d.data());

    ir.setSize (1, M);
    auto* h = ir.getWritePointer (0);
    for (int n = 0; n < M; ++n)
    {
        const double w = 0.42 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * n / M)
                              + 0.08 * std::cos (4.0 * juce::MathConstants<double>::pi * n / M);  // Blackman
        h[n] = d[(size_t) ((n + M / 2) % M)] * (float) w;
    }

    // normalise against the analytic magnitude at the band centre
    const float fRef = juce::jlimit (30.0f, (float) (sr * 0.4), bandCentre (lo, hi));
    double re = 0, im = 0;
    const double om = 2.0 * juce::MathConstants<double>::pi * fRef / sr;
    for (int n = 0; n < M; ++n) { re += h[n] * std::cos (om * n); im -= h[n] * std::sin (om * n); }
    const double got = std::sqrt (re * re + im * im);
    const double want = bandMag (fRef, lo, loOrd, hi, hiOrd);
    if (got > 1.0e-9) ir.applyGain ((float) (want / got));
}
}
