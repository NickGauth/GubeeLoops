#include "Playlist.h"
#include "BrowserPanel.h"
#include "WaveCache.h"
#include "Targets.h"

namespace gubee
{
static constexpr int titleH = 14;

static double srcSecPerBeat (const Project& pr, const juce::ValueTree& c)
{
    const bool st = (bool) c.getProperty (ids::stretch, false);
    const double bpm = c.getProperty (ids::srcBpm, 0.0);
    return 60.0 / ((st && bpm > 0) ? bpm : pr.tempo());
}

//==============================================================================
struct Playlist::Lane : public juce::Component
{
    explicit Lane (Playlist& o) : pl (o) {}
    Playlist& pl;

    juce::Rectangle<float> clipRect (const juce::ValueTree& c) const
    {
        return { (float) ((double) c.getProperty (ids::start) * pl.pxPerBeat), (float) ((int) c.getProperty (ids::track) * pl.trackH + 1),
                 (float) juce::jmax (4.0, (double) c.getProperty (ids::len) * pl.pxPerBeat), (float) (pl.trackH - 2) };
    }

    void paint (juce::Graphics& g) override
    {
        auto& pr = pl.ctx.project;
        auto clip = g.getClipBounds();
        for (int t = 0; t < numPlaylistTracks; ++t)
        {
            g.setColour (t % 2 ? col::bg : col::bg.brighter (0.03f));
            g.fillRect (clip.getX(), t * pl.trackH, clip.getWidth(), pl.trackH);
        }
        const double sn = pl.snap() > 0 ? pl.snap() : 1.0;
        const double gridStep = juce::jmax (sn, pl.pxPerBeat < 10 ? 4.0 : 1.0);
        for (double b = std::floor (clip.getX() / pl.pxPerBeat / gridStep) * gridStep; b * pl.pxPerBeat < clip.getRight(); b += gridStep)
        {
            bool bar = std::fmod (b + 1.0e-9, 4.0) < 1.0e-6;
            g.setColour (bar ? col::outline.brighter (0.25f) : col::outline.withAlpha (0.45f));
            g.drawVerticalLine ((int) (b * pl.pxPerBeat), (float) clip.getY(), (float) clip.getBottom());
        }
        const double ls = pr.root.getProperty (ids::loopStart, 0.0), le = pr.root.getProperty (ids::loopEnd, 0.0);
        if (le > ls)
        {
            g.setColour (col::accent.withAlpha (0.06f));
            g.fillRect (juce::Rectangle<float> ((float) (ls * pl.pxPerBeat), 0, (float) ((le - ls) * pl.pxPerBeat), (float) getHeight()));
        }

        for (auto c : pr.playlist())
        {
            if (! c.hasType (ids::CLIP)) continue;
            auto r = clipRect (c);
            if (! r.intersects (clip.toFloat())) continue;
            drawClip (g, c, r);
        }
        if (! lasso.isEmpty()) { g.setColour (col::accent.withAlpha (0.15f)); g.fillRect (lasso); g.setColour (col::accent); g.drawRect (lasso); }

        if (pl.ctx.engine->getMode() == TransportMode::song)
        {
            g.setColour (col::accent2);
            float x = (float) (pl.ctx.engine->getBeat() * pl.pxPerBeat);
            g.drawLine (x, (float) clip.getY(), x, (float) clip.getBottom(), 1.5f);
        }
    }

