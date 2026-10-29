// Entry point shared by every Gubee VST3 plugin. CMake defines GUBEE_PLUGIN_CLASS per target.
#include "GubeeEffects.h"
#include "GubeeInstruments.h"
#include "GubeeDynamics.h"
#include "GubeeFX.h"
#include "GubeeSynths.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GUBEE_PLUGIN_CLASS();
}
