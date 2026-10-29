#include "Project.h"

namespace gubee
{
Project::Project() : root (ids::PROJECT)
{
    newProject (false);
}

void Project::ensureStructure()
{
    for (auto& t : { ids::CHANNELS, ids::PATTERNS, ids::PLAYLIST, ids::MIXER, ids::JAM, ids::MODS })
        if (! root.getChildWithName (t).isValid()) root.appendChild (juce::ValueTree (t), nullptr);

    auto pl = playlist();
    while (pl.getNumChildren() < numPlaylistTracks)
    {
        juce::ValueTree t (ids::TRACK);
        t.setProperty (ids::name, "Track " + juce::String (pl.getNumChildren() + 1), nullptr);
        pl.appendChild (t, nullptr);
    }
    // clips are stored in the playlist node too (after TRACK children); keep tracks first.
    auto mx = mixer();
    while (mx.getNumChildren() < numMixerTracks)
    {
        int i = mx.getNumChildren();
        juce::ValueTree t (ids::MTRACK);
        t.setProperty (ids::name, i == 0 ? juce::String ("Master") : "Insert " + juce::String (i), nullptr);
        t.setProperty (ids::vol, i == 0 ? 0.9f : 0.8f, nullptr);
        t.setProperty (ids::pan, 0.0f, nullptr);
        t.setProperty (ids::route, i == 0 ? -1 : 0, nullptr);
        t.setProperty (ids::sendTo, -1, nullptr);
        t.setProperty (ids::sendLevel, 0.0f, nullptr);
        mx.appendChild (t, nullptr);
    }
    auto jm = jam();
    auto countJam = [&] { int n = 0; for (auto c : jm) n += c.hasType (ids::JTRACK) ? 1 : 0; return n; };
    for (int n = countJam(); n < numJamTracks; ++n)
    {
        juce::ValueTree t (ids::JTRACK);
        t.setProperty (ids::index, n, nullptr);
        t.setProperty (ids::name, "Jam " + juce::String (n + 1), nullptr);
        t.setProperty (ids::mixer, 0, nullptr);
        jm.appendChild (t, nullptr);
    }
    if (! root.hasProperty (ids::tempo)) root.setProperty (ids::tempo, 140.0, nullptr);
    if (! root.hasProperty (ids::swing)) root.setProperty (ids::swing, 0.0f, nullptr);
    if (! root.hasProperty (ids::nextId)) root.setProperty (ids::nextId, 1, nullptr);
    if (! root.hasProperty (ids::loopStart)) root.setProperty (ids::loopStart, 0.0, nullptr);
    if (! root.hasProperty (ids::loopEnd)) root.setProperty (ids::loopEnd, 0.0, nullptr);
    if (patterns().getNumChildren() == 0) addPattern ("Pattern 1");
}

void Project::newProject (bool withDefaultKit)
{
    juce::ValueTree fresh (ids::PROJECT);
    fresh.setProperty (ids::version, 1, nullptr);
    fresh.setProperty (ids::name, "Untitled", nullptr);
    root.copyPropertiesAndChildrenFrom (fresh, nullptr);
    ensureStructure();
    if (withDefaultKit)
    {
        addChannel ("builtin", "Gubee 808", "Gubee 808");
        addChannel ("builtin", "Gubee Synth", "Gubee Synth");
    }
    undo.clearUndoHistory();
    file = juce::File();
    dirty = false;
}

bool Project::load (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName (ids::PROJECT.toString())) return false;
    auto tree = juce::ValueTree::fromXml (*xml);
    root.copyPropertiesAndChildrenFrom (tree, nullptr);
    ensureStructure();
    undo.clearUndoHistory();
    file = f;
    dirty = false;
    return true;
}

bool Project::save (const juce::File& f)
{
    if (auto xml = root.createXml())
    {
        f.getParentDirectory().createDirectory();
        juce::TemporaryFile tmp (f);
        if (xml->writeTo (tmp.getFile()) && tmp.overwriteTargetFileWithTemporary())
        {
            if (! f.getFileNameWithoutExtension().startsWithIgnoreCase ("autosave"))
            {
                file = f;
                dirty = false;
            }
            return true;
        }
    }
    return false;
}

int Project::allocId()
{
    int id = (int) root.getProperty (ids::nextId, 1);
    root.setProperty (ids::nextId, id + 1, nullptr);
    return id;
}

