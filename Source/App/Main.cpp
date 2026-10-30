#include <JuceHeader.h>
#include "UI/MainComponent.h"
#include "GubeeDSP/GubeeFX.h"

namespace gubee
{
/** Splash screen with the cat while the app starts up. */
class Splash : public juce::Component
{
public:
    Splash() { setSize (420, 300); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg);
        drawCatLogo (g, getLocalBounds().removeFromTop (220).toFloat().reduced (20));
        g.setColour (col::text);
        g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        g.drawText ("GubeeLoops", getLocalBounds().removeFromBottom (80).removeFromTop (44), juce::Justification::centred);
        g.setColour (col::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("warming up the studio...", getLocalBounds().removeFromBottom (36), juce::Justification::centred);
    }
};

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (AppContext& ctx)
        : juce::DocumentWindow ("GubeeLoops", col::bg, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        auto* mc = new MainComponent (ctx);
        mc->onTitleChange = [this] (const juce::String& t) { setName (t); };
        main = mc;
        setContentOwned (mc, true);
        setResizable (true, true);
        setResizeLimits (1000, 640, 10000, 10000);
        centreWithSize (juce::jmin (1600, juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.getWidth() - 40),
                        juce::jmin (950, juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.getHeight() - 60));
        setVisible (true);
        mc->grabKeyboardFocus();
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    MainComponent* main = nullptr;
};

class GubeeApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "GubeeLoops"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
        auto args = getCommandLineParameterArray();
        if (args.contains ("--gubee-scan"))
        {
            // child process used for crash-safe plugin scanning
            setApplicationReturnValue (runPluginScanChild (args));
            quit();
            return;
        }
        juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
        splash = std::make_unique<juce::DocumentWindow> ("GubeeLoops", col::bg, 0);
        splash->setUsingNativeTitleBar (false);
        splash->setTitleBarHeight (0);
        splash->setContentOwned (new Splash(), true);
        splash->centreWithSize (420, 300);
        splash->setVisible (true);
        splash->toFront (false);
        // let the splash paint, then build the studio
        juce::Timer::callAfterDelay (150, [this]
        {
            ctx = std::make_unique<AppContext>();
            ctx->plugins.known.setCustomScanner (std::make_unique<OutOfProcessScanner>());
            seedPluginPaths();
            window = std::make_unique<MainWindow> (*ctx);
            splash.reset();
            auto a = getCommandLineParameterArray();
            int st = a.indexOf ("--gubee-selftest");
            if (st >= 0) runSelfTest (juce::File (a[st + 1]));
        });
    }

    /** Developer self-test: snapshots every view and renders audio, then quits. */
    void runSelfTest (juce::File dir)
    {
        dir.createDirectory();
        auto* mc = window->main;
        auto shot = [mc, dir] (const juce::String& name)
        {
            auto img = mc->createComponentSnapshot (mc->getLocalBounds());
            juce::FileOutputStream os (dir.getChildFile (name + ".png"));
            os.setPosition (0); os.truncate();
            juce::PNGImageFormat().writeImageToStream (img, os);
        };
        auto log = [dir] (const juce::String& s) { dir.getChildFile ("selftest.log").appendText (s + "\n"); };
        int step = 0;
        selfTestTimer = std::make_unique<LambdaTimer> ([this, mc, shot, log, dir, step]() mutable
        {
            auto& c = *ctx;
            switch (step++)
            {
                case 0: { juce::String err; Engine::ExportOptions o; o.file = dir.getChildFile ("pattern.wav"); o.patternOnly = true;
                          c.engine->storeStatesToTree(); c.engine->syncNow();
                          bool ok = c.engine->exportAudio (o, nullptr, err); log ("export pattern: " + juce::String (ok ? "ok" : err)); break; }
                case 1: mc->showView (viewRack); break;
                case 2: shot ("rack"); mc->showView (viewPiano); break;
                case 3: shot ("piano"); mc->showView (viewPlaylist); break;
                case 4: { // arrange: 4 clips + an audio loop
                          int pat = c.currentPattern();
                          for (int i = 0; i < 4; ++i) c.project.addPatternClip (0, i * 4.0, pat);
                          auto f = paths::starterKit().getChildFile ("808 F.wav");
                          c.project.addAudioClip (1, 4.0, f, 1.8, 0.0);
                          c.project.addAutomationClip (2, 0, 16, "mx:0:vol", "Master volume");
                          break; }
                case 5: shot ("playlist"); mc->showView (viewMixer); break;
                case 6: shot ("mixer"); c.project.setSlot (1, 0, "builtin", "Gubee Reverb", {}); mc->showView (viewJam); break;
                case 7: c.project.setJamPattern (0, 0, c.currentPattern()); c.project.setJamAudio (1, 0, paths::starterKit().getChildFile ("Shaker.wav"), 1, 0); break;
                case 8: shot ("jam"); c.openInChopShop (paths::starterKit().getChildFile ("808 F.wav"), -1, {}); break;
                case 9:
                {
                    shot ("chop");
                    for (auto name : { "Gubee FM", "Gubee Comp", "Gubee Scope", "Gubee Drum Synth", "Gubee Wavetable", "Gubee Amp" })
                    {
                        auto proc = BuiltinRegistry::create (name);
                        proc->prepareToPlay (44100, 512);
                        std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
                        auto img = ed->createComponentSnapshot (ed->getLocalBounds());
                        juce::FileOutputStream os (dir.getChildFile (juce::String ("plugin-") + name + ".png"));
                        os.setPosition (0); os.truncate();
                        juce::PNGImageFormat().writeImageToStream (img, os);
                        ed.reset();
                    }
                    break;
                }
                case 10: { // give the 808 + synth some notes and put effects on the kit
                           int pat = c.currentPattern();
                           auto chs = c.project.channels();
                           int id808 = chs.getChild (1).getProperty (ids::id), idSyn = chs.getChild (2).getProperty (ids::id);
                           c.project.addNote (pat, id808, 0, 1.5, 36, 0.9f);
                           c.project.addNote (pat, id808, 2, 1.0, 39, 0.9f);
                           for (int k : { 60, 63, 67 }) c.project.addNote (pat, idSyn, 0, 3.0, k, 0.7f);
                           c.project.setSlot (1, 1, "builtin", "Gubee Delay", {});
                           c.project.setSlot (0, 0, "builtin", "Gubee Limiter", {});
                           c.project.addModulator ("mx:3:pan", "Synth pan wobble");
                           juce::String err; Engine::ExportOptions o; o.file = dir.getChildFile ("song.wav"); o.stems = true;
                           for (int i = 0; i < numMixerTracks; ++i) o.trackNames.add (c.project.mixerTrack (i).getProperty (ids::name).toString());
                           c.engine->storeStatesToTree(); c.engine->syncNow();
                           juce::Thread::sleep (300);
                           bool ok = c.engine->exportAudio (o, nullptr, err); log ("export song: " + juce::String (ok ? "ok" : err)); break; }
                case 11:
                {
                    c.engine->launchJam (0, 0);
                    c.engine->launchJam (1, 0);
                    const double sr = c.engine->getSampleRate();
                    juce::AudioBuffer<float> all (2, (int) (sr * 60.0 / c.project.tempo() * 16)), blk (2, 512);
                    all.clear();
                    int pos = 0, launched = 0;
                    while (pos + 512 <= all.getNumSamples())
                    {
                        c.engine->renderBlock (blk);
                        for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, pos, blk, ch, 0, 512);
                        pos += 512;
                        if (pos > sr * 60.0 / c.project.tempo() * 6 && launched == 0) { c.engine->stopJamTrack (1); launched = 1; }
                    }
                    log ("jam playing: t0=" + juce::String (c.engine->jamPlayingScene (0)) + " t1=" + juce::String (c.engine->jamPlayingScene (1))
                         + " peak=" + juce::String (all.getMagnitude (0, all.getNumSamples()), 3));
                    tools::writeWav (dir.getChildFile ("jam.wav"), all, sr);
                    c.engine->stop();
                    break;
                }
                case 13:
                {
                    // arpeggiator: synth channel, chord held for a bar, arp up at 1/16
                    auto syn = c.project.channels().getChild (2);
                    syn.setProperty (ids::arp, 1, nullptr); syn.setProperty (ids::arpRate, 0.25, nullptr); syn.setProperty (ids::arpGate, 0.5, nullptr);
                    auto pat = c.project.addPattern ("arp test");
                    const int pid = (int) pat.getProperty (ids::id);
                    for (int k : { 60, 64, 67 }) c.project.addNote (pid, (int) syn.getProperty (ids::id), 0, 4, k, 0.8f);
                    c.setCurrentPattern (pid);
                    c.engine->storeStatesToTree(); c.engine->syncNow();
                    juce::String err; Engine::ExportOptions o; o.file = dir.getChildFile ("arp.wav"); o.patternOnly = true;
                    c.engine->exportAudio (o, nullptr, err);
                    if (auto d = c.samples.get (o.file))
                    {
                        auto tr = tools::detectTransients (d->buffer, d->sampleRate, 0.7f, 0.03);
                        log ("arp onsets in 1 bar: " + juce::String ((int) tr.size()) + " (expect ~16)");
                    }
                    syn.setProperty (ids::arp, 0, nullptr);
                    break;
                }
                case 14:
                {
                    // sidechain: comp on the synth's track keyed from the drum track
                    c.setCurrentPattern ((int) c.project.patterns().getChild (0).getProperty (ids::id));
                    auto syn = c.project.channels().getChild (2);
                    const int synTrack = (int) syn.getProperty (ids::mixer);
                    const int kitTrack = (int) c.project.channels().getChild (0).getProperty (ids::mixer);
                    auto pat = c.project.findPattern (c.currentPattern());
                    for (int k : { 48, 55 }) c.project.addNote (c.currentPattern(), (int) syn.getProperty (ids::id), 0, 4, k, 0.8f);
                    c.project.patterns();
                    auto measure = [&] (const juce::String& nm)
                    {
                        c.engine->storeStatesToTree(); c.engine->syncNow();
                        juce::String err; Engine::ExportOptions o; o.file = dir.getChildFile (nm + ".wav"); o.patternOnly = true; o.stems = true;
                        for (int i = 0; i < numMixerTracks; ++i) o.trackNames.add ("t" + juce::String (i));
                        c.engine->exportAudio (o, nullptr, err);
                        auto f = dir.getChildFile (nm + " - t" + juce::String (synTrack) + ".wav");
                        c.samples.invalidate (f);
                        auto d = c.samples.get (f);
                        return d ? d->buffer.getRMSLevel (0, 0, d->buffer.getNumSamples()) : -1.0f;
                    };
                    const float before = measure ("sc-off");
                    auto slot = c.project.setSlot (synTrack, 0, "builtin", "Gubee Comp", {});
                    slot.setProperty (ids::sc, kitTrack, nullptr);
                    c.engine->syncNow();
                    if (auto* comp = c.engine->slotProcessor (synTrack, 0))
                        if (auto* gp = dynamic_cast<GubeeProcessor*> (comp))
                        {
                            gp->apvts.getParameter ("sidechain")->setValueNotifyingHost (1.0f);
                            gp->apvts.getParameter ("threshold")->setValueNotifyingHost (gp->apvts.getParameter ("threshold")->convertTo0to1 (-40.0f));
                            gp->apvts.getParameter ("ratio")->setValueNotifyingHost (gp->apvts.getParameter ("ratio")->convertTo0to1 (20.0f));
                        }
                    if (auto* comp = c.engine->slotProcessor (synTrack, 0))
                        log ("comp channels in=" + juce::String (comp->getTotalNumInputChannels()) + " buses=" + juce::String (comp->getBusCount (true))
                             + " scEnabled=" + juce::String ((int) (comp->getBusCount (true) > 1 && comp->getBus (true, 1)->isEnabled())));
                    const float after = measure ("sc-on");
                    log ("sidechain: synth RMS " + juce::String (before, 4) + " -> " + juce::String (after, 4) + " (should drop)");
                    juce::ignoreUnused (pat);
                    break;
                }
                case 15: c.engine->stop(); c.project.save (dir.getChildFile ("test.gubee")); log ("saved"); break;
                case 16: { bool ok = c.openProject (dir.getChildFile ("test.gubee")); log ("reopen: " + juce::String (ok ? "ok" : "fail")
                               + " channels=" + juce::String (c.project.channels().getNumChildren())); break; }
                case 17:
                {
                    auto d = c.samples.get (dir.getChildFile ("pattern.wav"));
                    if (d)
                    {
                        auto tr = tools::detectTransients (d->buffer, d->sampleRate, 0.6f);
                        juce::String t; for (auto p : tr) t << juce::String (p / (d->sampleRate * 60.0 / 140.0 / 4.0), 2) << " ";
                        log ("transients (in steps): " + t);
                        auto st = tools::timeStretch (d->buffer, 1.25, d->sampleRate);
                        log ("stretch 1.25 ratio: " + juce::String ((double) st.getNumSamples() / d->buffer.getNumSamples(), 3));
                        auto pt = tools::stretchAndPitch (d->buffer, d->sampleRate, d->sampleRate, 1.0, 12);
                        log ("pitch +12 length ratio: " + juce::String ((double) pt.getNumSamples() / d->buffer.getNumSamples(), 3));
                        tools::writeWav (dir.getChildFile ("stretched.wav"), st, d->sampleRate);
                        tools::writeWav (dir.getChildFile ("pitched.wav"), pt, d->sampleRate);
                    }
                    for (auto name : BuiltinRegistry::effects())
                    {
                        auto p = BuiltinRegistry::create (name);
                        p->prepareToPlay (44100, 512);
                        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer mb;
                        float peak = 0; bool finite = true;
                        for (int blk = 0; blk < 172; ++blk)
                        {
                            for (int i = 0; i < 512; ++i) { float v = blk < 40 ? 0.5f * std::sin ((blk * 512 + i) * 0.0627f) : 0.0f; buf.setSample (0, i, v); buf.setSample (1, i, v); }
                            p->processBlock (buf, mb);
                            for (int i = 0; i < 512; ++i) { float v = buf.getSample (0, i); if (! std::isfinite (v)) finite = false; peak = juce::jmax (peak, std::abs (v)); }
                        }
                        log (name + ": peak " + juce::String (peak, 3) + (finite ? "" : "  NaN!"));
                    }
                    for (auto name : BuiltinRegistry::instruments())
                    {
                        auto p = BuiltinRegistry::create (name);
                        p->prepareToPlay (44100, 512);
                        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer mb;
                        mb.addEvent (juce::MidiMessage::noteOn (1, 48, 0.9f), 0);
                        float peak = 0;
                        for (int blk = 0; blk < 40; ++blk) { p->processBlock (buf, mb); mb.clear(); peak = juce::jmax (peak, buf.getMagnitude (0, 512)); }
                        log (name + ": peak " + juce::String (peak, 3));
                    }
                    auto measure = [] (juce::AudioProcessor& proc, float inHz)
                    {
                        proc.prepareToPlay (44100, 512);
                        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer mb;
                        std::vector<float> outv;
                        for (int blk = 0; blk < 120; ++blk)
                        {
                            for (int i = 0; i < 512; ++i) { float v = 0.5f * (float) std::sin ((blk * 512 + i) * juce::MathConstants<double>::twoPi * inHz / 44100.0); buf.setSample (0, i, v); buf.setSample (1, i, v); }
                            proc.processBlock (buf, mb);
                            if (blk > 80) for (int i = 0; i < 512; ++i) outv.push_back (buf.getSample (0, i));
                        }
                        return PitchDetector::detect (outv.data(), 4096, 44100, 50, 2000);
                    };
                    { PitchProcessor pp; pp.apvts.getParameter ("semis")->setValueNotifyingHost (pp.apvts.getParameter ("semis")->convertTo0to1 (12.0f));
                      log ("Pitch +12 on 220 Hz -> " + juce::String (measure (pp, 220.0f), 1) + " Hz (expect 440)"); }
                    { PitchProcessor pp; pp.apvts.getParameter ("semis")->setValueNotifyingHost (pp.apvts.getParameter ("semis")->convertTo0to1 (-5.0f));
                      log ("Pitch -5 on 440 Hz -> " + juce::String (measure (pp, 440.0f), 1) + " Hz (expect 329.6)"); }
                    { TuneProcessor tp; tp.apvts.getParameter ("humanize")->setValueNotifyingHost (0.0f);
                      log ("Tune C minor on 450 Hz -> " + juce::String (measure (tp, 450.0f), 1) + " Hz (expect 466.2)"); }
                    log ("bpm from name 'Dark_Loop_140bpm_Cmin': " + juce::String (tools::bpmFromName ("Dark_Loop_140bpm_Cmin")) + " key " + tools::keyFromName ("Dark_Loop_140bpm_Cmin"));
                    log ("done"); quit(); break;
                }
                default: break;
            }
        });
        selfTestTimer->startTimer (700);
    }

    struct LambdaTimer : public juce::Timer
    {
        explicit LambdaTimer (std::function<void()> f) : fn (std::move (f)) {}
        void timerCallback() override { fn(); }
        std::function<void()> fn;
    };
    std::unique_ptr<LambdaTimer> selfTestTimer;

    void seedPluginPaths()
    {
        auto& p = *ctx->props;
        const juce::String key = "lastPluginScanPath_VST3";
        juce::FileSearchPath path (p.getValue (key));
        for (auto f : { juce::File ("C:\\Program Files\\Common Files\\VST3"), paths::plugins() })
            if (f.isDirectory()) path.addIfNotAlreadyThere (f);
        p.setValue (key, path.toString());
    }

    void shutdown() override
    {
        selfTestTimer.reset();
        window.reset();
        ctx.reset();
        splash.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (window == nullptr || window->main == nullptr) { quit(); return; }
        window->main->tryQuit ([] (bool ok) { if (ok) juce::JUCEApplication::getInstance()->quit(); });
    }

    void anotherInstanceStarted (const juce::String&) override {}

private:
    GubeeLookAndFeel lnf;
    std::unique_ptr<AppContext> ctx;
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<juce::DocumentWindow> splash;
};
}

START_JUCE_APPLICATION (gubee::GubeeApplication)
