#include "MixerView.h"

namespace gubee
{
static constexpr int stripW = 92;

/** Vertical fader bound to a tree property. */
struct TreeFader : public juce::Slider, private juce::ValueTree::Listener
{
    TreeFader (juce::UndoManager* um) : juce::Slider (juce::Slider::LinearVertical, juce::Slider::NoTextBox), undo (um)
    {
        setRange (0.0, 1.25, 0.0);
        setSkewFactorFromMidPoint (0.5);
        setDoubleClickReturnValue (true, 0.8);
        setPopupDisplayEnabled (true, true, nullptr);
        textFromValueFunction = [] (double v) { return v <= 0.0001 ? juce::String ("-inf dB") : juce::String (juce::Decibels::gainToDecibels (v), 1) + " dB"; };
        onDragStart = [this] { if (undo) undo->beginNewTransaction ("Fader"); };
        onValueChange = [this] { if (node.isValid() && ! updating) node.setProperty (ids::vol, getValue(), undo); };
    }
    ~TreeFader() override { node.removeListener (this); }
    void bind (juce::ValueTree n) { node = n; node.addListener (this); refresh(); }
    void refresh() { const juce::ScopedValueSetter<bool> s (updating, true); setValue ((double) node.getProperty (ids::vol, 0.8), juce::dontSendNotification); }
    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& i) override { if (t == node && i == ids::vol) refresh(); }
    juce::UndoManager* undo;
    juce::ValueTree node;
    bool updating = false;
};

struct MixerView::Strip : public juce::Component
{
    Strip (MixerView& m, int idx) : mixer (m), index (idx), pan (m.ctx.project.um(), -1, 1, 0, "Pan"), send (m.ctx.project.um(), 0, 1, 0, "Send level"),
                                    fader (m.ctx.project.um())
    {
        node = m.ctx.project.mixerTrack (idx);
        pan.bind (node, ids::pan);
        send.bind (node, ids::sendLevel);
        fader.bind (node);
        for (int i = 0; i < numFxSlots; ++i)
        {
            auto* b = slots.add (new juce::TextButton());
            b->onClick = [this, i] { slotClicked (i); };
            addAndMakeVisible (b);
        }
        addAndMakeVisible (pan);
        addAndMakeVisible (fader);
        addAndMakeVisible (muteBtn);
        addAndMakeVisible (soloBtn);
        if (idx > 0) { addAndMakeVisible (routeBtn); addAndMakeVisible (send); addAndMakeVisible (sendBtn); }
        muteBtn.onClick = [this] { auto& pr = mixer.ctx.project; pr.beginEdit ("Mute"); node.setProperty (ids::mute, ! (bool) node.getProperty (ids::mute), pr.um()); };
        soloBtn.onClick = [this] { auto& pr = mixer.ctx.project; pr.beginEdit ("Solo"); node.setProperty (ids::solo, ! (bool) node.getProperty (ids::solo), pr.um()); };
        routeBtn.onClick = [this] { routeMenu (false); };
        sendBtn.onClick = [this] { routeMenu (true); };
        routeBtn.setTooltip ("Where this track's output goes");
        sendBtn.setTooltip ("Send a copy (e.g. to a reverb track)");
        muteBtn.setColour (juce::TextButton::buttonOnColourId, col::warn);
        soloBtn.setColour (juce::TextButton::buttonOnColourId, col::accent);
        refresh();
    }

    void refresh()
    {
        for (int i = 0; i < numFxSlots; ++i)
        {
            auto s = mixer.ctx.project.getSlot (index, i);
            auto* b = slots[i];
            if (s.isValid())
            {
                juce::String label = s.getProperty (ids::builtin).toString();
                if (s.getProperty (ids::kind).toString() == "plugin")
                {
                    juce::PluginDescription d;
                    if (auto x = juce::XmlDocument::parse (s.getProperty (ids::plugin).toString())) d.loadFromXml (*x);
                    label = d.name;
                }
                bool bypass = (bool) s.getProperty (ids::bypass, false);
                const int scSrc = (int) s.getProperty (ids::sc, -1);
                if (scSrc >= 0) label << "  [SC " << scSrc << "]";
                b->setButtonText (label);
                b->setColour (juce::TextButton::buttonColourId, bypass ? col::panel : col::accent.withAlpha (0.35f));
                b->setColour (juce::TextButton::textColourOffId, bypass ? col::textDim : col::text);
                b->setTooltip (label + (bypass ? " (bypassed)" : "") + " - click to open, right-click for options");
            }
            else
            {
                b->setButtonText ("");
                b->setColour (juce::TextButton::buttonColourId, col::bg);
                b->setTooltip ("Empty slot - click to add an effect");
            }
        }
        muteBtn.setButtonText ("M"); soloBtn.setButtonText ("S");
        muteBtn.setToggleState ((bool) node.getProperty (ids::mute), juce::dontSendNotification);
        soloBtn.setToggleState ((bool) node.getProperty (ids::solo), juce::dontSendNotification);
        int route = (int) node.getProperty (ids::route, 0);
        routeBtn.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92 ")) + (route <= 0 ? juce::String ("Master") : juce::String (route)));
        int st = (int) node.getProperty (ids::sendTo, -1);
        sendBtn.setButtonText (st < 0 ? juce::String ("Send: off") : "Send " + juce::String (st));
        repaint();
    }

