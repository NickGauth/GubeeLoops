#include "PianoRoll.h"

namespace gubee
{
static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static bool isBlack (int k) { int n = k % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; }
static const int scaleIntervals[][7] = {
    { 0, 2, 4, 5, 7, 9, 11 },   // major
    { 0, 2, 3, 5, 7, 8, 10 },   // natural minor
    { 0, 2, 3, 5, 7, 8, 11 },   // harmonic minor
    { 0, 2, 3, 5, 7, 9, 10 },   // dorian
    { 0, 1, 3, 5, 7, 8, 10 },   // phrygian
    { 0, 2, 4, 7, 9, -1, -1 },  // major pentatonic
    { 0, 3, 5, 7, 10, -1, -1 }, // minor pentatonic
};

//==============================================================================
struct PianoRoll::Grid : public juce::Component
{
    explicit Grid (PianoRoll& o) : roll (o) { setWantsKeyboardFocus (false); }
    PianoRoll& roll;

    double beatAt (int x) const { return x / roll.pxPerBeat; }
    int keyAt (int y) const { return 127 - y / roll.rowH; }
    juce::Rectangle<float> noteRect (const juce::ValueTree& n) const
    {
        double st = n.getProperty (ids::start), len = n.getProperty (ids::len);
        int k = n.getProperty (ids::key);
        return { (float) (st * roll.pxPerBeat), (float) ((127 - k) * roll.rowH), (float) juce::jmax (3.0, len * roll.pxPerBeat), (float) roll.rowH };
    }

    void paint (juce::Graphics& g) override
    {
        auto clip = g.getClipBounds();
        const int k0 = juce::jlimit (0, 127, keyAt (clip.getBottom())), k1 = juce::jlimit (0, 127, keyAt (clip.getY()));
        for (int k = k0; k <= k1; ++k)
        {
            juce::Rectangle<int> r (clip.getX(), (127 - k) * roll.rowH, clip.getWidth(), roll.rowH);
            juce::Colour c = isBlack (k) ? col::bg.darker (0.15f) : col::bg.brighter (0.04f);
            if (roll.scaleType.getSelectedId() > 1 && roll.inScale (k)) c = c.interpolatedWith (col::accent, 0.08f);
            g.setColour (c);
            g.fillRect (r);
            if (k % 12 == 0) { g.setColour (col::outline); g.drawHorizontalLine (r.getBottom() - 1, (float) r.getX(), (float) r.getRight()); }
        }
        const double plen = roll.ctx.project.patternLengthBeats (roll.ctx.currentPattern());
        const double sn = roll.snap() > 0 ? roll.snap() : 0.25;
        for (double b = std::floor (clip.getX() / roll.pxPerBeat / sn) * sn; b * roll.pxPerBeat < clip.getRight(); b += sn)
        {
            float x = (float) (b * roll.pxPerBeat);
            bool bar = std::fmod (b + 1.0e-9, 4.0) < 1.0e-6, beat = std::fmod (b + 1.0e-9, 1.0) < 1.0e-6;
            g.setColour (bar ? col::catGray.withAlpha (0.6f) : beat ? col::outline.brighter (0.2f) : col::outline.withAlpha (0.5f));
            g.drawVerticalLine ((int) x, (float) clip.getY(), (float) clip.getBottom());
        }
        // end of pattern
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> ((float) (plen * roll.pxPerBeat), (float) clip.getY(), (float) clip.getRight(), (float) clip.getHeight()));