    void drawClip (juce::Graphics& g, const juce::ValueTree& c, juce::Rectangle<float> r)
    {
        auto& pr = pl.ctx.project;
        const auto kind = c.getProperty (ids::kind).toString();
        const bool selected = pl.selection.contains (c);
        const bool clipMuted = (bool) c.getProperty (ids::mute, false);
        juce::Graphics::ScopedSaveState clipState (g);
        if (clipMuted) g.setOpacity (0.35f);
        juce::Colour colour;
        juce::String title;
        if (kind == "pattern")
        {
            auto p = pr.findPattern ((int) c.getProperty (ids::pattern));
            colour = col::fromIndex ((int) p.getProperty (ids::colour, 0));
            title = p.getProperty (ids::name).toString();
        }
        else if (kind == "audio") { colour = juce::Colour (0xff5fa8c4); title = c.getProperty (ids::name).toString(); }
        else { colour = juce::Colour (0xffb08ae0); title = c.getProperty (ids::name).toString(); }

        g.setColour (colour.withAlpha (0.28f));
        g.fillRoundedRectangle (r, 3);
        g.setColour (colour.withAlpha (0.85f));
        g.fillRoundedRectangle (r.withHeight ((float) titleH), 3);
        g.setColour (selected ? juce::Colours::white : colour);
        g.drawRoundedRectangle (r, 3, selected ? 1.8f : 1.0f);
        g.setColour (col::bg);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        auto label = title;
        if (kind == "audio")
        {
            if ((bool) c.getProperty (ids::reverse)) label << "  [rev]";
            if ((double) c.getProperty (ids::pitch, 0.0) != 0) label << "  [" << juce::String ((double) c.getProperty (ids::pitch), 0) << " st]";
            if ((bool) c.getProperty (ids::stretch)) label << "  [" << juce::String ((double) c.getProperty (ids::srcBpm), 0) << " bpm]";
        }
        g.drawText (label, r.withHeight ((float) titleH).reduced (4, 0), juce::Justification::centredLeft, true);

        auto body = r.withTrimmedTop ((float) titleH).reduced (1, 2);
        g.saveState();
        g.reduceClipRegion (body.toNearestInt());
        if (kind == "pattern")
        {
            auto p = pr.findPattern ((int) c.getProperty (ids::pattern));
            const double L = juce::jmax (0.25, (double) p.getProperty (ids::length, 4.0));
            int lo = 127, hi = 0;
            for (auto n : p) { int k = n.getProperty (ids::key); lo = juce::jmin (lo, k); hi = juce::jmax (hi, k); }
            if (hi >= lo)
            {
                const double off = c.getProperty (ids::offset, 0.0), len = c.getProperty (ids::len);
                g.setColour (colour.brighter (0.4f));
                for (double rep = -std::fmod (off, L); rep < len; rep += L)
                    for (auto n : p)
                    {
                        double st = rep + (double) n.getProperty (ids::start);
                        if (st < 0 || st >= len) continue;
                        float x = body.getX() + (float) (st * pl.pxPerBeat);
                        float y = body.getY() + body.getHeight() * (1.0f - ((int) n.getProperty (ids::key) - lo + 0.5f) / (float) (hi - lo + 1));
                        g.fillRect (x, y - 1.0f, juce::jmax (1.5f, (float) ((double) n.getProperty (ids::len) * pl.pxPerBeat)), 2.0f);
                    }
            }
        }
        else if (kind == "audio")
        {
            juce::File f (c.getProperty (ids::file).toString());
            auto peaks = WaveCache::get().peaks (pl.ctx.samples, f);
            const double spb = srcSecPerBeat (pr, c);
            const double off = c.getProperty (ids::offset, 0.0), len = c.getProperty (ids::len);
            const double s0 = off * peaks->sampleRate, s1 = (off + len * spb) * peaks->sampleRate;
            g.setColour (colour.brighter (0.3f));
            WaveCache::draw (g, body, *peaks, s0, s1, (bool) c.getProperty (ids::reverse));
            // fades
            const double fi = c.getProperty (ids::fadeIn, 0.0), fo = c.getProperty (ids::fadeOut, 0.0);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            if (fi > 0) { juce::Path tri; tri.addTriangle (body.getX(), body.getY(), body.getX() + (float) (fi * pl.pxPerBeat), body.getY(), body.getX(), body.getBottom()); g.fillPath (tri); }
            if (fo > 0) { juce::Path tri; tri.addTriangle (body.getRight(), body.getY(), body.getRight() - (float) (fo * pl.pxPerBeat), body.getY(), body.getRight(), body.getBottom()); g.fillPath (tri); }
            if (s1 > peaks->length && peaks->length > 0)
            {
                float endX = body.getX() + (float) ((peaks->length / peaks->sampleRate - off) / spb * pl.pxPerBeat);
                g.setColour (juce::Colours::black.withAlpha (0.3f));
                g.fillRect (body.withLeft (juce::jmax (body.getX(), endX)));
            }
        }
        else
        {
            auto pts = Project::pointsFromString (c.getProperty (ids::points).toString());
            juce::Path path;
            for (size_t i = 0; i < pts.size(); ++i)
            {
                auto pt = juce::Point<float> (body.getX() + pts[i].x * body.getWidth(), body.getBottom() - pts[i].y * body.getHeight());
                if (i == 0) path.startNewSubPath (pt); else path.lineTo (pt);
            }
            g.setColour (colour.brighter (0.4f));
            g.strokePath (path, juce::PathStrokeType (2.0f));
            for (auto& p : pts) g.fillEllipse (body.getX() + p.x * body.getWidth() - 3, body.getBottom() - p.y * body.getHeight() - 3, 6, 6);
        }
        g.restoreState();
    }

    juce::ValueTree clipAt (juce::Point<int> p) const
    {
        auto pl2 = pl.ctx.project.playlist();
        for (int i = pl2.getNumChildren(); --i >= 0;)
        {
            auto c = pl2.getChild (i);
            if (c.hasType (ids::CLIP) && clipRect (c).contains (p.toFloat())) return c;
        }
        return {};
    }

    double snapBeat (double b) const { double s = pl.snap(); return s > 0 ? std::floor (b / s + 1.0e-9) * s : b; }

