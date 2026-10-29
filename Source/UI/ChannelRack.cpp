#include "ChannelRack.h"
#include "BrowserPanel.h"

namespace gubee
{
static constexpr int rowH = 30, ctrlW = 16 + 6 + 26 + 26 + 34 + 140 + 12;

//==============================================================================
struct ChannelRack::Row : public juce::Component
{
    Row (ChannelRack& r, juce::ValueTree c, int padIndex, bool isHeader)
        : rack (r), ch (c), pad (padIndex), header (isHeader),
          pan (r.ctx.project.um(), -1, 1, 0, "Pan"), vol (r.ctx.project.um(), 0, padIndex >= 0 ? 2.0 : 1.25, padIndex >= 0 ? 1.0 : 0.78, "Volume")
    {
        auto padNode = pad >= 0 ? r.ctx.project.getPad ((int) ch.getProperty (ids::id), pad) : juce::ValueTree();
        if (pad >= 0) { pan.bind (padNode, ids::pan); vol.bind (padNode, ids::gain); }
        else { pan.bind (ch, ids::pan); vol.bind (ch, ids::vol); }
        addAndMakeVisible (pan);
        addAndMakeVisible (vol);
        addAndMakeVisible (name);
        addAndMakeVisible (mixBtn);
        if (pad < 0) addAndMakeVisible (muteBtn);
        muteBtn.setClickingTogglesState (false);
        muteBtn.onClick = [this]
        {
            rack.ctx.project.beginEdit ("Mute");
            ch.setProperty (ids::mute, ! (bool) ch.getProperty (ids::mute), rack.ctx.project.um());
        };
        name.onClick = [this] { onNameClick(); };
        mixBtn.onClick = [this] { mixerMenu(); };
        mixBtn.setTooltip ("Mixer track this goes to");
        refresh();
    }

