#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "SixLookAndFeel.h"

namespace six
{
// Every widget shows the AUDIBLE value (incl. morph) and writes the host parameter with a
// begin / perform / end gesture so Ableton Live can record it.
struct Refreshable { virtual ~Refreshable() = default; virtual void refresh() = 0; };

class ControlSlider : public juce::Slider, public Refreshable
{
public:
    ControlSlider (SIXAudioProcessor& p, int controlIndex, SliderStyle style);
    void refresh() override;
    void setControl (int controlIndex);
    int getControl() const noexcept { return control; }
    std::function<void (const juce::String&)> onRejected;

private:
    void startedDragging() override;
    void stoppedDragging() override;
    void valueChanged() override;
    juce::String getTextFromValue (double v) override;
    double getValueFromText (const juce::String& t) override;
    bool allowChange (float newValue);

    SIXAudioProcessor& proc;
    int control = 0;
    juce::RangedAudioParameter* param = nullptr;
    bool inGesture = false, updating = false;
};

// Two-state pill ("Mono"/"Stereo", "IN") bound to a toggle control
class ControlPill : public juce::Component, public Refreshable, public juce::SettableTooltipClient
{
public:
    ControlPill (SIXAudioProcessor& p, int controlIndex, juce::String offText, juce::String onText);
    void refresh() override { repaint(); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void setControl (int c) { control = c; repaint(); }
    bool filled = true;          // filled when "on"
    juce::Colour onColour;       // default: teal
private:
    SIXAudioProcessor& proc;
    int control;
    juce::String off, on;
};

// "Pre | Post" segmented switch
class PrePostSwitch : public juce::Component, public Refreshable
{
public:
    PrePostSwitch (SIXAudioProcessor& p, int controlIndex) : proc (p), control (controlIndex) {}
    void refresh() override { repaint(); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void setControl (int c) { control = c; repaint(); }
private:
    SIXAudioProcessor& proc;
    int control;
};

class OutSelector : public juce::ComboBox, public Refreshable
{
public:
    OutSelector (SIXAudioProcessor& p, int channel);
    void refresh() override;
    std::function<void (const juce::String&)> onRejected;
private:
    SIXAudioProcessor& proc;
    int ch;
    bool updating = false;
};

class LevelMeter : public juce::Component, public Refreshable
{
public:
    LevelMeter (SIXAudioProcessor& p, int channel) : proc (p), ch (channel) {}
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    SIXAudioProcessor& proc;
    int ch;
    float levelL = 0, levelR = 0;
};

// Makes a value label typeable: click it, type a value ("-21", "L20", "-inf"), press Enter.
// The text is read by the parameter itself and sent to the host as one gesture.
void makeValueTypeable (juce::Label& label, SIXAudioProcessor& p, std::function<int()> controlIndex,
                        std::function<void (const juce::String&)> onRejected = {});

// Draws a rounded panel like the reference
void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill, juce::Colour outline, float radius = 10.0f);
}
