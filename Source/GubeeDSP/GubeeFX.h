#pragma once
#include "GubeeBase.h"

namespace gubee
{
//==============================================================================
/** Shared modulated-delay engine for chorus / flanger. */
class ModDelayProcessor : public GubeeProcessor
{
public:
    ModDelayProcessor (const juce::String& name, APVTS::ParameterLayout l, float baseMs, float maxDepthMs)
        : GubeeProcessor (name, false, std::move (l)), base (baseMs), maxDepth (maxDepthMs) {}

    void prepareToPlay (double s, int) override
    {
        sr = s;
        for (auto& d : lines) d.setSize ((int) (s * 0.06) + 8);
        phase = 0; fb[0] = fb[1] = 0;
    }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        const float rate = p ("rate"), depth = p ("depth"), mix = p ("mix"), feedback = p ("feedback");
        const int voices = (int) p ("voices");
        const float spread = p ("spread");
        const double inc = rate / sr;
        const int nc = juce::jmin (2, b.getNumChannels());
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float in[2] = { b.getSample (0, i), nc > 1 ? b.getSample (1, i) : b.getSample (0, i) };
            for (int c = 0; c < 2; ++c)
            {
                float wet = 0;
                for (int v = 0; v < voices; ++v)
                {
                    double ph = phase + v / (double) voices + (c == 1 ? spread * 0.25 : 0.0);
                    float lfo = 0.5f + 0.5f * (float) std::sin (ph * juce::MathConstants<double>::twoPi);
                    float dMs = base + maxDepth * depth * lfo;
                    wet += lines[(size_t) c].read (dMs * 0.001f * (float) sr);
                }
                wet /= (float) voices;
                lines[(size_t) c].push (in[c] + fb[c] * feedback);
                fb[c] = wet;
                if (c < nc) b.setSample (c, i, in[c] * (1.0f - mix * 0.5f) + wet * mix);
            }
            phase += inc; if (phase >= 1.0) phase -= 1.0;
        }
    }

protected:
    std::array<DelayLine, 2> lines;
    float base, maxDepth;
    double phase = 0;
    float fb[2] {};
};

class ChorusProcessor : public ModDelayProcessor
{
public:
    ChorusProcessor() : ModDelayProcessor ("Gubee Chorus", layout(), 8.0f, 14.0f) {}
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (fparam ("rate", "Chorus|Rate", 0.05f, 5, 0.6f, 1, "Hz"));
        l.add (fparam ("depth", "Chorus|Depth", 0, 1, 0.5f));
        l.add (iparam ("voices", "Chorus|Voices", 1, 4, 2));
        l.add (fparam ("spread", "Chorus|Stereo", 0, 1, 1));
        l.add (fparam ("feedback", "Chorus|Feedback", 0, 0.7f, 0));
        l.add (fparam ("mix", "Chorus|Mix", 0, 1, 0.5f));
        return l;
    }
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Wide Chorus", {} }, { "Lush Pads", { { "voices", 4 }, { "depth", 0.8f }, { "rate", 0.3f }, { "mix", 0.6f } } },
                 { "Subtle Doubler", { { "voices", 1 }, { "depth", 0.2f }, { "mix", 0.35f } } },
                 { "Vibrato", { { "voices", 1 }, { "depth", 0.4f }, { "rate", 5 }, { "mix", 1.0f } } } };
    }
};

class FlangerProcessor : public ModDelayProcessor
{
public:
    FlangerProcessor() : ModDelayProcessor ("Gubee Flanger", layout(), 0.6f, 6.0f) {}
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (fparam ("rate", "Flanger|Rate", 0.02f, 5, 0.25f, 0.5f, "Hz"));
        l.add (fparam ("depth", "Flanger|Depth", 0, 1, 0.7f));
        l.add (fparam ("feedback", "Flanger|Feedback", -0.95f, 0.95f, 0.6f));
        l.add (iparam ("voices", "Flanger|Voices", 1, 2, 1));
        l.add (fparam ("spread", "Flanger|Stereo", 0, 1, 0.5f));
        l.add (fparam ("mix", "Flanger|Mix", 0, 1, 0.5f));
        return l;
    }
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Jet Sweep", {} }, { "Metallic", { { "feedback", 0.9f }, { "rate", 0.1f } } },
                 { "Soft Swirl", { { "feedback", 0.2f }, { "depth", 0.5f }, { "mix", 0.4f } } } };
    }
};

//==============================================================================
class PhaserProcessor : public GubeeProcessor
{
public:
    PhaserProcessor() : GubeeProcessor ("Gubee Phaser", false, layout()) {}
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (fparam ("rate", "Phaser|Rate", 0.02f, 8, 0.4f, 0.8f, "Hz"));
        l.add (fparam ("depth", "Phaser|Depth", 0, 1, 0.7f));
        l.add (fparam ("centre", "Phaser|Centre", 200, 4000, 1000, 900, "Hz"));
        l.add (fparam ("feedback", "Phaser|Feedback", -0.95f, 0.95f, 0.5f));
        l.add (fparam ("mix", "Phaser|Mix", 0, 1, 0.5f));
        return l;
    }
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Classic Swirl", {} }, { "Slow Space", { { "rate", 0.08f }, { "depth", 1.0f }, { "feedback", 0.7f } } },
                 { "Funky Keys", { { "rate", 1.5f }, { "centre", 1400 }, { "feedback", 0.3f } } } };
    }
    void prepareToPlay (double s, int bs) override { sr = s; ph.prepare ({ s, (juce::uint32) bs, 2 }); ph.reset(); }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        ph.setRate (p ("rate")); ph.setDepth (p ("depth")); ph.setCentreFrequency (p ("centre"));
        ph.setFeedback (p ("feedback")); ph.setMix (p ("mix"));
        juce::dsp::AudioBlock<float> blk (b);
        auto sub = blk.getSubsetChannelBlock (0, (size_t) juce::jmin (2, b.getNumChannels()));
        ph.process (juce::dsp::ProcessContextReplacing<float> (sub));
    }
private:
    juce::dsp::Phaser<float> ph;
};

