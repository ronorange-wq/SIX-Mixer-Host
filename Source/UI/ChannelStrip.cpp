#include "ChannelStrip.h"

namespace six
{
using namespace juce;

ChannelStrip::ChannelStrip (SIXAudioProcessor& p, int c)
    : proc (p), ch (c),
      liveIn (p, channelControl (c, ccLiveIn), "IN", "IN"),
      solo (p, soloControl (c), "S", "S"),
      sendIn (p, channelControl (c, ccSendIn), Slider::RotaryHorizontalVerticalDrag),
      pan (p, channelControl (c, ccPan), Slider::RotaryHorizontalVerticalDrag),
      fader (p, channelControl (c, ccFader), Slider::LinearVertical),
      mono (p, channelControl (c, ccMono), "Stereo", "Mono"),
      out (p, c),
      meter (p, c)
{
    number.setText (twoDigits (ch + 1), dontSendNotification);
    number.setFont (sixFont (27.0f, true));
    number.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (number);

    name.setFont (sixFont (25.0f));
    name.setEditable (false, true, false);
    name.setTooltip ("Double-click to rename");
    name.onTextChange = [this] { proc.host->channelNames[(size_t) ch] = name.getText().trim(); };
    addAndMakeVisible (name);

    liveIn.setTooltip ("Live In: feed this channel with the track's input");
    addAndMakeVisible (liveIn);
    solo.onColour = Colour (0xffffd24a);
    solo.setTooltip ("Solo: hear only this channel (and what it feeds)");
    addAndMakeVisible (solo);

    for (auto* l : { &sendInLabel, &panLabel, &outLabel })
    {
        l->setFont (sixFont (16.0f));
        l->setColour (Label::textColourId, col::text.withAlpha (0.9f));
        l->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (*l);
    }
    sendInLabel.setText ("Send In", dontSendNotification);
    panLabel.setText ("Pan", dontSendNotification);
    outLabel.setText ("Out", dontSendNotification);
    outLabel.setColour (Label::textColourId, col::muted);

    for (auto* l : { &sendInValue, &panValue, &faderValue })
    {
        l->setFont (sixFont (16.0f));
        l->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (*l);
    }
    faderValue.setFont (sixFont (19.0f));
    faderValue.setJustificationType (Justification::centred);
    sendInValue.setJustificationType (Justification::centredRight);

    sendIn.setTooltip ("Send In: input level of everything entering this channel (Live input, Outs and sends)");
    pan.setTooltip ("Pan (double-click: centre)");
    pan.setDoubleClickReturnValue (true, 0.0);
    fader.setDoubleClickReturnValue (true, 0.0);
    for (auto* s : { &sendIn, &pan, &fader }) addAndMakeVisible (*s);

    mono.filled = false;
    addAndMakeVisible (mono);
    out.onRejected = [this] (const String& m) { if (onMessage) onMessage (m); };
    addAndMakeVisible (out);
    addAndMakeVisible (meter);

    addMouseListener (this, true);
    refresh();
}

ChannelStrip::~ChannelStrip() { removeMouseListener (this); }

void ChannelStrip::mouseDown (const MouseEvent&)
{
    if (onSelect) onSelect (ch);
}

void ChannelStrip::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    if (selected)
    {
        ColourGradient grad (col::selBg.withAlpha (0.95f), 0, 0, col::selBg.withAlpha (0.55f), 0, b.getHeight(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (b, 10.0f);
    }
    // header underline
    g.setColour (selected ? col::teal : col::border);
    g.fillRect (Rectangle<float> (24.0f, 52.0f, b.getWidth() - 48.0f, selected ? 2.5f : 1.0f));
    // separator between mono pill and pan
    g.setColour (col::border.brighter (0.3f));
    g.fillRect (Rectangle<float> (102.0f, 121.0f, 1.0f, 20.0f));
}

void ChannelStrip::resized()
{
    number.setBounds (18, 12, 46, 36);
    name.setBounds (62, 12, getWidth() - 62 - 82, 36);
    solo.setBounds (getWidth() - 82, 20, 30, 20);
    liveIn.setBounds (getWidth() - 46, 20, 34, 20);

    sendIn.setBounds (24, 70, 38, 38);
    sendInLabel.setBounds (70, 74, 70, 30);
    sendInValue.setBounds (getWidth() - 110, 74, 92, 30);

    mono.setBounds (16, 117, 72, 28);
    panLabel.setBounds (110, 116, 44, 30);
    pan.setBounds (152, 113, 34, 34);
    panValue.setBounds (192, 116, 50, 30);

    fader.setBounds (getWidth() / 2 - 68, 160, 60, 150);
    meter.setBounds (getWidth() / 2 + 12, 164, 14, 140);
    faderValue.setBounds (getWidth() / 2 - 80, 318, 130, 30);

    outLabel.setBounds (18, 368, 40, 30);
    out.setBounds (60, 364, 128, 36);
}

void ChannelStrip::refresh()
{
    for (auto* r : std::initializer_list<Refreshable*> { &sendIn, &pan, &fader, &mono, &liveIn, &solo, &out, &meter }) r->refresh();
    const auto& e = proc.engine.effective;
    const bool running = proc.engine.processedBlocks.load() > 0;
    auto val = [&] (int i) { return running ? e[(size_t) i].load() : proc.params.raw[(size_t) i]->load(); };

    sendInValue.setText (formatDb (val (channelControl (ch, ccSendIn))), dontSendNotification);
    panValue.setText (formatPan (val (channelControl (ch, ccPan))), dontSendNotification);
    faderValue.setText (formatDb (val (channelControl (ch, ccFader))), dontSendNotification);

    const auto nm = proc.host->channelNames[(size_t) ch];
    if (! name.isBeingEdited() && name.getText() != nm) name.setText (nm, dontSendNotification);
    name.setColour (Label::textColourId, selected ? col::teal : col::text);
    number.setColour (Label::textColourId, selected ? col::teal : col::text);
}
}