    int autoPointAt (const juce::ValueTree& c, juce::Point<int> p) const
    {
        auto body = clipRect (c).withTrimmedTop ((float) titleH).reduced (1, 2);
        auto pts = Project::pointsFromString (c.getProperty (ids::points).toString());
        for (size_t i = 0; i < pts.size(); ++i)
            if (juce::Point<float> (body.getX() + pts[i].x * body.getWidth(), body.getBottom() - pts[i].y * body.getHeight()).getDistanceFrom (p.toFloat()) < 7)
                return (int) i;
        return -1;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pl.grabKeyboardFocus();
        auto& pr = pl.ctx.project;
        const double beat = e.x / pl.pxPerBeat;
        const int track = juce::jlimit (0, numPlaylistTracks - 1, e.y / pl.trackH);
        auto c = clipAt (e.getPosition());

        if (e.mods.isPopupMenu())
        {
            if (c.isValid())
            {
                if (c.getProperty (ids::kind).toString() == "auto" && e.y > clipRect (c).getY() + titleH)
                {
                    int pi = autoPointAt (c, e.getPosition());
                    auto pts = Project::pointsFromString (c.getProperty (ids::points).toString());
                    if (pi >= 0 && pts.size() > 2) { pr.beginEdit ("Delete point"); pts.erase (pts.begin() + pi); c.setProperty (ids::points, Project::pointsToString (pts), pr.um()); return; }
                }
                if (! pl.selection.contains (c)) { pl.selection.clear(); pl.selection.add (c); }
                pl.clipMenu (c);
            }
            else pl.emptyMenu (track, snapBeat (beat));
            pl.repaintAll();
            return;
        }

        if (c.isValid() && pl.tool == sliceTool)
        {
            pr.beginEdit ("Slice clip");
            pl.splitClip (c, snapBeat (beat));
            return;
        }

        if (! c.isValid())
        {
            if (pl.tool == selectTool || e.mods.isCtrlDown())
            {
                mode = lassoing; lassoStart = e.getPosition(); lasso = {};
                if (! e.mods.isShiftDown()) pl.selection.clear();
                pl.repaintAll();
                return;
            }
            pl.selection.clear();
            pr.beginEdit ("Add clip");
            c = pr.addPatternClip (track, snapBeat (beat), pl.ctx.currentPattern());
            pl.selection.add (c);
        }
        else
        {
            if (! pl.selection.contains (c)) { if (! e.mods.isShiftDown()) pl.selection.clear(); pl.selection.add (c); }
            pr.beginEdit ("Edit clip");
        }

        auto r = clipRect (c);
        mode = moving;
        if (c.getProperty (ids::kind).toString() == "auto" && e.y > r.getY() + titleH && e.x < r.getRight() - 6 && e.x > r.getX() + 6)
        {
            int pi = autoPointAt (c, e.getPosition());
            auto pts = Project::pointsFromString (c.getProperty (ids::points).toString());
            if (pi < 0)
            {
                auto body = r.withTrimmedTop ((float) titleH).reduced (1, 2);
                pts.push_back ({ juce::jlimit (0.0f, 1.0f, (e.x - body.getX()) / body.getWidth()), juce::jlimit (0.0f, 1.0f, (body.getBottom() - e.y) / body.getHeight()) });
                std::sort (pts.begin(), pts.end(), [] (auto& a, auto& b) { return a.x < b.x; });
                c.setProperty (ids::points, Project::pointsToString (pts), pr.um());
                pi = autoPointAt (c, e.getPosition());
            }
            mode = pointDrag; autoClip = c; pointIndex = pi;
            return;
        }
        if (e.x > r.getRight() - 6) mode = resizing;
        else if (e.x < r.getX() + 6) mode = trimming;

        if (mode == moving && e.mods.isAltDown())
        {
            // alt-drag duplicates
            juce::Array<juce::ValueTree> copies;
            for (auto& s : pl.selection) { auto cp = s.createCopy(); pr.playlist().appendChild (cp, pr.um()); copies.add (cp); }
            pl.selection = copies;
        }
        origins.clear();
        for (auto& s : pl.selection)
            origins.push_back ({ s, (double) s.getProperty (ids::start), (int) s.getProperty (ids::track), (double) s.getProperty (ids::len), (double) s.getProperty (ids::offset, 0.0) });
        pl.repaintAll();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& pr = pl.ctx.project;
        if (mode == lassoing)
        {
            lasso = juce::Rectangle<int> (lassoStart, e.getPosition());
            pl.selection.clear();
            for (auto c : pr.playlist()) if (c.hasType (ids::CLIP) && clipRect (c).intersects (lasso.toFloat())) pl.selection.add (c);
            repaint();
            return;
        }
        if (mode == pointDrag && autoClip.isValid())
        {
            auto body = clipRect (autoClip).withTrimmedTop ((float) titleH).reduced (1, 2);
            auto pts = Project::pointsFromString (autoClip.getProperty (ids::points).toString());
            if (pointIndex >= 0 && pointIndex < (int) pts.size())
            {
                float lo = pointIndex > 0 ? pts[(size_t) pointIndex - 1].x : 0.0f;
                float hi = pointIndex + 1 < (int) pts.size() ? pts[(size_t) pointIndex + 1].x : 1.0f;
                pts[(size_t) pointIndex] = { juce::jlimit (lo, hi, (e.x - body.getX()) / body.getWidth()), juce::jlimit (0.0f, 1.0f, (body.getBottom() - e.y) / body.getHeight()) };
                autoClip.setProperty (ids::points, Project::pointsToString (pts), pr.um());
            }
            return;
        }
        double d = e.getDistanceFromDragStartX() / pl.pxPerBeat;
        const double s = pl.snap();
        if (s > 0) d = std::round (d / s) * s;
        const int dt = (int) std::round ((double) e.getDistanceFromDragStartY() / pl.trackH);
        for (auto& o : origins)
        {
            if (mode == moving)
            {
                o.node.setProperty (ids::start, juce::jmax (0.0, o.start + d), pr.um());
                o.node.setProperty (ids::track, juce::jlimit (0, numPlaylistTracks - 1, o.track + dt), pr.um());
            }
            else if (mode == resizing)
                o.node.setProperty (ids::len, juce::jmax (s > 0 ? s : 0.0625, o.len + d), pr.um());
            else if (mode == trimming)
            {
                double nd = juce::jlimit (-o.start, o.len - (s > 0 ? s : 0.0625), d);
                o.node.setProperty (ids::start, o.start + nd, pr.um());
                o.node.setProperty (ids::len, o.len - nd, pr.um());
                const auto kind = o.node.getProperty (ids::kind).toString();
                if (kind == "audio") o.node.setProperty (ids::offset, juce::jmax (0.0, o.offset + nd * srcSecPerBeat (pr, o.node)), pr.um());
                else if (kind == "pattern") o.node.setProperty (ids::offset, juce::jmax (0.0, o.offset + nd), pr.um());
            }
        }
    }

    void mouseUp (const juce::MouseEvent&) override { mode = none; lasso = {}; autoClip = {}; repaint(); }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto c = clipAt (e.getPosition());
        if (! c.isValid()) return;
        auto kind = c.getProperty (ids::kind).toString();
        if (kind == "pattern")
        {
            pl.ctx.setCurrentPattern ((int) c.getProperty (ids::pattern));
            if (pl.ctx.showView) pl.ctx.showView (viewRack);
        }
        else if (kind == "audio" && pl.ctx.openInChopShop)
            pl.ctx.openInChopShop (juce::File (c.getProperty (ids::file).toString()), -1, c);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        auto c = clipAt (e.getPosition());
        if (! c.isValid()) { setMouseCursor (pl.tool == sliceTool ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::NormalCursor); return; }
        auto r = clipRect (c);
        setMouseCursor (pl.tool == sliceTool ? juce::MouseCursor::IBeamCursor
                        : (e.x > r.getRight() - 6 || e.x < r.getX() + 6) ? juce::MouseCursor::LeftRightResizeCursor
                        : juce::MouseCursor::DraggingHandCursor);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCtrlDown())
        {
            pl.pxPerBeat = juce::jlimit (2.0, 200.0, pl.pxPerBeat * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15));
            pl.resized();
        }
        else Component::mouseWheelMove (e, w);
    }

    enum Mode { none, moving, resizing, trimming, lassoing, pointDrag } mode = none;
    struct Origin { juce::ValueTree node; double start; int track; double len, offset; };
    std::vector<Origin> origins;
    juce::Point<int> lassoStart;
    juce::Rectangle<int> lasso;
    juce::ValueTree autoClip;
    int pointIndex = -1;
};

