#include "Windows.h"
#include "BrowserPanel.h"
#include "Targets.h"

namespace gubee
{
ToolWindow::ToolWindow (const juce::String& title, juce::Component* content, bool resizable, std::function<void()> onClose)
    : juce::DocumentWindow (title, col::panel, juce::DocumentWindow::closeButton), closeFn (std::move (onClose))
{
    setUsingNativeTitleBar (true);
    setContentOwned (content, true);
    setResizable (resizable, false);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

//==============================================================================
// Drum Pads editor (MPC / Drum Rack style)
//==============================================================================
namespace
{
const char padKeys[] = { 'z', 'x', 'c', 'v', 'a', 's', 'd', 'f', 'q', 'w', 'e', 'r', '1', '2', '3', '4' };

struct PadsEditor : public juce::Component, public juce::DragAndDropTarget, public juce::FileDragAndDropTarget,
                    private juce::ValueTree::Listener, private juce::Timer
{
    PadsEditor (AppContext& c, int id) : ctx (c), channelId (id)
    {
        auto setupSlider = [this] (juce::Slider& s, double lo, double hi, double step, const juce::Identifier& prop, double def)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
            s.setRange (lo, hi, step);
            s.setDoubleClickReturnValue (true, def);
            s.onDragStart = [this] { ctx.project.beginEdit ("Pad"); };
            s.onValueChange = [this, &s, prop] { if (! updating) { auto p = pad(); if (p.isValid()) p.setProperty (prop, s.getValue(), ctx.project.um()); } };
            addAndMakeVisible (s);
        };
        setupSlider (gain, 0, 2, 0.01, ids::gain, 1.0);
        setupSlider (pan, -1, 1, 0.01, ids::pan, 0.0);
        setupSlider (pitch, -24, 24, 1, ids::pitch, 0.0);
        addAndMakeVisible (reverse);
        reverse.setButtonText ("Reverse");
        reverse.onClick = [this] { auto p = pad(); if (p.isValid() && ! updating) { ctx.project.beginEdit ("Pad"); p.setProperty (ids::reverse, reverse.getToggleState(), ctx.project.um()); } };
        addAndMakeVisible (choke);
        choke.addItem ("No choke group", 1);
        for (int i = 1; i <= 8; ++i) choke.addItem ("Choke group " + juce::String (i), i + 1);
        choke.setTooltip ("Pads in the same group cut each other off (e.g. open + closed hi-hat)");
        choke.onChange = [this] { auto p = pad(); if (p.isValid() && ! updating) { ctx.project.beginEdit ("Pad"); p.setProperty (ids::choke, choke.getSelectedId() - 1, ctx.project.um()); } };
        addAndMakeVisible (output);
        output.addItem ("Mixer: same as channel", 1);
        for (int i = 0; i < numMixerTracks; ++i) output.addItem ("Mixer: " + juce::String (i) + " " + ctx.project.mixerTrack (i).getProperty (ids::name).toString(), i + 2);
        output.setTooltip ("Send this pad to its own mixer track so it can have its own effects");
        output.onChange = [this] { auto p = pad(); if (p.isValid() && ! updating) { ctx.project.beginEdit ("Pad"); p.setProperty (ids::output, output.getSelectedId() - 2, ctx.project.um()); } };
        for (auto* b : { &loadBtn, &chopBtn, &clearBtn, &autoRouteBtn }) addAndMakeVisible (*b);
        loadBtn.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Load sample onto pad", paths::packs(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult().existsAsFile()) { ctx.project.beginEdit ("Load pad"); ctx.project.setPadSample (channelId, selected, fc.getResult()); }
            });
        };
        chopBtn.onClick = [this] { auto p = pad(); if (p.isValid() && ctx.openInChopShop) ctx.openInChopShop (juce::File (p.getProperty (ids::sample).toString()), -1, {}); };
        clearBtn.onClick = [this] { auto p = pad(); if (p.isValid()) { ctx.project.beginEdit ("Clear pad"); p.getParent().removeChild (p, ctx.project.um()); } };
        autoRouteBtn.setTooltip ("Give every loaded pad its own mixer track, named after the sound");
        autoRouteBtn.onClick = [this] { autoRoute(); };

