#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include "Params.h"
#include "DSP/Filters.h"
#include "DSP/AnalyzerFifo.h"

class SilverMBAudioProcessor : public juce::AudioProcessor,
                               private juce::Timer
{
public:
    SilverMBAudioProcessor();
    ~SilverMBAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SILVER-MB"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- shared with the editor -------------------------------------------
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;

    struct BandMeter { std::atomic<float> dynDb { 0.0f }, levelDb { -120.0f }; };
    std::array<BandMeter, SMB::kNumBands> meters;
    std::atomic<float> inPeak[2]  { { 0.0f }, { 0.0f } };
    std::atomic<float> outPeak[2] { { 0.0f }, { 0.0f } };
    std::atomic<bool>  sidechainConnected { false };
    std::atomic<double> currentSR { 44100.0 };

    SMB::AnalyzerFifo fifoPre, fifoPost, fifoSC;

    // A/B compare
    void selectAB (int slot);
    void copyCurrentToOther();
    int  getABSlot() const { return abSlot; }

    float bandParam (int band, int p) const { return bandPtr[(size_t) band][(size_t) p]->load(); }

    // internal sidechain: which frequency range drives band b's detector
    // returns the trigger band index (b itself, another band) or -1 for the free range
    int detectorRange (int b, float& lo, float& hi, int& loOrd, int& hiOrd) const
    {
        const int t = juce::jlimit (0, 7, (int) bandParam (b, SMB::Trig));
        int src = b;
        if (t >= 1 && t <= SMB::kNumBands && t - 1 != b && bandParam (t - 1, SMB::Active) > 0.5f) src = t - 1;
        loOrd = SMB::slopeToOrder ((int) bandParam (src, SMB::LoSlope));
        hiOrd = SMB::slopeToOrder ((int) bandParam (src, SMB::HiSlope));
        if (t == 7)
        {
            lo = bandParam (b, SMB::FLo); hi = bandParam (b, SMB::FHi);
            if (lo > hi) std::swap (lo, hi);
            return -1;
        }
        lo = bandParam (src, SMB::Lo); hi = bandParam (src, SMB::Hi);
        return src;
    }

private:
    void timerCallback() override;
    void processChunk (float* const* io, int nCh, const float* const* sc, bool hasSC, int n);
    void updateFIRs (bool force);
    void updateLatency();

    static float computeGain (float x, float T, float R, float K, float rng, bool expand);

    struct BandDSP
    {
        SMB::ButterChain aHP, aLP, dHP, dLP;
        SMB::DynShelf loSh, hiSh;
        juce::AudioBuffer<float> gdb;   // per-sample total band gain (dB), per channel
        SMB::DelayLine audDelay[2], gainDelay[2];
        float env[2] {}, gs[2] {};
        bool  wasActive = false;
        std::unique_ptr<juce::dsp::Convolution> conv;
        float firLo = -1, firHi = -1; int firLoS = -1, firHiS = -1;
        void reset()
        {
            aHP.reset(); aLP.reset(); dHP.reset(); dLP.reset(); loSh.reset(); hiSh.reset();
            for (auto& d : audDelay)  d.clear();
            for (auto& d : gainDelay) d.clear();
            env[0] = env[1] = gs[0] = gs[1] = 0.0f;
        }
    };

    juce::dsp::ConvolutionMessageQueue convQueue;
    std::array<BandDSP, SMB::kNumBands> bands;
    std::array<std::array<std::atomic<float>*, SMB::NumBP>, SMB::kNumBands> bandPtr {};
    std::atomic<float>* pIn = nullptr; std::atomic<float>* pOut = nullptr; std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pMode = nullptr; std::atomic<float>* pLA = nullptr;

    SMB::DelayLine dryDelay[2];
    juce::AudioBuffer<float> inBuf, bandBuf, detBuf, outBuf, soloBuf, audBuf;
    std::vector<float> monoTmp;

    double sr = 44100.0;
    int maxBlock = 512;
    int firOrder = 12;
    int laMaxSamples = 0, linLatency = 0, maxDelay = 0;
    int reportedLatency = -1;
    std::atomic<bool> firReady { false };

    juce::ValueTree abSlots[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SilverMBAudioProcessor)
};
