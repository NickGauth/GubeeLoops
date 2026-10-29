#include "MainComponent.h"

namespace gubee
{
static const std::vector<std::pair<int, int>>& typingMap()
{
    // key code -> semitone offset from the current octave's C
    static const std::vector<std::pair<int, int>> m = {
        { 'z', 0 }, { 's', 1 }, { 'x', 2 }, { 'd', 3 }, { 'c', 4 }, { 'v', 5 }, { 'g', 6 }, { 'b', 7 }, { 'h', 8 }, { 'n', 9 }, { 'j', 10 }, { 'm', 11 }, { ',', 12 },
        { 'q', 12 }, { '2', 13 }, { 'w', 14 }, { '3', 15 }, { 'e', 16 }, { 'r', 17 }, { '5', 18 }, { 't', 19 }, { '6', 20 }, { 'y', 21 }, { '7', 22 }, { 'u', 23 }, { 'i', 24 } };
    return m;
}

MainComponent::MainComponent (AppContext& c) : ctx (c), topBar (c), browser (c)
{
    addAndMakeVisible (topBar);
    addAndMakeVisible (browser);
    views[viewPlaylist] = std::make_unique<Playlist> (ctx);
    views[viewRack] = std::make_unique<ChannelRack> (ctx);
    views[viewPiano] = std::make_unique<PianoRoll> (ctx);
    views[viewMixer] = std::make_unique<MixerView> (ctx);
    views[viewJam] = std::make_unique<JamView> (ctx);
    views[viewChop] = std::make_unique<ChopShop> (ctx);
    for (auto& v : views) addChildComponent (*v);

    topBar.onMenu = [this] (const juce::String& n, juce::Component* b) { menu (n, b); };
    topBar.onShowView = [this] (int v) { showView (v); };
    ctx.setStatus = [this] (const juce::String& s) { topBar.setStatus (s); };
    ctx.showView = [this] (int v) { showView (v); };
    ctx.openChannelEditor = [this] (int id) { openChannelEditor (id); };
    ctx.openInChopShop = [this] (const juce::File& f, int ch, juce::ValueTree clip)
    {
        static_cast<ChopShop*> (views[viewChop].get())->load (f, ch, clip);
        showView (viewChop);
    };
    ctx.openSlotEditor = [this] (int track, int slot)
    {
        ctx.engine->syncNow();
        if (auto* p = ctx.engine->slotProcessor (track, slot))
            openProcessorWindow (p, "slot:" + juce::String (track) + ":" + juce::String (slot),
                                 p->getName() + "  -  " + ctx.project.mixerTrack (track).getProperty (ids::name).toString());
        else
            ctx.status ("That effect could not be loaded");
    };
    ctx.openProcessorWindow = [this] (juce::AudioProcessor* p, const juce::String& key) { openProcessorWindow (p, key, p->getName()); };
    ctx.openPluginManager = [this] { openWindow ("plugins", "Plugin Manager", makePluginManager (ctx), true); };
    ctx.openMidiTools = [this] (int id) { openWindow ("tools:" + juce::String (id), "MIDI tools", makeMidiToolsPanel (ctx, id), false); };
    ctx.engine->onStatus = [this] (const juce::String& s) { juce::MessageManager::callAsync ([this, s] { topBar.setStatus (s); }); };

    typingPiano = ctx.props->getBoolValue ("typingPiano", true);
    ctx.engine->countIn = ctx.props->getBoolValue ("countIn", true);
    ctx.project.root.addListener (this);
    setWantsKeyboardFocus (true);
    showView (viewRack);
    setSize (1500, 900);
    startTimerHz (1);
    ctx.status ("Welcome to GubeeLoops! Press Space to hear the starter beat.");
}

MainComponent::~MainComponent()
{
    ctx.project.root.removeListener (this);
    windows.clear();
    ctx.setStatus = nullptr;
    ctx.showView = nullptr;
    ctx.openChannelEditor = nullptr;
    ctx.openInChopShop = nullptr;
    ctx.openSlotEditor = nullptr;
    ctx.openProcessorWindow = nullptr;
    ctx.openPluginManager = nullptr;
    ctx.openMidiTools = nullptr;
    ctx.engine->onStatus = nullptr;
}

void MainComponent::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void MainComponent::resized()
{
    auto r = getLocalBounds();
    topBar.setBounds (r.removeFromTop (78));
    browser.setVisible (browserVisible);
    if (browserVisible) browser.setBounds (r.removeFromLeft (300));
    for (auto& v : views) v->setBounds (r);
}

void MainComponent::showView (int v)
{
    currentView = juce::jlimit (0, numViews - 1, v);
    for (int i = 0; i < numViews; ++i) views[i]->setVisible (i == currentView);
    topBar.setActiveView (currentView);
    views[currentView]->grabKeyboardFocus();
}

void MainComponent::updateTitle()
{
    auto name = ctx.project.file.existsAsFile() ? ctx.project.file.getFileNameWithoutExtension() : juce::String ("Untitled");
    if (onTitleChange) onTitleChange ("GubeeLoops - " + name + (ctx.project.dirty ? " *" : ""));
}

void MainComponent::timerCallback()
{
    // autosave every two minutes when there are changes
    if (++lastAutosave >= 120 && ctx.project.dirty)
    {
        lastAutosave = 0;
        ctx.engine->storeStatesToTree();
        ctx.project.save (paths::projects().getChildFile ("autosave.gubee"));
    }
    // close plugin windows whose processor disappeared
    for (auto it = windows.begin(); it != windows.end();)
    {
        const auto& key = it->first;
        bool stale = false;
        if (key.startsWith ("ch:"))
        {
            auto id = key.fromFirstOccurrenceOf (":", false, false).upToFirstOccurrenceOf ("@", false, false).getIntValue();
            auto ptr = (juce::pointer_sized_int) key.fromLastOccurrenceOf ("@", false, false).getLargeIntValue();
            stale = (juce::pointer_sized_int) ctx.engine->channelProcessor (id) != ptr;
        }
        else if (key.startsWith ("slot:"))
        {
            auto parts = juce::StringArray::fromTokens (key.upToFirstOccurrenceOf ("@", false, false), ":", {});
            auto ptr = (juce::pointer_sized_int) key.fromLastOccurrenceOf ("@", false, false).getLargeIntValue();
            stale = (juce::pointer_sized_int) ctx.engine->slotProcessor (parts[1].getIntValue(), parts[2].getIntValue()) != ptr;
        }
        else if (key.startsWith ("pads:"))
            stale = ! ctx.project.findChannel (key.fromFirstOccurrenceOf (":", false, false).getIntValue()).isValid();
        it = stale ? windows.erase (it) : std::next (it);
    }
}

//==============================================================================
void MainComponent::openWindow (const juce::String& key, const juce::String& title, std::unique_ptr<juce::Component> comp, bool resizable)
{
    if (auto it = windows.find (key); it != windows.end()) { it->second->toFront (true); return; }
    windows[key] = std::make_unique<ToolWindow> (title, comp.release(), resizable, [this, key] { juce::MessageManager::callAsync ([this, key] { closeWindow (key); }); });
}

void MainComponent::closeWindow (const juce::String& key) { windows.erase (key); }

void MainComponent::openProcessorWindow (juce::AudioProcessor* p, const juce::String& baseKey, const juce::String& title)
{
    if (p == nullptr) return;
    const auto key = baseKey + "@" + juce::String ((juce::int64) (juce::pointer_sized_int) p);
    if (auto it = windows.find (key); it != windows.end()) { it->second->toFront (true); return; }
    juce::AudioProcessorEditor* editor = p->hasEditor() ? p->createEditorIfNeeded() : nullptr;
    if (editor == nullptr) editor = new juce::GenericAudioProcessorEditor (*p);
    windows[key] = std::make_unique<ToolWindow> (title, editor, editor->isResizable(), [this, key] { juce::MessageManager::callAsync ([this, key] { closeWindow (key); }); });
}

void MainComponent::openChannelEditor (int id)
{
    auto ch = ctx.project.findChannel (id);
    if (! ch.isValid()) return;
    ctx.engine->syncNow();
    if (ch.getProperty (ids::kind).toString() == "pads")
    {
        openWindow ("pads:" + juce::String (id), ch.getProperty (ids::name).toString() + " - Drum Pads", makeDrumPadsEditor (ctx, id), false);
        return;
    }
    if (auto* p = ctx.engine->channelProcessor (id))
        openProcessorWindow (p, "ch:" + juce::String (id), ch.getProperty (ids::name).toString());
    else
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't open instrument",
                                                ctx.engine->channelError (id).isNotEmpty() ? ctx.engine->channelError (id) : juce::String ("This instrument failed to load."));
}