        ctx.project.root.addListener (this);
        setWantsKeyboardFocus (true);
        setSize (760, 470);
        refresh();
        startTimerHz (30);
    }
    ~PadsEditor() override { ctx.project.root.removeListener (this); releaseAll(); }

    juce::ValueTree pad() const { return ctx.project.getPad (channelId, selected); }

    void autoRoute()
    {
        auto& pr = ctx.project;
        pr.beginEdit ("Route pads");
        std::set<int> used;
        for (auto c : pr.channels()) { used.insert ((int) c.getProperty (ids::mixer)); for (auto p : c) if (p.hasType (ids::PAD)) used.insert ((int) p.getProperty (ids::output, -1)); }
        int next = 1;
        for (int i = 0; i < DrumPadsProcessor::numPads; ++i)
        {
            auto p = pr.getPad (channelId, i);
            if (! p.isValid() || (int) p.getProperty (ids::output, -1) > 0) continue;
            while (next < numMixerTracks && used.count (next)) ++next;
            if (next >= numMixerTracks) break;
            p.setProperty (ids::output, next, pr.um());
            pr.mixerTrack (next).setProperty (ids::name, p.getProperty (ids::name), pr.um());
            used.insert (next);
        }
        ctx.status ("Each pad now has its own mixer track");
    }

    void refresh()
    {
        const juce::ScopedValueSetter<bool> s (updating, true);
        auto p = pad();
        for (auto* comp : std::initializer_list<juce::Component*> { &gain, &pan, &pitch, &reverse, &choke, &output, &chopBtn, &clearBtn })
            comp->setEnabled (p.isValid());
        gain.setValue ((double) p.getProperty (ids::gain, 1.0), juce::dontSendNotification);
        pan.setValue ((double) p.getProperty (ids::pan, 0.0), juce::dontSendNotification);
        pitch.setValue ((double) p.getProperty (ids::pitch, 0.0), juce::dontSendNotification);
        reverse.setToggleState ((bool) p.getProperty (ids::reverse, false), juce::dontSendNotification);
        choke.setSelectedId ((int) p.getProperty (ids::choke, 0) + 1, juce::dontSendNotification);
        output.setSelectedId ((int) p.getProperty (ids::output, -1) + 2, juce::dontSendNotification);
        repaint();
    }

    juce::Rectangle<int> padRect (int i) const
    {
        auto area = padArea();
        const int w = area.getWidth() / 4, h = area.getHeight() / 4;
        const int row = 3 - i / 4, colm = i % 4;
        return { area.getX() + colm * w, area.getY() + row * h, w, h };
    }
    juce::Rectangle<int> padArea() const { return getLocalBounds().removeFromLeft (440).reduced (12).withTrimmedTop (30); }

    int padAt (juce::Point<int> p) const { for (int i = 0; i < 16; ++i) if (padRect (i).contains (p)) return i; return -1; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg);
        auto ch = ctx.project.findChannel (channelId);
        g.setColour (col::text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (ch.getProperty (ids::name).toString() + "  -  Drum Pads", 12, 6, 420, 22, juce::Justification::centredLeft);
        auto rt = ctx.engine->channelRuntime (channelId);
        for (int i = 0; i < 16; ++i)
        {
            auto r = padRect (i).reduced (4).toFloat();
            auto p = ctx.project.getPad (channelId, i);
            float flash = flashes[(size_t) i];
            auto base = p.isValid() ? col::fromIndex (i) : col::panel;
            g.setColour (base.withAlpha (p.isValid() ? 0.45f + flash * 0.5f : 0.6f));
            g.fillRoundedRectangle (r, 6);
            g.setColour (i == selected ? juce::Colours::white : col::outline);
            g.drawRoundedRectangle (r, 6, i == selected ? 2.0f : 1.0f);
            g.setColour (p.isValid() ? col::text : col::textDim);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawFittedText (p.isValid() ? p.getProperty (ids::name).toString() : juce::String ("empty"), r.reduced (6).toNearestInt(), juce::Justification::centred, 2);
            g.setFont (juce::FontOptions (10.0f));
            g.setColour (col::textDim);
            g.drawText (juce::String (i + 1) + "   [" + juce::String::charToString ((juce::juce_wchar) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) padKeys[i])) + "]",
                        r.reduced (6, 4), juce::Justification::topLeft);
            if (p.isValid() && (int) p.getProperty (ids::choke, 0) > 0)
                g.drawText ("choke " + p.getProperty (ids::choke).toString(), r.reduced (6, 4), juce::Justification::bottomLeft);
        }
        auto side = getLocalBounds().withTrimmedLeft (440).reduced (12);
        g.setColour (col::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        auto p = pad();
        g.drawText ("Pad " + juce::String (selected + 1) + (p.isValid() ? ": " + p.getProperty (ids::name).toString() : juce::String (" (empty - drop a sound)")),
                    side.removeFromTop (24), juce::Justification::centredLeft, true);
        g.setColour (col::textDim);
        g.setFont (juce::FontOptions (12.0f));
        for (auto* s : { &gain, &pan, &pitch })
            g.drawText (s == &gain ? "Volume" : s == &pan ? "Pan" : "Pitch (st)", s->getBounds().withX (side.getX()).withWidth (70), juce::Justification::centredLeft);
        g.drawFittedText ("Click or press the keys shown to play. Drag sounds from the browser (or Explorer) onto a pad. "
                          "Turn on Note Repeat in the Channel Rack for rolls.", side.removeFromBottom (54), juce::Justification::bottomLeft, 3);
        juce::ignoreUnused (rt);
    }

    void resized() override
    {
        auto side = getLocalBounds().withTrimmedLeft (440).reduced (12);
        side.removeFromTop (34);
        for (auto* s : { &gain, &pan, &pitch }) { s->setBounds (side.removeFromTop (28).withTrimmedLeft (74)); side.removeFromTop (6); }
        reverse.setBounds (side.removeFromTop (26)); side.removeFromTop (6);
        choke.setBounds (side.removeFromTop (26)); side.removeFromTop (6);
        output.setBounds (side.removeFromTop (26)); side.removeFromTop (12);
        auto row = side.removeFromTop (28);
        loadBtn.setBounds (row.removeFromLeft (row.getWidth() / 3).reduced (2, 0));
        chopBtn.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2, 0));
        clearBtn.setBounds (row.reduced (2, 0));
        side.removeFromTop (8);
        autoRouteBtn.setBounds (side.removeFromTop (28));
    }

    void trigger (int i, float vel)
    {
        if (ctx.selectedChannel() != channelId) ctx.selectChannel (channelId);
        ctx.engine->sendLiveNote (DrumPadsProcessor::baseNote + i, vel);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        int i = padAt (e.getPosition());
        if (i < 0) return;
        selected = i;
        refresh();
        auto r = padRect (i);
        float vel = juce::jlimit (0.2f, 1.0f, 1.0f - (e.y - r.getY()) / (float) r.getHeight() * 0.7f);
        mouseNote = i;
        trigger (i, vel);
    }
    void mouseUp (const juce::MouseEvent&) override { if (mouseNote >= 0) ctx.engine->sendLiveNote (DrumPadsProcessor::baseNote + mouseNote, 0.0f); mouseNote = -1; }

    bool keyPressed (const juce::KeyPress& k) override
    {
        for (int i = 0; i < 16; ++i) if (k.getKeyCode() == padKeys[i] || k.getKeyCode() == juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) padKeys[i])) return true;
        return false;
    }

    bool keyStateChanged (bool) override
    {
        bool used = false;
        for (int i = 0; i < 16; ++i)
        {
            const bool down = juce::KeyPress::isKeyCurrentlyDown (padKeys[i]) || juce::KeyPress::isKeyCurrentlyDown (juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) padKeys[i]));
            if (down != keysDown[(size_t) i])
            {
                keysDown[(size_t) i] = down;
                if (down) { selected = i; trigger (i, 0.85f); refresh(); }
                else ctx.engine->sendLiveNote (DrumPadsProcessor::baseNote + i, 0.0f);
                used = true;
            }
        }
        return used;
    }

    void releaseAll()
    {
        for (int i = 0; i < 16; ++i) if (keysDown[(size_t) i]) ctx.engine->sendLiveNote (DrumPadsProcessor::baseNote + i, 0.0f);
    }

    void timerCallback() override
    {
        bool dirty = false;
        if (auto rt = ctx.engine->channelRuntime (channelId))
            if (rt->pads != nullptr)
                for (int i = 0; i < 16; ++i)
                {
                    int h = rt->pads->hits[(size_t) i].load();
                    if (h != lastHits[(size_t) i]) { lastHits[(size_t) i] = h; flashes[(size_t) i] = 1.0f; dirty = true; }
                }
        for (auto& f : flashes) if (f > 0) { f = juce::jmax (0.0f, f - 0.12f); dirty = true; }
        if (dirty) repaint (padArea());
    }

    bool isInterestedInDragSource (const SourceDetails&) override { return true; }
    void itemDropped (const SourceDetails& d) override
    {
        auto f = fileFromDrag (d.description);
        int i = padAt (d.localPosition);
        if (f.existsAsFile() && i >= 0) { ctx.project.beginEdit ("Drop on pad"); ctx.project.setPadSample (channelId, i, f); selected = i; refresh(); }
    }
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        int i = padAt ({ x, y });
        if (i < 0) i = 0;
        ctx.project.beginEdit ("Drop on pads");
        for (auto& path : files)
        {
            juce::File f (path);
            if (! Library::isAudioFile (f) || i >= 16) continue;
            ctx.project.setPadSample (channelId, i++, f);
        }
        refresh();
    }

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { refresh(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& c) override { if (c.hasType (ids::PAD)) refresh(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& c, int) override { if (c.hasType (ids::PAD)) refresh(); }

    AppContext& ctx;
    int channelId, selected = 0, mouseNote = -1;
    bool updating = false;
    std::array<bool, 16> keysDown {};
    std::array<int, 16> lastHits {};
    std::array<float, 16> flashes {};
    juce::Slider gain, pan, pitch;
    juce::ToggleButton reverse;
    juce::ComboBox choke, output;
    juce::TextButton loadBtn { "Load..." }, chopBtn { "Chop Shop" }, clearBtn { "Clear" }, autoRouteBtn { "Give each pad its own mixer track" };
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
struct PluginManagerComp : public juce::Component, private juce::ChangeListener
{
    explicit PluginManagerComp (AppContext& c) : ctx (c),
        list (c.plugins.formats, c.plugins.known, c.plugins.deadMansPedal, c.props.get(), true)
    {
        list.setNumberOfThreadsForScanning (1);
        list.setScanDialogText ("Scanning plugins", "GubeeLoops is checking each plugin in a separate process, so a broken plugin can't crash the app.");
        addAndMakeVisible (list);
        addAndMakeVisible (info);
        info.setJustificationType (juce::Justification::centredLeft);
        info.setText ("Click 'Options...' below and choose 'Scan for new or updated VST3 plug-ins'. Default folders: "
                      "C:\\Program Files\\Common Files\\VST3 and " + paths::plugins().getFullPathName() + " (put downloaded plugins and the Gubee pack there).",
                      juce::dontSendNotification);
        addAndMakeVisible (openFolder);
        openFolder.onClick = [] { paths::plugins().startAsProcess(); };
        ctx.plugins.known.addChangeListener (this);
        setSize (760, 560);
    }
    ~PluginManagerComp() override { ctx.plugins.known.removeChangeListener (this); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { ctx.plugins.save(); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto top = r.removeFromTop (48);
        openFolder.setBounds (top.removeFromRight (180).reduced (0, 8));
        info.setBounds (top);
        list.setBounds (r);
    }
    AppContext& ctx;
    juce::PluginListComponent list;
    juce::Label info;
    juce::TextButton openFolder { "Open Plugins folder" };
};

//==============================================================================
struct ExportPanel : public juce::Component
{
    ExportPanel (AppContext& c, std::function<void()> done) : ctx (c), onDone (std::move (done))
    {
        addAndMakeVisible (format);
        format.addItemList ({ "WAV (best quality)", "FLAC (smaller, lossless)", "OGG (small, lossy)" }, 1);
        format.setSelectedId (1);
        format.onChange = [this] { updateName(); };
        addAndMakeVisible (bits);
        bits.addItemList ({ "16-bit", "24-bit", "32-bit float" }, 1);
        bits.setSelectedId (2);
        addAndMakeVisible (stems);
        stems.setButtonText ("Also export stems (one file per mixer track)");
        addAndMakeVisible (patternOnly);
        patternOnly.setButtonText ("Export only the current pattern (loop)");
        patternOnly.setToggleState (ctx.project.songLengthBeats() <= 0, juce::dontSendNotification);
        addAndMakeVisible (fileLabel);
        addAndMakeVisible (choose);
        choose.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Export to", target, "*" + ext());
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this] (const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) { target = fc.getResult().withFileExtension (ext()); fileLabel.setText (target.getFullPathName(), juce::dontSendNotification); } });
        };
        addAndMakeVisible (go);
        go.onClick = [this] { run(); };
        auto name = ctx.project.file.existsAsFile() ? ctx.project.file.getFileNameWithoutExtension() : juce::String ("GubeeLoops song");
        target = paths::exports().getChildFile (name + ".wav");
        updateName();
        setSize (520, 230);
    }
    juce::String ext() const { return format.getSelectedId() == 2 ? ".flac" : format.getSelectedId() == 3 ? ".ogg" : ".wav"; }
    void updateName() { target = target.withFileExtension (ext()); fileLabel.setText (target.getFullPathName(), juce::dontSendNotification); }
    void paint (juce::Graphics& g) override { g.fillAll (col::panel); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        auto row = r.removeFromTop (26); format.setBounds (row.removeFromLeft (250)); row.removeFromLeft (8); bits.setBounds (row);
        r.removeFromTop (8);
        stems.setBounds (r.removeFromTop (24));
        patternOnly.setBounds (r.removeFromTop (24));
        r.removeFromTop (8);
        row = r.removeFromTop (26); choose.setBounds (row.removeFromRight (90)); fileLabel.setBounds (row);
        r.removeFromTop (12);
        go.setBounds (r.removeFromTop (34));
    }

    struct Job : public juce::ThreadWithProgressWindow
    {
        Job (AppContext& c, Engine::ExportOptions o) : juce::ThreadWithProgressWindow ("Exporting audio...", true, true), ctx (c), opts (std::move (o)) {}
        void run() override
        {
            ok = ctx.engine->exportAudio (opts, [this] (float p) { setProgress (p); return ! threadShouldExit(); }, error);
        }
        void threadComplete (bool cancelled) override
        {
            if (ok && ! cancelled) { ctx.status ("Exported " + opts.file.getFileName()); opts.file.revealToUser(); }
            else ctx.status ("Export failed: " + error);
            juce::MessageManager::callAsync ([this] { delete this; });
        }
        AppContext& ctx;
        Engine::ExportOptions opts;
        bool ok = false;
        juce::String error;
    };

    void run()
    {
        Engine::ExportOptions o;
        o.file = target;
        o.format = format.getSelectedId() - 1;
        o.bits = bits.getSelectedId() == 1 ? 16 : bits.getSelectedId() == 2 ? 24 : 32;
        o.stems = stems.getToggleState();
        o.patternOnly = patternOnly.getToggleState();
        for (int i = 0; i < numMixerTracks; ++i) o.trackNames.add (ctx.project.mixerTrack (i).getProperty (ids::name).toString());
        target.getParentDirectory().createDirectory();
        ctx.engine->stop();
        ctx.engine->storeStatesToTree();
        ctx.engine->syncNow();
        (new Job (ctx, o))->launchThread();
        if (onDone) onDone();
    }

    AppContext& ctx;
    std::function<void()> onDone;
    juce::ComboBox format, bits;
    juce::ToggleButton stems, patternOnly;
    juce::Label fileLabel;
    juce::TextButton choose { "Change..." }, go { "Export" };
    juce::File target;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
