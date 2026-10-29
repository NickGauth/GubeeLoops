#pragma once
#include <JuceHeader.h>

namespace gubee::paths
{
/** D:\GubeeLoops when the exe lives in D:\GubeeLoops\App, otherwise the exe's folder. */
inline juce::File base()
{
    auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    if (exeDir.getFileName().equalsIgnoreCase ("App")) return exeDir.getParentDirectory();
    return exeDir;
}
inline juce::File sub (const char* name) { auto d = base().getChildFile (name); d.createDirectory(); return d; }
inline juce::File library()    { return sub ("Library"); }
inline juce::File packs()      { auto d = library().getChildFile ("Packs"); d.createDirectory(); return d; }
inline juce::File starterKit() { return packs().getChildFile ("Gubee Starter Kit"); }
inline juce::File projects()   { return sub ("Projects"); }
inline juce::File plugins()    { return sub ("Plugins"); }
inline juce::File settings()   { return sub ("Settings"); }
inline juce::File chops()      { auto d = library().getChildFile ("My Chops"); d.createDirectory(); return d; }
inline juce::File recordings() { auto d = library().getChildFile ("Recordings"); d.createDirectory(); return d; }
inline juce::File exports()    { return sub ("Exports"); }
}