    void refresh()
    {
        auto& pr = rack.ctx.project;
        const bool selected = (int) ch.getProperty (ids::id) == rack.ctx.selectedChannel();
        if (pad >= 0)
        {
            auto pn = pr.getPad ((int) ch.getProperty (ids::id), pad);
            name.setButtonText ("   " + juce::String (pad + 1) + "  " + pn.getProperty (ids::name).toString());
            int out = (int) pn.getProperty (ids::output, -1);
            mixBtn.setButtonText (out < 0 ? juce::String ("--") : juce::String (out));
        }
        else
        {
            juce::String label = ch.getProperty (ids::name).toString();
            if (header) label = (rack.collapsed.count ((int) ch.getProperty (ids::id)) ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb8 ")) : juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe "))) + label;
            if (rack.ctx.engine->channelError ((int) ch.getProperty (ids::id)).isNotEmpty()) label = "(!) " + label;
            name.setButtonText (label);
            mixBtn.setButtonText (juce::String ((int) ch.getProperty (ids::mixer, 0)));
            muteBtn.setColour (juce::TextButton::buttonColourId, (bool) ch.getProperty (ids::mute) ? col::outline : col::accent);
        }
        auto colour = col::fromIndex ((int) ch.getProperty (ids::colour, 0));
        name.setColour (juce::TextButton::buttonColourId, selected ? colour.darker (0.2f) : col::panelHi);
        name.setColour (juce::TextButton::textColourOffId, selected ? col::bg : col::text);
        repaint();
    }

    int key() const { return pad >= 0 ? DrumPadsProcessor::baseNote + pad : (int) ch.getProperty (ids::root, 60); }

    juce::ValueTree findStepNote (int step) const
    {
        auto p = rack.ctx.project.findPattern (rack.ctx.currentPattern());
        const double st = step * stepBeats;
        const int id = (int) ch.getProperty (ids::id);
        for (auto n : p)
            if ((int) n.getProperty (ids::ch) == id && std::abs ((double) n.getProperty (ids::start) - st) < 1.0e-6
                && (pad < 0 || (int) n.getProperty (ids::key) == key()))
                return n;
        return {};
    }

    void setStep (int step, bool on)
    {
        auto& pr = rack.ctx.project;
        auto n = findStepNote (step);
        if (on && ! n.isValid()) pr.addNote (rack.ctx.currentPattern(), (int) ch.getProperty (ids::id), step * stepBeats, stepBeats, key(), 0.78f);
        else if (! on && n.isValid()) n.getParent().removeChild (n, pr.um());
    }

    juce::Rectangle<int> stepArea() const { return getLocalBounds().withTrimmedLeft (ctrlW); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (pad >= 0 ? col::panel.darker (0.15f) : col::panel);
        g.fillRect (getLocalBounds());
        if (header && pad < 0)
        {
            auto a = stepArea().reduced (4, 6);
            g.setColour (col::textDim);
            g.setFont (juce::FontOptions (12.0f));
            int n = 0; for (auto c : ch) n += c.hasType (ids::PAD) ? 1 : 0;
            g.drawText ("Drum Pads - " + juce::String (n) + " sounds loaded.  Double-click the name to open the pads.",
                        a, juce::Justification::centredLeft);
            return;
        }
        const int steps = rack.numSteps();
        const int w = rack.stepWidth;
        auto area = stepArea();
        auto p = rack.ctx.project.findPattern (rack.ctx.currentPattern());
        const int id = (int) ch.getProperty (ids::id);
        std::vector<float> vel ((size_t) steps, -1.0f);
        for (auto n : p)
        {
            if ((int) n.getProperty (ids::ch) != id) continue;
            if (pad >= 0 && (int) n.getProperty (ids::key) != key()) continue;
            double st = n.getProperty (ids::start);
            int s = (int) std::round (st / stepBeats);
            if (s >= 0 && s < steps && std::abs (s * stepBeats - st) < 1.0e-6) vel[(size_t) s] = (float) n.getProperty (ids::vel);
        }
        int playStep = -1;
        if (rack.ctx.engine->isPlaying() && rack.ctx.engine->getMode() == TransportMode::pattern)
            playStep = (int) (rack.ctx.engine->getBeat() / stepBeats);
        auto colour = col::fromIndex ((int) ch.getProperty (ids::colour, 0));
        for (int s = 0; s < steps; ++s)
        {
            juce::Rectangle<int> r (area.getX() + s * w, area.getY() + 4, w - 3, area.getHeight() - 8);
            bool alt = (s / 4) % 2 == 1;
            g.setColour (vel[(size_t) s] >= 0 ? colour.withMultipliedBrightness (0.55f + vel[(size_t) s] * 0.6f)
                                              : (alt ? col::stepOffAlt : col::stepOff));
            g.fillRoundedRectangle (r.toFloat(), 3);
            if (s == playStep) { g.setColour (juce::Colours::white.withAlpha (0.35f)); g.fillRoundedRectangle (r.toFloat(), 3); }
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 3);
        r.removeFromLeft (4);
        if (pad < 0) muteBtn.setBounds (r.removeFromLeft (14).reduced (0, 5)); else r.removeFromLeft (14);
        r.removeFromLeft (6);
        pan.setBounds (r.removeFromLeft (26));
        vol.setBounds (r.removeFromLeft (26));
        mixBtn.setBounds (r.removeFromLeft (34).reduced (2, 1));
        name.setBounds (r.removeFromLeft (140));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (header && pad < 0) return;
        auto a = stepArea();
        if (! a.contains (e.getPosition())) return;
        int s = (e.x - a.getX()) / rack.stepWidth;
        if (s < 0 || s >= rack.numSteps()) return;
        rack.ctx.project.beginEdit ("Steps");
        if (e.mods.isPopupMenu()) { dragOn = 0; setStep (s, false); }
        else if (e.mods.isShiftDown())
        {
            // shift-click = change velocity by dragging; start at current
            dragOn = -2; velStep = s; velStartY = e.y;
            auto n = findStepNote (s);
            velStart = n.isValid() ? (float) n.getProperty (ids::vel) : 0.78f;
        }
        else
        {
            dragOn = findStepNote (s).isValid() ? 0 : 1;
            setStep (s, dragOn == 1);
            if (dragOn == 1) { rack.ctx.selectChannel ((int) ch.getProperty (ids::id)); rack.ctx.engine->sendLiveNote (key(), 0.78f); rack.ctx.engine->sendLiveNote (key(), 0.0f); }
        }
        lastStep = s;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragOn == -2)
        {
            auto n = findStepNote (velStep);
            if (n.isValid()) n.setProperty (ids::vel, juce::jlimit (0.05f, 1.0f, velStart - (e.y - velStartY) / 80.0f), rack.ctx.project.um());
            return;
        }
        if (dragOn < 0) return;
        int s = (e.x - stepArea().getX()) / rack.stepWidth;
        if (s < 0 || s >= rack.numSteps() || s == lastStep) return;
        lastStep = s;
        setStep (s, dragOn == 1);
    }

    void mouseUp (const juce::MouseEvent&) override { dragOn = -1; }

    void onNameClick()
    {
        auto& ctx = rack.ctx;
        const int id = (int) ch.getProperty (ids::id);
        auto mods = juce::ModifierKeys::getCurrentModifiers();
        if (mods.isPopupMenu()) { nameMenu(); return; }
        if (header && pad < 0 && juce::Time::getMillisecondCounter() - lastClick > 350)
        {
            if (rack.collapsed.count (id)) rack.collapsed.erase (id); else rack.collapsed.insert (id);
            lastClick = juce::Time::getMillisecondCounter();
            ctx.selectChannel (id);
            return;
        }
        if (juce::Time::getMillisecondCounter() - lastClick < 350)
        {
            if (ctx.openChannelEditor) ctx.openChannelEditor (id);
        }
        else
        {
            ctx.selectChannel (id);
            if (pad >= 0) { ctx.engine->sendLiveNote (key(), 0.8f); ctx.engine->sendLiveNote (key(), 0.0f); }
        }
        lastClick = juce::Time::getMillisecondCounter();
    }

    void mixerMenu()
    {
        auto& pr = rack.ctx.project;
        juce::PopupMenu m;
        if (pad >= 0) m.addItem (1000, "Same as Drum Pads channel");
        for (int i = 0; i < numMixerTracks; ++i)
            m.addItem (1 + i, juce::String (i) + "  " + pr.mixerTrack (i).getProperty (ids::name).toString());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&mixBtn), [this] (int r)
        {
            if (r == 0) return;
            auto& pr2 = rack.ctx.project;
            pr2.beginEdit ("Route");
            if (pad >= 0) pr2.getPad ((int) ch.getProperty (ids::id), pad).setProperty (ids::output, r == 1000 ? -1 : r - 1, pr2.um());
            else ch.setProperty (ids::mixer, r - 1, pr2.um());
        });
    }

    void nameMenu()
    {
        auto& ctx = rack.ctx;
        const int id = (int) ch.getProperty (ids::id);
        const auto kind = ch.getProperty (ids::kind).toString();
        juce::PopupMenu m;
        m.addItem (1, "Open instrument / editor");
        m.addItem (2, "Open in Piano Roll");
        m.addItem (3, "Rename...");
        m.addItem (4, "Change colour");
        if (kind == "sampler") { m.addItem (5, "Open sample in Chop Shop"); m.addItem (6, "Replace sample..."); }
        m.addItem (9, "Fill every 4 steps");
        m.addItem (10, "Fill every 2 steps");
        m.addItem (11, "Clear steps in this pattern");
        m.addSeparator();
        m.addItem (7, "Clone channel");
        m.addItem (8, "Delete channel");
        m.showMenuAsync ({}, [this, id, kind] (int r)
        {
            auto& c2 = rack.ctx;
            auto& pr = c2.project;
            if (r == 1 && c2.openChannelEditor) c2.openChannelEditor (id);
            else if (r == 2) { c2.selectChannel (id); if (c2.showView) c2.showView (viewPiano); }
            else if (r == 3) askForText ("Rename channel", ch.getProperty (ids::name), [this] (juce::String t) { rack.ctx.project.beginEdit ("Rename"); ch.setProperty (ids::name, t, rack.ctx.project.um()); });
            else if (r == 4) { pr.beginEdit ("Colour"); ch.setProperty (ids::colour, ((int) ch.getProperty (ids::colour) + 1) % 10, pr.um()); }
            else if (r == 5 && c2.openInChopShop) c2.openInChopShop (juce::File (ch.getProperty (ids::sample).toString()), id, {});
            else if (r == 6)
            {
                rack.chooser = std::make_unique<juce::FileChooser> ("Choose a sample", paths::packs(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
                rack.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
                {
                    if (! fc.getResult().existsAsFile()) return;
                    rack.ctx.project.beginEdit ("Replace sample");
                    ch.setProperty (ids::sample, fc.getResult().getFullPathName(), rack.ctx.project.um());
                    ch.setProperty (ids::slices, "", rack.ctx.project.um());
                });
            }
            else if (r == 7)
            {
                pr.beginEdit ("Clone channel");
                auto copy = ch.createCopy();
                copy.setProperty (ids::id, pr.allocId(), nullptr);
                copy.setProperty (ids::name, ch.getProperty (ids::name).toString() + " 2", nullptr);
                c2.engine->storeStatesToTree();
                copy.setProperty (ids::state, ch.getProperty (ids::state), nullptr);
                pr.channels().addChild (copy, pr.channels().indexOf (ch) + 1, pr.um());
            }
            else if (r == 8) { pr.beginEdit ("Delete channel"); pr.removeChannel (id); c2.ensureValidSelection(); }
            else if (r == 9 || r == 10)
            {
                pr.beginEdit ("Fill steps");
                for (int s = 0; s < rack.numSteps(); s += (r == 9 ? 4 : 2)) setStep (s, true);
            }
            else if (r == 11)
            {
                pr.beginEdit ("Clear steps");
                for (int s = 0; s < rack.numSteps(); ++s) setStep (s, false);
            }
        });
    }

    ChannelRack& rack;
    juce::ValueTree ch;
    int pad;
    bool header;
    TreeKnob pan, vol;
    juce::TextButton name, mixBtn, muteBtn;
    int dragOn = -1, lastStep = -1, velStep = 0, velStartY = 0;
    float velStart = 0.78f;
    juce::uint32 lastClick = 0;
};

