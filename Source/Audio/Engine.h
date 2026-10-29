#pragma once
#include <JuceHeader.h>
#include <optional>
#include <set>
#include "Model/Project.h"
#include "AudioTools.h"
#include "GubeeDSP/GubeeEffects.h"
#include "GubeeDSP/GubeeInstruments.h"
#include "GubeeDSP/GubeeSampler.h"
#include "GubeeDSP/GubeeDrumPads.h"

namespace gubee
{
enum class TransportMode { pattern = 0, song = 1, jam = 2 };

/** Built-in processors available everywhere in the app. */
struct BuiltinRegistry
{
    static juce::StringArray instruments() { return { "Gubee 808", "Gubee Synth" }; }
    static juce::StringArray effects() { return { "Gubee EQ", "Gubee Comp", "Gubee Reverb", "Gubee Delay", "Gubee Filter", "Gubee Crush", "Gubee Limiter" }; }
    static std::unique_ptr<juce::AudioProcessor> create (const juce::String& name);
};

/** Plugin formats + known plugin list (persisted). */
class PluginHost
{
public:
    PluginHost();
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    juce::File listFile, deadMansPedal;
    void load();
    void save();
    std::unique_ptr<juce::AudioPluginInstance> create (const juce::String& descXml, double sr, int bs, juce::String& error);
    static juce::String descToXml (const juce::PluginDescription& d);
};

//==============================================================================
struct ProcessedAudio
{
    juce::AudioBuffer<float> buffer;   // already stretched / pitched / reversed, at engine rate
    double rate = 44100.0;
    double srcSecondsPerOutSecond = 1.0;
};
using ProcessedPtr = std::shared_ptr<const ProcessedAudio>;

/** Background renderer of time-stretched clip audio. */
class ClipAudioCache
{
public:
    ClipAudioCache (SampleCache& sc) : samples (sc) {}
    ~ClipAudioCache() { pool.removeAllJobs (true, 10000); }
    /** Returns processed audio if ready; otherwise queues work and returns the best fallback (may be null). */
    ProcessedPtr request (const juce::File& f, bool reverse, double speed, double semitones, double outRate);
    std::function<void()> onReady;
    void clear();

private:
    SampleCache& samples;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1) };
    juce::CriticalSection lock;
    std::map<juce::String, ProcessedPtr> ready;
    std::map<juce::String, ProcessedPtr> latestByFile;
    std::set<juce::String> pending;
};

//==============================================================================
struct ChannelRuntime
{
    int id = 0;
    juce::String key;
    std::unique_ptr<juce::AudioProcessor> proc;
    juce::String error;
    std::atomic<float> vol { 0.8f }, pan { 0.0f };
    std::atomic<bool> mute { false };
    std::atomic<float> level { 0.0f };
    juce::AudioBuffer<float> buf;
    juce::MidiBuffer midi, midiOns;
    int numChannels = 2;
    DrumPadsProcessor* pads = nullptr;
    SamplerProcessor* sampler = nullptr;
};

struct SlotRuntime
{
    int id = 0;
    juce::String key;
    std::unique_ptr<juce::AudioProcessor> proc;
    juce::String error;
    std::atomic<bool> bypass { false };
    juce::AudioBuffer<float> buf;
    juce::MidiBuffer emptyMidi;
    int numChannels = 2;
};

struct TrackRuntime
{
    std::atomic<float> vol { 0.8f }, pan { 0.0f };
    std::atomic<bool> mute { false }, solo { false };
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    juce::AudioBuffer<float> buf;
};

struct AutoTarget
{
    enum Type { none, chVol, chPan, mxVol, mxPan, param, tempo } type = none;
    std::shared_ptr<ChannelRuntime> ch;
    std::shared_ptr<TrackRuntime> track;
    std::shared_ptr<void> keepAlive;
    juce::AudioProcessorParameter* parameter = nullptr;
    void apply (float v01) const;
};

struct NoteEv { double start, end; int key; float vel; };

