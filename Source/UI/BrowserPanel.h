#pragma once
#include "Widgets.h"

namespace six
{
// Internal plug-in browser: your own folders on the left (Reverb, Delay...), search and
// results on the right. Drag a result onto a slot, or onto a folder to file it there.
class BrowserPanel : public juce::Component,
                     public Refreshable,
                     private juce::ListBoxModel,
                     private juce::ChangeListener
{
public:
    explicit BrowserPanel (SIXAudioProcessor& p);
    ~BrowserPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override;
    void visibilityChanged() override;

    std::function<void (const juce::PluginDescription&)> onLoad;   // double-click
    std::function<void()> onClose;

    int selectedFolder = -1;   // -1 = All effects
    void selectFolder (int f);
    PluginCatalog& catalog();

private:
    class FolderList;

    int getNumRows() override { return results.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override { loadRow (row); }
    void loadRow (int row);
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { updateResults(); }
    void updateResults();
    void showRowMenu (int row);

    SIXAudioProcessor& proc;
    juce::TextEditor search;
    juce::TextButton rescan { "Rescan" }, close { "Close" };
    juce::ListBox list { "plugins", this };
    juce::Label status;
    std::unique_ptr<FolderList> folderList;
    juce::Array<juce::PluginDescription> results;
};
}
