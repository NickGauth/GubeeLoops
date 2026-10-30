// All Gubee DSP is compiled in this one file so the rest of the app builds quickly.
#include "Engine.h"
#include "GubeeDSP/GubeeEffects.h"
#include "GubeeDSP/GubeeInstruments.h"
#include "GubeeDSP/GubeeDynamics.h"
#include "GubeeDSP/GubeeFX.h"
#include "GubeeDSP/GubeeSynths.h"

namespace gubee
{
std::unique_ptr<juce::AudioProcessor> BuiltinRegistry::create (const juce::String& n)
{
    if (n == "Gubee 808")     return std::make_unique<Bass808Processor>();
    if (n == "Gubee Synth")   return std::make_unique<SynthProcessor>();
    if (n == "Gubee EQ")      return std::make_unique<EQProcessor>();
    if (n == "Gubee Comp")    return std::make_unique<CompProcessor>();
    if (n == "Gubee Reverb")  return std::make_unique<ReverbProcessor>();
    if (n == "Gubee Delay")   return std::make_unique<DelayProcessor>();
    if (n == "Gubee Filter")  return std::make_unique<FilterProcessor>();
    if (n == "Gubee Crush")   return std::make_unique<CrushProcessor>();
    if (n == "Gubee Limiter") return std::make_unique<LimiterProcessor>();
    if (n == "Gubee Graphic EQ") return std::make_unique<GraphicEQProcessor>();
    if (n == "Gubee Multiband") return std::make_unique<MultibandProcessor>();
    if (n == "Gubee Clipper") return std::make_unique<ClipperProcessor>();
    if (n == "Gubee Gate") return std::make_unique<GateProcessor>();
    if (n == "Gubee Transient") return std::make_unique<TransientProcessor>();
    if (n == "Gubee Pump") return std::make_unique<PumpProcessor>();
    if (n == "Gubee Chorus") return std::make_unique<ChorusProcessor>();
    if (n == "Gubee Flanger") return std::make_unique<FlangerProcessor>();
    if (n == "Gubee Phaser") return std::make_unique<PhaserProcessor>();
    if (n == "Gubee Tremolo") return std::make_unique<TremoloProcessor>();
    if (n == "Gubee Ring") return std::make_unique<RingProcessor>();
    if (n == "Gubee Drive") return std::make_unique<DriveProcessor>();
    if (n == "Gubee Amp") return std::make_unique<AmpProcessor>();
    if (n == "Gubee Vocoder") return std::make_unique<VocoderProcessor>();
    if (n == "Gubee Stereo") return std::make_unique<StereoProcessor>();
    if (n == "Gubee Pitch") return std::make_unique<PitchProcessor>();
    if (n == "Gubee Tune") return std::make_unique<TuneProcessor>();
    if (n == "Gubee Stutter") return std::make_unique<StutterProcessor>();
    if (n == "Gubee Scope") return std::make_unique<ScopeProcessor>();
    if (n == "Gubee FM") return std::make_unique<FMProcessor>();
    if (n == "Gubee Wavetable") return std::make_unique<WavetableProcessor>();
    if (n == "Gubee Super") return std::make_unique<SuperProcessor>();
    if (n == "Gubee Keys") return std::make_unique<KeysProcessor>();
    if (n == "Gubee Organ") return std::make_unique<OrganProcessor>();
    if (n == "Gubee Pluck") return std::make_unique<PluckProcessor>();
    if (n == "Gubee Drum Synth") return std::make_unique<DrumSynthProcessor>();
    if (n == "Gubee Sampler") return std::make_unique<SamplerProcessor>();
    if (n == "Gubee Drum Pads") return std::make_unique<DrumPadsProcessor>();
    return nullptr;
}

}