struct Snapshot
{
    double tempo = 140.0;
    float swing = 0.0f;
    struct Channel { int id; int mixer; std::shared_ptr<ChannelRuntime> rt; };
    std::vector<Channel> channels;
    struct Pattern { int id; double length; std::vector<std::vector<NoteEv>> notes; };   // notes[channelIndex]
    std::vector<Pattern> patterns;
    std::map<int, int> patternIndex;
    struct Clip
    {
        int kind = 0;   // 0 pattern, 1 audio, 2 automation
        int track = 0;
        double start = 0, len = 0, offset = 0;
        int patternIdx = -1;
        ProcessedPtr audio; double offsetOut = 0; float gain = 1; double fadeIn = 0, fadeOut = 0; int mixer = 0;
        AutoTarget target; std::vector<juce::Point<float>> points;
    };
    std::vector<Clip> clips;
    struct Mod { AutoTarget target; int shape; double rate; float depth, base; };
    std::vector<Mod> mods;
    struct Track { std::shared_ptr<TrackRuntime> rt; std::vector<std::shared_ptr<SlotRuntime>> slots; int route, sendTo; float sendLevel; };
    std::vector<Track> tracks;
    struct JamSlot { int kind = -1; int patternIdx = -1; ProcessedPtr audio; double len = 4; float gain = 1; };
    std::array<std::array<JamSlot, numJamScenes>, numJamTracks> jam;
    std::array<int, numJamTracks> jamMixer {};
    double songEnd = 0, loopStart = 0, loopEnd = 0;
    bool anySolo = false;
};
using SnapshotPtr = std::shared_ptr<const Snapshot>;

