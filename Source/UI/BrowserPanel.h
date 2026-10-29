#pragma once
#include "AppContext.h"

namespace gubee
{
/** Drag payload prefix for files dragged inside the app. */
inline const juce::String dragFilePrefix = "gubee-file:";
inline juce::File fileFromDrag (const juce::var& d)
{
    auto s = d.toString();
    return s.startsWith (dragFilePrefix) ? juce::File (s.substring (dragFilePrefix.length())) : juce::File();
}

class BrowserPanel : public juce::Component,
                     private juce::ListBoxModel,
                     private juce::ChangeListener,
                     public juce::FileDragAndDropTarget
{
public:
    explicit BrowserPanel (AppContext& c);
    ~BrowserPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

    static void sendToPlaylist (AppContext& ctx, const juce::File& f, int track, double beat);

private:
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    void selectedRowsChanged (int) override {}
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refilter();
    void importMenu();
    void rowMenu (int row);
    void useSample (const SampleInfo& s);

    AppContext& ctx;
    juce::TextButton importBtn { "+ Import Pack" };
    juce::TextEditor search;
    juce::ComboBox typeBox, packBox;
    juce::ListBox list { "sounds", this };
    std::vector<SampleInfo> all, shown;
    juce::String lastStatus;
    bool dropHover = false;
    std::unique_ptr<juce::FileChooser> chooser;
};
}