//==============================================================================
juce::ValueTree Project::addChannel (const juce::String& kind, const juce::String& name, const juce::String& builtin,
                                     const juce::String& pluginXml, const juce::File& sample)
{
    juce::ValueTree c (ids::CHANNEL);
    c.setProperty (ids::id, allocId(), nullptr);
    c.setProperty (ids::name, name, nullptr);
    c.setProperty (ids::kind, kind, nullptr);
    c.setProperty (ids::builtin, builtin, nullptr);
    c.setProperty (ids::plugin, pluginXml, nullptr);
    c.setProperty (ids::sample, sample.getFullPathName(), nullptr);
    c.setProperty (ids::vol, 0.78f, nullptr);
    c.setProperty (ids::pan, 0.0f, nullptr);
    c.setProperty (ids::mute, false, nullptr);
    c.setProperty (ids::mixer, firstFreeMixerTrack(), nullptr);
    c.setProperty (ids::colour, channels().getNumChildren() % 10, nullptr);
    c.setProperty (ids::root, 60, nullptr);
    channels().appendChild (c, um());
    return c;
}

juce::ValueTree Project::findChannel (int id) const
{
    return channels().getChildWithProperty (ids::id, id);
}

int Project::channelIndex (int id) const
{
    return channels().indexOf (findChannel (id));
}

void Project::removeChannel (int id)
{
    removeNotesForChannel (id);
    auto c = findChannel (id);
    if (c.isValid()) channels().removeChild (c, um());
}

int Project::firstFreeMixerTrack() const
{
    std::set<int> used;
    for (auto c : channels()) used.insert ((int) c.getProperty (ids::mixer, 0));
    for (int i = 1; i < numMixerTracks; ++i)
        if (used.count (i) == 0) return i;
    return 0;
}

//==============================================================================
juce::ValueTree Project::addPattern (const juce::String& name)
{
    juce::ValueTree p (ids::PATTERN);
    p.setProperty (ids::id, allocId(), nullptr);
    p.setProperty (ids::name, name.isNotEmpty() ? name : "Pattern " + juce::String (patterns().getNumChildren() + 1), nullptr);
    p.setProperty (ids::length, 4.0, nullptr);
    p.setProperty (ids::colour, patterns().getNumChildren() % 10, nullptr);
    patterns().appendChild (p, um());
    return p;
}

juce::ValueTree Project::findPattern (int id) const
{
    return patterns().getChildWithProperty (ids::id, id);
}

juce::ValueTree Project::findOrCreatePattern (int id)
{
    auto p = findPattern (id);
    if (! p.isValid()) p = addPattern();
    return p;
}

juce::ValueTree Project::findNote (const juce::ValueTree& pattern, int channel, double start, int key) const
{
    for (auto n : pattern)
        if ((int) n.getProperty (ids::ch) == channel
            && std::abs ((double) n.getProperty (ids::start) - start) < 1.0e-6
            && (key < 0 || (int) n.getProperty (ids::key) == key))
            return n;
    return {};
}

void Project::toggleStep (int patternId, int channelId, int step, int rootKey)
{
    auto p = findPattern (patternId);
    if (! p.isValid()) return;
    double start = step * stepBeats;
    juce::ValueTree existing;
    for (auto n : p)
        if ((int) n.getProperty (ids::ch) == channelId && std::abs ((double) n.getProperty (ids::start) - start) < 1.0e-6)
            existing = n;
    if (existing.isValid()) p.removeChild (existing, um());
    else addNote (patternId, channelId, start, stepBeats, rootKey, 0.78f);
}

juce::ValueTree Project::addNote (int patternId, int channelId, double start, double len, int key, float vel)
{
    auto p = findPattern (patternId);
    if (! p.isValid()) return {};
    juce::ValueTree n (ids::NOTE);
    n.setProperty (ids::ch, channelId, nullptr);
    n.setProperty (ids::start, start, nullptr);
    n.setProperty (ids::len, len, nullptr);
    n.setProperty (ids::key, key, nullptr);
    n.setProperty (ids::vel, vel, nullptr);
    p.appendChild (n, um());
    double end = start + len;
    double plen = (double) p.getProperty (ids::length, 4.0);
    if (end > plen + 1.0e-6) p.setProperty (ids::length, std::ceil (end / 4.0) * 4.0, um());
    return n;
}

void Project::removeNotesForChannel (int channelId)
{
    for (auto p : patterns())
        for (int i = p.getNumChildren(); --i >= 0;)
            if ((int) p.getChild (i).getProperty (ids::ch) == channelId) p.removeChild (i, um());
}

double Project::patternLengthBeats (int patternId) const
{
    auto p = findPattern (patternId);
    return p.isValid() ? juce::jmax (0.25, (double) p.getProperty (ids::length, 4.0)) : 4.0;
}

