#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Paleta "SILVER" (mesma família do RefPro SILVER: prata / cinza claro com fontes azul-marinho)
namespace Pal
{
    const juce::Colour bgTop     { 0xffeceff3 };
    const juce::Colour bgBot     { 0xffc8ced6 };
    const juce::Colour panel     { 0xfff4f6f8 };
    const juce::Colour panelEdge { 0xff9ea7b3 };
    const juce::Colour navy      { 0xff14284d };
    const juce::Colour navy2     { 0xff2a4676 };
    const juce::Colour accent    { 0xff3b6cb6 };
    const juce::Colour dim       { 0xff5b6a83 };
    const juce::Colour grid      { 0xffb3bbc6 };
    const juce::Colour dispTop   { 0xffe4e8ed };
    const juce::Colour dispBot   { 0xffc9d0d9 };
    const juce::Colour metal1    { 0xfffbfcfd };
    const juce::Colour metal2    { 0xffc2c9d2 };

    inline juce::Colour band (int i)
    {
        static const juce::uint32 c[] = { 0xff2f6fd6, 0xff139e93, 0xff8a4fd0, 0xffcf467c, 0xffdc861c, 0xff4a9a39 };
        return juce::Colour (c[(i % 6 + 6) % 6]);
    }
}

class SilverLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SilverLookAndFeel()
    {
        using namespace juce;
        setColour (ResizableWindow::backgroundColourId, Pal::bgTop);
        setColour (Label::textColourId, Pal::navy);
        setColour (Slider::textBoxTextColourId, Pal::navy);
        setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
        setColour (Slider::textBoxHighlightColourId, Pal::accent.withAlpha (0.3f));
        setColour (Slider::backgroundColourId, Pal::grid.withAlpha (0.6f));
        setColour (Slider::trackColourId, Pal::navy2);
        setColour (TextEditor::textColourId, Pal::navy);
        setColour (TextEditor::backgroundColourId, Pal::panel);
        setColour (TextEditor::highlightColourId, Pal::accent.withAlpha (0.3f));
        setColour (CaretComponent::caretColourId, Pal::navy);
        setColour (ComboBox::backgroundColourId, Pal::panel);
        setColour (ComboBox::textColourId, Pal::navy);
        setColour (ComboBox::outlineColourId, Pal::panelEdge);
        setColour (ComboBox::arrowColourId, Pal::navy2);
        setColour (PopupMenu::backgroundColourId, Pal::panel);
        setColour (PopupMenu::textColourId, Pal::navy);
        setColour (PopupMenu::highlightedBackgroundColourId, Pal::accent.withAlpha (0.85f));
        setColour (PopupMenu::highlightedTextColourId, Colours::white);
        setColour (TextButton::buttonColourId, Pal::panel);
        setColour (TextButton::buttonOnColourId, Pal::navy2);
        setColour (TextButton::textColourOffId, Pal::navy);
        setColour (TextButton::textColourOnId, Colours::white);
        setColour (TooltipWindow::backgroundColourId, Pal::navy);
        setColour (TooltipWindow::textColourId, Colours::white);
    }

    juce::Font getLabelFont (juce::Label&) override { return juce::Font (juce::FontOptions (12.5f)); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return juce::Font (juce::FontOptions (juce::jmin (13.0f, h * 0.55f), juce::Font::bold)); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (12.5f)); }
    juce::Font getPopupMenuFont() override { return juce::Font (juce::FontOptions (13.5f)); }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float a0, float a1, juce::Slider& s) override
    {
        using namespace juce;
        const auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
        const float r  = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto  c  = bounds.getCentre();
        const float lw = jmax (2.5f, r * 0.11f);
        const float ar = r - lw * 0.5f;
        const float ang = a0 + pos * (a1 - a0);
        const auto colour = s.getProperties().contains ("colour") ? Colour ((uint32) (int64) s.getProperties()["colour"]) : Pal::navy2;

        Path track; track.addCentredArc (c.x, c.y, ar, ar, 0, a0, a1, true);
        g.setColour (Pal::grid.withAlpha (0.75f));
        g.strokePath (track, PathStrokeType (lw, PathStrokeType::curved, PathStrokeType::rounded));

        // detector level ring (threshold knob)
        if (s.getProperties().contains ("meter"))
        {
            const float m = jlimit (0.0f, 1.0f, (float) (double) s.getProperties()["meter"]);
            if (m > 0.001f)
            {
                Path mp; mp.addCentredArc (c.x, c.y, ar, ar, 0, a0, a0 + m * (a1 - a0), true);
                g.setColour (Pal::accent.withAlpha (0.35f));
                g.strokePath (mp, PathStrokeType (lw, PathStrokeType::curved, PathStrokeType::rounded));
            }
        }

        const bool bip = s.getProperties()["bipolar"];
        const float from = bip ? (a0 + a1) * 0.5f : a0;
        Path val; val.addCentredArc (c.x, c.y, ar, ar, 0, jmin (from, ang), jmax (from, ang), true);
        g.setColour (s.isEnabled() ? colour : Pal::grid);
        g.strokePath (val, PathStrokeType (lw, PathStrokeType::curved, PathStrokeType::rounded));

        const float kr = r - lw - 3.0f;
        ColourGradient grad (Pal::metal1, c.x - kr * 0.5f, c.y - kr * 0.7f, Pal::metal2, c.x + kr * 0.4f, c.y + kr, true);
        g.setGradientFill (grad);
        g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
        g.setColour (Pal::panelEdge);
        g.drawEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2, 1.0f);
        g.setColour (Colours::white.withAlpha (0.8f));
        g.drawEllipse (c.x - kr + 1.5f, c.y - kr + 1.5f, kr * 2 - 3, kr * 2 - 3, 0.8f);

        Path p;
        p.addRoundedRectangle (-1.6f, -kr + 3.0f, 3.2f, kr * 0.5f, 1.5f);
        g.setColour (Pal::navy);
        g.fillPath (p, AffineTransform::rotation (ang).translated (c.x, c.y));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearBar) { LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s); return; }
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        g.setColour (Pal::panel);           g.fillRoundedRectangle (r, 3.0f);
        g.setColour (Pal::accent.withAlpha (0.22f));
        g.fillRoundedRectangle (r.withRight (pos), 3.0f);
        g.setColour (Pal::panelEdge);       g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        juce::Colour onCol = b.getProperties().contains ("onColour") ? juce::Colour ((juce::uint32) (juce::int64) b.getProperties()["onColour"]) : Pal::navy2;
        if (on)
        {
            g.setColour (onCol);
            g.fillRoundedRectangle (r, 4.0f);
        }
        else
        {
            juce::ColourGradient grad (Pal::metal1, 0, r.getY(), Pal::metal2.brighter (0.15f), 0, r.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (r, 4.0f);
        }
        if (over || down) { g.setColour (Pal::accent.withAlpha (down ? 0.25f : 0.12f)); g.fillRoundedRectangle (r, 4.0f); }
        g.setColour (on ? onCol.darker (0.3f) : Pal::panelEdge);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        juce::ColourGradient grad (Pal::metal1, 0, 0, Pal::metal2.brighter (0.2f), 0, (float) h, false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (box.hasKeyboardFocus (true) ? Pal::accent : Pal::panelEdge);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        juce::Path arrow;
        const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
        arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
        g.setColour (Pal::navy2);
        g.fillPath (arrow);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (4, 1, box.getWidth() - 22, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }
};
