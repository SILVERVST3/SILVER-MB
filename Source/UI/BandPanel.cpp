#include "BandPanel.h"

using namespace SMB;

BandPanel::BandPanel (SilverMBAudioProcessor& p) : proc (p)
{
    for (auto* k : { &thresh, &range, &output, &ratio, &knee, &attack, &release, &look, &link, &fLo, &fHi })
        addAndMakeVisible (k);

    for (auto* s : { &loFreq, &hiFreq })
    {
        s->setSliderStyle (juce::Slider::LinearBar);
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 80, 20);
        s->setColour (juce::Slider::textBoxTextColourId, Pal::navy);
        addAndMakeVisible (s);
    }
    const juce::StringArray slopes { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "30 dB/oct", "36 dB/oct", "42 dB/oct", "48 dB/oct" };
    loSlope.addItemList (slopes, 1);
    hiSlope.addItemList (slopes, 1);
    stMode.addItemList ({ "Stereo", "Mid", "Side" }, 1);
    for (auto* c : { &loSlope, &hiSlope, &stMode }) addAndMakeVisible (c);

    for (auto* l : { &loLbl, &hiLbl })
    {
        l->setFont (juce::FontOptions (11.5f));
        l->setColour (juce::Label::textColourId, Pal::dim);
        addAndMakeVisible (l);
    }
    title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (title);

    emptyLbl.setText (juce::CharPointer_UTF8 ("Selecione uma banda no gr\xc3\xa1" "fico, ou d\xc3\xaa um duplo clique para criar uma nova."), juce::dontSendNotification);
    emptyLbl.setJustificationType (juce::Justification::centred);
    emptyLbl.setColour (juce::Label::textColourId, Pal::dim);
    emptyLbl.setFont (juce::FontOptions (14.0f));
    addAndMakeVisible (emptyLbl);

    expertLbl.setText ("EXPERT  -  Sidechain / Detector", juce::dontSendNotification);
    expertLbl.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    expertLbl.setColour (juce::Label::textColourId, Pal::navy2);
    addAndMakeVisible (expertLbl);

    for (auto* b : { &compBtn, &expBtn })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }
    compBtn.onClick = [this] { setParam (Mode, 0.0f); };
    expBtn.onClick  = [this] { setParam (Mode, 1.0f); };

    for (auto* b : { &solo, &mute, &byp, &freeBtn, &extBtn, &audBtn })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (b);
    }
    solo.getProperties().set ("onColour", (juce::int64) juce::Colour (0xffd9a21c).getARGB());
    mute.getProperties().set ("onColour", (juce::int64) juce::Colour (0xffc0392b).getARGB());
    audBtn.getProperties().set ("onColour", (juce::int64) juce::Colour (0xffd9781c).getARGB());
    solo.setTooltip ("Solo da banda");
    mute.setTooltip ("Mute da banda");
    del.setTooltip ("Excluir banda");
    freeBtn.setTooltip ("Detector em faixa livre (Free) em vez da propria banda");
    extBtn.setTooltip ("Usar o sidechain externo (canais 3/4 no REAPER)");
    audBtn.setTooltip ("Ouvir o sinal do detector");
    addAndMakeVisible (del);
    del.onClick = [this] { if (onDeleteBand && band >= 0) onDeleteBand (band); };

    setBand (-1);
    startTimerHz (25);
}

BandPanel::~BandPanel() { stopTimer(); }

void BandPanel::setParam (int p, float v)
{
    if (band < 0) return;
    if (auto* prm = proc.apvts.getParameter (bandID (band, p)))
    {
        proc.undoManager.beginNewTransaction();
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
        prm->endChangeGesture();
    }
}

void BandPanel::setBand (int b)
{
    band = b;
    auto& s = proc.apvts;

    loAtt.reset(); hiAtt.reset(); loSAtt.reset(); hiSAtt.reset(); stAtt.reset();
    soloAtt.reset(); muteAtt.reset(); bypAtt.reset(); freeAtt.reset(); extAtt.reset(); audAtt.reset();

    if (b >= 0)
    {
        thresh.attach (s, bandID (b, Thresh));
        range.attach (s, bandID (b, Range));
        output.attach (s, bandID (b, Gain));
        ratio.attach (s, bandID (b, Ratio));
        knee.attach (s, bandID (b, Knee));
        attack.attach (s, bandID (b, Attack));
        release.attach (s, bandID (b, Release));
        look.attach (s, bandID (b, LA));
        link.attach (s, bandID (b, Link));
        fLo.attach (s, bandID (b, FLo));
        fHi.attach (s, bandID (b, FHi));
        loAtt  = std::make_unique<SA> (s, bandID (b, Lo), loFreq);
        hiAtt  = std::make_unique<SA> (s, bandID (b, Hi), hiFreq);
        loSAtt = std::make_unique<CA> (s, bandID (b, LoSlope), loSlope);
        hiSAtt = std::make_unique<CA> (s, bandID (b, HiSlope), hiSlope);
        stAtt  = std::make_unique<CA> (s, bandID (b, StMode), stMode);
        soloAtt = std::make_unique<BA> (s, bandID (b, Solo), solo);
        muteAtt = std::make_unique<BA> (s, bandID (b, Mute), mute);
        bypAtt  = std::make_unique<BA> (s, bandID (b, Bypass), byp);
        freeAtt = std::make_unique<BA> (s, bandID (b, Free), freeBtn);
        extAtt  = std::make_unique<BA> (s, bandID (b, Ext), extBtn);
        audAtt  = std::make_unique<BA> (s, bandID (b, Audition), audBtn);

        const auto col = Pal::band (b);
        for (auto* k : { &thresh, &range, &output, &ratio, &knee, &attack, &release, &look, &link, &fLo, &fHi })
            k->setColour (col);
        title.setText ("BANDA " + juce::String (b + 1), juce::dontSendNotification);
        title.setColour (juce::Label::textColourId, col.darker (0.25f));
    }
    updateVisibility();
    resized();
    repaint();
}

