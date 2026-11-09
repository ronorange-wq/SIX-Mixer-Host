#include "Widgets.h"
#include "../Core/Routing.h"

namespace six
{
using namespace juce;

void drawPanel (Graphics& g, Rectangle<float> r, Colour fill, Colour outline, float radius)
{
    g.setColour (fill);
    g.fillRoundedRectangle (r, radius);
    g.setColour (outline);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

// ------------------------------------------------------------------------------ ControlSlider
ControlSlider::ControlSlider (SIXAudioProcessor& p, int c, SliderStyle style)
    : Slider (style, NoTextBox), proc (p)
{
    setControl (c);
    setScrollWheelEnabled (true);
    setVelocityBasedMode (false);
    if (style == RotaryHorizontalVerticalDrag)
        setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
}

void ControlSlider::setControl (int c)
{
    control = c;
    param = proc.params.controls[(size_t) c];
    const ScopedValueSetter<bool> s (updating, true);
    setNormalisableRange (param->getNormalisableRange().getRange().getLength() > 0
                              ? NormalisableRange<double> (param->getNormalisableRange().start, param->getNormalisableRange().end,
                                    [this] (double, double, double x) { return (double) param->convertFrom0to1 ((float) x); },
                                    [this] (double, double, double v) { return (double) param->convertTo0to1 ((float) v); })
                              : NormalisableRange<double> (0, 1));
    setDoubleClickReturnValue (true, controlInfo (c).defaultValue);
    refresh();
}

void ControlSlider::refresh()
{
    if (inGesture || isMouseButtonDown()) return;
    const float v = proc.engine.effective[(size_t) control].load();
    const float shown = proc.engine.processedBlocks.load() > 0 ? v : proc.params.raw[(size_t) control]->load();
    if (std::abs (shown - (float) getValue()) > 1.0e-5f)
    {
        const ScopedValueSetter<bool> s (updating, true);
        setValue (shown, dontSendNotification);
    }
}

bool ControlSlider::allowChange (float v)
{
    if (controlInfo (control).kind != ControlKind::gainDb || controlInfo (control).other < 0) return true;
    const auto err = validateRoutingChange (proc.host->currentValues(), control, v);
    if (err.isEmpty()) return true;
    if (onRejected) onRejected (err);
    return false;
}

void ControlSlider::startedDragging()
{
    inGesture = true;
    param->beginChangeGesture();
}

void ControlSlider::stoppedDragging()
{
    param->endChangeGesture();
    inGesture = false;
}

void ControlSlider::valueChanged()
{
    if (updating) return;
    const float v = (float) getValue();
    if (! allowChange (v))
    {
        const ScopedValueSetter<bool> s (updating, true);
        setValue (proc.engine.effective[(size_t) control].load(), dontSendNotification);
        return;
    }
    const float norm = param->convertTo0to1 (v);
    if (inGesture) param->setValueNotifyingHost (norm);
    else { param->beginChangeGesture(); param->setValueNotifyingHost (norm); param->endChangeGesture(); }
}

String ControlSlider::getTextFromValue (double v) { return param->getText (param->convertTo0to1 ((float) v), 32); }
double ControlSlider::getValueFromText (const String& t) { return param->convertFrom0to1 (param->getValueForText (t)); }

// ------------------------------------------------------------------------------ ControlPill
ControlPill::ControlPill (SIXAudioProcessor& p, int c, String offText, String onText)
    : proc (p), control (c), off (std::move (offText)), on (std::move (onText)) {}

void ControlPill::paint (Graphics& g)
{
    const bool isOn = proc.engine.effective[(size_t) control].load() > 0.5f;
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float rad = r.getHeight() * 0.5f;
    if (isOn && filled)
    {
        g.setColour (col::teal.withAlpha (0.18f));
        g.fillRoundedRectangle (r, rad);
    }
    g.setColour (isOn ? col::teal : col::border.brighter (0.2f));
    g.drawRoundedRectangle (r, rad, 1.2f);
    g.setColour (isOn ? col::text : col::muted);
    g.setFont (sixFont (r.getHeight() * 0.58f));
    g.drawText (isOn ? on : off, r, Justification::centred);
}

void ControlPill::mouseUp (const MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition())) return;
    const bool isOn = proc.engine.effective[(size_t) control].load() > 0.5f;
    ParamRefs::setWithGesture (proc.params.controls[(size_t) control], isOn ? 0.0f : 1.0f);
}

