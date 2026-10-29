#include "BrowserPanel.h"

namespace gubee
{
BrowserPanel::BrowserPanel (AppContext& c) : ctx (c)
{
    addAndMakeVisible (importBtn);
    importBtn.setTooltip ("Import a sample pack (folder or .zip). You can also drag packs from Explorer onto this panel.");
    importBtn.onClick = [this] { importMenu(); };

    addAndMakeVisible (search);
    search.setTextToShowWhenEmpty ("Search sounds...  (e.g. kick 140, dark loop, Cmin)", col::textDim);
    search.onTextChange = [this] { refilter(); };

    addAndMakeVisible (typeBox);
    typeBox.addItemList (Library::types(), 1);
    typeBox.setSelectedId (1, juce::dontSendNotification);
    typeBox.onChange = [this] { refilter(); };

    addAndMakeVisible (packBox);
    packBox.onChange = [this] { refilter(); };

    addAndMakeVisible (list);
    list.setRowHeight (24);
    list.setMultipleSelectionEnabled (false);
    list.setColour (juce::ListBox::backgroundColourId, col::panel);

    ctx.library.addChangeListener (this);
    changeListenerCallback (nullptr);
}

BrowserPanel::~BrowserPanel() { ctx.library.removeChangeListener (this); }

void BrowserPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    all = ctx.library.getAll();
    auto cur = packBox.getText();
    packBox.clear (juce::dontSendNotification);
    packBox.addItem ("All packs", 1);
    int i = 2, sel = 1;
    for (auto& p : ctx.library.getPacks()) { packBox.addItem (p, i); if (p == cur) sel = i; ++i; }
    packBox.setSelectedId (sel, juce::dontSendNotification);
    if (ctx.library.status != lastStatus) { lastStatus = ctx.library.status; ctx.status (lastStatus); }
    refilter();
}

void BrowserPanel::refilter()
{
    auto words = juce::StringArray::fromTokens (search.getText().toLowerCase(), " ", "\"");
    words.removeEmptyStrings();
    auto type = typeBox.getText();
    auto pack = packBox.getSelectedId() > 1 ? packBox.getText() : juce::String();
    shown.clear();
    for (auto& s : all)
    {
        if (pack.isNotEmpty() && s.pack != pack) continue;
        if (type == "Favorites" && ! ctx.library.isFavorite (s.file)) continue;
        if (type == "Loop" && ! s.loop) continue;
        if (type == "One-shot" && (s.loop || s.midi)) continue;
        if (type == "MIDI" && ! s.midi) continue;
        if (type != "All" && type != "Favorites" && type != "Loop" && type != "One-shot" && type != "MIDI" && s.type != type) continue;
        bool ok = true;
        for (auto& w : words)
        {
            if (w.containsOnly ("0123456789") && w.length() >= 2)
            { if (std::abs (s.bpm - w.getIntValue()) > 0.5 && ! s.searchText.contains (w)) { ok = false; break; } }
            else if (! s.searchText.contains (w)) { ok = false; break; }
        }
        if (ok) shown.push_back (s);
    }
    list.updateContent();
    list.repaint();
    repaint();
}

void BrowserPanel::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    g.setColour (col::textDim);
    g.setFont (juce::FontOptions (11.0f));
    auto foot = getLocalBounds().removeFromBottom (20).reduced (6, 0);
    g.drawText (ctx.library.isScanning() ? juce::String ("Scanning library...") : juce::String ((int) shown.size()) + " of " + juce::String ((int) all.size()) + " sounds",
                foot, juce::Justification::centredLeft);
    g.drawText ("click = preview, drag = use", foot, juce::Justification::centredRight);
    if (dropHover)
    {
        g.setColour (col::accent.withAlpha (0.25f));
        g.fillRect (getLocalBounds());
        g.setColour (col::accent);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText ("Drop to import pack", getLocalBounds(), juce::Justification::centred);
    }
    g.setColour (col::outline);
    g.drawVerticalLine (getWidth() - 1, 0, (float) getHeight());
}

