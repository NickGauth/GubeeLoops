#pragma once
#include "AppContext.h"

namespace gubee
{
struct NotifyingViewport : public juce::Viewport
{
    std::function<void()> onMove;
    void visibleAreaChanged (const juce::Rectangle<int>&) override { if (onMove) onMove(); }
};

class PianoRoll : public juce::Component, private juce::ValueTree::Listener, private juce::ChangeListener, private juce::Timer
{
public:
    explicit PianoRoll (AppContext& c);
    ~PianoRoll() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    AppContext& ctx;
    double pxPerBeat = 120.0;
    int rowH = 14;
    double snap() const;
    bool inScale (int key) const;
    std::vector<int> chordFor (int rootKey) const;
    juce::String keyLabel (int key) const;

    struct Grid; struct Keys; struct Ruler; struct VelLane;
    juce::Array<juce::ValueTree> selection;
    double lastLength = 0.25;
    void quantizeSelectedOrAll();
    void insertProgression (int which);
    juce::ValueTree pattern() const { return ctx.project.findPattern (ctx.currentPattern()); }
    std::vector<juce::ValueTree> channelNotes() const;
    void repaintAll();

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaintAll(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { repaintAll(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { repaintAll(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { selection.clear(); updateSize(); repaintAll(); }
    void timerCallback() override;
    void updateSize();

    juce::ComboBox snapBox, scaleRoot, scaleType, chordBox;
    juce::TextButton quantBtn { "Quantize" }, progBtn { "Chord Progressions" }, zoomIn { "+" }, zoomOut { "-" };
    std::unique_ptr<Grid> grid;
    std::unique_ptr<Keys> keys;
    std::unique_ptr<Ruler> ruler;
    std::unique_ptr<VelLane> vel;
    NotifyingViewport viewport;
    friend struct Grid;
};
}
