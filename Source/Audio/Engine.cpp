#include "Engine.h"
#include "App/Paths.h"

namespace gubee
{
//==============================================================================
//==============================================================================
PluginHost::PluginHost()
{
    formats.addDefaultFormats();
    listFile = paths::settings().getChildFile ("plugins.xml");
    deadMansPedal = paths::settings().getChildFile ("plugin-scan-crash.txt");
    load();
}

void PluginHost::load()
{
    if (auto xml = juce::XmlDocument::parse (listFile)) known.recreateFromXml (*xml);
}

void PluginHost::save()
{
    if (auto xml = known.createXml()) xml->writeTo (listFile);
}

juce::String PluginHost::descToXml (const juce::PluginDescription& d)
{
    if (auto xml = d.createXml()) return xml->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
    return {};
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::create (const juce::String& descXml, double sr, int bs, juce::String& error)
{
    juce::PluginDescription d;
    auto xml = juce::XmlDocument::parse (descXml);
    if (xml == nullptr || ! d.loadFromXml (*xml)) { error = "Bad plugin description"; return nullptr; }
    auto inst = formats.createPluginInstance (d, sr, bs, error);
    if (inst == nullptr && error.isEmpty()) error = "Could not load " + d.name;
    return inst;
}

//==============================================================================
ProcessedPtr ClipAudioCache::request (const juce::File& f, bool reverse, double speed, double semis, double outRate)
{
    const auto path = f.getFullPathName();
    const auto key = path + "|" + (reverse ? "r" : "f") + "|" + juce::String (speed, 5) + "|" + juce::String (semis, 3) + "|" + juce::String (outRate, 1);
    const juce::ScopedLock sl (lock);
    if (auto it = ready.find (key); it != ready.end()) return it->second;
    if (pending.count (key) == 0)
    {
        pending.insert (key);
        pool.addJob ([this, f, reverse, speed, semis, outRate, key, path]
        {
            ProcessedPtr result;
            if (auto src = samples.get (f))
            {
                auto pa = std::make_shared<ProcessedAudio>();
                juce::AudioBuffer<float> b (src->buffer);
                if (reverse) tools::reverseRange (b, { 0, b.getNumSamples() });
                pa->buffer = tools::stretchAndPitch (b, src->sampleRate, outRate, 1.0 / juce::jmax (0.05, speed), semis);
                if (pa->buffer.getNumChannels() == 1)
                {
                    pa->buffer.setSize (2, pa->buffer.getNumSamples(), true);
                    pa->buffer.copyFrom (1, 0, pa->buffer, 0, 0, pa->buffer.getNumSamples());
                }
                pa->rate = outRate;
                pa->srcSecondsPerOutSecond = speed;
                result = pa;
            }
            {
                const juce::ScopedLock sl2 (lock);
                pending.erase (key);
                if (result != nullptr) { ready[key] = result; latestByFile[path] = result; }
                // keep the cache bounded
                if (ready.size() > 400) ready.erase (ready.begin());
            }
            if (onReady) juce::MessageManager::callAsync (onReady);
        });
    }
    if (auto it = latestByFile.find (path); it != latestByFile.end()) return it->second;
    return nullptr;
}

void ClipAudioCache::clear()
{
    const juce::ScopedLock sl (lock);
    ready.clear();
}

//==============================================================================
void AutoTarget::apply (float v, bool notifyHost) const
{
    v = juce::jlimit (0.0f, 1.0f, v);
    switch (type)
    {
        case chVol: if (ch) ch->vol = v; break;
        case chPan: if (ch) ch->pan = v * 2.0f - 1.0f; break;
        case mxVol: if (track) track->vol = v * 1.25f; break;
        case mxPan: if (track) track->pan = v * 2.0f - 1.0f; break;
        case param: if (parameter) { if (notifyHost) parameter->setValueNotifyingHost (v); else parameter->setValue (v); } break;
        default: break;
    }
}

//==============================================================================
Engine::Engine (Project& p, SampleCache& s, PluginHost& ph, juce::AudioDeviceManager& dm)
    : project (p), samples (s), plugins (ph), devices (dm), clipCache (s)
{
    for (int i = 0; i < numMixerTracks; ++i) trackRts.push_back (std::make_shared<TrackRuntime>());
    for (auto& a : jamPlaying) a = -1;
    for (auto& a : jamQueued) a = -1;
    heldVel.fill (0.0f);
    clipCache.onReady = [this] { triggerAsyncUpdate(); };
    project.root.addListener (this);
    recThread.startThread();
    sync();
    devices.addAudioCallback (this);
    startTimerHz (20);
}

Engine::~Engine()
{
    stopTimer();
    devices.removeAudioCallback (this);
    project.root.removeListener (this);
    {
        const juce::ScopedLock sl (writerLock);
        recWriter.reset();
    }
    recThread.stopThread (2000);
    clipCache.onReady = nullptr;
    {
        const juce::SpinLock::ScopedLockType sl (snapLock);
        snapshot.reset();
    }
    graveyard.clear();
}

//==============================================================================
void Engine::play()
{
    if (playing) return;
    recordFrom = -1.0e9;
    if (recordArmed && getMode() != TransportMode::jam)
    {
        double start = seekRequest.load() >= 0 ? seekRequest.load() : position.load();
        if (getMode() == TransportMode::pattern) start = 0.0;
        start = juce::jmax (0.0, std::floor (start / 4.0 + 1.0e-9) * 4.0);
        recordFrom = start;
        if (countIn) seekRequest = start - 4.0;
        else seekRequest = start;
    }
    if (getMode() == TransportMode::song && recordArmed) startAudioRecording();
    if (getMode() == TransportMode::jam) jamPendingStart = true;
    jamLog.clear();
    playing = true;
}

void Engine::stop()
{
    const bool wasPlaying = playing.exchange (false);
    panicRequest = true;
    if (recActive) finishAudioRecording();
    for (int t = 0; t < numJamTracks; ++t) { jamPlaying[(size_t) t] = -1; jamQueued[(size_t) t] = -1; }
    juce::ignoreUnused (wasPlaying);
    const double ls = project.root.getProperty (ids::loopStart, 0.0), le = project.root.getProperty (ids::loopEnd, 0.0);
    seekRequest = (getMode() == TransportMode::song && le > ls + 0.01) ? ls : 0.0;
    triggerAsyncUpdate();
}

void Engine::setMode (TransportMode m)
{
    if ((int) m == mode.load()) return;
    const bool was = playing.load();
    if (was) stop();
    mode = (int) m;
    seekRequest = 0.0;
}

void Engine::setBeat (double beats) { seekRequest = juce::jmax (0.0, beats); }

void Engine::sendLiveNote (int note, float velocity)
{
    auto m = velocity > 0 ? juce::MidiMessage::noteOn (1, note, velocity) : juce::MidiMessage::noteOff (1, note);
    m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    collector.addMessageToQueue (m);
}

void Engine::previewFile (const juce::File& f)
{
    previewBuffer (samples.get (f));
}

void Engine::previewBuffer (SampleDataPtr data, juce::Range<int> region)
{
    const juce::SpinLock::ScopedLockType sl (previewLock);
    previewData = std::move (data);
    if (previewData != nullptr)
    {
        previewRegion = region.isEmpty() ? juce::Range<int> (0, previewData->buffer.getNumSamples())
                                         : region.getIntersectionWith ({ 0, previewData->buffer.getNumSamples() });
        previewIdx = previewRegion.getStart();
    }
}

void Engine::stopPreview()
{
    const juce::SpinLock::ScopedLockType sl (previewLock);
    previewData.reset();
    previewPos = -1.0;
}

//==============================================================================
void Engine::launchJam (int track, int scene)
{
    if (track < 0 || track >= numJamTracks) return;
    jamQueued[(size_t) track] = scene;
    if (! playing) { setMode (TransportMode::jam); mode = (int) TransportMode::jam; play(); }
}

void Engine::stopJamTrack (int track)
{
    if (track >= 0 && track < numJamTracks) jamQueued[(size_t) track] = -2;
}

void Engine::launchScene (int scene)
{
    SnapshotPtr s;
    { const juce::SpinLock::ScopedLockType sl (snapLock); s = snapshot; }
    for (int t = 0; t < numJamTracks; ++t)
        jamQueued[(size_t) t] = (s && s->jam[(size_t) t][(size_t) scene].kind >= 0) ? scene : -2;
    if (! playing) { mode = (int) TransportMode::jam; play(); }
}

void Engine::stopAllJam()
{
    for (int t = 0; t < numJamTracks; ++t) jamQueued[(size_t) t] = -2;
}

void Engine::captureJamToPlaylist()
{
    timerCallback();   // drain events
    if (jamLog.empty()) return;
    auto log = jamLog;
    std::sort (log.begin(), log.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
    const double now = getBeat();
    // Place the capture after anything already in the playlist.
    const double base = std::ceil (project.songLengthBeats() / 4.0) * 4.0;
    const double first = std::floor (log.front().beat / 4.0) * 4.0;
    project.beginEdit ("Capture jam");
    for (size_t i = 0; i < log.size(); ++i)
    {
        auto e = log[i];
        if (e.scene < 0) continue;
        double end = now;
        for (size_t j = i + 1; j < log.size(); ++j)
            if (log[j].track == e.track) { end = log[j].beat; break; }
        if (end <= e.beat) continue;
        auto slot = project.getJamSlot (e.track, e.scene);
        if (! slot.isValid()) continue;
        const double start = base + (e.beat - first);
        if (slot.getProperty (ids::kind).toString() == "pattern")
        {
            auto c = project.addPatternClip (e.track, start, (int) slot.getProperty (ids::pattern));
            c.setProperty (ids::len, end - e.beat, project.um());
        }
        else
        {
            auto f = juce::File (slot.getProperty (ids::file).toString());
            auto data = samples.get (f);
            if (data == nullptr) continue;
            auto c = project.addAudioClip (e.track, start, f, data->lengthSeconds(), (double) slot.getProperty (ids::srcBpm, 0.0));
            c.setProperty (ids::len, end - e.beat, project.um());
        }
    }
    jamLog.clear();
    if (onStatus) onStatus ("Jam captured to the Playlist");
}

//==============================================================================
juce::AudioProcessor* Engine::channelProcessor (int id)
{
    const juce::ScopedLock sl (runtimeLock);
    auto it = channelRts.find (id);
    return it != channelRts.end() ? it->second->proc.get() : nullptr;
}

juce::String Engine::channelError (int id)
{
    const juce::ScopedLock sl (runtimeLock);
    auto it = channelRts.find (id);
    return it != channelRts.end() ? it->second->error : juce::String();
}

std::shared_ptr<ChannelRuntime> Engine::channelRuntime (int id)
{
    const juce::ScopedLock sl (runtimeLock);
    auto it = channelRts.find (id);
    return it != channelRts.end() ? it->second : nullptr;
}

juce::AudioProcessor* Engine::slotProcessor (int track, int slot)
{
    auto s = project.getSlot (track, slot);
    if (! s.isValid()) return nullptr;
    const juce::ScopedLock sl (runtimeLock);
    auto it = slotRts.find ((int) s.getProperty (ids::id));
    return it != slotRts.end() ? it->second->proc.get() : nullptr;
}

std::shared_ptr<TrackRuntime> Engine::trackRuntime (int i)
{
    return (i >= 0 && i < (int) trackRts.size()) ? trackRts[(size_t) i] : nullptr;
}

void Engine::storeStatesToTree()
{
    const juce::ScopedLock sl (runtimeLock);
    auto store = [] (juce::AudioProcessor* p, juce::ValueTree node)
    {
        if (p == nullptr || ! node.isValid()) return;
        juce::MemoryBlock mb;
        p->getStateInformation (mb);
        node.setProperty (ids::state, mb.toBase64Encoding(), nullptr);
    };
    for (auto c : project.channels())
        if (auto it = channelRts.find ((int) c.getProperty (ids::id)); it != channelRts.end())
            store (it->second->proc.get(), c);
    for (auto t : project.mixer())
        for (auto s : t)
            if (auto it = slotRts.find ((int) s.getProperty (ids::id)); it != slotRts.end())
                store (it->second->proc.get(), s);
}

//==============================================================================
std::unique_ptr<juce::AudioProcessor> Engine::createProcessor (const juce::ValueTree& node, juce::String& err)
{
    const auto kind = node.getProperty (ids::kind).toString();
    std::unique_ptr<juce::AudioProcessor> p;
    if (kind == "sampler")      p = std::make_unique<SamplerProcessor>();
    else if (kind == "pads")    p = std::make_unique<DrumPadsProcessor>();
    else if (kind == "builtin") p = BuiltinRegistry::create (node.getProperty (ids::builtin).toString());
    else if (kind == "plugin")  p = plugins.create (node.getProperty (ids::plugin).toString(), sampleRate, blockSize, err);
    if (p == nullptr)
    {
        if (err.isEmpty()) err = "Unknown instrument";
        return nullptr;
    }
    auto state = node.getProperty (ids::state).toString();
    if (state.isNotEmpty())
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (state)) p->setStateInformation (mb.getData(), (int) mb.getSize());
    }
    return p;
}

void Engine::prepareProcessor (juce::AudioProcessor& p, int& numCh)
{
    p.setPlayHead (this);
    p.enableAllBuses();
    p.setNonRealtime (exporting);
    p.setRateAndBufferSizeDetails (sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);
    numCh = juce::jmax (2, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
}

std::shared_ptr<ChannelRuntime> Engine::makeChannel (const juce::ValueTree& c)
{
    auto rt = std::make_shared<ChannelRuntime>();
    rt->id = (int) c.getProperty (ids::id);
    rt->key = c.getProperty (ids::kind).toString() + "|" + c.getProperty (ids::builtin).toString() + "|" + c.getProperty (ids::plugin).toString();
    rt->proc = createProcessor (c, rt->error);
    if (rt->proc != nullptr)
    {
        prepareProcessor (*rt->proc, rt->numChannels);
        rt->pads = dynamic_cast<DrumPadsProcessor*> (rt->proc.get());
        rt->sampler = dynamic_cast<SamplerProcessor*> (rt->proc.get());
    }
    rt->buf.setSize (rt->numChannels, juce::jmax (blockSize, 2048));
    return rt;
}

std::shared_ptr<SlotRuntime> Engine::makeSlot (const juce::ValueTree& s)
{
    auto rt = std::make_shared<SlotRuntime>();
    rt->id = (int) s.getProperty (ids::id);
    rt->key = s.getProperty (ids::kind).toString() + "|" + s.getProperty (ids::builtin).toString() + "|" + s.getProperty (ids::plugin).toString();
    rt->proc = createProcessor (s, rt->error);
    if (rt->proc != nullptr) prepareProcessor (*rt->proc, rt->numChannels);
    rt->buf.setSize (rt->numChannels, juce::jmax (blockSize, 2048));
    return rt;
}

void Engine::applySamplerSettings (ChannelRuntime& rt, const juce::ValueTree& c)
{
    if (rt.sampler != nullptr)
    {
        juce::File f (c.getProperty (ids::sample).toString());
        auto data = samples.get (f);
        if (rt.sampler->getSample() != data) rt.sampler->setSample (data);
        rt.sampler->setSlices (Project::slicesFromString (c.getProperty (ids::slices).toString()));
        rt.sampler->rootNote = (int) c.getProperty (ids::root, 60);
    }
    if (rt.pads != nullptr)
    {
        std::array<bool, DrumPadsProcessor::numPads> set {};
        for (auto p : c)
        {
            if (! p.hasType (ids::PAD)) continue;
            int i = (int) p.getProperty (ids::index);
            if (i < 0 || i >= DrumPadsProcessor::numPads) continue;
            DrumPadsProcessor::Pad pad;
            pad.sample = samples.get (juce::File (p.getProperty (ids::sample).toString()));
            int st = (int) p.getProperty (ids::start, 0), en = (int) p.getProperty (ids::end, 0);
            if (en > st) pad.region = { st, en };
            pad.gain = (float) p.getProperty (ids::gain, 1.0f);
            pad.pan = (float) p.getProperty (ids::pan, 0.0f);
            pad.pitch = (float) p.getProperty (ids::pitch, 0.0f);
            pad.reverse = (bool) p.getProperty (ids::reverse, false);
            pad.choke = (int) p.getProperty (ids::choke, 0);
            pad.output = (int) p.getProperty (ids::output, -1);
            rt.pads->setPad (i, pad);
            set[(size_t) i] = true;
        }
        for (int i = 0; i < DrumPadsProcessor::numPads; ++i)
            if (! set[(size_t) i]) rt.pads->setPad (i, {});
    }
}

AutoTarget Engine::resolveTarget (const juce::String& target)
{
    AutoTarget t;
    auto tok = juce::StringArray::fromTokens (target, ":", {});
    if (tok.size() >= 3 && tok[0] == "ch")
    {
        auto it = channelRts.find (tok[1].getIntValue());
        if (it == channelRts.end()) return t;
        t.ch = it->second;
        if (tok[2] == "vol") t.type = AutoTarget::chVol;
        else if (tok[2] == "pan") t.type = AutoTarget::chPan;
        else if (tok[2] == "p" && tok.size() >= 4 && t.ch->proc != nullptr)
        {
            auto& ps = t.ch->proc->getParameters();
            int idx = tok[3].getIntValue();
            if (idx >= 0 && idx < ps.size()) { t.type = AutoTarget::param; t.parameter = ps[idx]; t.keepAlive = t.ch; }
        }
    }
    else if (tok.size() >= 3 && tok[0] == "mx")
    {
        int tr = tok[1].getIntValue();
        if (tr < 0 || tr >= (int) trackRts.size()) return t;
        t.track = trackRts[(size_t) tr];
        if (tok[2] == "vol") t.type = AutoTarget::mxVol;
        else if (tok[2] == "pan") t.type = AutoTarget::mxPan;
        else if (tok.size() >= 5 && tok[3] == "p")
        {
            auto s = project.getSlot (tr, tok[2].getIntValue());
            if (! s.isValid()) return t;
            auto it = slotRts.find ((int) s.getProperty (ids::id));
            if (it == slotRts.end() || it->second->proc == nullptr) return t;
            auto& ps = it->second->proc->getParameters();
            int idx = tok[4].getIntValue();
            if (idx >= 0 && idx < ps.size()) { t.type = AutoTarget::param; t.parameter = ps[idx]; t.keepAlive = it->second; }
        }
    }
    return t;
}

void Engine::sync()
{
    const juce::ScopedLock sl (runtimeLock);
    auto s = std::make_shared<Snapshot>();
    s->tempo = juce::jlimit (20.0, 999.0, project.tempo());
    s->swing = (float) project.root.getProperty (ids::swing, 0.0f);
    s->loopStart = (double) project.root.getProperty (ids::loopStart, 0.0);
    s->loopEnd = (double) project.root.getProperty (ids::loopEnd, 0.0);

    // things driven by automation / modulators keep their live value while playing
    std::set<juce::String> automated;
    for (auto c : project.playlist()) if (c.hasType (ids::CLIP) && c.getProperty (ids::kind).toString() == "auto") automated.insert (c.getProperty (ids::target).toString());
    for (auto m : project.mods()) if ((bool) m.getProperty (ids::enabled, true)) automated.insert (m.getProperty (ids::target).toString());
    const bool live = playing.load();

    // channels
    std::map<int, int> chIndex;
    std::set<int> seen;
    for (auto c : project.channels())
    {
        int id = (int) c.getProperty (ids::id);
        seen.insert (id);
        auto key = c.getProperty (ids::kind).toString() + "|" + c.getProperty (ids::builtin).toString() + "|" + c.getProperty (ids::plugin).toString();
        auto& rt = channelRts[id];
        if (rt == nullptr || rt->key != key) rt = makeChannel (c);
        const auto base = "ch:" + juce::String (id) + ":";
        if (! live || ! automated.count (base + "vol")) rt->vol = (float) c.getProperty (ids::vol, 0.78f);
        if (! live || ! automated.count (base + "pan")) rt->pan = (float) c.getProperty (ids::pan, 0.0f);
        rt->mute = (bool) c.getProperty (ids::mute, false);
        rt->arpMode = (int) c.getProperty (ids::arp, 0);
        rt->arpRate = (float) c.getProperty (ids::arpRate, 0.25f);
        rt->arpOct = juce::jlimit (1, 4, (int) c.getProperty (ids::arpOct, 1));
        rt->arpGate = (float) c.getProperty (ids::arpGate, 0.5f);
        rt->chordType = (int) c.getProperty (ids::chord, 0);
        rt->scaleType = (int) c.getProperty (ids::scale, 0);
        rt->scaleRoot = (int) c.getProperty (ids::scaleRoot, 0);
        applySamplerSettings (*rt, c);
        chIndex[id] = (int) s->channels.size();
        s->channels.push_back ({ id, juce::jlimit (0, numMixerTracks - 1, (int) c.getProperty (ids::mixer, 0)), rt });
    }
    for (auto it = channelRts.begin(); it != channelRts.end();)
        it = seen.count (it->first) ? std::next (it) : channelRts.erase (it);

    // mixer
    std::set<int> seenSlots;
    for (int i = 0; i < numMixerTracks; ++i)
    {
        auto mt = project.mixerTrack (i);
        auto trt = trackRts[(size_t) i];
        const auto mbase = "mx:" + juce::String (i) + ":";
        if (! live || ! automated.count (mbase + "vol")) trt->vol = (float) mt.getProperty (ids::vol, 0.8f);
        if (! live || ! automated.count (mbase + "pan")) trt->pan = (float) mt.getProperty (ids::pan, 0.0f);
        trt->mute = (bool) mt.getProperty (ids::mute, false);
        trt->solo = (bool) mt.getProperty (ids::solo, false);
        if (i > 0 && trt->solo) s->anySolo = true;
        Snapshot::Track tr { trt, {}, (int) mt.getProperty (ids::route, 0), (int) mt.getProperty (ids::sendTo, -1),
                             (float) mt.getProperty (ids::sendLevel, 0.0f) };
        std::vector<std::pair<int, juce::ValueTree>> slots;
        for (auto sn : mt) slots.push_back ({ (int) sn.getProperty (ids::index), sn });
        std::sort (slots.begin(), slots.end(), [] (auto& a, auto& b) { return a.first < b.first; });
        for (auto& [idx, sn] : slots)
        {
            int id = (int) sn.getProperty (ids::id);
            seenSlots.insert (id);
            auto key = sn.getProperty (ids::kind).toString() + "|" + sn.getProperty (ids::builtin).toString() + "|" + sn.getProperty (ids::plugin).toString();
            auto& rt = slotRts[id];
            if (rt == nullptr || rt->key != key) rt = makeSlot (sn);
            rt->bypass = (bool) sn.getProperty (ids::bypass, false);
            rt->sidechain = (int) sn.getProperty (ids::sc, -1);
            if (rt->sidechain >= 0) s->anySidechain = true;
            tr.slots.push_back (rt);
        }
        s->tracks.push_back (std::move (tr));
    }
    for (auto it = slotRts.begin(); it != slotRts.end();)
        it = seenSlots.count (it->first) ? std::next (it) : slotRts.erase (it);

    // patterns
    for (auto p : project.patterns())
    {
        Snapshot::Pattern pat;
        pat.id = (int) p.getProperty (ids::id);
        pat.length = juce::jmax (0.25, (double) p.getProperty (ids::length, 4.0));
        pat.notes.resize (s->channels.size());
        for (auto n : p)
        {
            auto it = chIndex.find ((int) n.getProperty (ids::ch));
            if (it == chIndex.end()) continue;
            double st = n.getProperty (ids::start), len = n.getProperty (ids::len);
            pat.notes[(size_t) it->second].push_back ({ st, st + juce::jmax (0.01, len), (int) n.getProperty (ids::key, 60),
                                                         (float) n.getProperty (ids::vel, 0.78f) });
        }
        for (auto& v : pat.notes) std::sort (v.begin(), v.end(), [] (auto& a, auto& b) { return a.start < b.start; });
        s->patternIndex[pat.id] = (int) s->patterns.size();
        s->patterns.push_back (std::move (pat));
    }

    // playlist
    auto pl = project.playlist();
    for (auto c : pl)
    {
        if (! c.hasType (ids::CLIP)) continue;
        Snapshot::Clip clip;
        clip.track = (int) c.getProperty (ids::track);
        auto trackNode = pl.getChild (clip.track);
        if (trackNode.isValid() && (bool) trackNode.getProperty (ids::mute, false)) continue;
        if ((bool) c.getProperty (ids::mute, false)) continue;
        clip.start = c.getProperty (ids::start);
        clip.len = juce::jmax (0.01, (double) c.getProperty (ids::len));
        clip.offset = c.getProperty (ids::offset, 0.0);
        auto kind = c.getProperty (ids::kind).toString();
        if (kind == "pattern")
        {
            auto it = s->patternIndex.find ((int) c.getProperty (ids::pattern));
            if (it == s->patternIndex.end()) continue;
            clip.kind = 0; clip.patternIdx = it->second;
        }
        else if (kind == "audio")
        {
            clip.kind = 1;
            const bool stretch = (bool) c.getProperty (ids::stretch, false);
            const double srcBpm = c.getProperty (ids::srcBpm, 0.0);
            const double r = (stretch && srcBpm > 0) ? s->tempo / srcBpm : 1.0;
            clip.audio = clipCache.request (juce::File (c.getProperty (ids::file).toString()), (bool) c.getProperty (ids::reverse, false),
                                            r, (double) c.getProperty (ids::pitch, 0.0), sampleRate);
            clip.offsetOut = (double) clip.offset / r * sampleRate;
            clip.gain = juce::Decibels::decibelsToGain ((float) c.getProperty (ids::gain, 0.0f));
            clip.fadeIn = c.getProperty (ids::fadeIn, 0.0);
            clip.fadeOut = c.getProperty (ids::fadeOut, 0.0);
            clip.mixer = trackNode.isValid() ? juce::jlimit (0, numMixerTracks - 1, (int) trackNode.getProperty (ids::mixer, 0)) : 0;
        }
        else if (kind == "auto")
        {
            clip.kind = 2;
            clip.target = resolveTarget (c.getProperty (ids::target).toString());
            clip.points = Project::pointsFromString (c.getProperty (ids::points).toString());
            if (clip.target.type == AutoTarget::none || clip.points.empty()) continue;
        }
        else continue;
        s->clips.push_back (std::move (clip));
    }
    s->songEnd = project.songLengthBeats();

    // modulators
    for (auto m : project.mods())
    {
        if (! (bool) m.getProperty (ids::enabled, true)) continue;
        Snapshot::Mod mod { resolveTarget (m.getProperty (ids::target).toString()), (int) m.getProperty (ids::shape, 0),
                            juce::jmax (0.0625, (double) m.getProperty (ids::rate, 1.0)), (float) m.getProperty (ids::depth, 0.3f),
                            (float) m.getProperty (ids::base, 0.5f) };
        if (mod.target.type != AutoTarget::none) s->mods.push_back (mod);
    }

    // MIDI learn map
    for (auto m : project.root.getChildWithName (ids::MIDIMAP))
    {
        auto target = m.getProperty (ids::target).toString();
        auto t = resolveTarget (target);
        if (t.type != AutoTarget::none)
            s->midiMap.push_back ({ (int) m.getProperty (ids::cc), t, t.type != AutoTarget::param });
    }

    // jam
    for (auto n : project.jam())
    {
        if (n.hasType (ids::JTRACK))
        {
            int i = (int) n.getProperty (ids::index);
            if (i >= 0 && i < numJamTracks) s->jamMixer[(size_t) i] = juce::jlimit (0, numMixerTracks - 1, (int) n.getProperty (ids::mixer, 0));
        }
        else if (n.hasType (ids::JSLOT))
        {
            int t = (int) n.getProperty (ids::track), sc = (int) n.getProperty (ids::scene);
            if (t < 0 || t >= numJamTracks || sc < 0 || sc >= numJamScenes) continue;
            auto& slot = s->jam[(size_t) t][(size_t) sc];
            if (n.getProperty (ids::kind).toString() == "pattern")
            {
                auto it = s->patternIndex.find ((int) n.getProperty (ids::pattern));
                if (it == s->patternIndex.end()) continue;
                slot.kind = 0; slot.patternIdx = it->second; slot.len = s->patterns[(size_t) it->second].length;
            }
            else
            {
                const bool stretch = (bool) n.getProperty (ids::stretch, false);
                const double srcBpm = n.getProperty (ids::srcBpm, 0.0);
                const double r = (stretch && srcBpm > 0) ? s->tempo / srcBpm : 1.0;
                slot.kind = 1;
                slot.audio = clipCache.request (juce::File (n.getProperty (ids::file).toString()), false, r, 0.0, sampleRate);
                slot.len = juce::jmax (0.25, (double) n.getProperty (ids::len, 4.0));
                slot.gain = juce::Decibels::decibelsToGain ((float) n.getProperty (ids::gain, 0.0f));
            }
        }
    }

    const juce::SpinLock::ScopedLockType sl2 (snapLock);
    if (snapshot != nullptr) graveyard.push_back (snapshot);
    snapshot = s;
}

void Engine::timerCallback()
{
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(), [] (auto& p) { return p.use_count() <= 1; }), graveyard.end());