        // ghost notes of other channels
        const int sel = roll.ctx.selectedChannel();
        auto pat = roll.pattern();
        for (auto n : pat)
            if ((int) n.getProperty (ids::ch) != sel)
            {
                g.setColour (col::catGray.withAlpha (0.18f));
                g.fillRect (noteRect (n).reduced (0, 1));
            }
        auto colour = col::fromIndex ((int) roll.ctx.project.findChannel (sel).getProperty (ids::colour, 0));
        for (auto n : pat)
        {
            if ((int) n.getProperty (ids::ch) != sel) continue;
            auto r = noteRect (n).reduced (0.5f, 1.0f);
            float v = (float) n.getProperty (ids::vel, 0.78f);
            bool selected = roll.selection.contains (n);
            g.setColour (colour.withMultipliedBrightness (0.6f + v * 0.5f));
            g.fillRoundedRectangle (r, 2.5f);
            g.setColour (selected ? juce::Colours::white : colour.darker (0.6f));
            g.drawRoundedRectangle (r, 2.5f, selected ? 1.6f : 1.0f);
            if (r.getWidth() > 28 && roll.rowH >= 12)
            {
                g.setColour (col::bg);
                g.setFont (juce::FontOptions (10.0f));
                g.drawText (roll.keyLabel ((int) n.getProperty (ids::key)), r.reduced (3, 0), juce::Justification::centredLeft, false);
            }
        }
        if (! lasso.isEmpty())
        {
            g.setColour (col::accent.withAlpha (0.15f)); g.fillRect (lasso);
            g.setColour (col::accent); g.drawRect (lasso);
        }
        // playhead
        if (roll.ctx.engine->isPlaying() && roll.ctx.engine->getMode() == TransportMode::pattern)
        {
            g.setColour (col::accent2);
            g.drawVerticalLine ((int) (roll.ctx.engine->getBeat() * roll.pxPerBeat), (float) clip.getY(), (float) clip.getBottom());
        }
    }

    juce::ValueTree noteAt (juce::Point<int> p) const
    {
        const int sel = roll.ctx.selectedChannel();
        auto pat = roll.pattern();
        for (int i = pat.getNumChildren(); --i >= 0;)
        {
            auto n = pat.getChild (i);
            if ((int) n.getProperty (ids::ch) == sel && noteRect (n).contains (p.toFloat())) return n;
        }
        return {};
    }

    double snapBeat (double b) const
    {
        double s = roll.snap();
        return s > 0 ? std::floor (b / s + 1.0e-9) * s : b;
    }

