#include "DeviceChain.h"
#include "PluginWindows.h"

namespace six
{
using namespace juce;

// ------------------------------------------------------------------------------ DeviceChain
DeviceChain::DeviceChain (SIXAudioProcessor& p) : proc (p)
{
    for (int s = 0; s < kNumSlots; ++s)
    {
        slots[(size_t) s] = std::make_unique<DeviceSlot> (*this, s);
        addAndMakeVisible (*slots[(size_t) s]);
    }
}

DeviceChain::~DeviceChain() = default;

void DeviceChain::setChannel (int ch)
{
    channel = ch;
    refresh();
    repaint();
}

void DeviceChain::paint (Graphics& g)
{
    g.setColour (col::border);
    g.fillRect (30, 0, getWidth() - 60, 1);
    g.setFont (sixFont (24.0f));
    g.setColour (col::teal);
    const auto num = twoDigits (channel + 1);
    g.drawText (num, 44, 16, 40, 34, Justification::centredLeft);
    const auto nm = proc.host->channelNames[(size_t) channel];
    auto x = 88;
    if (nm.isNotEmpty())
    {
        g.setColour (col::text);
        g.drawText (nm, x, 16, 300, 34, Justification::centredLeft);
        x += GlyphArrangement::getStringWidthInt (sixFont (24.0f), nm) + 18;
    }
    g.setColour (col::muted);
    g.setFont (sixFont (19.0f));
    g.drawText ("/   Device Chain", x, 16, 300, 34, Justification::centredLeft);
}

void DeviceChain::resized()
{
    const int w = (getWidth() - 80 - 3 * 20) / 4;
    for (int s = 0; s < kNumSlots; ++s)
        slots[(size_t) s]->setBounds (40 + s * (w + 20), 64, w, getHeight() - 84);
}

void DeviceChain::refresh()
{
    for (auto& s : slots) s->refresh();
}

void DeviceChain::loadDescription (int s, const PluginDescription& d)
{
    String err;
    if (! proc.host->loadIntoSlot (channel, s, d, err))
    {
        if (onMessage) onMessage (err);
    }
    refresh();
}

void DeviceChain::chooseAndLoad (int s, const Array<PluginDescription>& types, Component* target)
{
    if (types.isEmpty()) return;
    if (types.size() == 1) { loadDescription (s, types[0]); return; }
    PopupMenu m;
    m.addSectionHeader ("This bundle contains several effects");
    for (int i = 0; i < types.size(); ++i) m.addItem (i + 1, types[i].name + "  (" + types[i].pluginFormatName + ")");
    SafePointer<DeviceChain> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (target), [safe, types, s] (int r)
    {
        if (safe != nullptr && r > 0) safe->loadDescription (s, types[r - 1]);
    });
}

// ------------------------------------------------------------------------------ DeviceSlot
DeviceSlot::DeviceSlot (DeviceChain& o, int s) : chain (o), slot (s)
{
    power.setClickingTogglesState (false);
    power.setTooltip ("Bypass");
    power.onClick = [this]
    {
        const int c = bypassControl (chain.getChannel(), slot);
        const bool byp = chain.proc.engine.effective[(size_t) c].load() > 0.5f;
        ParamRefs::setWithGesture (chain.proc.params.controls[(size_t) c], byp ? 0.0f : 1.0f);
    };
    edit.onClick = [this]
    {
        if (auto* h = model().inst) host().windows().toggle (h, title());
        refresh();
    };
    mapBtn.setTooltip ("Map a plug-in parameter to a SIX automation lane");
    mapBtn.onClick = [this] { showMapMenu(); };
    remove.setTooltip ("Remove");
    remove.onClick = [this] { host().removeSlot (chain.getChannel(), slot); chain.refresh(); };
    for (auto* b : { &power, &edit, &mapBtn, &remove }) addChildComponent (*b);
}

HostManager& DeviceSlot::host() const { return *chain.proc.host; }
const HostManager::SlotModel& DeviceSlot::model() const { return host().slot (chain.getChannel(), slot); }
String DeviceSlot::title() const
{
    return "CH " + twoDigits (chain.getChannel() + 1) + " FX" + String (slot + 1) + " - " + model().displayName();
}