struct ModulatorsPanel : public juce::Component, private juce::ValueTree::Listener, private juce::AsyncUpdater
{
    struct Row : public juce::Component
    {
        Row (ModulatorsPanel& p, juce::ValueTree m) : panel (p), node (m),
            depth (p.ctx.project.um(), 0, 1, 0.3, "Depth"), base (p.ctx.project.um(), 0, 1, 0.5, "Centre value")
        {
            auto& pr = p.ctx.project;
            addAndMakeVisible (enabled);
            enabled.setToggleState ((bool) m.getProperty (ids::enabled, true), juce::dontSendNotification);
            enabled.onClick = [this, &pr] { pr.beginEdit ("Mod"); node.setProperty (ids::enabled, enabled.getToggleState(), pr.um()); };
            addAndMakeVisible (target);
            target.setButtonText (m.getProperty (ids::name).toString());
            target.onClick = [this]
            {
                juce::PopupMenu menu;
                auto list = std::make_shared<std::vector<std::pair<juce::String, juce::String>>>();
                buildTargetMenu (panel.ctx, menu, *list, 1);
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [this, list] (int r)
                {
                    if (r <= 0 || r - 1 >= (int) list->size()) return;
                    auto& pr2 = panel.ctx.project;
                    pr2.beginEdit ("Mod target");
                    node.setProperty (ids::target, (*list)[(size_t) r - 1].first, pr2.um());
                    node.setProperty (ids::name, (*list)[(size_t) r - 1].second, pr2.um());
                });
            };
            addAndMakeVisible (shape);
            shape.addItemList ({ "Sine", "Triangle", "Saw", "Square", "Random" }, 1);
            shape.setSelectedId ((int) m.getProperty (ids::shape, 0) + 1, juce::dontSendNotification);
            shape.onChange = [this, &pr] { pr.beginEdit ("Mod"); node.setProperty (ids::shape, shape.getSelectedId() - 1, pr.um()); };
            addAndMakeVisible (rate);
            const double rates[] = { 0.25, 0.5, 1, 2, 4, 8, 16, 32 };
            const char* names[] = { "1/16 note", "1/8 note", "1 beat", "2 beats", "1 bar", "2 bars", "4 bars", "8 bars" };
            for (int i = 0; i < 8; ++i) rate.addItem (juce::String ("Cycle: ") + names[i], i + 1);
            double cur = m.getProperty (ids::rate, 1.0);
            for (int i = 0; i < 8; ++i) if (std::abs (rates[i] - cur) < 1.0e-6) rate.setSelectedId (i + 1, juce::dontSendNotification);
            rate.onChange = [this, &pr]
            {
                const double rs[] = { 0.25, 0.5, 1, 2, 4, 8, 16, 32 };
                pr.beginEdit ("Mod"); node.setProperty (ids::rate, rs[rate.getSelectedId() - 1], pr.um());
            };
            depth.bind (m, ids::depth); base.bind (m, ids::base);
            addAndMakeVisible (depth); addAndMakeVisible (base);
            addAndMakeVisible (del);
            del.onClick = [this, &pr] { pr.beginEdit ("Delete mod"); node.getParent().removeChild (node, pr.um()); };
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (col::panelHi); g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2), 4);
            g.setColour (col::textDim); g.setFont (juce::FontOptions (10.0f));
            g.drawText ("DEPTH", depth.getBounds().translated (0, 14), juce::Justification::centredBottom);
            g.drawText ("CENTRE", base.getBounds().translated (0, 14), juce::Justification::centredBottom);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (8, 6);
            enabled.setBounds (r.removeFromLeft (26));
            target.setBounds (r.removeFromLeft (230).reduced (0, 6)); r.removeFromLeft (6);
            shape.setBounds (r.removeFromLeft (100).reduced (0, 8)); r.removeFromLeft (6);
            rate.setBounds (r.removeFromLeft (130).reduced (0, 8)); r.removeFromLeft (6);
            depth.setBounds (r.removeFromLeft (40).withTrimmedBottom (12));
            base.setBounds (r.removeFromLeft (40).withTrimmedBottom (12));
            del.setBounds (r.removeFromRight (30).reduced (0, 8));
        }
        ModulatorsPanel& panel;
        juce::ValueTree node;
        juce::ToggleButton enabled;
        juce::TextButton target, del { "X" };
        juce::ComboBox shape, rate;
        TreeKnob depth, base;
    };

    explicit ModulatorsPanel (AppContext& c) : ctx (c)
    {
        addAndMakeVisible (add);
        add.onClick = [this]
        {
            juce::PopupMenu menu;
            auto list = std::make_shared<std::vector<std::pair<juce::String, juce::String>>>();
            buildTargetMenu (ctx, menu, *list, 1);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&add), [this, list] (int r)
            {
                if (r <= 0 || r - 1 >= (int) list->size()) return;
                ctx.project.beginEdit ("Add modulator");
                ctx.project.addModulator ((*list)[(size_t) r - 1].first, (*list)[(size_t) r - 1].second);
            });
        };
        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&content, false);
        ctx.project.root.addListener (this);
        rebuild();
        setSize (680, 420);
    }
    ~ModulatorsPanel() override { ctx.project.root.removeListener (this); }
    void rebuild()
    {
        rows.clear();
        for (auto m : ctx.project.mods()) content.addAndMakeVisible (rows.add (new Row (*this, m)));
        resized();
    }
    void handleAsyncUpdate() override { rebuild(); }
    void valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree&) override { if (p.hasType (ids::MODS)) triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree&, int) override { if (p.hasType (ids::MODS)) triggerAsyncUpdate(); }
    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& i) override { if (t.hasType (ids::MOD) && i == ids::name) triggerAsyncUpdate(); }
    void valueTreeRedirected (juce::ValueTree&) override { triggerAsyncUpdate(); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::panel);
        g.setColour (col::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText ("Modulators are tempo-synced LFOs that move any knob while the music plays - wobble a filter, pulse a volume, sweep a pan. "
                          "Pick a target, then set the shape, speed, depth and centre.", getLocalBounds().reduced (12).removeFromTop (36), juce::Justification::topLeft, 2);
        if (rows.isEmpty())
            g.drawText ("No modulators yet - click '+ Add modulator'", getLocalBounds(), juce::Justification::centred);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        r.removeFromTop (40);
        add.setBounds (r.removeFromTop (30).removeFromLeft (180));
        r.removeFromTop (6);
        viewport.setBounds (r);
        content.setSize (r.getWidth() - 12, rows.size() * 64);
        for (int i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, i * 64, content.getWidth(), 62);
    }
    AppContext& ctx;
    juce::TextButton add { "+ Add modulator" };
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<Row> rows;
};
}