    // recorded notes
    {
        int s1, n1, s2, n2;
        const int ready = noteFifo.getNumReady();
        if (ready > 0)
        {
            noteFifo.prepareToRead (ready, s1, n1, s2, n2);
            project.beginEdit ("Record notes");
            auto add = [&] (int start, int num)
            {
                for (int i = 0; i < num; ++i)
                {
                    auto& r = recNotes[(size_t) (start + i)];
                    double st = r.start, len = r.len;
                    if (quantizeRecording)
                    {
                        st = std::round (st / stepBeats) * stepBeats;
                        len = juce::jmax (stepBeats, std::round (len / stepBeats) * stepBeats);
                    }
                    auto p = project.findPattern (r.pattern);
                    if (p.isValid())
                    {
                        double plen = (double) p.getProperty (ids::length, 4.0);
                        st = std::fmod (juce::jmax (0.0, st), plen);
                        if (! project.findNote (p, r.channel, st, r.key).isValid())
                            project.addNote (r.pattern, r.channel, st, len, r.key, r.vel);
                    }
                }
            };
            add (s1, n1); add (s2, n2);
            noteFifo.finishedRead (n1 + n2);
        }
    }
    // MIDI-learned knobs that live in the project (volume / pan) -> update the tree so the UI follows
    {
        int s1, n1, s2, n2;
        const int ready = ccFifo.getNumReady();
        if (ready > 0)
        {
            ccFifo.prepareToRead (ready, s1, n1, s2, n2);
            auto handle = [this] (const CCEvent& e)
            {
                for (auto m : project.root.getChildWithName (ids::MIDIMAP))
                {
                    if ((int) m.getProperty (ids::cc) != e.cc) continue;
                    auto tok = juce::StringArray::fromTokens (m.getProperty (ids::target).toString(), ":", {});
                    if (tok.size() < 3) continue;
                    juce::ValueTree node = tok[0] == "ch" ? project.findChannel (tok[1].getIntValue()) : project.mixerTrack (tok[1].getIntValue());
                    if (! node.isValid()) continue;
                    if (tok[2] == "vol") node.setProperty (ids::vol, tok[0] == "ch" ? e.value : e.value * 1.25f, nullptr);
                    else if (tok[2] == "pan") node.setProperty (ids::pan, e.value * 2.0f - 1.0f, nullptr);
                }
            };
            for (int i = 0; i < n1; ++i) handle (ccEvents[(size_t) (s1 + i)]);
            for (int i = 0; i < n2; ++i) handle (ccEvents[(size_t) (s2 + i)]);
            ccFifo.finishedRead (n1 + n2);
        }
    }
    // jam launches
    {
        int s1, n1, s2, n2;
        const int ready = jamFifo.getNumReady();
        if (ready > 0)
        {
            jamFifo.prepareToRead (ready, s1, n1, s2, n2);
            for (int i = 0; i < n1; ++i) jamLog.push_back (jamEvents[(size_t) (s1 + i)]);
            for (int i = 0; i < n2; ++i) jamLog.push_back (jamEvents[(size_t) (s2 + i)]);
            jamFifo.finishedRead (n1 + n2);
        }
    }
}

