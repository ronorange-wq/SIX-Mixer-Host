#pragma once
#include "Widgets.h"

namespace six
{
// Top area: logo, Sync/ms + one time field, Browse; below it the Morph Presets bank.
class HeaderBar : public juce::Component, public Refreshable
{
public:
    explicit HeaderBar (SIXAudioProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    void refresh() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    std::function<void()> onBrowse;
    juce::TextButton browse { "Browse" };

private:
    juce::Rectangle<int> syncArea() const { return { 908, 14, 76, 36 }; }
    juce::Rectangle<int> msArea() const   { return { 984, 14, 62, 36 }; }
    juce::Rectangle<int> timeArea() const { return { 1050, 14, 110, 36 }; }
    bool isMs() const;
    SIXAudioProcessor& proc;
    float dragStartMs = 0;
    bool draggingTime = false;
    std::unique_ptr<juce::TextEditor> msEditor;
    juce::Path iconPath;
};

class PresetBar : public juce::Component, public Refreshable
{
public:
    explicit PresetBar (SIXAudioProcessor& p);
    ~PresetBar() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override;
    int page() const;

private:
    class PresetButton;
    SIXAudioProcessor& proc;
    std::array<std::unique_ptr<PresetButton>, (size_t) kPresetsPerPage> buttons;
    juce::TextButton add { "+" }, prev { "<" }, next { ">" };
    void setPage (int p);
};
}
