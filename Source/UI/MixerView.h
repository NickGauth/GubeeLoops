#pragma once
#include "AppContext.h"

namespace gubee
{
class MixerView : public juce::Component, private juce::ValueTree::Listener, private juce::Timer, private juce::AsyncUpdater
{
public:
    explicit MixerView (AppContext& c);
    ~MixerView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    struct Strip;
    AppContext& ctx;

private:
    void rebuild();
    void timerCallback() override;
    void handleAsyncUpdate() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree&, int) override;
    void valueTreeRedirected (juce::ValueTree&) override { rebuild(); }

    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<Strip> strips;
};
}