//==============================================================================
struct Playlist::Headers : public juce::Component
{
    explicit Headers (Playlist& o) : pl (o) {}
    Playlist& pl;
    int offsetY = 0;
    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::panel);
        auto tr = pl.ctx.project.playlist();
        for (int t = 0; t < numPlaylistTracks; ++t)
        {
            auto node = tr.getChild (t);
            juce::Rectangle<int> r (0, t * pl.trackH - offsetY, getWidth(), pl.trackH);
            if (r.getBottom() < 0 || r.getY() > getHeight()) continue;
            g.setColour (col::outline);
            g.drawHorizontalLine (r.getBottom() - 1, 0, (float) getWidth());
            const bool muted = (bool) node.getProperty (ids::mute, false);
            g.setColour (muted ? col::outline : col::accent);
            g.fillEllipse (r.getX() + 6.0f, r.getCentreY() - 5.0f, 10, 10);
            g.setColour (muted ? col::textDim : col::text);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (node.getProperty (ids::name).toString(), r.withTrimmedLeft (22).withTrimmedRight (34), juce::Justification::centredLeft, true);
            g.setColour (col::textDim);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92 ")) + juce::String ((int) node.getProperty (ids::mixer, 0)), r.removeFromRight (32), juce::Justification::centred);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        int t = (e.y + offsetY) / pl.trackH;
        auto node = pl.ctx.project.playlist().getChild (t);
        if (! node.isValid()) return;
        auto& pr = pl.ctx.project;
        if (e.x < 20) { pr.beginEdit ("Mute track"); node.setProperty (ids::mute, ! (bool) node.getProperty (ids::mute, false), pr.um()); return; }
        if (e.x > getWidth() - 34 || e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem (1000, "Rename track...");
            m.addSectionHeader ("Audio clips on this track go to mixer:");
            for (int i = 0; i < numMixerTracks; ++i) m.addItem (1 + i, juce::String (i) + "  " + pr.mixerTrack (i).getProperty (ids::name).toString());
            m.addItem (2000, "Record audio onto this track");
            m.showMenuAsync ({}, [this, node] (int r) mutable
            {
                auto& p2 = pl.ctx.project;
                if (r == 1000) askForText ("Rename track", node.getProperty (ids::name), [this, node] (juce::String s) mutable { pl.ctx.project.beginEdit ("Rename"); node.setProperty (ids::name, s, pl.ctx.project.um()); });
                else if (r == 2000) { pl.ctx.engine->recordTrack = p2.playlist().indexOf (node); pl.ctx.status ("Recording will go to " + node.getProperty (ids::name).toString()); }
                else if (r > 0) { p2.beginEdit ("Track route"); node.setProperty (ids::mixer, r - 1, p2.um()); }
                repaint();
            });
        }
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        int t = (e.y + offsetY) / pl.trackH;
        auto node = pl.ctx.project.playlist().getChild (t);
        if (node.isValid()) askForText ("Rename track", node.getProperty (ids::name), [this, node] (juce::String s) mutable { pl.ctx.project.beginEdit ("Rename"); node.setProperty (ids::name, s, pl.ctx.project.um()); });
    }
};

struct Playlist::Ruler : public juce::Component
{
    explicit Ruler (Playlist& o) : pl (o) {}
    Playlist& pl;
    int offsetX = 0;
    double dragStart = -1;
    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::panel);
        auto& pr = pl.ctx.project;
        const double ls = pr.root.getProperty (ids::loopStart, 0.0), le = pr.root.getProperty (ids::loopEnd, 0.0);
        if (le > ls)
        {
            g.setColour (col::accent.withAlpha (0.5f));
            g.fillRect ((float) (ls * pl.pxPerBeat - offsetX), 0.0f, (float) ((le - ls) * pl.pxPerBeat), 6.0f);
        }
        g.setFont (juce::FontOptions (11.0f));
        const int barEvery = pl.pxPerBeat * 4 < 30 ? 4 : 1;
        for (int bar = 0; bar * 4 * pl.pxPerBeat - offsetX < getWidth(); ++bar)
        {
            int x = (int) (bar * 4 * pl.pxPerBeat) - offsetX;
            if (x < -40 || bar % barEvery) continue;
            g.setColour (col::text);
            g.drawText (juce::String (bar + 1), x + 3, 4, 40, getHeight() - 4, juce::Justification::centredLeft);
            g.setColour (col::outline);
            g.drawVerticalLine (x, 8, (float) getHeight());
        }
        float px = (float) (pl.ctx.engine->getBeat() * pl.pxPerBeat - offsetX);
        if (pl.ctx.engine->getMode() == TransportMode::song)
        {
            juce::Path tri; tri.addTriangle (px - 6, 6, px + 6, 6, px, (float) getHeight());
            g.setColour (col::accent2); g.fillPath (tri);
        }
    }
    double beatAt (int x) const { return juce::jmax (0.0, (x + offsetX) / pl.pxPerBeat); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& pr = pl.ctx.project;
        if (e.mods.isPopupMenu())
        {
            pr.beginEdit ("Clear loop");
            pr.root.setProperty (ids::loopStart, 0.0, pr.um());
            pr.root.setProperty (ids::loopEnd, 0.0, pr.um());
            return;
        }
        dragStart = std::floor (beatAt (e.x) / 4.0) * 4.0;
        if (pl.ctx.engine->getMode() != TransportMode::song) pl.ctx.engine->setMode (TransportMode::song);
        pl.ctx.engine->setBeat (dragStart);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (std::abs (e.getDistanceFromDragStartX()) < 6) return;
        auto& pr = pl.ctx.project;
        double b = std::round (beatAt (e.x) / 4.0) * 4.0;
        double a = juce::jmin (dragStart, b), z = juce::jmax (dragStart, b);
        if (z - a < 4.0) z = a + 4.0;
        pr.root.setProperty (ids::loopStart, a, nullptr);
        pr.root.setProperty (ids::loopEnd, z, nullptr);
    }
};

