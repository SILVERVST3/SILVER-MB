#include "PluginEditor.h"

using namespace SMB;

SilverMBAudioProcessorEditor::SilverMBAudioProcessorEditor (SilverMBAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p), panel (p)
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);

    addAndMakeVisible (display);
    addAndMakeVisible (panel);

    display.onSelectionChanged = [this] (int b) { panel.setBand (b); };
    panel.onDeleteBand = [this] (int b) { display.deleteBand (b); };

    // ---- top bar ----
    for (auto* b : { &undoBtn, &redoBtn, &aBtn, &bBtn, &copyBtn, &expertBtn }) addAndMakeVisible (b);
    undoBtn.onClick = [this] { proc.undoManager.undo(); };
    redoBtn.onClick = [this] { proc.undoManager.redo(); };
    aBtn.onClick    = [this] { proc.selectAB (0); refreshToggles(); };
    bBtn.onClick    = [this] { proc.selectAB (1); refreshToggles(); };
    copyBtn.onClick = [this] { proc.copyCurrentToOther(); };
    copyBtn.setTooltip ("Copiar o estado atual para o outro slot (A/B)");
    expertBtn.setClickingTogglesState (true);
    expertBtn.setToggleState ((bool) proc.apvts.state.getProperty ("expert", false), juce::dontSendNotification);
    expertBtn.onClick = [this]
    {
        setStateProp ("expert", expertBtn.getToggleState());
        panel.setExpert (expertBtn.getToggleState());
        layoutMain();
    };
    panel.setExpert (expertBtn.getToggleState());

    latencyLbl.setJustificationType (juce::Justification::centredRight);
    latencyLbl.setColour (juce::Label::textColourId, Pal::dim);
    latencyLbl.setFont (juce::FontOptions (11.5f));
    addAndMakeVisible (latencyLbl);

    // ---- bottom bar ----
    modeBox.addItemList ({ "Dynamic Phase", "Linear Phase" }, 1);
    addAndMakeVisible (modeBox);
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ID::procMode, modeBox);

    laBtn.setClickingTogglesState (true);
    addAndMakeVisible (laBtn);
    laAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ID::laOn, laBtn);
    laBtn.setTooltip ("Lookahead global (20 ms de latencia)");

    for (auto* b : { &preBtn, &postBtn, &scBtn, &freezeBtn })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (b);
    }
    preBtn.onClick    = [this] { setStateProp ("anaPre",    preBtn.getToggleState()); };
    postBtn.onClick   = [this] { setStateProp ("anaPost",   postBtn.getToggleState()); };
    scBtn.onClick     = [this] { setStateProp ("anaSC",     scBtn.getToggleState()); };
    freezeBtn.onClick = [this] { setStateProp ("anaFreeze", freezeBtn.getToggleState()); };

    resBox.addItemList ({ "Res: 2048", "Res: 4096", "Res: 8192" }, 1);
    speedBox.addItemList ({ "Fast", "Medium", "Slow" }, 1);
    const juce::String pm (juce::CharPointer_UTF8 ("\xc2\xb1"));
    scaleBox.addItemList ({ pm + "3 dB", pm + "6 dB", pm + "12 dB", pm + "30 dB" }, 1);
    for (auto* c : { &resBox, &speedBox, &scaleBox }) addAndMakeVisible (c);
    resBox.onChange   = [this] { setStateProp ("anaRes",    resBox.getSelectedItemIndex()); };
    speedBox.onChange = [this] { setStateProp ("anaSpeed",  speedBox.getSelectedItemIndex()); };
    scaleBox.onChange = [this] { setStateProp ("dispRange", scaleBox.getSelectedItemIndex()); };

    for (auto* k : { &inKnob, &mixKnob, &outKnob })
    {
        k->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 16);
        addAndMakeVisible (k);
    }
    inKnob.attach (proc.apvts, ID::inGain);
    mixKnob.attach (proc.apvts, ID::mix);
    outKnob.attach (proc.apvts, ID::outGain);

    refreshToggles();
    addMouseListener (this, true);

    setResizable (true, true);
    setResizeLimits (1060, 680, 2000, 1300);
    setSize (1100, 740);
    startTimerHz (30);
}

