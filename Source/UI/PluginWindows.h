#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Engine/HostedInstance.h"

namespace six
{
// Native editor windows of hosted plug-ins (opened from the device chain).
class PluginWindowManager
{
public:
    PluginWindowManager();
    ~PluginWindowManager();

    void toggle (HostedInstance* inst, const juce::String& title);
    void open (HostedInstance* inst, const juce::String& title);
    void closeFor (HostedInstance* inst);
    void closeAll();
    bool isOpen (HostedInstance* inst) const;

    std::function<void()> onChange;

private:
    class Window;
    std::vector<std::unique_ptr<Window>> windows;
};
}
