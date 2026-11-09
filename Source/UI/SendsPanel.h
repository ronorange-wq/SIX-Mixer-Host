#pragma once
#include "Widgets.h"

namespace six
{
// Floating panel with the five sends of the selected channel.
class SendsPanel : public juce::Component, public Refreshable
{
public:
    explicit SendsPanel (SIXAudioProcessor& p);
    void setChannel (int ch);
    int getChannel() const noexcept { return channel; }
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override;
    float arrowX = 100.0f;   // where the pointer arrow sits (local x)
    std::function<void (const juce::String&)> onMessage;

private:
    struct Row
    {
        juce::Label target, value;
        std::unique_ptr<ControlSlider> level;
        std::unique_ptr<PrePostSwitch> mode;
    };
    SIXAudioProcessor& proc;
    int channel = 0;
    std::array<Row, (size_t) kNumSends> rows;
};
}
