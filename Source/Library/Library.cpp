#include "Library.h"
#include "App/Paths.h"
#include "Audio/AudioTools.h"

namespace gubee
{
Library::Library()
{
    loadSettings();
}

Library::~Library()
{
    pool.removeAllJobs (true, 5000);
}

bool Library::isAudioFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg");
}

void Library::classify (SampleInfo& info, const juce::String& relPath)
{
    auto p = relPath.toLowerCase().replaceCharacters ("\\/_-.()[]", "         ");
    auto name = info.file.getFileNameWithoutExtension().toLowerCase();
    auto has = [&] (std::initializer_list<const char*> words, const juce::String& in)
    {
        auto padded = " " + in + " ";
        for (auto* w : words)
        {
            juce::String ws (w);
            if (ws.endsWith ("*")) { if (padded.contains (" " + ws.dropLastCharacters (1))) return true; }
            else if (padded.contains (" " + ws + " ") || padded.contains (" " + ws + "s ")) return true;
        }
        return false;
    };
    info.midi = info.file.hasFileExtension ("mid;midi");
    info.bpm = tools::bpmFromName (info.file.getFileNameWithoutExtension());
    info.key = tools::keyFromName (info.file.getFileNameWithoutExtension());
    info.loop = has ({ "loop*", "break*", "groove*" }, p) || info.bpm > 0;

    auto pick = [&] (const juce::String& in) -> juce::String
    {
        if (has ({ "808*" }, in)) return "808";
        if (has ({ "kick*", "kik", "bd", "bassdrum" }, in)) return "Kick";
        if (has ({ "snare*", "snr", "sd", "rimshot" }, in)) return "Snare";
        if (has ({ "clap*", "clp", "snap*" }, in)) return "Clap";
        if (has ({ "open hat", "openhat*", "ohh", "oh", "open hh", "open" }, in) && has ({ "hat*", "hh", "oh", "ohh", "openhat*" }, in)) return "Open Hat";
        if (has ({ "hat*", "hh", "hihat*", "hi hat*", "chh", "ch" }, in)) return "Hat";
        if (has ({ "crash*", "cymbal*", "ride*", "splash" }, in)) return "Cymbal";
        if (has ({ "perc*", "shaker*", "conga*", "bongo*", "rim*", "tom*", "cowbell*", "tamb*", "clave*", "wood*" }, in)) return "Perc";
        if (has ({ "vox", "vocal*", "voc", "acapella*", "chant*", "adlib*" }, in)) return "Vocal";
        if (has ({ "fx", "sfx", "riser*", "impact*", "sweep*", "downlifter*", "uplifter*", "transition*", "noise*", "whoosh*" }, in)) return "FX";
        if (has ({ "bass*", "sub*", "reese*" }, in)) return "Bass";
        if (has ({ "melod*", "piano*", "keys", "key", "guitar*", "synth*", "pad*", "lead*", "chord*", "pluck*", "bell*", "string*", "arp*", "flute*", "brass*" }, in)) return "Melodic";
        return {};
    };
    info.type = pick (name);
    if (info.type.isEmpty()) info.type = pick (p);
    if (info.type.isEmpty()) info.type = info.loop ? "Loop" : "Other";
    info.searchText = (relPath + " " + info.type + " " + info.key + (info.loop ? " loop" : " oneshot")).toLowerCase();
}

std::vector<juce::String> Library::getPacks() const
{
    const juce::ScopedLock sl (lock);
    std::vector<juce::String> packs;
    for (auto& i : items)
        if (std::find (packs.begin(), packs.end(), i.pack) == packs.end()) packs.push_back (i.pack);
    std::sort (packs.begin(), packs.end());
    return packs;
}

