#include "SendsPanel.h"

namespace six
{
using namespace juce;

SendsPanel::SendsPanel (SIXAudioProcessor& p) : proc (p)
{
    for (int k = 0; k < kNumSends; ++k)
    {
        auto& r = rows[(size_t) k];
        r.target.setFont (sixFont (18.0f));
        r.value.setFont (sixFont (16.5f));
        r.value.setJustificationType (Justification::centredRight);
        addAndMakeVisible (r.target);
        addAndMakeVisible (r.value);
        auto* lvl = &r;
        makeValueTypeable (r.value, p, [lvl] { return lvl->level->getControl(); },
                           [this] (const String& m) { if (onMessage) onMessage (m); });
        r.level = std::make_unique<ControlSlider> (p, sendLevelControl (0, sendTarget (0, k)), Slider::LinearHorizontal);
        r.level->onRejected = [this] (const String& m) { if (onMessage) onMessage (m); };
        r.level->setDoubleClickReturnValue (true, kMinDb);
        addAndMakeVisible (*r.level);
        r.mode = std::make_unique<PrePostSwitch> (p, sendModeControl (0, sendTarget (0, k)));
        addAndMakeVisible (*r.mode);
    }
    setChannel (0);
}

void SendsPanel::setChannel (int ch)
{
    channel = ch;
    for (int k = 0; k < kNumSends; ++k)
    {
        auto& r = rows[(size_t) k];
        const int dst = sendTarget (ch, k);   // built from channel numbers: never a duplicate or itself
        r.target.setText (String (CharPointer_UTF8 ("\xe2\x86\x92  ")) + twoDigits (dst + 1), dontSendNotification);
        r.level->setControl (sendLevelControl (ch, dst));
        r.mode->setControl (sendModeControl (ch, dst));
    }
    repaint();
    refresh();
}

void SendsPanel::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().withTrimmedBottom (12.0f);
    Path p;
    p.addRoundedRectangle (b.reduced (0.5f), 12.0f);
    Path arrow;
    arrow.addTriangle (arrowX - 12, b.getBottom() - 1, arrowX + 12, b.getBottom() - 1, arrowX, b.getBottom() + 11);
    g.setColour (Colour (0xf2132350));
    g.fillPath (p);
    g.fillPath (arrow);
    g.setColour (col::border.brighter (0.15f));
    g.strokePath (p, PathStrokeType (1.0f));

    g.setFont (sixFont (21.0f));
    g.setColour (col::text);
    g.drawText ("Sends", 22, 10, 70, 30, Justification::centredLeft);
    g.setColour (col::muted);
    g.drawText (String (CharPointer_UTF8 ("\xe2\x80\x94")), 86, 10, 24, 30, Justification::centred);
    g.setColour (col::teal);
    auto nm = proc.host->channelNames[(size_t) channel];
    g.drawText (twoDigits (channel + 1) + (nm.isNotEmpty() ? " " + nm : String()), 114, 10, 300, 30, Justification::centredLeft);
}

void SendsPanel::resized()
{
    for (int k = 0; k < kNumSends; ++k)
    {
        auto& r = rows[(size_t) k];
        const int y = 46 + k * 28;
        r.target.setBounds (18, y, 70, 26);
        r.level->setBounds (92, y + 2, 150, 22);
        r.value.setBounds (246, y, 90, 26);
        r.mode->setBounds (352, y + 2, 112, 23);
    }
}

void SendsPanel::refresh()
{
    for (int k = 0; k < kNumSends; ++k)
    {
        auto& r = rows[(size_t) k];
        r.level->refresh();
        r.mode->refresh();
        const float db = proc.engine.processedBlocks.load() > 0 ? proc.engine.effective[(size_t) r.level->getControl()].load()
                                                                : proc.params.raw[(size_t) r.level->getControl()]->load();
        if (! r.value.isBeingEdited()) r.value.setText (formatDb (db), dontSendNotification);
        r.value.setColour (Label::textColourId, db <= kMinDb ? col::dim : col::text);
    }
}
}