//==============================================================================
Playlist::Playlist (AppContext& c) : ctx (c)
{
    lane = std::make_unique<Lane> (*this);
    headers = std::make_unique<Headers> (*this);
    ruler = std::make_unique<Ruler> (*this);
    viewport.setViewedComponent (lane.get(), false);
    viewport.onMove = [this]
    {
        headers->offsetY = viewport.getViewPositionY(); headers->repaint();
        ruler->offsetX = viewport.getViewPositionX(); ruler->repaint();
    };
    addAndMakeVisible (viewport);
    addAndMakeVisible (*headers);
    addAndMakeVisible (*ruler);

    for (auto* b : { &drawBtn, &sliceBtn, &selectBtn, &followBtn })
    {
        addAndMakeVisible (*b);
        b->setColour (juce::TextButton::buttonOnColourId, col::accent);
    }
    drawBtn.setTooltip ("Draw: click to place the current pattern, drag clips to move, edges to resize (B)");
    sliceBtn.setTooltip ("Slice: click a clip to cut it in two (C)");
    selectBtn.setTooltip ("Select: drag a box to select several clips (E). Ctrl-drag also selects.");
    drawBtn.onClick = [this] { tool = drawTool; resized(); };
    sliceBtn.onClick = [this] { tool = sliceTool; resized(); };
    selectBtn.onClick = [this] { tool = selectTool; resized(); };
    followBtn.setClickingTogglesState (true);
    followBtn.setToggleState (true, juce::dontSendNotification);
    followBtn.setTooltip ("Scroll to follow the playhead");
    addAndMakeVisible (snapBox);
    snapBox.addItemList ({ "Snap: Bar", "Snap: Beat", "Snap: Step", "Snap: None" }, 1);
    snapBox.setSelectedId (2);
    for (auto* b : { &zoomIn, &zoomOut }) addAndMakeVisible (*b);
    zoomIn.onClick = [this] { pxPerBeat = juce::jmin (200.0, pxPerBeat * 1.3); resized(); };
    zoomOut.onClick = [this] { pxPerBeat = juce::jmax (2.0, pxPerBeat / 1.3); resized(); };

    setWantsKeyboardFocus (true);
    ctx.project.root.addListener (this);
    ctx.addChangeListener (this);
    startTimerHz (30);
}

Playlist::~Playlist()
{
    ctx.project.root.removeListener (this);
    ctx.removeChangeListener (this);
}

double Playlist::snap() const
{
    const double v[] = { 4.0, 1.0, 0.25, 0.0 };
    return v[juce::jlimit (0, 3, snapBox.getSelectedItemIndex())];
}

void Playlist::repaintAll() { lane->repaint(); headers->repaint(); ruler->repaint(); }

void Playlist::updateSize()
{
    const double len = juce::jmax (64.0, ctx.project.songLengthBeats() + 64.0);
    lane->setSize ((int) (len * pxPerBeat), numPlaylistTracks * trackH);
}

void Playlist::timerCallback()
{
    if (! ctx.engine->isPlaying() || ctx.engine->getMode() != TransportMode::song) return;
    lane->repaint(); ruler->repaint();
    if (followBtn.getToggleState())
    {
        int x = (int) (ctx.engine->getBeat() * pxPerBeat);
        auto vis = viewport.getViewArea();
        if (x > vis.getRight() - 40 || x < vis.getX()) viewport.setViewPosition (juce::jmax (0, x - 40), vis.getY());
    }
}

void Playlist::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    drawViewHeader (g, getLocalBounds().removeFromTop (36).withTrimmedRight (420), "Playlist", "Right-click for options  -  drag sounds in from the browser  -  drag on the ruler to loop");
}

void Playlist::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (36).reduced (6, 5);
    zoomIn.setBounds (head.removeFromRight (26)); zoomOut.setBounds (head.removeFromRight (26)); head.removeFromRight (6);
    followBtn.setBounds (head.removeFromRight (60)); head.removeFromRight (6);
    snapBox.setBounds (head.removeFromRight (110)); head.removeFromRight (6);
    selectBtn.setBounds (head.removeFromRight (60));
    sliceBtn.setBounds (head.removeFromRight (56));
    drawBtn.setBounds (head.removeFromRight (56));
    drawBtn.setToggleState (tool == drawTool, juce::dontSendNotification);
    sliceBtn.setToggleState (tool == sliceTool, juce::dontSendNotification);
    selectBtn.setToggleState (tool == selectTool, juce::dontSendNotification);

    auto rulerArea = r.removeFromTop (22);
    headers->setBounds (r.removeFromLeft (150));
    ruler->setBounds (rulerArea.withTrimmedLeft (150));
    viewport.setBounds (r);
    updateSize();
    viewport.onMove();
}

