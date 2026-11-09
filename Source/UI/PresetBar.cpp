#include "PresetBar.h"

namespace six
{
using namespace juce;

// ------------------------------------------------------------------------------ HeaderBar
HeaderBar::HeaderBar (SIXAudioProcessor& p) : proc (p)
{
    browse.onClick = [this] { if (onBrowse) onBrowse(); };
    browse.setButtonText (String::repeatedString (" ", 6) + "Browse");
    addAndMakeVisible (browse);
    setRepaintsOnMouseActivity (false);
}

bool HeaderBar::isMs() const { return proc.params.rawMorphMode->load() > 0.5f; }

void HeaderBar::paint (Graphics& g)
{
    g.setColour (col::text);
    g.setFont (sixFont (44.0f, true));
    g.drawText ("SIX", 40, 8, 110, 50, Justification::centredLeft);
    g.setColour (col::muted);
    g.fillRect (148, 18, 2, 30);
    g.setColour (col::text);
    g.setFont (sixFont (21.0f));
    g.drawText ("Channel Mixer", 180, 14, 220, 36, Justification::centredLeft);

    // folder icon inside the Browse button
    auto fb = browse.getBounds().toFloat();
    auto ic = Rectangle<float> (fb.getX() + 26, fb.getCentreY() - 8, 22, 16);
    Path folder;
    folder.startNewSubPath (ic.getX(), ic.getY() + 3);
    folder.lineTo (ic.getX() + 8, ic.getY() + 3);
    folder.lineTo (ic.getX() + 10, ic.getY());
    folder.lineTo (ic.getRight(), ic.getY());
    folder.lineTo (ic.getRight(), ic.getBottom());
    folder.lineTo (ic.getX(), ic.getBottom());
    folder.closeSubPath();
    iconPath = folder;

    // Sync | ms | time  (one rounded group)
    auto group = Rectangle<int> (908, 14, 252, 36).toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (group, 8.0f);
    const bool ms = isMs();
    auto seg = (ms ? msArea() : syncArea()).toFloat().reduced (2.0f);
    g.setColour (col::teal);
    g.fillRoundedRectangle (seg, 6.0f);
    g.setFont (sixFont (19.0f));
    g.setColour (ms ? col::text : col::bgBottom);
    g.drawText ("Sync", syncArea(), Justification::centred);
    g.setColour (ms ? col::bgBottom : col::text);
    g.drawText ("ms", msArea(), Justification::centred);
    g.setColour (col::border.brighter (0.3f));
    g.fillRect (1048, 22, 1, 20);

    String t;
    if (ms) t = String (roundToInt (proc.params.rawMorphMs->load())) + " ms";
    else    t = syncChoiceNames()[roundToInt (proc.params.rawMorphSync->load())];
    g.setColour (col::text);
    if (msEditor == nullptr) g.drawText (t, timeArea(), Justification::centred);
}

void HeaderBar::resized()
{
    browse.setBounds (getWidth() - 190, 12, 162, 40);
}

void HeaderBar::paintOverChildren (Graphics& g)
{
    if (iconPath.isEmpty()) return;
    g.setColour (col::text);
    g.strokePath (iconPath, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

void HeaderBar::refresh() { repaint (Rectangle<int> (900, 0, 280, 64)); }

void HeaderBar::mouseDown (const MouseEvent& e)
{
    draggingTime = false;
    if (syncArea().contains (e.getPosition()) && isMs())       ParamRefs::setWithGesture (proc.params.morphMode, 0.0f);
    else if (msArea().contains (e.getPosition()) && ! isMs())  ParamRefs::setWithGesture (proc.params.morphMode, 1.0f);
    else if (timeArea().contains (e.getPosition()))
    {
        if (isMs())
        {
            dragStartMs = proc.params.rawMorphMs->load();
            draggingTime = true;
            proc.params.morphMs->beginChangeGesture();
        }
        else
        {
            PopupMenu m;
            const int cur = roundToInt (proc.params.rawMorphSync->load());
            for (int i = 0; i < kNumSyncChoices; ++i) m.addItem (i + 1, syncChoiceNames()[i], true, i == cur);
            SafePointer<HeaderBar> safe (this);
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (timeArea())),
                [safe] (int r) { if (safe != nullptr && r > 0) ParamRefs::setWithGesture (safe->proc.params.morphSync, (float) (r - 1)); });
        }
    }
    repaint();
}

void HeaderBar::mouseDrag (const MouseEvent& e)
{
    if (! draggingTime) return;
    const float dy = (float) -e.getDistanceFromDragStartY();
    const float v = jlimit (0.0f, 10000.0f, dragStartMs * std::pow (1.012f, dy) + dy * 0.5f);
    proc.params.morphMs->setValueNotifyingHost (proc.params.morphMs->convertTo0to1 ((float) roundToInt (v)));
    repaint();
}

void HeaderBar::mouseUp (const MouseEvent&)
{
    if (draggingTime) proc.params.morphMs->endChangeGesture();
    draggingTime = false;
}

void HeaderBar::mouseDoubleClick (const MouseEvent& e)
{
    if (! timeArea().contains (e.getPosition()) || ! isMs()) return;
    msEditor = std::make_unique<TextEditor>();
    msEditor->setFont (sixFont (19.0f));
    msEditor->setJustification (Justification::centred);
    msEditor->setInputRestrictions (5, "0123456789");
    msEditor->setText (String (roundToInt (proc.params.rawMorphMs->load())), false);
    msEditor->setBounds (timeArea().reduced (4));
    addAndMakeVisible (*msEditor);
    msEditor->grabKeyboardFocus();
    msEditor->selectAll();
    auto commit = [this] (bool apply)
    {
        if (msEditor == nullptr) return;
        if (apply) ParamRefs::setWithGesture (proc.params.morphMs, jlimit (0.0f, 10000.0f, msEditor->getText().getFloatValue()));
        MessageManager::callAsync ([safe = SafePointer<HeaderBar> (this)] { if (safe != nullptr) { safe->msEditor.reset(); safe->repaint(); } });
    };
    msEditor->onReturnKey = [commit] { commit (true); };
    msEditor->onEscapeKey = [commit] { commit (false); };
    msEditor->onFocusLost = [commit] { commit (true); };
}

// ------------------------------------------------------------------------------ PresetButton
class PresetBar::PresetButton : public Component
{
public:
    PresetButton (SIXAudioProcessor& p) : proc (p) {}
    int index = 0;