//==============================================================================
class TremoloProcessor : public GubeeProcessor
{
public:
    TremoloProcessor() : GubeeProcessor ("Gubee Tremolo", false, layout()) {}
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (cparam ("mode", "Tremolo|Mode", { "Tremolo", "Auto-Pan" }, 0));
        l.add (cparam ("shape", "Tremolo|Shape", { "Sine", "Triangle", "Square", "Saw Down" }, 0));
        l.add (bparam ("sync", "Tremolo|Tempo Sync", true));
        l.add (cparam ("division", "Tremolo|Division", syncDivisions(), 5));
        l.add (fparam ("rate", "Tremolo|Rate (free)", 0.1f, 20, 5, 4, "Hz"));
        l.add (fparam ("depth", "Tremolo|Depth", 0, 1, 0.6f));
        return l;
    }
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Amp Tremolo", { { "sync", 0 }, { "rate", 5.5f }, { "depth", 0.5f } } },
                 { "Auto-Pan 1/4", { { "mode", 1 }, { "division", 8 }, { "depth", 0.8f } } },
                 { "Choppy 1/16", { { "shape", 2 }, { "division", 2 }, { "depth", 1.0f } } } };
    }
    void prepareToPlay (double s, int) override { sr = s; phase = 0; }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        const bool sync = p ("sync") > 0.5f;
        const double beats = syncBeats ((int) p ("division"));
        const double rate = sync ? hostBpm (*this) / 60.0 / beats : p ("rate");
        if (sync) { const double hb = hostBeats (*this); if (hb >= 0) phase = std::fmod (hb, beats) / beats; }
        const int shape = (int) p ("shape"), mode = (int) p ("mode");
        const float depth = p ("depth");
        const int nc = b.getNumChannels();
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float w;
            switch (shape)
            {
                case 1: w = (float) (phase < 0.5 ? phase * 4 - 1 : 3 - phase * 4); break;
                case 2: w = phase < 0.5 ? 1.0f : -1.0f; break;
                case 3: w = (float) (1.0 - 2.0 * phase); break;
                default: w = (float) std::sin (phase * juce::MathConstants<double>::twoPi);
            }
            if (mode == 0 || nc < 2)
            {
                const float g = 1.0f - depth * (0.5f - 0.5f * w);
                for (int c = 0; c < nc; ++c) b.setSample (c, i, b.getSample (c, i) * g);
            }
            else
            {
                const float pan = w * depth;
                b.setSample (0, i, b.getSample (0, i) * juce::jmin (1.0f, 1.0f - pan));
                b.setSample (1, i, b.getSample (1, i) * juce::jmin (1.0f, 1.0f + pan));
            }
            phase += rate / sr; if (phase >= 1.0) phase -= 1.0;
        }
    }
private:
    double phase = 0;
};

//==============================================================================
/** Ring modulator / frequency shifter (Hilbert transform). */
class RingProcessor : public GubeeProcessor
{
public:
    RingProcessor() : GubeeProcessor ("Gubee Ring", false, layout()) {}
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (cparam ("mode", "Ring|Mode", { "Ring Mod", "Frequency Shift" }, 0));
        l.add (fparam ("freq", "Ring|Frequency", -2000, 2000, 120, -1, "Hz"));
        l.add (fparam ("mix", "Ring|Mix", 0, 1, 0.5f));
        return l;
    }
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Robot Bell", { { "freq", 440 } } }, { "Barber Pole Up", { { "mode", 1 }, { "freq", 3 }, { "mix", 1.0f } } },
                 { "Detuned Shift", { { "mode", 1 }, { "freq", -25 }, { "mix", 0.5f } } } };
    }
    void prepareToPlay (double s, int) override { sr = s; phase = 0; for (auto& h : hil) h = {}; }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        const bool shift = (int) p ("mode") == 1;
        const double f = p ("freq");
        const float mix = p ("mix");
        const int nc = juce::jmin (2, b.getNumChannels());
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double w = phase * juce::MathConstants<double>::twoPi;
            const float cs = (float) std::cos (w), sn = (float) std::sin (w);
            for (int c = 0; c < nc; ++c)
            {
                float x = b.getSample (c, i), y;
                if (shift) { float re, im; hil[(size_t) c].process (x, re, im); y = re * cs - im * sn; }
                else y = x * cs;
                b.setSample (c, i, x * (1.0f - mix) + y * mix);
            }
            phase += f / sr; phase -= std::floor (phase);
        }
    }
