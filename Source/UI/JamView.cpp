#include "JamView.h"
#include "BrowserPanel.h"

namespace gubee
{
static constexpr int headerH = 46, sceneW = 90;

JamView::JamView (AppContext& c) : ctx (c)
{
    addAndMakeVisible (stopAll);
    stopAll.onClick = [this] { ctx.engine->stopAllJam(); };
    addAndMakeVisible (capture);
    capture.setTooltip ("Turns what you just jammed into clips in the Playlist");
    capture.onClick = [this] { ctx.engine->captureJamToPlaylist(); };
    ctx.project.root.addListener (this);
    startTimerHz (20);
}

JamView::~JamView() { ctx.project.root.removeListener (this); }

juce::ValueTree JamView::jamTrack (int t) const
{
    for (auto n : ctx.project.jam()) if (n.hasType (ids::JTRACK) && (int) n.getProperty (ids::index) == t) return n;
    return {};
}

juce::Rectangle<int> JamView::cellRect (int t, int s) const
{
    const int cw = (grid.getWidth() - sceneW) / numJamTracks;
    const int ch = (grid.getHeight() - headerH) / numJamScenes;
    return { grid.getX() + t * cw, grid.getY() + headerH + s * ch, cw, ch };
}

bool JamView::cellAt (juce::Point<int> p, int& t, int& s) const
{
    for (t = 0; t < numJamTracks; ++t)
        for (s = 0; s < numJamScenes; ++s)
            if (cellRect (t, s).contains (p)) return true;
    return false;
}

void JamView::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    auto head = getLocalBounds().removeFromTop (36);
    g.setColour (col::panel);
    g.fillRect (head);
    g.setColour (col::text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("Jam View", head.reduced (10, 0), juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Drop loops or patterns into slots, click to launch (starts on the next bar). Scenes launch a whole row.",
                head.withTrimmedLeft (100).withTrimmedRight (330).reduced (6, 0), juce::Justification::centredLeft);

    const auto& eng = *ctx.engine;
    const bool blink = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
    const double beat = eng.getBeat();
    for (int t = 0; t < numJamTracks; ++t)
    {
        auto jt = jamTrack (t);
        auto hr = cellRect (t, 0).withY (grid.getY()).withHeight (headerH).reduced (2);
        g.setColour (col::panel);
        g.fillRoundedRectangle (hr.toFloat(), 4);
        g.setColour (col::text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (jt.getProperty (ids::name).toString(), hr.withHeight (20), juce::Justification::centred, true);
        g.setColour (col::textDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0 stop     \xe2\x86\x92 ")) + juce::String ((int) jt.getProperty (ids::mixer, 0)),
                    hr.withTrimmedTop (22), juce::Justification::centred);

        const int playing = eng.jamPlayingScene (t), queued = eng.jamQueuedScene (t);
        for (int s = 0; s < numJamScenes; ++s)
        {
            auto r = cellRect (t, s).reduced (3);
            auto slot = ctx.project.getJamSlot (t, s);
            if (! slot.isValid())
            {
                g.setColour (r.contains (hover) ? col::accent.withAlpha (0.25f) : col::panel.darker (0.1f));
                g.fillRoundedRectangle (r.toFloat(), 4);
                g.setColour (col::outline);
                g.drawRoundedRectangle (r.toFloat(), 4, 1);
                continue;
            }
            const bool isPattern = slot.getProperty (ids::kind).toString() == "pattern";
            juce::String name;
            juce::Colour colour;
            double len = 4;
            if (isPattern)
            {
                auto p = ctx.project.findPattern ((int) slot.getProperty (ids::pattern));
                name = p.getProperty (ids::name).toString();
                colour = col::fromIndex ((int) p.getProperty (ids::colour, 0));
                len = ctx.project.patternLengthBeats ((int) slot.getProperty (ids::pattern));
            }
            else
            {
                name = slot.getProperty (ids::name).toString();
                colour = juce::Colour (0xff5fa8c4);
                len = (double) slot.getProperty (ids::len, 4.0);
            }
            const bool isPlaying = playing == s && eng.isPlaying();
            g.setColour (colour.withAlpha (isPlaying ? 0.9f : 0.45f));
            g.fillRoundedRectangle (r.toFloat(), 4);
            if (queued == s && blink) { g.setColour (juce::Colours::white.withAlpha (0.35f)); g.fillRoundedRectangle (r.toFloat(), 4); }
            if (isPlaying)
            {
                // progress bar through the loop
                double prog = std::fmod (juce::jmax (0.0, beat), len) / len;
                g.setColour (juce::Colours::white.withAlpha (0.6f));
                g.fillRect (r.removeFromBottom (4).withWidth ((int) (r.getWidth() * prog)));
            }
            g.setColour (isPlaying ? col::bg : col::text);
            g.setFont (juce::FontOptions (12.0f));
            juce::String icon = isPlaying ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6 ")) : juce::String();
            g.drawFittedText (icon + name, r.reduced (6, 2), juce::Justification::centredLeft, 2);
        }
    }
    // scene launchers
    for (int s = 0; s < numJamScenes; ++s)
    {
        auto r = cellRect (numJamTracks - 1, s);
        r = r.withX (r.getRight()).withWidth (sceneW).reduced (4);
        g.setColour (col::panelHi);
        g.fillRoundedRectangle (r.toFloat(), 4);
        g.setColour (col::accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6 Scene ")) + juce::String (s + 1), r, juce::Justification::centred);
    }
}

void JamView::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (36).reduced (6, 5);
    capture.setBounds (head.removeFromRight (180)); head.removeFromRight (6);
    stopAll.setBounds (head.removeFromRight (80));
    grid = r.reduced (8);
}

void JamView::mouseDown (const juce::MouseEvent& e)
{
    // header clicks
    for (int t = 0; t < numJamTracks; ++t)
    {
        auto hr = cellRect (t, 0).withY (grid.getY()).withHeight (headerH);
        if (! hr.contains (e.getPosition())) continue;
        auto jt = jamTrack (t);
        if (e.y > hr.getY() + 22 && e.x < hr.getCentreX() && ! e.mods.isPopupMenu()) { ctx.engine->stopJamTrack (t); return; }
        juce::PopupMenu m;
        m.addItem (1000, "Rename...");
        m.addItem (1001, "Stop this track");
        m.addSectionHeader ("Audio loops go to mixer:");
        for (int i = 0; i < numMixerTracks; ++i) m.addItem (1 + i, juce::String (i) + "  " + ctx.project.mixerTrack (i).getProperty (ids::name).toString());
        m.showMenuAsync ({}, [this, jt, t] (int r) mutable
        {
            auto& pr = ctx.project;
            if (r == 1000) askForText ("Rename jam track", jt.getProperty (ids::name), [this, jt] (juce::String s) mutable { ctx.project.beginEdit ("Rename"); jt.setProperty (ids::name, s, ctx.project.um()); });
            else if (r == 1001) ctx.engine->stopJamTrack (t);
            else if (r > 0) { pr.beginEdit ("Route jam"); jt.setProperty (ids::mixer, r - 1, pr.um()); }
        });
        return;
    }
    // scene launchers
    for (int s = 0; s < numJamScenes; ++s)
    {
        auto r = cellRect (numJamTracks - 1, s);
        if (r.withX (r.getRight()).withWidth (sceneW).contains (e.getPosition())) { ctx.engine->launchScene (s); return; }
    }
    int t, s;
    if (! cellAt (e.getPosition(), t, s)) return;
    auto slot = ctx.project.getJamSlot (t, s);
    if (! slot.isValid()) { emptyCellMenu (t, s); return; }
    if (e.mods.isPopupMenu()) { filledCellMenu (t, s); return; }
    if (ctx.engine->jamPlayingScene (t) == s && ctx.engine->isPlaying()) ctx.engine->stopJamTrack (t);
    else ctx.engine->launchJam (t, s);
}

void JamView::emptyCellMenu (int t, int s)
{
    juce::PopupMenu m;
    m.addItem (1, "Put the current pattern here");
    juce::PopupMenu pats;
    int i = 0;
    for (auto p : ctx.project.patterns()) pats.addItem (100 + i++, p.getProperty (ids::name).toString());
    m.addSubMenu ("Put a pattern here", pats);
    m.addItem (2, "Put an audio loop here...");
    m.showMenuAsync ({}, [this, t, s] (int r)
    {
        auto& pr = ctx.project;
        if (r == 1) { pr.beginEdit ("Jam slot"); pr.setJamPattern (t, s, ctx.currentPattern()); }
        else if (r >= 100) { pr.beginEdit ("Jam slot"); pr.setJamPattern (t, s, (int) pr.patterns().getChild (r - 100).getProperty (ids::id)); }
        else if (r == 2)
        {
            chooser = std::make_unique<juce::FileChooser> ("Choose a loop", paths::packs(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, t, s] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (! f.existsAsFile()) return;
                auto data = ctx.samples.get (f);
                if (data == nullptr) return;
                double bpm = tools::bpmFromName (f.getFileNameWithoutExtension());
                if (bpm <= 0) bpm = tools::guessLoopBpm (data->lengthSeconds());
                double beats = bpm > 0 ? std::round (data->lengthSeconds() * bpm / 60.0) : std::round (data->lengthSeconds() * ctx.project.tempo() / 60.0);
                ctx.project.beginEdit ("Jam slot");
                ctx.project.setJamAudio (t, s, f, juce::jmax (1.0, beats), bpm);
            });
        }
    });
}

void JamView::filledCellMenu (int t, int s)
{
    auto slot = ctx.project.getJamSlot (t, s);
    const bool isPattern = slot.getProperty (ids::kind).toString() == "pattern";
    juce::PopupMenu m;
    m.addItem (1, "Launch");
    if (isPattern) m.addItem (2, "Edit this pattern");
    else
    {
        m.addItem (3, "Stretch to tempo", true, (bool) slot.getProperty (ids::stretch));
        m.addItem (4, "Set loop length (beats)...");
        m.addItem (5, "Set original BPM...");
    }
    m.addItem (6, "Clear slot");
    m.showMenuAsync ({}, [this, slot, t, s] (int r) mutable
    {
        auto& pr = ctx.project;
        if (r == 1) ctx.engine->launchJam (t, s);
        else if (r == 2) { ctx.setCurrentPattern ((int) slot.getProperty (ids::pattern)); if (ctx.showView) ctx.showView (viewRack); }
        else if (r == 3) { pr.beginEdit ("Stretch"); slot.setProperty (ids::stretch, ! (bool) slot.getProperty (ids::stretch), pr.um()); }
        else if (r == 4) askForText ("Loop length in beats", slot.getProperty (ids::len).toString(), [this, slot] (juce::String v) mutable
                         { ctx.project.beginEdit ("Loop length"); slot.setProperty (ids::len, juce::jmax (0.25, v.getDoubleValue()), ctx.project.um()); });
        else if (r == 5) askForText ("Original BPM", slot.getProperty (ids::srcBpm).toString(), [this, slot] (juce::String v) mutable
                         { ctx.project.beginEdit ("BPM"); slot.setProperty (ids::srcBpm, juce::jlimit (20.0, 400.0, v.getDoubleValue()), ctx.project.um()); slot.setProperty (ids::stretch, true, ctx.project.um()); });
        else if (r == 6) { pr.beginEdit ("Clear slot"); pr.clearJamSlot (t, s); }
    });
}

void JamView::itemDropped (const SourceDetails& d)
{
    hover = { -1, -1 };
    auto f = fileFromDrag (d.description);
    int t, s;
    if (! f.existsAsFile() || ! cellAt (d.localPosition, t, s)) return;
    auto data = ctx.samples.get (f);
    if (data == nullptr) return;
    double bpm = tools::bpmFromName (f.getFileNameWithoutExtension());
    if (bpm <= 0) bpm = tools::guessLoopBpm (data->lengthSeconds());
    double beats = bpm > 0 ? std::round (data->lengthSeconds() * bpm / 60.0) : std::round (data->lengthSeconds() * ctx.project.tempo() / 60.0);
    ctx.project.beginEdit ("Jam slot");
    ctx.project.setJamAudio (t, s, f, juce::jmax (1.0, beats), bpm);
    repaint();
}
}