    void paint (Graphics& g) override
    {
        auto& hm = *proc.host;
        const auto& pr = hm.bank.get (index);
        const bool active = pr.used && proc.engine.activePreset.load() == index;
        auto r = getLocalBounds().toFloat().reduced (1.0f);

        if (active)                    { g.setColour (col::teal); g.fillRoundedRectangle (r, 7.0f); }
        else if (pr.used)              { drawPanel (g, r, col::panelHi, col::border.brighter (0.35f), 7.0f); }
        else                           { g.setColour (col::border.withAlpha (0.8f)); g.drawRoundedRectangle (r, 7.0f, 1.0f); }
        if (isMouseOver() && ! active) { g.setColour (Colours::white.withAlpha (0.05f)); g.fillRoundedRectangle (r, 7.0f); }

        // morph progress
        const float prog = proc.engine.morphProgress.load();
        if (proc.engine.morphTarget.load() == index && prog < 1.0f)
        {
            g.setColour (active ? col::bgBottom.withAlpha (0.6f) : col::teal);
            g.fillRoundedRectangle (r.getX() + 6, r.getBottom() - 5, (r.getWidth() - 12) * prog, 3.0f, 1.5f);
        }

        const auto num = twoDigits (index + 1);
        g.setColour (active ? col::bgBottom : pr.used ? col::text : col::dim);
        if (pr.name.isEmpty())
        {
            g.setFont (sixFont (21.0f, active));
            g.drawText (num, r, Justification::centred);
        }
        else
        {
            g.setFont (sixFont (17.0f, active));
            g.drawFittedText (num + "  " + pr.name, r.reduced (8, 0).toNearestInt(), Justification::centred, 1, 0.8f);
        }
    }

    void mouseEnter (const MouseEvent&) override { repaint(); }
    void mouseExit (const MouseEvent&) override { repaint(); }

    void mouseUp (const MouseEvent& e) override
    {
        if (! getLocalBounds().contains (e.getPosition())) return;
        auto& hm = *proc.host;
        if (e.mods.isPopupMenu()) { showMenu(); return; }
        hm.clickPreset (index);   // empty slot: nothing changes
    }

