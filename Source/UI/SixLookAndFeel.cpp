#include "SixLookAndFeel.h"

namespace six
{
using namespace juce;

Font sixFont (float h, bool bold)
{
    return Font (FontOptions (h, bold ? Font::bold : Font::plain));
}

SixLookAndFeel::SixLookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, col::bgBottom);
    setColour (Label::textColourId, col::text);
    setColour (ComboBox::backgroundColourId, col::panel);
    setColour (ComboBox::textColourId, col::text);
    setColour (ComboBox::outlineColourId, col::border);
    setColour (ComboBox::arrowColourId, col::text);
    setColour (PopupMenu::backgroundColourId, col::panel);
    setColour (PopupMenu::textColourId, col::text);
    setColour (PopupMenu::highlightedBackgroundColourId, col::tealDim);
    setColour (PopupMenu::highlightedTextColourId, Colours::white);
    setColour (PopupMenu::headerTextColourId, col::muted);
    setColour (TextEditor::backgroundColourId, col::bgBottom);
    setColour (TextEditor::textColourId, col::text);
    setColour (TextEditor::highlightColourId, col::tealDim);
    setColour (TextEditor::outlineColourId, col::border);
    setColour (TextEditor::focusedOutlineColourId, col::teal);
    setColour (CaretComponent::caretColourId, col::teal);
    setColour (TextButton::buttonColourId, col::panel);
    setColour (TextButton::buttonOnColourId, col::teal);
    setColour (TextButton::textColourOffId, col::text);
    setColour (TextButton::textColourOnId, col::bgBottom);
    setColour (ListBox::backgroundColourId, Colours::transparentBlack);
    setColour (ScrollBar::thumbColourId, col::border);
    setColour (AlertWindow::backgroundColourId, col::panel);
    setColour (AlertWindow::textColourId, col::text);
    setColour (AlertWindow::outlineColourId, col::border);
    setColour (Slider::textBoxTextColourId, col::text);
    setColour (Slider::textBoxBackgroundColourId, col::bgBottom);
    setColour (Slider::textBoxOutlineColourId, col::border);
}

void SixLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float r = jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float lw = jmax (2.0f, r * 0.14f);

    g.setColour (col::track);
    g.drawEllipse (Rectangle<float> (r * 2, r * 2).withCentre (c).reduced (lw * 0.5f), lw);

    // bipolar knobs (pan) draw from the top, others from the start
    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float zero = bipolar ? (start + end) * 0.5f : start;
    const float ang = start + pos * (end - start);
    Path arc;
    arc.addCentredArc (c.x, c.y, r - lw * 0.5f, r - lw * 0.5f, 0.0f, jmin (zero, ang), jmax (zero, ang), true);
    g.setColour (s.isEnabled() ? col::teal : col::dim);
    g.strokePath (arc, PathStrokeType (lw, PathStrokeType::curved, PathStrokeType::rounded));

    // pointer
    Path p;
    const float pl = r * 0.55f;
    p.startNewSubPath (c.x, c.y - r + lw * 1.6f);
    p.lineTo (c.x, c.y - r + lw * 1.6f + pl * 0.45f);
    p.applyTransform (AffineTransform::rotation (ang, c.x, c.y));
    g.setColour (col::text);
    g.strokePath (p, PathStrokeType (lw * 0.75f, PathStrokeType::curved, PathStrokeType::rounded));
}

void SixLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle st, Slider& s)
{
    const bool vertical = st == Slider::LinearVertical;
    if (vertical)
    {
        const float cx = x + w * 0.5f;
        const float tw = 5.0f;
        auto track = Rectangle<float> (cx - tw * 0.5f, (float) y, tw, (float) h);
        g.setColour (col::track);
        g.fillRoundedRectangle (track, tw * 0.5f);
        g.setColour (col::teal);
        g.fillRoundedRectangle (track.withTop (pos), tw * 0.5f);
        auto thumb = Rectangle<float> (40.0f, 15.0f).withCentre ({ cx, pos });
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (thumb.translated (0, 2), 4.0f);
        g.setColour (Colour (0xfff3f6ff));
        g.fillRoundedRectangle (thumb, 4.0f);
        g.setColour (col::track);
        g.drawHorizontalLine ((int) pos, thumb.getX() + 6, thumb.getRight() - 6);
    }
    else
    {
        const float cy = y + h * 0.5f;
        const float th = 5.0f;
        auto track = Rectangle<float> ((float) x, cy - th * 0.5f, (float) w, th);
        g.setColour (col::track);
        g.fillRoundedRectangle (track, th * 0.5f);
        g.setColour (s.findColour (Slider::trackColourId, true) == Colour() ? col::teal : s.findColour (Slider::trackColourId));
        g.fillRoundedRectangle (track.withRight (pos), th * 0.5f);
        g.setColour (Colour (0xfff3f6ff));
        g.fillEllipse (Rectangle<float> (15.0f, 15.0f).withCentre ({ pos, cy }));
    }
}

void SixLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    auto b = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.isEnabled() ? col::panel : col::bgBottom);
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (box.hasKeyboardFocus (true) ? col::teal : col::border);
    g.drawRoundedRectangle (b, 6.0f, 1.0f);
    Path arrow;
    const float ax = (float) w - 18.0f, ay = h * 0.5f;
    arrow.addTriangle (ax - 5, ay - 2.5f, ax + 5, ay - 2.5f, ax, ay + 3.5f);
    g.setColour (col::text);
    g.fillPath (arrow);
}

void SixLookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (Justification::centred);
}

void SixLookAndFeel::drawPopupMenuBackground (Graphics& g, int w, int h)
{
    g.fillAll (col::panel);
    g.setColour (col::border);
    g.drawRect (0, 0, w, h);
}

void SixLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour& bg, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? b.findColour (TextButton::buttonOnColourId) : bg;
    if (down) c = c.brighter (0.15f); else if (over) c = c.brighter (0.07f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (b.getToggleState() ? c : col::border);
    g.drawRoundedRectangle (r, 7.0f, 1.0f);
}

void SixLookAndFeel::fillTextEditorBackground (Graphics& g, int w, int h, TextEditor& e)
{
    g.setColour (e.findColour (TextEditor::backgroundColourId));
    g.fillRoundedRectangle (0, 0, (float) w, (float) h, 6.0f);
}

void SixLookAndFeel::drawTextEditorOutline (Graphics& g, int w, int h, TextEditor& e)
{
    g.setColour (e.hasKeyboardFocus (true) ? col::teal : col::border);
    g.drawRoundedRectangle (0.5f, 0.5f, w - 1.0f, h - 1.0f, 6.0f, 1.0f);
}

void SixLookAndFeel::drawScrollbar (Graphics& g, ScrollBar&, int x, int y, int w, int h, bool vertical, int ts, int tsz, bool over, bool)
{
    auto r = vertical ? Rectangle<int> (x + w / 2 - 2, ts, 4, tsz) : Rectangle<int> (ts, y + h / 2 - 2, tsz, 4);
    g.setColour (over ? col::muted : col::border);
    g.fillRoundedRectangle (r.toFloat(), 2.0f);
}
}
