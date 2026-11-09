#include "BrowserPanel.h"

namespace six
{
using namespace juce;

// ------------------------------------------------------------------------------ folder column
class BrowserPanel::FolderList : public Component, public DragAndDropTarget
{
public:
    explicit FolderList (BrowserPanel& b) : owner (b) {}
    int rowH = 38;

    int rowAt (int y) const { return y / rowH; }                         // 0 = All, 1..n folders, n+1 = "+ New"
    int numRows() const { return owner.catalog().numFolders() + 2; }

    void paint (Graphics& g) override
    {
        auto& cat = owner.catalog();
        g.setColour (col::bgBottom.withAlpha (0.6f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);

        for (int r = 0; r < numRows(); ++r)
        {
            auto row = Rectangle<int> (0, r * rowH, getWidth(), rowH).reduced (6, 3).toFloat();
            const int f = r - 1;
            const bool isNew = r == numRows() - 1;
            const bool sel = ! isNew && owner.selectedFolder == f;
            const bool drop = dropRow == r && r >= 1 && ! isNew;
            if (sel)  { g.setColour (col::teal.withAlpha (0.18f)); g.fillRoundedRectangle (row, 7.0f); }
            if (drop) { g.setColour (col::teal); g.drawRoundedRectangle (row, 7.0f, 1.5f); }

            g.setFont (sixFont (16.0f));
            if (isNew)
            {
                g.setColour (col::muted);
                g.drawText ("+  New folder", row.reduced (10, 0), Justification::centredLeft);
                continue;
            }
            g.setColour (sel ? col::teal : col::text);
            const String name = r == 0 ? String ("All effects") : cat.folderName (f);
            const int count = r == 0 ? cat.getEffects().size() : cat.folderSize (f);
            g.drawFittedText (name, row.reduced (10, 0).withTrimmedRight (34).toNearestInt(), Justification::centredLeft, 1);
            g.setColour (col::dim);
            g.setFont (sixFont (13.0f));
            g.drawText (String (count), row.reduced (10, 0), Justification::centredRight);
        }
    }

    void mouseUp (const MouseEvent& e) override
    {
        if (editor != nullptr) return;
        const int r = rowAt (e.y);
        if (r < 0 || r >= numRows()) return;
        if (r == numRows() - 1) { startEdit (-1); return; }
        const int f = r - 1;
        if (e.mods.isPopupMenu() && f >= 0) { showMenu (f); return; }
        owner.selectFolder (f);
    }

    void mouseDoubleClick (const MouseEvent& e) override
    {
        const int f = rowAt (e.y) - 1;
        if (f >= 0 && f < owner.catalog().numFolders()) startEdit (f);
    }

    void showMenu (int f)
    {
        PopupMenu m;
        m.addSectionHeader (owner.catalog().folderName (f));
        m.addItem (1, "Rename...");
        m.addItem (2, "Move up", f > 0);
        m.addItem (3, "Move down", f < owner.catalog().numFolders() - 1);
        m.addSeparator();
        m.addItem (4, "Delete folder (plug-ins stay installed)");
        SafePointer<FolderList> safe (this);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [safe, f] (int r)
        {
            if (safe == nullptr) return;
            auto& cat = safe->owner.catalog();
            if (r == 1) safe->startEdit (f);
            if (r == 2) { cat.moveFolder (f, f - 1); safe->owner.selectFolder (f - 1); }
            if (r == 3) { cat.moveFolder (f, f + 1); safe->owner.selectFolder (f + 1); }
            if (r == 4) { cat.deleteFolder (f); safe->owner.selectFolder (-1); }
            safe->repaint();
        });
    }

    // f = -1 creates a new folder
    void startEdit (int f)
    {
        editor = std::make_unique<TextEditor>();
        editor->setFont (sixFont (16.0f));
        editor->setIndents (8, 8);
        editor->setTextToShowWhenEmpty ("Folder name (e.g. Reverb)", col::dim);
        if (f >= 0) editor->setText (owner.catalog().folderName (f), false);
        const int row = f >= 0 ? f + 1 : numRows() - 1;
        editor->setBounds (Rectangle<int> (0, row * rowH, getWidth(), rowH).reduced (4, 2));
        addAndMakeVisible (*editor);
        editor->grabKeyboardFocus();
        editor->selectAll();
        auto commit = [this, f] (bool apply)
        {
            if (editor == nullptr) return;
            const auto name = editor->getText().trim();
            auto& cat = owner.catalog();
            if (apply && name.isNotEmpty())
            {
                if (f >= 0) cat.renameFolder (f, name);
                else owner.selectFolder (cat.createFolder (name));
            }
            MessageManager::callAsync ([safe = SafePointer<FolderList> (this)] { if (safe != nullptr) { safe->editor.reset(); safe->repaint(); } });
        };
        editor->onReturnKey = [commit] { commit (true); };
        editor->onEscapeKey = [commit] { commit (false); };
        editor->onFocusLost = [commit] { commit (true); };
    }

