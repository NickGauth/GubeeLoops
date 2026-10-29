#include "TopBar.h"

namespace gubee
{
static void styleToggle (juce::TextButton& b)
{
    b.setClickingTogglesState (false);
    b.setColour (juce::TextButton::buttonOnColourId, col::accent);
    b.setColour (juce::TextButton::textColourOnId, col::bg);
}

TopBar::TopBar (AppContext& c) : ctx (c)
{
    for (auto* b : { &fileBtn, &editBtn, &toolsBtn })
    {
        addAndMakeVisible (*b);
        b->onClick = [this, b] { if (onMenu) onMenu (b->getButtonText(), b); };
    }

    playBtn.setButtonText (juce::CharPointer_UTF8 ("\xe2\x96\xb6"));   // ▶
    stopBtn.setButtonText (juce::CharPointer_UTF8 ("\xe2\x96\xa0"));   // ■
    recBtn.setButtonText (juce::CharPointer_UTF8 ("\xe2\x97\x8f"));    // ●
    playBtn.setTooltip ("Play / Pause (Space)");
    stopBtn.setTooltip ("Stop and return to start");
    recBtn.setTooltip ("Record: audio input in Song mode, notes in Pattern mode (R)");
    for (auto* b : { &playBtn, &stopBtn, &recBtn, &patModeBtn, &songModeBtn, &jamModeBtn, &metroBtn }) { styleToggle (*b); addAndMakeVisible (*b); }
    recBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe05050));
    playBtn.onClick = [this] { ctx.engine->togglePlay(); };
    stopBtn.onClick = [this] { ctx.engine->stop(); };
    recBtn.onClick = [this] { ctx.engine->recordArmed = ! ctx.engine->recordArmed; };
    patModeBtn.onClick = [this] { ctx.engine->setMode (TransportMode::pattern); };
    songModeBtn.onClick = [this] { ctx.engine->setMode (TransportMode::song); };
    jamModeBtn.onClick = [this] { ctx.engine->setMode (TransportMode::jam); if (onShowView) onShowView (viewJam); };
    patModeBtn.setTooltip ("Pattern mode: loop the current pattern (L toggles)");
    songModeBtn.setTooltip ("Song mode: play the Playlist arrangement");
    jamModeBtn.setTooltip ("Jam mode: launch loops live from the Jam View");
    metroBtn.onClick = [this] { ctx.engine->metronome = ! ctx.engine->metronome; };

    addAndMakeVisible (tempo);
    tempo.suffix = " BPM";
    tempo.setValue (ctx.project.tempo());
    tempo.onDragStart = [this] { ctx.project.beginEdit ("Tempo"); };
    tempo.onChange = [this] (double v) { ctx.project.root.setProperty (ids::tempo, v, ctx.project.um()); };

    addAndMakeVisible (swing);
    swing.setRange (0, 1, 0);
    swing.setTooltip ("Swing");
    swing.setPopupDisplayEnabled (true, true, nullptr);
    swing.setValue ((double) ctx.project.root.getProperty (ids::swing, 0.0), juce::dontSendNotification);
    swing.onDragStart = [this] { ctx.project.beginEdit ("Swing"); };
    swing.onValueChange = [this] { ctx.project.root.setProperty (ids::swing, swing.getValue(), ctx.project.um()); };

    addAndMakeVisible (patternBox);
    patternBox.setTooltip ("Current pattern (+ / - to switch)");
    patternBox.onChange = [this]
    {
        if (refreshing) return;
        int idx = patternBox.getSelectedItemIndex();
        if (idx >= 0 && idx < ctx.project.patterns().getNumChildren())
            ctx.setCurrentPattern ((int) ctx.project.patterns().getChild (idx).getProperty (ids::id));
    };
    addAndMakeVisible (patMenuBtn);
    patMenuBtn.setTooltip ("New / clone / rename / delete pattern");
    patMenuBtn.onClick = [this] { patternMenu(); };

    const char* names[] = { "Playlist  F5", "Channel Rack  F6", "Piano Roll  F7", "Mixer  F9", "Jam View  F8", "Chop Shop  F10" };
    for (int i = 0; i < numViews; ++i)
    {
        auto* b = viewBtns.add (new juce::TextButton (names[i]));
        styleToggle (*b);
        b->onClick = [this, i] { if (onShowView) onShowView (i); };
        addAndMakeVisible (b);
    }

    ctx.addChangeListener (this);
    ctx.project.root.addListener (this);
    refreshPatterns();
    startTimerHz (30);
}

