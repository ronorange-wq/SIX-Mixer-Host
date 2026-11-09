#pragma once
#include "Widgets.h"

namespace six
{
class ChannelStrip : public juce::Component, public Refreshable
{
public:
    ChannelStrip (SIXAudioProcessor& p, int channel);
    ~ChannelStrip() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override;
    void mouseDown (const juce::MouseEvent&) override;

    void setSelected (bool s) { selected = s; repaint(); }
    std::function<void (int)> onSelect;
    std::function<void (const juce::String&)> onMessage;

private:
    SIXAudioProcessor& proc;
    const int ch;
    bool selected = false;

    juce::Label number, name;
    ControlPill liveIn;
    ControlSlider sendIn, pan, fader;
    juce::Label sendInLabel, sendInValue, panLabel, panValue, faderValue, outLabel;
    ControlPill mono;
    OutSelector out;
    LevelMeter meter;
};
}
