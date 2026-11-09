#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace six
{
// Installed VST3 / Audio Unit effects. Scanning runs on a background thread for VST3 and
// incrementally on the message thread for Audio Units; never on the audio thread.
// Results are cached on disk so Browse is instant on the next session.
class PluginCatalog : private juce::Timer,
                      private juce::Thread
{
public:
    explicit PluginCatalog (juce::AudioPluginFormatManager& fm);
    ~PluginCatalog() override;

    void startScan (bool rescanAll);
    bool isScanning() const noexcept { return scanning.load(); }
    juce::String getScanStatus() const;
    float getScanProgress() const noexcept { return progress.load(); }

    juce::Array<juce::PluginDescription> getEffects() const;     // sorted, SIX itself excluded
    juce::Array<juce::PluginDescription> search (const juce::String& text) const;

    // Message thread: the effects inside one .vst3 / .component bundle (for Finder drops).
    juce::Array<juce::PluginDescription> findTypesForFile (const juce::File& f, juce::String& error);

    // Interprets dropped text (path, URL or a plug-in name, e.g. from Ableton's browser).
    juce::Array<juce::PluginDescription> resolveDroppedText (const juce::String& text, juce::String& howResolved);

    std::optional<juce::PluginDescription> findByIdentifier (const juce::String& id) const;

    static bool isSelf (const juce::PluginDescription& d);
    static bool isEffect (const juce::PluginDescription& d) { return ! d.isInstrument; }

    juce::ChangeBroadcaster changes;

private:
    void run() override;          // VST3 scan
    void timerCallback() override; // AU scan step / notifications
    void save();
    void load();
    static juce::File dataDir();

    juce::AudioPluginFormatManager& formats;
    juce::KnownPluginList known;
    std::atomic<bool> scanning { false };
    std::atomic<float> progress { 0.0f };
    juce::String status;
    mutable juce::CriticalSection statusLock;
    std::unique_ptr<juce::PluginDirectoryScanner> mtScanner;   // AU (message thread)
    bool rescanRequested = false;
    std::atomic<bool> listChanged { false };
};
}