TopBar::~TopBar()
{
    ctx.removeChangeListener (this);
    ctx.project.root.removeListener (this);
}

void TopBar::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& id)
{
    if (t == ctx.project.root && id == ids::tempo) tempo.setValue (ctx.project.tempo());
    if (t == ctx.project.root && id == ids::swing) swing.setValue ((double) t.getProperty (id), juce::dontSendNotification);
    if (t.hasType (ids::PATTERN) && id == ids::name) refreshPatterns();
}

void TopBar::refreshPatterns()
{
    const juce::ScopedValueSetter<bool> s (refreshing, true);
    patternBox.clear (juce::dontSendNotification);
    int i = 1, sel = 0;
    for (auto p : ctx.project.patterns())
    {
        patternBox.addItem (p.getProperty (ids::name).toString(), i);
        if ((int) p.getProperty (ids::id) == ctx.currentPattern()) sel = i;
        ++i;
    }
    patternBox.setSelectedId (sel, juce::dontSendNotification);
    tempo.setValue (ctx.project.tempo());
}

void TopBar::patternMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "New pattern");
    m.addItem (2, "Clone pattern");
    m.addItem (3, "Rename pattern...");
    m.addItem (4, "Set length: 1 bar");
    m.addItem (5, "Set length: 2 bars");
    m.addItem (6, "Set length: 4 bars");
    m.addItem (7, "Set length: 8 bars");
    m.addSeparator();
    m.addItem (8, "Delete pattern", ctx.project.patterns().getNumChildren() > 1);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patMenuBtn), [this] (int r)
    {
        auto& pr = ctx.project;
        auto cur = pr.findPattern (ctx.currentPattern());
        if (r == 1) { pr.beginEdit ("New pattern"); auto p = pr.addPattern(); ctx.setCurrentPattern ((int) p.getProperty (ids::id)); }
        else if (r == 2 && cur.isValid())
        {
            pr.beginEdit ("Clone pattern");
            auto p = pr.addPattern (cur.getProperty (ids::name).toString() + " copy");
            p.setProperty (ids::length, cur.getProperty (ids::length), pr.um());
            for (auto n : cur) p.appendChild (n.createCopy(), pr.um());
            ctx.setCurrentPattern ((int) p.getProperty (ids::id));
        }
        else if (r == 3 && cur.isValid())
            askForText ("Rename pattern", cur.getProperty (ids::name), [this, cur] (juce::String t) mutable
            { ctx.project.beginEdit ("Rename"); cur.setProperty (ids::name, t, ctx.project.um()); });
        else if (r >= 4 && r <= 7 && cur.isValid())
        {
            const double bars[] = { 1, 2, 4, 8 };
            pr.beginEdit ("Pattern length");
            cur.setProperty (ids::length, bars[r - 4] * 4.0, pr.um());
        }
        else if (r == 8 && cur.isValid())
        {
            pr.beginEdit ("Delete pattern");
            int id = (int) cur.getProperty (ids::id);
            auto pl = pr.playlist();
            for (int i = pl.getNumChildren(); --i >= 0;)
                if (pl.getChild (i).hasType (ids::CLIP) && (int) pl.getChild (i).getProperty (ids::pattern, -1) == id
                    && pl.getChild (i).getProperty (ids::kind).toString() == "pattern")
                    pl.removeChild (i, pr.um());
            pr.patterns().removeChild (cur, pr.um());
            ctx.ensureValidSelection();
        }
        refreshPatterns();
    });
}

void TopBar::setActiveView (int v)
{
    for (int i = 0; i < viewBtns.size(); ++i) viewBtns[i]->setToggleState (i == v, juce::dontSendNotification);
}

void TopBar::timerCallback()
{
    auto& e = *ctx.engine;
    playBtn.setToggleState (e.isPlaying(), juce::dontSendNotification);
    recBtn.setToggleState (e.recordArmed.load(), juce::dontSendNotification);
    metroBtn.setToggleState (e.metronome.load(), juce::dontSendNotification);
    auto m = e.getMode();
    patModeBtn.setToggleState (m == TransportMode::pattern, juce::dontSendNotification);
    songModeBtn.setToggleState (m == TransportMode::song, juce::dontSendNotification);
    jamModeBtn.setToggleState (m == TransportMode::jam, juce::dontSendNotification);
    if (auto t = e.trackRuntime (0)) { meterL = t->peakL.load(); meterR = t->peakR.load(); }
    if (statusText.isNotEmpty() && juce::Time::getMillisecondCounter() - statusTime > 6000) statusText.clear();
    repaint (timeArea); repaint (cpuArea); repaint (meterArea); repaint (statusArea);
}