bool Playlist::keyPressed (const juce::KeyPress& k)
{
    auto& pr = ctx.project;
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        pr.beginEdit ("Delete clips");
        for (auto& c : selection) pr.playlist().removeChild (c, pr.um());
        selection.clear();
        return true;
    }
    if (k == juce::KeyPress ('c', juce::ModifierKeys::ctrlModifier, 0)) { clipboard.clear(); for (auto& c : selection) clipboard.add (c.createCopy()); return true; }
    if (k == juce::KeyPress ('v', juce::ModifierKeys::ctrlModifier, 0) && ! clipboard.isEmpty())
    {
        double first = 1.0e9;
        for (auto& c : clipboard) first = juce::jmin (first, (double) c.getProperty (ids::start));
        const double at = std::floor (ctx.engine->getBeat());
        pr.beginEdit ("Paste");
        selection.clear();
        for (auto& c : clipboard)
        {
            auto cp = c.createCopy();
            cp.setProperty (ids::start, at + (double) c.getProperty (ids::start) - first, nullptr);
            pr.playlist().appendChild (cp, pr.um());
            selection.add (cp);
        }
        return true;
    }
    if (k == juce::KeyPress ('d', juce::ModifierKeys::ctrlModifier, 0) && ! selection.isEmpty())
    {
        double lo = 1.0e9, hi = 0;
        for (auto& c : selection) { lo = juce::jmin (lo, (double) c.getProperty (ids::start)); hi = juce::jmax (hi, (double) c.getProperty (ids::start) + (double) c.getProperty (ids::len)); }
        const double shift = std::ceil ((hi - lo) / 4.0 - 1.0e-9) * 4.0;
        pr.beginEdit ("Duplicate");
        juce::Array<juce::ValueTree> copies;
        for (auto& c : selection) { auto cp = c.createCopy(); cp.setProperty (ids::start, (double) c.getProperty (ids::start) + shift, nullptr); pr.playlist().appendChild (cp, pr.um()); copies.add (cp); }
        selection = copies;
        return true;
    }
    if (k.getTextCharacter() == 'b') { tool = drawTool; resized(); return true; }
    if (k.getTextCharacter() == 'c') { tool = sliceTool; resized(); return true; }
    if (k.getTextCharacter() == 'e') { tool = selectTool; resized(); return true; }
    return false;
}

//==============================================================================
void Playlist::splitClip (juce::ValueTree c, double at)
{
    auto& pr = ctx.project;
    const double st = c.getProperty (ids::start), len = c.getProperty (ids::len);
    if (at <= st + 1.0e-6 || at >= st + len - 1.0e-6) return;
    auto right = c.createCopy();
    const double cut = at - st;
    c.setProperty (ids::len, cut, pr.um());
    right.setProperty (ids::start, at, nullptr);
    right.setProperty (ids::len, len - cut, nullptr);
    const auto kind = c.getProperty (ids::kind).toString();
    if (kind == "audio")
    {
        right.setProperty (ids::offset, (double) c.getProperty (ids::offset, 0.0) + cut * srcSecPerBeat (pr, c), nullptr);
        right.setProperty (ids::fadeIn, 0.0, nullptr);
        c.setProperty (ids::fadeOut, 0.0, pr.um());
    }
    else if (kind == "pattern")
        right.setProperty (ids::offset, (double) c.getProperty (ids::offset, 0.0) + cut, nullptr);
    else if (kind == "auto")
    {
        // split the automation curve
        auto pts = Project::pointsFromString (c.getProperty (ids::points).toString());
        const float f = (float) (cut / len);
        auto valueAt = [&] (float x)
        {
            for (size_t i = 1; i < pts.size(); ++i)
                if (x <= pts[i].x) { float t = (x - pts[i - 1].x) / juce::jmax (1.0e-6f, pts[i].x - pts[i - 1].x); return pts[i - 1].y + (pts[i].y - pts[i - 1].y) * t; }
            return pts.empty() ? 0.5f : pts.back().y;
        };
        std::vector<juce::Point<float>> a, b;
        for (auto& p : pts) { if (p.x <= f) a.push_back ({ p.x / f, p.y }); else b.push_back ({ (p.x - f) / (1 - f), p.y }); }
        a.push_back ({ 1.0f, valueAt (f) });
        b.insert (b.begin(), { 0.0f, valueAt (f) });
        c.setProperty (ids::points, Project::pointsToString (a), pr.um());
        right.setProperty (ids::points, Project::pointsToString (b), nullptr);
    }
    pr.playlist().appendChild (right, pr.um());
}

void Playlist::chopAtTransients (juce::ValueTree c)
{
    auto& pr = ctx.project;
    auto data = ctx.samples.get (juce::File (c.getProperty (ids::file).toString()));
    if (data == nullptr) return;
    const double spb = srcSecPerBeat (pr, c);
    const double off = c.getProperty (ids::offset, 0.0), len = c.getProperty (ids::len), st = c.getProperty (ids::start);
    const int s0 = (int) (off * data->sampleRate), s1 = juce::jmin (data->buffer.getNumSamples(), (int) ((off + len * spb) * data->sampleRate));
    if (s1 - s0 < 1000) return;
    auto region = tools::extract (data->buffer, { s0, s1 });
    if ((bool) c.getProperty (ids::reverse)) tools::reverseRange (region, { 0, region.getNumSamples() });
    auto pts = tools::detectTransients (region, data->sampleRate, 0.6f);
    std::vector<double> beats;
    for (int p : pts) if (p > 0) beats.push_back (st + p / data->sampleRate / spb);
    std::sort (beats.rbegin(), beats.rend());
    pr.beginEdit ("Chop at transients");
    for (double b : beats) splitClip (c, b);
    ctx.status ("Chopped into " + juce::String ((int) beats.size() + 1) + " pieces");
}