//==============================================================================
juce::ValueTree Project::addPatternClip (int track, double start, int patternId)
{
    juce::ValueTree c (ids::CLIP);
    c.setProperty (ids::kind, "pattern", nullptr);
    c.setProperty (ids::track, track, nullptr);
    c.setProperty (ids::start, start, nullptr);
    c.setProperty (ids::len, patternLengthBeats (patternId), nullptr);
    c.setProperty (ids::pattern, patternId, nullptr);
    c.setProperty (ids::offset, 0.0, nullptr);
    playlist().appendChild (c, um());
    return c;
}

juce::ValueTree Project::addAudioClip (int track, double start, const juce::File& f, double lengthSeconds, double srcBpm)
{
    juce::ValueTree c (ids::CLIP);
    c.setProperty (ids::kind, "audio", nullptr);
    c.setProperty (ids::name, f.getFileNameWithoutExtension(), nullptr);
    c.setProperty (ids::track, track, nullptr);
    c.setProperty (ids::start, start, nullptr);
    bool stretch = srcBpm > 0;
    double beats = lengthSeconds * (stretch ? srcBpm : tempo()) / 60.0;
    c.setProperty (ids::len, juce::jmax (0.25, beats), nullptr);
    c.setProperty (ids::file, f.getFullPathName(), nullptr);
    c.setProperty (ids::offset, 0.0, nullptr);
    c.setProperty (ids::gain, 0.0f, nullptr);
    c.setProperty (ids::fadeIn, 0.0, nullptr);
    c.setProperty (ids::fadeOut, 0.0, nullptr);
    c.setProperty (ids::reverse, false, nullptr);
    c.setProperty (ids::stretch, stretch, nullptr);
    c.setProperty (ids::srcBpm, stretch ? srcBpm : tempo(), nullptr);
    c.setProperty (ids::pitch, 0.0, nullptr);
    playlist().appendChild (c, um());
    return c;
}

juce::ValueTree Project::addAutomationClip (int track, double start, double len, const juce::String& target, const juce::String& name)
{
    juce::ValueTree c (ids::CLIP);
    c.setProperty (ids::kind, "auto", nullptr);
    c.setProperty (ids::name, name, nullptr);
    c.setProperty (ids::track, track, nullptr);
    c.setProperty (ids::start, start, nullptr);
    c.setProperty (ids::len, len, nullptr);
    c.setProperty (ids::target, target, nullptr);
    c.setProperty (ids::points, "0,0.8;1,0.8", nullptr);
    playlist().appendChild (c, um());
    return c;
}

double Project::songLengthBeats() const
{
    double end = 0;
    for (auto c : playlist())
        if (c.hasType (ids::CLIP))
            end = juce::jmax (end, (double) c.getProperty (ids::start) + (double) c.getProperty (ids::len));
    return end;
}

//==============================================================================
juce::ValueTree Project::getPad (int channelId, int pad) const
{
    auto c = findChannel (channelId);
    for (auto p : c) if (p.hasType (ids::PAD) && (int) p.getProperty (ids::index) == pad) return p;
    return {};
}

juce::ValueTree Project::setPadSample (int channelId, int pad, const juce::File& f, juce::Range<int> region)
{
    auto c = findChannel (channelId);
    if (! c.isValid()) return {};
    auto p = getPad (channelId, pad);
    if (! p.isValid())
    {
        p = juce::ValueTree (ids::PAD);
        p.setProperty (ids::index, pad, nullptr);
        p.setProperty (ids::gain, 1.0f, nullptr);
        p.setProperty (ids::pan, 0.0f, nullptr);
        p.setProperty (ids::pitch, 0.0f, nullptr);
        p.setProperty (ids::reverse, false, nullptr);
        p.setProperty (ids::choke, 0, nullptr);
        p.setProperty (ids::output, -1, nullptr);
        c.appendChild (p, um());
    }
    p.setProperty (ids::sample, f.getFullPathName(), um());
    p.setProperty (ids::name, f.getFileNameWithoutExtension(), um());
    p.setProperty (ids::start, region.getStart(), um());
    p.setProperty (ids::end, region.getEnd(), um());
    return p;
}

//==============================================================================
juce::ValueTree Project::getJamSlot (int track, int scene) const
{
    for (auto s : jam())
        if (s.hasType (ids::JSLOT) && (int) s.getProperty (ids::track) == track && (int) s.getProperty (ids::scene) == scene)
            return s;
    return {};
}