    void audition (int key)
    {
        roll.ctx.engine->sendLiveNote (key, 0.8f);
        juce::Timer::callAfterDelay (180, [e = roll.ctx.engine.get(), key] { e->sendLiveNote (key, 0.0f); });
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        roll.grabKeyboardFocus();
        auto& pr = roll.ctx.project;
        if (roll.ctx.selectedChannel() < 0) return;
        auto n = noteAt (e.getPosition());
        if (e.mods.isPopupMenu())
        {
            pr.beginEdit ("Delete note");
            if (n.isValid()) n.getParent().removeChild (n, pr.um());
            mode = erasing;
            return;
        }
        if (e.mods.isCtrlDown() && ! n.isValid())
        {
            mode = lassoing; lassoStart = e.getPosition(); lasso = {};
            if (! e.mods.isShiftDown()) roll.selection.clear();
            return;
        }
        pr.beginEdit ("Edit notes");
        if (n.isValid())
        {
            if (! roll.selection.contains (n)) { if (! e.mods.isShiftDown()) roll.selection.clear(); roll.selection.add (n); }
            auto r = noteRect (n);
            mode = (e.x > r.getRight() - 6) ? resizing : moving;
            dragNote = n;
            startBeat = n.getProperty (ids::start); startKey = n.getProperty (ids::key); startLen = n.getProperty (ids::len);
            origins.clear();
            for (auto& s : roll.selection) origins.push_back ({ s, (double) s.getProperty (ids::start), (int) s.getProperty (ids::key), (double) s.getProperty (ids::len) });
            if (mode == moving) audition (startKey);
        }
        else
        {
            roll.selection.clear();
            const double b = snapBeat (beatAt (e.x));
            const int key = keyAt (e.y);
            auto chord = roll.chordFor (key);
            juce::ValueTree first;
            for (int k : chord)
            {
                auto nn = pr.addNote (roll.ctx.currentPattern(), roll.ctx.selectedChannel(), b, roll.lastLength, k, 0.78f);
                if (! first.isValid()) first = nn;
                roll.selection.add (nn);
                audition (k);
            }
            dragNote = first;
            mode = moving;
            startBeat = b; startKey = key; startLen = roll.lastLength;
            origins.clear();
            for (auto& s : roll.selection) origins.push_back ({ s, (double) s.getProperty (ids::start), (int) s.getProperty (ids::key), (double) s.getProperty (ids::len) });
        }
        roll.repaintAll();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& pr = roll.ctx.project;
        if (mode == erasing)
        {
            auto n = noteAt (e.getPosition());
            if (n.isValid()) n.getParent().removeChild (n, pr.um());
            return;
        }
        if (mode == lassoing)
        {
            lasso = juce::Rectangle<int> (lassoStart, e.getPosition());
            roll.selection.clear();
            const int sel = roll.ctx.selectedChannel();
            for (auto n : roll.pattern())
                if ((int) n.getProperty (ids::ch) == sel && noteRect (n).intersects (lasso.toFloat())) roll.selection.add (n);
            repaint();
            return;
        }
        if (! dragNote.isValid()) return;
        const double s = roll.snap();
        if (mode == moving)
        {
            double db = beatAt (e.getDistanceFromDragStartX());
            if (s > 0) db = std::round (db / s) * s;
            int dk = -(int) std::round ((double) e.getDistanceFromDragStartY() / roll.rowH);
            for (auto& o : origins)
            {
                o.node.setProperty (ids::start, juce::jmax (0.0, o.start + db), pr.um());
                int nk = juce::jlimit (0, 127, o.key + dk);
                if ((int) o.node.getProperty (ids::key) != nk) { o.node.setProperty (ids::key, nk, pr.um()); if (&o == &origins.front()) audition (nk); }
            }
        }
        else if (mode == resizing)
        {
            double d = beatAt (e.getDistanceFromDragStartX());
            for (auto& o : origins)
            {
                double nl = o.len + d;
                if (s > 0) nl = juce::jmax (s, std::round (nl / s) * s);
                nl = juce::jmax (1.0 / 64.0, nl);
                o.node.setProperty (ids::len, nl, pr.um());
                roll.lastLength = nl;
            }
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (mode == moving && dragNote.isValid()) roll.lastLength = (double) dragNote.getProperty (ids::len);
        // grow pattern if notes moved past the end
        auto p = roll.pattern();
        double end = 0;
        for (auto n : p) end = juce::jmax (end, (double) n.getProperty (ids::start) + (double) n.getProperty (ids::len));
        if (end > (double) p.getProperty (ids::length, 4.0) + 1.0e-6) p.setProperty (ids::length, std::ceil (end / 4.0) * 4.0, roll.ctx.project.um());
        mode = none; lasso = {}; dragNote = {};
        repaint();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        auto n = noteAt (e.getPosition());
        setMouseCursor (n.isValid() && e.x > noteRect (n).getRight() - 6 ? juce::MouseCursor::LeftRightResizeCursor
                        : n.isValid() ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCtrlDown())
        {
            roll.pxPerBeat = juce::jlimit (20.0, 600.0, roll.pxPerBeat * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15));
            roll.resized();
        }
        else Component::mouseWheelMove (e, w);
    }

    enum Mode { none, moving, resizing, erasing, lassoing } mode = none;
    struct Origin { juce::ValueTree node; double start; int key; double len; };
    std::vector<Origin> origins;
    juce::ValueTree dragNote;
    double startBeat = 0, startLen = 0.25; int startKey = 60;
    juce::Point<int> lassoStart;
    juce::Rectangle<int> lasso;
};

//==============================================================================
struct PianoRoll::Keys : public juce::Component
{
    explicit Keys (PianoRoll& o) : roll (o) {}
    PianoRoll& roll;
    int offsetY = 0;
    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::panel);
        for (int k = 0; k < 128; ++k)
        {
            int y = (127 - k) * roll.rowH - offsetY;
            if (y > getHeight() || y + roll.rowH < 0) continue;
            juce::Rectangle<int> r (0, y, getWidth(), roll.rowH);
            g.setColour (isBlack (k) ? juce::Colour (0xff202226) : juce::Colour (0xffd8dadd));
            g.fillRect (isBlack (k) ? r.withWidth (getWidth() * 2 / 3) : r);
            g.setColour (col::outline);
            g.drawHorizontalLine (r.getBottom() - 1, 0, (float) getWidth());
            if (k == pressed) { g.setColour (col::accent.withAlpha (0.6f)); g.fillRect (r); }
            if (roll.rowH >= 10 && (k % 12 == 0 || roll.keyLabel (k) != juce::String (noteNames[k % 12]) + juce::String (k / 12 - 1)))
            {
                g.setColour (isBlack (k) ? col::text : col::bg);
                g.setFont (juce::FontOptions (juce::jmin (11.0f, roll.rowH - 2.0f)));
                g.drawText (roll.keyLabel (k), r.reduced (3, 0), juce::Justification::centredRight, true);
            }
        }
    }
    int keyAt (int y) const { return juce::jlimit (0, 127, 127 - (y + offsetY) / roll.rowH); }
    void mouseDown (const juce::MouseEvent& e) override { pressed = keyAt (e.y); roll.ctx.engine->sendLiveNote (pressed, 0.8f); repaint(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        int k = keyAt (e.y);
        if (k != pressed) { roll.ctx.engine->sendLiveNote (pressed, 0); pressed = k; roll.ctx.engine->sendLiveNote (k, 0.8f); repaint(); }
    }
    void mouseUp (const juce::MouseEvent&) override { roll.ctx.engine->sendLiveNote (pressed, 0); pressed = -1; repaint(); }
    int pressed = -1;
};