//==============================================================================
ChannelRack::ChannelRack (AppContext& c) : ctx (c)
{
    addAndMakeVisible (addBtn);
    addBtn.setTooltip ("Add an instrument: drum pads, sampler, 808, synth or a plugin");
    addBtn.onClick = [this] { addInstrument(); };
    addAndMakeVisible (drummerBtn);
    drummerBtn.setTooltip ("Generate a drum pattern by genre (like GarageBand's Drummer)");
    drummerBtn.onClick = [this] { showDrummerDialog (ctx, this); };
    addAndMakeVisible (repeatBtn);
    repeatBtn.setTooltip ("Note Repeat: hold a pad / key to retrigger it in time (great for hi-hat rolls)");
    repeatBtn.setClickingTogglesState (true);
    repeatBtn.setColour (juce::TextButton::buttonOnColourId, col::accent);
    repeatBtn.onClick = [this] { ctx.engine->noteRepeat = repeatBtn.getToggleState(); };
    addAndMakeVisible (repeatRate);
    repeatRate.addItemList ({ "1/4", "1/8", "1/8 T", "1/16", "1/16 T", "1/32" }, 1);
    repeatRate.setSelectedId (4, juce::dontSendNotification);
    repeatRate.onChange = [this]
    {
        const double v[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
        ctx.engine->noteRepeatBeats = v[juce::jlimit (0, 5, repeatRate.getSelectedItemIndex())];
    };
    addAndMakeVisible (clearBtn);
    clearBtn.setTooltip ("Clear every note in the current pattern");
    clearBtn.onClick = [this]
    {
        auto p = ctx.project.findPattern (ctx.currentPattern());
        ctx.project.beginEdit ("Clear pattern");
        for (int i = p.getNumChildren(); --i >= 0;) p.removeChild (i, ctx.project.um());
    };

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, true);

    ctx.project.root.addListener (this);
    ctx.addChangeListener (this);
    rebuild();
    startTimerHz (20);
}

