#include "BandDisplay.h"

using namespace SMB;

static const float kLog1000 = std::log (1000.0f);

BandDisplay::BandDisplay (SilverMBAudioProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setRepaintsOnMouseActivity (false);
    startTimerHz (30);
}

BandDisplay::~BandDisplay() { stopTimer(); endAllGestures(); }

void BandDisplay::resized()
{
    plot = getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (18.0f).withTrimmedRight (34.0f).withTrimmedLeft (6.0f).withTrimmedTop (4.0f);
    numCols = juce::jmax (8, (int) (plot.getWidth() / 2.0f) + 1);
    for (auto* a : { &pre, &post, &sc })
    {
        a->disp.assign ((size_t) numCols, -150.0f);
        a->frozen.assign ((size_t) numCols, -150.0f);
    }
}

// ---------------------------------------------------------------- geometry
float BandDisplay::dispRange() const
{
    static const float r[] = { 3.f, 6.f, 12.f, 30.f };
    return r[juce::jlimit (0, 3, (int) proc.apvts.state.getProperty ("dispRange", 2))];
}
float BandDisplay::freqToX (float f) const { return plot.getX() + plot.getWidth() * std::log (juce::jmax (1.0f, f) / kMinFreq) / kLog1000; }
float BandDisplay::xToFreq (float x) const { return kMinFreq * std::exp ((x - plot.getX()) / plot.getWidth() * kLog1000); }
float BandDisplay::gainToY (float db) const { return plot.getCentreY() - db / dispRange() * plot.getHeight() * 0.46f; }
float BandDisplay::yToGain (float y) const  { return (plot.getCentreY() - y) / (plot.getHeight() * 0.46f) * dispRange(); }
float BandDisplay::anaToY (float db) const  { return plot.getBottom() - (db + 90.0f) / 90.0f * plot.getHeight() * 0.92f; }

float BandDisplay::magAt (int b, float f) const
{
    return bandMag (f, bp (b, Lo), slopeToOrder ((int) bp (b, LoSlope)), bp (b, Hi), slopeToOrder ((int) bp (b, HiSlope)));
}

juce::Point<float> BandDisplay::dotPos (int b) const
{
    return { freqToX (bandCentre (bp (b, Lo), bp (b, Hi))), gainToY (juce::jlimit (-dispRange(), dispRange(), bp (b, Gain))) };
}

juce::Point<float> BandDisplay::rangePos (int b) const
{
    const float r = bp (b, Gain) + bp (b, Range);
    return { dotPos (b).x, gainToY (juce::jlimit (-dispRange() * 1.05f, dispRange() * 1.05f, r)) };
}

BandDisplay::HitInfo BandDisplay::hitTest (juce::Point<float> pt) const
{
    HitInfo best;
    float bestD = 1e9f;
    auto order = [this] (int i) { return selected >= 0 ? (i == 0 ? selected : (i <= selected ? i - 1 : i)) : i; };

    for (int i = 0; i < kNumBands; ++i)
    {
        const int b = order (i);
        if (b < 0 || b >= kNumBands || ! isActive (b)) continue;
        const float d = dotPos (b).getDistanceFrom (pt);
        if (d < 10.0f && d < bestD) { bestD = d; best = { Hit::Dot, b }; }
    }
    if (best.what != Hit::None) return best;

    for (int i = 0; i < kNumBands; ++i)
    {
        const int b = order (i);
        if (b < 0 || b >= kNumBands || ! isActive (b)) continue;
        const bool focus = (b == selected || b == hover.band);
        if (focus)
        {
            const auto rp = rangePos (b);
            if (std::abs (pt.x - rp.x) < 14.0f && std::abs (pt.y - rp.y) < 6.0f) return { Hit::Range, b };
        }
        const bool inHandleZone = pt.y > plot.getBottom() - 26.0f;
        if (focus || inHandleZone)
        {
            const float lo = bp (b, Lo), hi = bp (b, Hi);
            if (! loIsOpen (lo) && std::abs (pt.x - freqToX (lo)) < 5.0f) return { Hit::LoEdge, b };
            if (! hiIsOpen (hi) && std::abs (pt.x - freqToX (hi)) < 5.0f) return { Hit::HiEdge, b };
        }
    }
    return {};
}

// ---------------------------------------------------------------- params
juce::RangedAudioParameter* BandDisplay::param (int b, int p) const { return proc.apvts.getParameter (bandID (b, p)); }

