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
    juce::TextButton playBtn, stopBtn, recBtn;
    juce::TextButton patModeBtn { "PAT" }, songModeBtn { "SONG" }, jamModeBtn { "JAM" };
    juce::TextButton metroBtn { "METRO" };
    DragNumber tempo { 20, 400, 1, 2 };
    juce::Slider swing { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::ComboBox patternBox;
    juce::TextButton patMenuBtn { "..." };
    juce::OwnedArray<juce::TextButton> viewBtns;
    juce::String statusText;
    juce::uint32 statusTime = 0;
    juce::Rectangle<int> timeArea, cpuArea, logoArea, meterArea, statusArea;
    float meterL = 0, meterR = 0;
    bool refreshing = false;
};
}