//==============================================================================
void MainComponent::menu (const juce::String& name, juce::Component* button)
{
    juce::PopupMenu m;
    auto& pr = ctx.project;
    if (name == "FILE")
    {
        m.addItem (1, "New project");
        m.addItem (2, "Open project...");
        juce::PopupMenu recent;
        juce::StringArray recents = juce::StringArray::fromLines (ctx.props->getValue ("recent"));
        recents.removeEmptyStrings();
        for (int i = 0; i < recents.size(); ++i) recent.addItem (100 + i, juce::File (recents[i]).getFileNameWithoutExtension());
        auto autosave = paths::projects().getChildFile ("autosave.gubee");
        if (autosave.existsAsFile()) recent.addItem (99, "Autosave (" + autosave.getLastModificationTime().toString (true, true) + ")");
        m.addSubMenu ("Open recent", recent, recent.getNumItems() > 0);
        m.addSeparator();
        m.addItem (3, "Save  (Ctrl+S)");
        m.addItem (4, "Save as...");
        m.addSeparator();
        m.addItem (5, "Export audio (WAV / FLAC / OGG, stems)...  (Ctrl+R)");
        m.addItem (6, "Open GubeeLoops folder");
        m.addSeparator();
        m.addItem (8, "Quit");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (button), [this, recents] (int r)
        {
            if (r == 1) fileNew();
            else if (r == 2) fileOpen();
            else if (r == 3) fileSave (false);
            else if (r == 4) fileSave (true);
            else if (r == 5) openWindow ("export", "Export audio", makeExportPanel (ctx, [this] { juce::MessageManager::callAsync ([this] { closeWindow ("export"); }); }), false);
            else if (r == 6) paths::base().startAsProcess();
            else if (r == 8) juce::JUCEApplication::getInstance()->systemRequestedQuit();
            else if (r == 99 || r >= 100)
            {
                auto f = r == 99 ? paths::projects().getChildFile ("autosave.gubee") : juce::File (recents[r - 100]);
                tryQuit ([this, f] (bool ok) { if (ok && ! ctx.openProject (f)) ctx.status ("Couldn't open " + f.getFileName()); updateTitle(); });
            }
        });
    }
    else if (name == "EDIT")
    {
        m.addItem (1, "Undo " + pr.undo.getUndoDescription() + "  (Ctrl+Z)", pr.undo.canUndo());
        m.addItem (2, "Redo " + pr.undo.getRedoDescription() + "  (Ctrl+Y)", pr.undo.canRedo());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (button), [this] (int r)
        {
            if (r == 1) ctx.project.undo.undo();
            if (r == 2) ctx.project.undo.redo();
        });
    }
    else if (name == "TOOLS")
    {
        m.addItem (1, "Gubee Drummer (generate drums)...");
        m.addItem (2, "Modulators (LFOs on any knob)...");
        m.addItem (3, "Plugin manager (find VST3 plugins)...");
        m.addItem (4, "Audio & MIDI settings...");
        m.addItem (10, "MIDI learn (link controller knobs)...");
        m.addSeparator();
        m.addItem (5, "Computer keyboard plays notes (Z-M, Q-I rows; [ ] change octave)", true, typingPiano);
        m.addItem (6, "Show browser  (F8 = Jam, Ctrl+B = browser)", true, browserVisible);
        m.addItem (7, "Metronome", true, ctx.engine->metronome.load());
        m.addItem (8, "Quantize recorded notes", true, ctx.engine->quantizeRecording.load());
        m.addItem (11, "Count-in (1 bar) before recording", true, ctx.engine->countIn.load());
        m.addSeparator();
        m.addItem (9, "About GubeeLoops");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (button), [this] (int r)
        {
            if (r == 1) showDrummerDialog (ctx, this);
            else if (r == 2) openWindow ("mods", "Modulators", makeModulatorsPanel (ctx), true);
            else if (r == 3) ctx.openPluginManager();
            else if (r == 4) openWindow ("audio", "Audio & MIDI settings", makeAudioSettings (ctx), true);
            else if (r == 5) { typingPiano = ! typingPiano; ctx.props->setValue ("typingPiano", typingPiano); }
            else if (r == 6) { browserVisible = ! browserVisible; resized(); }
            else if (r == 7) ctx.engine->metronome = ! ctx.engine->metronome;
            else if (r == 8) ctx.engine->quantizeRecording = ! ctx.engine->quantizeRecording;
            else if (r == 10) openWindow ("midilearn", "MIDI learn", makeMidiLearnPanel (ctx), true);
            else if (r == 11) { ctx.engine->countIn = ! ctx.engine->countIn; ctx.props->setValue ("countIn", ctx.engine->countIn.load()); }
            else if (r == 9)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "GubeeLoops",
                    "GubeeLoops " + juce::JUCEApplication::getInstance()->getApplicationVersion() + "\n\n"
                    "A personal music studio: patterns, piano roll, playlist, Jam View, Chop Shop, drum pads, mixer and VST3 plugins.\n\n"
                    "Your files live in " + paths::base().getFullPathName() + "\n\nBuilt with JUCE.");
        });
    }
}

