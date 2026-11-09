#pragma once
#include "Widgets.h"

namespace six
{
class DeviceSlot;

// Bottom area: the four effect slots of the selected channel.
class DeviceChain : public juce::Component, public Refreshable
{
public:
    explicit DeviceChain (SIXAudioProcessor& p);
    ~DeviceChain() override;
    void setChannel (int ch);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override;
    std::function<void (const juce::String&)> onMessage;
    std::function<void()> onBrowse;

    // shared loading helper (Finder drop, Browse drop, double-click in Browse)
    void loadDescription (int slot, const juce::PluginDescription& d);
    void chooseAndLoad (int slot, const juce::Array<juce::PluginDescription>& types, juce::Component* target);
    int getChannel() const noexcept { return channel; }
    SIXAudioProcessor& proc;

private:
    int channel = 0;
    std::array<std::unique_ptr<DeviceSlot>, (size_t) kNumSlots> slots;
};

class DeviceSlot : public juce::Component,
                   public juce::FileDragAndDropTarget,
                   public juce::DragAndDropTarget,
                   public juce::TextDragAndDropTarget
{
public:
    DeviceSlot (DeviceChain& owner, int slotIndex);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { setHighlight (true); }
    void fileDragExit (const juce::StringArray&) override { setHighlight (false); }
    void filesDropped (const juce::StringArray&, int, int) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { setHighlight (true); }
    void itemDragExit (const SourceDetails&) override { setHighlight (false); }
    void itemDropped (const SourceDetails&) override;

    bool isInterestedInTextDrag (const juce::String&) override { return true; }
    void textDragEnter (const juce::String&, int, int) override { setHighlight (true); }
    void textDragExit (const juce::String&) override { setHighlight (false); }
    void textDropped (const juce::String&, int, int) override;

private:
    void setHighlight (bool h) { highlight = h; repaint(); }
    void showMenu();
    void showMapMenu();
    const HostManager::SlotModel& model() const;
    HostManager& host() const;
    juce::String title() const;

    DeviceChain& chain;
    const int slot;
    bool highlight = false, dragStarted = false;
    juce::TextButton power { "On" }, edit { "Edit" }, mapBtn { "Map" }, remove { "x" };
};
}
