#pragma once
#include "Ids.h"
#include <set>

namespace gubee
{
constexpr int numMixerTracks = 25;    // master + 24 inserts
constexpr int numPlaylistTracks = 32;
constexpr int numFxSlots = 10;
constexpr double stepBeats = 0.25;    // one 16th step
constexpr int numJamTracks = 8;
constexpr int numJamScenes = 8;

/** The song document. All edits go through the ValueTree with an UndoManager. */
class Project
{
public:
    Project();

    juce::ValueTree root;
    juce::UndoManager undo { 300000, 200 };

    juce::ValueTree channels() const  { return root.getChildWithName (ids::CHANNELS); }
    juce::ValueTree patterns() const  { return root.getChildWithName (ids::PATTERNS); }
    juce::ValueTree playlist() const  { return root.getChildWithName (ids::PLAYLIST); }
    juce::ValueTree mixer() const     { return root.getChildWithName (ids::MIXER); }
    juce::ValueTree mixerTrack (int i) const { return mixer().getChild (i); }
    juce::ValueTree jam() const       { return root.getChildWithName (ids::JAM); }
    juce::ValueTree mods() const      { return root.getChildWithName (ids::MODS); }

    void newProject (bool withDefaultKit = true);
    bool load (const juce::File& f);
    bool save (const juce::File& f);
    juce::File file;
    bool dirty = false;

    void beginEdit (const juce::String& name = {}) { undo.beginNewTransaction (name); }
    juce::UndoManager* um() { return &undo; }

    int allocId();
    double tempo() const { return (double) root.getProperty (ids::tempo, 140.0); }

    // channels
    juce::ValueTree addChannel (const juce::String& kind, const juce::String& name, const juce::String& builtin = {},
                                const juce::String& pluginXml = {}, const juce::File& sample = {});
    juce::ValueTree findChannel (int id) const;
    int channelIndex (int id) const;
    void removeChannel (int id);
    int firstFreeMixerTrack() const;

    // patterns
    juce::ValueTree addPattern (const juce::String& name = {});
    juce::ValueTree findPattern (int id) const;
    juce::ValueTree findOrCreatePattern (int id);
    juce::ValueTree findNote (const juce::ValueTree& pattern, int channel, double start, int key = -1) const;
    void toggleStep (int patternId, int channelId, int step, int rootKey);
    juce::ValueTree addNote (int patternId, int channelId, double start, double len, int key, float vel);
    void removeNotesForChannel (int channelId);

    // playlist
    juce::ValueTree addPatternClip (int track, double start, int patternId);
    juce::ValueTree addAudioClip (int track, double start, const juce::File& f, double lengthSeconds, double srcBpm);
    juce::ValueTree addAutomationClip (int track, double start, double len, const juce::String& target, const juce::String& name);
    double songLengthBeats() const;
    double patternLengthBeats (int patternId) const;

    // drum pads
    juce::ValueTree getPad (int channelId, int pad) const;
    juce::ValueTree setPadSample (int channelId, int pad, const juce::File& f, juce::Range<int> region = {});

    // jam view
    juce::ValueTree getJamSlot (int track, int scene) const;
    juce::ValueTree setJamPattern (int track, int scene, int patternId);
    juce::ValueTree setJamAudio (int track, int scene, const juce::File& f, double lengthBeats, double srcBpm);
    void clearJamSlot (int track, int scene);

    // modulators
    juce::ValueTree addModulator (const juce::String& target, const juce::String& name);

    // mixer
    juce::ValueTree setSlot (int track, int slot, const juce::String& kind, const juce::String& builtin, const juce::String& pluginXml);
    juce::ValueTree getSlot (int track, int slot) const;
    void clearSlot (int track, int slot);

    // helpers
    static juce::String slicesToString (const std::vector<juce::Range<int>>& s);
    static std::vector<juce::Range<int>> slicesFromString (const juce::String& s);
    static juce::String pointsToString (const std::vector<juce::Point<float>>& p);
    static std::vector<juce::Point<float>> pointsFromString (const juce::String& s);

private:
    void ensureStructure();
};
}