void Playlist::sliceToPads (juce::ValueTree c)
{
    auto& pr = ctx.project;
    juce::File f (c.getProperty (ids::file).toString());
    auto data = ctx.samples.get (f);
    if (data == nullptr) return;
    const double spb = srcSecPerBeat (pr, c);
    const double off = c.getProperty (ids::offset, 0.0), len = c.getProperty (ids::len);
    const int s0 = (int) (off * data->sampleRate), s1 = juce::jmin (data->buffer.getNumSamples(), (int) ((off + len * spb) * data->sampleRate));
    auto region = tools::extract (data->buffer, { s0, s1 });
    auto pts = tools::detectTransients (region, data->sampleRate, 0.6f);
    if (pts.size() > 16) { std::vector<int> fewer; for (size_t i = 0; i < 16; ++i) fewer.push_back (pts[i * pts.size() / 16]); pts = fewer; }
    auto ranges = tools::pointsToRanges (pts, region.getNumSamples());

    pr.beginEdit ("Slice to Drum Pads");
    auto ch = ctx.addPadsChannel (c.getProperty (ids::name).toString() + " chops", false);
    const int id = (int) ch.getProperty (ids::id);
    auto pat = pr.addPattern (c.getProperty (ids::name).toString() + " chops");
    const int pid = (int) pat.getProperty (ids::id);
    for (size_t i = 0; i < ranges.size() && i < 16; ++i)
    {
        pr.setPadSample (id, (int) i, f, { s0 + ranges[i].getStart(), s0 + ranges[i].getEnd() });
        const double beat = ranges[i].getStart() / data->sampleRate / spb;
        const double nlen = ranges[i].getLength() / data->sampleRate / spb;
        pr.addNote (pid, id, beat, juce::jmax (0.0625, nlen), DrumPadsProcessor::baseNote + (int) i, 0.8f);
    }
    pat.setProperty (ids::length, juce::jmax (4.0, std::ceil (len / 4.0 - 1.0e-9) * 4.0), pr.um());
    // put the new pattern under the clip and mute nothing - the user can choose which to keep
    int track = juce::jmin (numPlaylistTracks - 1, (int) c.getProperty (ids::track) + 1);
    pr.addPatternClip (track, c.getProperty (ids::start), pid).setProperty (ids::len, len, pr.um());
    ctx.selectChannel (id);
    ctx.setCurrentPattern (pid);
    ctx.status ("Sliced into " + juce::String ((int) ranges.size()) + " pads - the new pattern plays them in order");
}

void Playlist::clipMenu (juce::ValueTree c)
{
    auto& pr = ctx.project;
    const auto kind = c.getProperty (ids::kind).toString();
    juce::PopupMenu m;
    if (kind == "pattern")
    {
        m.addItem (1, "Open pattern in Channel Rack");
        m.addItem (2, "Make unique (copy the pattern)");
        m.addItem (3, "Bounce to audio (resample)");
    }
    else if (kind == "audio")
    {
        m.addItem (10, "Reverse", true, (bool) c.getProperty (ids::reverse));
        m.addItem (11, "Stretch to project tempo", true, (bool) c.getProperty (ids::stretch));
        m.addItem (12, "Set original tempo (BPM)...");
        juce::PopupMenu pitch;
        for (int s : { 12, 7, 5, 2, 1, -1, -2, -5, -7, -12 }) pitch.addItem (100 + s + 24, (s > 0 ? "+" : "") + juce::String (s) + " semitones");
        pitch.addItem (99, "Reset pitch");
        m.addSubMenu ("Pitch", pitch);
        juce::PopupMenu fades;
        fades.addItem (20, "Fade in: 1 beat");
        fades.addItem (21, "Fade out: 1 beat");
        fades.addItem (22, "Short de-click fades");
        fades.addItem (23, "Remove fades");
        m.addSubMenu ("Fades", fades);
        juce::PopupMenu gain;
        gain.addItem (30, "+3 dB"); gain.addItem (31, "-3 dB"); gain.addItem (32, "Reset");
        m.addSubMenu ("Gain", gain);
        m.addSeparator();
        m.addItem (40, "Open in Chop Shop");
        m.addItem (41, "Chop at transients (split clip)");
        m.addItem (42, "Slice to Drum Pads + pattern");
    }
    else
    {
        m.addItem (50, "Ramp up");
        m.addItem (51, "Ramp down");
        m.addItem (52, "Wobble (4 cycles)");
        m.addItem (53, "Reset to flat");
    }
    m.addSeparator();
    m.addItem (63, "Mute clip", true, (bool) c.getProperty (ids::mute, false));
    m.addItem (60, "Split at playhead");
    m.addItem (61, "Duplicate (Ctrl+D)");
    m.addItem (62, "Delete");
    m.showMenuAsync ({}, [this, c, kind] (int r) mutable
    {
        auto& p = ctx.project;
        if (r == 0) return;
        p.beginEdit ("Clip edit");
        if (r == 1) { ctx.setCurrentPattern ((int) c.getProperty (ids::pattern)); if (ctx.showView) ctx.showView (viewRack); }
        else if (r == 2)
        {
            auto src = p.findPattern ((int) c.getProperty (ids::pattern));
            auto np = p.addPattern (src.getProperty (ids::name).toString() + " (unique)");
            np.setProperty (ids::length, src.getProperty (ids::length), p.um());
            for (auto n : src) np.appendChild (n.createCopy(), p.um());
            c.setProperty (ids::pattern, np.getProperty (ids::id), p.um());
        }
        else if (r == 3)
        {
            const int pid = (int) c.getProperty (ids::pattern);
            const int prevPat = ctx.currentPattern();
            ctx.engine->stop();
            ctx.engine->setCurrentPattern (pid);
            ctx.engine->storeStatesToTree();
            ctx.engine->syncNow();
            auto pname = p.findPattern (pid).getProperty (ids::name).toString();
            Engine::ExportOptions o;
            o.file = tools::uniqueFile (paths::library().getChildFile ("Bounces"), pname + " bounce", ".wav");
            o.file.getParentDirectory().createDirectory();
            o.patternOnly = true;
            juce::String err;
            const bool ok = ctx.engine->exportAudio (o, nullptr, err);
            ctx.engine->setCurrentPattern (prevPat);
            if (! ok) { ctx.status ("Bounce failed: " + err); return; }
            ctx.samples.invalidate (o.file);
            if (auto d = ctx.samples.get (o.file))
            {
                p.beginEdit ("Bounce");
                auto a = p.addAudioClip ((int) c.getProperty (ids::track), c.getProperty (ids::start), o.file, d->lengthSeconds(), p.tempo());
                a.setProperty (ids::len, c.getProperty (ids::len), p.um());
                p.playlist().removeChild (c, p.um());
                ctx.library.rescan();
                ctx.status ("Bounced " + pname + " to audio - double-click it to chop it up");
            }
        }
        else if (r == 10) c.setProperty (ids::reverse, ! (bool) c.getProperty (ids::reverse), p.um());
        else if (r == 11)
        {
            bool on = ! (bool) c.getProperty (ids::stretch);
            if (on && (double) c.getProperty (ids::srcBpm, 0.0) <= 0)
                if (auto d = ctx.samples.get (juce::File (c.getProperty (ids::file).toString()))) c.setProperty (ids::srcBpm, tools::guessLoopBpm (d->lengthSeconds()), p.um());
            c.setProperty (ids::stretch, on, p.um());
        }
        else if (r == 12)
            askForText ("Original tempo of this audio (BPM)", juce::String ((double) c.getProperty (ids::srcBpm, 0.0), 2), [this, c] (juce::String t) mutable
            {
                ctx.project.beginEdit ("Set BPM");
                c.setProperty (ids::srcBpm, juce::jlimit (20.0, 400.0, t.getDoubleValue()), ctx.project.um());
                c.setProperty (ids::stretch, true, ctx.project.um());
            });
        else if (r == 99) c.setProperty (ids::pitch, 0.0, p.um());
        else if (r >= 100 && r <= 150) c.setProperty (ids::pitch, (double) c.getProperty (ids::pitch, 0.0) + (r - 124), p.um());
        else if (r == 20) c.setProperty (ids::fadeIn, 1.0, p.um());
        else if (r == 21) c.setProperty (ids::fadeOut, 1.0, p.um());
        else if (r == 22) { c.setProperty (ids::fadeIn, 0.02, p.um()); c.setProperty (ids::fadeOut, 0.02, p.um()); }
        else if (r == 23) { c.setProperty (ids::fadeIn, 0.0, p.um()); c.setProperty (ids::fadeOut, 0.0, p.um()); }
        else if (r == 30) c.setProperty (ids::gain, (float) c.getProperty (ids::gain, 0.0f) + 3.0f, p.um());
        else if (r == 31) c.setProperty (ids::gain, (float) c.getProperty (ids::gain, 0.0f) - 3.0f, p.um());
        else if (r == 32) c.setProperty (ids::gain, 0.0f, p.um());
        else if (r == 40 && ctx.openInChopShop) ctx.openInChopShop (juce::File (c.getProperty (ids::file).toString()), -1, c);
        else if (r == 41) chopAtTransients (c);
        else if (r == 42) sliceToPads (c);
        else if (r == 50) c.setProperty (ids::points, "0,0;1,1", p.um());
        else if (r == 51) c.setProperty (ids::points, "0,1;1,0", p.um());
        else if (r == 52)
        {
            std::vector<juce::Point<float>> pts;
            for (int i = 0; i <= 16; ++i) pts.push_back ({ i / 16.0f, i % 2 ? 0.15f : 0.85f });
            c.setProperty (ids::points, Project::pointsToString (pts), p.um());
        }
        else if (r == 53) c.setProperty (ids::points, "0,0.5;1,0.5", p.um());
        else if (r == 60) splitClip (c, ctx.engine->getBeat());
        else if (r == 61) { selection.clear(); selection.add (c); keyPressed (juce::KeyPress ('d', juce::ModifierKeys::ctrlModifier, 0)); }
        else if (r == 62) { p.playlist().removeChild (c, p.um()); selection.removeAllInstancesOf (c); }
        else if (r == 63) c.setProperty (ids::mute, ! (bool) c.getProperty (ids::mute, false), p.um());
    });
}