//==============================================================================
void Engine::startAudioRecording()
{
    recFile = tools::uniqueFile (paths::recordings(), "Recording " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M"), ".wav");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (recFile.createOutputStream().release());
    if (os == nullptr) return;
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (24);
    auto w = wav.createWriterFor (os, opts);
    if (w == nullptr) return;
    {
        const juce::ScopedLock sl (writerLock);
        recWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), recThread, 32768);
    }
    recStartBeat = recordFrom.load() > -1.0e8 ? recordFrom.load() : position.load();
    recLoopStart = project.root.getProperty (ids::loopStart, 0.0);
    recLoopEnd = project.root.getProperty (ids::loopEnd, 0.0);
    recActive = true;
    if (onStatus) onStatus ("Recording audio...");
}

void Engine::finishAudioRecording()
{
    recActive = false;
    {
        const juce::ScopedLock sl (writerLock);
        recWriter.reset();
    }
    samples.invalidate (recFile);
    if (auto data = samples.get (recFile))
    {
        if (data->lengthSeconds() > 0.05)
        {
            project.beginEdit ("Record audio");
            const double secPerBeat = 60.0 / project.tempo();
            const double totalBeats = data->lengthSeconds() / secPerBeat;
            const bool looped = recLoopEnd > recLoopStart + 0.01 && recStartBeat < recLoopEnd && totalBeats > (recLoopEnd - recStartBeat) + 0.25;
            if (! looped)
            {
                project.addAudioClip (recordTrack.load(), recStartBeat, recFile, data->lengthSeconds(), 0.0);
                if (onStatus) onStatus ("Recorded " + recFile.getFileName());
            }
            else
            {
                // loop recording: one clip per pass ("take"), each on its own track; older takes are muted
                std::vector<juce::ValueTree> takes;
                double offsetBeats = 0, start = recStartBeat;
                int k = 0;
                while (offsetBeats < totalBeats - 0.25 && k < 16)
                {
                    const double len = juce::jmin (recLoopEnd - start, totalBeats - offsetBeats);
                    const int track = juce::jmin (numPlaylistTracks - 1, recordTrack.load() + k);
                    auto c = project.addAudioClip (track, start, recFile, data->lengthSeconds(), 0.0);
                    c.setProperty (ids::len, len, nullptr);
                    c.setProperty (ids::offset, offsetBeats * secPerBeat, nullptr);
                    c.setProperty (ids::name, "Take " + juce::String (k + 1), nullptr);
                    takes.push_back (c);
                    offsetBeats += len;
                    start = recLoopStart;
                    ++k;
                }
                for (size_t i = 0; i + 1 < takes.size(); ++i) takes[i].setProperty (ids::mute, true, nullptr);
                if (onStatus) onStatus ("Recorded " + juce::String ((int) takes.size()) + " takes - the last one is playing; right-click a take to unmute it");
            }
        }
    }
}

