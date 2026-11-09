#include "PluginWindows.h"

namespace six
{
using namespace juce;

class PluginWindowManager::Window : public DocumentWindow
{
public:
    Window (PluginWindowManager& m, HostedInstance& i, const String& title)
        : DocumentWindow (title, Colour (0xff0b1530), DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          manager (m), inst (&i)
    {
        setUsingNativeTitleBar (true);
        AudioProcessorEditor* ed = nullptr;
        if (i.plugin().hasEditor()) ed = i.plugin().createEditorIfNeeded();
        if (ed == nullptr) ed = new GenericAudioProcessorEditor (i.plugin());   // plug-in without a GUI
        ed->setVisible (true);
        setContentOwned (ed, true);
        setResizable (ed->isResizable(), false);
       #if JUCE_MAC || JUCE_WINDOWS
        setAlwaysOnTop (true);   // stay above Live's main window like Live's own plug-in windows
       #endif
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        toFront (true);
    }

    ~Window() override
    {
        clearContentComponent();   // deletes the editor before the plug-in can go away
    }

    void closeButtonPressed() override
    {
        auto* m = &manager;
        auto* h = inst;
        MessageManager::callAsync ([m, h] { m->closeFor (h); });
    }

    PluginWindowManager& manager;
    HostedInstance* inst;
};

PluginWindowManager::PluginWindowManager() = default;
PluginWindowManager::~PluginWindowManager() { closeAll(); }

bool PluginWindowManager::isOpen (HostedInstance* inst) const
{
    for (auto& w : windows) if (w->inst == inst) return true;
    return false;
}

void PluginWindowManager::open (HostedInstance* inst, const String& title)
{
    if (inst == nullptr) return;
    for (auto& w : windows) if (w->inst == inst) { w->toFront (true); return; }
    windows.push_back (std::make_unique<Window> (*this, *inst, title));
    if (onChange) onChange();
}

void PluginWindowManager::toggle (HostedInstance* inst, const String& title)
{
    if (isOpen (inst)) closeFor (inst);
    else open (inst, title);
}

void PluginWindowManager::closeFor (HostedInstance* inst)
{
    const auto before = windows.size();
    windows.erase (std::remove_if (windows.begin(), windows.end(), [inst] (auto& w) { return w->inst == inst; }), windows.end());
    if (before != windows.size() && onChange) onChange();
}

void PluginWindowManager::closeAll()
{
    windows.clear();
}
}
