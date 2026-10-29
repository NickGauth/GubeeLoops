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
        setColourScheme ({ col::bg, col::panel, col::panelHi, col::outline, col::text,
                           col::panelHi, col::text, col::accent, col::text });
        setColour (ResizableWindow::backgroundColourId, col::bg);
        setColour (Slider::rotarySliderFillColourId, col::accent);
        setColour (Slider::rotarySliderOutlineColourId, col::outline);
        setColour (Slider::thumbColourId, col::text);
        setColour (Slider::trackColourId, col::accent);
        setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour (Slider::textBoxTextColourId, col::textDim);
        setColour (TextButton::buttonColourId, col::panelHi);
        setColour (TextButton::buttonOnColourId, col::accent.darker (0.3f));
        setColour (TextButton::textColourOffId, col::text);
        setColour (TextButton::textColourOnId, col::bg);
        setColour (ComboBox::backgroundColourId, col::panelHi);
        setColour (ComboBox::outlineColourId, col::outline);
        setColour (PopupMenu::backgroundColourId, col::panel);
        setColour (PopupMenu::highlightedBackgroundColourId, col::accent.darker (0.4f));
        setColour (Label::textColourId, col::text);
        setColour (TextEditor::backgroundColourId, col::panelHi);
        setColour (TextEditor::outlineColourId, col::outline);
        setColour (ListBox::backgroundColourId, col::panel);
        setColour (TreeView::backgroundColourId, col::panel);
        setColour (ScrollBar::thumbColourId, col::catGray.withAlpha (0.6f));
        setColour (TabbedButtonBar::tabOutlineColourId, col::outline);
        setColour (TabbedButtonBar::frontOutlineColourId, col::accent);
        setColour (ToggleButton::tickColourId, col::accent);
        setColour (AlertWindow::backgroundColourId, col::panel);
        setColour (DocumentWindow::textColourId, col::text);
    }

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
        float thick = jmax (2.0f, size * 0.1f);

        Path bgArc; bgArc.addCentredArc (c.x, c.y, radius - thick, radius - thick, 0, startAngle, endAngle, true);
        g.setColour (col::outline);
        g.strokePath (bgArc, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));

        const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
        float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        Path valArc; valArc.addCentredArc (c.x, c.y, radius - thick, radius - thick, 0, jmin (from, angle), jmax (from, angle), true);
        g.setColour (s.findColour (Slider::rotarySliderFillColourId).withAlpha (s.isEnabled() ? 1.0f : 0.4f));
        g.strokePath (valArc, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));

        g.setColour (col::panelHi);
        g.fillEllipse (r.reduced (thick * 1.8f));
        Point<float> tip (c.x + (radius - thick * 2.2f) * std::sin (angle), c.y - (radius - thick * 2.2f) * std::cos (angle));
        g.setColour (col::text);
        g.drawLine ({ c, tip }, jmax (1.5f, thick * 0.6f));
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return juce::FontOptions (juce::jmin (14.0f, h * 0.6f)); }
};
}
