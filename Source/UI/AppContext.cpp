#include "AppContext.h"

namespace gubee
{
AppContext::AppContext()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "GubeeLoops";
    o.filenameSuffix = ".settings";
    o.folderName = paths::settings().getFullPathName();
    props = std::make_unique<juce::PropertiesFile> (paths::settings().getChildFile ("GubeeLoops.settings"), o);

    auto saved = props->getXmlValue ("audio");
    auto err = devices.initialise (2, 2, saved.get(), true);
    if (err.isNotEmpty()) devices.initialise (0, 2, nullptr, true);

    // listen to every MIDI keyboard / controller
    for (auto& d : juce::MidiInput::getAvailableDevices())
        if (! devices.isMidiInputDeviceEnabled (d.identifier)) devices.setMidiInputDeviceEnabled (d.identifier, true);

    engine = std::make_unique<Engine> (project, samples, plugins, devices);
    devices.addMidiInputDeviceCallback ({}, &engine->getMidiCollector());

    tools::generateStarterKit (paths::starterKit());
    library.rescan();
    newProject();
}

AppContext::~AppContext()
{
    if (auto xml = devices.createStateXml()) props->setValue ("audio", xml.get());
    props->saveIfNeeded();
    devices.removeMidiInputDeviceCallback ({}, &engine->getMidiCollector());
    engine.reset();
    devices.closeAudioDevice();
}

void AppContext::selectChannel (int id)
{
    selChannel = id;
    engine->selectedChannel = id;
    sendChangeMessage();
}

void AppContext::setCurrentPattern (int id)
{
    engine->setCurrentPattern (id);
    sendChangeMessage();
}

void AppContext::ensureValidSelection()
{
    if (! project.findChannel (selChannel).isValid())
        selectChannel (project.channels().getNumChildren() > 0 ? (int) project.channels().getChild (0).getProperty (ids::id) : -1);
    if (! project.findPattern (currentPattern()).isValid() && project.patterns().getNumChildren() > 0)
        setCurrentPattern ((int) project.patterns().getChild (0).getProperty (ids::id));
}

juce::ValueTree AppContext::addPadsChannel (const juce::String& name, bool withKit)
{
    auto ch = project.addChannel ("pads", name);
    if (withKit)
    {
        const char* names[] = { "Kick", "Snare", "Clap", "Hat Closed", "Hat Open", "Rim", "Perc", "Shaker",
                                "Tom Low", "Tom High", "Crash", "Snap", "808 F" };
        int id = (int) ch.getProperty (ids::id);
        for (int i = 0; i < (int) std::size (names); ++i)
        {
            auto f = paths::starterKit().getChildFile (juce::String (names[i]) + ".wav");
            if (! f.existsAsFile()) continue;
            auto pad = project.setPadSample (id, i, f);
            if (i == 3 || i == 4) pad.setProperty (ids::choke, 1, nullptr);
        }
    }
    return ch;
}

juce::ValueTree AppContext::addSamplerChannel (const juce::File& f)
{
    return project.addChannel ("sampler", f.getFileNameWithoutExtension(), {}, {}, f);
}

juce::ValueTree AppContext::addPluginChannel (const juce::PluginDescription& d)
{
    return project.addChannel ("plugin", d.name, {}, PluginHost::descToXml (d));
}

juce::ValueTree AppContext::addInstrument (const juce::String& kind, const juce::String& builtin, const juce::String& xml, const juce::String& name)
{
    project.beginEdit ("Add instrument");
    juce::ValueTree ch;
    if (kind == "pads-kit") ch = addPadsChannel (name, true);
    else if (kind == "pads") ch = addPadsChannel (name, false);
    else if (kind == "sampler") ch = project.addChannel ("sampler", name);
    else ch = project.addChannel (kind, name, builtin, xml);
    if (builtin == "Gubee 808") ch.setProperty (ids::root, 36, nullptr);
    selectChannel ((int) ch.getProperty (ids::id));
    return ch;
}

void AppContext::newProject()
{
    engine->stop();
    project.newProject (false);
    auto kit = addPadsChannel ("Gubee Kit", true);
    project.addChannel ("builtin", "Gubee 808", "Gubee 808").setProperty (ids::root, 36, nullptr);
    project.addChannel ("builtin", "Gubee Synth", "Gubee Synth");

    // a starter beat so the first press of Play does something
    int pat = (int) project.patterns().getChild (0).getProperty (ids::id);
    int kid = (int) kit.getProperty (ids::id);
    for (int s : { 0, 7, 10 }) project.addNote (pat, kid, s * stepBeats, stepBeats, 36 + 0, 0.9f);
    for (int s : { 4, 12 })    project.addNote (pat, kid, s * stepBeats, stepBeats, 36 + 2, 0.85f);
    for (int s = 0; s < 16; s += 2) project.addNote (pat, kid, s * stepBeats, stepBeats, 36 + 3, s % 4 == 0 ? 0.75f : 0.55f);

    project.undo.clearUndoHistory();
    project.dirty = false;
    selChannel = -1;
    ensureValidSelection();
    setCurrentPattern (pat);
    engine->setMode (TransportMode::pattern);
    engine->syncNow();
}

