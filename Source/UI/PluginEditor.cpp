#include "PluginEditor.h"
#include "../PluginProcessor.h"
#include "PresetBar.h"
#include "SendsPanel.h"
#include "ChannelStrip.h"
#include "DeviceChain.h"
#include "BrowserPanel.h"
#include "PluginWindows.h"

using namespace juce;
using namespace six;

// Everything is laid out on a fixed 1600 x 980 design canvas and scaled as a whole.
class SIXEditor::Content : public Component
{
public:
    Content (SIXAudioProcessor& p, SIXEditor& ed)
        : header (p), presets (p), sends (p), chain (p), browser (p)
    {
        addAndMakeVisible (header);
        addAndMakeVisible (presets);
        for (int i = 0; i < kNumChannels; ++i)
        {
            strips[(size_t) i] = std::make_unique<ChannelStrip> (p, i);
            strips[(size_t) i]->onSelect = [&ed] (int ch) { ed.selectChannel (ch); };
            strips[(size_t) i]->onMessage = [&ed] (const String& m) { ed.showMessage (m); };
            addAndMakeVisible (*strips[(size_t) i]);
        }
        addAndMakeVisible (sends);
        addAndMakeVisible (chain);
        addChildComponent (browser);

        toast.setFont (sixFont (16.0f));
        toast.setJustificationType (Justification::centred);
        toast.setColour (Label::textColourId, col::warn);
        addAndMakeVisible (toast);

        sends.onMessage = [&ed] (const String& m) { ed.showMessage (m); };
        chain.onMessage = [&ed] (const String& m) { ed.showMessage (m); };
        chain.onBrowse = [&ed] { ed.toggleBrowser(); };
        header.onBrowse = [&ed] { ed.toggleBrowser(); };
        browser.onClose = [this] { browser.setVisible (false); };
        browser.onLoad = [this] (const PluginDescription& d)
        {
            const int s = chain.proc.host->firstEmptySlot (chain.getChannel());
            if (s < 0) { toast.setText ("All four slots are in use - drag onto a slot to replace.", dontSendNotification); return; }
            chain.loadDescription (s, d);
        };
    }

    void paint (Graphics& g) override
    {
        ColourGradient grad (col::bgTop, 0, 0, col::bgBottom, 0, (float) getHeight(), false);
        g.setGradientFill (grad);
        g.fillAll();
    }

    void resized() override
    {
        header.setBounds (0, 0, designW, 64);
        toast.setBounds (420, 14, 470, 36);
        presets.setBounds (24, 68, designW - 48, 58);
        for (int i = 0; i < kNumChannels; ++i)
            strips[(size_t) i]->setBounds (40 + i * 254, 330, 246, 414);
        layoutSends();
        chain.setBounds (0, 760, designW, 220);
        browser.setBounds (designW - 780, 132, 756, 620);
    }

    void layoutSends()
    {
        const int ch = sends.getChannel();
        const auto sb = strips[(size_t) ch]->getBounds();
        const int w = 486, h = 206;
        const int x = jlimit (16, designW - w - 16, sb.getX() - 4);
        sends.setBounds (x, 136, w, h);
        sends.arrowX = (float) (sb.getX() + 96 - x);
        sends.repaint();
    }

    void refreshAll()
    {
        header.refresh();
        presets.refresh();
        sends.refresh();
        for (auto& s : strips) s->refresh();
        chain.refresh();
        if (browser.isVisible()) browser.refresh();
    }

    HeaderBar header;
    PresetBar presets;
    SendsPanel sends;
    std::array<std::unique_ptr<ChannelStrip>, (size_t) kNumChannels> strips;
    DeviceChain chain;
    BrowserPanel browser;
    Label toast;
    uint32 toastTime = 0;
};

SIXEditor::SIXEditor (SIXAudioProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    content = std::make_unique<Content> (p, *this);
    addAndMakeVisible (*content);
    content->setBounds (0, 0, designW, designH);

    setResizable (true, true);
    setResizeLimits (designW * 6 / 10, designH * 6 / 10, designW * 6 / 5, designH * 6 / 5);
    getConstrainer()->setFixedAspectRatio ((double) designW / designH);
    setSize (designW * 4 / 5, designH * 4 / 5);

    selectChannel (proc.selectedChannel);
    proc.host->addChangeListener (this);
    proc.host->windows().onChange = [this] { content->chain.refresh(); };
    lastToast = proc.host->toastSerial;
    startTimerHz (30);
}

SIXEditor::~SIXEditor()
{
    stopTimer();
    proc.host->windows().onChange = nullptr;
    proc.host->removeChangeListener (this);
    content.reset();
    setLookAndFeel (nullptr);
}

void SIXEditor::paint (Graphics& g) { g.fillAll (col::bgBottom); }

void SIXEditor::resized()
{
    const float s = (float) getWidth() / (float) designW;
    content->setTransform (AffineTransform::scale (s));
}

void SIXEditor::selectChannel (int ch)
{
    // Selecting a channel for editing never activates a preset (independent choices).
    proc.selectedChannel = ch;
    for (int i = 0; i < kNumChannels; ++i) content->strips[(size_t) i]->setSelected (i == ch);
    content->sends.setChannel (ch);
    content->layoutSends();
    content->chain.setChannel (ch);
}

void SIXEditor::showMessage (const String& m)
{
    content->toast.setText (m, dontSendNotification);
    content->toastTime = Time::getMillisecondCounter();
}

void SIXEditor::toggleBrowser()
{
    content->browser.setVisible (! content->browser.isVisible());
    if (content->browser.isVisible()) content->browser.toFront (true);
}

void SIXEditor::timerCallback()
{
    content->refreshAll();
    if (proc.host->toastSerial != lastToast)
    {
        lastToast = proc.host->toastSerial;
        showMessage (proc.host->toastText);
    }
    if (content->toast.getText().isNotEmpty() && Time::getMillisecondCounter() - content->toastTime > 6000)
        content->toast.setText ({}, dontSendNotification);
}

void SIXEditor::changeListenerCallback (ChangeBroadcaster*)
{
    content->chain.refresh();
    content->presets.repaint();
    content->sends.repaint();
}