    // drag a plug-in from the results onto a folder
    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("six-plugin:"); }
    void itemDragMove (const SourceDetails& d) override { dropRow = rowAt (d.localPosition.y); repaint(); }
    void itemDragExit (const SourceDetails&) override { dropRow = -1; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        const int f = rowAt (d.localPosition.y) - 1;
        dropRow = -1;
        auto& cat = owner.catalog();
        if (f >= 0 && f < cat.numFolders())
            if (auto desc = cat.findByIdentifier (d.description.toString().fromFirstOccurrenceOf ("six-plugin:", false, false)))
                cat.addToFolder (f, *desc);
        repaint();
    }

    BrowserPanel& owner;
    int dropRow = -1;
    std::unique_ptr<TextEditor> editor;
};

// ------------------------------------------------------------------------------ panel
BrowserPanel::BrowserPanel (SIXAudioProcessor& p) : proc (p)
{
    search.setTextToShowWhenEmpty ("Search effects (name, maker, VST3/AU)...", col::dim);
    search.setFont (sixFont (17.0f));
    search.setIndents (12, 9);
    search.onTextChange = [this] { updateResults(); };
    search.onReturnKey = [this] { if (results.size() > 0) loadRow (jmax (0, list.getSelectedRow())); };
    addAndMakeVisible (search);

    rescan.onClick = [this] { catalog().startScan (true); refresh(); };
    close.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (rescan);
    addAndMakeVisible (close);

    status.setFont (sixFont (14.0f));
    status.setColour (Label::textColourId, col::muted);
    addAndMakeVisible (status);

    list.setRowHeight (44);
    list.setColour (ListBox::outlineColourId, Colours::transparentBlack);
    addAndMakeVisible (list);

    folderList = std::make_unique<FolderList> (*this);
    addAndMakeVisible (*folderList);

    catalog().changes.addChangeListener (this);
    updateResults();
}

BrowserPanel::~BrowserPanel() { catalog().changes.removeChangeListener (this); }

PluginCatalog& BrowserPanel::catalog() { return proc.host->catalog; }

void BrowserPanel::selectFolder (int f)
{
    selectedFolder = jlimit (-1, catalog().numFolders() - 1, f);
    updateResults();
    folderList->repaint();
}

void BrowserPanel::visibilityChanged()
{
    if (! isVisible()) return;
    auto& c = catalog();
    c.reloadFolders();   // another SIX instance may have edited the folders
    if (c.getEffects().isEmpty() && ! c.isScanning()) c.startScan (false);
    selectFolder (selectedFolder);
    search.grabKeyboardFocus();
}

void BrowserPanel::paint (Graphics& g)
{
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (getLocalBounds().toFloat().translated (-4, 6), 14.0f);
    drawPanel (g, getLocalBounds().toFloat(), Colour (0xff0f1c42), col::border.brighter (0.3f), 14.0f);
    g.setColour (col::text);
    g.setFont (sixFont (22.0f));
    g.drawText ("Browser", 22, 14, 200, 30, Justification::centredLeft);
    g.setColour (col::muted);
    g.setFont (sixFont (14.0f));
    g.drawFittedText ("Drag an effect onto a slot to load it, or onto a folder to file it. Right-click for more.",
                      { 16, getHeight() - 38, getWidth() - 32, 32 }, Justification::centredLeft, 2);
}

