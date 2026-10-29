#pragma once
#include "AppContext.h"

namespace gubee
{
/** Ableton-style clip launcher: tracks x scenes of loops, launched on the next bar. */
class JamView : public juce::Component, public juce::DragAndDropTarget, private juce::ValueTree::Listener, private juce::Timer
{
public:
    explicit JamView (AppContext& c);
    ~JamView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool isInterestedInDragSource (const SourceDetails&) override { return true; }
    void itemDropped (const SourceDetails&) override;
    void itemDragMove (const SourceDetails& d) override { hover = d.localPosition; repaint(); }
    void itemDragExit (const SourceDetails&) override { hover = { -1, -1 }; repaint(); }

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaint(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { repaint(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { repaint(); }
    void valueTreeRedirected (juce::ValueTree&) override { repaint(); }
    void timerCallback() override { repaint(); }
    juce::Rectangle<int> cellRect (int track, int scene) const;
    bool cellAt (juce::Point<int> p, int& track, int& scene) const;
    juce::ValueTree jamTrack (int t) const;
    void emptyCellMenu (int t, int s);
    void filledCellMenu (int t, int s);

    AppContext& ctx;
    juce::TextButton stopAll { "Stop all" }, capture { "Capture jam to Playlist" };
    juce::Rectangle<int> grid;
    juce::Point<int> hover { -1, -1 };
    std::unique_ptr<juce::FileChooser> chooser;
};
}