private:
    struct Hilbert
    {
        float a[4] { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
        float bco[4] { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
        float x1[4] {}, x2[4] {}, y1[4] {}, y2[4] {}, u1[4] {}, u2[4] {}, v1[4] {}, v2[4] {};
        float delayed = 0;
        void process (float in, float& re, float& im)
        {
            float s = in;
            for (int k = 0; k < 4; ++k) { float c = a[k] * a[k]; float o = c * (s + y2[k]) - x2[k]; x2[k] = x1[k]; x1[k] = s; y2[k] = y1[k]; y1[k] = o; s = o; }
            float t = in;
            for (int k = 0; k < 4; ++k) { float c = bco[k] * bco[k]; float o = c * (t + v2[k]) - u2[k]; u2[k] = u1[k]; u1[k] = t; v2[k] = v1[k]; v1[k] = o; t = o; }
            re = s; im = delayed; delayed = t;
        }
    };
    std::array<Hilbert, 2> hil;
    double phase = 0;
};

//==============================================================================
class DriveProcessor : public GubeeProcessor
{
public:
    DriveProcessor() : GubeeProcessor ("Gubee Drive", false, layout()) {}
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (cparam ("type", "Drive|Type", { "Overdrive", "Fuzz", "Tape Saturation", "Tube", "Foldback", "Rectify" }, 0));
        l.add (fparam ("drive", "Drive|Drive", 0, 40, 12, 12, "dB"));
        l.add (fparam ("bias", "Drive|Bias", -0.5f, 0.5f, 0));
        l.add (fparam ("tone", "Tone|Tone", 500, 20000, 8000, 3000, "Hz"));
        l.add (fparam ("lowcut", "Tone|Low Cut", 20, 600, 30, 100, "Hz"));
        l.add (fparam ("mix", "Output|Mix", 0, 1, 1));
        l.add (fparam ("out", "Output|Output", -24, 6, -6, -1, "dB"));
        return l;
    }
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Warm Overdrive", {} }, { "Fuzz Box", { { "type", 1 }, { "drive", 28 }, { "tone", 5000 } } },
                 { "Tape Warmth", { { "type", 2 }, { "drive", 6 }, { "out", -2 } } }, { "Tube Saturation", { { "type", 3 }, { "drive", 10 } } },
                 { "808 Distortion", { { "type", 0 }, { "drive", 20 }, { "tone", 6000 }, { "mix", 0.7f } } },
                 { "Wavefolder", { { "type", 4 }, { "drive", 18 } } } };
    }
    void prepareToPlay (double s, int bs) override
    {
        sr = s;
        os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
        os->initProcessing ((size_t) bs);
        dry.setSize (2, bs);
        for (auto& f : lp) f.reset();
        for (auto& f : hp) f.reset();
    }
    static float shape (int type, float x, float bias)
    {
        x += bias;
        switch (type)
        {
            case 1: return juce::jlimit (-1.0f, 1.0f, x > 0 ? x : x * 0.6f);
            case 2: return x / (1.0f + std::abs (x));
            case 3: return x >= 0 ? 1.0f - std::exp (-x) : -(1.0f - std::exp (x)) * 0.9f;
            case 4: { float y = std::sin (x * 1.2f); return y; }
            case 5: return std::abs (x) * 1.2f - 0.4f;
            default: return std::tanh (x);
        }
    }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        const int type = (int) p ("type"), nc = juce::jmin (2, b.getNumChannels()), n = b.getNumSamples();
        const float drive = juce::Decibels::decibelsToGain (p ("drive")), bias = p ("bias"), mix = p ("mix");
        const float out = juce::Decibels::decibelsToGain (p ("out"));
        if (dry.getNumSamples() < n) dry.setSize (2, n, false, false, true);
        for (int c = 0; c < nc; ++c) dry.copyFrom (c, 0, b, c, 0, n);
        auto hpC = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, p ("lowcut"));
        for (int c = 0; c < nc; ++c) { *hp[(size_t) c].coefficients = *hpC; auto* d = b.getWritePointer (c); for (int i = 0; i < n; ++i) d[i] = hp[(size_t) c].processSample (d[i]); }
        juce::dsp::AudioBlock<float> blk (b);
        auto sub = blk.getSubsetChannelBlock (0, (size_t) nc);
        auto up = os->processSamplesUp (sub);
        const float dcComp = shape (type, 0.0f, bias);
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            auto* d = up.getChannelPointer (c);
            for (size_t i = 0; i < up.getNumSamples(); ++i) d[i] = shape (type, d[i] * drive, bias) - dcComp;
        }
        os->processSamplesDown (sub);
        auto lpC = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, juce::jmin (p ("tone"), (float) sr * 0.45f));
        for (int c = 0; c < nc; ++c)
        {
            *lp[(size_t) c].coefficients = *lpC;
            auto* d = b.getWritePointer (c);
            auto* dr = dry.getReadPointer (c);
            for (int i = 0; i < n; ++i) d[i] = (lp[(size_t) c].processSample (d[i]) * mix + dr[i] * (1.0f - mix)) * out;
        }
    }
private:
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::AudioBuffer<float> dry;
    std::array<juce::dsp::IIR::Filter<float>, 2> lp, hp;
};

//==============================================================================
/** Guitar / bass amp with tone stack and speaker cabinet. */
class AmpProcessor : public GubeeProcessor
{
public:
    AmpProcessor() : GubeeProcessor ("Gubee Amp", false, layout()) {}
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (cparam ("model", "Amp|Model", { "Clean", "Crunch", "Lead", "Bass" }, 1));
        l.add (fparam ("gain", "Amp|Gain", 0, 1, 0.5f));
        l.add (fparam ("bass", "Tone|Bass", 0, 1, 0.5f));
        l.add (fparam ("mid", "Tone|Mid", 0, 1, 0.5f));
        l.add (fparam ("treble", "Tone|Treble", 0, 1, 0.5f));
        l.add (fparam ("presence", "Tone|Presence", 0, 1, 0.5f));
        l.add (cparam ("cab", "Cabinet|Cabinet", { "Off", "1x12 Combo", "4x12 Stack", "Bass 4x10" }, 1));
        l.add (fparam ("out", "Cabinet|Output", -24, 6, -6, -1, "dB"));
        return l;
    }
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Sparkle Clean", { { "model", 0 }, { "gain", 0.3f }, { "treble", 0.65f } } },
                 { "Classic Crunch", {} },
                 { "Searing Lead", { { "model", 2 }, { "gain", 0.8f }, { "cab", 2 }, { "mid", 0.65f } } },
                 { "Bass Amp", { { "model", 3 }, { "gain", 0.4f }, { "cab", 3 }, { "bass", 0.65f } } },
                 { "Lo-fi Radio", { { "model", 1 }, { "bass", 0.1f }, { "treble", 0.2f }, { "cab", 1 } } } };
    }
    void prepareToPlay (double s, int) override { sr = s; for (auto& ch : f) for (auto& x : ch) x.reset(); last.fill (-1); }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        const int model = (int) p ("model"), cab = (int) p ("cab");
        std::array<float, 7> now { (float) model, p ("bass"), p ("mid"), p ("treble"), p ("presence"), (float) cab, 0 };
        if (now != last)
        {
            last = now;
            using C = juce::dsp::IIR::Coefficients<float>;
            auto db = [] (float v) { return juce::Decibels::decibelsToGain ((v - 0.5f) * 24.0f); };
            const float midF = model == 3 ? 500.0f : 750.0f;
            C::Ptr cs[7] = {
                C::makeHighPass (sr, model == 3 ? 30.0f : 80.0f),
                C::makeLowShelf (sr, model == 3 ? 90.0f : 120.0f, 0.7f, db (now[1])),
                C::makePeakFilter (sr, midF, 0.8f, db (now[2])),
                C::makeHighShelf (sr, 2500.0f, 0.7f, db (now[3])),
                C::makePeakFilter (sr, juce::jmin (4500.0f, (float) sr * 0.4f), 1.0f, db (now[4])),
                cab == 0 ? C::makeAllPass (sr, 1000.0f) : C::makeLowPass (sr, cab == 3 ? 4200.0f : cab == 2 ? 5200.0f : 6500.0f, 0.9f),
                cab == 0 ? C::makeAllPass (sr, 1000.0f) : C::makePeakFilter (sr, cab == 2 ? 120.0f : cab == 3 ? 90.0f : 2000.0f, 1.2f, cab == 1 ? 1.6f : 2.0f) };
            for (auto& ch : f) for (int k = 0; k < 7; ++k) *ch[(size_t) k].coefficients = *cs[k];
        }
        const float g = p ("gain");
        const float pre = juce::Decibels::decibelsToGain (model == 0 ? g * 18.0f : model == 1 ? 6 + g * 24.0f : model == 2 ? 12 + g * 34.0f : g * 20.0f);
        const float out = juce::Decibels::decibelsToGain (p ("out"));
        const int nc = juce::jmin (2, b.getNumChannels());
        for (int c = 0; c < nc; ++c)
        {
            auto* d = b.getWritePointer (c);
            auto& fl = f[(size_t) c];
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                float x = fl[0].processSample (d[i]) * pre;
                x = std::tanh (x + 0.1f) - 0.0997f;                 // asymmetric preamp stage
                if (model == 2) x = std::tanh (x * 3.0f) * 0.8f;     // extra gain stage
                for (int k = 1; k < 5; ++k) x = fl[(size_t) k].processSample (x);
                x = std::tanh (x * 1.5f) / 1.5f * 1.4f;             // power amp
                x = fl[5].processSample (x);
                x = fl[6].processSample (x);
                d[i] = x * out;
            }
        }
    }