void DeviceSlot::refresh()
{
    const auto& m = model();
    const bool loaded = m.inst != nullptr;
    for (auto* b : { &edit, &mapBtn, &power }) b->setVisible (loaded);
    remove.setVisible (m.occupied());
    if (loaded)
    {
        const bool byp = chain.proc.engine.effective[(size_t) bypassControl (chain.getChannel(), slot)].load() > 0.5f;
        power.setButtonText (byp ? "Off" : "On");
        power.setToggleState (! byp, dontSendNotification);
        const bool open = host().windows().isOpen (m.inst);
        edit.setButtonText (open ? "Close" : "Edit");
        int mapped = 0;
        for (int k = 0; k < kNumMaps; ++k) if (host().map (k).uid == m.uid && m.uid != 0) ++mapped;
        mapBtn.setButtonText (mapped > 0 ? "Map " + String (mapped) : "Map");
        mapBtn.setToggleState (host().isLearnArmed(), dontSendNotification);
    }
    repaint();
}

void DeviceSlot::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto& m = model();

    if (! m.occupied())
    {
        Path border;
        border.addRoundedRectangle (r, 10.0f);
        Path dashed;
        const float dash[] = { 5.0f, 5.0f };
        PathStrokeType (1.2f).createDashedStroke (dashed, border, dash, 2);
        if (highlight) { g.setColour (col::teal.withAlpha (0.12f)); g.fillRoundedRectangle (r, 10.0f); }
        g.setColour (highlight ? col::teal : col::border.brighter (0.35f));
        g.fillPath (dashed);
        g.setColour (highlight ? col::teal : col::text.withAlpha (0.85f));
        const auto c = r.getCentre();
        g.fillRect (Rectangle<float> (c.x - 11, c.y - 22, 22, 2));
        g.fillRect (Rectangle<float> (c.x - 1, c.y - 32, 2, 22));
        g.setFont (sixFont (18.0f).italicised());
        g.setColour (highlight ? col::teal : col::muted);
        g.drawText (highlight ? "Release to load" : "Drop device here", r.withTrimmedTop (r.getHeight() * 0.5f).withHeight (30), Justification::centred);
        return;
    }

    const bool byp = chain.proc.engine.effective[(size_t) bypassControl (chain.getChannel(), slot)].load() > 0.5f;
    drawPanel (g, r, highlight ? col::teal.withAlpha (0.15f) : col::panelHi, highlight ? col::teal : col::border.brighter (0.25f), 10.0f);

    g.setColour (col::dim);
    g.setFont (sixFont (13.0f));
    g.drawText ("FX " + String (slot + 1), r.reduced (14, 10).removeFromTop (16), Justification::topRight);

    if (m.inst != nullptr)
    {
        g.setColour (byp ? col::dim : col::text);
        g.setFont (sixFont (21.0f, true));
        g.drawFittedText (m.inst->description().name, r.reduced (16, 0).withTop (r.getY() + 14).withHeight (28).toNearestInt(), Justification::centredLeft, 1);
        g.setFont (sixFont (14.0f));
        g.setColour (col::muted);
        auto info = m.inst->description().manufacturerName + String (CharPointer_UTF8 ("  \xc2\xb7  ")) + m.inst->description().pluginFormatName
                  + String (CharPointer_UTF8 ("  \xc2\xb7  ")) + m.inst->layoutName();
        if (const int lat = m.inst->latency(); lat > 0) info += String (CharPointer_UTF8 ("  \xc2\xb7  ")) + String (lat) + " smp";
        g.drawFittedText (info, r.reduced (16, 0).withTop (r.getY() + 42).withHeight (20).toNearestInt(), Justification::centredLeft, 1);
        if (byp)
        {
            g.setColour (col::warn.withAlpha (0.8f));
            g.drawText ("Bypassed", r.reduced (16, 0).withTop (r.getY() + 62).withHeight (18), Justification::centredLeft);
        }
    }
    else
    {
        g.setColour (col::text);
        g.setFont (sixFont (21.0f, true));
        g.drawFittedText (m.missingDesc.name, r.reduced (16, 0).withTop (r.getY() + 14).withHeight (28).toNearestInt(), Justification::centredLeft, 1);
        g.setFont (sixFont (14.0f));
        g.setColour (col::warn);
        g.drawFittedText (String (CharPointer_UTF8 ("Not installed \xe2\x80\x94 settings kept for reload")),
                          r.reduced (16, 0).withTop (r.getY() + 44).withHeight (20).toNearestInt(), Justification::centredLeft, 1);
    }
}

