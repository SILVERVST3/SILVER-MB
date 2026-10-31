#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace SMB;

SilverMBAudioProcessor::SilverMBAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, &undoManager, "SILVERMB", createLayout())
{
    for (int b = 0; b < kNumBands; ++b)
        for (int p = 0; p < NumBP; ++p)
            bandPtr[(size_t) b][(size_t) p] = apvts.getRawParameterValue (bandID (b, p));

    pIn   = apvts.getRawParameterValue (ID::inGain);
    pOut  = apvts.getRawParameterValue (ID::outGain);
    pMix  = apvts.getRawParameterValue (ID::mix);
    pMode = apvts.getRawParameterValue (ID::procMode);
    pLA   = apvts.getRawParameterValue (ID::laOn);

    for (auto& b : bands)
        b.conv = std::make_unique<juce::dsp::Convolution> (juce::dsp::Convolution::Latency { 512 }, convQueue);

    startTimerHz (15);
}

SilverMBAudioProcessor::~SilverMBAudioProcessor() { stopTimer(); }

bool SilverMBAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void SilverMBAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    currentSR = sampleRate;
    maxBlock = juce::jmax (32, samplesPerBlock);
    firOrder = sr <= 50000.0 ? 12 : (sr <= 100000.0 ? 13 : 14);

    juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 2 };
    for (auto& b : bands)
    {
        b.conv->prepare (spec);
        b.firLo = -1;
    }

    laMaxSamples = (int) std::ceil (kMaxLAms * 0.001 * sr);
    linLatency   = (1 << (firOrder - 1)) + bands[0].conv->getLatency();
    maxDelay     = laMaxSamples + linLatency + 16;

    for (auto& d : dryDelay) d.prepare (maxDelay);
    for (auto& b : bands)
    {
        for (auto& d : b.audDelay)  d.prepare (maxDelay);
        for (auto& d : b.gainDelay) d.prepare (maxDelay);
        b.gdb.setSize (2, maxBlock);
        b.reset();
        b.wasActive = false;
    }

    inBuf.setSize (2, maxBlock);  bandBuf.setSize (2, maxBlock); detBuf.setSize (2, maxBlock);
    outBuf.setSize (2, maxBlock); soloBuf.setSize (2, maxBlock); audBuf.setSize (2, maxBlock);
    monoTmp.assign ((size_t) maxBlock, 0.0f);

    updateFIRs (true);
    reportedLatency = -1;
    updateLatency();
}

void SilverMBAudioProcessor::updateLatency()
{
    const bool lin = pMode->load() > 0.5f;
    const bool la  = pLA->load() > 0.5f;
    const int lat  = (la ? laMaxSamples : 0) + (lin ? linLatency : 0);
    if (lat != reportedLatency) { reportedLatency = lat; setLatencySamples (lat); }
}