struct PianoRoll::Ruler : public juce::Component
{
    explicit Ruler (PianoRoll& o) : roll (o) {}
    PianoRoll& roll;
    int offsetX = 0;
    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::panel);
        g.setColour (col::textDim);
        g.setFont (juce::FontOptions (11.0f));
        for (int b = 0; b * roll.pxPerBeat - offsetX < getWidth(); ++b)
        {
            int x = (int) (b * roll.pxPerBeat) - offsetX;
            if (x < -40) continue;
            if (b % 4 == 0)
            {
                g.setColour (col::text);
                g.drawText (juce::String (b / 4 + 1), x + 3, 0, 40, getHeight(), juce::Justification::centredLeft);
                g.drawVerticalLine (x, 0, (float) getHeight());
            }
            else { g.setColour (col::outline); g.drawVerticalLine (x, getHeight() * 0.6f, (float) getHeight()); }
        }
    }
};

struct PianoRoll::VelLane : public juce::Component
{
    explicit VelLane (PianoRoll& o) : roll (o) {}
    PianoRoll& roll;
    int offsetX = 0;
    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::panel.darker (0.2f));
        g.setColour (col::textDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("VELOCITY", 4, 2, 80, 12, juce::Justification::left);
        const int sel = roll.ctx.selectedChannel();
        auto colour = col::fromIndex ((int) roll.ctx.project.findChannel (sel).getProperty (ids::colour, 0));
        for (auto n : roll.pattern())
        {
            if ((int) n.getProperty (ids::ch) != sel) continue;
            int x = (int) ((double) n.getProperty (ids::start) * roll.pxPerBeat) - offsetX;
            float v = (float) n.getProperty (ids::vel, 0.78f);
            int h = (int) (v * (getHeight() - 6));
            g.setColour (roll.selection.contains (n) ? juce::Colours::white : colour);
            g.fillRect (x, getHeight() - h, 3, h);
            g.fillEllipse ((float) x - 2, (float) (getHeight() - h - 3), 7, 7);
        }
    }
    void setVel (const juce::MouseEvent& e)
    {
        const int sel = roll.ctx.selectedChannel();
        float v = juce::jlimit (0.02f, 1.0f, 1.0f - (e.y - 3) / (float) (getHeight() - 6));
        for (auto n : roll.pattern())
        {
            if ((int) n.getProperty (ids::ch) != sel) continue;
            int x = (int) ((double) n.getProperty (ids::start) * roll.pxPerBeat) - offsetX;
            if (std::abs (x - e.x) <= 4 && (roll.selection.isEmpty() || roll.selection.contains (n) || ! e.mods.isShiftDown()))
                n.setProperty (ids::vel, v, roll.ctx.project.um());
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { roll.ctx.project.beginEdit ("Velocity"); setVel (e); }
    void mouseDrag (const juce::MouseEvent& e) override { setVel (e); }
};

//==============================================================================
PianoRoll::PianoRoll (AppContext& c) : ctx (c)
{
    grid = std::make_unique<Grid> (*this);
    keys = std::make_unique<Keys> (*this);
    ruler = std::make_unique<Ruler> (*this);
    vel = std::make_unique<VelLane> (*this);
    viewport.setViewedComponent (grid.get(), false);
    viewport.setScrollBarsShown (true, true);
    viewport.onMove = [this]
    {
        keys->offsetY = viewport.getViewPositionY(); keys->repaint();
        ruler->offsetX = viewport.getViewPositionX(); ruler->repaint();
        vel->offsetX = viewport.getViewPositionX(); vel->repaint();
    };
    addAndMakeVisible (viewport);
    addAndMakeVisible (*keys);
    addAndMakeVisible (*ruler);
    addAndMakeVisible (*vel);

    addAndMakeVisible (snapBox);
    snapBox.addItemList ({ "Snap: Bar", "Snap: Beat", "Snap: 1/8", "Snap: 1/16 (step)", "Snap: 1/32", "Snap: 1/8 T", "Snap: 1/16 T", "Snap: None" }, 1);
    snapBox.setSelectedId (4);
    addAndMakeVisible (scaleRoot);
    for (int i = 0; i < 12; ++i) scaleRoot.addItem (noteNames[i], i + 1);
    scaleRoot.setSelectedId (1);
    scaleRoot.onChange = [this] { repaintAll(); };
    addAndMakeVisible (scaleType);
    scaleType.addItemList ({ "No scale", "Major", "Minor", "Harmonic Minor", "Dorian", "Phrygian", "Major Pentatonic", "Minor Pentatonic" }, 1);
    scaleType.setSelectedId (1);
    scaleType.setTooltip ("Highlights the notes of a key, and makes chord stamps follow it");
    scaleType.onChange = [this] { repaintAll(); };
    addAndMakeVisible (chordBox);
    chordBox.addItemList ({ "Chord: off", "Chord: in key (triad)", "Chord: in key (7th)", "Major", "Minor", "7", "Maj7", "Min7", "Sus2", "Sus4", "Power (5)", "Octave" }, 1);
    chordBox.setSelectedId (1);
    chordBox.setTooltip ("Chord stamp: clicking places a whole chord");
    addAndMakeVisible (quantBtn);
    quantBtn.setTooltip ("Snap selected notes (or all) to the grid (Ctrl+Q)");
    quantBtn.onClick = [this] { quantizeSelectedOrAll(); };
    addAndMakeVisible (progBtn);
    progBtn.setTooltip ("Insert a chord progression in the chosen key");
    progBtn.onClick = [this]
    {
        juce::PopupMenu m;
        const char* names[] = { "I - V - vi - IV  (pop)", "vi - IV - I - V  (emotional)", "ii - V - I  (jazz)", "i - VI - III - VII  (epic minor)",
                                "i - iv - v - i  (dark)", "i - VII - VI - VII  (trap)", "ii7 - V7 - Imaj7 - vi7  (lo-fi)", "I - IV - V - IV  (rock)" };
        for (int i = 0; i < 8; ++i) m.addItem (i + 1, names[i]);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&progBtn), [this] (int r) { if (r > 0) insertProgression (r - 1); });
    };
    for (auto* b : { &zoomIn, &zoomOut }) addAndMakeVisible (*b);
    zoomIn.onClick = [this] { pxPerBeat = juce::jmin (600.0, pxPerBeat * 1.25); resized(); };
    zoomOut.onClick = [this] { pxPerBeat = juce::jmax (20.0, pxPerBeat / 1.25); resized(); };

    setWantsKeyboardFocus (true);
    ctx.project.root.addListener (this);
    ctx.addChangeListener (this);
    startTimerHz (30);
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<PianoRoll> (this)]
    { if (sp) sp->viewport.setViewPosition (0, (127 - 72) * sp->rowH); });
}