// ------------------------------------------------------------------------------ PrePost
void PrePostSwitch::paint (Graphics& g)
{
    const bool post = proc.engine.effective[(size_t) control].load() > 0.5f;
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (col::bgBottom);
    g.fillRoundedRectangle (r, 6.0f);
    auto left = r.withWidth (r.getWidth() * 0.5f), right = r.withLeft (left.getRight());
    auto active = post ? right : left;
    g.setColour (col::panelHi.brighter (0.25f));
    g.fillRoundedRectangle (active.reduced (1.5f), 5.0f);
    g.setFont (sixFont (r.getHeight() * 0.62f));
    g.setColour (post ? col::dim : col::text);
    g.drawText ("Pre", left, Justification::centred);
    g.setColour (post ? col::text : col::dim);
    g.drawText ("Post", right, Justification::centred);
}

void PrePostSwitch::mouseUp (const MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition())) return;
    const float v = e.x < getWidth() / 2 ? 0.0f : 1.0f;
    if (std::abs (proc.engine.effective[(size_t) control].load() - v) > 0.01f || proc.params.raw[(size_t) control]->load() != v)
        ParamRefs::setWithGesture (proc.params.controls[(size_t) control], v);
}

// ------------------------------------------------------------------------------ OutSelector
OutSelector::OutSelector (SIXAudioProcessor& p, int c) : proc (p), ch (c)
{
    addItem ("Master", 1);
    for (int i = 0; i < kNumChannels; ++i)
    {
        addItem ("Ch " + twoDigits (i + 1), i + 2);
        if (i == ch) setItemEnabled (i + 2, false);
    }
    onChange = [this]
    {
        if (updating) return;
        const int v = getSelectedId() - 1;
        const int idx = channelControl (ch, ccOut);
        const auto err = validateRoutingChange (proc.host->currentValues(), idx, (float) v);
        if (err.isNotEmpty())
        {
            if (onRejected) onRejected (err);
            refresh();
            return;
        }
        ParamRefs::setWithGesture (proc.params.controls[(size_t) idx], (float) v);
    };
    refresh();
}

void OutSelector::refresh()
{
    if (isPopupActive()) return;
    const int v = roundToInt (proc.engine.processedBlocks.load() > 0 ? proc.engine.effective[(size_t) channelControl (ch, ccOut)].load()
                                                                     : proc.params.raw[(size_t) channelControl (ch, ccOut)]->load());
    if (getSelectedId() != v + 1)
    {
        const ScopedValueSetter<bool> s (updating, true);
        setSelectedId (v + 1, dontSendNotification);
    }
}

// ------------------------------------------------------------------------------ Meter
void LevelMeter::refresh()
{
    const float l = proc.engine.peakL[(size_t) ch].exchange (0.0f);
    const float r = proc.engine.peakR[(size_t) ch].exchange (0.0f);
    levelL = jmax (l, levelL * 0.82f);
    levelR = jmax (r, levelR * 0.82f);
    repaint();
}

void LevelMeter::paint (Graphics& g)
{
    const int segs = 22;
    auto b = getLocalBounds().toFloat();
    const float gap = 2.0f;
    const float sh = (b.getHeight() - gap * (segs - 1)) / segs;
    auto toSegs = [] (float lv)
    {
        const float db = Decibels::gainToDecibels (lv, -60.0f);
        return roundToInt (jmap (db, -60.0f, 6.0f, 0.0f, 22.0f));
    };
    const int on = jmax (toSegs (levelL), toSegs (levelR));
    for (int i = 0; i < segs; ++i)
    {
        auto r = Rectangle<float> (b.getX(), b.getBottom() - (i + 1) * sh - i * gap, b.getWidth(), sh);
        Colour c = i < on ? (i >= segs - 2 ? col::warn : col::teal) : col::track.withAlpha (0.55f);
        g.setColour (c);
        g.fillRect (r);
    }
}
}