void SilverMBAudioProcessor::updateFIRs (bool force)
{
    if (pMode->load() < 0.5f && ! force) return;

    for (int b = 0; b < kNumBands; ++b)
    {
        auto& B = bands[(size_t) b];
        if (! force && bandParam (b, Active) < 0.5f) continue;

        const float lo = bandParam (b, Lo), hi = bandParam (b, Hi);
        const int los = (int) bandParam (b, LoSlope), his = (int) bandParam (b, HiSlope);
        if (! force && lo == B.firLo && hi == B.firHi && los == B.firLoS && his == B.firHiS) continue;

        juce::AudioBuffer<float> ir;
        designBandFIR (ir, firOrder, sr, lo, slopeToOrder (los), hi, slopeToOrder (his));
        B.conv->loadImpulseResponse (std::move (ir), sr, juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        B.firLo = lo; B.firHi = hi; B.firLoS = los; B.firHiS = his;
    }
}

void SilverMBAudioProcessor::timerCallback()
{
    updateFIRs (false);
}

// gain computer: returns gain change in dB (negative = cut, positive = boost)
float SilverMBAudioProcessor::computeGain (float x, float T, float R, float K, float rng, bool expand)
{
    auto soft = [K] (float d)
    {
        if (K <= 0.01f) return juce::jmax (0.0f, d);
        if (d <= -0.5f * K) return 0.0f;
        if (d >=  0.5f * K) return d;
        const float t = d + 0.5f * K;
        return t * t / (2.0f * K);
    };

    const float compSlope = R >= 99.5f ? 1.0f : 1.0f - 1.0f / R;
    const float expSlope  = juce::jmin (R - 1.0f, 50.0f);

    if (! expand)
    {
        if (rng <= 0.0f) return juce::jmax (-soft (x - T) * compSlope, rng);   // downward compression
        return juce::jmin (soft (T - x) * compSlope, rng);                       // upward compression
    }
    if (rng <= 0.0f) return juce::jmax (-soft (T - x) * expSlope, rng);         // downward expansion / gate
    return juce::jmin (soft (x - T) * expSlope, rng);                            // upward expansion
}

void SilverMBAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto mainBus = getBusBuffer (buffer, true, 0);
    const int nCh = juce::jmin (2, mainBus.getNumChannels());
    const int N   = buffer.getNumSamples();
    if (nCh == 0 || N == 0) return;

    bool hasSC = false;
    const float* scPtr[2] { nullptr, nullptr };
    if (getBusCount (true) > 1)
    {
        auto scBus = getBusBuffer (buffer, true, 1);
        if (scBus.getNumChannels() > 0)
        {
            hasSC = true;
            scPtr[0] = scBus.getReadPointer (0);
            scPtr[1] = scBus.getReadPointer (juce::jmin (1, scBus.getNumChannels() - 1));
        }
    }
    sidechainConnected = hasSC;

    updateLatency();

    for (int off = 0; off < N; off += maxBlock)
    {
        const int n = juce::jmin (maxBlock, N - off);
        float* io[2] { mainBus.getWritePointer (0) + off, mainBus.getWritePointer (nCh > 1 ? 1 : 0) + off };
        const float* sc[2] { hasSC ? scPtr[0] + off : nullptr, hasSC ? scPtr[1] + off : nullptr };
        processChunk (io, nCh, sc, hasSC, n);
    }

    // silence any extra output channels
    for (int ch = nCh; ch < getTotalNumOutputChannels(); ++ch)
        if (ch < buffer.getNumChannels()) buffer.clear (ch, 0, N);
}