ChannelRack::~ChannelRack()
{
    ctx.project.root.removeListener (this);
    ctx.removeChangeListener (this);
}

int ChannelRack::numSteps() const
{
    return juce::jlimit (4, 256, (int) std::round (ctx.project.patternLengthBeats (ctx.currentPattern()) / stepBeats));
}

void ChannelRack::rebuild()
{
    auto pos = viewport.getViewPosition();
    rows.clear();
    for (auto c : ctx.project.channels())
    {
        const bool pads = c.getProperty (ids::kind).toString() == "pads";
        rows.add (new Row (*this, c, -1, pads));
        if (pads && ! collapsed.count ((int) c.getProperty (ids::id)))
        {
            std::vector<int> idx;
            for (auto p : c) if (p.hasType (ids::PAD)) idx.push_back ((int) p.getProperty (ids::index));
            std::sort (idx.begin(), idx.end());
            for (int i : idx) rows.add (new Row (*this, c, i, false));
        }
    }
    for (auto* r : rows) content.addAndMakeVisible (r);
    resized();
    viewport.setViewPosition (pos);
}

void ChannelRack::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& id)
{
    if (t.hasType (ids::NOTE)) { for (auto* r : rows) r->repaint(); return; }
    if (t.hasType (ids::PATTERN) && id == ids::length) { resized(); return; }
    if (t.hasType (ids::CHANNEL) || t.hasType (ids::PAD))
    {
        if (id == ids::kind || id == ids::builtin || id == ids::plugin) triggerAsyncUpdate();
        else for (auto* r : rows) r->refresh();
    }
}