PianoRoll::~PianoRoll()
{
    ctx.project.root.removeListener (this);
    ctx.removeChangeListener (this);
}

double PianoRoll::snap() const
{
    const double v[] = { 4.0, 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 0.0 };
    return v[juce::jlimit (0, 7, snapBox.getSelectedItemIndex())];
}

bool PianoRoll::inScale (int key) const
{
    int t = scaleType.getSelectedId() - 2;
    if (t < 0) return true;
    int rel = ((key - (scaleRoot.getSelectedId() - 1)) % 12 + 12) % 12;
    for (int i = 0; i < 7; ++i) if (scaleIntervals[t][i] == rel) return true;
    return false;
}

std::vector<int> PianoRoll::chordFor (int root) const
{
    int c = chordBox.getSelectedId();
    switch (c)
    {
        case 2: case 3:
        {
            // stack thirds inside the chosen scale (defaults to C major)
            int t = juce::jmax (0, scaleType.getSelectedId() - 2);
            std::vector<int> scale;
            for (int i = 0; i < 7; ++i) if (scaleIntervals[t][i] >= 0) scale.push_back (scaleIntervals[t][i]);
            const int tonic = scaleRoot.getSelectedId() - 1;
            int rel = ((root - tonic) % 12 + 12) % 12;
            int deg = 0;
            for (size_t i = 0; i < scale.size(); ++i) if (scale[i] <= rel) deg = (int) i;
            const int base = root - (rel - scale[(size_t) deg]);
            std::vector<int> out;
            const int n = (int) scale.size();
            for (int k = 0; k < (c == 3 ? 4 : 3); ++k)
            {
                int d = deg + k * 2;
                out.push_back (base - scale[(size_t) deg] + scale[(size_t) (d % n)] + 12 * (d / n));
            }
            return out;
        }
        case 4: return { root, root + 4, root + 7 };
        case 5: return { root, root + 3, root + 7 };
        case 6: return { root, root + 4, root + 7, root + 10 };
        case 7: return { root, root + 4, root + 7, root + 11 };
        case 8: return { root, root + 3, root + 7, root + 10 };
        case 9: return { root, root + 2, root + 7 };
        case 10: return { root, root + 5, root + 7 };
        case 11: return { root, root + 7, root + 12 };
        case 12: return { root, root + 12 };
        default: return { root };
    }
}