void Playlist::emptyMenu (int track, double beat)
{
    juce::PopupMenu m;
    m.addItem (1, "Place current pattern here");
    juce::PopupMenu targets;
    auto list = std::make_shared<std::vector<std::pair<juce::String, juce::String>>>();
    buildTargetMenu (ctx, targets, *list, 1000);
    m.addSubMenu ("Add automation clip for...", targets);
    m.addItem (2, "Paste (Ctrl+V)", ! clipboard.isEmpty());
    m.addItem (3, "Add audio file here...");
    m.showMenuAsync ({}, [this, track, beat, list] (int r)
    {
        auto& pr = ctx.project;
        if (r == 1) { pr.beginEdit ("Add clip"); pr.addPatternClip (track, beat, ctx.currentPattern()); }
        else if (r == 2) { ctx.engine->setBeat (beat); keyPressed (juce::KeyPress ('v', juce::ModifierKeys::ctrlModifier, 0)); }
        else if (r == 3)
        {
            chooser = std::make_unique<juce::FileChooser> ("Choose audio", paths::library(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, track, beat] (const juce::FileChooser& fc)
            {
                if (fc.getResult().existsAsFile()) BrowserPanel::sendToPlaylist (ctx, fc.getResult(), track, beat);
            });
        }
        else if (r >= 1000 && r - 1000 < (int) list->size())
        {
            auto& t = (*list)[(size_t) (r - 1000)];
            pr.beginEdit ("Add automation");
            pr.addAutomationClip (track, beat, 16.0, t.first, t.second);
        }
    });
}

void Playlist::itemDropped (const SourceDetails& d)
{
    auto f = fileFromDrag (d.description);
    if (! f.existsAsFile()) return;
    auto p = d.localPosition - viewport.getPosition() + viewport.getViewPosition();
    double beat = juce::jmax (0.0, p.x / pxPerBeat);
    const double s = snap();
    if (s > 0) beat = std::floor (beat / s) * s;
    int track = juce::jlimit (0, numPlaylistTracks - 1, p.y / trackH);
    BrowserPanel::sendToPlaylist (ctx, f, track, beat);
}
}