void ChannelRack::valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree& c)
{
    if (c.hasType (ids::NOTE)) { for (auto* r : rows) r->repaint(); return; }
    if (p.hasType (ids::CHANNELS) || c.hasType (ids::PAD)) triggerAsyncUpdate();
}

void ChannelRack::valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree& c, int)
{
    if (c.hasType (ids::NOTE)) { for (auto* r : rows) r->repaint(); return; }
    if (p.hasType (ids::CHANNELS) || c.hasType (ids::PAD)) triggerAsyncUpdate();
}

void ChannelRack::timerCallback()
{
    if (ctx.engine->isPlaying()) for (auto* r : rows) r->repaint (r->stepArea());
}

void ChannelRack::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    auto head = getLocalBounds().removeFromTop (36);
    g.setColour (col::panel);
    g.fillRect (head);
    g.setColour (col::text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    auto pat = ctx.project.findPattern (ctx.currentPattern());
    g.drawText ("Channel Rack  -  " + pat.getProperty (ids::name).toString(), head.reduced (10, 0), juce::Justification::centredLeft);
    if (rows.isEmpty())
    {
        g.setColour (col::textDim);
        g.drawText ("Click + Add, or drag a sound here from the browser", getLocalBounds().withTrimmedTop (40), juce::Justification::centred);
    }
}

void ChannelRack::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (36).reduced (6, 5);
    clearBtn.setBounds (head.removeFromRight (60)); head.removeFromRight (6);
    repeatRate.setBounds (head.removeFromRight (74)); head.removeFromRight (2);
    repeatBtn.setBounds (head.removeFromRight (96)); head.removeFromRight (6);
    drummerBtn.setBounds (head.removeFromRight (120)); head.removeFromRight (6);
    addBtn.setBounds (head.removeFromRight (70));
    viewport.setBounds (r);
    const int steps = numSteps();
    const int avail = viewport.getWidth() - ctrlW - 16;
    stepWidth = juce::jlimit (14, 30, avail / juce::jmax (1, steps));
    int w = juce::jmax (viewport.getWidth() - 12, ctrlW + steps * stepWidth + 10);
    content.setSize (w, juce::jmax (viewport.getHeight() - 12, rows.size() * rowH));
    int y = 0;
    for (auto* row : rows) { row->setBounds (0, y, w, rowH); y += rowH; }
}

void ChannelRack::addInstrument()
{
    showInstrumentMenu (ctx, &addBtn, [this] (juce::String kind, juce::String builtin, juce::String xml, juce::String name)
    {
        ctx.addInstrument (kind, builtin, xml, name);
    });
}