//==============================================================================
juce::Optional<juce::AudioPlayHead::PositionInfo> Engine::getPosition() const
{
    PositionInfo info;
    double tempo = 140.0;
    {
        const juce::SpinLock::ScopedTryLockType sl (const_cast<juce::SpinLock&> (snapLock));
        if (sl.isLocked() && snapshot != nullptr) tempo = snapshot->tempo;
    }
    const double pos = position.load();
    info.setBpm (tempo);
    info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
    info.setIsPlaying (playing.load());
    info.setPpqPosition (pos);
    info.setPpqPositionOfLastBarStart (std::floor (pos / 4.0) * 4.0);
    info.setTimeInSeconds (pos * 60.0 / tempo);
    info.setTimeInSamples ((juce::int64) (pos * 60.0 / tempo * sampleRate));
    return info;
}

void Engine::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    sampleRate = d->getCurrentSampleRate() > 0 ? d->getCurrentSampleRate() : 44100.0;
    blockSize = juce::jmax (32, d->getCurrentBufferSizeSamples());
    collector.reset (sampleRate);
    prepareAll();
    juce::MessageManager::callAsync ([this] { clipCache.clear(); sync(); });
}

void Engine::audioDeviceStopped() {}

void Engine::prepareAll()
{
    const juce::ScopedLock sl (runtimeLock);
    for (auto& [id, rt] : channelRts)
        if (rt->proc) { prepareProcessor (*rt->proc, rt->numChannels); rt->buf.setSize (rt->numChannels, juce::jmax (blockSize, 2048)); }
    for (auto& [id, rt] : slotRts)
        if (rt->proc) { prepareProcessor (*rt->proc, rt->numChannels); rt->buf.setSize (rt->numChannels, juce::jmax (blockSize, 2048)); }
    for (auto& t : trackRts) t->buf.setSize (2, juce::jmax (blockSize, 2048));
}

void Engine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                               int n, const juce::AudioIODeviceCallbackContext&)
{
    process (in, numIn, out, numOut, n);
}

double Engine::loopEndFor (const Snapshot& s, double& ls) const
{
    if (s.loopEnd > s.loopStart + 0.01) { ls = s.loopStart; return s.loopEnd; }
    ls = 0;
    return s.songEnd > 0 ? std::ceil (s.songEnd / 4.0 - 1.0e-9) * 4.0 : 4.0;
}

