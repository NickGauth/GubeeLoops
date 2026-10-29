#pragma once
#include "AppContext.h"
#include "TopBar.h"
#include "BrowserPanel.h"
#include "ChannelRack.h"
#include "PianoRoll.h"
#include "Playlist.h"
#include "MixerView.h"
#include "JamView.h"
#include "ChopShop.h"
#include "Windows.h"

namespace gubee
{
class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      private juce::ValueTree::Listener,
                      private juce::Timer
{
public:
    explicit MainComponent (AppContext& c);
    ~MainComponent() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    void showView (int v);
    void tryQuit (std::function<void (bool)> done);
    std::function<void (const juce::String&)> onTitleChange;

private:
    void menu (const juce::String& name, juce::Component* button);
    void fileNew();
    void fileOpen();
    void fileSave (bool saveAs, std::function<void()> after = {});
    void openWindow (const juce::String& key, const juce::String& title, std::unique_ptr<juce::Component> comp, bool resizable);
    void closeWindow (const juce::String& key);
    void openChannelEditor (int id);
    void openProcessorWindow (juce::AudioProcessor* p, const juce::String& key, const juce::String& title);
    void updateTitle();
    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { markDirty(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { markDirty(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { markDirty(); }
    void markDirty() { if (! ctx.project.dirty) { ctx.project.dirty = true; updateTitle(); } }

    AppContext& ctx;
    juce::TooltipWindow tooltips { this, 600 };
    TopBar topBar;
    BrowserPanel browser;
    std::unique_ptr<juce::Component> views[numViews];
    int currentView = viewRack;
    bool browserVisible = true;
    std::map<juce::String, std::unique_ptr<ToolWindow>> windows;
    std::unique_ptr<juce::FileChooser> chooser;

    // computer keyboard as a piano
    bool typingPiano = true;
    int typingOctave = 4;
    std::map<int, bool> typingDown;
    int lastAutosave = 0;
};
}