void ChannelRack::itemDropped (const SourceDetails& d)
{
    auto f = fileFromDrag (d.description);
    if (! f.existsAsFile()) return;
    // dropped on a row?
    auto pos = d.localPosition;
    for (auto* r : rows)
    {
        auto b = r->getBounds().translated (viewport.getX() - viewport.getViewPositionX(), viewport.getY() - viewport.getViewPositionY());
        if (! b.contains (pos)) continue;
        auto kind = r->ch.getProperty (ids::kind).toString();
        const int id = (int) r->ch.getProperty (ids::id);
        ctx.project.beginEdit ("Drop sample");
        if (r->pad >= 0) { ctx.project.setPadSample (id, r->pad, f); return; }
        if (kind == "pads")
        {
            for (int i = 0; i < DrumPadsProcessor::numPads; ++i)
                if (! ctx.project.getPad (id, i).isValid()) { ctx.project.setPadSample (id, i, f); return; }
        }
        if (kind == "sampler") { r->ch.setProperty (ids::sample, f.getFullPathName(), ctx.project.um()); r->ch.setProperty (ids::name, f.getFileNameWithoutExtension(), ctx.project.um()); return; }
        break;
    }
    ctx.project.beginEdit ("Add sampler");
    auto c = ctx.addSamplerChannel (f);
    ctx.selectChannel ((int) c.getProperty (ids::id));
}

//==============================================================================
// Gubee Drummer
//==============================================================================
namespace
{
struct DrummerComp : public juce::Component
{
    explicit DrummerComp (AppContext& c) : ctx (c)
    {
        addAndMakeVisible (genre);
        genre.addItemList ({ "Trap", "Boom Bap", "House", "Lo-Fi", "Drill", "Pop / Rock", "Reggaeton" }, 1);
        genre.setSelectedId (1);
        for (auto* s : { &complexity, &energy })
        {
            s->setSliderStyle (juce::Slider::LinearHorizontal);
            s->setRange (0, 1, 0.01);
            s->setValue (0.5);
            s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            addAndMakeVisible (*s);
        }
        addAndMakeVisible (bars);
        bars.addItemList ({ "1 bar", "2 bars", "4 bars" }, 1);
        bars.setSelectedId (2);
        addAndMakeVisible (fills);
        fills.setButtonText ("Add a fill at the end");
        fills.setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (setTempo);
        setTempo.setButtonText ("Set a typical tempo for the genre");
        addAndMakeVisible (gen);
        gen.setButtonText ("Generate into current pattern");
        gen.onClick = [this] { generate (false); };
        addAndMakeVisible (genNew);
        genNew.setButtonText ("Generate into a NEW pattern");
        genNew.onClick = [this] { generate (true); };
        setSize (380, 300);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::panel);
        g.setColour (col::text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Genre", 12, 14, 90, 24, juce::Justification::centredLeft);
        g.drawText ("Simple  <->  Complex", 12, 50, 150, 20, juce::Justification::centredLeft);
        g.drawText ("Quiet  <->  Loud", 12, 96, 150, 20, juce::Justification::centredLeft);
        g.drawText ("Length", 12, 144, 90, 24, juce::Justification::centredLeft);
    }

    void resized() override
    {
        genre.setBounds (110, 14, 250, 24);
        complexity.setBounds (12, 68, 350, 24);
        energy.setBounds (12, 114, 350, 24);
        bars.setBounds (110, 144, 250, 24);
        fills.setBounds (12, 176, 300, 22);
        setTempo.setBounds (12, 200, 300, 22);
        gen.setBounds (12, 234, 172, 30);
        genNew.setBounds (190, 234, 172, 30);
    }