private:
    std::array<std::array<juce::dsp::IIR::Filter<float>, 7>, 2> f;
    std::array<float, 7> last {};
};

//==============================================================================
/** Vocoder with a built-in carrier synth (plays a fixed note / chord, or MIDI when hosted on an instrument path). */
class VocoderProcessor : public GubeeProcessor
{
public:
    static constexpr int bands = 20;
    VocoderProcessor() : GubeeProcessor ("Gubee Vocoder", false, layout(), true) { wantsMidi = true; }
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (iparam ("note", "Carrier|Note", 24, 84, 48));
        l.add (cparam ("chord", "Carrier|Chord", { "Single", "Fifth", "Major", "Minor", "Octaves" }, 3));
        l.add (cparam ("wave", "Carrier|Wave", { "Saw", "Square", "Noise", "Sidechain input" }, 0));
        l.add (fparam ("formant", "Voice|Formant", -12, 12, 0, -1, "st"));
        l.add (fparam ("release", "Voice|Release", 5, 300, 40, 40, "ms"));
        l.add (fparam ("noise", "Voice|Breath", 0, 1, 0.15f));
        l.add (fparam ("mix", "Voice|Mix", 0, 1, 1));
        l.add (fparam ("out", "Voice|Output", -12, 18, 0, -1, "dB"));
        return l;
    }
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Robot Choir", {} }, { "Daft Robot", { { "chord", 0 }, { "note", 45 } } },
                 { "Whisper Pad", { { "wave", 2 }, { "release", 120 } } }, { "Deep Voice", { { "formant", -6 }, { "note", 36 } } } };
    }
    void prepareToPlay (double s, int) override
    {
        sr = s;
        for (int k = 0; k < bands; ++k)
        {
            const float fc = 120.0f * std::pow (2.0f, k * std::log2 (8000.0f / 120.0f) / (bands - 1));
            centres[(size_t) k] = juce::jmin (fc, (float) s * 0.45f);
            env[(size_t) k] = 0;
            for (auto& x : mod[(size_t) k]) x.reset();
            for (auto& x : car[(size_t) k]) x.reset();
        }
        lastFormant = -99;
        phases.fill (0);
    }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals nd;
        for (const auto m : midi)
        {
            auto msg = m.getMessage();
            if (msg.isNoteOn()) { midiNote = msg.getNoteNumber(); midiHeld = true; }
            else if (msg.isNoteOff() && msg.getNoteNumber() == midiNote) midiHeld = false;
        }
        const float formant = p ("formant");
        if (formant != lastFormant)
        {
            lastFormant = formant;
            const float fs = std::pow (2.0f, formant / 12.0f);
            for (int k = 0; k < bands; ++k)
            {
                auto cm = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, centres[(size_t) k], 6.0f);
                auto cc = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, juce::jlimit (40.0f, (float) sr * 0.45f, centres[(size_t) k] * fs), 6.0f);
                for (auto& x : mod[(size_t) k]) *x.coefficients = *cm;
                for (auto& x : car[(size_t) k]) *x.coefficients = *cc;
            }
        }
        auto sc = sidechainBuffer (b);
        const int wave = (int) p ("wave"), chord = (int) p ("chord");
        const int root = midiHeld ? midiNote : (int) p ("note");
        static const int chords[5][3] = { { 0, 0, 0 }, { 0, 7, 12 }, { 0, 4, 7 }, { 0, 3, 7 }, { 0, 12, -12 } };
        const int nVoices = chord == 0 ? 1 : 3;
        double incs[3];
        for (int v = 0; v < 3; ++v) incs[v] = juce::MidiMessage::getMidiNoteInHertz (root + chords[chord][v]) / sr;
        const float rel = coefFor (p ("release") * 0.001, sr), att = coefFor (0.002, sr);
        const float noiseAmt = p ("noise"), mix = p ("mix"), out = juce::Decibels::decibelsToGain (p ("out"));
        const int nc = juce::jmin (2, b.getNumChannels());
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float modIn = 0;
            for (int c = 0; c < nc; ++c) modIn += b.getSample (c, i);
            modIn /= (float) nc;
            float carrier = 0;
            if (wave == 3 && sc.getNumChannels() > 0) carrier = sc.getSample (0, i);
            else if (wave == 2) carrier = rng.nextFloat() * 2 - 1;
            else
                for (int v = 0; v < nVoices; ++v)
                {
                    phases[(size_t) v] += incs[v]; if (phases[(size_t) v] >= 1) phases[(size_t) v] -= 1;
                    carrier += wave == 0 ? (float) (2 * phases[(size_t) v] - 1) : (phases[(size_t) v] < 0.5 ? 1.0f : -1.0f);
                }
            carrier = carrier / (float) nVoices + (rng.nextFloat() * 2 - 1) * noiseAmt * 0.3f;
            float y = 0;
            for (int k = 0; k < bands; ++k)
            {
                float m = mod[(size_t) k][1].processSample (mod[(size_t) k][0].processSample (modIn));
                float a = std::abs (m);
                env[(size_t) k] = a > env[(size_t) k] ? a + (env[(size_t) k] - a) * att : a + (env[(size_t) k] - a) * rel;
                float cBand = car[(size_t) k][1].processSample (car[(size_t) k][0].processSample (carrier));
                y += cBand * env[(size_t) k];
            }
            y *= 12.0f * out;
            for (int c = 0; c < nc; ++c) b.setSample (c, i, b.getSample (c, i) * (1.0f - mix) + y * mix);
        }
    }