//==============================================================================
void Engine::process (const float* const* in, int numIn, float* const* out, int numOut, int n)
{
    juce::ScopedNoDenormals nd;
    for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);

    SnapshotPtr sp;
    {
        const juce::SpinLock::ScopedLockType sl (snapLock);
        sp = snapshot;
    }
    if (sp == nullptr || n <= 0) return;
    const Snapshot& s = *sp;
    const double spb = sampleRate * 60.0 / s.tempo;

    if (double seek = seekRequest.exchange (-1.0); seek >= 0.0) { position = seek; panicRequest = true; }
    const bool doPanic = panicRequest.exchange (false);

    for (auto& ch : s.channels)
    {
        auto& rt = *ch.rt;
        if (rt.buf.getNumSamples() < n) rt.buf.setSize (rt.numChannels, n, false, false, true);
        rt.buf.clear();
        rt.midi.clear(); rt.midiOns.clear();
        if (doPanic)
        {
            rt.midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            for (int k = 0; k < 128; ++k) rt.midi.addEvent (juce::MidiMessage::noteOff (1, k), 0);
        }
    }
    for (auto& t : s.tracks)
    {
        if (t.rt->buf.getNumSamples() < n) t.rt->buf.setSize (2, n, false, false, true);
        t.rt->buf.clear();
    }

    // ---- live input (MIDI keyboard, UI pads, computer keyboard)
    liveMidi.clear();
    collector.removeNextBlockOfMessages (liveMidi, n);
    int sel = -1;
    const int selId = selectedChannel.load();
    for (size_t i = 0; i < s.channels.size(); ++i) if (s.channels[i].id == selId) sel = (int) i;
    const bool isPlaying = playing.load();
    const double pos0 = position.load();
    const bool recordingNotes = isPlaying && recordArmed.load() && getMode() == TransportMode::pattern && sel >= 0 && pos0 >= recordFrom.load() - 1.0e-6;
    const int curPat = currentPattern.load();

    auto pushRecNote = [&] (double start, double len, int key, float vel)
    {
        int s1, n1, s2, n2;
        noteFifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 + n2 > 0)
        {
            recNotes[(size_t) (n1 > 0 ? s1 : s2)] = { selId, curPat, start, len, key, vel };
            noteFifo.finishedWrite (1);
        }
    };

    for (const auto meta : liveMidi)
    {
        auto m = meta.getMessage();
        const double beatNow = pos0 + meta.samplePosition / spb;
        if (m.isController())
        {
            const int cc = m.getControllerNumber();
            if (learning.load()) { learnedCC = cc; learning = false; continue; }
            bool mapped = false;
            for (auto& mm : s.midiMap)
                if (mm.cc == cc)
                {
                    const float v = m.getControllerValue() / 127.0f;
                    mm.target.apply (v, true);
                    mapped = true;
                    if (mm.treeTarget)
                    {
                        int s1, n1, s2, n2;
                        ccFifo.prepareToWrite (1, s1, n1, s2, n2);
                        if (n1 + n2 > 0) { ccEvents[(size_t) (n1 > 0 ? s1 : s2)] = { cc, v }; ccFifo.finishedWrite (1); }
                    }
                }
            if (mapped) continue;
        }
        if (m.isNoteOn())
        {
            heldVel[(size_t) m.getNoteNumber()] = m.getFloatVelocity();
            heldStartBeat[(size_t) m.getNoteNumber()] = beatNow;
        }
        else if (m.isNoteOff())
        {
            int k = m.getNoteNumber();
            if (recordingNotes && heldVel[(size_t) k] > 0 && ! noteRepeat)
                pushRecNote (heldStartBeat[(size_t) k], juce::jmax (0.05, beatNow - heldStartBeat[(size_t) k]), k, heldVel[(size_t) k]);
            heldVel[(size_t) k] = 0;
        }
        if (sel >= 0) s.channels[(size_t) sel].rt->midiOns.addEvent (m, meta.samplePosition);
    }

    // ---- note repeat
    {
        const double rate = juce::jmax (1.0 / 32.0, noteRepeatBeats.load());
        double r0 = isPlaying ? pos0 : repeatClock;
        double r1 = r0 + n / spb;
        if (noteRepeat && sel >= 0)
        {
            for (double g = std::ceil (r0 / rate - 1.0e-9) * rate; g < r1; g += rate)
            {
                int offs = juce::jlimit (0, n - 1, (int) ((g - r0) * spb));
                for (int k = 0; k < 128; ++k)
                {
                    float v = heldVel[(size_t) k];
                    if (v <= 0 || g - heldStartBeat[(size_t) k] < rate * 0.5) continue;
                    auto& rt = *s.channels[(size_t) sel].rt;
                    rt.midi.addEvent (juce::MidiMessage::noteOff (1, k), offs);
                    rt.midiOns.addEvent (juce::MidiMessage::noteOn (1, k, v), offs);
                    if (recordingNotes) pushRecNote (g, rate * 0.5, k, v);
                }
            }
            // record the initial press too
        }
        repeatClock = r1;
    }

    // ---- sequencing
    struct Click { int offset; bool accent; };
    std::array<Click, 16> clicks; int numClicks = 0;
    if (isPlaying)
    {
        double pos = pos0;
        int off = 0, remaining = n;
        const auto m = getMode();
        int guard = 0;
        while (remaining > 0 && guard++ < 64)
        {
            double ls = 0, le = 1.0e12;
            bool looping = true;
            if (m == TransportMode::pattern)
            {
                auto it = s.patternIndex.find (curPat);
                le = (it != s.patternIndex.end()) ? s.patterns[(size_t) it->second].length : (s.patterns.empty() ? 4.0 : s.patterns[0].length);
                if (pos >= le) pos = std::fmod (pos, le);
            }
            else if (m == TransportMode::song)
            {
                le = loopEndFor (s, ls);
                if (pos >= le) pos = ls;
            }
            else looping = false;

            // jam launch quantisation (next bar)
            if (m == TransportMode::jam)
            {
                bool anyQueued = false, anyPlaying = false;
                for (int t = 0; t < numJamTracks; ++t)
                {
                    anyQueued |= jamQueued[(size_t) t].load() != -1;
                    anyPlaying |= jamPlaying[(size_t) t].load() >= 0;
                }
                const bool onBar = std::fmod (pos + 1.0e-7, 4.0) < 1.0e-5;
                if (anyQueued && (onBar || ! anyPlaying || jamPendingStart))
                {
                    for (int t = 0; t < numJamTracks; ++t)
                    {
                        int q = jamQueued[(size_t) t].exchange (-1);
                        if (q == -1) continue;
                        jamPlaying[(size_t) t] = q >= 0 ? q : -1;
                        jamStart[(size_t) t] = pos;
                        if (q == -2 || q >= 0)
                            for (auto& ch : s.channels) ch.rt->midi.addEvent (juce::MidiMessage::allNotesOff (1), off);
                        int s1, n1, s2, n2;
                        jamFifo.prepareToWrite (1, s1, n1, s2, n2);
                        if (n1 + n2 > 0) { jamEvents[(size_t) (n1 > 0 ? s1 : s2)] = { t, q >= 0 ? q : -1, pos }; jamFifo.finishedWrite (1); }
                    }
                    jamPendingStart = false;
                }
                else if (anyQueued)
                {
                    le = std::ceil (pos / 4.0 - 1.0e-9) * 4.0;
                    if (le <= pos + 1.0e-9) le = pos + 4.0;
                    looping = false;
                    double segEnd = pos + remaining / spb;
                    if (le < segEnd)
                    {
                        int segLen = juce::jlimit (1, remaining, (int) std::ceil ((le - pos) * spb - 1.0e-6));
                        scheduleSegment (s, pos, le, off, segLen, spb);
                        off += segLen; remaining -= segLen; pos = le;
                        continue;
                    }
                }
            }

            const double segEndBeat = pos + remaining / spb;
            int segLen = remaining;
            double b1 = segEndBeat;
            bool wrap = false;
            if (looping && le < segEndBeat)
            {
                segLen = juce::jlimit (1, remaining, (int) std::ceil ((le - pos) * spb - 1.0e-6));
                b1 = le;
                wrap = true;
            }
            // metronome clicks
            for (double k = std::ceil (pos - 1.0e-9); k < b1 && numClicks < 16; k += 1.0)
                clicks[(size_t) numClicks++] = { off + juce::jlimit (0, segLen - 1, (int) ((k - pos) * spb)), std::fmod (k, 4.0) < 0.001 };

            scheduleSegment (s, pos, b1, off, segLen, spb);
            off += segLen; remaining -= segLen;
            pos = wrap ? ls : b1;
        }
        position = pos;
    }
    sampleCounter += n;
    if (! isPlaying) freeClock += n / spb; else freeClock = position.load();

    // ---- instruments
    for (auto& ch : s.channels)
    {
        auto& rt = *ch.rt;
        if (rt.proc == nullptr) continue;
        rt.midi.addEvents (rt.midiOns, 0, -1, 0);
        applyMidiTools (rt, n, isPlaying ? pos0 : freeClock, spb);
        const float v = rt.vol.load(), pan = rt.pan.load();
        const float g = v * v * 1.6f;
        const float gl = g * juce::jmin (1.0f, 1.0f - pan), gr = g * juce::jmin (1.0f, 1.0f + pan);
        const bool muted = rt.mute.load();
        if (rt.pads != nullptr)
        {
            rt.pads->renderPads (rt.midi, n);
            float lvl = 0;
            for (int p = 0; p < DrumPadsProcessor::numPads; ++p)
            {
                auto& pb = rt.pads->padOutput (p);
                int target = rt.pads->padOutputTarget (p);
                if (target < 0 || target >= (int) s.tracks.size()) target = ch.mixer;
                if (muted) continue;
                auto& tb = s.tracks[(size_t) target].rt->buf;
                tb.addFrom (0, 0, pb, 0, 0, n, gl);
                tb.addFrom (1, 0, pb, 1, 0, n, gr);
                lvl = juce::jmax (lvl, pb.getMagnitude (0, n));
            }
            rt.level = juce::jmax (lvl * g, rt.level.load() * 0.85f);
            continue;
        }
        juce::AudioBuffer<float> view (rt.buf.getArrayOfWritePointers(), rt.numChannels, n);
        view.clear();
        rt.proc->processBlock (view, rt.midi);
        if (muted) { rt.level = 0; continue; }
        auto& tb = s.tracks[(size_t) ch.mixer].rt->buf;
        tb.addFrom (0, 0, view, 0, 0, n, gl);
        tb.addFrom (1, 0, view, juce::jmin (1, rt.numChannels - 1), 0, n, gr);
        rt.level = juce::jmax (view.getMagnitude (0, n) * g, rt.level.load() * 0.85f);
    }

    // ---- mixer
    if (stemOnly.has_value() && *stemOnly > 0) s.tracks[0].rt->buf.clear();   // stems exclude sounds routed straight to master
    if (s.anySidechain)
        for (auto& t : s.tracks)
        {
            if (t.rt->dry.getNumSamples() < n) t.rt->dry.setSize (2, n, false, false, true);
            t.rt->dry.copyFrom (0, 0, t.rt->buf, 0, 0, n);
            t.rt->dry.copyFrom (1, 0, t.rt->buf, 1, 0, n);
        }
    auto runTrack = [&] (int i)
    {
        auto& tr = s.tracks[(size_t) i];
        auto& tb = tr.rt->buf;
        for (auto& slot : tr.slots)
        {
            if (slot->proc == nullptr || slot->bypass.load()) continue;
            if (slot->buf.getNumSamples() < n) slot->buf.setSize (slot->numChannels, n, false, false, true);
            juce::AudioBuffer<float> view (slot->buf.getArrayOfWritePointers(), slot->numChannels, n);
            view.clear();
            view.copyFrom (0, 0, tb, 0, 0, n);
            view.copyFrom (1, 0, tb, 1, 0, n);
            const int scSrc = slot->sidechain.load();
            if (scSrc >= 0 && scSrc < (int) s.tracks.size() && slot->numChannels >= 4)
            {
                view.copyFrom (2, 0, s.tracks[(size_t) scSrc].rt->dry, 0, 0, n);
                view.copyFrom (3, 0, s.tracks[(size_t) scSrc].rt->dry, 1, 0, n);
            }
            slot->emptyMidi.clear();
            slot->proc->processBlock (view, slot->emptyMidi);
            tb.copyFrom (0, 0, view, 0, 0, n);
            tb.copyFrom (1, 0, view, 1, 0, n);
        }
        const float v = tr.rt->vol.load(), pan = tr.rt->pan.load();
        const float gl = v * juce::jmin (1.0f, 1.0f - pan), gr = v * juce::jmin (1.0f, 1.0f + pan);
        bool silent = tr.rt->mute.load();
        if (i > 0 && s.anySolo && ! tr.rt->solo.load()) silent = true;
        if (i > 0 && stemOnly.has_value() && *stemOnly != i) silent = true;
        if (silent) tb.clear();
        tb.applyGain (0, 0, n, gl);
        tb.applyGain (1, 0, n, gr);
        tr.rt->peakL = juce::jmax (tb.getMagnitude (0, 0, n), tr.rt->peakL.load() * 0.9f);
        tr.rt->peakR = juce::jmax (tb.getMagnitude (1, 0, n), tr.rt->peakR.load() * 0.9f);
    };
    for (int i = 1; i < (int) s.tracks.size(); ++i)
    {
        runTrack (i);
        auto& tr = s.tracks[(size_t) i];
        int route = tr.route;
        if (route <= i || route >= (int) s.tracks.size()) route = 0;
        auto& dest = s.tracks[(size_t) route].rt->buf;
        dest.addFrom (0, 0, tr.rt->buf, 0, 0, n);
        dest.addFrom (1, 0, tr.rt->buf, 1, 0, n);
        if (tr.sendTo >= 0 && tr.sendLevel > 0.0f && (tr.sendTo == 0 || tr.sendTo > i) && tr.sendTo < (int) s.tracks.size())
        {
            auto& sd = s.tracks[(size_t) tr.sendTo].rt->buf;
            sd.addFrom (0, 0, tr.rt->buf, 0, 0, n, tr.sendLevel);
            sd.addFrom (1, 0, tr.rt->buf, 1, 0, n, tr.sendLevel);
        }
    }
    runTrack (0);
    auto& master = s.tracks[0].rt->buf;
    for (int c = 0; c < juce::jmin (2, numOut); ++c)
        if (out[c] != nullptr) juce::FloatVectorOperations::copy (out[c], master.getReadPointer (c), n);
    if (numOut == 1 && out[0] != nullptr) juce::FloatVectorOperations::addWithMultiply (out[0], master.getReadPointer (1), 1.0f, n);

    if (exporting) return;

    // ---- metronome
    if (metronome.load() || (isPlaying && pos0 < recordFrom.load()))
    {
        for (int k = 0; k < numClicks; ++k) { metroSamplesLeft = (int) (sampleRate * 0.03); metroFreq = clicks[(size_t) k].accent ? 1600.0f : 1000.0f; metroPhase = -(double) clicks[(size_t) k].offset; }
        if (metroSamplesLeft > 0)
        {
            for (int i = 0; i < n && metroSamplesLeft > 0; ++i)
            {
                if (metroPhase < 0) { metroPhase += 1; continue; }
                float env = metroSamplesLeft / (float) (sampleRate * 0.03);
                float smp = (float) std::sin (metroPhase * juce::MathConstants<double>::twoPi * metroFreq / sampleRate) * env * 0.35f;
                metroPhase += 1;
                --metroSamplesLeft;
                for (int c = 0; c < juce::jmin (2, numOut); ++c) if (out[c]) out[c][i] += smp;
            }
        }
    }

    // ---- preview
    {
        const juce::SpinLock::ScopedTryLockType sl (previewLock);
        if (sl.isLocked() && previewData != nullptr)
        {
            const auto& pb = previewData->buffer;
            const double inc = previewData->sampleRate / sampleRate;
            const int pch = pb.getNumChannels();
            for (int i = 0; i < n; ++i)
            {
                int i0 = (int) previewIdx;
                if (i0 + 1 >= previewRegion.getEnd()) { previewData.reset(); previewPos = -1.0; break; }
                float fr = (float) (previewIdx - i0);
                for (int c = 0; c < juce::jmin (2, numOut); ++c)
                {
                    int sc = juce::jmin (c, pch - 1);
                    float a = pb.getSample (sc, i0), b = pb.getSample (sc, i0 + 1);
                    if (out[c]) out[c][i] += (a + fr * (b - a)) * 0.8f;
                }
                previewIdx += inc;
            }
            if (previewData != nullptr) previewPos = previewIdx / previewData->sampleRate;
        }
    }

    // ---- audio recording
    if (recActive.load() && isPlaying && numIn > 0 && in != nullptr && pos0 >= recordFrom.load() - 1.0e-6)
    {
        const juce::ScopedTryLock tl (writerLock);
        if (tl.isLocked() && recWriter != nullptr)
        {
            const float* chans[2] = { in[0], numIn > 1 ? in[1] : in[0] };
            recWriter->write (chans, n);
        }
    }
}