void TopBar::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    g.setColour (col::outline);
    g.drawHorizontalLine (getHeight() - 1, 0, (float) getWidth());
    drawCatLogo (g, logoArea.toFloat());
    g.setColour (col::text);
    g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
    g.drawText ("GubeeLoops", logoArea.withX (logoArea.getRight() + 4).withWidth (120), juce::Justification::centredLeft);

    // time display  bar:beat:step
    double pos = ctx.engine->getBeat();
    int bar = (int) (pos / 4.0) + 1, beat = (int) std::fmod (pos, 4.0) + 1, step = (int) std::fmod (pos * 4.0, 4.0) + 1;
    g.setColour (col::bg);
    g.fillRoundedRectangle (timeArea.toFloat(), 4);
    g.setColour (col::text);
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 18.0f, juce::Font::bold));
    g.drawText (juce::String::formatted ("%3d:%d:%d", bar, beat, step), timeArea, juce::Justification::centred);

    g.setFont (juce::FontOptions (11.0f));
    g.setColour (col::textDim);
    g.drawText ("CPU " + juce::String (juce::roundToInt (ctx.engine->getCpu() * 100)) + "%", cpuArea, juce::Justification::centredLeft);

    auto mr = meterArea.toFloat();
    g.setColour (col::bg); g.fillRect (mr);
    auto drawBar = [&] (juce::Rectangle<float> r, float v)
    {
        float w = juce::jlimit (0.0f, 1.0f, v) * r.getWidth();
        g.setColour (v > 0.99f ? juce::Colour (0xffe05050) : col::accent);
        g.fillRect (r.withWidth (w));
    };
    drawBar (mr.removeFromTop (mr.getHeight() / 2).reduced (1), meterL);
    drawBar (mr.reduced (1), meterR);

    if (statusText.isNotEmpty())
    {
        g.setColour (col::accent2);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (statusText, statusArea, juce::Justification::centredRight, true);
    }
    g.setColour (col::textDim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("SWING", swing.getBounds().translated (0, 15), juce::Justification::centredTop);
}

void TopBar::resized()
{
    auto r = getLocalBounds();
    auto row1 = r.removeFromTop (44).reduced (6, 6);
    auto row2 = r.reduced (6, 3);

    logoArea = row1.removeFromLeft (34);
    row1.removeFromLeft (126);
    for (auto* b : { &fileBtn, &editBtn, &toolsBtn }) { b->setBounds (row1.removeFromLeft (54)); row1.removeFromLeft (3); }
    row1.removeFromLeft (14);
    playBtn.setBounds (row1.removeFromLeft (38)); row1.removeFromLeft (3);
    stopBtn.setBounds (row1.removeFromLeft (38)); row1.removeFromLeft (3);
    recBtn.setBounds (row1.removeFromLeft (38)); row1.removeFromLeft (10);
    patModeBtn.setBounds (row1.removeFromLeft (46));
    songModeBtn.setBounds (row1.removeFromLeft (50));
    jamModeBtn.setBounds (row1.removeFromLeft (44)); row1.removeFromLeft (10);
    tempo.setBounds (row1.removeFromLeft (104)); row1.removeFromLeft (6);
    timeArea = row1.removeFromLeft (100); row1.removeFromLeft (6);
    swing.setBounds (row1.removeFromLeft (30).withTrimmedBottom (8)); row1.removeFromLeft (6);
    metroBtn.setBounds (row1.removeFromLeft (56)); row1.removeFromLeft (10);
    patternBox.setBounds (row1.removeFromLeft (150)); row1.removeFromLeft (2);
    patMenuBtn.setBounds (row1.removeFromLeft (28)); row1.removeFromLeft (10);
    meterArea = row1.removeFromRight (90).reduced (0, 6);
    row1.removeFromRight (6);
    cpuArea = row1.removeFromRight (56);
    statusArea = row1;

    int w = juce::jmin (130, row2.getWidth() / juce::jmax (1, viewBtns.size()));
    for (auto* b : viewBtns) { b->setBounds (row2.removeFromLeft (w).reduced (1, 0)); }
}
}