bool AppContext::openProject (const juce::File& f)
{
    engine->stop();
    if (! project.load (f)) return false;
    selChannel = -1;
    ensureValidSelection();
    if (project.patterns().getNumChildren() > 0) setCurrentPattern ((int) project.patterns().getChild (0).getProperty (ids::id));
    engine->syncNow();
    props->setValue ("lastProject", f.getFullPathName());
    sendChangeMessage();
    return true;
}

bool AppContext::saveProject (const juce::File& f)
{
    engine->storeStatesToTree();
    bool ok = project.save (f);
    if (ok) props->setValue ("lastProject", f.getFullPathName());
    sendChangeMessage();
    return ok;
}

//==============================================================================
void askForText (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> onOk)
{
    auto w = std::make_shared<juce::AlertWindow> (title, juce::String(), juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("t", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, onOk] (int r)
    {
        if (r == 1) onOk (w->getTextEditorContents ("t"));
    }), false);
}

void showInstrumentMenu (AppContext& ctx, juce::Component* target,
                         std::function<void (juce::String, juce::String, juce::String, juce::String)> cb)
{
    juce::PopupMenu m;
    m.addSectionHeader ("Gubee instruments");
    m.addItem (1, "Gubee Drum Pads (starter kit)");
    m.addItem (2, "Gubee Drum Pads (empty)");
    m.addItem (3, "Gubee Sampler (empty)");
    m.addItem (4, "Gubee 808");
    m.addItem (5, "Gubee Synth");
    juce::PopupMenu plug;
    auto types = ctx.plugins.known.getTypes();
    std::vector<juce::PluginDescription> inst;
    for (auto& t : types) if (t.isInstrument) inst.push_back (t);
    std::sort (inst.begin(), inst.end(), [] (auto& a, auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    for (size_t i = 0; i < inst.size(); ++i) plug.addItem (100 + (int) i, inst[i].name + "  (" + inst[i].manufacturerName + ")");
    if (inst.empty()) plug.addItem (-1, "No instrument plugins found yet", false);
    m.addSubMenu ("Plugins", plug);
    m.addSeparator();
    m.addItem (50, "Find / manage plugins...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [&ctx, inst, cb] (int r)
    {
        if (r == 1) cb ("pads-kit", {}, {}, "Gubee Kit");
        else if (r == 2) cb ("pads", {}, {}, "Drum Pads");
        else if (r == 3) cb ("sampler", {}, {}, "Sampler");
        else if (r == 4) cb ("builtin", "Gubee 808", {}, "Gubee 808");
        else if (r == 5) cb ("builtin", "Gubee Synth", {}, "Gubee Synth");
        else if (r == 50) { if (ctx.openPluginManager) ctx.openPluginManager(); }
        else if (r >= 100) cb ("plugin", {}, PluginHost::descToXml (inst[(size_t) (r - 100)]), inst[(size_t) (r - 100)].name);
    });
}

void showEffectMenu (AppContext& ctx, juce::Component* target, std::function<void (juce::String, juce::String, juce::String)> cb)
{
    juce::PopupMenu m;
    m.addSectionHeader ("Gubee effects");
    auto fx = BuiltinRegistry::effects();
    for (int i = 0; i < fx.size(); ++i) m.addItem (1 + i, fx[i]);
    juce::PopupMenu plug;
    std::vector<juce::PluginDescription> effects;
    for (auto& t : ctx.plugins.known.getTypes()) if (! t.isInstrument) effects.push_back (t);
    std::sort (effects.begin(), effects.end(), [] (auto& a, auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    for (size_t i = 0; i < effects.size(); ++i) plug.addItem (100 + (int) i, effects[i].name + "  (" + effects[i].manufacturerName + ")");
    if (effects.empty()) plug.addItem (-1, "No effect plugins found yet", false);
    m.addSubMenu ("Plugins", plug);
    m.addSeparator();
    m.addItem (50, "Find / manage plugins...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [&ctx, fx, effects, cb] (int r)
    {
        if (r >= 1 && r <= fx.size()) cb ("builtin", fx[r - 1], {});
        else if (r == 50) { if (ctx.openPluginManager) ctx.openPluginManager(); }
        else if (r >= 100) cb ("plugin", {}, PluginHost::descToXml (effects[(size_t) (r - 100)]));
    });
}
}