juce::ValueTree Project::setJamPattern (int track, int scene, int patternId)
{
    clearJamSlot (track, scene);
    juce::ValueTree s (ids::JSLOT);
    s.setProperty (ids::track, track, nullptr);
    s.setProperty (ids::scene, scene, nullptr);
    s.setProperty (ids::kind, "pattern", nullptr);
    s.setProperty (ids::pattern, patternId, nullptr);
    jam().appendChild (s, um());
    return s;
}

juce::ValueTree Project::setJamAudio (int track, int scene, const juce::File& f, double lengthBeats, double srcBpm)
{
    clearJamSlot (track, scene);
    juce::ValueTree s (ids::JSLOT);
    s.setProperty (ids::track, track, nullptr);
    s.setProperty (ids::scene, scene, nullptr);
    s.setProperty (ids::kind, "audio", nullptr);
    s.setProperty (ids::file, f.getFullPathName(), nullptr);
    s.setProperty (ids::name, f.getFileNameWithoutExtension(), nullptr);
    s.setProperty (ids::len, juce::jmax (1.0, lengthBeats), nullptr);
    s.setProperty (ids::srcBpm, srcBpm, nullptr);
    s.setProperty (ids::stretch, srcBpm > 0, nullptr);
    s.setProperty (ids::gain, 0.0f, nullptr);
    jam().appendChild (s, um());
    return s;
}

void Project::clearJamSlot (int track, int scene)
{
    auto s = getJamSlot (track, scene);
    if (s.isValid()) jam().removeChild (s, um());
}

juce::ValueTree Project::addModulator (const juce::String& target, const juce::String& name)
{
    juce::ValueTree m (ids::MOD);
    m.setProperty (ids::id, allocId(), nullptr);
    m.setProperty (ids::name, name, nullptr);
    m.setProperty (ids::target, target, nullptr);
    m.setProperty (ids::shape, 0, nullptr);
    m.setProperty (ids::rate, 1.0, nullptr);      // beats per cycle
    m.setProperty (ids::depth, 0.3f, nullptr);
    m.setProperty (ids::base, 0.5f, nullptr);
    m.setProperty (ids::enabled, true, nullptr);
    mods().appendChild (m, um());
    return m;
}

//==============================================================================
juce::ValueTree Project::getSlot (int track, int slot) const
{
    auto t = mixerTrack (track);
    for (auto s : t) if ((int) s.getProperty (ids::index) == slot) return s;
    return {};
}

juce::ValueTree Project::setSlot (int track, int slot, const juce::String& kind, const juce::String& builtin, const juce::String& pluginXml)
{
    clearSlot (track, slot);
    juce::ValueTree s (ids::SLOT);
    s.setProperty (ids::id, allocId(), nullptr);
    s.setProperty (ids::index, slot, nullptr);
    s.setProperty (ids::kind, kind, nullptr);
    s.setProperty (ids::builtin, builtin, nullptr);
    s.setProperty (ids::plugin, pluginXml, nullptr);
    s.setProperty (ids::bypass, false, nullptr);
    mixerTrack (track).appendChild (s, um());
    return s;
}

void Project::clearSlot (int track, int slot)
{
    auto s = getSlot (track, slot);
    if (s.isValid()) mixerTrack (track).removeChild (s, um());
}

//==============================================================================
juce::String Project::slicesToString (const std::vector<juce::Range<int>>& s)
{
    juce::StringArray a;
    for (auto& r : s) a.add (juce::String (r.getStart()) + ":" + juce::String (r.getEnd()));
    return a.joinIntoString (",");
}

std::vector<juce::Range<int>> Project::slicesFromString (const juce::String& s)
{
    std::vector<juce::Range<int>> out;
    for (auto& part : juce::StringArray::fromTokens (s, ",", {}))
        if (part.contains (":"))
            out.push_back ({ part.upToFirstOccurrenceOf (":", false, false).getIntValue(),
                             part.fromFirstOccurrenceOf (":", false, false).getIntValue() });
    return out;
}

juce::String Project::pointsToString (const std::vector<juce::Point<float>>& p)
{
    juce::StringArray a;
    for (auto& pt : p) a.add (juce::String (pt.x, 4) + "," + juce::String (pt.y, 4));
    return a.joinIntoString (";");
}

std::vector<juce::Point<float>> Project::pointsFromString (const juce::String& s)
{
    std::vector<juce::Point<float>> out;
    for (auto& part : juce::StringArray::fromTokens (s, ";", {}))
        if (part.contains (","))
            out.push_back ({ part.upToFirstOccurrenceOf (",", false, false).getFloatValue(),
                             part.fromFirstOccurrenceOf (",", false, false).getFloatValue() });
    std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.x < b.x; });
    return out;
}
}
