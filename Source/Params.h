#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

namespace SMB
{
constexpr int   kNumBands  = 6;
constexpr float kMinFreq   = 20.0f;
constexpr float kMaxFreq   = 20000.0f;
constexpr float kMaxLAms   = 20.0f;

// ---- Band parameters -------------------------------------------------------
enum BP
{
    Active, Lo, Hi, LoSlope, HiSlope,
    Thresh, Range, Ratio, Knee, Attack, Release, LA, Gain,
    Mode, Link, StMode, Solo, Mute, Bypass,
    Trig, FLo, FHi, Ext, Audition,
    NumBP
};

inline const char* bpName (int p)
{
    static const char* n[] = { "active", "lo", "hi", "loSlope", "hiSlope",
                               "thresh", "range", "ratio", "knee", "attack", "release", "la", "gain",
                               "mode", "link", "stMode", "solo", "mute", "bypass",
                               "trig", "fLo", "fHi", "ext", "audition" };
    return n[p];
}

inline juce::String bandID (int band, int p)  { return "b" + juce::String (band + 1) + "_" + bpName (p); }

namespace ID
{
    static const juce::String inGain   { "inGain" };
    static const juce::String outGain  { "outGain" };
    static const juce::String mix      { "mix" };
    static const juce::String procMode { "procMode" };   // 0 Dynamic Phase, 1 Linear Phase
    static const juce::String laOn     { "lookaheadOn" };
}

// default edges for the six slots (only used as starting values)
inline float slotDefaultLo (int b) { static const float v[] = { 20.f, 120.f, 400.f, 1200.f, 3500.f, 9000.f };   return v[b]; }
inline float slotDefaultHi (int b) { static const float v[] = { 120.f, 400.f, 1200.f, 3500.f, 9000.f, 20000.f }; return v[b]; }

inline int slopeToOrder (int slopeIndex) { return juce::jlimit (1, 8, slopeIndex + 1); } // 6 dB/oct per order

// ---- shared math -----------------------------------------------------------
// Linear-phase band shape: magnitude-complementary crossover, |LP|+|HP| = 1 (adjacent bands sum to unity)
inline float xoverMag (bool highpass, int order, float fc, float f)
{
    if (f <= 0.0f) return highpass ? 0.0f : 1.0f;
    const double r = highpass ? (double) fc / f : (double) f / fc;
    return (float) (1.0 / (1.0 + std::pow (r, (double) order)));
}

inline bool loIsOpen (float lo) { return lo <= 20.5f; }
inline bool hiIsOpen (float hi) { return hi >= 19900.0f; }

inline float bandMag (float f, float lo, int loOrd, float hi, int hiOrd)
{
    float m = 1.0f;
    if (! loIsOpen (lo)) m *= xoverMag (true,  loOrd, lo, f);
    if (! hiIsOpen (hi)) m *= xoverMag (false, hiOrd, hi, f);
    return m;
}

// Dynamic Phase band shape: Butterworth high-shelf pair (exactly reciprocal, so snapped bands with equal gain merge perfectly)
inline float shelfMagDb (float f, float fc, int N, float gDb)
{
    if (std::abs (gDb) < 1.0e-6f) return 0.0f;
    const double rg = std::pow (10.0, gDb / (40.0 * N));          // G^(1/2N)
    const double wz = fc / rg, wp = fc * rg;
    const double num = 1.0 + std::pow (f / wz, 2.0 * N);
    const double den = 1.0 + std::pow (f / wp, 2.0 * N);
    return (float) (10.0 * std::log10 (num / den));
}

inline float bandDynDb (float f, float lo, int loOrd, float hi, int hiOrd, float gDb)
{
    const bool lOpen = loIsOpen (lo), hOpen = hiIsOpen (hi);
    if (lOpen && hOpen) return gDb;
    if (lOpen) return gDb + shelfMagDb (f, hi, hiOrd, -gDb);
    if (hOpen) return shelfMagDb (f, lo, loOrd, gDb);
    return shelfMagDb (f, lo, loOrd, gDb) + shelfMagDb (f, hi, hiOrd, -gDb);
}

inline float bandCentre (float lo, float hi)
{
    return std::sqrt (juce::jlimit (kMinFreq, kMaxFreq, lo) * juce::jlimit (kMinFreq, kMaxFreq, hi));
}

inline float attackMs  (float pct) { return 0.5f  * std::pow (500.0f, pct / 100.0f); }   // 0.5 .. 250 ms
inline float releaseMs (float pct) { return 10.0f * std::pow (300.0f, pct / 100.0f); }   // 10 .. 3000 ms

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
float bandDefault (int p, int band);
}