private:
    std::array<float, bands> centres {}, env {};
    std::array<std::array<juce::dsp::IIR::Filter<float>, 2>, bands> mod, car;
    std::array<double, 3> phases {};
    float lastFormant = -99;
    int midiNote = 48; bool midiHeld = false;
    juce::Random rng;
};

//==============================================================================
class StereoProcessor : public GubeeProcessor
{
public:
    StereoProcessor() : GubeeProcessor ("Gubee Stereo", false, layout()) {}
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (fparam ("width", "Stereo|Width", 0, 2, 1, 1, "x"));
        l.add (fparam ("pan", "Stereo|Pan", -1, 1, 0));
        l.add (fparam ("haas", "Stereo|Haas Delay", 0, 30, 0, 10, "ms"));
        l.add (fparam ("monobass", "Stereo|Mono Below", 0, 300, 0, 120, "Hz"));
        l.add (bparam ("invl", "Utility|Invert L", false));
        l.add (bparam ("invr", "Utility|Invert R", false));
        l.add (bparam ("swap", "Utility|Swap L/R", false));
        l.add (fparam ("gain", "Utility|Gain", -24, 24, 0, -1, "dB"));
        return l;
    }
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Neutral", {} }, { "Extra Wide", { { "width", 1.6f } } }, { "Mono Check", { { "width", 0 } } },
                 { "Wide + Tight Low End", { { "width", 1.4f }, { "monobass", 140 } } }, { "Haas Spread", { { "haas", 12 } } } };
    }
    void prepareToPlay (double s, int) override { sr = s; for (auto& d : dl) d.setSize ((int) (s * 0.04) + 4); lp[0].reset(); lp[1].reset(); }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        if (b.getNumChannels() < 2) return;
        const float width = p ("width"), pan = p ("pan"), haas = p ("haas") * 0.001f * (float) sr;
        const float mb = p ("monobass"), g = juce::Decibels::decibelsToGain (p ("gain"));
        const bool il = p ("invl") > 0.5f, ir = p ("invr") > 0.5f, swap = p ("swap") > 0.5f;
        auto lpc = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, juce::jmax (20.0f, mb));
        *lp[0].coefficients = *lpc; *lp[1].coefficients = *lpc;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float l = b.getSample (0, i), r = b.getSample (1, i);
            if (swap) std::swap (l, r);
            if (il) l = -l;
            if (ir) r = -r;
            float m = (l + r) * 0.5f, s = (l - r) * 0.5f;
            if (mb > 1)
            {
                float sLow = lp[0].processSample (s);
                s -= sLow;
            }
            s *= width;
            l = m + s; r = m - s;
            dl[0].push (l); dl[1].push (r);
            if (haas > 0.5f) r = dl[1].read (haas);
            l *= juce::jmin (1.0f, 1.0f - pan) * g;
            r *= juce::jmin (1.0f, 1.0f + pan) * g;
            b.setSample (0, i, l); b.setSample (1, i, r);
        }
    }
private:
    std::array<DelayLine, 2> dl;
    std::array<juce::dsp::IIR::Filter<float>, 2> lp;
};

//==============================================================================
/** Two-tap granular pitch shifter (used by Pitch and Tune). */
struct GrainShifter
{
    DelayLine line;
    double ph = 0;
    int window = 2048;
    void prepare (double sr, double windowMs) { window = juce::jmax (256, (int) (sr * windowMs * 0.001)); line.setSize (window * 3); ph = 0; }
    float process (float x, double ratio)
    {
        line.push (x);
        ph += (1.0 - ratio) / window;
        ph -= std::floor (ph);
        const double ph2 = std::fmod (ph + 0.5, 1.0);
        const float d1 = (float) (ph * window) + 2.0f, d2 = (float) (ph2 * window) + 2.0f;
        const float w1 = (float) std::sin (ph * juce::MathConstants<double>::pi), w2 = (float) std::sin (ph2 * juce::MathConstants<double>::pi);
        return line.read (d1) * w1 * w1 + line.read (d2) * w2 * w2;
    }
};

class PitchProcessor : public GubeeProcessor
{
public:
    PitchProcessor() : GubeeProcessor ("Gubee Pitch", false, layout()) {}
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (fparam ("semis", "Pitch|Semitones", -24, 24, 0, -1, "st"));
        l.add (fparam ("cents", "Pitch|Fine", -100, 100, 0, -1, "ct"));
        l.add (fparam ("window", "Pitch|Grain", 20, 120, 50, 50, "ms"));
        l.add (fparam ("mix", "Pitch|Mix", 0, 1, 1));
        return l;
    }
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Octave Down", { { "semis", -12 } } }, { "Octave Up", { { "semis", 12 } } }, { "Fifth Harmony", { { "semis", 7 }, { "mix", 0.5f } } },
                 { "Chipmunk", { { "semis", 7 }, { "window", 30 } } }, { "Thicken (detune)", { { "cents", 12 }, { "mix", 0.5f } } } };
    }
    void prepareToPlay (double s, int) override { sr = s; lastWin = -1; }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        if (p ("window") != lastWin) { lastWin = p ("window"); for (auto& g : sh) g.prepare (sr, lastWin); }
        const double ratio = std::pow (2.0, (p ("semis") + p ("cents") / 100.0) / 12.0);
        const float mix = p ("mix");
        for (int c = 0; c < juce::jmin (2, b.getNumChannels()); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                float x = b.getSample (c, i);
                b.setSample (c, i, x * (1 - mix) + sh[(size_t) c].process (x, ratio) * mix);
            }
    }
