#pragma once
#include <JuceHeader.h>

namespace gubee
{
struct SampleInfo
{
    juce::File file;
    juce::String pack, name, type, key, searchText;
    double bpm = 0;
    bool loop = false, midi = false;
};

/** Sample-pack library: imports packs (folders / zips), indexes and auto-tags sounds. */
class Library : public juce::ChangeBroadcaster
{
public:
    Library();
    ~Library() override;

    static juce::StringArray types() { return { "All", "Kick", "Snare", "Clap", "Hat", "Open Hat", "Cymbal", "Perc", "808", "Bass",
                                                "Melodic", "Vocal", "FX", "Loop", "One-shot", "MIDI", "Favorites" }; }

    void rescan();
    bool isScanning() const { return scanning.load(); }
    std::vector<SampleInfo> getAll() const { const juce::ScopedLock sl (lock); return items; }
    std::vector<juce::String> getPacks() const;

    /** Import a folder or .zip into the Packs folder (copy). Runs in the background. */
    void importPack (const juce::File& source);
    /** Use a folder in place without copying. */
    void linkFolder (const juce::File& folder);
    void unlinkFolder (const juce::File& folder);
    juce::Array<juce::File> getLinkedFolders() const { return linked; }

    bool isFavorite (const juce::File& f) const { return favorites.contains (f.getFullPathName()); }
    void toggleFavorite (const juce::File& f);

    juce::String status;
    static bool isAudioFile (const juce::File& f);
    static void classify (SampleInfo& info, const juce::String& relPath);

private:
    void saveSettings();
    void loadSettings();
    struct Job;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1) };
    mutable juce::CriticalSection lock;
    std::vector<SampleInfo> items;
    juce::Array<juce::File> linked;
    juce::StringArray favorites;
    std::atomic<bool> scanning { false };
    std::atomic<int> scanGeneration { 0 };
};
}