void DeviceSlot::resized()
{
    auto r = getLocalBounds().reduced (12, 10).removeFromBottom (30);
    power.setBounds (r.removeFromLeft (48));
    r.removeFromLeft (8);
    edit.setBounds (r.removeFromLeft (70));
    r.removeFromLeft (8);
    mapBtn.setBounds (r.removeFromLeft (70));
    remove.setBounds (r.removeFromRight (32));
}

void DeviceSlot::mouseDown (const MouseEvent& e)
{
    dragStarted = false;
    if (e.mods.isPopupMenu()) showMenu();
}

void DeviceSlot::mouseDrag (const MouseEvent& e)
{
    if (dragStarted || e.mods.isPopupMenu() || ! model().occupied() || e.getDistanceFromDragStart() < 8) return;
    if (auto* c = DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragStarted = true;
        c->startDragging ("six-slot:" + String (chain.getChannel()) + ":" + String (slot), this,
                          ScaledImage (createComponentSnapshot (getLocalBounds(), true, 0.6f)));
    }
}

void DeviceSlot::mouseUp (const MouseEvent& e)
{
    if (dragStarted || e.mods.isPopupMenu() || ! getLocalBounds().contains (e.getPosition())) return;
    const auto& m = model();
    if (m.inst != nullptr) { host().windows().toggle (m.inst, title()); refresh(); }   // click = native editor
    else if (! m.occupied() && chain.onBrowse) chain.onBrowse();
}

void DeviceSlot::showMenu()
{
    const auto& m = model();
    PopupMenu menu;
    if (m.inst != nullptr)
    {
        menu.addItem (1, host().windows().isOpen (m.inst) ? "Close editor" : "Open editor");
        menu.addItem (2, "Bypass", true, chain.proc.engine.effective[(size_t) bypassControl (chain.getChannel(), slot)].load() > 0.5f);
        menu.addItem (3, "Map / Learn...");
        menu.addSeparator();
    }
    menu.addItem (4, "Browse...");
    if (m.occupied()) menu.addItem (5, "Remove");
    SafePointer<DeviceSlot> safe (this);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [safe] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1) safe->edit.triggerClick();
        if (r == 2) safe->power.triggerClick();
        if (r == 3) safe->showMapMenu();
        if (r == 4 && safe->chain.onBrowse) safe->chain.onBrowse();
        if (r == 5) safe->remove.triggerClick();
    });
}

void DeviceSlot::showMapMenu()
{
    auto* inst = model().inst;
    if (inst == nullptr) return;
    auto& hm = host();
    PopupMenu menu;
    menu.addSectionHeader (inst->description().name + String (CharPointer_UTF8 (" \xe2\x80\x94 automation lanes")));
    menu.addItem (1, hm.isLearnArmed() ? "Cancel Learn" : "Learn: move a control in the plug-in window");

    auto& ps = inst->plugin().getParameters();
    PopupMenu list;
    PopupMenu* current = &list;
    PopupMenu chunk;
    const bool chunked = ps.size() > 40;
    for (int i = 0; i < ps.size(); ++i)
    {
        auto* p = ps[i];
        const bool automatable = p->isAutomatable();
        const int mapped = hm.findMap (inst->placement(), i);
        String text = p->getName (60);
        if (text.isEmpty()) text = "Param " + String (i + 1);
        if (mapped >= 0) text += String (CharPointer_UTF8 ("  \xe2\x86\x92 Map ")) + twoDigits (mapped + 1);
        if (! automatable) text += "  (not automatable by the plug-in)";
        (chunked ? chunk : *current).addItem (1000 + i, text, automatable && mapped < 0, mapped >= 0);
        if (chunked && ((i + 1) % 40 == 0 || i == ps.size() - 1))
        {
            list.addSubMenu (String (i / 40 * 40 + 1) + String (CharPointer_UTF8 (" \xe2\x80\x93 ")) + String (i + 1), chunk);
            chunk = PopupMenu();
        }
    }
    if (ps.isEmpty()) list.addItem (9, "This plug-in exposes no automatable parameters", false);
    menu.addSubMenu ("Map parameter", list);

    PopupMenu current2;
    bool any = false;
    for (int k = 0; k < kNumMaps; ++k)
        if (hm.map (k).uid == inst->placement())
        {
            current2.addItem (500 + k, "Map " + twoDigits (k + 1) + String (CharPointer_UTF8 (" \xe2\x86\x90 ")) + hm.map (k).paramName + "   (clear)");
            any = true;
        }
    if (any) menu.addSubMenu ("Mapped lanes", current2);
    menu.addSeparator();
    menu.addItem (10, "Lanes appear in Live as \"Map 01..32\" (Configure / automation)", false);

    SafePointer<DeviceSlot> safe (this);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&mapBtn), [safe, inst] (int r)
    {
        if (safe == nullptr) return;
        auto& h = safe->host();
        if (safe->model().inst != inst) return;
        if (r == 1)
        {
            if (h.isLearnArmed()) h.cancelLearn();
            else
            {
                h.armLearn (inst->placement());
                if (! h.windows().isOpen (inst)) h.windows().open (inst, safe->title());
                h.toast ("Learn armed: move a control in the plug-in window.");
            }
        }
        else if (r >= 1000)
        {
            const int k = h.assignMap (inst, r - 1000);
            if (k >= 0) h.toast ("Mapped to Map " + twoDigits (k + 1) + " - choose it in Live's Configure / automation chooser.");
        }
        else if (r >= 500) h.clearMap (r - 500);
        safe->refresh();
    });
}

