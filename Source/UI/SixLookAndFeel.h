#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace six
{
namespace col
{
    const juce::Colour bgTop    { 0xff0e1b3d };
    const juce::Colour bgBottom { 0xff09122b };
    const juce::Colour panel    { 0xff111f47 };
    const juce::Colour panelHi  { 0xff16285a };
    const juce::Colour border   { 0xff22346a };
    const juce::Colour teal     { 0xff1fe3f2 };
    const juce::Colour tealDim  { 0xff0f6f7e };
    const juce::Colour selBg    { 0xff0b3b55 };
    const juce::Colour text     { 0xffe8eefc };
    const juce::Colour muted    { 0xff95a1c6 };
    const juce::Colour dim      { 0xff5b6894 };
    const juce::Colour track    { 0xff2a3866 };
    const juce::Colour warn     { 0xffffb35c };
    const juce::Colour purple   { 0xff8d4fe0 };
}

juce::Font sixFont (float height, bool bold = false);

class SixLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SixLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return sixFont (17.0f); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override { return sixFont (15.0f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return sixFont (juce::jmin (17.0f, h * 0.55f)); }
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;
};
}