void BrowserPanel::resized()
{
    close.setBounds (getWidth() - 96, 12, 80, 34);
    rescan.setBounds (getWidth() - 186, 12, 82, 34);
    if (getWidth() < 520)   // docked, narrow: folders on top, results below
    {
        folderList->rowH = 30;
        const int fh = jmin (folderList->numRows() * 30, 30 * 6);
        folderList->setBounds (12, 54, getWidth() - 24, fh);
        const int y = 54 + fh + 8;
        search.setBounds (12, y, getWidth() - 24, 36);
        status.setBounds (12, y + 38, getWidth() - 24, 20);
        list.setBounds (6, y + 60, getWidth() - 12, getHeight() - (y + 60) - 40);
        return;
    }
    folderList->rowH = 38;
    const int fw = 190;
    folderList->setBounds (14, 56, fw, getHeight() - 56 - 44);
    const int x = 14 + fw + 12;
    search.setBounds (x, 56, getWidth() - x - 16, 38);
    status.setBounds (x, 98, getWidth() - x - 16, 22);
    list.setBounds (x - 6, 124, getWidth() - x - 4, getHeight() - 124 - 44);
}

void BrowserPanel::refresh()
{
    auto& c = catalog();
    if (c.isScanning())
        status.setText (String (roundToInt (c.getScanProgress() * 100)) + "%  " + c.getScanStatus(), dontSendNotification);
    else
    {
        const auto where = selectedFolder >= 0 ? "in " + c.folderName (selectedFolder) : String ("(VST3 + Audio Units)");
        String s = String (results.size()) + " effects " + where;
        if (selectedFolder >= 0 && c.folderSize (selectedFolder) == 0 && search.isEmpty())
            s = "Empty folder - drag effects here from \"All effects\", or right-click an effect > Add to folder";
        status.setText (s, dontSendNotification);
    }
}

void BrowserPanel::updateResults()
{
    results = catalog().search (search.getText(), selectedFolder);
    list.updateContent();
    list.repaint();
    if (folderList != nullptr) { folderList->repaint(); resized(); }
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

    // the folders this effect is filed in
    StringArray in;
    auto& cat = proc.host->catalog;
    for (int f = 0; f < cat.numFolders(); ++f) if (cat.isInFolder (f, d)) in.add (cat.folderName (f));
    auto sub = d.manufacturerName;
    if (! in.isEmpty()) sub += String (CharPointer_UTF8 ("  \xc2\xb7  ")) + in.joinIntoString (", ");
    g.drawText (sub, r.reduced (12, 0).withTrimmedTop (r.getHeight() * 0.58f).withTrimmedRight (64), Justification::topLeft);

    auto badge = r.removeFromRight (64).reduced (8, 12);
    g.setColour (col::border.brighter (0.2f));
    g.drawRoundedRectangle (badge, 5.0f, 1.0f);
    g.setFont (sixFont (12.0f));
    g.drawText (d.pluginFormatName == "AudioUnit" ? "AU" : d.pluginFormatName, badge, Justification::centred);
}

void BrowserPanel::listBoxItemDoubleClicked (int row, const MouseEvent&) { loadRow (row); }

void BrowserPanel::listBoxItemClicked (int row, const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) showRowMenu (row);
}

void BrowserPanel::showRowMenu (int row)
{
    if (! isPositiveAndBelow (row, results.size())) return;
    const auto d = results[row];
    auto& cat = catalog();
    PopupMenu add;
    for (int f = 0; f < cat.numFolders(); ++f)
        add.addItem (100 + f, cat.folderName (f), ! cat.isInFolder (f, d), cat.isInFolder (f, d));
    if (cat.numFolders() > 0) add.addSeparator();
    add.addItem (2, "New folder...");

    PopupMenu m;
    m.addSectionHeader (d.name);
    m.addItem (1, "Load into the selected channel");
    m.addSubMenu ("Add to folder", add);
    if (selectedFolder >= 0) m.addItem (3, "Remove from \"" + cat.folderName (selectedFolder) + "\"");
    SafePointer<BrowserPanel> safe (this);
    m.showMenuAsync (PopupMenu::Options(), [safe, d] (int r)
    {
        if (safe == nullptr) return;
        auto& c = safe->catalog();
        if (r == 1 && safe->onLoad) safe->onLoad (d);
        if (r >= 100) c.addToFolder (r - 100, d);
        if (r == 2)
        {
            const int f = c.createFolder ("New folder");
            c.addToFolder (f, d);
            safe->folderList->startEdit (f);   // type the name right away
        }
        if (r == 3) c.removeFromFolder (safe->selectedFolder, d);
        safe->updateResults();
    });
}

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