void BandPanel::updateVisibility()
{
    const bool has = band >= 0;
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &thresh, &range, &output, &ratio, &knee, &attack, &release, &look, &link,
             &loFreq, &hiFreq, &loSlope, &hiSlope, &stMode, &compBtn, &expBtn, &solo, &mute, &byp, &del,
             &title, &loLbl, &hiLbl })
        c->setVisible (has);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &fLo, &fHi, &freeBtn, &extBtn, &audBtn, &expertLbl })
        c->setVisible (has && expert);
    emptyLbl.setVisible (! has);
}

void BandPanel::timerCallback()
{
    if (band < 0) return;
    const bool expd = proc.bandParam (band, Mode) > 0.5f;
    compBtn.setToggleState (! expd, juce::dontSendNotification);
    expBtn.setToggleState (expd, juce::dontSendNotification);

    // detector level ring on the threshold knob (-60..0 dB)
    const float lvl = proc.meters[(size_t) band].levelDb.load();
    thresh.slider.getProperties().set ("meter", juce::jlimit (0.0, 1.0, (lvl + 60.0) / 60.0));
    thresh.slider.repaint();

    const bool free = proc.bandParam (band, Free) > 0.5f;
    fLo.setEnabled (free); fHi.setEnabled (free);
    extBtn.setButtonText (proc.sidechainConnected.load() ? "Ext SC" : "Ext SC (--)");
}

void BandPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    juce::ColourGradient grad (Pal::panel, 0, 0, Pal::bgBot.brighter (0.25f), 0, r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (band >= 0 ? Pal::band (band).withAlpha (0.8f) : Pal::panelEdge);
    g.drawRoundedRectangle (r, 6.0f, band >= 0 ? 1.4f : 1.0f);

    if (band >= 0)
    {
        g.setColour (Pal::panelEdge.withAlpha (0.6f));
        g.drawHorizontalLine (32, 8.0f, (float) getWidth() - 8.0f);
        if (expert) g.drawHorizontalLine (getHeight() - 82, 8.0f, (float) getWidth() - 8.0f);
    }
}

void BandPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 4);
    emptyLbl.setBounds (getLocalBounds());
    if (band < 0) return;

    auto head = r.removeFromTop (26);
    title.setBounds (head.removeFromLeft (92));
    del.setBounds (head.removeFromRight (28)); head.removeFromRight (6);
    byp.setBounds (head.removeFromRight (62));  head.removeFromRight (4);
    mute.setBounds (head.removeFromRight (28)); head.removeFromRight (4);
    solo.setBounds (head.removeFromRight (28)); head.removeFromRight (12);
    stMode.setBounds (head.removeFromRight (84)); head.removeFromRight (10);

    loLbl.setBounds (head.removeFromLeft (64));
    loFreq.setBounds (head.removeFromLeft (110).reduced (0, 2)); head.removeFromLeft (4);
    loSlope.setBounds (head.removeFromLeft (92).reduced (0, 1)); head.removeFromLeft (14);
    hiLbl.setBounds (head.removeFromLeft (66));
    hiFreq.setBounds (head.removeFromLeft (110).reduced (0, 2)); head.removeFromLeft (4);
    hiSlope.setBounds (head.removeFromLeft (92).reduced (0, 1));

    r.removeFromTop (6);
    if (expert)
    {
        auto ex = r.removeFromBottom (76);
        expertLbl.setBounds (ex.removeFromLeft (220).withTrimmedTop (4).removeFromTop (20));
        auto col = ex.removeFromLeft (110).reduced (4, 10);
        freeBtn.setBounds (col.removeFromTop (26));
        fLo.setBounds (ex.removeFromLeft (90));
        fHi.setBounds (ex.removeFromLeft (90));
        ex.removeFromLeft (16);
        auto col2 = ex.removeFromLeft (120).reduced (4, 10);
        extBtn.setBounds (col2.removeFromTop (26));
        auto col3 = ex.removeFromLeft (120).reduced (4, 10);
        audBtn.setBounds (col3.removeFromTop (26));
        r.removeFromBottom (4);
    }

    auto body = r;
    const int big = juce::jmin (112, body.getHeight());
    thresh.setBounds (body.removeFromLeft (big));
    range.setBounds (body.removeFromLeft (big));
    output.setBounds (body.removeFromRight (big));
    body.removeFromRight (8);

    auto modeCol = body.removeFromLeft (96).reduced (8, 0);
    modeCol.removeFromTop (juce::jmax (0, (modeCol.getHeight() - 64) / 2));
    compBtn.setBounds (modeCol.removeFromTop (28)); modeCol.removeFromTop (8);
    expBtn.setBounds (modeCol.removeFromTop (28));

    const int n = 6;
    const int w = body.getWidth() / n;
    auto row = body.withSizeKeepingCentre (body.getWidth(), juce::jmin (body.getHeight(), 96));
    for (auto* k : { &ratio, &knee, &attack, &release, &look, &link })
        k->setBounds (row.removeFromLeft (w));
}