    void slotClicked (int i)
    {
        auto& ctx = mixer.ctx;
        auto s = ctx.project.getSlot (index, i);
        auto mods = juce::ModifierKeys::getCurrentModifiers();
        if (! s.isValid())
        {
            showEffectMenu (ctx, slots[i], [this, i] (juce::String kind, juce::String builtin, juce::String xml)
            {
                auto& c = mixer.ctx;
                c.project.beginEdit ("Add effect");
                c.project.setSlot (index, i, kind, builtin, xml);
                juce::Timer::callAfterDelay (100, [&c, idx = index, i] { if (c.openSlotEditor) c.openSlotEditor (idx, i); });
            });
            return;
        }
        if (mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem (1, "Open editor");
            m.addItem (2, "Bypass", true, (bool) s.getProperty (ids::bypass, false));
            m.addItem (3, "Replace...");
            m.addItem (4, "Move up", i > 0);
            m.addItem (5, "Move down", i < numFxSlots - 1);
            juce::PopupMenu scm;
            const int curSc = (int) s.getProperty (ids::sc, -1);
            scm.addItem (2000, "None", true, curSc < 0);
            for (int t = 0; t < numMixerTracks; ++t)
                if (t != index) scm.addItem (2001 + t, juce::String (t) + "  " + ctx.project.mixerTrack (t).getProperty (ids::name).toString(), true, curSc == t);
            m.addSubMenu ("Sidechain input from", scm);
            m.addItem (6, "Remove");
            m.showMenuAsync ({}, [this, i, s] (int r) mutable
            {
                auto& c = mixer.ctx;
                auto& pr = c.project;
                if (r == 1 && c.openSlotEditor) c.openSlotEditor (index, i);
                else if (r == 2) { pr.beginEdit ("Bypass"); s.setProperty (ids::bypass, ! (bool) s.getProperty (ids::bypass), pr.um()); }
                else if (r == 3) showEffectMenu (c, slots[i], [this, i] (juce::String k, juce::String b, juce::String x) { mixer.ctx.project.beginEdit ("Replace effect"); mixer.ctx.project.setSlot (index, i, k, b, x); });
                else if (r == 4 || r == 5)
                {
                    int j = r == 4 ? i - 1 : i + 1;
                    pr.beginEdit ("Move effect");
                    auto other = pr.getSlot (index, j);
                    s.setProperty (ids::index, j, pr.um());
                    if (other.isValid()) other.setProperty (ids::index, i, pr.um());
                }
                else if (r == 6) { pr.beginEdit ("Remove effect"); pr.clearSlot (index, i); }
                else if (r >= 2000)
                {
                    pr.beginEdit ("Sidechain");
                    s.setProperty (ids::sc, r == 2000 ? -1 : r - 2001, pr.um());
                    if (r > 2000) c.status ("Sidechain set. In Gubee Comp / Gate turn on 'Use sidechain'; Gubee Pump: set Trigger to Sidechain.");
                }
            });
            return;
        }
        if (ctx.openSlotEditor) ctx.openSlotEditor (index, i);
    }