    void showMenu()
    {
        auto& hm = *proc.host;
        const bool used = hm.bank.get (index).used;
        PopupMenu m;
        m.addSectionHeader ("Preset " + twoDigits (index + 1));
        m.addItem (1, used ? "Update (save current mix here)" : "Save current mix here");
        m.addItem (2, "Rename...", used);
        m.addItem (3, "Clear name", used && hm.bank.get (index).name.isNotEmpty());
        m.addSeparator();
        m.addItem (4, "Delete preset", used);
        SafePointer<PresetButton> safe (this);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [safe] (int r)
        {
            if (safe == nullptr) return;
            auto& h = *safe->proc.host;
            if (r == 1) h.updatePreset (safe->index);
            if (r == 2) safe->startRename();
            if (r == 3) h.renamePreset (safe->index, {});
            if (r == 4) h.clearPreset (safe->index);
            safe->getParentComponent()->repaint();
        });
    }

    void startRename()
    {
        editor = std::make_unique<TextEditor>();
        editor->setFont (sixFont (17.0f));
        editor->setJustification (Justification::centred);
        editor->setText (proc.host->bank.get (index).name, false);
        editor->setBounds (getLocalBounds().reduced (3));
        editor->setTextToShowWhenEmpty ("name (optional)", col::dim);
        addAndMakeVisible (*editor);
        editor->grabKeyboardFocus();
        editor->selectAll();
        auto commit = [this] (bool apply)
        {
            if (editor == nullptr) return;
            if (apply) proc.host->renamePreset (index, editor->getText());   // empty name -> number only
            MessageManager::callAsync ([safe = SafePointer<PresetButton> (this)] { if (safe != nullptr) { safe->editor.reset(); safe->repaint(); } });
        };
        editor->onReturnKey = [commit] { commit (true); };
        editor->onEscapeKey = [commit] { commit (false); };
        editor->onFocusLost = [commit] { commit (true); };
    }

    SIXAudioProcessor& proc;
    std::unique_ptr<TextEditor> editor;
};

// ------------------------------------------------------------------------------ PresetBar
PresetBar::PresetBar (SIXAudioProcessor& p) : proc (p)
{
    for (auto& b : buttons) { b = std::make_unique<PresetButton> (p); addAndMakeVisible (*b); }
    add.setTooltip ("Save the current mix into the next free preset");
    add.onClick = [this]
    {
        const int i = proc.host->saveNewPreset();
        if (i >= 0) setPage (i / kPresetsPerPage);
        repaint();
    };
    prev.onClick = [this] { setPage (page() - 1); };
    next.onClick = [this] { setPage (page() + 1); };
    for (auto* b : { &add, &prev, &next }) addAndMakeVisible (*b);
    setPage (proc.presetPage);
}

PresetBar::~PresetBar() = default;

int PresetBar::page() const { return proc.presetPage; }

void PresetBar::setPage (int pg)
{
    proc.presetPage = jlimit (0, kNumPresets / kPresetsPerPage - 1, pg);
    for (int i = 0; i < kPresetsPerPage; ++i) buttons[(size_t) i]->index = proc.presetPage * kPresetsPerPage + i;
    prev.setVisible (proc.presetPage > 0);
    next.setEnabled (proc.presetPage < kNumPresets / kPresetsPerPage - 1);
    repaint();
}

void PresetBar::paint (Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat(), col::panel.withAlpha (0.55f), col::border, 12.0f);
    g.setColour (col::text);
    g.setFont (sixFont (22.0f));
    g.drawText ("Morph Presets", 24, 0, 180, getHeight(), Justification::centredLeft);
    g.setColour (col::border.brighter (0.3f));
    g.fillRect (205, 16, 1, getHeight() - 32);
    g.fillRect (1462, 16, 1, getHeight() - 32);
    if (page() > 0)
    {
        g.setColour (col::muted);
        g.setFont (sixFont (12.0f));
        g.drawText ("p" + String (page() + 1), 1520, getHeight() - 16, 30, 14, Justification::centred);
    }
}

void PresetBar::resized()
{
    for (int i = 0; i < kPresetsPerPage; ++i)
        buttons[(size_t) i]->setBounds (228 + i * 153, 10, 143, 38);
    add.setBounds (1476, 13, 34, 32);
    prev.setVisible (page() > 0);
    if (page() > 0) { prev.setBounds (1514, 13, 24, 32); next.setBounds (1540, 13, 24, 32); }
    else next.setBounds (1522, 13, 30, 32);
}

void PresetBar::refresh()
{
    resized();
    for (auto& b : buttons) b->repaint();
}
}