void BrowserPanel::resized()
{
    auto r = getLocalBounds().reduced (6);
    r.removeFromBottom (16);
    importBtn.setBounds (r.removeFromTop (28));
    r.removeFromTop (4);
    search.setBounds (r.removeFromTop (26));
    r.removeFromTop (4);
    auto row = r.removeFromTop (24);
    typeBox.setBounds (row.removeFromLeft (row.getWidth() / 2 - 2));
    row.removeFromLeft (4);
    packBox.setBounds (row);
    r.removeFromTop (4);
    list.setBounds (r);
}

void BrowserPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) shown.size()) return;
    auto& s = shown[(size_t) row];
    if (selected) { g.setColour (col::accent.withAlpha (0.2f)); g.fillRect (0, 0, w, h); }
    juce::Rectangle<int> r (4, 0, w - 8, h);
    // type pill
    auto pill = r.removeFromLeft (58).reduced (0, 4);
    int ci = Library::types().indexOf (s.type);
    g.setColour (col::fromIndex (ci).withAlpha (0.8f));
    g.fillRoundedRectangle (pill.toFloat(), 3);
    g.setColour (col::bg);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (s.midi ? juce::String ("MIDI") : s.type, pill, juce::Justification::centred);
    r.removeFromLeft (6);
    if (ctx.library.isFavorite (s.file)) { g.setColour (col::warn); g.drawText (juce::CharPointer_UTF8 ("\xe2\x98\x85"), r.removeFromRight (14), juce::Justification::centred); }
    juce::String meta;
    if (s.bpm > 0) meta << juce::String (s.bpm, 0) << " ";
    if (s.key.isNotEmpty()) meta << s.key;
    g.setColour (col::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (meta, r.removeFromRight (64), juce::Justification::centredRight);
    g.setColour (col::text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (s.name, r, juce::Justification::centredLeft, true);
}

void BrowserPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) shown.size()) return;
    if (e.mods.isPopupMenu()) { rowMenu (row); return; }
    auto& s = shown[(size_t) row];
    if (! s.midi) ctx.engine->previewFile (s.file);
}

void BrowserPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) shown.size()) useSample (shown[(size_t) row]);
}

juce::var BrowserPanel::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (rows.size() == 0 || rows[0] >= (int) shown.size()) return {};
    return dragFilePrefix + shown[(size_t) rows[0]].file.getFullPathName();
}

void BrowserPanel::useSample (const SampleInfo& s)
{
    auto& pr = ctx.project;
    if (s.midi)
    {
        // import MIDI notes into the current pattern for the selected channel
        juce::MidiFile mf2;
        juce::FileInputStream in2 (s.file);
        if (! in2.openedOk() || ! mf2.readFrom (in2)) { ctx.status ("Couldn't read MIDI file"); return; }
        const double ppq = mf2.getTimeFormat() > 0 ? mf2.getTimeFormat() : 96;
        pr.beginEdit ("Import MIDI");
        int ch = ctx.selectedChannel();
        int pat = ctx.currentPattern();
        int count = 0;
        for (int t = 0; t < mf2.getNumTracks(); ++t)
        {
            juce::MidiMessageSequence seq (*mf2.getTrack (t));
            seq.updateMatchedPairs();
            for (int i = 0; i < seq.getNumEvents(); ++i)
            {
                auto* ev = seq.getEventPointer (i);
                if (! ev->message.isNoteOn()) continue;
                double st = ev->message.getTimeStamp() / ppq;
                double en = ev->noteOffObject ? ev->noteOffObject->message.getTimeStamp() / ppq : st + 0.25;
                pr.addNote (pat, ch, st, juce::jmax (0.05, en - st), ev->message.getNoteNumber(), ev->message.getFloatVelocity());
                ++count;
            }
        }
        ctx.status ("Imported " + juce::String (count) + " notes into the current pattern");
        if (ctx.showView) ctx.showView (viewPiano);
        return;
    }
    pr.beginEdit ("Add sampler");
    auto c = ctx.addSamplerChannel (s.file);
    ctx.selectChannel ((int) c.getProperty (ids::id));
    ctx.status ("Added " + s.name + " to the Channel Rack");
}