std::unique_ptr<juce::Component> makeDrumPadsEditor (AppContext& ctx, int id) { return std::make_unique<PadsEditor> (ctx, id); }
std::unique_ptr<juce::Component> makePluginManager (AppContext& ctx) { return std::make_unique<PluginManagerComp> (ctx); }
std::unique_ptr<juce::Component> makeExportPanel (AppContext& ctx, std::function<void()> done) { return std::make_unique<ExportPanel> (ctx, std::move (done)); }
std::unique_ptr<juce::Component> makeModulatorsPanel (AppContext& ctx) { return std::make_unique<ModulatorsPanel> (ctx); }

std::unique_ptr<juce::Component> makeAudioSettings (AppContext& ctx)
{
    auto c = std::make_unique<juce::AudioDeviceSelectorComponent> (ctx.devices, 0, 2, 0, 2, true, false, true, false);
    c->setSize (600, 520);
    return c;
}

//==============================================================================
bool OutOfProcessScanner::findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                                              const juce::String& fileOrIdentifier)
{
    auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    auto tmp = juce::File::createTempFile (".xml");
    juce::ChildProcess cp;
    if (! cp.start (juce::StringArray { exe.getFullPathName(), "--gubee-scan", format.getName(), fileOrIdentifier, tmp.getFullPathName() }, 0))
    {
        format.findAllTypesForFile (result, fileOrIdentifier);
        return true;
    }
    const auto start = juce::Time::getMillisecondCounter();
    while (cp.isRunning())
    {
        if (shouldExit()) { cp.kill(); return true; }
        if (juce::Time::getMillisecondCounter() - start > 120000) { cp.kill(); tmp.deleteFile(); return false; }
        juce::Thread::sleep (20);
    }
    auto xml = juce::XmlDocument::parse (tmp);
    tmp.deleteFile();
    if (xml == nullptr) return false;   // the plugin crashed the scanner -> GubeeLoops marks it as failed
    for (auto* e : xml->getChildIterator())
    {
        juce::PluginDescription d;
        if (d.loadFromXml (*e)) result.add (new juce::PluginDescription (d));
    }
    return true;
}

int runPluginScanChild (const juce::StringArray& args)
{
    int i = args.indexOf ("--gubee-scan");
    if (i < 0 || args.size() < i + 4) return 2;
    juce::AudioPluginFormatManager fm;
    fm.addDefaultFormats();
    juce::XmlElement out ("PLUGINS");
    for (auto* f : fm.getFormats())
    {
        if (f->getName() != args[i + 1]) continue;
        juce::OwnedArray<juce::PluginDescription> found;
        f->findAllTypesForFile (found, args[i + 2]);
        for (auto* d : found) out.addChildElement (d->createXml().release());
    }
    return out.writeTo (juce::File (args[i + 3])) ? 0 : 1;
}
}