private:
    std::array<GrainShifter, 2> sh;
    float lastWin = -1;
};

//==============================================================================
/** Pitch detector (YIN) shared by Tune and Scope. */
struct PitchDetector
{
    static float detect (const float* x, int n, double sr, float minHz = 60, float maxHz = 1200)
    {
        const int maxLag = juce::jmin (n / 2, (int) (sr / minHz)), minLag = juce::jmax (2, (int) (sr / maxHz));
        if (maxLag <= minLag + 2) return 0;
        std::vector<float> d ((size_t) maxLag + 1, 0.0f);
        const int w = n - maxLag;
        float energy = 0;
        for (int i = 0; i < w; ++i) energy += x[i] * x[i];
        if (energy < 1.0e-5f * w) return 0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            float s = 0;
            for (int i = 0; i < w; i += 2) { float diff = x[i] - x[i + tau]; s += diff * diff; }
            d[(size_t) tau] = s;
        }
        float run = 0;
        d[0] = 1;
        for (int tau = 1; tau <= maxLag; ++tau) { run += d[(size_t) tau]; d[(size_t) tau] = d[(size_t) tau] * tau / juce::jmax (1.0e-9f, run); }
        for (int tau = minLag; tau < maxLag; ++tau)
        {
            if (d[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < maxLag && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
                float a = d[(size_t) tau - 1], bb = d[(size_t) tau], c = d[(size_t) tau + 1];
                float denom = a - 2 * bb + c;
                float off = std::abs (denom) > 1e-9f ? 0.5f * (a - c) / denom : 0.0f;
                return (float) (sr / (tau + off));
            }
        }
        return 0;
    }
};

class TuneProcessor : public GubeeProcessor
{
public:
    TuneProcessor() : GubeeProcessor ("Gubee Tune", false, layout()) {}
    static juce::StringArray keys() { return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; }
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (cparam ("key", "Tune|Key", keys(), 0));
        l.add (cparam ("scale", "Tune|Scale", { "Chromatic", "Major", "Minor", "Major Pentatonic", "Minor Pentatonic" }, 2));
        l.add (fparam ("speed", "Tune|Retune Speed", 0, 200, 20, 40, "ms"));
        l.add (fparam ("humanize", "Tune|Humanize", 0, 1, 0.2f));
        l.add (fparam ("mix", "Tune|Mix", 0, 1, 1));
        return l;
    }
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Natural Correction", { { "speed", 60 }, { "humanize", 0.5f } } },
                 { "Hard Tune (robot)", { { "speed", 0 }, { "humanize", 0 } } },
                 { "C Major Pop", { { "scale", 1 }, { "speed", 25 } } } };
    }
    void prepareToPlay (double s, int) override
    {
        sr = s;
        for (auto& g : sh) g.prepare (s, 30);
        ana.assign (2048, 0.0f); anaPos = 0; ratio = 1; targetRatio = 1; counter = 0;
    }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        static const int sc[5][12] = { { 1,1,1,1,1,1,1,1,1,1,1,1 }, { 1,0,1,0,1,1,0,1,0,1,0,1 }, { 1,0,1,1,0,1,0,1,1,0,1,0 },
                                       { 1,0,1,0,1,0,0,1,0,1,0,0 }, { 1,0,0,1,0,1,0,1,0,0,1,0 } };
        const int key = (int) p ("key"), scale = (int) p ("scale");
        const float mix = p ("mix"), hum = p ("humanize");
        const float smooth = coefFor (juce::jmax (0.0005f, p ("speed") * 0.001f), sr);
        const int nc = juce::jmin (2, b.getNumChannels());
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float mono = 0;
            for (int c = 0; c < nc; ++c) mono += b.getSample (c, i);
            ana[(size_t) anaPos] = mono / (float) nc;
            anaPos = (anaPos + 1) % (int) ana.size();
            if (++counter >= 512)
            {
                counter = 0;
                std::vector<float> lin (ana.size());
                for (size_t k = 0; k < ana.size(); ++k) lin[k] = ana[(size_t) ((anaPos + (int) k) % (int) ana.size())];
                const float hz = PitchDetector::detect (lin.data(), (int) lin.size(), sr, 70, 1000);
                if (hz > 0)
                {
                    const float note = 69.0f + 12.0f * std::log2 (hz / 440.0f);
                    int best = (int) std::round (note);
                    for (int dd = 0; dd < 7; ++dd)
                    {
                        if (sc[scale][((best + dd - key) % 12 + 12) % 12]) { best = best + dd; break; }
                        if (sc[scale][((best - dd - key) % 12 + 12) % 12]) { best = best - dd; break; }
                    }
                    float diff = (float) best - note;
                    diff *= 1.0f - hum * juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (diff) * 2.0f);   // humanize: leave small deviations
                    targetRatio = std::pow (2.0, diff / 12.0);
                }
                else targetRatio = 1.0;
            }
            ratio = targetRatio + (ratio - targetRatio) * smooth;
            for (int c = 0; c < nc; ++c)
            {
                float x = b.getSample (c, i);
                b.setSample (c, i, x * (1 - mix) + sh[(size_t) c].process (x, ratio) * mix);
            }
        }
    }
private:
    std::array<GrainShifter, 2> sh;
    std::vector<float> ana;
    int anaPos = 0, counter = 0;
    double ratio = 1, targetRatio = 1;
};

