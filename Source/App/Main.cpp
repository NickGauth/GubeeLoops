#include <JuceHeader.h>
#include "Audio/Engine.h"
class Stub : public juce::JUCEApplication { public:
 const juce::String getApplicationName() override { return "GubeeLoops"; }
 const juce::String getApplicationVersion() override { return "0.1"; }
 void initialise (const juce::String&) override {} void shutdown() override {} };
START_JUCE_APPLICATION (Stub)