void BandDisplay::setP (int b, int p, float v)
{
    if (auto* prm = param (b, p))
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

void BandDisplay::beginGestureFor (int b, int p)
{
    if (auto* prm = param (b, p))
        if (std::find (gestures.begin(), gestures.end(), prm) == gestures.end())
        {
            prm->beginChangeGesture();
            gestures.push_back (prm);
        }
}

void BandDisplay::endAllGestures()
{
    for (auto* g : gestures) g->endChangeGesture();
    gestures.clear();
}

void BandDisplay::resetBandToDefaults (int b)
{
    for (int p = 0; p < NumBP; ++p)
        if (auto* prm = param (b, p))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->getDefaultValue());
            prm->endChangeGesture();
        }
}

int BandDisplay::createBand (float f)
{
    int slot = -1;
    for (int b = 0; b < kNumBands; ++b) if (! isActive (b)) { slot = b; break; }
    if (slot < 0) return -1;

    proc.undoManager.beginNewTransaction();
    resetBandToDefaults (slot);
    const float lo = juce::jlimit (21.0f, 14000.0f, f / std::sqrt (2.0f));
    const float hi = juce::jlimit (lo * 1.2f, 19800.0f, f * std::sqrt (2.0f));
    for (auto [p, v] : { std::pair<int, float> { Lo, lo }, { Hi, hi }, { Active, 1.0f } })
    {
        beginGestureFor (slot, p); setP (slot, p, v);
    }
    endAllGestures();
    setSelected (slot);
    return slot;
}

void BandDisplay::deleteBand (int b)
{
    if (b < 0) return;
    proc.undoManager.beginNewTransaction();
    for (int p : { (int) Solo, (int) Audition, (int) Active })
    {
        beginGestureFor (b, p); setP (b, p, 0.0f);
    }
    endAllGestures();
    if (selected == b) setSelected (-1);
}

void BandDisplay::setSelected (int b)
{
    if (b == selected) return;
    selected = b;
    if (onSelectionChanged) onSelectionChanged (b);
    repaint();
}

float BandDisplay::snapFreq (float f, int excludeBand) const
{
    const float x = freqToX (f);
    float best = f, bestD = 8.0f;
    for (int b = 0; b < kNumBands; ++b)
    {
        if (b == excludeBand || ! isActive (b)) continue;
        for (float e : { bp (b, Lo), bp (b, Hi) })
        {
            if (loIsOpen (e) || hiIsOpen (e)) continue;
            const float d = std::abs (freqToX (e) - x);
            if (d < bestD) { bestD = d; best = e; }
        }
    }
    return best;
}

// ---------------------------------------------------------------- mouse
void BandDisplay::mouseMove (const juce::MouseEvent& e)
{
    auto h = hitTest (e.position);
    if (h.what != hover.what || h.band != hover.band)
    {
        hover = h;
        switch (h.what)
        {
            case Hit::LoEdge: case Hit::HiEdge: setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
            case Hit::Range:  setMouseCursor (juce::MouseCursor::UpDownResizeCursor); break;
            case Hit::Dot:    setMouseCursor (juce::MouseCursor::DraggingHandCursor); break;
            default:          setMouseCursor (juce::MouseCursor::NormalCursor); break;
        }
    }
}

void BandDisplay::mouseExit (const juce::MouseEvent&) { hover = {}; }

void BandDisplay::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    proc.undoManager.beginNewTransaction();
    drag = hitTest (e.position);
    dragStart = e.position;
    axisLock = 0;
    linkedEdges.clear();

    if (drag.what == Hit::None)
    {
        setSelected (-1);
        return;
    }

    const int b = drag.band;
    setSelected (b);

    if (e.mods.isPopupMenu()) { drag = {}; showBandMenu (b); return; }

    startLo = bp (b, Lo); startHi = bp (b, Hi); startGain = bp (b, Gain); startRange = bp (b, Range);

    if (drag.what == Hit::Dot)    { beginGestureFor (b, Lo); beginGestureFor (b, Hi); beginGestureFor (b, Gain); }
    if (drag.what == Hit::Range)  { beginGestureFor (b, Range); }
    if (drag.what == Hit::LoEdge || drag.what == Hit::HiEdge)
    {
        const bool isLo = drag.what == Hit::LoEdge;
        const float edge = isLo ? startLo : startHi;
        beginGestureFor (b, isLo ? Lo : Hi);
        for (int c = 0; c < kNumBands; ++c)
        {
            if (c == b || ! isActive (c)) continue;
            const float other = bp (c, isLo ? Hi : Lo);
            if (std::abs (std::log (other / edge)) < 0.004f)
            {
                linkedEdges.push_back ({ c, isLo ? Hi : Lo });
                beginGestureFor (c, isLo ? Hi : Lo);
            }
        }
    }
}

void BandDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (drag.what == Hit::None) return;
    const int b = drag.band;
    const float fine = e.mods.isShiftDown() ? 0.25f : 1.0f;
    const auto delta = (e.position - dragStart) * fine;

    if (drag.what == Hit::Dot)
    {
        if (! e.mods.isAltDown() && axisLock == 0 && delta.getDistanceFromOrigin() > 4.0f)
            axisLock = std::abs (delta.x) > std::abs (delta.y) ? 1 : 2;
        const bool doH = e.mods.isAltDown() || axisLock == 1;
        const bool doV = e.mods.isAltDown() || axisLock == 2;

        if (doH)
        {
            const float oct = delta.x / plot.getWidth() * std::log2 (1000.0f);
            const bool loOpen = loIsOpen (startLo), hiOpen = hiIsOpen (startHi);
            float lo = startLo, hi = startHi;
            if (loOpen && ! hiOpen)       hi = juce::jlimit (30.0f, 19800.0f, snapFreq (startHi * std::exp2 (oct), b));
            else if (hiOpen && ! loOpen)  lo = juce::jlimit (21.0f, 14000.0f, snapFreq (startLo * std::exp2 (oct), b));
            else if (! loOpen && ! hiOpen)
            {
                const float ratio = startHi / startLo;
                lo = startLo * std::exp2 (oct);
                lo = juce::jlimit (20.6f, 19800.0f / ratio, lo);
                const float sLo = snapFreq (lo, b);
                if (sLo != lo) lo = sLo;
                else
                {
                    const float sHi = snapFreq (lo * ratio, b);
                    if (sHi != lo * ratio) lo = sHi / ratio;
                }
                lo = juce::jlimit (20.6f, 19800.0f / ratio, lo);
                hi = lo * ratio;
            }
            setP (b, Lo, lo); setP (b, Hi, hi);
        }
        if (doV)
        {
            const float g = juce::jlimit (-30.0f, 30.0f, startGain + yToGain (dragStart.y + delta.y) - yToGain (dragStart.y));
            setP (b, Gain, std::abs (g) < 0.15f * dispRange() / 12.0f ? 0.0f : g);
        }
    }
    else if (drag.what == Hit::Range)
    {
        const float r = juce::jlimit (-30.0f, 30.0f, startRange + yToGain (dragStart.y + delta.y) - yToGain (dragStart.y));
        setP (b, Range, r);
    }
    else
    {
        const bool isLo = drag.what == Hit::LoEdge;
        const float x = dragStart.x + delta.x;
        float f = xToFreq (x);
        f = snapFreq (f, b);
        if (x < plot.getX() + 8.0f)     f = kMinFreq;
        if (x > plot.getRight() - 8.0f) f = kMaxFreq;

        if (isLo)
        {
            const float hi = bp (b, Hi);
            f = juce::jlimit (kMinFreq, hiIsOpen (hi) ? 19000.0f : hi / 1.08f, f);
        }
        else
        {
            const float lo = bp (b, Lo);
            f = juce::jlimit (loIsOpen (lo) ? 25.0f : lo * 1.08f, kMaxFreq, f);
        }
        setP (b, isLo ? Lo : Hi, f);

        for (auto [c, p] : linkedEdges)
        {
            float g = f;
            if (p == Hi) { const float lo = bp (c, Lo); g = juce::jmax (loIsOpen (lo) ? 25.0f : lo * 1.08f, g); }
            else         { const float hi = bp (c, Hi); g = juce::jmin (hiIsOpen (hi) ? 19000.0f : hi / 1.08f, g); }
            setP (c, p, g);
        }
    }
}

void BandDisplay::mouseUp (const juce::MouseEvent&)
{
    endAllGestures();
    drag = {};
    linkedEdges.clear();
}

void BandDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto h = hitTest (e.position);
    if (h.what == Hit::Dot)
    {
        proc.undoManager.beginNewTransaction();
        beginGestureFor (h.band, Gain); setP (h.band, Gain, 0.0f); endAllGestures();
        return;
    }
    if (h.what == Hit::None && plot.contains (e.position))
        createBand (xToFreq (e.position.x));
}

void BandDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int b = hitTest (e.position).band;
    if (b < 0) b = selected;
    if (b < 0 || ! isActive (b)) return;

    const float lo = bp (b, Lo), hi = bp (b, Hi);
    const bool loOpen = loIsOpen (lo), hiOpen = hiIsOpen (hi);
    const float step = (w.deltaY >= 0 ? 1.0f : -1.0f) * (e.mods.isShiftDown() ? 0.05f : 0.2f);

    proc.undoManager.beginNewTransaction();
    beginGestureFor (b, Lo); beginGestureFor (b, Hi);
    if (loOpen && ! hiOpen)       setP (b, Hi, juce::jlimit (30.0f, 19800.0f, hi * std::exp2 (step)));
    else if (hiOpen && ! loOpen)  setP (b, Lo, juce::jlimit (21.0f, 14000.0f, lo * std::exp2 (-step)));
    else if (! loOpen && ! hiOpen)
    {
        const float c  = std::sqrt (lo * hi);
        const float hw = juce::jlimit (0.06f, 5.0f, 0.5f * std::log2 (hi / lo) + step * 0.5f);
        setP (b, Lo, juce::jmax (20.6f, c / std::exp2 (hw)));
        setP (b, Hi, juce::jmin (19800.0f, c * std::exp2 (hw)));
    }
    endAllGestures();
}

bool BandDisplay::keyPressed (const juce::KeyPress& k)
{
    if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && selected >= 0)
    {
        deleteBand (selected);
        return true;
    }
    return false;
}

void BandDisplay::showBandMenu (int b)
{
    juce::PopupMenu m;
    m.addSectionHeader ("Banda " + juce::String (b + 1));
    m.addItem (1, "Solo",   true, bp (b, Solo) > 0.5f);
    m.addItem (2, "Mute",   true, bp (b, Mute) > 0.5f);
    m.addItem (3, "Bypass", true, bp (b, Bypass) > 0.5f);
    m.addSeparator();
    m.addItem (4, "Compress", true, bp (b, Mode) < 0.5f);
    m.addItem (5, "Expand",   true, bp (b, Mode) > 0.5f);
    m.addSeparator();
    {
        juce::PopupMenu t;
        const int cur = (int) bp (b, Trig);
        t.addItem (100, juce::CharPointer_UTF8 ("Pr\xc3\xb3pria banda"), true, cur == 0);
        for (int i = 0; i < kNumBands; ++i)
            if (i != b) t.addItem (101 + i, "Banda " + juce::String (i + 1), isActive (i), cur == i + 1);
        t.addItem (107, "Faixa livre (Expert)", true, cur == 7);
        m.addSubMenu ("Trigger (sidechain interno)", t);
        m.addSeparator();
    }
    m.addItem (6, "Resetar ganho");
    m.addItem (7, "Excluir banda");

    juce::Component::SafePointer<BandDisplay> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, b] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& d = *safe;
        d.proc.undoManager.beginNewTransaction();
        auto toggle = [&] (int p) { d.beginGestureFor (b, p); d.setP (b, p, d.bp (b, p) > 0.5f ? 0.0f : 1.0f); };
        switch (r)
        {
            case 1: toggle (Solo); break;
            case 2: toggle (Mute); break;
            case 3: toggle (Bypass); break;
            case 4: d.beginGestureFor (b, Mode); d.setP (b, Mode, 0.0f); break;
            case 5: d.beginGestureFor (b, Mode); d.setP (b, Mode, 1.0f); break;
            case 6: d.beginGestureFor (b, Gain); d.setP (b, Gain, 0.0f); break;
            case 7: d.endAllGestures(); d.deleteBand (b); return;
            case 100: case 101: case 102: case 103: case 104: case 105: case 106: case 107:
                d.beginGestureFor (b, Trig); d.setP (b, Trig, (float) (r - 100)); break;
            default: break;
        }
        d.endAllGestures();
    });
}