SilverMBAudioProcessorEditor::~SilverMBAudioProcessorEditor()
{
    stopTimer();
    removeMouseListener (this);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void SilverMBAudioProcessorEditor::refreshToggles()
{
    auto& st = proc.apvts.state;
    preBtn.setToggleState    ((bool) st.getProperty ("anaPre", true),     juce::dontSendNotification);
    postBtn.setToggleState   ((bool) st.getProperty ("anaPost", true),    juce::dontSendNotification);
    scBtn.setToggleState     ((bool) st.getProperty ("anaSC", false),     juce::dontSendNotification);
    freezeBtn.setToggleState ((bool) st.getProperty ("anaFreeze", false), juce::dontSendNotification);
    resBox.setSelectedItemIndex   ((int) st.getProperty ("anaRes", 1),    juce::dontSendNotification);
    speedBox.setSelectedItemIndex ((int) st.getProperty ("anaSpeed", 1),  juce::dontSendNotification);
    scaleBox.setSelectedItemIndex ((int) st.getProperty ("dispRange", 2), juce::dontSendNotification);
    aBtn.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification);
    bBtn.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
}

void SilverMBAudioProcessorEditor::mouseDown (const juce::MouseEvent&)
{
    proc.undoManager.beginNewTransaction();
}

void SilverMBAudioProcessorEditor::timerCallback()
{
    auto decay = [] (float cur, std::atomic<float>& src)
    {
        const float v = src.exchange (0.0f);
        const float db = juce::Decibels::gainToDecibels (v, -100.0f);
        return juce::jmax (db, cur - 1.2f);
    };
    inL = decay (inL, proc.inPeak[0]);   inR = decay (inR, proc.inPeak[1]);
    outL = decay (outL, proc.outPeak[0]); outR = decay (outR, proc.outPeak[1]);
    const float o = juce::jmax (outL, outR);
    if (o >= holdOut || ++holdCount > 60) { holdOut = o; holdCount = 0; }

    const int lat = proc.getLatencySamples();
    latencyLbl.setText ("Latencia: " + juce::String (lat) + " amostras (" + juce::String (lat * 1000.0 / juce::jmax (1.0, proc.currentSR.load()), 1) + " ms)",
                        juce::dontSendNotification);

    undoBtn.setEnabled (proc.undoManager.canUndo());
    redoBtn.setEnabled (proc.undoManager.canRedo());

    // selected band may have been deleted (undo / automation)
    const int sel = display.getSelected();
    if (sel >= 0 && proc.bandParam (sel, Active) < 0.5f) display.setSelected (-1);

    repaint (meterArea);
}

void SilverMBAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace juce;
    ColourGradient bg (Pal::bgTop, 0, 0, Pal::bgBot, 0, (float) getHeight(), false);
    g.setGradientFill (bg);
    g.fillAll();

    // brushed-metal hint
    g.setColour (Colours::white.withAlpha (0.05f));
    for (int y = 0; y < getHeight(); y += 3) g.drawHorizontalLine (y, 0.0f, (float) getWidth());

    // title
    auto t = topBar.reduced (14, 0);
    g.setColour (Pal::navy);
    g.setFont (FontOptions (24.0f, Font::bold));
    g.drawText ("SILVER-MB", t.removeFromLeft (150), Justification::centredLeft);
    g.setColour (Pal::dim);
    g.setFont (FontOptions (12.5f));
    g.drawText ("Multiband Compressor", t.removeFromLeft (170), Justification::centredLeft);

    g.setColour (Pal::panelEdge.withAlpha (0.7f));
    g.drawHorizontalLine (topBar.getBottom(), 8.0f, (float) getWidth() - 8.0f);
    g.drawHorizontalLine (bottomBar.getY() - 2, 8.0f, (float) getWidth() - 8.0f);

    // labels for bottom groups
    g.setColour (Pal::dim);
    g.setFont (FontOptions (10.5f, Font::bold));
    g.drawText ("PROCESSAMENTO", modeBox.getX(), bottomBar.getY() + 2, 200, 12, Justification::centredLeft);
    g.drawText ("ANALISADOR", preBtn.getX(), bottomBar.getY() + 2, 200, 12, Justification::centredLeft);
    g.drawText ("DISPLAY", scaleBox.getX(), bottomBar.getY() + 2, 100, 12, Justification::centredLeft);

    // meters
    auto m = meterArea.toFloat();
    g.setColour (Pal::panel.withAlpha (0.8f));
    g.fillRoundedRectangle (m, 5.0f);
    g.setColour (Pal::panelEdge);
    g.drawRoundedRectangle (m.reduced (0.5f), 5.0f, 1.0f);
    auto inner = m.reduced (5.0f).withTrimmedTop (16.0f).withTrimmedBottom (4.0f);
    auto dbToY = [inner] (float db) { return inner.getBottom() - (jlimit (-60.0f, 6.0f, db) + 60.0f) / 66.0f * inner.getHeight(); };
    const float bw = (inner.getWidth() - 6.0f) / 4.0f;
    const float vals[] = { inL, inR, outL, outR };
    for (int i = 0; i < 4; ++i)
    {
        const float x = inner.getX() + i * bw + (i >= 2 ? 6.0f : 0.0f);
        auto bar = Rectangle<float> (x, inner.getY(), bw - 2.0f, inner.getHeight());
        g.setColour (Pal::grid.withAlpha (0.45f)); g.fillRect (bar);
        const float y = dbToY (vals[i]);
        ColourGradient mg (Pal::navy2, 0, inner.getBottom(), Pal::accent.brighter (0.3f), 0, inner.getY(), false);
        if (vals[i] > 0.0f) mg = ColourGradient (Colour (0xffc0392b), 0, inner.getY(), Pal::accent, 0, inner.getBottom(), false);
        g.setGradientFill (mg);
        g.fillRect (bar.withTop (y));
    }
    g.setColour (Pal::navy.withAlpha (0.5f));
    for (float d : { 0.f, -6.f, -12.f, -24.f, -48.f })
        g.drawHorizontalLine (roundToInt (dbToY (d)), inner.getX(), inner.getRight());
    g.setColour (Pal::navy);
    g.setFont (FontOptions (10.0f, Font::bold));
    g.drawText (holdOut > -99.0f ? String (holdOut, 1) : String ("-inf"), m.withHeight (16.0f), Justification::centred);
}