    void routeMenu (bool sendMenu)
    {
        auto& pr = mixer.ctx.project;
        juce::PopupMenu m;
        if (sendMenu) m.addItem (1000, "No send");
        m.addItem (1 + 0, "Master");
        for (int t = index + 1; t < numMixerTracks; ++t)
            m.addItem (1 + t, juce::String (t) + "  " + pr.mixerTrack (t).getProperty (ids::name).toString());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sendMenu ? &sendBtn : &routeBtn), [this, sendMenu] (int r)
        {
            if (r == 0) return;
            auto& p = mixer.ctx.project;
            p.beginEdit ("Routing");
            if (sendMenu) { node.setProperty (ids::sendTo, r == 1000 ? -1 : r - 1, p.um()); if (r != 1000 && (float) node.getProperty (ids::sendLevel, 0.0f) <= 0) node.setProperty (ids::sendLevel, 0.5f, p.um()); }
            else node.setProperty (ids::route, r - 1, p.um());
        });
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (index == 0 ? col::panelHi : col::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2), 4);
        g.setColour (col::text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (index == 0 ? juce::String ("M") : juce::String (index), 0, 4, getWidth(), 14, juce::Justification::centred);
        g.setFont (juce::FontOptions (11.0f));
        g.setColour (col::textDim);
        g.drawText (node.getProperty (ids::name).toString(), 4, 18, getWidth() - 8, 14, juce::Justification::centred, true);
        // meter
        auto m = meterArea.toFloat();
        g.setColour (col::bg); g.fillRect (m);
        auto drawBar = [&] (juce::Rectangle<float> r, float v)
        {
            float db = juce::Decibels::gainToDecibels (v, -60.0f);
            float h = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f) * r.getHeight();
            g.setColour (v > 0.99f ? juce::Colour (0xffe05050) : v > 0.7f ? col::warn : col::accent);
            g.fillRect (r.removeFromBottom (h));
        };
        drawBar (m.removeFromLeft (m.getWidth() / 2).reduced (1, 0), peakL);
        drawBar (m.reduced (1, 0), peakR);
        if (index > 0)
        {
            g.setColour (col::textDim);
            g.setFont (juce::FontOptions (9.0f));
            g.drawText ("SEND", send.getBounds().translated (0, -10).withHeight (10), juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (5);
        r.removeFromTop (30);
        for (auto* s : slots) { s->setBounds (r.removeFromTop (20)); r.removeFromTop (2); }
        r.removeFromTop (4);
        if (index > 0)
        {
            auto row = r.removeFromTop (34);
            r.removeFromTop (2);
            send.setBounds (row.removeFromLeft (30).withTrimmedTop (8));
            sendBtn.setBounds (row.withTrimmedTop (8).reduced (2, 4));
        }
        auto bottom = r.removeFromBottom (22);
        muteBtn.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).reduced (1, 0));
        soloBtn.setBounds (bottom.reduced (1, 0));
        r.removeFromBottom (2);
        if (index > 0) { routeBtn.setBounds (r.removeFromBottom (20)); r.removeFromBottom (2); }
        pan.setBounds (r.removeFromTop (30));
        meterArea = r.removeFromRight (16);
        fader.setBounds (r);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.y < 32)
            askForText ("Rename mixer track", node.getProperty (ids::name), [this] (juce::String s)
            { mixer.ctx.project.beginEdit ("Rename"); node.setProperty (ids::name, s, mixer.ctx.project.um()); });
    }

    MixerView& mixer;
    int index;
    juce::ValueTree node;
    juce::OwnedArray<juce::TextButton> slots;
    TreeKnob pan, send;
    TreeFader fader;
    juce::TextButton muteBtn, soloBtn, routeBtn, sendBtn;
    juce::Rectangle<int> meterArea;
    float peakL = 0, peakR = 0;
};

//==============================================================================
MixerView::MixerView (AppContext& c) : ctx (c)
{
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    ctx.project.root.addListener (this);
    rebuild();
    startTimerHz (25);
}

MixerView::~MixerView() { ctx.project.root.removeListener (this); }

void MixerView::rebuild()
{
    strips.clear();
    for (int i = 0; i < numMixerTracks; ++i)
        content.addAndMakeVisible (strips.add (new Strip (*this, i)));
    resized();
}

void MixerView::handleAsyncUpdate() { for (auto* s : strips) s->refresh(); }

void MixerView::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier&)
{
    if (t.hasType (ids::MTRACK) || t.hasType (ids::SLOT)) triggerAsyncUpdate();
}
void MixerView::valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree&) { if (p.hasType (ids::MTRACK)) triggerAsyncUpdate(); }
void MixerView::valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree&, int) { if (p.hasType (ids::MTRACK)) triggerAsyncUpdate(); }

void MixerView::timerCallback()
{
    for (auto* s : strips)
        if (auto t = ctx.engine->trackRuntime (s->index))
        {
            s->peakL = t->peakL.load(); s->peakR = t->peakR.load();
            s->repaint (s->meterArea);
        }
}

void MixerView::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    drawViewHeader (g, getLocalBounds().removeFromTop (36), "Mixer", "Click an empty slot to add an effect  -  right-click a slot for bypass, sidechain, move or remove");
}

void MixerView::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (36);
    viewport.setBounds (r);
    const int h = juce::jmax (420, r.getHeight() - viewport.getScrollBarThickness() - 2);
    content.setSize (numMixerTracks * stripW + 8, h);
    for (int i = 0; i < strips.size(); ++i) strips[i]->setBounds (4 + i * stripW, 0, stripW, h);
}
}