//==============================================================================
void Engine::scheduleSegment (const Snapshot& s, double b0, double b1, int off, int len, double spb)
{
    if (len <= 0 || b1 <= b0) return;
    const auto m = getMode();

    if (m == TransportMode::pattern)
    {
        auto it = s.patternIndex.find (currentPattern.load());
        int idx = it != s.patternIndex.end() ? it->second : (s.patterns.empty() ? -1 : 0);
        if (idx >= 0)
        {
            auto& pat = s.patterns[(size_t) idx];
            for (size_t ci = 0; ci < s.channels.size() && ci < pat.notes.size(); ++ci)
                emitNotes (s, (int) ci, pat.notes[ci], 0.0, 0.0, pat.length, b0, b1, off, len, spb);
        }
    }
    else if (m == TransportMode::song)
    {
        for (auto& c : s.clips)
        {
            const double ce = c.start + c.len;
            if (c.kind == 2)
            {
                if (b0 >= c.start && b0 < ce)
                {
                    float t = (float) ((b0 - c.start) / c.len);
                    const auto& pts = c.points;
                    float v = pts.front().y;
                    for (size_t i = 1; i < pts.size(); ++i)
                    {
                        if (t <= pts[i].x)
                        {
                            float span = juce::jmax (1.0e-6f, pts[i].x - pts[i - 1].x);
                            v = pts[i - 1].y + (pts[i].y - pts[i - 1].y) * juce::jlimit (0.0f, 1.0f, (t - pts[i - 1].x) / span);
                            break;
                        }
                        v = pts[i].y;
                    }
                    c.target.apply (v);
                }
                continue;
            }
            if (ce <= b0 || c.start >= b1) continue;
            if (c.kind == 0)
            {
                auto& pat = s.patterns[(size_t) c.patternIdx];
                const double L = pat.length;
                const double from = juce::jmax (b0, c.start) - c.start + c.offset;
                const double to = juce::jmin (b1, ce) - c.start + c.offset;
                for (int k = (int) std::floor (from / L); k <= (int) std::floor ((to - 1.0e-9) / L); ++k)
                {
                    const double ws = c.start - c.offset + k * L;
                    for (size_t ci = 0; ci < s.channels.size() && ci < pat.notes.size(); ++ci)
                        emitNotes (s, (int) ci, pat.notes[ci], ws, c.start, juce::jmin (ce, ws + L), b0, b1, off, len, spb);
                }
            }
            else if (c.kind == 1)
            {
                const double ov0 = juce::jmax (b0, c.start), ov1 = juce::jmin (b1, ce);
                const int o = off + (int) std::llround ((ov0 - b0) * spb);
                const int e = off + (int) std::llround ((ov1 - b0) * spb);
                renderAudio (s, c.audio, c.mixer, ov0 - c.start, c.offsetOut, c.gain, c.fadeIn, c.fadeOut, c.len,
                             juce::jlimit (off, off + len, o), juce::jlimit (0, off + len - o, e - o), spb, false);
            }
        }
    }
    else // jam
    {
        for (int t = 0; t < numJamTracks; ++t)
        {
            int sc = jamPlaying[(size_t) t].load();
            if (sc < 0) continue;
            auto& slot = s.jam[(size_t) t][(size_t) sc];
            const double st = jamStart[(size_t) t];
            if (slot.kind == 0)
            {
                auto& pat = s.patterns[(size_t) slot.patternIdx];
                const double L = pat.length;
                const double from = juce::jmax (b0, st) - st, to = b1 - st;
                if (to <= 0) continue;
                for (int k = (int) std::floor (from / L); k <= (int) std::floor ((to - 1.0e-9) / L); ++k)
                {
                    const double ws = st + k * L;
                    for (size_t ci = 0; ci < s.channels.size() && ci < pat.notes.size(); ++ci)
                        emitNotes (s, (int) ci, pat.notes[ci], ws, st, ws + L, b0, b1, off, len, spb);
                }
            }
            else if (slot.kind == 1 && b1 > st)
            {
                const double ov0 = juce::jmax (b0, st);
                const int o = off + (int) std::llround ((ov0 - b0) * spb);
                renderAudio (s, slot.audio, s.jamMixer[(size_t) t], ov0 - st, 0.0, slot.gain, 0, 0, slot.len,
                             juce::jlimit (off, off + len, o), juce::jlimit (0, off + len - o, off + len - o), spb, true);
            }
        }
    }

    // modulators (LFOs)
    for (auto& md : s.mods)
    {
        double ph = b0 / md.rate;
        ph -= std::floor (ph);
        float w = 0;
        switch (md.shape)
        {
            case 0: w = (float) std::sin (ph * juce::MathConstants<double>::twoPi); break;
            case 1: w = (float) (ph < 0.5 ? ph * 4.0 - 1.0 : 3.0 - ph * 4.0); break;
            case 2: w = (float) (ph * 2.0 - 1.0); break;
            case 3: w = ph < 0.5 ? 1.0f : -1.0f; break;
            default:
            {
                auto cycle = (juce::uint32) std::floor (b0 / md.rate);
                cycle = cycle * 1664525u + 1013904223u;
                w = (float) ((cycle >> 8) & 0xffff) / 32767.5f - 1.0f;
            }
        }
        md.target.apply (md.base + md.depth * w * 0.5f);
    }
}

