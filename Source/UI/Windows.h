#pragma once
#include "AppContext.h"

namespace gubee
{
/** A floating window that owns a component. */
class ToolWindow : public juce::DocumentWindow
{
public:
    ToolWindow (const juce::String& title, juce::Component* content, bool resizable, std::function<void()> onClose);
    void closeButtonPressed() override { if (closeFn) closeFn(); }
private:
    std::function<void()> closeFn;
};

std::unique_ptr<juce::Component> makeDrumPadsEditor (AppContext& ctx, int channelId);
std::unique_ptr<juce::Component> makePluginManager (AppContext& ctx);
std::unique_ptr<juce::Component> makeAudioSettings (AppContext& ctx);
std::unique_ptr<juce::Component> makeExportPanel (AppContext& ctx, std::function<void()> onDone);
std::unique_ptr<juce::Component> makeModulatorsPanel (AppContext& ctx);
std::unique_ptr<juce::Component> makeMidiToolsPanel (AppContext& ctx, int channelId);
std::unique_ptr<juce::Component> makeMidiLearnPanel (AppContext& ctx);

/** Out-of-process plugin scanning so a crashing plugin can't take GubeeLoops down. */
class OutOfProcessScanner : public juce::KnownPluginList::CustomScanner
{
public:
    bool findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override;
};

/** Runs inside the child process launched with --gubee-scan. Returns exit code. */
int runPluginScanChild (const juce::StringArray& args);
}