//==============================================================================
class Engine : public juce::AudioIODeviceCallback,
               public juce::AudioPlayHead,
               private juce::ValueTree::Listener,
               private juce::AsyncUpdater,
               private juce::Timer
{
public:
    Engine (Project&, SampleCache&, PluginHost&, juce::AudioDeviceManager&);
    ~Engine() override;

    // transport
    void play();
    void stop();
    void togglePlay() { isPlaying() ? stop() : play(); }
    bool isPlaying() const { return playing.load(); }
    void setMode (TransportMode m);
    TransportMode getMode() const { return (TransportMode) mode.load(); }
    void setBeat (double beats);
    double getBeat() const { return position.load(); }
    void setCurrentPattern (int id) { currentPattern = id; }
    int getCurrentPattern() const { return currentPattern.load(); }
    std::atomic<bool> metronome { false }, recordArmed { false }, noteRepeat { false };
    std::atomic<double> noteRepeatBeats { 0.25 };
    std::atomic<int> selectedChannel { -1 };
    std::atomic<int> recordTrack { 0 };
    std::atomic<bool> quantizeRecording { true };

    // live notes from UI / MIDI
    void sendLiveNote (int note, float velocity);   // velocity 0 = note off
    juce::MidiMessageCollector& getMidiCollector() { return collector; }

    // preview (browser)
    void previewFile (const juce::File& f);
    void previewBuffer (SampleDataPtr data, juce::Range<int> region = {});
    void stopPreview();
    double previewPositionSeconds() const { return previewPos.load(); }

    // jam view
    void launchJam (int track, int scene);
    void stopJamTrack (int track);
    void launchScene (int scene);
    void stopAllJam();
    int jamPlayingScene (int track) const { return jamPlaying[(size_t) track].load(); }
    int jamQueuedScene (int track) const { return jamQueued[(size_t) track].load(); }
    void captureJamToPlaylist();

    // runtimes (message thread)
    juce::AudioProcessor* channelProcessor (int channelId);
    juce::String channelError (int channelId);
    juce::AudioProcessor* slotProcessor (int track, int slot);
    std::shared_ptr<TrackRuntime> trackRuntime (int i);
    std::shared_ptr<ChannelRuntime> channelRuntime (int id);
    void storeStatesToTree();
    void syncNow() { cancelPendingUpdate(); sync(); }

    // export
    struct ExportOptions { juce::File file; int format = 0; int bits = 24; bool stems = false; bool patternOnly = false; juce::StringArray trackNames; };
    bool exportAudio (const ExportOptions&, std::function<bool (float)> progress, juce::String& error);

    double getSampleRate() const { return sampleRate; }
    float getCpu() const { return (float) devices.getCpuUsage(); }
    std::function<void (const juce::String&)> onStatus;

    // AudioPlayHead
    juce::Optional<PositionInfo> getPosition() const override;

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

private:
    void process (const float* const* in, int numIn, float* const* out, int numOut, int n);
    void scheduleSegment (const Snapshot& s, double b0, double b1, int off, int len, double spb);
    void emitNotes (const Snapshot& s, int chIndex, const std::vector<NoteEv>& notes, double windowStart,
                    double clipStart, double clipEnd, double b0, double b1, int off, int len, double spb);
    void renderAudio (const Snapshot& s, const ProcessedPtr& a, int mixer, double localBeat0, double offsetOut,
                      float gain, double fadeIn, double fadeOut, double clipLen, int off, int len, double spb, bool loop);
    void prepareAll();
    void prepareProcessor (juce::AudioProcessor& p, int& numCh);
    void sync();
    std::shared_ptr<ChannelRuntime> makeChannel (const juce::ValueTree& c);
    std::shared_ptr<SlotRuntime> makeSlot (const juce::ValueTree& s);
    std::unique_ptr<juce::AudioProcessor> createProcessor (const juce::ValueTree& node, juce::String& err);
    AutoTarget resolveTarget (const juce::String& target);
    void applySamplerSettings (ChannelRuntime& rt, const juce::ValueTree& c);
    double loopEndFor (const Snapshot& s, double& loopStart) const;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { triggerAsyncUpdate(); }
    void valueTreeRedirected (juce::ValueTree&) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { sync(); }
    void timerCallback() override;

    Project& project;
    SampleCache& samples;
    PluginHost& plugins;
    juce::AudioDeviceManager& devices;
    ClipAudioCache clipCache;

    juce::SpinLock snapLock;
    SnapshotPtr snapshot;
    std::vector<SnapshotPtr> graveyard;

    juce::CriticalSection runtimeLock;
    std::map<int, std::shared_ptr<ChannelRuntime>> channelRts;
    std::map<int, std::shared_ptr<SlotRuntime>> slotRts;
    std::vector<std::shared_ptr<TrackRuntime>> trackRts;

    double sampleRate = 44100.0;
    int blockSize = 512;
    std::atomic<bool> playing { false };
    std::atomic<int> mode { 0 };
    std::atomic<double> position { 0.0 };
    std::atomic<double> seekRequest { -1.0 };
    std::atomic<bool> panicRequest { false };
    std::atomic<int> currentPattern { -1 };
    juce::int64 sampleCounter = 0;
    double metroPhase = 0; int metroSamplesLeft = 0; float metroFreq = 1000;

    juce::MidiMessageCollector collector;
    juce::MidiBuffer liveMidi;
    std::array<float, 128> heldVel {};
    double repeatClock = 0;
    std::array<double, 128> heldStartBeat {};

    // preview
    juce::SpinLock previewLock;
    SampleDataPtr previewData;
    juce::Range<int> previewRegion;
    double previewIdx = 0;
    std::atomic<double> previewPos { -1.0 };

    // jam
    std::array<std::atomic<int>, numJamTracks> jamPlaying, jamQueued;
    std::array<double, numJamTracks> jamStart {};
    struct JamEvent { int track, scene; double beat; };
    juce::AbstractFifo jamFifo { 256 };
    std::array<JamEvent, 256> jamEvents;
    std::vector<JamEvent> jamLog;
    bool jamPendingStart = false;

    // recording
    juce::CriticalSection writerLock;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> recWriter;
    juce::TimeSliceThread recThread { "GubeeRecorder" };
    juce::File recFile;
    double recStartBeat = 0;
    std::atomic<bool> recActive { false };
    struct RecNote { int channel, pattern; double start, len; int key; float vel; };
    juce::AbstractFifo noteFifo { 1024 };
    std::array<RecNote, 1024> recNotes;
    void startAudioRecording();
    void finishAudioRecording();

    bool exporting = false;
    std::optional<int> stemOnly;
};
}