bool DeviceSlot::isInterestedInFileDrag (const StringArray& files)
{
    // Accept everything so a drop from any source (Finder, Ableton's browser...) is at least reported.
    return ! files.isEmpty();
}

void DeviceSlot::filesDropped (const StringArray& files, int, int)
{
    setHighlight (false);
    const File f (files[0].trimCharactersAtEnd ("/"));
    const bool bundle = f.hasFileExtension (".vst3") || f.hasFileExtension (".component");
    String err;
    auto types = bundle ? host().catalog.findTypesForFile (f, err) : Array<PluginDescription>();
    if (types.isEmpty() && ! bundle)
    {
        String how;
        types = host().catalog.resolveDroppedText (f.getFileNameWithoutExtension(), how);   // e.g. a named item
        if (types.isEmpty())
        {
            if (chain.onMessage) chain.onMessage ("Received file \"" + f.getFileName() + "\" - not a VST3/AU bundle. Use Browse or drag the plug-in from Finder.");
            host().logDrag ("Unrecognised file drop: " + f.getFullPathName());
            return;
        }
        host().logDrag ("File drop resolved by name: " + f.getFullPathName());
    }
    if (types.isEmpty()) { if (chain.onMessage) chain.onMessage (err); return; }
    if (bundle) host().logDrag ("Finder bundle drop: " + f.getFullPathName());
    chain.chooseAndLoad (slot, types, this);
}

bool DeviceSlot::isInterestedInDragSource (const SourceDetails& d)
{
    const auto s = d.description.toString();
    return s.startsWith ("six-plugin:") || s.startsWith ("six-slot:");
}

void DeviceSlot::itemDropped (const SourceDetails& d)
{
    setHighlight (false);
    const auto s = d.description.toString();
    if (s.startsWith ("six-plugin:"))
    {
        if (auto desc = host().catalog.findByIdentifier (s.fromFirstOccurrenceOf ("six-plugin:", false, false)))
            chain.loadDescription (slot, *desc);
    }
    else if (s.startsWith ("six-slot:"))
    {
        auto parts = StringArray::fromTokens (s.fromFirstOccurrenceOf ("six-slot:", false, false), ":", "");
        const int ch = parts[0].getIntValue(), from = parts[1].getIntValue();
        if (ch != chain.getChannel()) { if (chain.onMessage) chain.onMessage ("Drag within the same channel to re-order."); return; }
        host().moveSlot (ch, from, slot);
        chain.refresh();
    }
}

void DeviceSlot::textDropped (const String& text, int, int)
{
    setHighlight (false);
    String how;
    auto types = host().catalog.resolveDroppedText (text, how);
    if (types.isEmpty())
    {
        host().logDrag ("Unrecognised text drop: " + text.substring (0, 200));
        if (chain.onMessage)
            chain.onMessage ("Dropped text not recognised as a plug-in: \"" + text.substring (0, 80) + "\". Use Browse or drag the .vst3 from Finder.");
        return;
    }
    host().logDrag ("Text drop (" + how + "): " + text.substring (0, 120));
    chain.chooseAndLoad (slot, types, this);
}
}
