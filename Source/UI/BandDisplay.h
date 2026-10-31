#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "../PluginProcessor.h"
#include "SilverLookAndFeel.h"

class BandDisplay : public juce::Component, private juce::Timer
{
public:
    explicit BandDisplay (SilverMBAudioProcessor&);
    ~BandDisplay() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    int  getSelected() const { return selected; }
    void setSelected (int b);
    std::function<void (int)> onSelectionChanged;

    void deleteBand (int b);
    int  createBand (float centreHz);

private:
    void timerCallback() override;

    // --- geometry ---
    juce::Rectangle<float> plot;
    float freqToX (float f) const;
    float xToFreq (float x) const;
    float gainToY (float db) const;
    float yToGain (float y) const;
    float anaToY (float db) const;
    float dispRange() const;

    // --- band helpers ---
    float bp (int b, int p) const { return proc.bandParam (b, p); }
    bool  isActive (int b) const  { return bp (b, SMB::Active) > 0.5f; }
    float magAt (int b, float f) const;
    juce::Point<float> dotPos (int b) const;
    juce::Point<float> rangePos (int b) const;

    enum class Hit { None, Dot, Range, LoEdge, HiEdge };
    struct HitInfo { Hit what = Hit::None; int band = -1; };
    HitInfo hitTest (juce::Point<float>) const;

    // --- parameter helpers ---
    juce::RangedAudioParameter* param (int b, int p) const;
    void setP (int b, int p, float v);
    void beginGestureFor (int b, int p);
    void endAllGestures();
    std::vector<juce::RangedAudioParameter*> gestures;
    void resetBandToDefaults (int b);
    float snapFreq (float f, int excludeBand) const;

    void showBandMenu (int b);

    SilverMBAudioProcessor& proc;
    int selected = -1;
    HitInfo hover, drag;
    juce::Point<float> dragStart;
    float startLo = 0, startHi = 0, startGain = 0, startRange = 0;
    int axisLock = 0; // 0 undecided, 1 horizontal, 2 vertical
    std::vector<std::pair<int, int>> linkedEdges; // (band, param) moved together with the dragged edge

    // --- analyzer ---
    struct Ana
    {
        std::vector<float> ring; int w = 0;
        std::vector<float> disp, frozen;
    };
    Ana pre, post, sc;
    int fftOrder = 12;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, fftData;
    void ensureFFT();
    void feed (SMB::AnalyzerFifo&, Ana&);
    void analyse (Ana&, bool freeze);
    int  numCols = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandDisplay)
};