juce::String PianoRoll::keyLabel (int key) const
{
    auto ch = ctx.project.findChannel (ctx.selectedChannel());
    if (ch.getProperty (ids::kind).toString() == "pads")
    {
        int pad = key - DrumPadsProcessor::baseNote;
        auto p = ctx.project.getPad (ctx.selectedChannel(), pad);
        if (p.isValid()) return p.getProperty (ids::name).toString();
    }
    return juce::String (noteNames[key % 12]) + juce::String (key / 12 - 1);
}

std::vector<juce::ValueTree> PianoRoll::channelNotes() const
{
    std::vector<juce::ValueTree> v;
    for (auto n : pattern()) if ((int) n.getProperty (ids::ch) == ctx.selectedChannel()) v.push_back (n);
    return v;
}

void PianoRoll::quantizeSelectedOrAll()
{
    const double s = snap() > 0 ? snap() : 0.25;
    ctx.project.beginEdit ("Quantize");
    auto notes = selection.isEmpty() ? channelNotes() : std::vector<juce::ValueTree> (selection.begin(), selection.end());
    for (auto& n : notes)
    {
        n.setProperty (ids::start, std::round ((double) n.getProperty (ids::start) / s) * s, ctx.project.um());
        n.setProperty (ids::len, juce::jmax (s, std::round ((double) n.getProperty (ids::len) / s) * s), ctx.project.um());
    }
    ctx.status ("Quantized " + juce::String ((int) notes.size()) + " notes");
}

void PianoRoll::insertProgression (int which)
{
    // degrees (0-based scale degrees), with 7th flags
    static const std::vector<std::vector<int>> progs = {
        { 0, 4, 5, 3 }, { 5, 3, 0, 4 }, { 1, 4, 0, 0 }, { 0, 5, 2, 6 }, { 0, 3, 4, 0 }, { 0, 6, 5, 6 }, { 1, 4, 0, 5 }, { 0, 3, 4, 3 } };
    const bool minorProg = which == 3 || which == 4 || which == 5;
    const bool sevenths = which == 6;
    if (ctx.selectedChannel() < 0) return;
    // pick a scale: keep the user's root, use major / minor to suit the progression
    const int tonic = scaleRoot.getSelectedId() - 1;
    const int* sc = scaleIntervals[minorProg ? 1 : 0];
    if (scaleType.getSelectedId() == 1) scaleType.setSelectedId (minorProg ? 3 : 2);
    ctx.project.beginEdit ("Chord progression");
    const int pat = ctx.currentPattern();
    auto p = pattern();
    const double startBar = 0.0;
    const auto& prog = progs[(size_t) which];
    for (size_t i = 0; i < prog.size(); ++i)
    {
        int deg = prog[i];
        for (int k = 0; k < (sevenths ? 4 : 3); ++k)
        {
            int d = deg + k * 2;
            int key = 48 + tonic + sc[d % 7] + 12 * (d / 7);
            ctx.project.addNote (pat, ctx.selectedChannel(), startBar + i * 4.0, 4.0, key, 0.7f);
        }
        ctx.project.addNote (pat, ctx.selectedChannel(), startBar + i * 4.0, 4.0, 36 + tonic + sc[deg % 7], 0.7f);   // bass note
    }
    p.setProperty (ids::length, juce::jmax ((double) p.getProperty (ids::length, 4.0), 16.0), ctx.project.um());
    ctx.status ("Inserted a 4-bar chord progression");
}

