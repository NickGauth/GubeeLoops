#pragma once
#include <JuceHeader.h>
#include "Model/Project.h"
#include "Audio/Engine.h"
#include "Library/Library.h"
#include "App/Paths.h"

namespace gubee
{
enum ViewId { viewPlaylist = 0, viewRack, viewPiano, viewMixer, viewJam, viewChop, numViews };

/** Everything the UI needs, shared by all views. */
class AppContext : public juce::ChangeBroadcaster
{
public:
    AppContext();
    ~AppContext() override;

    Project project;
    SampleCache samples;
    PluginHost plugins;
    juce::AudioDeviceManager devices;
    Library library;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<juce::PropertiesFile> props;

    int selectedChannel() const { return selChannel; }
    void selectChannel (int id);
    int currentPattern() const { return engine->getCurrentPattern(); }
    void setCurrentPattern (int id);
    void ensureValidSelection();

    void newProject();
    bool openProject (const juce::File& f);
    bool saveProject (const juce::File& f);

    juce::ValueTree addSamplerChannel (const juce::File& f);
    juce::ValueTree addPadsChannel (const juce::String& name, bool withStarterKit);
    juce::ValueTree addPluginChannel (const juce::PluginDescription& d);

    // navigation hooks (set by MainComponent)
    std::function<void (const juce::String&)> setStatus;
    std::function<void (int)> showView;
    std::function<void (int)> openChannelEditor;
    std::function<void (const juce::File&, int, juce::ValueTree)> openInChopShop;
    std::function<void (int, int)> openSlotEditor;
    std::function<void (juce::AudioProcessor*, const juce::String&)> openProcessorWindow;
    std::function<void()> openPluginManager;

    void status (const juce::String& s) { if (setStatus) setStatus (s); }

private:
    int selChannel = -1;
};

/** A rotary knob bound to a ValueTree property (with undo). Double-click resets. */
class TreeKnob : public juce::Slider, private juce::ValueTree::Listener
{
public:
    TreeKnob (juce::UndoManager* um, double lo, double hi, double def, const juce::String& tip = {})
        : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox), undo (um), defaultValue (def)
    {
        setRange (lo, hi, 0.0);
        setDoubleClickReturnValue (true, def);
        setTooltip (tip);
        setPopupDisplayEnabled (true, true, nullptr);
        onDragStart = [this] { if (undo) undo->beginNewTransaction ("Knob"); };
        onValueChange = [this] { if (node.isValid() && ! updating) node.setProperty (prop, getValue(), undo); };
    }
    ~TreeKnob() override { node.removeListener (this); }

    void bind (juce::ValueTree n, const juce::Identifier& p)
    {
        node.removeListener (this);
        node = n; prop = p;
        node.addListener (this);
        refresh();
    }

private:
    void refresh()
    {
        const juce::ScopedValueSetter<bool> s (updating, true);
        setValue (node.isValid() ? (double) node.getProperty (prop, defaultValue) : defaultValue, juce::dontSendNotification);
    }
    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& i) override { if (t == node && i == prop) refresh(); }
    juce::UndoManager* undo;
    double defaultValue;
    juce::ValueTree node;
    juce::Identifier prop;
    bool updating = false;
};

/** Utility: show a text-entry prompt asynchronously. */
void askForText (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> onOk);

/** Utility: popup menu of all instruments (built-in + scanned plugins). Returns via callback. */
void showInstrumentMenu (AppContext& ctx, juce::Component* target, std::function<void (juce::String kind, juce::String builtin, juce::String pluginXml, juce::String name)> cb);
void showEffectMenu (AppContext& ctx, juce::Component* target, std::function<void (juce::String kind, juce::String builtin, juce::String pluginXml)> cb);
}
