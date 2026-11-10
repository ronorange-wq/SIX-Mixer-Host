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
    if (auto xml = parseXML (dataDir().getChildFile ("PluginCatalog2.xml")))
        known.recreateFromXml (*xml);
}

void PluginCatalog::save()
{
    if (auto xml = known.createXml())
        xml->writeTo (dataDir().getChildFile ("PluginCatalog2.xml"));
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
    scanning = true;
    progress = 0.0f;
    { const ScopedLock sl (statusLock); status = "Scanning..."; }
    startThread();
    startTimerHz (10);
}

String PluginCatalog::getScanStatus() const
{
    const ScopedLock sl (statusLock);
    return status;
}

// ------------------------------------------------------------------------------ safe scanning
// The Browser list is built WITHOUT loading any plug-in into the host: VST3 names come from
// each bundle's moduleinfo.json / Info.plist, Audio Units from the system component registry.
// (Loading every installed plug-in inside Live to read its name made some third-party
// plug-ins crash Live.) A plug-in is only instantiated when you actually load it into a slot.
static const char* lightMarker = "six-light";

static PluginDescription lightDescription (const String& format, const String& name, const String& vendor,
                                           const String& fileOrId, const String& category, bool instrument)
{
    PluginDescription d;
    d.name = name;
    d.descriptiveName = name;
    d.manufacturerName = vendor;
    d.pluginFormatName = format;
    d.fileOrIdentifier = fileOrId;
    d.category = category;
    d.isInstrument = instrument;
    d.version = lightMarker;
    d.uniqueId = (int) (fileOrId + "|" + name).hashCode();
    d.deprecatedUid = d.uniqueId;
    d.numInputChannels = 2;
    d.numOutputChannels = 2;
    return d;
}

static void findVST3Bundles (const File& dir, Array<File>& out, int depth)
{
    if (depth > 4 || ! dir.isDirectory()) return;
    for (const auto& e : RangedDirectoryIterator (dir, false, "*", File::findDirectories))
    {
        const auto f = e.getFile();
        if (f.hasFileExtension (".vst3")) out.add (f);
        else findVST3Bundles (f, out, depth + 1);
    }
}

static void describeVST3Bundle (const File& bundle, Array<PluginDescription>& out)
{
    const auto info = bundle.getChildFile ("Contents/Resources/moduleinfo.json");
    if (info.existsAsFile())
    {
        const auto json = JSON::parse (info);
        String vendor = json.getProperty ("Factory Info", var()).getProperty ("Vendor", var()).toString();
        bool any = false;
        if (auto* classes = json.getProperty ("Classes", var()).getArray())
            for (auto& c : *classes)
            {
                if (c.getProperty ("Category", var()).toString() != "Audio Module Class") continue;
                StringArray subs;
                if (auto* a = c.getProperty ("Sub Categories", var()).getArray())
                    for (auto& x : *a) subs.add (x.toString());
                const bool instrument = subs.contains ("Instrument") || subs.contains ("Synth");
                auto v = c.getProperty ("Vendor", var()).toString();
                out.add (lightDescription ("VST3", c.getProperty ("Name", var()).toString(), v.isNotEmpty() ? v : vendor,
                                           bundle.getFullPathName(), subs.joinIntoString ("|"), instrument));
                any = true;
            }
        if (any) return;
    }

    // No moduleinfo.json: list the bundle by name (its effects are resolved when you load it).
    String vendor;
    if (auto plist = parseXML (bundle.getChildFile ("Contents/Info.plist")))
        if (auto* dict = plist->getChildByName ("dict"))
            for (int i = 0; i + 1 < dict->getNumChildElements(); ++i)
                if (dict->getChildElement (i)->getAllSubText() == "CFBundleIdentifier")
                {
                    auto parts = StringArray::fromTokens (dict->getChildElement (i + 1)->getAllSubText(), ".", "");
                    if (parts.size() >= 2) vendor = parts[1];
                    break;
                }
    out.add (lightDescription ("VST3", bundle.getFileNameWithoutExtension(), vendor, bundle.getFullPathName(), "VST3", false));
}