void PianoRoll::repaintAll()
{
    grid->repaint(); keys->repaint(); vel->repaint(); ruler->repaint();
}

void PianoRoll::updateSize()
{
    const double len = ctx.project.patternLengthBeats (ctx.currentPattern());
    grid->setSize ((int) ((len + 8.0) * pxPerBeat), 128 * rowH);
}

void PianoRoll::timerCallback()
{
    if (ctx.engine->isPlaying()) grid->repaint();
}

void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    auto head = getLocalBounds().removeFromTop (36);
    g.setColour (col::panel);
    g.fillRect (head);
    g.setColour (col::text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    auto ch = ctx.project.findChannel (ctx.selectedChannel());
    g.drawText ("Piano Roll  -  " + (ch.isValid() ? ch.getProperty (ids::name).toString() : juce::String ("(select a channel)")),
                head.reduced (10, 0), juce::Justification::centredLeft);
}

void PianoRoll::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (36).reduced (6, 5);
    zoomIn.setBounds (head.removeFromRight (26)); zoomOut.setBounds (head.removeFromRight (26)); head.removeFromRight (6);
    quantBtn.setBounds (head.removeFromRight (74)); head.removeFromRight (4);
    progBtn.setBounds (head.removeFromRight (140)); head.removeFromRight (4);
    chordBox.setBounds (head.removeFromRight (150)); head.removeFromRight (4);
    scaleType.setBounds (head.removeFromRight (140)); head.removeFromRight (2);
    scaleRoot.setBounds (head.removeFromRight (56)); head.removeFromRight (4);
    snapBox.setBounds (head.removeFromRight (140));

    auto velArea = r.removeFromBottom (70);
    auto rulerArea = r.removeFromTop (20);
    keys->setBounds (r.removeFromLeft (64));
    ruler->setBounds (rulerArea.withTrimmedLeft (64));
    vel->setBounds (velArea.withTrimmedLeft (64).withTrimmedRight (viewport.getScrollBarThickness()));
    viewport.setBounds (r);
    updateSize();
    viewport.onMove();
}

bool PianoRoll::keyPressed (const juce::KeyPress& k)
{
    auto& pr = ctx.project;
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        pr.beginEdit ("Delete notes");
        for (auto& n : selection) n.getParent().removeChild (n, pr.um());
        selection.clear();
        return true;
    }
    if (k == juce::KeyPress ('a', juce::ModifierKeys::ctrlModifier, 0))
    {
        selection.clear();
        for (auto& n : channelNotes()) selection.add (n);
        repaintAll();
        return true;
    }
    if (k == juce::KeyPress ('q', juce::ModifierKeys::ctrlModifier, 0)) { quantizeSelectedOrAll(); return true; }
    const int kc = k.getKeyCode();
    if (! selection.isEmpty() && (kc == juce::KeyPress::upKey || kc == juce::KeyPress::downKey))
    {
        int d = (kc == juce::KeyPress::upKey ? 1 : -1) * (k.getModifiers().isShiftDown() ? 12 : 1);
        pr.beginEdit ("Transpose");
        for (auto& n : selection) n.setProperty (ids::key, juce::jlimit (0, 127, (int) n.getProperty (ids::key) + d), pr.um());
        return true;
    }
    if (! selection.isEmpty() && (kc == juce::KeyPress::leftKey || kc == juce::KeyPress::rightKey))
    {
        double d = (kc == juce::KeyPress::rightKey ? 1 : -1) * (snap() > 0 ? snap() : 0.25);
        pr.beginEdit ("Nudge");
        for (auto& n : selection) n.setProperty (ids::start, juce::jmax (0.0, (double) n.getProperty (ids::start) + d), pr.um());
        return true;
    }
    return false;
}
}
