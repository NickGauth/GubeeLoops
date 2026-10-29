#pragma once
#include "PianoRoll.h"

namespace gubee
{
class Playlist : public juce::Component,
                 public juce::DragAndDropTarget,
                 private juce::ValueTree::Listener,
                 private juce::ChangeListener,
                 private juce::Timer
{
public:
    explicit Playlist (AppContext& c);
    ~Playlist() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInDragSource (const SourceDetails&) override { return true; }
    void itemDropped (const SourceDetails&) override;

    AppContext& ctx;
    double pxPerBeat = 22.0;
    int trackH = 44;
    enum Tool { drawTool, sliceTool, selectTool } tool = drawTool;
    double snap() const;
    juce::Array<juce::ValueTree> selection;
    juce::Array<juce::ValueTree> clipboard;
    void splitClip (juce::ValueTree clip, double atBeat);
    void clipMenu (juce::ValueTree clip);
    void emptyMenu (int track, double beat);
    void chopAtTransients (juce::ValueTree clip);
    void sliceToPads (juce::ValueTree clip);
    void repaintAll();

    struct Lane; struct Headers; struct Ruler;

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaintAll(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { updateSize(); repaintAll(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { repaintAll(); }
    void valueTreeRedirected (juce::ValueTree&) override { selection.clear(); updateSize(); repaintAll(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaintAll(); }
    void timerCallback() override;
    void updateSize();

    juce::TextButton drawBtn { "Draw" }, sliceBtn { "Slice" }, selectBtn { "Select" }, zoomIn { "+" }, zoomOut { "-" }, followBtn { "Follow" };
    juce::ComboBox snapBox;
    std::unique_ptr<Lane> lane;
    std::unique_ptr<Headers> headers;
    std::unique_ptr<Ruler> ruler;
    NotifyingViewport viewport;
    std::unique_ptr<juce::FileChooser> chooser;
};
}