// ---------------------------------------------------------------- analyzer
void BandDisplay::ensureFFT()
{
    const int order = 11 + juce::jlimit (0, 2, (int) proc.apvts.state.getProperty ("anaRes", 1));
    if (fft != nullptr && order == fftOrder) return;
    fftOrder = order;
    const int N = 1 << order;
    fft = std::make_unique<juce::dsp::FFT> (order);
    window.resize ((size_t) N);
    for (int i = 0; i < N; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / N);
    fftData.assign ((size_t) (2 * N), 0.0f);
    for (auto* a : { &pre, &post, &sc }) { a->ring.assign ((size_t) N, 0.0f); a->w = 0; }
}

void BandDisplay::feed (AnalyzerFifo& f, Ana& a)
{
    const int N = (int) a.ring.size();
    f.pull ([&a, N] (float x) { a.ring[(size_t) a.w] = x; a.w = (a.w + 1) % N; });
}

void BandDisplay::analyse (Ana& a, bool freeze)
{
    const int N = 1 << fftOrder;
    float wsum = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        fftData[(size_t) i] = a.ring[(size_t) ((a.w + i) % N)] * window[(size_t) i];
        wsum += window[(size_t) i];
    }
    std::fill (fftData.begin() + N, fftData.end(), 0.0f);
    fft->performFrequencyOnlyForwardTransform (fftData.data());

    const float norm = 2.0f / wsum;
    const double sr = proc.currentSR.load();
    static const float falls[] = { 2.5f, 1.2f, 0.5f };
    const float fall = falls[juce::jlimit (0, 2, (int) proc.apvts.state.getProperty ("anaSpeed", 1))];

    for (int c = 0; c < numCols; ++c)
    {
        const float f  = xToFreq (plot.getX() + c * 2.0f);
        const float k1 = (float) (f * std::exp2 (-1.0f / 24.0f) * N / sr);
        const float k2 = (float) (f * std::exp2 ( 1.0f / 24.0f) * N / sr);
        float mag = 0.0f;
        if (k2 - k1 < 1.0f)
        {
            const float k = (float) (f * N / sr);
            const int i0 = juce::jlimit (0, N / 2 - 1, (int) k);
            const float t = k - (float) i0;
            mag = fftData[(size_t) i0] * (1.0f - t) + fftData[(size_t) (i0 + 1)] * t;
        }
        else
        {
            const int i1 = juce::jlimit (0, N / 2, (int) k1), i2 = juce::jlimit (0, N / 2, (int) std::ceil (k2));
            for (int i = i1; i <= i2; ++i) mag = juce::jmax (mag, fftData[(size_t) i]);
        }
        const float db = 20.0f * std::log10 (mag * norm + 1.0e-9f) + 4.5f * std::log2 (f / 1000.0f);
        auto& d = a.disp[(size_t) c];
        d = juce::jmax (db, d - fall);
        if (freeze) a.frozen[(size_t) c] = juce::jmax (a.frozen[(size_t) c], d);
    }
}

void BandDisplay::timerCallback()
{
    ensureFFT();
    auto& st = proc.apvts.state;
    feed (proc.fifoPre, pre);
    feed (proc.fifoPost, post);
    feed (proc.fifoSC, sc);
    const bool freeze = st.getProperty ("anaFreeze", false);
    if (st.getProperty ("anaPre", true))  analyse (pre, false);
    if (st.getProperty ("anaPost", true)) analyse (post, freeze);
    if (st.getProperty ("anaSC", false))  analyse (sc, false);
    if (! freeze) std::fill (post.frozen.begin(), post.frozen.end(), -150.0f);
    repaint();
}

// ---------------------------------------------------------------- paint
static juce::String fmtFreq (float f)
{
    if (f >= 1000.0f) return juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz";
    return juce::String (juce::roundToInt (f)) + " Hz";
}

