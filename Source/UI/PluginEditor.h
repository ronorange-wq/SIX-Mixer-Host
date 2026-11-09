#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SixLookAndFeel.h"

class SIXAudioProcessor;

namespace six
{
class HeaderBar; class PresetBar; class SendsPanel; class ChannelStrip; class DeviceChain; class BrowserPanel;
}

class SIXEditor : public juce::AudioProcessorEditor,
                  public juce::DragAndDropContainer,
                  private juce::Timer,
                  private juce::ChangeListener
{
public:
    explicit SIXEditor (SIXAudioProcessor& p);
    ~SIXEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int designW = 1600, designH = 980;
    void selectChannel (int ch);
    void showMessage (const juce::String& m);
    void toggleBrowser();

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    class Content;
    SIXAudioProcessor& proc;
    six::SixLookAndFeel lnf;
    std::unique_ptr<Content> content;
    juce::TooltipWindow tooltips { this, 600 };
    juce::uint32 lastToast = 0;
};
