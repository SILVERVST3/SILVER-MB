#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "SilverLookAndFeel.h"

// knob = label + rotary slider (+ attachment that can be re-targeted)
struct Knob : public juce::Component
{
    Knob (const juce::String& name, bool bipolar = false)
    {
        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, Pal::navy);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 15);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.getProperties().set ("bipolar", bipolar);
        slider.setColour (juce::Slider::textBoxTextColourId, Pal::navy);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (label);
        addAndMakeVisible (slider);
    }
    void attach (juce::AudioProcessorValueTreeState& s, const juce::String& id)
    {
        att.reset();
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
    }
    void setColour (juce::Colour c) { slider.getProperties().set ("colour", (juce::int64) c.getARGB()); slider.repaint(); }
    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (14));
        slider.setBounds (r);
    }
    juce::Label label;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

class BandPanel : public juce::Component, private juce::Timer
{
public:
    explicit BandPanel (SilverMBAudioProcessor&);
    ~BandPanel() override;

    void setBand (int b);
    void setExpert (bool e) { expert = e; updateVisibility(); resized(); }
    std::function<void (int)> onDeleteBand;

    void paint (juce::Graphics&) override;
    void resized() override;

    static int heightFor (bool expert) { return expert ? 238 : 160; }

private:
    void timerCallback() override;
    void updateVisibility();
    void setParam (int p, float v);

    SilverMBAudioProcessor& proc;
    int band = -1;
    bool expert = false;

    Knob thresh { "THRESHOLD" }, range { "RANGE", true }, output { "OUTPUT", true };
    Knob ratio { "RATIO" }, knee { "KNEE" }, attack { "ATTACK" }, release { "RELEASE" }, look { "LOOKAHEAD" }, link { "STEREO LINK" };
    Knob fLo { "DETECT LOW" }, fHi { "DETECT HIGH" };

    juce::Slider loFreq, hiFreq;
    juce::ComboBox loSlope, hiSlope, stMode;
    juce::TextButton compBtn { "Compress" }, expBtn { "Expand" };
    juce::TextButton solo { "S" }, mute { "M" }, byp { "Bypass" }, del { "X" };
    juce::TextButton freeBtn { "Free" }, extBtn { "Ext SC" }, audBtn { "Audition" };
    juce::Label title, loLbl { {}, "Corte inf." }, hiLbl { {}, "Corte sup." }, emptyLbl, expertLbl;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SA> loAtt, hiAtt;
    std::unique_ptr<CA> loSAtt, hiSAtt, stAtt;
    std::unique_ptr<BA> soloAtt, muteAtt, bypAtt, freeAtt, extAtt, audAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandPanel)
};
