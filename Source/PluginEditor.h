#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/SilverLookAndFeel.h"
#include "UI/BandDisplay.h"
#include "UI/BandPanel.h"

class SilverMBAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit SilverMBAudioProcessorEditor (SilverMBAudioProcessor&);
    ~SilverMBAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void layoutMain();
    void setStateProp (const juce::Identifier& id, const juce::var& v) { proc.apvts.state.setProperty (id, v, nullptr); }
    void refreshToggles();

    SilverMBAudioProcessor& proc;
    SilverLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    BandDisplay display;
    BandPanel panel;

    // top bar
    juce::TextButton undoBtn { "Undo" }, redoBtn { "Redo" }, aBtn { "A" }, bBtn { "B" }, copyBtn { "A>B" }, expertBtn { "Expert" };
    juce::Label latencyLbl;

    // bottom bar
    juce::ComboBox modeBox, resBox, speedBox, scaleBox;
    juce::TextButton laBtn { "Lookahead" }, preBtn { "Pre" }, postBtn { "Post" }, scBtn { "SC" }, freezeBtn { "Freeze" };
    Knob inKnob { "INPUT", true }, mixKnob { "MIX" }, outKnob { "OUTPUT", true };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> laAtt;

    juce::Rectangle<int> topBar, bottomBar, meterArea;
    float inL = 0, inR = 0, outL = 0, outR = 0;
    float holdOut = -100.0f; int holdCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SilverMBAudioProcessorEditor)
};
