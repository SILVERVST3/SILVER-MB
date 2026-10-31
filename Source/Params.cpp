#include "Params.h"

namespace SMB
{
using namespace juce;
using APF  = AudioParameterFloat;
using APB  = AudioParameterBool;
using APC  = AudioParameterChoice;
using Attr = AudioParameterFloatAttributes;

static NormalisableRange<float> freqRange()
{
    return { kMinFreq, kMaxFreq,
             [] (float s, float e, float n) { return s * std::pow (e / s, n); },
             [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
             [] (float s, float e, float v) { return jlimit (s, e, v); } };
}

static String freqText (float v, int)
{
    if (v <= 20.5f)    return "20 Hz";
    if (v >= 19900.f)  return "20 kHz";
    if (v >= 1000.f)   return String (v / 1000.f, v >= 10000.f ? 1 : 2) + " kHz";
    return String (roundToInt (v)) + " Hz";
}

static float freqFromText (const String& t)
{
    auto s = t.trim().toLowerCase();
    float v = s.getFloatValue();
    if (s.contains ("k")) v *= 1000.f;
    return v;
}

static String dbText (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; }

float bandDefault (int p, int band)
{
    switch (p)
    {
        case Active:   return 0.f;
        case Lo:       return slotDefaultLo (band);
        case Hi:       return slotDefaultHi (band);
        case LoSlope:  return 3.f;     // 24 dB/oct
        case HiSlope:  return 3.f;
        case Thresh:   return -20.f;
        case Range:    return -12.f;
        case Ratio:    return 3.f;
        case Knee:     return 12.f;
        case Attack:   return 35.f;
        case Release:  return 40.f;
        case LA:       return 2.f;
        case Gain:     return 0.f;
        case Mode:     return 0.f;     // Compress
        case Link:     return 100.f;
        case StMode:   return 0.f;     // Stereo
        case FLo:      return 200.f;
        case FHi:      return 2000.f;
        default:       return 0.f;     // bools
    }
}

AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<APF> (ParameterID { ID::inGain, 1 }, "Input Gain", NormalisableRange<float> (-36.f, 36.f, 0.01f), 0.f,
                                       Attr().withStringFromValueFunction (dbText).withLabel ("dB")));
    layout.add (std::make_unique<APF> (ParameterID { ID::outGain, 1 }, "Output Gain", NormalisableRange<float> (-36.f, 36.f, 0.01f), 0.f,
                                       Attr().withStringFromValueFunction (dbText).withLabel ("dB")));
    layout.add (std::make_unique<APF> (ParameterID { ID::mix, 1 }, "Mix", NormalisableRange<float> (0.f, 200.f, 0.1f), 100.f,
                                       Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; })));
    layout.add (std::make_unique<APC> (ParameterID { ID::procMode, 1 }, "Processing Mode", StringArray { "Dynamic Phase", "Linear Phase" }, 0));
    layout.add (std::make_unique<APB> (ParameterID { ID::laOn, 1 }, "Lookahead", true));

    const StringArray slopes { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "30 dB/oct", "36 dB/oct", "42 dB/oct", "48 dB/oct" };

    for (int b = 0; b < kNumBands; ++b)
    {
        const String pre = "B" + String (b + 1) + " ";
        auto fAttr = Attr().withStringFromValueFunction (freqText).withValueFromStringFunction (freqFromText);
        auto pct   = Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; });

        layout.add (std::make_unique<APB> (ParameterID { bandID (b, Active), 1 }, pre + "Active", false));
        layout.add (std::make_unique<APF> (ParameterID { bandID (b, Lo), 1 }, pre + "Low Freq",  freqRange(), bandDefault (Lo, b), fAttr));
        layout.add (std::make_unique<APF> (ParameterID { bandID (b, Hi), 1 }, pre + "High Freq", freqRange(), bandDefault (Hi, b), fAttr));
        layout.add (std::make_unique<APC> (ParameterID { bandID (b, LoSlope), 1 }, pre + "Low Slope",  slopes, 3));
        layout.add (std::make_unique<APC> (ParameterID { bandID (b, HiSlope), 1 }, pre + "High Slope", slopes, 3));

        layout.add (std::make_unique<APF> (ParameterID { bandID (b, Thresh), 1 }, pre + "Threshold", NormalisableRange<float> (-60.f, 0.f, 0.01f), bandDefault (Thresh, b),
                                           Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
        layout.add (std::make_unique<APF> (ParameterID { bandID (b, Range), 1 }, pre + "Range", NormalisableRange<float> (-30.f, 30.f, 0.01f), bandDefault (Range, b),
                                           Attr().withStringFromValueFunction (dbText)));
        {
            NormalisableRange<float> rr (1.f, 100.f, 0.01f);
            rr.setSkewForCentre (4.f);
            layout.add (std::make_unique<APF> (ParameterID { bandID (b, Ratio), 1 }, pre + "Ratio", rr, bandDefault (Ratio, b),
                                               Attr().withStringFromValueFunction ([] (float v, int) { return v >= 99.5f ? String ("inf:1") : String (v, v < 10 ? 2 : 1) + ":1"; })));
        }
        layout.add (std::make_unique<APF> (ParameterID { bandID (b, Knee), 1 }, pre + "Knee", NormalisableRange<float> (0.f, 30.f, 0.01f), bandDefault (Knee, b),
                                           Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
        layout.add (std::make_unique<APF> (ParameterID { bandID (b, Attack), 1 }, pre + "Attack", NormalisableRange<float> (0.f, 100.f, 0.1f), bandDefault (Attack, b), pct));
        layout.add (std::make_unique<APF> (ParameterID { bandID (b, Release), 1 }, pre + "Release", NormalisableRange<float> (0.f, 100.f, 0.1f), bandDefault (Release, b), pct));
        layout.add (std::make_unique<APF> (ParameterID { bandID (b, LA), 1 }, pre + "Lookahead", NormalisableRange<float> (0.f, kMaxLAms, 0.01f), bandDefault (LA, b),
                                           Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " ms"; })));
        layout.add (std::make_unique<APF> (ParameterID { bandID (b, Gain), 1 }, pre + "Output", NormalisableRange<float> (-30.f, 30.f, 0.01f), bandDefault (Gain, b),
                                           Attr().withStringFromValueFunction (dbText)));
        layout.add (std::make_unique<APC> (ParameterID { bandID (b, Mode), 1 }, pre + "Mode", StringArray { "Compress", "Expand" }, 0));
        layout.add (std::make_unique<APF> (ParameterID { bandID (b, Link), 1 }, pre + "Stereo Link", NormalisableRange<float> (0.f, 100.f, 0.1f), bandDefault (Link, b), pct));
        layout.add (std::make_unique<APC> (ParameterID { bandID (b, StMode), 1 }, pre + "Stereo Mode", StringArray { "Stereo", "Mid", "Side" }, 0));
        layout.add (std::make_unique<APB> (ParameterID { bandID (b, Solo), 1 },   pre + "Solo",   false));
        layout.add (std::make_unique<APB> (ParameterID { bandID (b, Mute), 1 },   pre + "Mute",   false));
        layout.add (std::make_unique<APB> (ParameterID { bandID (b, Bypass), 1 }, pre + "Bypass", false));
        layout.add (std::make_unique<APB> (ParameterID { bandID (b, Free), 1 },   pre + "Free Detection", false));
        layout.add (std::make_unique<APF> (ParameterID { bandID (b, FLo), 1 }, pre + "Detect Low",  freqRange(), bandDefault (FLo, b), fAttr));
        layout.add (std::make_unique<APF> (ParameterID { bandID (b, FHi), 1 }, pre + "Detect High", freqRange(), bandDefault (FHi, b), fAttr));
        layout.add (std::make_unique<APB> (ParameterID { bandID (b, Ext), 1 },      pre + "External Sidechain", false));
        layout.add (std::make_unique<APB> (ParameterID { bandID (b, Audition), 1 }, pre + "Audition", false));
    }
    return layout;
}
}
