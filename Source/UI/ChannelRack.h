#pragma once
#include "AppContext.h"

namespace gubee
{
class ChannelRack : public juce::Component,
                    public juce::DragAndDropTarget,
                    private juce::ValueTree::Listener,
                    private juce::ChangeListener,
                    private juce::AsyncUpdater,
                    private juce::Timer
{
public:
    explicit ChannelRack (AppContext& c);
    ~ChannelRack() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInDragSource (const SourceDetails&) override { return true; }
    void itemDropped (const SourceDetails&) override;

    AppContext& ctx;
    int stepWidth = 22;
    int numSteps() const;
    void addInstrument();

    struct Row;
private:
    void rebuild();
    void handleAsyncUpdate() override { rebuild(); }
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuild(); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeRedirected (juce::ValueTree&) override { triggerAsyncUpdate(); }

    juce::TextButton addBtn { "+ Add" }, drummerBtn { "Gubee Drummer" }, repeatBtn { "Note Repeat" }, clearBtn { "Clear" };
    juce::ComboBox repeatRate;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<Row> rows;
    std::set<int> collapsed;
public:
    std::unique_ptr<juce::FileChooser> chooser;
};

/** Generates drum patterns by genre (Gubee Drummer). */
void showDrummerDialog (AppContext& ctx, juce::Component* parent);
}