//==============================================================================
/** Beat-repeat / trance-gate / tape-stop tool (tempo synced). */
class StutterProcessor : public GubeeProcessor
{
public:
    StutterProcessor() : GubeeProcessor ("Gubee Stutter", false, layout()) {}
    static juce::StringArray patterns() { return { "Off", "Straight 8ths", "Offbeats", "Trance 1", "Trance 2", "Dotted", "Triplet Feel", "Build-up", "Random" }; }
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (bparam ("repeat", "Repeat|Repeat On", false));
        l.add (cparam ("slice", "Repeat|Slice", syncDivisions(), 5));
        l.add (bparam ("reverse", "Repeat|Reverse", false));
        l.add (fparam ("pitchdrop", "Repeat|Pitch Drop", 0, 1, 0));
        l.add (cparam ("gate", "Gate|Gate Pattern", patterns(), 0));
        l.add (fparam ("gatedepth", "Gate|Depth", 0, 1, 1));
        l.add (fparam ("smooth", "Gate|Smooth", 0.5f, 30, 4, 5, "ms"));
        l.add (bparam ("tapestop", "Tape|Tape Stop", false));
        l.add (fparam ("stoptime", "Tape|Stop Time", 0.1f, 4, 1, 1, "beats"));
        l.add (fparam ("mix", "Tape|Mix", 0, 1, 1));
        return l;
    }
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Trance Gate", { { "gate", 3 } } }, { "Offbeat Chop", { { "gate", 2 } } },
                 { "1/16 Repeat (automate 'Repeat On')", { { "slice", 2 } } }, { "Build-up Gate", { { "gate", 7 } } },
                 { "Tape Stop (automate 'Tape Stop')", { { "stoptime", 2 } } } };
    }
    void prepareToPlay (double s, int) override
    {
        sr = s;
        hist.setSize (2, (int) (s * 8) + 16); hist.clear();
        w = 0; capturing = false; slicePos = 0; gateEnv = 1; tapeSpeed = 1; tapeRead = 0; tapeActive = false;
    }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        const int nc = juce::jmin (2, b.getNumChannels()), n = b.getNumSamples(), size = hist.getNumSamples();
        const double bpm = hostBpm (*this);
        const double spb = sr * 60.0 / bpm;
        double beat = hostBeats (*this);
        const bool beatValid = beat >= 0;
        if (! beatValid) beat = freeBeat;
        const bool rep = p ("repeat") > 0.5f, rev = p ("reverse") > 0.5f;
        const int sliceLen = juce::jmax (16, (int) (syncBeats ((int) p ("slice")) * spb));
        const float drop = p ("pitchdrop");
        const int gate = (int) p ("gate");
        const float gdepth = p ("gatedepth"), gsm = coefFor (p ("smooth") * 0.001, sr), mix = p ("mix");
        const bool tape = p ("tapestop") > 0.5f;
        const double stopSamples = p ("stoptime") * spb;
        for (int i = 0; i < n; ++i)
        {
            float in[2] = { b.getSample (0, i), nc > 1 ? b.getSample (1, i) : b.getSample (0, i) };
            for (int c = 0; c < 2; ++c) hist.setSample (c, w, in[c]);
            float y[2] = { in[0], in[1] };
            const double bt = beat + i / spb;

            // beat repeat
            if (rep)
            {
                if (! capturing) { capturing = true; sliceStart = w; slicePos = 0; repeatCount = 0; }
                int idx = rev ? sliceLen - 1 - (int) slicePos : (int) slicePos;
                if (repeatCount == 0) idx = (int) slicePos;   // first pass plays through live
                int rp = (sliceStart + idx) % size;
                for (int c = 0; c < 2; ++c) y[c] = hist.getSample (c, rp);
                double speed = std::pow (2.0, -drop * repeatCount * 0.25);
                slicePos += repeatCount == 0 ? 1.0 : speed;
                if (slicePos >= sliceLen) { slicePos = 0; ++repeatCount; }
                // de-click at slice edges
                float edge = (float) juce::jmin (1.0, juce::jmin (slicePos, sliceLen - slicePos) / 64.0);
                for (auto& v : y) v *= edge;
            }
            else capturing = false;

            // gate pattern
            if (gate > 0)
            {
                const int step = (int) std::floor (bt * 4.0) & 15;
                bool on;
                switch (gate)
                {
                    case 1: on = (step % 2) == 0; break;
                    case 2: on = (step % 4) == 2 || (step % 4) == 3; break;
                    case 3: on = ((0xb6dbu >> step) & 1) != 0; break;
                    case 4: on = ((0xeeeeu >> step) & 1) != 0; break;
                    case 5: on = ((0x9249u >> step) & 1) != 0; break;
                    case 6: on = ((0x6db6u >> step) & 1) != 0; break;
                    case 7: { const double bar = std::fmod (bt, 16.0) / 16.0; const int div = bar < 0.5 ? 4 : bar < 0.75 ? 8 : 16;
                              on = std::fmod (bt * div / 4.0, 1.0) < 0.5; break; }
                    default:
                    {
                        auto h = (juce::uint32) (std::floor (bt * 4.0)) * 2654435761u;
                        on = (h >> 13) & 1;
                    }
                }
                const float target = on ? 1.0f : 1.0f - gdepth;
                gateEnv = target + (gateEnv - target) * gsm;
                for (auto& v : y) v *= gateEnv;
            }

            // tape stop
            if (tape)
            {
                if (! tapeActive) { tapeActive = true; tapeSpeed = 1.0; tapeRead = (double) w; }
                tapeSpeed = juce::jmax (0.0, tapeSpeed - 1.0 / juce::jmax (1.0, stopSamples));
                tapeRead += tapeSpeed - 1.0;
                double rp = tapeRead; while (rp < 0) rp += size;
                int i0 = (int) rp % size; float fr = (float) (rp - std::floor (rp));
                for (int c = 0; c < 2; ++c) y[c] = (hist.getSample (c, i0) * (1 - fr) + hist.getSample (c, (i0 + 1) % size) * fr) * (float) juce::jmin (1.0, tapeSpeed * 4.0);
                tapeRead += 1.0;
            }
            else tapeActive = false;

            for (int c = 0; c < nc; ++c) b.setSample (c, i, in[c] * (1 - mix) + y[c] * mix);
            if (++w >= size) w = 0;
        }
        if (! beatValid) freeBeat += n / spb;
    }
private:
    juce::AudioBuffer<float> hist;
    int w = 0, sliceStart = 0, repeatCount = 0;
    double slicePos = 0, freeBeat = 0, tapeSpeed = 1, tapeRead = 0;
    bool capturing = false, tapeActive = false;
    float gateEnv = 1;
};