void PluginCatalog::run()
{
    Array<PluginDescription> found;

    // VST3 bundles (file reads only)
    Array<File> bundles;
    for (auto& dir : StringArray { "~/Library/Audio/Plug-Ins/VST3", "/Library/Audio/Plug-Ins/VST3",
                                   "~/.vst3", "/usr/lib/vst3", "/usr/local/lib/vst3" })
        findVST3Bundles (File (dir), bundles, 0);
    for (auto* f : formats.getFormats())   // anything else the format would search (e.g. custom paths)
        if (f->getName() == "VST3")
            for (int i = 0; i < f->getDefaultLocationsToSearch().getNumPaths(); ++i)
                findVST3Bundles (f->getDefaultLocationsToSearch()[i], bundles, 0);

    StringArray seen;
    for (int i = 0; i < bundles.size() && ! threadShouldExit(); ++i)
    {
        const auto& b = bundles.getReference (i);
        if (seen.contains (b.getFullPathName())) continue;
        seen.add (b.getFullPathName());
        { const ScopedLock sl (statusLock); status = "VST3: " + b.getFileName(); }
        describeVST3Bundle (b, found);
        progress = 0.8f * (float) (i + 1) / (float) jmax (1, bundles.size());
    }

   #if JUCE_MAC
    // Audio Units from the system registry (no plug-in is opened)
    for (auto* f : formats.getFormats())
    {
        if (f->getName() != "AudioUnit" || threadShouldExit()) continue;
        { const ScopedLock sl (statusLock); status = "Audio Units"; }
        for (auto& id : f->searchPathsForPlugins ({}, false, true))
        {
            if (! id.containsIgnoreCase ("Effects/")) continue;   // effects only
            const auto full = f->getNameOfPluginFromIdentifier (id);
            String vendor, name = full;
            if (full.containsChar (':'))
            {
                vendor = full.upToFirstOccurrenceOf (":", false, false).trim();
                name = full.fromFirstOccurrenceOf (":", false, false).trim();
            }
            found.add (lightDescription ("AudioUnit", name, vendor, id, "AU", false));
        }
    }
   #endif

    if (threadShouldExit()) return;
    known.clear();
    for (auto& d : found) known.addType (d);
    listChanged = true;
}

void PluginCatalog::timerCallback()
{
    if (isThreadRunning()) { if (listChanged.exchange (false)) changes.sendChangeMessage(); return; }
    stopTimer();
    scanning = false;
    progress = 1.0f;
    { const ScopedLock sl (statusLock); status = String (getEffects().size()) + " effects"; }
    save();
    changes.sendChangeMessage();
}

Array<PluginDescription> PluginCatalog::resolveForLoad (const PluginDescription& d, String& error)
{
    if (d.version != lightMarker) return { d };
    String err;
    auto all = findTypesForFile (File::isAbsolutePath (d.fileOrIdentifier) ? File (d.fileOrIdentifier) : File(), err, d.fileOrIdentifier);
    if (all.isEmpty()) { error = err.isNotEmpty() ? err : "Couldn't open " + d.name; return {}; }
    for (auto& t : all)
        if (t.name.equalsIgnoreCase (d.name)) return { t };
    return all;
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

Array<PluginDescription> PluginCatalog::findTypesForFile (const File& file, String& error, const String& identifier)
{
    Array<PluginDescription> r;
    OwnedArray<PluginDescription> found;
    const auto target = identifier.isNotEmpty() ? identifier : file.getFullPathName();
    for (auto* f : formats.getFormats())
        if (f->fileMightContainThisPluginType (target))
            f->findAllTypesForFile (found, target);   // instantiates only this plug-in, because you chose it

    for (auto* d : found)
    {
        if (isSelf (*d)) { error = "SIX can't be loaded inside itself."; continue; }
        if (! isEffect (*d)) { if (error.isEmpty()) error = d->name + " is an instrument, not an effect."; continue; }
        r.add (*d);
        const ScopedLock sl (statusLock);
        bool have = false;   // findable by id (drags), not listed in Browse
        for (auto& x : resolved) have |= x.isDuplicateOf (*d) && x.name == d->name;
        if (! have) resolved.add (*d);
    }
    if (r.isEmpty() && error.isEmpty())
        error = "No VST3 / Audio Unit effect found in " + file.getFileName();
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
    const ScopedLock sl (statusLock);
    for (auto& d : resolved)
        if (d.createIdentifierString() == id) return d;
    return std::nullopt;
}
}