void MainComponent::fileNew()
{
    tryQuit ([this] (bool ok) { if (ok) { ctx.newProject(); updateTitle(); } });
}

void MainComponent::fileOpen()
{
    tryQuit ([this] (bool ok)
    {
        if (! ok) return;
        chooser = std::make_unique<juce::FileChooser> ("Open project", paths::projects(), "*.gubee");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            if (! ctx.openProject (f)) ctx.status ("Couldn't open " + f.getFileName());
            else
            {
                auto recents = juce::StringArray::fromLines (ctx.props->getValue ("recent"));
                recents.removeString (f.getFullPathName()); recents.insert (0, f.getFullPathName()); recents.removeEmptyStrings();
                while (recents.size() > 10) recents.remove (10);
                ctx.props->setValue ("recent", recents.joinIntoString ("\n"));
            }
            updateTitle();
        });
    });
}

void MainComponent::fileSave (bool saveAs, std::function<void()> after)
{
    auto doSave = [this, after] (juce::File f)
    {
        f = f.withFileExtension (".gubee");
        if (ctx.saveProject (f))
        {
            ctx.project.root.setProperty (ids::name, f.getFileNameWithoutExtension(), nullptr);
            ctx.project.dirty = false;
            ctx.status ("Saved " + f.getFileName());
            auto recents = juce::StringArray::fromLines (ctx.props->getValue ("recent"));
            recents.removeString (f.getFullPathName()); recents.insert (0, f.getFullPathName()); recents.removeEmptyStrings();
            while (recents.size() > 10) recents.remove (10);
            ctx.props->setValue ("recent", recents.joinIntoString ("\n"));
            updateTitle();
            if (after) after();
        }
        else ctx.status ("Save failed!");
    };
    if (! saveAs && ctx.project.file.existsAsFile()) { doSave (ctx.project.file); return; }
    chooser = std::make_unique<juce::FileChooser> ("Save project", paths::projects().getChildFile ("My Song.gubee"), "*.gubee");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [doSave] (const juce::FileChooser& fc)
    {
        if (fc.getResult() != juce::File()) doSave (fc.getResult());
    });
}