static int snapToScale (int key, int scale, int root)
{
    static const int scales[8][12] = {
        { 1,1,1,1,1,1,1,1,1,1,1,1 }, { 1,0,1,0,1,1,0,1,0,1,0,1 }, { 1,0,1,1,0,1,0,1,1,0,1,0 }, { 1,0,1,1,0,1,0,1,1,0,0,1 },
        { 1,0,1,0,1,0,0,1,0,1,0,0 }, { 1,0,0,1,0,1,0,1,0,0,1,0 }, { 1,0,0,1,0,1,1,1,0,0,1,0 }, { 1,0,1,1,0,1,0,1,0,1,1,0 } };
    const int sc = juce::jlimit (0, 7, scale);
    for (int d = 0; d < 7; ++d)
    {
        if (scales[sc][((key - d - root) % 12 + 12) % 12]) return key - d;
        if (scales[sc][((key + d - root) % 12 + 12) % 12]) return key + d;
    }
    return key;
}

void Engine::applyMidiTools (ChannelRuntime& rt, int n, double beat0, double spb)
{
    const int arp = rt.arpMode.load(), chord = rt.chordType.load(), scale = rt.scaleType.load();
    if (arp == 0 && chord == 0 && scale == 0 && rt.arpNote < 0 && rt.held.empty()) return;
    static const int chords[10][4] = { { 0, -1, -1, -1 }, { 0, 4, 7, -1 }, { 0, 3, 7, -1 }, { 0, 4, 7, 10 }, { 0, 4, 7, 11 },
                                       { 0, 3, 7, 10 }, { 0, 5, 7, -1 }, { 0, 7, -1, -1 }, { 0, 12, -1, -1 }, { 0, 7, 16, -1 } };
    auto& out = rt.toolOut;
    out.clear();
    for (const auto meta : rt.midi)
    {
        auto m = meta.getMessage();
        if (m.isNoteOn() || m.isNoteOff())
        {
            const int orig = m.getNoteNumber();
            if (m.isNoteOn())
            {
                int key = scale > 0 ? snapToScale (orig, scale, rt.scaleRoot.load()) : orig;
                int cnt = 0;
                for (int k = 0; k < 4; ++k)
                {
                    const int off = chords[juce::jlimit (0, 9, chord)][k];
                    if (off < 0) break;
                    const int nn = juce::jlimit (0, 127, key + off);
                    rt.noteMap[(size_t) orig][(size_t) cnt++] = nn;
                }
                rt.noteMapCount[(size_t) orig] = cnt;
                for (int k = 0; k < cnt; ++k)
                {
                    const int nn = rt.noteMap[(size_t) orig][(size_t) k];
                    if (arp > 0)
                    {
                        if (std::find (rt.held.begin(), rt.held.end(), nn) == rt.held.end()) { rt.held.push_back (nn); rt.heldOrder.push_back (nn); }
                    }
                    else out.addEvent (juce::MidiMessage::noteOn (m.getChannel(), nn, m.getVelocity()), meta.samplePosition);
                }
            }
            else
            {
                const int cnt = rt.noteMapCount[(size_t) orig];
                for (int k = 0; k < (cnt > 0 ? cnt : 1); ++k)
                {
                    const int nn = cnt > 0 ? rt.noteMap[(size_t) orig][(size_t) k] : orig;
                    if (arp > 0 || ! rt.held.empty())
                    {
                        rt.held.erase (std::remove (rt.held.begin(), rt.held.end(), nn), rt.held.end());
                        rt.heldOrder.erase (std::remove (rt.heldOrder.begin(), rt.heldOrder.end(), nn), rt.heldOrder.end());
                    }
                    if (arp == 0) out.addEvent (juce::MidiMessage::noteOff (m.getChannel(), nn), meta.samplePosition);
                }
                rt.noteMapCount[(size_t) orig] = 0;
            }
        }
        else
        {
            if (m.isAllNotesOff() || m.isAllSoundOff()) { rt.held.clear(); rt.heldOrder.clear(); }
            out.addEvent (m, meta.samplePosition);
        }
    }

    if (arp > 0 || rt.arpNote >= 0)
    {
        const double rate = juce::jmax (1.0 / 32.0, (double) rt.arpRate.load());
        const double b1 = beat0 + n / spb;
        auto offsetOf = [&] (double b) { return juce::jlimit (0, n - 1, (int) ((b - beat0) * spb)); };
        if (rt.arpNote >= 0 && (rt.arpOffBeat < b1 || arp == 0 || rt.held.empty()))
        {
            const double ob = juce::jmax (beat0, rt.arpOffBeat);
            out.addEvent (juce::MidiMessage::noteOff (1, rt.arpNote), (arp == 0 || rt.held.empty()) ? 0 : offsetOf (ob));
            rt.arpNote = -1;
        }
        if (arp > 0 && ! rt.held.empty())
        {
            for (double g = std::ceil (beat0 / rate - 1.0e-9) * rate; g < b1; g += rate)
            {
                std::vector<int> seq = arp == 5 ? rt.heldOrder : rt.held;
                if (arp != 5) std::sort (seq.begin(), seq.end());
                const int octs = rt.arpOct.load();
                std::vector<int> full;
                for (int o = 0; o < octs; ++o) for (int k : seq) full.push_back (juce::jmin (127, k + 12 * o));
                if (full.empty()) break;
                const int len = (int) full.size();
                int idx;
                switch (arp)
                {
                    case 2: idx = len - 1 - (rt.arpStep % len); break;
                    case 3: { const int period = juce::jmax (1, 2 * len - 2); int st = rt.arpStep % period; idx = st < len ? st : period - st; break; }
                    case 4: idx = juce::Random::getSystemRandom().nextInt (len); break;
                    default: idx = rt.arpStep % len; break;
                }
                ++rt.arpStep;
                const int off = offsetOf (g);
                if (rt.arpNote >= 0) out.addEvent (juce::MidiMessage::noteOff (1, rt.arpNote), off);
                rt.arpNote = full[(size_t) juce::jlimit (0, len - 1, idx)];
                out.addEvent (juce::MidiMessage::noteOn (1, rt.arpNote, (juce::uint8) 100), off);
                rt.arpOffBeat = g + rate * juce::jlimit (0.05f, 1.0f, rt.arpGate.load());
                if (rt.arpOffBeat < b1)
                {
                    out.addEvent (juce::MidiMessage::noteOff (1, rt.arpNote), offsetOf (rt.arpOffBeat));
                    rt.arpNote = -1;
                }
            }
        }
        else if (rt.held.empty()) rt.arpStep = 0;
    }
    rt.midi.swapWith (out);
}

