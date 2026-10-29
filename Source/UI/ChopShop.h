#pragma once
#include "AppContext.h"

namespace gubee
{
class ChopShop : public juce::Component, public juce::DragAndDropTarget, private juce::Timer
{
public:
    explicit ChopShop (AppContext& c);
    ~ChopShop() override;
    void load (const juce::File& f, int channelId, juce::ValueTree clip);
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool isInterestedInDragSource (const SourceDetails&) override { return true; }
    void itemDropped (const SourceDetails&) override;

    struct Wave;

private:
    void timerCallback() override;
    void pushUndo();
    void edited();
    juce::Range<int> selOrAll() const;
    void detect();
    void playSlice (int i);
    void slicesToPads();
    void slicesToSampler();
    void exportSlices();
    juce::File saveWorking (const juce::String& suffix);
    void applyToSource();
    void stretchToTempo();
    void applyPitch();
    std::vector<juce::Range<int>> ranges() const;

    AppContext& ctx;
    juce::File source;
    int sourceChannel = -1;
    juce::ValueTree sourceClip;
    std::shared_ptr<SampleData> work;
    std::vector<juce::AudioBuffer<float>> undoStack;
    juce::AudioBuffer<float> clipboard;
    std::vector<int> markers;
    juce::Range<int> selection;
    int cursor = 0;
    double bpm = 0;
    bool lazyChop = false;

    std::unique_ptr<Wave> wave;
    juce::TextButton playBtn { "Play" }, stopBtn { "Stop" }, trimBtn { "Trim" }, delBtn { "Delete" }, silenceBtn { "Silence" },
                     revBtn { "Reverse" }, fadeInBtn { "Fade In" }, fadeOutBtn { "Fade Out" }, normBtn { "Normalize" },
                     gainUp { "+3 dB" }, gainDown { "-3 dB" }, copyBtn { "Copy" }, pasteBtn { "Paste" }, undoBtn { "Undo" },
                     stretchBtn { "Stretch to project tempo" }, pitchBtn { "Apply pitch" },
                     detectBtn { "Chop!" }, clearBtn { "Clear chops" }, lazyBtn { "Lazy Chop" },
                     toPads { "Slices -> Drum Pads" }, toSampler { "Slices -> Sampler keys" }, exportBtn { "Export slices" },
                     saveBtn { "Save as new sample" }, applyBtn { "Apply to source" }, openBtn { "Open file..." };
    juce::ComboBox modeBox, amountBox, pitchBox;
    juce::Slider sensitivity { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label bpmLabel;
    juce::TextEditor bpmEdit;
    juce::OwnedArray<juce::TextButton> pads;
    std::unique_ptr<juce::FileChooser> chooser;
};
}
