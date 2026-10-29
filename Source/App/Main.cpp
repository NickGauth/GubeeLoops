#include <JuceHeader.h>
#include "UI/MainComponent.h"

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
                case 9: shot ("chop"); break;
                case 10: { juce::String err; Engine::ExportOptions o; o.file = dir.getChildFile ("song.wav"); o.stems = true;
                           for (int i = 0; i < numMixerTracks; ++i) o.trackNames.add (c.project.mixerTrack (i).getProperty (ids::name).toString());
                           c.engine->storeStatesToTree(); c.engine->syncNow();
                           juce::Thread::sleep (300);
                           bool ok = c.engine->exportAudio (o, nullptr, err); log ("export song: " + juce::String (ok ? "ok" : err)); break; }
                case 11: c.engine->play(); break;
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