void Engine::emitNotes (const Snapshot& s, int ci, const std::vector<NoteEv>& notes, double ws, double cs, double ce,
                        double b0, double b1, int off, int len, double spb)
{
    auto& rt = *s.channels[(size_t) ci].rt;
    const double swingShift = s.swing / 12.0;
    for (auto& nt : notes)
    {
        double gs = ws + nt.start;
        if (gs < cs - 1.0e-9 || gs >= ce - 1.0e-9) continue;
        double ge = juce::jmin (ws + nt.end, ce);
        if (swingShift > 0 && std::abs (std::fmod (nt.start, 0.5) - 0.25) < 1.0e-6) { gs += swingShift; ge = juce::jmin (ce, ge + swingShift); }
        if (gs >= b0 && gs < b1)
            rt.midiOns.addEvent (juce::MidiMessage::noteOn (1, nt.key, juce::jlimit (0.01f, 1.0f, nt.vel)),
                                 off + juce::jlimit (0, len - 1, (int) ((gs - b0) * spb)));
        const bool endsHere = ge > gs && ge >= b0 && (ge < b1 || (ge >= ce - 1.0e-9 && ge <= b1 + 1.0e-9));
        if (endsHere)
            rt.midi.addEvent (juce::MidiMessage::noteOff (1, nt.key), off + juce::jlimit (0, len - 1, (int) ((ge - b0) * spb)));
    }
}

void Engine::renderAudio (const Snapshot& s, const ProcessedPtr& a, int mixer, double lb0, double offsetOut, float gain,
                          double fadeIn, double fadeOut, double clipLen, int off, int len, double spb, bool loop)
{
    if (a == nullptr || len <= 0 || mixer < 0 || mixer >= (int) s.tracks.size()) return;
    auto& tb = s.tracks[(size_t) mixer].rt->buf;
    const auto& src = a->buffer;
    const int total = src.getNumSamples();
    const auto* L = src.getReadPointer (0);
    const auto* R = src.getReadPointer (juce::jmin (1, src.getNumChannels() - 1));
    auto* dl = tb.getWritePointer (0);
    auto* dr = tb.getWritePointer (1);
    for (int i = 0; i < len; ++i)
    {
        double lb = lb0 + i / spb;
        if (loop) lb = std::fmod (lb, clipLen);
        else if (lb >= clipLen) break;
        double idx = offsetOut + lb * spb;
        int i0 = (int) idx;
        if (i0 < 0 || i0 + 1 >= total) continue;
        float fr = (float) (idx - i0);
        float env = gain;
        if (fadeIn > 0 && lb < fadeIn) env *= (float) (lb / fadeIn);
        if (fadeOut > 0 && lb > clipLen - fadeOut) env *= (float) juce::jmax (0.0, (clipLen - lb) / fadeOut);
        if (loop)
        {
            const double edge = 0.004 * spb;   // tiny anti-click at loop seam
            double toEnd = (clipLen - lb) * spb;
            if (toEnd < 64) env *= (float) (toEnd / 64.0);
            juce::ignoreUnused (edge);
        }
        dl[off + i] += (L[i0] + fr * (L[i0 + 1] - L[i0])) * env;
        dr[off + i] += (R[i0] + fr * (R[i0 + 1] - R[i0])) * env;
    }
}

//==============================================================================
bool Engine::exportAudio (const ExportOptions& opt, std::function<bool (float)> progress, juce::String& error)
{
    // NB: call storeStatesToTree() + syncNow() on the message thread before this (it may run on a worker thread)
    SnapshotPtr sp;
    { const juce::SpinLock::ScopedLockType sl (snapLock); sp = snapshot; }
    if (sp == nullptr) { error = "Nothing to export"; return false; }

    const bool songEmpty = sp->songEnd <= 0.0;
    const bool patternOnly = opt.patternOnly || songEmpty;
    double lengthBeats = std::ceil (sp->songEnd / 4.0 - 1.0e-9) * 4.0;
    if (patternOnly)
    {
        auto it = sp->patternIndex.find (currentPattern.load());
        lengthBeats = it != sp->patternIndex.end() ? sp->patterns[(size_t) it->second].length : 4.0;
    }
    const double tailBeats = 2.0;
    const double spb = sampleRate * 60.0 / sp->tempo;
    const auto savedMode = mode.load();
    const auto savedPos = position.load();

    std::vector<std::optional<int>> passes;
    if (opt.stems)
    {
        std::set<int> used;
        for (auto& c : sp->channels) used.insert (c.mixer);
        for (auto& c : sp->channels) if (c.rt->pads) for (int p = 0; p < DrumPadsProcessor::numPads; ++p) if (c.rt->pads->padOutputTarget (p) > 0) used.insert (c.rt->pads->padOutputTarget (p));
        for (auto& c : sp->clips) if (c.kind == 1) used.insert (c.mixer);
        for (int t : used) passes.push_back (t);
        passes.push_back (std::nullopt);
    }
    else passes.push_back (std::nullopt);

    devices.removeAudioCallback (this);
    exporting = true;
    {
        const juce::ScopedLock sl (runtimeLock);
        for (auto& [id, rt] : channelRts) if (rt->proc) rt->proc->setNonRealtime (true);
        for (auto& [id, rt] : slotRts) if (rt->proc) rt->proc->setNonRealtime (true);
    }

    bool ok = true;
    const int block = 512;
    juce::AudioBuffer<float> buf (2, block);
    for (size_t pi = 0; pi < passes.size() && ok; ++pi)
    {
        stemOnly = passes[pi];
        juce::File target = opt.file;
        if (stemOnly.has_value())
        {
            auto name = *stemOnly == 0 ? juce::String ("Master (direct)")
                      : opt.trackNames.size() > *stemOnly ? opt.trackNames[*stemOnly] : "Insert " + juce::String (*stemOnly);
            target = opt.file.getSiblingFile (opt.file.getFileNameWithoutExtension() + " - " + juce::File::createLegalFileName (name))
                         .withFileExtension (opt.file.getFileExtension());
        }
        std::unique_ptr<juce::AudioFormat> fmt;
        if (opt.format == 1) fmt = std::make_unique<juce::FlacAudioFormat>();
        else if (opt.format == 2) fmt = std::make_unique<juce::OggVorbisAudioFormat>();
        else fmt = std::make_unique<juce::WavAudioFormat>();
        target.deleteFile();
        std::unique_ptr<juce::OutputStream> os (target.createOutputStream().release());
        int bits = opt.format == 2 ? 16 : (opt.format == 1 ? juce::jmin (24, opt.bits) : opt.bits);
        auto wopts = juce::AudioFormatWriterOptions().withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (bits);
        if (opt.format == 2) wopts = wopts.withQualityOptionIndex (8);
        std::unique_ptr<juce::AudioFormatWriter> writer = os ? fmt->createWriterFor (os, wopts) : nullptr;
        if (writer == nullptr) { error = "Could not write " + target.getFullPathName(); ok = false; break; }

        mode = patternOnly ? (int) TransportMode::pattern : (int) TransportMode::song;
        position = 0.0; seekRequest = -1.0; panicRequest = true;
        playing = true;
        const juce::int64 total = (juce::int64) ((lengthBeats + tailBeats) * spb);
        const juce::int64 playEnd = (juce::int64) (lengthBeats * spb);
        juce::int64 done = 0;
        float peak = 0;
        while (done < total)
        {
            int nb = (int) std::min<juce::int64> (block, total - done);
            if (done < playEnd) nb = (int) std::min<juce::int64> (nb, playEnd - done);   // stop exactly at the end
            if (done >= playEnd && playing) { playing = false; panicRequest = true; }
            float* ptrs[2] = { buf.getWritePointer (0), buf.getWritePointer (1) };
            process (nullptr, 0, ptrs, 2, nb);
            writer->writeFromAudioSampleBuffer (buf, 0, nb);
            peak = juce::jmax (peak, buf.getMagnitude (0, nb));
            done += nb;
            if (progress && ! progress ((float) ((pi + (double) done / (double) total) / passes.size()))) { ok = false; error = "Cancelled"; break; }
        }
        playing = false;
        writer.reset();
        if (stemOnly.has_value() && peak < 1.0e-6f) target.deleteFile();   // skip silent stems
    }

    stemOnly.reset();
    exporting = false;
    {
        const juce::ScopedLock sl (runtimeLock);
        for (auto& [id, rt] : channelRts) if (rt->proc) { rt->proc->setNonRealtime (false); rt->proc->reset(); }
        for (auto& [id, rt] : slotRts) if (rt->proc) { rt->proc->setNonRealtime (false); rt->proc->reset(); }
    }
    mode = savedMode;
    position = savedPos;
    panicRequest = true;
    devices.addAudioCallback (this);
    return ok;
}
}