void SilverMBAudioProcessor::processChunk (float* const* io, int nCh, const float* const* sc, bool hasSC, int n)
{
    const float inG  = juce::Decibels::decibelsToGain (pIn->load());
    const float outG = juce::Decibels::decibelsToGain (pOut->load());
    const float mixS = pMix->load() / 100.0f;
    const bool  lin  = pMode->load() > 0.5f;
    const bool  laOn = pLA->load() > 0.5f;
    const int   Dla  = laOn ? laMaxSamples : 0;
    const int   Dtot = Dla + (lin ? linLatency : 0);

    // ---- input stage ----
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = inBuf.getWritePointer (ch);
        const float* src = io[juce::jmin (ch, nCh - 1)];
        float pk = 0.0f;
        for (int i = 0; i < n; ++i) { d[i] = src[i] * inG; pk = juce::jmax (pk, std::abs (d[i])); }
        if (ch < nCh) inPeak[ch] = juce::jmax (inPeak[ch].load(), pk);
    }
    {
        const auto* l = inBuf.getReadPointer (0); const auto* r = inBuf.getReadPointer (1);
        for (int i = 0; i < n; ++i) monoTmp[(size_t) i] = 0.5f * (l[i] + r[i]);
        fifoPre.push (monoTmp.data(), n);
        if (hasSC)
        {
            for (int i = 0; i < n; ++i) monoTmp[(size_t) i] = 0.5f * (sc[0][i] + sc[1][i]);
            fifoSC.push (monoTmp.data(), n);
        }
    }

    // ---- delayed dry path ----
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto* x = inBuf.getReadPointer (ch);
        auto* y = outBuf.getWritePointer (ch);
        for (int i = 0; i < n; ++i) y[i] = dryDelay[ch].process (x[i], Dtot);
    }

    bool anySolo = false, anyAud = false;
    for (int b = 0; b < kNumBands; ++b)
        if (bandParam (b, Active) > 0.5f)
        {
            anySolo |= bandParam (b, Solo) > 0.5f;
            anyAud  |= bandParam (b, Audition) > 0.5f;
        }
    soloBuf.clear(); audBuf.clear();

    const int stereo = nCh > 1 ? 2 : 1;
    int activeStm[kNumBands] {};
    bool activeB[kNumBands] {};

    // ================= 1) detection + gain computation for every band =================
    for (int b = 0; b < kNumBands; ++b)
    {
        auto& B = bands[(size_t) b];
        auto& meter = meters[(size_t) b];

        if (bandParam (b, Active) < 0.5f)
        {
            B.wasActive = false;
            meter.dynDb = 0.0f; meter.levelDb = -120.0f;
            continue;
        }
        if (! B.wasActive) { B.reset(); B.wasActive = true; }
        activeB[b] = true;

        const float lo = bandParam (b, Lo), hi = bandParam (b, Hi);
        const int loOrd = slopeToOrder ((int) bandParam (b, LoSlope));
        const int hiOrd = slopeToOrder ((int) bandParam (b, HiSlope));
        const bool freeDet = bandParam (b, Free) > 0.5f;
        const float dLo = freeDet ? bandParam (b, FLo) : lo;
        const float dHi = freeDet ? bandParam (b, FHi) : hi;
        const bool ext = bandParam (b, Ext) > 0.5f && hasSC;

        B.aHP.setup (true,  loOrd, lo, sr, loIsOpen (lo));
        B.aLP.setup (false, hiOrd, hi, sr, hiIsOpen (hi));
        B.dHP.setup (true,  loOrd, dLo, sr, loIsOpen (dLo));
        B.dLP.setup (false, hiOrd, dHi, sr, hiIsOpen (dHi));
        B.loSh.setup (loOrd, lo, sr);
        B.hiSh.setup (hiOrd, hi, sr);

        // detector signal
        for (int ch = 0; ch < stereo; ++ch)
        {
            const float* x = ext ? sc[ch] : inBuf.getReadPointer (ch);
            auto* y = detBuf.getWritePointer (ch);
            for (int i = 0; i < n; ++i) y[i] = B.dLP.process (B.dHP.process (x[i], ch), ch);
        }
        if (stereo == 1) detBuf.copyFrom (1, 0, detBuf, 0, 0, n);

        const float T    = bandParam (b, Thresh);
        const float R    = bandParam (b, Ratio);
        const float K    = bandParam (b, Knee);
        const float rng  = bandParam (b, Range);
        const bool  expd = bandParam (b, Mode) > 0.5f;
        const float link = bandParam (b, Link) / 100.0f;
        const int   stm  = stereo == 2 ? (int) bandParam (b, StMode) : 0;
        const float gOut = bandParam (b, Gain);
        const bool  mute = bandParam (b, Mute) > 0.5f;
        const bool  byp  = bandParam (b, Bypass) > 0.5f;
        const bool  aud  = bandParam (b, Audition) > 0.5f;
        const int   laS  = laOn ? juce::jlimit (0, Dla, (int) std::round (bandParam (b, LA) * 0.001 * sr)) : 0;
        const int   gDel = juce::jmax (0, Dtot - laS);
        activeStm[b] = stm;

        // frequency-adaptive timing: low bands react slower, high bands faster
        const float fc     = bandCentre (dLo, dHi);
        const float adapt  = juce::jlimit (0.5f, 2.5f, std::pow (1000.0f / fc, 0.3f));
        const float atkC   = std::exp (-1.0f / (attackMs  (bandParam (b, Attack))  * 0.001f * adapt * (float) sr));
        const float relC   = std::exp (-1.0f / (releaseMs (bandParam (b, Release)) * 0.001f * adapt * (float) sr));
        const float detTau = juce::jlimit (0.0005f, 0.03f, 1.5f / fc);
        const float detC   = std::exp (-1.0f / (detTau * (float) sr));

        const float* detL = detBuf.getReadPointer (0); const float* detR = detBuf.getReadPointer (1);
        float* g0 = B.gdb.getWritePointer (0); float* g1 = B.gdb.getWritePointer (1);
        float* auL = audBuf.getWritePointer (0); float* auR = audBuf.getWritePointer (1);
        float maxLevel = -120.0f, lastDyn = 0.0f;

        auto smooth = [&] (float tgt, float& s)
        {
            const float c = std::abs (tgt) > std::abs (s) ? atkC : relC;
            s = tgt + c * (s - tgt);
            return s;
        };
        auto total = [&] (float dynDb)
        {
            if (byp)  return 0.0f;
            if (mute) return -60.0f;
            return juce::jlimit (-60.0f, 36.0f, mixS * (dynDb + gOut));
        };

        for (int i = 0; i < n; ++i)
        {
            if (stm == 0)
            {
                const float aL = std::abs (detL[i]), aR = std::abs (detR[i]);
                B.env[0] = aL > B.env[0] ? aL : B.env[0] * detC;
                B.env[1] = aR > B.env[1] ? aR : B.env[1] * detC;
                const float mx = juce::jmax (B.env[0], B.env[1]);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float e   = (1.0f - link) * B.env[ch] + link * mx;
                    const float lvl = 20.0f * std::log10 (e + 1.0e-9f);
                    maxLevel = juce::jmax (maxLevel, lvl);
                    const float dyn = B.gainDelay[ch].process (smooth (computeGain (lvl, T, R, K, rng, expd), B.gs[ch]), gDel);
                    if (std::abs (dyn) > std::abs (lastDyn)) lastDyn = dyn;
                    (ch == 0 ? g0 : g1)[i] = total (dyn);
                }
                if (aud) { auL[i] += detL[i]; auR[i] += detR[i]; }
            }
            else
            {
                const float sgn = stm == 1 ? 1.0f : -1.0f;               // 1 = Mid, 2 = Side
                const float d   = 0.5f * (detL[i] + sgn * detR[i]);
                const float a   = std::abs (d);
                B.env[0] = a > B.env[0] ? a : B.env[0] * detC;
                const float lvl = 20.0f * std::log10 (B.env[0] + 1.0e-9f);
                maxLevel = juce::jmax (maxLevel, lvl);
                const float dyn = B.gainDelay[0].process (smooth (computeGain (lvl, T, R, K, rng, expd), B.gs[0]), gDel);
                lastDyn = dyn;
                g0[i] = g1[i] = total (dyn);
                if (aud) { auL[i] += d; auR[i] += d * sgn; }
            }
        }
        meter.dynDb   = lastDyn;
        meter.levelDb = maxLevel;
    }

    float* outL = outBuf.getWritePointer (0); float* outR = outBuf.getWritePointer (1);

    // ================= 2a) Linear Phase: y = x + sum (g-1) * FIR_band(x) =================
    if (lin)
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            if (! activeB[b]) continue;
            auto& B = bands[(size_t) b];
            juce::dsp::AudioBlock<const float> inBlk (inBuf.getArrayOfReadPointers(), (size_t) stereo, (size_t) n);
            juce::dsp::AudioBlock<float> outBlk (bandBuf.getArrayOfWritePointers(), (size_t) stereo, (size_t) n);
            juce::dsp::ProcessContextNonReplacing<float> ctx (inBlk, outBlk);
            B.conv->process (ctx);
            for (int ch = 0; ch < stereo; ++ch)
            {
                auto* y = bandBuf.getWritePointer (ch);
                for (int i = 0; i < n; ++i) y[i] = B.audDelay[ch].process (y[i], Dla);
            }
            if (stereo == 1) bandBuf.copyFrom (1, 0, bandBuf, 0, 0, n);

            const float* bandL = bandBuf.getReadPointer (0); const float* bandR = bandBuf.getReadPointer (1);
            const float* g0 = B.gdb.getReadPointer (0); const float* g1 = B.gdb.getReadPointer (1);
            const bool solo = bandParam (b, Solo) > 0.5f;
            float* soL = soloBuf.getWritePointer (0); float* soR = soloBuf.getWritePointer (1);
            const int stm = activeStm[b];

            for (int i = 0; i < n; ++i)
            {
                const float gl = juce::Decibels::decibelsToGain (g0[i], -59.9f);
                const float gr = juce::Decibels::decibelsToGain (g1[i], -59.9f);
                if (stm == 0)
                {
                    outL[i] += (gl - 1.0f) * bandL[i];
                    outR[i] += (gr - 1.0f) * bandR[i];
                    if (solo) { soL[i] += gl * bandL[i]; soR[i] += gr * bandR[i]; }
                }
                else
                {
                    const float sgn = stm == 1 ? 1.0f : -1.0f;
                    const float bm = 0.5f * (bandL[i] + sgn * bandR[i]);
                    outL[i] += (gl - 1.0f) * bm;
                    outR[i] += (gl - 1.0f) * bm * sgn;
                    if (solo) { soL[i] += gl * bm; soR[i] += gl * bm * sgn; }
                }
            }
        }
    }
    // ================= 2b) Dynamic Phase: cascade of reciprocal Butterworth shelf pairs =================
    else
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            if (! activeB[b]) continue;
            auto& B = bands[(size_t) b];
            const float lo = bandParam (b, Lo), hi = bandParam (b, Hi);
            const bool lOpen = loIsOpen (lo), hOpen = hiIsOpen (hi);
            const float* g0 = B.gdb.getReadPointer (0); const float* g1 = B.gdb.getReadPointer (1);
            const int stm = activeStm[b];

            auto apply = [&] (float x, int ch, float gd)
            {
                if (lOpen && hOpen) return x * juce::Decibels::decibelsToGain (gd, -99.0f);
                if (lOpen)          return B.hiSh.process (x * juce::Decibels::decibelsToGain (gd, -99.0f), ch, -gd);
                if (hOpen)          return B.loSh.process (x, ch, gd);
                return B.hiSh.process (B.loSh.process (x, ch, gd), ch, -gd);
            };

            for (int i = 0; i < n; ++i)
            {
                if (stm == 0)
                {
                    outL[i] = apply (outL[i], 0, g0[i]);
                    if (stereo == 2) outR[i] = apply (outR[i], 1, g1[i]);
                    else             outR[i] = outL[i];
                }
                else
                {
                    const float sgn = stm == 1 ? 1.0f : -1.0f;
                    const float tgt = 0.5f * (outL[i] + sgn * outR[i]);      // mid or side
                    const float oth = 0.5f * (outL[i] - sgn * outR[i]);      // the other one
                    const float t2  = apply (tgt, 0, g0[i]);
                    outL[i] = t2 + oth;
                    outR[i] = sgn * (t2 - oth);
                }
            }
        }

        // solo: listen to the processed signal through each soloed band's crossover filters
        if (anySolo)
        {
            float* soL = soloBuf.getWritePointer (0); float* soR = soloBuf.getWritePointer (1);
            for (int b = 0; b < kNumBands; ++b)
            {
                if (! activeB[b] || bandParam (b, Solo) < 0.5f) continue;
                auto& B = bands[(size_t) b];
                for (int i = 0; i < n; ++i)
                {
                    soL[i] += B.aLP.process (B.aHP.process (outL[i], 0), 0);
                    soR[i] += B.aLP.process (B.aHP.process (outR[i], 1), 1);
                }
            }
        }
    }

    const auto& fin = anyAud ? audBuf : (anySolo ? soloBuf : outBuf);

    for (int ch = 0; ch < nCh; ++ch)
    {
        const auto* s = fin.getReadPointer (ch);
        float pk = 0.0f;
        for (int i = 0; i < n; ++i) { io[ch][i] = s[i] * outG; pk = juce::jmax (pk, std::abs (io[ch][i])); }
        outPeak[ch] = juce::jmax (outPeak[ch].load(), pk);
    }
    {
        for (int i = 0; i < n; ++i) monoTmp[(size_t) i] = nCh > 1 ? 0.5f * (io[0][i] + io[1][i]) : io[0][i];
        fifoPost.push (monoTmp.data(), n);
    }
}

// ---------------------------------------------------------------------------
void SilverMBAudioProcessor::selectAB (int slot)
{
    if (slot == abSlot) return;
    abSlots[abSlot] = apvts.copyState();
    abSlot = slot;
    if (abSlots[abSlot].isValid())
        apvts.replaceState (abSlots[abSlot].createCopy());
}

void SilverMBAudioProcessor::copyCurrentToOther()
{
    abSlots[1 - abSlot] = apvts.copyState();
}

void SilverMBAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void SilverMBAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SilverMBAudioProcessor::createEditor() { return new SilverMBAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SilverMBAudioProcessor(); }