void BandDisplay::paint (juce::Graphics& g)
{
    using namespace juce;
    auto bounds = getLocalBounds().toFloat();

    ColourGradient bg (Pal::dispTop, 0, 0, Pal::dispBot, 0, bounds.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (Pal::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (bounds.reduced (1.0f).toNearestInt());

    // grid
    g.setFont (FontOptions (11.0f));
    for (float f : { 20.f, 30.f, 40.f, 50.f, 60.f, 70.f, 80.f, 90.f, 100.f, 200.f, 300.f, 400.f, 500.f, 600.f, 700.f, 800.f, 900.f,
                     1000.f, 2000.f, 3000.f, 4000.f, 5000.f, 6000.f, 7000.f, 8000.f, 9000.f, 10000.f, 20000.f })
    {
        const float x = freqToX (f);
        const bool major = (f == 50.f || f == 100.f || f == 200.f || f == 500.f || f == 1000.f || f == 2000.f || f == 5000.f || f == 10000.f);
        g.setColour (Pal::grid.withAlpha (major ? 0.75f : 0.35f));
        g.drawVerticalLine (roundToInt (x), plot.getY(), plot.getBottom());
        if (major)
        {
            g.setColour (Pal::dim);
            g.drawText (f >= 1000.f ? String (roundToInt (f / 1000.f)) + "k" : String (roundToInt (f)),
                        Rectangle<float> (x - 20, plot.getBottom() + 2, 40, 14), Justification::centred);
        }
    }
    const float R = dispRange();
    const float step = R <= 3 ? 1.f : (R <= 6 ? 2.f : (R <= 12 ? 3.f : 10.f));
    for (float d = -R; d <= R + 0.01f; d += step)
    {
        const float y = gainToY (d);
        g.setColour (d == 0.0f ? Pal::navy2.withAlpha (0.35f) : Pal::grid.withAlpha (0.45f));
        g.drawHorizontalLine (roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (Pal::dim);
        g.drawText ((d > 0 ? "+" : "") + String (roundToInt (d)), Rectangle<float> (plot.getRight() + 3, y - 7, 30, 14), Justification::centredLeft);
    }

    // analyzer
    auto& st = proc.apvts.state;
    auto anaPath = [this] (const std::vector<float>& v, bool closed)
    {
        Path p;
        for (int c = 0; c < numCols && c < (int) v.size(); ++c)
        {
            const float x = plot.getX() + c * 2.0f;
            const float y = jlimit (plot.getY() - 2.0f, plot.getBottom() + 2.0f, anaToY (v[(size_t) c]));
            if (c == 0) { if (closed) { p.startNewSubPath (x, plot.getBottom() + 2); p.lineTo (x, y); } else p.startNewSubPath (x, y); }
            else p.lineTo (x, y);
        }
        if (closed) { p.lineTo (plot.getX() + (numCols - 1) * 2.0f, plot.getBottom() + 2); p.closeSubPath(); }
        return p;
    };
    if (st.getProperty ("anaPre", true))
    {
        auto p = anaPath (pre.disp, true);
        g.setColour (Pal::navy.withAlpha (0.07f)); g.fillPath (p);
        g.setColour (Pal::navy.withAlpha (0.14f)); g.strokePath (p, PathStrokeType (1.0f));
    }
    if (st.getProperty ("anaSC", false))
    {
        g.setColour (Colour (0xffd9781c).withAlpha (0.55f));
        g.strokePath (anaPath (sc.disp, false), PathStrokeType (1.0f));
    }
    if (st.getProperty ("anaPost", true))
    {
        g.setColour (Pal::navy2.withAlpha (0.45f));
        g.strokePath (anaPath (post.disp, false), PathStrokeType (1.2f));
        if (st.getProperty ("anaFreeze", false))
        {
            g.setColour (Pal::accent.withAlpha (0.65f));
            g.strokePath (anaPath (post.frozen, false), PathStrokeType (1.0f));
        }
    }

    // bands
    std::vector<int> drawOrder;
    for (int b = 0; b < kNumBands; ++b) if (isActive (b) && b != selected) drawOrder.push_back (b);
    if (selected >= 0 && isActive (selected)) drawOrder.push_back (selected);

    std::vector<float> sumStatic ((size_t) numCols, 0.0f), sumDyn ((size_t) numCols, 0.0f);
    const float mixS = proc.apvts.getRawParameterValue (ID::mix)->load() / 100.0f;
    bool anyBand = false;
    const bool lin = proc.apvts.getRawParameterValue (ID::procMode)->load() > 0.5f;

    for (int b : drawOrder)
    {
        anyBand = true;
        const auto col = Pal::band (b);
        const bool sel = b == selected, hov = b == hover.band;
        const bool byp = bp (b, Bypass) > 0.5f, mute = bp (b, Mute) > 0.5f;
        const float gain = bp (b, Gain), rng = bp (b, SMB::Range), dyn = proc.meters[(size_t) b].dynDb.load();

        const float dS = byp ? 0.0f : jlimit (-60.0f, 36.0f, gain * mixS);
        const float dR = byp ? 0.0f : jlimit (-60.0f, 36.0f, (gain + rng) * mixS);
        const float dD = mute ? -60.0f : (byp ? 0.0f : jlimit (-60.0f, 36.0f, (gain + dyn) * mixS));
        const float lo = bp (b, Lo), hi = bp (b, Hi);
        const int loO = slopeToOrder ((int) bp (b, LoSlope)), hiO = slopeToOrder ((int) bp (b, HiSlope));

        std::vector<Point<float>> ps, pr, pd;
        ps.reserve ((size_t) numCols); pr.reserve ((size_t) numCols); pd.reserve ((size_t) numCols);
        for (int c = 0; c < numCols; ++c)
        {
            const float x = plot.getX() + c * 2.0f;
            const float f = xToFreq (x);
            if (lin)
            {
                const float m = magAt (b, f);
                auto cv = [m] (float d) { return 20.0f * std::log10 (jmax (1.0e-4f, 1.0f + (Decibels::decibelsToGain (d, -59.9f) - 1.0f) * m)); };
                ps.push_back ({ x, gainToY (cv (dS)) });
                pr.push_back ({ x, gainToY (cv (dR)) });
                pd.push_back ({ x, gainToY (cv (dD)) });
                sumStatic[(size_t) c] += (Decibels::decibelsToGain (dS, -59.9f) - 1.0f) * m;
                sumDyn[(size_t) c]    += (Decibels::decibelsToGain (dD, -59.9f) - 1.0f) * m;
            }
            else
            {
                const float s = bandDynDb (f, lo, loO, hi, hiO, dS);
                const float d = bandDynDb (f, lo, loO, hi, hiO, dD);
                ps.push_back ({ x, gainToY (s) });
                pr.push_back ({ x, gainToY (bandDynDb (f, lo, loO, hi, hiO, dR)) });
                pd.push_back ({ x, gainToY (d) });
                sumStatic[(size_t) c] += s;
                sumDyn[(size_t) c]    += d;
            }
        }
        auto fillBetween = [] (const std::vector<Point<float>>& a, const std::vector<Point<float>>& bb)
        {
            Path p;
            if (a.empty()) return p;
            p.startNewSubPath (a.front());
            for (size_t i = 1; i < a.size(); ++i) p.lineTo (a[i]);
            for (size_t i = bb.size(); i-- > 0;) p.lineTo (bb[i]);
            p.closeSubPath();
            return p;
        };
        g.setColour (col.withAlpha (sel ? 0.16f : 0.09f));
        g.fillPath (fillBetween (ps, pr));
        g.setColour (col.withAlpha (sel ? 0.55f : 0.38f));
        g.fillPath (fillBetween (ps, pd));

        Path staticLine;
        staticLine.startNewSubPath (ps.front());
        for (size_t i = 1; i < ps.size(); ++i) staticLine.lineTo (ps[i]);
        g.setColour (col.withAlpha (byp ? 0.35f : (sel ? 1.0f : 0.75f)));
        g.strokePath (staticLine, PathStrokeType (sel ? 1.8f : 1.2f));

        // range line (dashed)
        if (sel || hov)
        {
            Path rl; rl.startNewSubPath (pr.front());
            for (size_t i = 1; i < pr.size(); ++i) rl.lineTo (pr[i]);
            Path dashed; const float dl[] = { 4.0f, 3.0f };
            PathStrokeType (1.0f).createDashedStroke (dashed, rl, dl, 2);
            g.setColour (col.withAlpha (0.8f)); g.fillPath (dashed);

            const auto rp = rangePos (b);
            g.setColour (col); g.fillRoundedRectangle (rp.x - 12, rp.y - 3, 24, 6, 3);
            g.setColour (Colours::white.withAlpha (0.8f)); g.drawRoundedRectangle (rp.x - 12, rp.y - 3, 24, 6, 3, 1.0f);
        }

        // edges
        for (int ed = 0; ed < 2; ++ed)
        {
            const float f = bp (b, ed == 0 ? Lo : Hi);
            if (ed == 0 ? loIsOpen (f) : hiIsOpen (f)) continue;
            const float x = freqToX (f);
            const bool edgeHover = hover.band == b && hover.what == (ed == 0 ? Hit::LoEdge : Hit::HiEdge);
            if (sel || edgeHover)
            {
                g.setColour (col.withAlpha (edgeHover ? 0.8f : 0.4f));
                g.drawLine (x, plot.getY(), x, plot.getBottom(), edgeHover ? 1.6f : 1.0f);
            }
            g.setColour (col.withAlpha (sel ? 0.95f : 0.55f));
            g.fillRoundedRectangle (x - 3.0f, plot.getBottom() - 22.0f, 6.0f, 18.0f, 3.0f);
        }
    }

    // overall curves
    if (anyBand)
    {
        Path ps, pd;
        for (int c = 0; c < numCols; ++c)
        {
            const float x = plot.getX() + c * 2.0f;
            const float s = lin ? 20.0f * std::log10 (jmax (1.0e-4f, 1.0f + sumStatic[(size_t) c])) : sumStatic[(size_t) c];
            const float d = lin ? 20.0f * std::log10 (jmax (1.0e-4f, 1.0f + sumDyn[(size_t) c]))    : sumDyn[(size_t) c];
            if (c == 0) { ps.startNewSubPath (x, gainToY (s)); pd.startNewSubPath (x, gainToY (d)); }
            else        { ps.lineTo (x, gainToY (s)); pd.lineTo (x, gainToY (d)); }
        }
        g.setColour (Pal::navy.withAlpha (0.35f)); g.strokePath (ps, PathStrokeType (1.2f));
        g.setColour (Pal::navy);                   g.strokePath (pd, PathStrokeType (2.0f));
    }

    // internal sidechain indicator for the selected band
    if (selected >= 0 && isActive (selected) && (int) bp (selected, Trig) != 0)
    {
        float dLo, dHi; int o1, o2;
        const int src = proc.detectorRange (selected, dLo, dHi, o1, o2);
        if (src != selected)
        {
            const auto col = src >= 0 ? Pal::band (src) : Pal::navy2;
            const float x1 = freqToX (dLo), x2 = freqToX (dHi);
            auto strip = Rectangle<float> (x1, plot.getY() + 3.0f, jmax (4.0f, x2 - x1), 15.0f);
            g.setColour (col.withAlpha (0.18f));
            g.fillRect (Rectangle<float> (x1, plot.getY(), jmax (4.0f, x2 - x1), plot.getHeight()));
            g.setColour (col.withAlpha (0.85f));
            g.fillRoundedRectangle (strip, 4.0f);
            g.setColour (Colours::white);
            g.setFont (FontOptions (10.5f, Font::bold));
            const String txt = "SC " + String (selected + 1) + String (CharPointer_UTF8 (" \xe2\x86\x90 ")) + (src >= 0 ? "Banda " + String (src + 1) : String ("faixa livre"));
            g.drawText (txt, strip.withWidth (jmax (strip.getWidth(), 120.0f)), Justification::centred);
        }
    }

    // dots
    for (int b : drawOrder)
    {
        const auto col = Pal::band (b);
        const auto p = dotPos (b);
        const bool sel = b == selected, hov = hover.band == b && hover.what == Hit::Dot;
        const float r = sel ? 7.5f : 6.0f;
        g.setColour (Colours::white);
        g.fillEllipse (p.x - r - 1.5f, p.y - r - 1.5f, (r + 1.5f) * 2, (r + 1.5f) * 2);
        g.setColour (col);
        g.fillEllipse (p.x - r, p.y - r, r * 2, r * 2);
        g.setColour (Colours::white);
        g.setFont (FontOptions (10.0f, Font::bold));
        g.drawText (String (b + 1), Rectangle<float> (p.x - r, p.y - r, r * 2, r * 2), Justification::centred);

        if (sel || hov)
        {
            const String txt = fmtFreq (bandCentre (bp (b, Lo), bp (b, Hi))) + "   " + (bp (b, Gain) >= 0 ? "+" : "") + String (bp (b, Gain), 1) + " dB";
            auto tb = Rectangle<float> (p.x - 60, p.y - 34, 120, 18);
            if (tb.getY() < plot.getY()) tb.setY (p.y + 14);
            g.setColour (Pal::panel.withAlpha (0.92f)); g.fillRoundedRectangle (tb, 4);
            g.setColour (col);                          g.drawRoundedRectangle (tb, 4, 1);
            g.setColour (Pal::navy);
            g.setFont (FontOptions (11.0f));
            g.drawText (txt, tb, Justification::centred);
        }
    }

    if (! anyBand)
    {
        g.setColour (Pal::navy.withAlpha (0.55f));
        g.setFont (FontOptions (15.0f));
        g.drawText (CharPointer_UTF8 ("Duplo clique no gr\xc3\xa1" "fico para criar uma banda"), plot, Justification::centred);
    }
}