void Library::rescan()
{
    const int gen = ++scanGeneration;
    scanning = true;
    auto roots = juce::Array<juce::File> { paths::packs() };
    roots.addArray (linked);
    pool.addJob ([this, roots, gen]
    {
        std::vector<SampleInfo> found;
        for (auto& root : roots)
        {
            if (! root.isDirectory()) continue;
            const bool isPacks = root == paths::packs();
            for (const auto& entry : juce::RangedDirectoryIterator (root, true, "*", juce::File::findFiles))
            {
                if (scanGeneration.load() != gen) return;
                auto f = entry.getFile();
                if (! isAudioFile (f) && ! f.hasFileExtension ("mid;midi")) continue;
                SampleInfo info;
                info.file = f;
                auto rel = f.getRelativePathFrom (root);
                info.pack = isPacks ? rel.upToFirstOccurrenceOf (juce::File::getSeparatorString(), false, false) : root.getFileName();
                if (info.pack == rel) info.pack = isPacks ? juce::String ("Loose Sounds") : root.getFileName();
                info.name = f.getFileNameWithoutExtension();
                classify (info, rel);
                found.push_back (std::move (info));
            }
        }
        std::sort (found.begin(), found.end(), [] (auto& a, auto& b)
        {
            if (a.pack != b.pack) return a.pack.compareNatural (b.pack) < 0;
            return a.file.getFullPathName().compareNatural (b.file.getFullPathName()) < 0;
        });
        juce::MessageManager::callAsync ([this, found = std::move (found), gen]() mutable
        {
            if (scanGeneration.load() != gen) return;
            {
                const juce::ScopedLock sl (lock);
                items = std::move (found);
            }
            scanning = false;
            status = juce::String ((int) items.size()) + " sounds";
            sendChangeMessage();
        });
    });
}

void Library::importPack (const juce::File& source)
{
    status = "Importing " + source.getFileName() + "...";
    sendChangeMessage();
    pool.addJob ([this, source]
    {
        juce::String result;
        auto destBase = paths::packs();
        if (source.isDirectory())
        {
            auto dest = tools::uniqueFile (destBase, source.getFileName(), "");
            result = source.copyDirectoryTo (dest) ? "Imported pack: " + dest.getFileName() : "Import failed: " + source.getFileName();
        }
        else if (source.hasFileExtension ("zip"))
        {
            juce::ZipFile zip (source);
            auto dest = tools::uniqueFile (destBase, source.getFileNameWithoutExtension(), "");
            dest.createDirectory();
            auto r = zip.uncompressTo (dest, true);
            // If the zip contained a single top folder, flatten one level.
            juce::Array<juce::File> kids = dest.findChildFiles (juce::File::findFilesAndDirectories, false);
            if (kids.size() == 1 && kids[0].isDirectory())
            {
                auto inner = kids[0];
                auto tmp = dest.getSiblingFile (dest.getFileName() + "_tmp");
                inner.moveFileTo (tmp);
                dest.deleteRecursively();
                tmp.moveFileTo (dest);
            }
            result = r.wasOk() ? "Imported pack: " + dest.getFileName() : "Zip import failed: " + r.getErrorMessage();
        }
        else if (isAudioFile (source) || source.hasFileExtension ("mid;midi"))
        {
            auto dir = destBase.getChildFile ("Loose Sounds");
            dir.createDirectory();
            source.copyFileTo (tools::uniqueFile (dir, source.getFileNameWithoutExtension(), source.getFileExtension()));
            result = "Imported " + source.getFileName();
        }
        else if (source.hasFileExtension ("rar;7z"))
            result = "RAR/7z packs aren't supported yet - please extract it first, then import the folder.";
        else
            result = "Not a pack: " + source.getFileName();

        juce::MessageManager::callAsync ([this, result]
        {
            status = result;
            sendChangeMessage();
            rescan();
        });
    });
}

void Library::linkFolder (const juce::File& folder)
{
    if (folder.isDirectory() && ! linked.contains (folder)) linked.add (folder);
    saveSettings();
    rescan();
}

void Library::unlinkFolder (const juce::File& folder)
{
    linked.removeAllInstancesOf (folder);
    saveSettings();
    rescan();
}

void Library::toggleFavorite (const juce::File& f)
{
    auto p = f.getFullPathName();
    if (favorites.contains (p)) favorites.removeString (p); else favorites.add (p);
    saveSettings();
    sendChangeMessage();
}

void Library::saveSettings()
{
    juce::XmlElement x ("LIBRARY");
    for (auto& f : linked) x.createNewChildElement ("LINK")->setAttribute ("path", f.getFullPathName());
    for (auto& f : favorites) x.createNewChildElement ("FAV")->setAttribute ("path", f);
    x.writeTo (paths::settings().getChildFile ("library.xml"));
}

void Library::loadSettings()
{
    if (auto x = juce::XmlDocument::parse (paths::settings().getChildFile ("library.xml")))
    {
        for (auto* e : x->getChildWithTagNameIterator ("LINK")) linked.add (juce::File (e->getStringAttribute ("path")));
        for (auto* e : x->getChildWithTagNameIterator ("FAV")) favorites.add (e->getStringAttribute ("path"));
    }
}
}