void MainComponent::tryQuit (std::function<void (bool)> done)
{
    if (! ctx.project.dirty) { done (true); return; }
    juce::AlertWindow::showYesNoCancelBox (juce::MessageBoxIconType::QuestionIcon, "Save changes?",
        "Do you want to save the changes to your song first?", "Save", "Don't save", "Cancel", this,
        juce::ModalCallbackFunction::create ([this, done] (int r)
        {
            if (r == 1) fileSave (false, [done] { done (true); });
            else if (r == 2) done (true);
            else done (false);
        }));
}

//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    auto& e = *ctx.engine;
    const auto mods = k.getModifiers();
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::spaceKey) { e.togglePlay(); return true; }
    if (code == juce::KeyPress::F5Key) { showView (viewPlaylist); return true; }
    if (code == juce::KeyPress::F6Key) { showView (viewRack); return true; }
    if (code == juce::KeyPress::F7Key) { showView (viewPiano); return true; }
    if (code == juce::KeyPress::F8Key) { showView (viewJam); return true; }
    if (code == juce::KeyPress::F9Key) { showView (viewMixer); return true; }
    if (code == juce::KeyPress::F10Key) { showView (viewChop); return true; }
    if (mods.isCtrlDown())
    {
        const auto c = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) code);
        if (c == 's') { fileSave (mods.isShiftDown()); return true; }
        if (c == 'o') { fileOpen(); return true; }
        if (c == 'n') { fileNew(); return true; }
        if (c == 'z') { mods.isShiftDown() ? ctx.project.undo.redo() : ctx.project.undo.undo(); return true; }
        if (c == 'y') { ctx.project.undo.redo(); return true; }
        if (c == 'r') { openWindow ("export", "Export audio", makeExportPanel (ctx, [this] { juce::MessageManager::callAsync ([this] { closeWindow ("export"); }); }), false); return true; }
        if (c == 'b') { browserVisible = ! browserVisible; resized(); return true; }
        if (c == 'l') { e.setMode (e.getMode() == TransportMode::pattern ? TransportMode::song : TransportMode::pattern); return true; }
        if (c == 'm') { e.metronome = ! e.metronome; return true; }
        if (c == 'e') { e.recordArmed = ! e.recordArmed; return true; }
    }
    if (code == juce::KeyPress::numberPadAdd || code == juce::KeyPress::numberPadSubtract)
    {
        auto pats = ctx.project.patterns();
        int idx = pats.indexOf (ctx.project.findPattern (ctx.currentPattern()));
        idx = juce::jlimit (0, pats.getNumChildren() - 1, idx + (code == juce::KeyPress::numberPadAdd ? 1 : -1));
        ctx.setCurrentPattern ((int) pats.getChild (idx).getProperty (ids::id));
        return true;
    }
    if (typingPiano && ! mods.isCtrlDown() && ! mods.isAltDown())
    {
        const auto c = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) k.getTextCharacter());
        if (c == '[') { typingOctave = juce::jmax (0, typingOctave - 1); ctx.status ("Keyboard octave " + juce::String (typingOctave)); return true; }
        if (c == ']') { typingOctave = juce::jmin (8, typingOctave + 1); ctx.status ("Keyboard octave " + juce::String (typingOctave)); return true; }
        for (auto& [key, semi] : typingMap()) if (c == key) return true;
    }
    return false;
}

bool MainComponent::keyStateChanged (bool)
{
    if (! typingPiano) return false;
    if (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr) return false;
    if (juce::ModifierKeys::getCurrentModifiers().isCtrlDown()) return false;
    bool any = false;
    for (auto& [key, semi] : typingMap())
    {
        const bool down = juce::KeyPress::isKeyCurrentlyDown (key);
        const int note = juce::jlimit (0, 127, (typingOctave + 1) * 12 + semi - 12);
        bool& was = typingDown[key];
        if (down != was)
        {
            was = down;
            ctx.engine->sendLiveNote (note, down ? 0.8f : 0.0f);
            any = true;
        }
    }
    return any;
}
}
