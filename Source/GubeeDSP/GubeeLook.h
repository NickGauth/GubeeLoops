#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace gubee
{
namespace col
{
    const juce::Colour bg        { 0xff1e2126 };
    const juce::Colour panel     { 0xff272b31 };
    const juce::Colour panelHi   { 0xff30353c };
    const juce::Colour outline   { 0xff3a3f47 };
    const juce::Colour text      { 0xffe6e8eb };
    const juce::Colour textDim   { 0xff9aa0a8 };
    const juce::Colour catGray   { 0xff8a8f98 };
    const juce::Colour accent    { 0xffa6e36b };   // cat-eye green
    const juce::Colour accent2   { 0xffe8a0b0 };   // nose pink
    const juce::Colour warn      { 0xffe0a040 };
    const juce::Colour stepOn    { 0xffa6e36b };
    const juce::Colour stepOff   { 0xff3a3f47 };
    const juce::Colour stepOffAlt{ 0xff4a3f45 };

    inline juce::Colour fromIndex (int i)
    {
        static const juce::uint32 palette[] = { 0xff6fa8dc, 0xffa6e36b, 0xffe8a0b0, 0xffe0a040, 0xffb08ae0,
                                                0xff5fd3c4, 0xffe07060, 0xffd8d070, 0xff8a8f98, 0xff70c070 };
        return juce::Colour (palette[(size_t) juce::jmax (0, i) % (sizeof (palette) / sizeof (palette[0]))]);
    }
}

/** Draws the GubeeLoops gray cat logo scaled into the given bounds. */
inline void drawCatLogo (juce::Graphics& g, juce::Rectangle<float> area, bool withBackground = false)
{
    const float s = juce::jmin (area.getWidth(), area.getHeight()) / 512.0f;
    const float ox = area.getCentreX() - 256.0f * s, oy = area.getCentreY() - 256.0f * s;
    auto P = [&] (float x, float y) { return juce::Point<float> (ox + x * s, oy + y * s); };
    auto tri = [&] (juce::Point<float> a, juce::Point<float> b, juce::Point<float> c)
    { juce::Path p; p.addTriangle (a, b, c); return p; };
    auto ell = [&] (float cx, float cy, float rx, float ry)
    { return juce::Rectangle<float> (ox + (cx - rx) * s, oy + (cy - ry) * s, rx * 2 * s, ry * 2 * s); };

    if (withBackground)
    {
        g.setColour (col::bg);
        g.fillRoundedRectangle (ox, oy, 512 * s, 512 * s, 96 * s);
    }
    const juce::Colour fur (0xff8a8f98), furDark (0xff5e636b), ear (0xff7d838c), pink (0xffe8a0b0);
    const juce::PathStrokeType edge (8 * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    for (auto& p : { tri (P (104, 236), P (124, 58), P (238, 150)), tri (P (408, 236), P (388, 58), P (274, 150)) })
    {
        g.setColour (ear); g.fillPath (p);
        g.setColour (furDark); g.strokePath (p, edge);
    }
    g.setColour (pink);
    g.fillPath (tri (P (134, 196), P (142, 104), P (206, 156)));
    g.fillPath (tri (P (378, 196), P (370, 104), P (306, 156)));

    g.setColour (fur);     g.fillEllipse (ell (256, 292, 172, 150));
    g.setColour (furDark); g.drawEllipse (ell (256, 292, 172, 150), 8 * s);

    g.setColour (juce::Colour (0xff6b7078));
    g.fillRoundedRectangle (ox + 226 * s, oy + 150 * s, 14 * s, 52 * s, 7 * s);
    g.fillRoundedRectangle (ox + 249 * s, oy + 144 * s, 14 * s, 62 * s, 7 * s);
    g.fillRoundedRectangle (ox + 272 * s, oy + 150 * s, 14 * s, 52 * s, 7 * s);

    g.setColour (juce::Colour (0xffb4b8bf)); g.fillEllipse (ell (256, 362, 74, 52));

    for (float cx : { 186.0f, 326.0f })
    {
        g.setColour (col::accent);               g.fillEllipse (ell (cx, 280, 34, 40));
        g.setColour (juce::Colour (0xff15171a)); g.fillEllipse (ell (cx, 282, 10, 30));
        g.setColour (juce::Colours::white);      g.fillEllipse (ell (cx + 12, 264, 7, 7));
    }

    g.setColour (pink); g.fillPath (tri (P (236, 334), P (276, 334), P (256, 356)));

    juce::Path mouth;
    mouth.startNewSubPath (P (256, 356)); mouth.quadraticTo (P (246, 380), P (226, 372));
    mouth.startNewSubPath (P (256, 356)); mouth.quadraticTo (P (266, 380), P (286, 372));
    g.setColour (juce::Colour (0xff3a3e44));
    g.strokePath (mouth, juce::PathStrokeType (7 * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (juce::Colour (0xffe6e8eb));
    static const float whiskers[4][4] = { { 200, 352, 74, 330 }, { 200, 366, 70, 374 }, { 312, 352, 438, 330 }, { 312, 366, 442, 374 } };
    for (auto& w : whiskers)
        g.drawLine ({ P (w[0], w[1]), P (w[2], w[3]) }, 5 * s);
}

/** Dark gray theme shared by the app and the Gubee plugins. */
class GubeeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GubeeLookAndFeel()
    {
        using namespace juce;
       #if JUCE_WINDOWS
        setDefaultSansSerifTypefaceName ("Segoe UI");
       #endif
        setColourScheme ({ col::bg, col::panel, col::panelHi, col::outline, col::text,
                           col::panelHi, col::text, col::accent, col::text });
        setColour (ResizableWindow::backgroundColourId, col::bg);
        setColour (Slider::rotarySliderFillColourId, col::accent);
        setColour (Slider::rotarySliderOutlineColourId, col::outline);
        setColour (Slider::thumbColourId, col::text);
        setColour (Slider::trackColourId, col::accent);
        setColour (Slider::backgroundColourId, col::bg);
        setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour (Slider::textBoxTextColourId, col::textDim);
        setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
        setColour (TextButton::buttonColourId, col::panelHi);
        setColour (TextButton::buttonOnColourId, col::accent);
        setColour (TextButton::textColourOffId, col::text);
        setColour (TextButton::textColourOnId, col::bg);
        setColour (ComboBox::backgroundColourId, col::panelHi);
        setColour (ComboBox::outlineColourId, col::outline);
        setColour (ComboBox::arrowColourId, col::textDim);
        setColour (PopupMenu::backgroundColourId, col::panel.brighter (0.03f));
        setColour (PopupMenu::highlightedBackgroundColourId, col::accent.withAlpha (0.85f));
        setColour (PopupMenu::highlightedTextColourId, col::bg);
        setColour (PopupMenu::headerTextColourId, col::accent);
        setColour (Label::textColourId, col::text);
        setColour (TextEditor::backgroundColourId, col::bg);
        setColour (TextEditor::outlineColourId, col::outline);
        setColour (TextEditor::focusedOutlineColourId, col::accent);
        setColour (ListBox::backgroundColourId, col::panel);
        setColour (TreeView::backgroundColourId, col::panel);
        setColour (ScrollBar::thumbColourId, col::catGray.withAlpha (0.5f));
        setColour (TabbedButtonBar::tabOutlineColourId, col::outline);
        setColour (TabbedButtonBar::frontOutlineColourId, col::accent);
        setColour (ToggleButton::tickColourId, col::accent);
        setColour (ToggleButton::tickDisabledColourId, col::outline);
        setColour (AlertWindow::backgroundColourId, col::panel);
        setColour (AlertWindow::outlineColourId, col::outline);
        setColour (DocumentWindow::textColourId, col::text);
        setColour (TooltipWindow::backgroundColourId, col::panelHi);
        setColour (TooltipWindow::textColourId, col::text);
        setColour (TooltipWindow::outlineColourId, col::outline);
    }

    // ---- knobs
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        using namespace juce;
        auto r = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
        auto size = jmin (r.getWidth(), r.getHeight());
        r = r.withSizeKeepingCentre (size, size);
        auto c = r.getCentre();
        auto radius = size * 0.5f;
        auto angle = startAngle + pos * (endAngle - startAngle);
        float thick = jmax (2.0f, size * 0.09f);
        const bool hover = s.isMouseOverOrDragging();

        Path bgArc; bgArc.addCentredArc (c.x, c.y, radius - thick, radius - thick, 0, startAngle, endAngle, true);
        g.setColour (col::bg.darker (0.2f));
        g.strokePath (bgArc, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));

        const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
        float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        Path valArc; valArc.addCentredArc (c.x, c.y, radius - thick, radius - thick, 0, jmin (from, angle), jmax (from, angle), true);
        auto fill = s.findColour (Slider::rotarySliderFillColourId).withAlpha (s.isEnabled() ? 1.0f : 0.35f);
        if (hover) { g.setColour (fill.withAlpha (0.25f)); g.strokePath (valArc, PathStrokeType (thick * 2.2f, PathStrokeType::curved, PathStrokeType::rounded)); }
        g.setColour (fill);
        g.strokePath (valArc, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));

        auto cap = r.reduced (thick * 2.0f);
        g.setGradientFill (ColourGradient (col::panelHi.brighter (0.15f), cap.getX(), cap.getY(), col::panel.darker (0.1f), cap.getRight(), cap.getBottom(), false));
        g.fillEllipse (cap);
        g.setColour (Colours::black.withAlpha (0.35f));
        g.drawEllipse (cap, 1.0f);
        Point<float> tip (c.x + (cap.getWidth() * 0.5f - 2.0f) * std::sin (angle), c.y - (cap.getWidth() * 0.5f - 2.0f) * std::cos (angle));
        Point<float> inner (c.x + cap.getWidth() * 0.12f * std::sin (angle), c.y - cap.getWidth() * 0.12f * std::cos (angle));
        g.setColour (col::text);
        g.drawLine ({ inner, tip }, jmax (1.5f, thick * 0.55f));
    }

    // ---- faders
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        using namespace juce;
        if (style != Slider::LinearVertical) { LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s); return; }
        auto r = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        auto track = r.withSizeKeepingCentre (4.0f, r.getHeight() - 8.0f);
        g.setColour (col::bg.darker (0.3f));
        g.fillRoundedRectangle (track, 2.0f);
        g.setColour (col::accent.withAlpha (0.8f));
        g.fillRoundedRectangle (track.withTop (pos), 2.0f);
        // tick marks
        g.setColour (col::outline);
        for (int i = 0; i <= 10; ++i)
        {
            float ty = track.getY() + track.getHeight() * i / 10.0f;
            g.drawHorizontalLine ((int) ty, track.getRight() + 5, track.getRight() + (i % 5 == 0 ? 12.0f : 8.0f));
        }
        auto capR = Rectangle<float> (r.getCentreX() - 13.0f, pos - 9.0f, 26.0f, 18.0f);
        g.setGradientFill (ColourGradient (col::catGray.brighter (0.4f), 0, capR.getY(), col::catGray.darker (0.4f), 0, capR.getBottom(), false));
        g.fillRoundedRectangle (capR, 3.0f);
        g.setColour (Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (capR, 3.0f, 1.0f);
        g.setColour (col::bg);
        g.drawHorizontalLine ((int) capR.getCentreY(), capR.getX() + 3, capR.getRight() - 3);
        ignoreUnused (minPos, maxPos);
    }

    // ---- buttons
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgCol, bool over, bool down) override
    {
        using namespace juce;
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float cr = jmin (6.0f, r.getHeight() * 0.3f);
        auto base = b.getToggleState() ? b.findColour (TextButton::buttonOnColourId) : bgCol;
        if (! b.isEnabled()) base = base.withMultipliedAlpha (0.5f);
        if (down) base = base.darker (0.2f);
        else if (over) base = base.brighter (b.getToggleState() ? 0.1f : 0.12f);
        g.setGradientFill (ColourGradient (base.brighter (0.06f), 0, r.getY(), base.darker (0.06f), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, cr);
        g.setColour (b.getToggleState() ? base.darker (0.3f) : Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (r, cr, 1.0f);
        if (! b.getToggleState())
        {
            g.setColour (Colours::white.withAlpha (0.04f));
            g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + cr, r.getRight() - cr);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return juce::FontOptions (juce::jmin (14.0f, h * 0.55f)); }

    // ---- combo boxes
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        using namespace juce;
        auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (box.findColour (ComboBox::backgroundColourId).brighter (box.isMouseOver (true) ? 0.08f : 0.0f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (box.hasKeyboardFocus (true) ? col::accent.withAlpha (0.7f) : Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
        Path arrow;
        const float ax = (float) w - 14.0f, ay = h * 0.5f;
        arrow.startNewSubPath (ax - 4, ay - 2); arrow.lineTo (ax, ay + 2); arrow.lineTo (ax + 4, ay - 2);
        g.setColour (col::textDim);
        g.strokePath (arrow, PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, box.getWidth() - 26, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }
    juce::Font getComboBoxFont (juce::ComboBox& box) override { return juce::FontOptions (juce::jmin (14.0f, box.getHeight() * 0.55f)); }

    // ---- menus
    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
        g.setColour (col::outline);
        g.drawRect (0, 0, w, h);
    }
    juce::Font getPopupMenuFont() override { return juce::FontOptions (14.5f); }

    // ---- scrollbars
    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override
    {
        auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, w, thumbSize) : juce::Rectangle<int> (thumbStart, y, thumbSize, h);
        g.setColour (col::catGray.withAlpha (down ? 0.8f : over ? 0.65f : 0.4f));
        g.fillRoundedRectangle (thumb.toFloat().reduced (vertical ? 2.5f : 1.0f, vertical ? 1.0f : 2.5f), 3.0f);
    }
    int getDefaultScrollbarWidth() override { return 10; }

    // ---- toggles
    void drawTickBox (juce::Graphics& g, juce::Component&, float x, float y, float w, float h, bool ticked, bool enabled, bool, bool) override
    {
        auto r = juce::Rectangle<float> (x, y, w, h).reduced (1.0f);
        g.setColour (ticked ? col::accent : col::bg);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (ticked ? col::accent.darker (0.3f) : col::outline);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        if (ticked)
        {
            juce::Path tick;
            tick.startNewSubPath (r.getX() + r.getWidth() * 0.25f, r.getCentreY());
            tick.lineTo (r.getX() + r.getWidth() * 0.43f, r.getBottom() - r.getHeight() * 0.28f);
            tick.lineTo (r.getRight() - r.getWidth() * 0.22f, r.getY() + r.getHeight() * 0.28f);
            g.setColour (enabled ? col::bg : col::outline);
            g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
};

/** Shared header bar used at the top of every main view. */
inline void drawViewHeader (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, const juce::String& hint = {})
{
    g.setGradientFill (juce::ColourGradient (col::panelHi, 0, (float) area.getY(), col::panel, 0, (float) area.getBottom(), false));
    g.fillRect (area);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawHorizontalLine (area.getBottom() - 1, (float) area.getX(), (float) area.getRight());
    g.setColour (col::accent);
    g.fillRoundedRectangle ((float) area.getX() + 10.0f, (float) area.getCentreY() - 8.0f, 3.0f, 16.0f, 1.5f);
    g.setColour (col::text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    auto titleArea = area.withTrimmedLeft (20);
    g.drawText (title, titleArea, juce::Justification::centredLeft);
    if (hint.isNotEmpty())
    {
        const int tw = (int) juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)), title) + 34;
        g.setColour (col::textDim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (hint, titleArea.withTrimmedLeft (tw), juce::Justification::centredLeft, true);
    }
}
}
