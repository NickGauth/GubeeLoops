#pragma once
#include "AppContext.h"

namespace gubee
{
/** Click-and-drag numeric field (tempo etc). Double-click to type. */
class DragNumber : public juce::Component
{
public:
    DragNumber (double lo, double hi, double step, int decimals) : lo (lo), hi (hi), step (step), decimals (decimals) {}
    std::function<void (double)> onChange;
    std::function<void()> onDragStart;
    void setValue (double v) { value = v; repaint(); }
    double getValue() const { return value; }
    juce::String suffix;

    void paint (juce::Graphics& g) override
    {
        g.setColour (col::bg);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4);
        g.setColour (col::accent);
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawText (juce::String (value, decimals) + suffix, getLocalBounds(), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent&) override { startValue = value; if (onDragStart) onDragStart(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        double fine = e.mods.isShiftDown() ? 0.1 : 1.0;
        double v = juce::jlimit (lo, hi, startValue - e.getDistanceFromDragStartY() * step * fine);
        v = std::round (v / (step * fine)) * (step * fine);
        if (v != value) { value = v; repaint(); if (onChange) onChange (value); }
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        askForText ("Enter value", juce::String (value, decimals), [this] (juce::String t)
        {
            value = juce::jlimit (lo, hi, t.getDoubleValue());
            repaint();
            if (onChange) onChange (value);
        });
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        value = juce::jlimit (lo, hi, value + (w.deltaY > 0 ? step : -step));
        repaint();
        if (onChange) onChange (value);
    }

private:
    double lo, hi, step; int decimals;
    double value = 0, startValue = 0;
};

/** Transport button that draws its icon. */
class IconButton : public juce::Button
{
public:
    enum Icon { play, stop, record, metronome };
    explicit IconButton (Icon i) : juce::Button ({}), icon (i) {}
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), over, down);
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre (14.0f, 14.0f);
        const bool on = getToggleState();
        auto c = on ? (icon == record ? juce::Colours::white : col::bg) : (icon == record ? juce::Colour (0xffe05050) : col::text);
        g.setColour (c);
        juce::Path p;
        switch (icon)
        {
            case play:
                if (on) { p.addRoundedRectangle (r.getX() + 1, r.getY(), 4.5f, r.getHeight(), 1.0f); p.addRoundedRectangle (r.getRight() - 5.5f, r.getY(), 4.5f, r.getHeight(), 1.0f); }
                else p.addTriangle (r.getX() + 2, r.getY(), r.getX() + 2, r.getBottom(), r.getRight(), r.getCentreY());
                break;
            case stop: p.addRoundedRectangle (r.reduced (1.5f), 2.0f); break;
            case record: p.addEllipse (r.reduced (1.0f)); break;
            case metronome:
                p.addTriangle (r.getCentreX(), r.getY(), r.getX() + 1, r.getBottom(), r.getRight() - 1, r.getBottom());
                g.fillPath (p);
                g.setColour (on ? col::accent : col::panelHi);
                g.drawLine (r.getCentreX(), r.getBottom() - 3, r.getRight() - 1, r.getY() + 2, 1.6f);
                return;
        }
        g.fillPath (p);
    }
private:
    Icon icon;
};

class TopBar : public juce::Component, private juce::Timer, private juce::ChangeListener, private juce::ValueTree::Listener
{
public:
    explicit TopBar (AppContext& c);
    ~TopBar() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (const juce::String& menu, juce::Component* button)> onMenu;
    std::function<void (int view)> onShowView;
    void setActiveView (int v);
    void setStatus (const juce::String& s) { statusText = s; statusTime = juce::Time::getMillisecondCounter(); repaint(); }
    void refreshPatterns();

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshPatterns(); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& id) override;
    void valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree&) override { if (p.hasType (ids::PATTERNS)) refreshPatterns(); }
    void valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree&, int) override { if (p.hasType (ids::PATTERNS)) refreshPatterns(); }
    void valueTreeRedirected (juce::ValueTree&) override { refreshPatterns(); }
    void patternMenu();

    AppContext& ctx;
    juce::TextButton fileBtn { "FILE" }, editBtn { "EDIT" }, toolsBtn { "TOOLS" };
    IconButton playBtn { IconButton::play }, stopBtn { IconButton::stop }, recBtn { IconButton::record };
    juce::TextButton patModeBtn { "PAT" }, songModeBtn { "SONG" }, jamModeBtn { "JAM" };
    IconButton metroBtn { IconButton::metronome };
    DragNumber tempo { 20, 400, 1, 2 };
    juce::Slider swing { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::ComboBox patternBox;
    juce::TextButton patMenuBtn { "..." };
    juce::OwnedArray<juce::TextButton> viewBtns;
    juce::String statusText;
    juce::uint32 statusTime = 0;
    juce::Rectangle<int> timeArea, cpuArea, logoArea, meterArea, statusArea, transportArea;
    float meterL = 0, meterR = 0;
    bool refreshing = false;
};
}
