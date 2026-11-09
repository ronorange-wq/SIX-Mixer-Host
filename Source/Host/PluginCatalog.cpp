#include "PluginCatalog.h"

namespace six
{
using namespace juce;

PluginCatalog::PluginCatalog (AudioPluginFormatManager& fm) : Thread ("SIX plug-in scan"), formats (fm)
{
    load();
    loadFolders();
}

PluginCatalog::~PluginCatalog()
{
    stopTimer();
    stopThread (8000);
}

File PluginCatalog::dataDir()
{
    auto d = File::getSpecialLocation (File::userApplicationDataDirectory)
#if JUCE_MAC
                 .getChildFile ("Application Support")
#endif
                 .getChildFile ("SIX Mixer Host");
    d.createDirectory();
    return d;
}

void PluginCatalog::load()
{
    if (auto xml = parseXML (dataDir().getChildFile ("PluginCatalog.xml")))
        known.recreateFromXml (*xml);
}

void PluginCatalog::save()
{
    if (auto xml = known.createXml())
        xml->writeTo (dataDir().getChildFile ("PluginCatalog.xml"));
}

bool PluginCatalog::isSelf (const PluginDescription& d)
{
    if (d.name.equalsIgnoreCase ("SIX") && d.manufacturerName.equalsIgnoreCase ("SIX Audio")) return true;
    const auto me = File::getSpecialLocation (File::currentExecutableFile);
    const File f (d.fileOrIdentifier);
    if (File::isAbsolutePath (d.fileOrIdentifier) && (me == f || me.isAChildOf (f))) return true;
    return false;
}

void PluginCatalog::startScan (bool rescanAll)
{
    if (scanning.load()) return;
    rescanRequested = rescanAll;
    if (rescanAll) known.clear();
    scanning = true;
    progress = 0.0f;
    { const ScopedLock sl (statusLock); status = "Scanning..."; }
    startThread();
    startTimerHz (20);
}

String PluginCatalog::getScanStatus() const
{
    const ScopedLock sl (statusLock);
    return status;
}

void PluginCatalog::run()
{
    const auto pedal = dataDir().getChildFile ("ScanCrashes.txt");
    for (auto* f : formats.getFormats())
    {
        if (threadShouldExit()) break;
        if (f->getName() != "VST3") continue;
        PluginDirectoryScanner scanner (known, *f, f->getDefaultLocationsToSearch(), true, pedal, false);
        String name;
        while (! threadShouldExit())
        {
            { const ScopedLock sl (statusLock); status = "VST3: " + scanner.getNextPluginFileThatWillBeScanned(); }
            const bool more = scanner.scanNextFile (! rescanRequested, name);
            progress = jlimit (0.0f, 0.5f, scanner.getProgress() * 0.5f);
            listChanged = true;
            if (! more) break;
        }
    }
}

void PluginCatalog::timerCallback()
{
    if (isThreadRunning()) { if (listChanged.exchange (false)) changes.sendChangeMessage(); return; }

    // Audio Units are scanned on the message thread, one bundle per tick.
    if (mtScanner == nullptr)
    {
        for (auto* f : formats.getFormats())
            if (f->getName() == "AudioUnit")
                mtScanner = std::make_unique<PluginDirectoryScanner> (known, *f, f->getDefaultLocationsToSearch(), true,
                                                                       dataDir().getChildFile ("ScanCrashes.txt"), true);
        if (mtScanner == nullptr) goto finished;
    }

    {
        String name;
        { const ScopedLock sl (statusLock); status = "Audio Units: " + mtScanner->getNextPluginFileThatWillBeScanned(); }
        const bool more = mtScanner->scanNextFile (! rescanRequested, name);
        progress = 0.5f + 0.5f * mtScanner->getProgress();
        changes.sendChangeMessage();
        if (more) return;
    }

finished:
    mtScanner.reset();
    stopTimer();
    scanning = false;
    progress = 1.0f;
    { const ScopedLock sl (statusLock); status = String (getEffects().size()) + " effects"; }
    save();
    changes.sendChangeMessage();
}

Array<PluginDescription> PluginCatalog::getEffects() const
{
    Array<PluginDescription> r;
    for (auto& d : known.getTypes())
        if (isEffect (d) && ! isSelf (d)) r.add (d);
    std::sort (r.begin(), r.end(), [] (const PluginDescription& a, const PluginDescription& b)
    {
        const int c = a.name.compareIgnoreCase (b.name);
        return c != 0 ? c < 0 : a.pluginFormatName < b.pluginFormatName;
    });
    return r;
}

Array<PluginDescription> PluginCatalog::search (const String& text) const
{
    auto all = getEffects();
    const auto words = StringArray::fromTokens (text.trim(), " ", "");
    if (words.isEmpty()) return all;
    Array<PluginDescription> r;
    for (auto& d : all)
    {
        const auto hay = d.name + " " + d.manufacturerName + " " + d.category + " " + d.pluginFormatName;
        bool ok = true;
        for (auto& w : words) if (! hay.containsIgnoreCase (w)) { ok = false; break; }
        if (ok) r.add (d);
    }
    return r;
}

// ------------------------------------------------------------------------------ folders
void PluginCatalog::loadFolders()
{
    folders.clear();
    if (auto xml = parseXML (dataDir().getChildFile ("Folders.xml")))
        for (auto* f : xml->getChildWithTagNameIterator ("FOLDER"))
        {
            Folder fo;
            fo.name = f->getStringAttribute ("name");
            for (auto* p : f->getChildWithTagNameIterator ("PLUGIN")) fo.ids.add (p->getStringAttribute ("id"));
            folders.add (fo);
        }
}

void PluginCatalog::saveFolders()
{
    XmlElement root ("SIXFOLDERS");
    for (auto& f : folders)
    {
        auto* e = root.createNewChildElement ("FOLDER");
        e->setAttribute ("name", f.name);
        for (auto& id : f.ids) e->createNewChildElement ("PLUGIN")->setAttribute ("id", id);
    }
    root.writeTo (dataDir().getChildFile ("Folders.xml"));
    changes.sendChangeMessage();
}

int PluginCatalog::createFolder (const String& name)
{
    Folder f;
    f.name = name.trim().isEmpty() ? String ("New folder") : name.trim();
    folders.add (f);
    saveFolders();
    return folders.size() - 1;
}

void PluginCatalog::renameFolder (int f, const String& name)
{
    if (! isPositiveAndBelow (f, folders.size()) || name.trim().isEmpty()) return;
    folders.getReference (f).name = name.trim();
    saveFolders();
}

void PluginCatalog::deleteFolder (int f)
{
    if (! isPositiveAndBelow (f, folders.size())) return;
    folders.remove (f);   // only the folder: plug-ins stay installed and in "All effects"
    saveFolders();
}

void PluginCatalog::moveFolder (int from, int to)
{
    if (! isPositiveAndBelow (from, folders.size()) || ! isPositiveAndBelow (to, folders.size()) || from == to) return;
    folders.move (from, to);
    saveFolders();
}

static String folderKey (const PluginDescription& d) { return d.createIdentifierString(); }

bool PluginCatalog::addToFolder (int f, const PluginDescription& d)
{
    if (! isPositiveAndBelow (f, folders.size()) || isInFolder (f, d)) return false;
    folders.getReference (f).ids.add (folderKey (d));
    saveFolders();
    return true;
}

void PluginCatalog::removeFromFolder (int f, const PluginDescription& d)
{
    if (! isPositiveAndBelow (f, folders.size())) return;
    folders.getReference (f).ids.removeString (folderKey (d));
    saveFolders();
}

bool PluginCatalog::isInFolder (int f, const PluginDescription& d) const
{
    return isPositiveAndBelow (f, folders.size()) && folders.getReference (f).ids.contains (folderKey (d));
}

Array<PluginDescription> PluginCatalog::search (const String& text, int folder) const
{
    auto r = search (text);
    if (! isPositiveAndBelow (folder, folders.size())) return r;
    Array<PluginDescription> out;
    for (auto& d : r) if (isInFolder (folder, d)) out.add (d);
    return out;
}

Array<PluginDescription> PluginCatalog::findTypesForFile (const File& file, String& error)
{
    Array<PluginDescription> r;
    OwnedArray<PluginDescription> found;
    for (auto* f : formats.getFormats())
        if (f->fileMightContainThisPluginType (file.getFullPathName()))
            f->findAllTypesForFile (found, file.getFullPathName());

    for (auto* d : found)
    {
        if (isSelf (*d)) { error = "SIX can't be loaded inside itself."; continue; }
        if (! isEffect (*d)) { if (error.isEmpty()) error = d->name + " is an instrument, not an effect."; continue; }
        r.add (*d);
        known.addType (*d);
    }
    if (r.isEmpty() && error.isEmpty())
        error = "No VST3 / Audio Unit effect found in " + file.getFileName();
    if (! r.isEmpty()) { save(); changes.sendChangeMessage(); }
    return r;
}

Array<PluginDescription> PluginCatalog::resolveDroppedText (const String& textIn, String& how)
{
    auto text = textIn.trim();
    if (text.startsWithIgnoreCase ("file://"))
        text = URL (text).getLocalFile().getFullPathName();

    for (auto& line : StringArray::fromLines (text))
    {
        const auto l = line.trim();
        if (File::isAbsolutePath (l) && File (l).exists())
        {
            String err;
            auto r = findTypesForFile (File (l), err);
            how = "path in dropped text";
            if (! r.isEmpty()) return r;
        }
    }

    // A plug-in name (Ableton's browser may provide only the name)
    Array<PluginDescription> r;
    for (auto& d : getEffects())
        if (d.name.equalsIgnoreCase (text) || d.descriptiveName.equalsIgnoreCase (text)) r.add (d);
    if (r.isEmpty())
        for (auto& d : getEffects())
            if (text.isNotEmpty() && text.containsIgnoreCase (d.name) && d.name.length() > 2) r.add (d);
    how = r.isEmpty() ? String() : "plug-in name in dropped text";
    return r;
}

std::optional<PluginDescription> PluginCatalog::findByIdentifier (const String& id) const
{
    for (auto& d : known.getTypes())
        if (d.createIdentifierString() == id) return d;
    return std::nullopt;
}
}
