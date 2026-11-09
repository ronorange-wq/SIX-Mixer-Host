#include "BrowserPanel.h"

namespace six
{
using namespace juce;

BrowserPanel::BrowserPanel (SIXAudioProcessor& p) : proc (p)
{
    search.setTextToShowWhenEmpty ("Search effects (name, maker, VST3/AU)...", col::dim);
    search.setFont (sixFont (17.0f));
    search.setIndents (12, 9);
    search.onTextChange = [this] { updateResults(); };
    search.onReturnKey = [this] { if (results.size() > 0) loadRow (jmax (0, list.getSelectedRow())); };
    addAndMakeVisible (search);

    rescan.onClick = [this] { proc.host->catalog.startScan (true); refresh(); };
    close.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (rescan);
    addAndMakeVisible (close);

    status.setFont (sixFont (14.0f));
    status.setColour (Label::textColourId, col::muted);
    addAndMakeVisible (status);

    list.setRowHeight (44);
    list.setColour (ListBox::outlineColourId, Colours::transparentBlack);
    addAndMakeVisible (list);

    proc.host->catalog.changes.addChangeListener (this);
    updateResults();
}

BrowserPanel::~BrowserPanel() { proc.host->catalog.changes.removeChangeListener (this); }

void BrowserPanel::visibilityChanged()
{
    if (! isVisible()) return;
    auto& c = proc.host->catalog;
    if (c.getEffects().isEmpty() && ! c.isScanning()) c.startScan (false);
    updateResults();
    search.grabKeyboardFocus();
}

void BrowserPanel::paint (Graphics& g)
{
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (getLocalBounds().toFloat().translated (-4, 6), 14.0f);
    drawPanel (g, getLocalBounds().toFloat(), Colour (0xff0f1c42), col::border.brighter (0.3f), 14.0f);
    g.setColour (col::text);
    g.setFont (sixFont (22.0f));
    g.drawText ("Browse", 22, 14, 200, 30, Justification::centredLeft);
    g.setColour (col::muted);
    g.setFont (sixFont (14.0f));
    g.drawText ("Drag an effect onto a slot below, or double-click to load it into the selected channel.",
                22, getHeight() - 34, getWidth() - 44, 24, Justification::centredLeft);
}

void BrowserPanel::resized()
{
    close.setBounds (getWidth() - 96, 12, 80, 34);
    rescan.setBounds (getWidth() - 186, 12, 82, 34);
    search.setBounds (20, 56, getWidth() - 40, 38);
    status.setBounds (20, 98, getWidth() - 40, 22);
    list.setBounds (12, 124, getWidth() - 24, getHeight() - 124 - 40);
}

void BrowserPanel::refresh()
{
    auto& c = proc.host->catalog;
    if (c.isScanning())
        status.setText (String (roundToInt (c.getScanProgress() * 100)) + "%  " + c.getScanStatus(), dontSendNotification);
    else
        status.setText (String (results.size()) + " effects" + (search.isEmpty() ? String() : " found")
                         + "   (VST3 + Audio Units, instruments hidden)", dontSendNotification);
}

void BrowserPanel::updateResults()
{
    results = proc.host->catalog.search (search.getText());
    list.updateContent();
    list.repaint();
    refresh();
}

void BrowserPanel::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    if (! isPositiveAndBelow (row, results.size())) return;
    const auto& d = results.getReference (row);
    auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (6, 2);
    if (selected) { g.setColour (col::tealDim.withAlpha (0.5f)); g.fillRoundedRectangle (r, 7.0f); }
    g.setColour (col::text);
    g.setFont (sixFont (17.0f));
    g.drawText (d.name, r.reduced (12, 0).withHeight (r.getHeight() * 0.58f).translated (0, 2), Justification::bottomLeft);
    g.setColour (col::muted);
    g.setFont (sixFont (13.0f));
    g.drawText (d.manufacturerName + String (CharPointer_UTF8 ("  \xc2\xb7  ")) + d.category,
                r.reduced (12, 0).withTrimmedTop (r.getHeight() * 0.58f), Justification::topLeft);
    auto badge = r.removeFromRight (64).reduced (8, 12);
    g.setColour (col::border.brighter (0.2f));
    g.drawRoundedRectangle (badge, 5.0f, 1.0f);
    g.setFont (sixFont (12.0f));
    g.drawText (d.pluginFormatName == "AudioUnit" ? "AU" : d.pluginFormatName, badge, Justification::centred);
}

void BrowserPanel::listBoxItemDoubleClicked (int row, const MouseEvent&) { loadRow (row); }

void BrowserPanel::loadRow (int row)
{
    if (isPositiveAndBelow (row, results.size()) && onLoad) onLoad (results[row]);
}

var BrowserPanel::getDragSourceDescription (const SparseSet<int>& rows)
{
    if (rows.isEmpty() || ! isPositiveAndBelow (rows[0], results.size())) return {};
    return "six-plugin:" + results[rows[0]].createIdentifierString();
}
}