    void generate (bool newPattern)
    {
        auto& pr = ctx.project;
        pr.beginEdit ("Gubee Drummer");
        // find a drum pads channel
        juce::ValueTree kit = pr.findChannel (ctx.selectedChannel());
        if (kit.getProperty (ids::kind).toString() != "pads")
        {
            kit = {};
            for (auto c : pr.channels()) if (c.getProperty (ids::kind).toString() == "pads") { kit = c; break; }
        }
        if (! kit.isValid()) kit = ctx.addPadsChannel ("Gubee Kit", true);
        const int kid = (int) kit.getProperty (ids::id);

        auto findPad = [&] (std::initializer_list<const char*> words, int fallback)
        {
            for (auto* w : words)
                for (auto p : kit)
                    if (p.hasType (ids::PAD) && p.getProperty (ids::name).toString().containsIgnoreCase (w))
                        return (int) p.getProperty (ids::index);
            return fallback;
        };
        const int kick = findPad ({ "kick", "bd" }, 0), snare = findPad ({ "snare", "sd" }, 1), clap = findPad ({ "clap" }, 2);
        const int hat = findPad ({ "hat closed", "closed", "chh", "hh", "hat" }, 3), ohat = findPad ({ "open" }, 4);
        const int rim = findPad ({ "rim" }, 5), perc = findPad ({ "perc", "shaker" }, 6), tomL = findPad ({ "tom low", "tom" }, 8), tomH = findPad ({ "tom high" }, 9);
        const int crash = findPad ({ "crash", "cymbal" }, 10);

        int pat = ctx.currentPattern();
        if (newPattern) { auto p = pr.addPattern(); pat = (int) p.getProperty (ids::id); ctx.setCurrentPattern (pat); }
        auto p = pr.findPattern (pat);
        for (int i = p.getNumChildren(); --i >= 0;)
            if ((int) p.getChild (i).getProperty (ids::ch) == kid) p.removeChild (i, pr.um());
        const int nb = bars.getSelectedId() == 1 ? 1 : bars.getSelectedId() == 2 ? 2 : 4;
        p.setProperty (ids::length, juce::jmax ((double) p.getProperty (ids::length, 4.0), nb * 4.0), pr.um());

        juce::Random rnd ((juce::int64) juce::Time::getMillisecondCounterHiRes());
        const float cx = (float) complexity.getValue(), en = (float) energy.getValue();
        const int g = genre.getSelectedId();
        auto hit = [&] (int bar, double step16, int padIdx, float v)
        {
            v = juce::jlimit (0.05f, 1.0f, v * (0.55f + 0.45f * en) + (rnd.nextFloat() - 0.5f) * 0.1f);
            pr.addNote (pat, kid, bar * 4.0 + step16 * stepBeats, stepBeats * 0.9, DrumPadsProcessor::baseNote + padIdx, v);
        };
        auto chance = [&] (float p01) { return rnd.nextFloat() < p01; };

        const double tempos[] = { 0, 142, 90, 124, 80, 142, 110, 95 };
        if (setTempo.getToggleState()) pr.root.setProperty (ids::tempo, tempos[g], pr.um());

        for (int b = 0; b < nb; ++b)
        {
            const bool fillBar = fills.getToggleState() && b == nb - 1 && nb > 1;
            switch (g)
            {
                case 1: // Trap: half-time snare on 3, rolling hats
                {
                    hit (b, 0, kick, 1.0f);
                    if (chance (0.5f + cx * 0.4f)) hit (b, rnd.nextBool() ? 7 : 6, kick, 0.9f);
                    if (chance (0.6f)) hit (b, 10, kick, 0.9f);
                    if (chance (cx * 0.6f)) hit (b, 13, kick, 0.8f);
                    hit (b, 8, clap, 1.0f); hit (b, 8, snare, 0.8f);
                    for (int s = 0; s < 16; s += 2)
                    {
                        bool roll = chance (cx * 0.35f) && s != 0;
                        if (roll)
                        {
                            const int div = chance (0.5f) ? 3 : 4;   // triplet or 32nd roll
                            for (int k = 0; k < div; ++k) hit (b, s + k * (2.0 / div), hat, 0.55f + 0.1f * k);
                        }
                        else hit (b, s, hat, s % 4 == 0 ? 0.8f : 0.6f);
                    }
                    if (chance (en * 0.5f)) hit (b, 14, ohat, 0.6f);
                    break;
                }
                case 2: // Boom bap
                case 4: // Lo-fi (softer, rim)
                {
                    const float soft = g == 4 ? 0.75f : 1.0f;
                    hit (b, 0, kick, soft);
                    hit (b, 10, kick, 0.85f * soft);
                    if (chance (0.4f + cx * 0.4f)) hit (b, 7, kick, 0.7f * soft);
                    if (chance (cx * 0.5f)) hit (b, 15, kick, 0.6f * soft);
                    const int sn = g == 4 && chance (0.5f) ? rim : snare;
                    hit (b, 4, sn, soft); hit (b, 12, sn, soft);
                    if (chance (cx * 0.6f)) hit (b, rnd.nextBool() ? 9 : 14, snare, 0.35f);
                    for (int s = 0; s < 16; s += 2) hit (b, s, hat, (s % 4 == 0 ? 0.7f : 0.5f) * soft);
                    if (chance (cx * 0.5f)) hit (b, 15, hat, 0.35f);
                    break;
                }
                case 3: // House
                {
                    for (int s = 0; s < 16; s += 4) hit (b, s, kick, 1.0f);
                    hit (b, 4, clap, 0.9f); hit (b, 12, clap, 0.9f);
                    for (int s = 2; s < 16; s += 4) hit (b, s, en > 0.4f ? ohat : hat, 0.7f);
                    if (cx > 0.3f) for (int s = 0; s < 16; ++s) if (s % 2 == 1 && chance (cx * 0.6f)) hit (b, s, hat, 0.45f);
                    if (chance (cx * 0.7f)) hit (b, rnd.nextBool() ? 7 : 11, perc, 0.6f);
                    break;
                }
                case 5: // Drill: sliding kicks, late snares, triplet hats
                {
                    hit (b, 0, kick, 1.0f);
                    hit (b, chance (0.5f) ? 11 : 10, kick, 0.85f);
                    if (chance (cx)) hit (b, 6, kick, 0.75f);
                    hit (b, 8, snare, 1.0f);
                    if (chance (0.6f)) hit (b, 14, snare, 0.85f);
                    for (double s = 0; s < 16; s += 4.0 / 3.0 * (chance (cx * 0.5f) ? 0.5 : 1.0)) hit (b, s, hat, 0.6f);
                    if (chance (en * 0.4f)) hit (b, 3, perc, 0.6f);
                    break;
                }
                case 6: // Pop / rock
                {
                    hit (b, 0, kick, 1.0f); hit (b, 8, kick, 0.9f);
                    if (chance (0.5f + cx * 0.3f)) hit (b, 10, kick, 0.75f);
                    if (chance (cx * 0.5f)) hit (b, 6, kick, 0.7f);
                    hit (b, 4, snare, 1.0f); hit (b, 12, snare, 1.0f);
                    for (int s = 0; s < 16; s += 2) hit (b, s, en > 0.75f ? ohat : hat, s % 4 == 0 ? 0.75f : 0.55f);
                    if (b == 0 && en > 0.5f) hit (b, 0, crash, 0.8f);
                    break;
                }
                default: // Reggaeton / dembow
                {
                    for (int s = 0; s < 16; s += 4) hit (b, s, kick, 1.0f);
                    for (int s : { 3, 6, 11, 14 }) hit (b, s, snare, 0.85f);
                    for (int s = 0; s < 16; s += 2) hit (b, s, hat, 0.5f);
                    if (chance (cx * 0.6f)) hit (b, 7, perc, 0.6f);
                    break;
                }
            }
            if (fillBar)
            {
                // replace the end of the last bar with a fill
                const double fillStart = b * 4.0 + 12 * stepBeats;
                for (int i = p.getNumChildren(); --i >= 0;)
                {
                    auto n = p.getChild (i);
                    if ((int) n.getProperty (ids::ch) == kid && (double) n.getProperty (ids::start) >= fillStart - 1.0e-6
                        && (int) n.getProperty (ids::key) != DrumPadsProcessor::baseNote + kick)
                        p.removeChild (i, pr.um());
                }
                const int fillPads[] = { snare, snare, tomH, tomH, tomL, tomL, snare, snare };
                const int count = cx > 0.6f ? 8 : 4;
                for (int k = 0; k < count; ++k)
                    hit (b, 12 + k * (4.0 / count), fillPads[(size_t) (k * 8 / count)], 0.5f + 0.5f * k / count);
            }
        }
        ctx.selectChannel (kid);
        ctx.status ("Gubee Drummer wrote a " + genre.getText() + " beat");
    }

    AppContext& ctx;
    juce::ComboBox genre, bars;
    juce::Slider complexity, energy;
    juce::ToggleButton fills, setTempo;
    juce::TextButton gen, genNew;
};
}

void showDrummerDialog (AppContext& ctx, juce::Component* parent)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new DrummerComp (ctx));
    o.dialogTitle = "Gubee Drummer";
    o.dialogBackgroundColour = col::panel;
    o.componentToCentreAround = parent;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}
}