void BrowserPanel::sendToPlaylist (AppContext& ctx, const juce::File& f, int track, double beat)
{
    auto data = ctx.samples.get (f);
    if (data == nullptr) { ctx.status ("Can't read " + f.getFileName()); return; }
    double bpm = tools::bpmFromName (f.getFileNameWithoutExtension());
    SampleInfo info; info.file = f; Library::classify (info, f.getFileName());
    if (bpm <= 0 && (info.loop || data->lengthSeconds() > 1.5)) bpm = tools::guessLoopBpm (data->lengthSeconds());
    if (! info.loop && bpm > 0 && tools::bpmFromName (f.getFileNameWithoutExtension()) <= 0 && data->lengthSeconds() < 1.5) bpm = 0;
    ctx.project.beginEdit ("Add audio clip");
    ctx.project.addAudioClip (track, beat, f, data->lengthSeconds(), bpm);
}

void BrowserPanel::rowMenu (int row)
{
    auto s = shown[(size_t) row];
    juce::PopupMenu m;
    m.addItem (1, "Preview");
    m.addItem (2, s.midi ? "Import notes into current pattern" : "Add to Channel Rack (Sampler)");
    if (! s.midi)
    {
        m.addItem (3, "Put on next empty pad of selected Drum Pads");
        m.addItem (4, "Send to Playlist at playhead");
        m.addItem (5, "Open in Chop Shop");
    }
    m.addItem (6, ctx.library.isFavorite (s.file) ? "Remove from favorites" : "Add to favorites");
    m.addItem (7, "Show in Explorer");
    m.showMenuAsync ({}, [this, s] (int r)
    {
        if (r == 1) ctx.engine->previewFile (s.file);
        else if (r == 2) useSample (s);
        else if (r == 3)
        {
            auto ch = ctx.project.findChannel (ctx.selectedChannel());
            if (ch.getProperty (ids::kind).toString() != "pads") { ctx.status ("Select a Drum Pads channel first"); return; }
            for (int i = 0; i < DrumPadsProcessor::numPads; ++i)
                if (! ctx.project.getPad (ctx.selectedChannel(), i).isValid())
                {
                    ctx.project.beginEdit ("Assign pad");
                    ctx.project.setPadSample (ctx.selectedChannel(), i, s.file);
                    ctx.status (s.name + " -> pad " + juce::String (i + 1));
                    return;
                }
            ctx.status ("All 16 pads are full");
        }
        else if (r == 4) sendToPlaylist (ctx, s.file, 0, std::floor (ctx.engine->getBeat()));
        else if (r == 5 && ctx.openInChopShop) ctx.openInChopShop (s.file, -1, {});
        else if (r == 6) { ctx.library.toggleFavorite (s.file); refilter(); }
        else if (r == 7) s.file.revealToUser();
    });
}

void BrowserPanel::importMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Import pack folder (copies into GubeeLoops)...");
    m.addItem (2, "Import pack .zip...");
    m.addItem (3, "Use a folder in place (no copy)...");
    if (! ctx.library.getLinkedFolders().isEmpty())
    {
        juce::PopupMenu un;
        int i = 0;
        for (auto& f : ctx.library.getLinkedFolders()) un.addItem (100 + i++, f.getFullPathName());
        m.addSubMenu ("Stop using folder", un);
    }
    m.addItem (4, "Open packs folder");
    m.addItem (5, "Rescan library");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&importBtn), [this] (int r)
    {
        if (r == 1 || r == 3)
        {
            chooser = std::make_unique<juce::FileChooser> ("Choose a pack folder", juce::File::getSpecialLocation (juce::File::userHomeDirectory));
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, r] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (! f.isDirectory()) return;
                if (r == 1) ctx.library.importPack (f); else ctx.library.linkFolder (f);
            });
        }
        else if (r == 2)
        {
            chooser = std::make_unique<juce::FileChooser> ("Choose a pack .zip", juce::File::getSpecialLocation (juce::File::userHomeDirectory), "*.zip");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult().existsAsFile()) ctx.library.importPack (fc.getResult());
            });
        }
        else if (r == 4) paths::packs().startAsProcess();
        else if (r == 5) ctx.library.rescan();
        else if (r >= 100) ctx.library.unlinkFolder (ctx.library.getLinkedFolders()[r - 100]);
    });
}

void BrowserPanel::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    for (auto& f : files) ctx.library.importPack (juce::File (f));
    repaint();
}
}