//==============================================================================
/** Analyzer: spectrum + oscilloscope + tuner. Passes audio through. */
class ScopeProcessor : public GubeeProcessor
{
public:
    ScopeProcessor() : GubeeProcessor ("Gubee Scope", false, layout()) {}
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (cparam ("view", "Display|View", { "Spectrum + Wave", "Spectrum", "Oscilloscope" }, 0));
        l.add (fparam ("smoothing", "Display|Smoothing", 0, 0.95f, 0.7f));
        return l;
    }
    void prepareToPlay (double s, int) override { sr = s; }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        const int nc = juce::jmin (2, b.getNumChannels());
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float m = 0;
            for (int c = 0; c < nc; ++c) m += b.getSample (c, i);
            ring[(size_t) (writePos.load() & (ringSize - 1))] = m / (float) juce::jmax (1, nc);
            writePos.fetch_add (1);
        }
    }
    double getTailLengthSeconds() const override { return 0; }
    static constexpr int ringSize = 8192;
    std::array<float, ringSize> ring {};
    std::atomic<int> writePos { 0 };
    double getRate() const { return sr; }

    std::unique_ptr<juce::Component> createVisual() override;
    int visualHeight() const override { return 300; }
};

class ScopeView : public juce::Component, private juce::Timer
{
public:
    explicit ScopeView (ScopeProcessor& p) : proc (p), fft (11) { spectrum.fill (-100.0f); startTimerHz (30); }
    void timerCallback() override
    {
        const int n = 2048;
        const int wp = proc.writePos.load();
        for (int i = 0; i < n; ++i) data[(size_t) i] = proc.ring[(size_t) ((wp - n + i) & (ScopeProcessor::ringSize - 1))];
        std::array<float, 4096> f {};
        for (int i = 0; i < n; ++i) f[(size_t) i] = data[(size_t) i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / n));
        fft.performFrequencyOnlyForwardTransform (f.data());
        const float sm = proc.p ("smoothing");
        for (int i = 0; i < n / 2; ++i)
        {
            float db = juce::Decibels::gainToDecibels (f[(size_t) i] / (n * 0.25f), -100.0f);
            spectrum[(size_t) i] = juce::jmax (db, spectrum[(size_t) i] * sm + db * (1 - sm));
        }
        hz = PitchDetector::detect (data.data(), n, proc.getRate(), 40, 2000);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::bg.darker (0.3f));
        g.fillRoundedRectangle (r, 8);
        const int view = (int) proc.p ("view");
        auto specArea = view == 2 ? juce::Rectangle<float>() : (view == 0 ? r.removeFromTop (r.getHeight() * 0.62f) : r);
        auto waveArea = view == 1 ? juce::Rectangle<float>() : r;
        const double sr = proc.getRate();
        if (! specArea.isEmpty())
        {
            auto a = specArea.reduced (8);
            g.setColour (col::outline);
            for (float fq : { 100.0f, 1000.0f, 10000.0f })
            {
                float x = a.getX() + a.getWidth() * std::log (fq / 20.0f) / std::log (20000.0f / 20.0f);
                g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
                g.setFont (juce::FontOptions (10.0f));
                g.drawText (fq >= 1000 ? juce::String (fq / 1000, 0) + "k" : juce::String (fq, 0), (int) x + 2, (int) a.getY(), 30, 12, juce::Justification::left);
            }
            juce::Path path;
            for (int x = 0; x < (int) a.getWidth(); ++x)
            {
                const float fq = 20.0f * std::pow (1000.0f, x / a.getWidth());
                const int bin = juce::jlimit (1, 1023, (int) (fq / sr * 2048));
                const float db = spectrum[(size_t) bin];
                const float y = a.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 90.0f) / 90.0f) * a.getHeight();
                if (x == 0) path.startNewSubPath (a.getX(), y); else path.lineTo (a.getX() + x, y);
            }
            juce::Path fill (path);
            fill.lineTo (a.getRight(), a.getBottom()); fill.lineTo (a.getX(), a.getBottom()); fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (col::accent.withAlpha (0.35f), 0, a.getY(), col::accent.withAlpha (0.02f), 0, a.getBottom(), false));
            g.fillPath (fill);
            g.setColour (col::accent);
            g.strokePath (path, juce::PathStrokeType (1.6f));
        }
        if (! waveArea.isEmpty())
        {
            auto a = waveArea.reduced (8);
            int start = 0;
            for (int i = 1; i < 1024; ++i) if (data[(size_t) i - 1] < 0 && data[(size_t) i] >= 0) { start = i; break; }
            juce::Path path;
            for (int x = 0; x < (int) a.getWidth(); ++x)
            {
                float v = data[(size_t) juce::jmin (2047, start + x * 1024 / juce::jmax (1, (int) a.getWidth()))];
                float y = a.getCentreY() - juce::jlimit (-1.0f, 1.0f, v) * a.getHeight() * 0.48f;
                if (x == 0) path.startNewSubPath (a.getX(), y); else path.lineTo (a.getX() + x, y);
            }
            g.setColour (col::accent2);
            g.strokePath (path, juce::PathStrokeType (1.4f));
        }
        // tuner + level
        float rms = 0;
        for (auto v : data) rms += v * v;
        rms = std::sqrt (rms / (float) data.size());
        g.setColour (col::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        juce::String note = "--";
        if (hz > 0)
        {
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            const float m = 69.0f + 12.0f * std::log2 (hz / 440.0f);
            const int nn = (int) std::round (m);
            note = juce::String (names[((nn % 12) + 12) % 12]) + juce::String (nn / 12 - 1) + "  " + juce::String ((m - nn) * 100.0f, 0) + " ct  (" + juce::String (hz, 1) + " Hz)";
        }
        g.drawText ("Note: " + note + "     Level: " + juce::String (juce::Decibels::gainToDecibels (rms, -100.0f), 1) + " dB RMS",
                    getLocalBounds().reduced (12, 8), juce::Justification::topRight);
    }
private:
    ScopeProcessor& proc;
    juce::dsp::FFT fft;
    std::array<float, 2048> data {};
    std::array<float, 1024> spectrum {};
    float hz = 0;
};

inline std::unique_ptr<juce::Component> ScopeProcessor::createVisual() { return std::make_unique<ScopeView> (*this); }
}