void SilverMBAudioProcessorEditor::resized()
{
    layoutMain();
}

void SilverMBAudioProcessorEditor::layoutMain()
{
    auto r = getLocalBounds();
    topBar = r.removeFromTop (46);
    bottomBar = r.removeFromBottom (78);

    {
        auto t = topBar.reduced (12, 9);
        expertBtn.setBounds (t.removeFromRight (74)); t.removeFromRight (16);
        copyBtn.setBounds (t.removeFromRight (44)); t.removeFromRight (4);
        bBtn.setBounds (t.removeFromRight (30));    t.removeFromRight (2);
        aBtn.setBounds (t.removeFromRight (30));    t.removeFromRight (16);
        redoBtn.setBounds (t.removeFromRight (52)); t.removeFromRight (4);
        undoBtn.setBounds (t.removeFromRight (52)); t.removeFromRight (16);
        latencyLbl.setBounds (t.removeFromRight (260));
    }

    auto main = r.reduced (10, 6);
    const int ph = BandPanel::heightFor (expertBtn.getToggleState());
    panel.setBounds (main.removeFromBottom (ph));
    main.removeFromBottom (8);
    meterArea = main.removeFromRight (48);
    main.removeFromRight (8);
    display.setBounds (main);

    {
        auto b = bottomBar.reduced (12, 0);
        b.removeFromTop (16);
        auto row = b.removeFromTop (28);
        modeBox.setBounds (row.removeFromLeft (130)); row.removeFromLeft (6);
        laBtn.setBounds (row.removeFromLeft (86));   row.removeFromLeft (22);
        preBtn.setBounds (row.removeFromLeft (44));  row.removeFromLeft (3);
        postBtn.setBounds (row.removeFromLeft (48)); row.removeFromLeft (3);
        scBtn.setBounds (row.removeFromLeft (40));   row.removeFromLeft (3);
        freezeBtn.setBounds (row.removeFromLeft (62)); row.removeFromLeft (6);
        resBox.setBounds (row.removeFromLeft (96));  row.removeFromLeft (4);
        speedBox.setBounds (row.removeFromLeft (80)); row.removeFromLeft (22);
        scaleBox.setBounds (row.removeFromLeft (80));

        auto k = bottomBar.reduced (8, 3).withTrimmedBottom (6);
        outKnob.setBounds (k.removeFromRight (104));
        mixKnob.setBounds (k.removeFromRight (104));
        inKnob.setBounds (k.removeFromRight (104));
    }
    repaint();
}
