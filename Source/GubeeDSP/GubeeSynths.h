#pragma once
#include "GubeeBase.h"

namespace gubee
{
//==============================================================================
/** Base for polyphonic Gubee instruments: subclasses provide the voice. */
class PolySynthBase : public GubeeProcessor
{
public:
    PolySynthBase (const juce::String& name, APVTS::ParameterLayout l) : GubeeProcessor (name, true, std::move (l))
    {
        synth.addSound (new AnySound());
    }

    struct AnySound : juce::SynthesiserSound
    {
        bool appliesToNote (int) override { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    struct Voice : juce::SynthesiserVoice
    {
        explicit Voice (PolySynthBase& o) : owner (o) {}
        bool canPlaySound (juce::SynthesiserSound*) override { return true; }
        void startNote (int note, float v, juce::SynthesiserSound*, int) override
        {
            noteNum = note; vel = v; freq = juce::MidiMessage::getMidiNoteInHertz (note);
            env.setSampleRate (getSampleRate());
            env.setParameters (envParams());
            env.reset(); env.noteOn();
            onStart();
        }
        void stopNote (float, bool tail) override
        {
            if (tail) env.noteOff(); else { env.reset(); clearCurrentNote(); }
        }
        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}
        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
        {
            if (! isVoiceActive()) return;
            prepareBlock();
            for (int i = 0; i < num; ++i)
            {
                float l = 0, r = 0;
                renderSample (l, r);
                const float e = env.getNextSample() * vel;
                out.addSample (0, start + i, l * e);
                if (out.getNumChannels() > 1) out.addSample (1, start + i, r * e);
                if (! isVoiceActive()) break;
                if (! env.isActive()) { clearCurrentNote(); break; }
            }
        }
        virtual juce::ADSR::Parameters envParams() { return { 0.005f, 0.3f, 0.8f, 0.3f }; }
        virtual void onStart() {}
        virtual void prepareBlock() {}
        virtual void renderSample (float& l, float& r) = 0;

        PolySynthBase& owner;
        juce::ADSR env;
        double freq = 440;
        float vel = 1;
        int noteNum = 60;
    };

    void prepareToPlay (double s, int) override { sr = s; synth.setCurrentPlaybackSampleRate (s); }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals nd;
        b.clear();
        synth.renderNextBlock (b, midi, 0, b.getNumSamples());
        const float lvl = p ("level");
        b.applyGain (lvl);
    }

protected:
    juce::Synthesiser synth;
};

static inline float polyBlepS (double t, double dt)
{
    if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
    return 0.0f;
}

//==============================================================================
/** 4-operator FM synth. */
class FMProcessor : public PolySynthBase
{
public:
    FMProcessor() : PolySynthBase ("Gubee FM", layout()) { for (int i = 0; i < 12; ++i) synth.addVoice (new FMVoice (*this)); }

    static juce::StringArray algorithms()
    {
        return { "1: 4>3>2>1", "2: (3+4)>2>1", "3: 4>3>1 + 2>1", "4: 4>3 , 2>1 (two stacks)", "5: 4>(1,2,3)",
                 "6: 4>3>2 , 1", "7: 2>1 , 3 , 4", "8: all carriers (organ)" };
    }
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (cparam ("algo", "FM|Algorithm", algorithms(), 0));
        l.add (fparam ("feedback", "FM|Feedback", 0, 1, 0.1f));
        l.add (fparam ("level", "FM|Level", 0, 1, 0.5f));
        for (int o = 1; o <= 4; ++o)
        {
            const juce::String sec = "Op " + juce::String (o) + "|", id = "op" + juce::String (o);
            l.add (fparam (id + "ratio", sec + "Ratio", 0.5f, 16, o == 1 ? 1.0f : (float) o, 2));
            l.add (fparam (id + "level", sec + "Level", 0, 1, o == 1 ? 1.0f : 0.5f));
            l.add (fparam (id + "a", sec + "Attack", 0.001f, 4, 0.002f, 0.3f, "s"));
            l.add (fparam (id + "d", sec + "Decay", 0.01f, 6, 0.5f, 0.8f, "s"));
            l.add (fparam (id + "s", sec + "Sustain", 0, 1, o == 1 ? 0.8f : 0.4f));
            l.add (fparam (id + "r", sec + "Release", 0.01f, 6, 0.4f, 0.8f, "s"));
        }
        return l;
    }
    std::vector<Preset> factoryPresets() const override
    {
        return {
            { "Electric Piano", { { "algo", 3 }, { "op2ratio", 14 }, { "op2level", 0.3f }, { "op2d", 0.4f }, { "op2s", 0 }, { "op4ratio", 1 }, { "op4level", 0.6f }, { "op4d", 1.2f }, { "op4s", 0.1f }, { "op3ratio", 1 }, { "op1d", 2.5f }, { "op1s", 0.2f } } },
            { "FM Bass", { { "algo", 0 }, { "op2ratio", 1 }, { "op2level", 0.7f }, { "op2d", 0.25f }, { "op2s", 0.2f }, { "op3ratio", 2 }, { "op3level", 0.3f }, { "op4level", 0.0f }, { "feedback", 0.3f } } },
            { "Bell", { { "algo", 3 }, { "op2ratio", 3.5f }, { "op2level", 0.6f }, { "op2d", 3 }, { "op2s", 0 }, { "op1d", 4 }, { "op1s", 0 }, { "op1r", 3 }, { "op4ratio", 7 }, { "op4level", 0.3f } } },
            { "Brass", { { "algo", 0 }, { "op2ratio", 1 }, { "op2level", 0.6f }, { "op2a", 0.08f }, { "op1a", 0.05f }, { "op3level", 0.2f }, { "op4level", 0.0f } } },
            { "Organ", { { "algo", 7 }, { "op2ratio", 2 }, { "op3ratio", 3 }, { "op4ratio", 4 }, { "op2level", 0.6f }, { "op3level", 0.4f }, { "op4level", 0.3f }, { "op1s", 1 }, { "op2s", 1 }, { "op3s", 1 }, { "op4s", 1 } } } };
    }

private:
    struct FMVoice : Voice
    {
        explicit FMVoice (FMProcessor& o) : Voice (o), fm (o) {}
        juce::ADSR::Parameters envParams() override { return { 0.001f, 0.1f, 1.0f, fm.p ("op1r") }; }
        void onStart() override
        {
            for (int k = 0; k < 4; ++k)
            {
                const juce::String id = "op" + juce::String (k + 1);
                ops[k].env.setSampleRate (getSampleRate());
                ops[k].env.setParameters ({ fm.p ((id + "a").toRawUTF8()), fm.p ((id + "d").toRawUTF8()), fm.p ((id + "s").toRawUTF8()), fm.p ((id + "r").toRawUTF8()) });
                ops[k].env.reset(); ops[k].env.noteOn();
                ops[k].phase = 0;
            }
            fbState = 0;
        }
        void stopNote (float v, bool tail) override { for (auto& o : ops) o.env.noteOff(); Voice::stopNote (v, tail); }
        void prepareBlock() override
        {
            algo = (int) fm.p ("algo");
            fb = fm.p ("feedback");
            for (int k = 0; k < 4; ++k)
            {
                const juce::String id = "op" + juce::String (k + 1);
                ops[k].inc = freq * fm.p ((id + "ratio").toRawUTF8()) / getSampleRate();
                ops[k].level = fm.p ((id + "level").toRawUTF8());
            }
        }
        float op (int k, float mod)
        {
            auto& o = ops[k];
            float y = (float) std::sin ((o.phase + mod) * juce::MathConstants<double>::twoPi) * o.env.getNextSample() * o.level;
            o.phase += o.inc; if (o.phase >= 1) o.phase -= 1;
            return y;
        }
        void renderSample (float& l, float& r) override
        {
            const float I = 2.0f;   // modulation index scale
            float o4 = op (3, fbState * fb * 0.5f);
            fbState = o4;
            float out = 0;
            switch (algo)
            {
                case 0: { float o3 = op (2, o4 * I); float o2 = op (1, o3 * I); out = op (0, o2 * I); break; }
                case 1: { float o3 = op (2, 0); float o2 = op (1, (o3 + o4) * I); out = op (0, o2 * I); break; }
                case 2: { float o3 = op (2, o4 * I); float o2 = op (1, 0); out = op (0, (o3 + o2) * I); break; }
                case 3: { float o3 = op (2, o4 * I); float o2 = op (1, 0); out = (op (0, o2 * I) + o3) * 0.6f; break; }
                case 4: { out = (op (0, o4 * I) + op (1, o4 * I) + op (2, o4 * I)) * 0.4f; break; }
                case 5: { float o3 = op (2, o4 * I); out = (op (1, o3 * I) + op (0, 0)) * 0.6f; break; }
                case 6: { float o2 = op (1, 0); out = (op (0, o2 * I) + op (2, 0) + o4) * 0.45f; break; }
                default: { out = (op (0, 0) + op (1, 0) + op (2, 0) + o4) * 0.35f; break; }
            }
            l = r = out * 0.5f;
        }
        struct Op { double phase = 0, inc = 0; float level = 1; juce::ADSR env; };
        FMProcessor& fm;
        Op ops[4];
        int algo = 0;
        float fb = 0, fbState = 0;
    };
};

//==============================================================================
/** Wavetable synth with built-in morphing tables, unison and filter. */
class WavetableProcessor : public PolySynthBase
{
public:
    static constexpr int tableSize = 2048, frames = 16, mips = 8;
    WavetableProcessor() : PolySynthBase ("Gubee Wavetable", layout())
    {
        buildTables();
        for (int i = 0; i < 10; ++i) synth.addVoice (new WTVoice (*this));
    }
    static juce::StringArray tables() { return { "Basic Shapes", "Pulse Width", "Formant Vowels", "Harmonic Sweep", "Digital Bells" }; }
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (cparam ("table", "Oscillator|Wavetable", tables(), 0));
        l.add (fparam ("pos", "Oscillator|Position", 0, 1, 0.3f));
        l.add (fparam ("posenv", "Oscillator|Position Env", -1, 1, 0));
        l.add (fparam ("poslfo", "Oscillator|Position LFO", 0, 1, 0));
        l.add (fparam ("lforate", "Oscillator|LFO Rate", 0.05f, 10, 0.5f, 1, "Hz"));
        l.add (iparam ("unison", "Unison|Voices", 1, 7, 3));
        l.add (fparam ("detune", "Unison|Detune", 0, 1, 0.25f));
        l.add (fparam ("width", "Unison|Width", 0, 1, 0.8f));
        l.add (fparam ("cutoff", "Filter|Cutoff", 40, 18000, 8000, 1500, "Hz"));
        l.add (fparam ("reso", "Filter|Resonance", 0.5f, 8, 0.8f, 2));
        l.add (fparam ("fenv", "Filter|Env Amount", 0, 1, 0.2f));
        l.add (fparam ("attack", "Envelope|Attack", 0.001f, 4, 0.01f, 0.3f, "s"));
        l.add (fparam ("decay", "Envelope|Decay", 0.01f, 4, 0.5f, 0.5f, "s"));
        l.add (fparam ("sustain", "Envelope|Sustain", 0, 1, 0.7f));
        l.add (fparam ("release", "Envelope|Release", 0.01f, 6, 0.4f, 0.6f, "s"));
        l.add (fparam ("level", "Envelope|Level", 0, 1, 0.4f));
        return l;
    }
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Init", {} },
                 { "Evolving Pad", { { "table", 3 }, { "poslfo", 0.6f }, { "lforate", 0.15f }, { "unison", 5 }, { "attack", 1.2f }, { "release", 2.5f }, { "cutoff", 4000 } } },
                 { "Vowel Lead", { { "table", 2 }, { "posenv", 0.8f }, { "unison", 2 }, { "decay", 0.8f } } },
                 { "Wide Supersaw-ish", { { "table", 0 }, { "pos", 0.66f }, { "unison", 7 }, { "detune", 0.45f } } },
                 { "Digital Keys", { { "table", 4 }, { "pos", 0.4f }, { "posenv", -0.5f }, { "unison", 1 }, { "sustain", 0.2f } } } };
    }

    const float* frame (int table, int f, int mip) const { return tables_[(size_t) table][(size_t) f][(size_t) mip].data(); }

private:
    void buildTables()
    {
        tables_.resize (5);
        for (int t = 0; t < 5; ++t)
        {
            tables_[(size_t) t].resize (frames);
            for (int f = 0; f < frames; ++f)
            {
                const float x = f / (float) (frames - 1);
                std::vector<float> amp (513, 0.0f), ph (513, 0.0f);
                for (int h = 1; h <= 512; ++h)
                {
                    float a = 0;
                    switch (t)
                    {
                        case 0: // sine -> triangle -> saw -> square
                        {
                            float sine = h == 1 ? 1.0f : 0.0f;
                            float tri = (h % 2) ? 8.0f / (juce::MathConstants<float>::pi * juce::MathConstants<float>::pi * h * h) * ((h / 2) % 2 ? -1.0f : 1.0f) : 0.0f;
                            float saw = 2.0f / (juce::MathConstants<float>::pi * h);
                            float sq = (h % 2) ? 4.0f / (juce::MathConstants<float>::pi * h) : 0.0f;
                            float seg = x * 3.0f;
                            a = seg < 1 ? sine + (tri - sine) * seg : seg < 2 ? tri + (saw - tri) * (seg - 1) : saw + (sq - saw) * (seg - 2);
                            break;
                        }
                        case 1: { const float pw = 0.5f - x * 0.45f; a = 2.0f / (juce::MathConstants<float>::pi * h) * std::sin (juce::MathConstants<float>::pi * h * pw); break; }
                        case 2:
                        {
                            const float f1[] = { 800, 400, 300, 600, 350 }, f2[] = { 1200, 2200, 2600, 1000, 900 };
                            float pos = x * 4.0f; int i0 = juce::jmin (3, (int) pos); float fr = pos - i0;
                            float F1 = f1[i0] + (f1[i0 + 1] - f1[i0]) * fr, F2 = f2[i0] + (f2[i0 + 1] - f2[i0]) * fr;
                            float hz = h * 110.0f;
                            a = (std::exp (-std::pow ((hz - F1) / 120.0f, 2.0f)) + 0.6f * std::exp (-std::pow ((hz - F2) / 180.0f, 2.0f)) + 0.02f) / (float) std::sqrt ((double) h);
                            break;
                        }
                        case 3: { const float n = 1 + x * 40; a = h <= n ? 1.0f / std::pow ((float) h, 0.7f) : 0.0f; break; }
                        default: { const int partials[] = { 1, 3, 5, 7, 9, 11, 13 }; for (int k = 0; k < 7; ++k) if (h == partials[k] * (1 + (int) (x * 3))) a = 1.0f / (k + 1); if (h == 1) a = 1; break; }
                    }
                    amp[(size_t) h] = a;
                }
                tables_[(size_t) t][(size_t) f].resize (mips);
                for (int m = 0; m < mips; ++m)
                {
                    auto& tab = tables_[(size_t) t][(size_t) f][(size_t) m];
                    tab.assign (tableSize + 1, 0.0f);
                    const int maxH = juce::jmax (1, 512 >> m);
                    for (int h = 1; h <= maxH; ++h)
                    {
                        if (amp[(size_t) h] == 0) continue;
                        for (int i = 0; i < tableSize; ++i)
                            tab[(size_t) i] += amp[(size_t) h] * std::sin (juce::MathConstants<float>::twoPi * h * i / tableSize);
                    }
                    float peak = 0;
                    for (auto v : tab) peak = juce::jmax (peak, std::abs (v));
                    if (peak > 0) for (auto& v : tab) v /= peak;
                    tab[tableSize] = tab[0];
                }
            }
        }
    }

    struct WTVoice : Voice
    {
        explicit WTVoice (WavetableProcessor& o) : Voice (o), wt (o) {}
        juce::ADSR::Parameters envParams() override { return { wt.p ("attack"), wt.p ("decay"), wt.p ("sustain"), wt.p ("release") }; }
        void onStart() override
        {
            juce::Random r;
            for (auto& ph : phases) ph = r.nextDouble();
            filt.prepare ({ getSampleRate(), 512, 2 });
            filt.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
            filt.reset();
            fenv.setSampleRate (getSampleRate());
            fenv.setParameters ({ wt.p ("attack"), wt.p ("decay") * 1.5f, 0.0f, wt.p ("release") });
            fenv.reset(); fenv.noteOn();
            lfoPh = 0; counter = 0;
        }
        void stopNote (float v, bool t) override { fenv.noteOff(); Voice::stopNote (v, t); }
        void prepareBlock() override
        {
            table = (int) wt.p ("table"); unison = (int) wt.p ("unison");
            detune = wt.p ("detune"); width = wt.p ("width");
            pos = wt.p ("pos"); posEnv = wt.p ("posenv"); posLfo = wt.p ("poslfo"); lfoRate = wt.p ("lforate");
            cutoff = wt.p ("cutoff"); fAmt = wt.p ("fenv");
            filt.setResonance (wt.p ("reso"));
            const double baseInc = freq / getSampleRate();
            mip = juce::jlimit (0, mips - 1, (int) std::floor (std::log2 (juce::jmax (1.0, freq * 1024.0 / getSampleRate() * 2.0 / 2.0))) );
            for (int u = 0; u < unison; ++u)
            {
                const float spread = unison > 1 ? (u / (float) (unison - 1) - 0.5f) * 2.0f : 0.0f;
                incs[u] = baseInc * std::pow (2.0, spread * detune * 0.5 / 12.0);
                pans[u] = spread * width;
            }
        }
        void renderSample (float& l, float& r) override
        {
            const float fe = fenv.getNextSample();
            lfoPh += lfoRate / getSampleRate(); if (lfoPh >= 1) lfoPh -= 1;
            float ps = juce::jlimit (0.0f, 0.999f, pos + posEnv * fe + posLfo * 0.5f * (float) std::sin (lfoPh * juce::MathConstants<double>::twoPi));
            const float fpos = ps * (frames - 1);
            const int f0 = (int) fpos; const float ff = fpos - f0;
            const float* A = wt.frame (table, f0, mip);
            const float* B = wt.frame (table, juce::jmin (frames - 1, f0 + 1), mip);
            l = r = 0;
            for (int u = 0; u < unison; ++u)
            {
                const double idx = phases[u] * tableSize;
                const int i0 = (int) idx; const float fr = (float) (idx - i0);
                const float a = A[i0] + fr * (A[i0 + 1] - A[i0]), b = B[i0] + fr * (B[i0 + 1] - B[i0]);
                const float s = a + (b - a) * ff;
                l += s * juce::jmin (1.0f, 1.0f - pans[u]);
                r += s * juce::jmin (1.0f, 1.0f + pans[u]);
                phases[u] += incs[u]; if (phases[u] >= 1) phases[u] -= 1;
            }
            const float norm = 1.0f / std::sqrt ((float) unison);
            if ((counter++ & 15) == 0)
                filt.setCutoffFrequency (juce::jlimit (30.0f, (float) getSampleRate() * 0.45f, cutoff * std::pow (2.0f, fe * fAmt * 6.0f)));
            l = filt.processSample (0, l * norm) * 0.6f;
            r = filt.processSample (1, r * norm) * 0.6f;
        }
        WavetableProcessor& wt;
        double phases[7] {}, incs[7] {};
        float pans[7] {};
        int table = 0, unison = 1, mip = 0, counter = 0;
        float detune = 0, width = 0, pos = 0, posEnv = 0, posLfo = 0, lfoRate = 1, cutoff = 8000, fAmt = 0;
        double lfoPh = 0;
        juce::ADSR fenv;
        juce::dsp::StateVariableTPTFilter<float> filt;
    };

    std::vector<std::vector<std::vector<std::vector<float>>>> tables_;
};

//==============================================================================
/** Supersaw for big pads, leads and plucks. */
class SuperProcessor : public PolySynthBase
{
public:
    SuperProcessor() : PolySynthBase ("Gubee Super", layout()) { for (int i = 0; i < 10; ++i) synth.addVoice (new SVoice (*this)); }
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (fparam ("detune", "Saws|Detune", 0, 1, 0.35f));
        l.add (fparam ("mixs", "Saws|Side Saws", 0, 1, 0.7f));
        l.add (fparam ("width", "Saws|Stereo", 0, 1, 0.9f));
        l.add (fparam ("sub", "Saws|Sub Osc", 0, 1, 0.2f));
        l.add (fparam ("cutoff", "Filter|Cutoff", 60, 18000, 6000, 1500, "Hz"));
        l.add (fparam ("reso", "Filter|Resonance", 0.5f, 6, 0.8f, 2));
        l.add (fparam ("fenv", "Filter|Env Amount", 0, 1, 0.3f));
        l.add (fparam ("attack", "Envelope|Attack", 0.001f, 4, 0.02f, 0.3f, "s"));
        l.add (fparam ("decay", "Envelope|Decay", 0.01f, 4, 0.6f, 0.6f, "s"));
        l.add (fparam ("sustain", "Envelope|Sustain", 0, 1, 0.8f));
        l.add (fparam ("release", "Envelope|Release", 0.01f, 6, 0.5f, 0.6f, "s"));
        l.add (fparam ("level", "Envelope|Level", 0, 1, 0.35f));
        return l;
    }
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Trance Lead", {} }, { "Huge Pad", { { "attack", 1.0f }, { "release", 2.5f }, { "cutoff", 3500 }, { "detune", 0.5f } } },
                 { "Pluck", { { "sustain", 0 }, { "decay", 0.35f }, { "fenv", 0.7f }, { "cutoff", 800 } } },
                 { "Hoover-ish", { { "detune", 0.9f }, { "sub", 0.6f }, { "cutoff", 2500 } } } };
    }
private:
    struct SVoice : Voice
    {
        explicit SVoice (SuperProcessor& o) : Voice (o), sp (o) {}
        juce::ADSR::Parameters envParams() override { return { sp.p ("attack"), sp.p ("decay"), sp.p ("sustain"), sp.p ("release") }; }
        void onStart() override
        {
            juce::Random r; for (auto& ph : phases) ph = r.nextDouble();
            subPh = 0;
            filt.prepare ({ getSampleRate(), 512, 2 }); filt.setType (juce::dsp::StateVariableTPTFilterType::lowpass); filt.reset();
            fenv.setSampleRate (getSampleRate()); fenv.setParameters ({ 0.002f, sp.p ("decay"), 0.0f, sp.p ("release") }); fenv.reset(); fenv.noteOn();
        }
        void stopNote (float v, bool t) override { fenv.noteOff(); Voice::stopNote (v, t); }
        void prepareBlock() override
        {
            static const float offs[7] = { -0.11002f, -0.06288f, -0.01952f, 0.0f, 0.01991f, 0.06216f, 0.10745f };
            const float d = sp.p ("detune");
            for (int k = 0; k < 7; ++k) incs[k] = freq * (1.0 + offs[k] * d) / getSampleRate();
            side = sp.p ("mixs"); width = sp.p ("width"); sub = sp.p ("sub");
            cutoff = sp.p ("cutoff"); famt = sp.p ("fenv");
            filt.setResonance (sp.p ("reso"));
        }
        void renderSample (float& l, float& r) override
        {
            l = r = 0;
            for (int k = 0; k < 7; ++k)
            {
                float s = (float) (2 * phases[k] - 1) - polyBlepS (phases[k], incs[k]);
                const float g = k == 3 ? 1.0f : side;
                const float pan = (k - 3) / 3.0f * width;
                l += s * g * juce::jmin (1.0f, 1.0f - pan);
                r += s * g * juce::jmin (1.0f, 1.0f + pan);
                phases[k] += incs[k]; if (phases[k] >= 1) phases[k] -= 1;
            }
            subPh += incs[3] * 0.5; if (subPh >= 1) subPh -= 1;
            const float sb = (float) std::sin (subPh * juce::MathConstants<double>::twoPi) * sub * 2.0f;
            const float fe = fenv.getNextSample();
            if ((cnt++ & 15) == 0) filt.setCutoffFrequency (juce::jlimit (30.0f, (float) getSampleRate() * 0.45f, cutoff * std::pow (2.0f, fe * famt * 6.0f)));
            const float norm = 0.25f;
            l = filt.processSample (0, l * norm + sb * 0.3f);
            r = filt.processSample (1, r * norm + sb * 0.3f);
        }
        SuperProcessor& sp;
        double phases[7] {}, incs[7] {}, subPh = 0;
        float side = 0.7f, width = 0.9f, sub = 0, cutoff = 6000, famt = 0;
        int cnt = 0;
        juce::ADSR fenv;
        juce::dsp::StateVariableTPTFilter<float> filt;
    };
};

//==============================================================================
/** Keys: electric piano (tine / reed), clav and a soft piano. */
class KeysProcessor : public PolySynthBase
{
public:
    KeysProcessor() : PolySynthBase ("Gubee Keys", layout()) { for (int i = 0; i < 16; ++i) synth.addVoice (new KVoice (*this)); }
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (cparam ("model", "Keys|Model", { "Tine EP", "Reed EP", "Soft Piano", "Clav" }, 0));
        l.add (fparam ("tone", "Keys|Tone", 0, 1, 0.5f));
        l.add (fparam ("decay", "Keys|Decay", 0.3f, 8, 3, 2.5f, "s"));
        l.add (fparam ("hammer", "Keys|Hammer", 0, 1, 0.4f));
        l.add (fparam ("tremrate", "Tremolo|Rate", 0.5f, 10, 4.5f, 4, "Hz"));
        l.add (fparam ("tremdepth", "Tremolo|Depth", 0, 1, 0.2f));
        l.add (fparam ("level", "Tremolo|Level", 0, 1, 0.5f));
        return l;
    }
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Classic Tine", {} }, { "Bright Reed", { { "model", 1 }, { "tone", 0.7f }, { "tremdepth", 0.35f } } },
                 { "Soft Piano", { { "model", 2 }, { "tremdepth", 0 }, { "decay", 5 } } }, { "Funky Clav", { { "model", 3 }, { "tremdepth", 0 }, { "tone", 0.7f } } },
                 { "Lo-fi Keys", { { "model", 0 }, { "tone", 0.25f }, { "tremdepth", 0.5f }, { "tremrate", 3 } } } };
    }
private:
    struct KVoice : Voice
    {
        explicit KVoice (KeysProcessor& o) : Voice (o), kp (o) {}
        juce::ADSR::Parameters envParams() override { return { 0.001f, 0.05f, 1.0f, kp.p ("model") > 2.5f ? 0.04f : 0.35f }; }
        void onStart() override
        {
            model = (int) kp.p ("model");
            const double sr = getSampleRate();
            const float decay = kp.p ("decay") * (float) std::pow (0.97, (noteNum - 60) * 0.5);
            const float tone = kp.p ("tone");
            for (auto& ph : ph_) ph = 0;
            decayMul = std::exp (-6.9 / (decay * sr));
            amp = 1;
            modAmp = (0.5f + tone * 2.0f) * (0.4f + vel);
            modMul = std::exp (-6.9 / ((0.15 + tone * 0.6) * sr));
            hammer = kp.p ("hammer") * vel; hammerMul = std::exp (-6.9 / (0.008 * sr));
            tremPh = 0;
        }
        void renderSample (float& l, float& r) override
        {
            const double sr = getSampleRate();
            const double base = freq / sr;
            float s = 0;
            switch (model)
            {
                case 0: // tine: carrier + 14x modulator decaying fast + bell partial
                    s = (float) std::sin ((ph_[0] + modAmp * 0.15 * std::sin (ph_[1] * juce::MathConstants<double>::twoPi)) * juce::MathConstants<double>::twoPi)
                        + 0.15f * modAmp * (float) std::sin (ph_[2] * juce::MathConstants<double>::twoPi);
                    ph_[1] += base * 1.0; ph_[2] += base * 14.0; break;
                case 1: // reed: asymmetric, odd harmonics
                {
                    float x = (float) std::sin (ph_[0] * juce::MathConstants<double>::twoPi + modAmp * 0.6 * std::sin (ph_[1] * juce::MathConstants<double>::twoPi));
                    s = std::tanh ((x + 0.3f) * (1.0f + modAmp)) - 0.29f;
                    ph_[1] += base; break;
                }
                case 2: // soft piano: slightly inharmonic partials
                    for (int k = 1; k <= 6; ++k)
                    {
                        const double fk = k * std::sqrt (1.0 + 0.0004 * k * k);
                        s += (float) std::sin (ph_[(size_t) k] * juce::MathConstants<double>::twoPi) / (k * k * 0.7f + 0.3f) * (k > 2 ? (float) modAmp * 0.6f : 1.0f);
                        ph_[(size_t) k] += base * fk;
                    }
                    s *= 0.6f; break;
                default: // clav: bright pulse-ish with fast decay
                {
                    double p0 = ph_[0];
                    s = (p0 < 0.25 ? 1.0f : -0.33f) * (float) modAmp * 0.5f + (float) std::sin (p0 * juce::MathConstants<double>::twoPi) * 0.5f;
                    break;
                }
            }
            ph_[0] += base;
            for (auto& v : ph_) if (v >= 1) v -= std::floor (v);
            s = s * amp + hammer * (rng.nextFloat() * 2 - 1);
            amp *= (float) decayMul; modAmp *= (float) modMul; hammer *= (float) hammerMul;
            tremPh += kp.p ("tremrate") / sr; if (tremPh >= 1) tremPh -= 1;
            const float tr = (float) std::sin (tremPh * juce::MathConstants<double>::twoPi) * kp.p ("tremdepth");
            l = s * 0.4f * (1.0f - juce::jmax (0.0f, tr));
            r = s * 0.4f * (1.0f + juce::jmin (0.0f, tr));
        }
        KeysProcessor& kp;
        std::array<double, 8> ph_ {};
        double decayMul = 1, modMul = 1, hammerMul = 1, tremPh = 0;
        float amp = 1, modAmp = 1, hammer = 0;
        int model = 0;
        juce::Random rng;
    };
};

//==============================================================================
/** Drawbar organ with percussion and rotary-style vibrato. */
class OrganProcessor : public PolySynthBase
{
public:
    OrganProcessor() : PolySynthBase ("Gubee Organ", layout()) { for (int i = 0; i < 16; ++i) synth.addVoice (new OVoice (*this)); }
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        const char* names[] = { "16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "1'" };
        const float defs[] = { 8, 8, 8, 0, 0, 0, 0, 0, 0 };
        for (int i = 0; i < 9; ++i) l.add (iparam ("db" + juce::String (i), juce::String ("Drawbars|") + names[i], 0, 8, defs[i]));
        l.add (bparam ("perc", "Voice|Percussion", true));
        l.add (cparam ("rotary", "Voice|Rotary", { "Off", "Slow", "Fast" }, 1));
        l.add (fparam ("drive", "Voice|Drive", 0, 1, 0.2f));
        l.add (fparam ("level", "Voice|Level", 0, 1, 0.4f));
        return l;
    }
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Full Gospel", { { "db0", 8 }, { "db1", 8 }, { "db2", 8 }, { "db3", 8 }, { "db4", 8 }, { "db5", 8 }, { "db6", 8 }, { "db7", 8 }, { "db8", 8 }, { "rotary", 2 } } },
                 { "Jazz (888 000 000)", {} },
                 { "Soft Flutes", { { "db0", 0 }, { "db1", 0 }, { "db2", 6 }, { "db3", 4 }, { "perc", 0 }, { "drive", 0 } } },
                 { "Rock Organ", { { "db3", 6 }, { "db5", 4 }, { "drive", 0.7f }, { "rotary", 2 } } } };
    }
    double rotPhase = 0;
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override
    {
        PolySynthBase::processBlock (b, m);
        // rotary: amplitude + small pitch wobble emulated with a short modulated delay
        const int rot = (int) p ("rotary");
        const float drive = p ("drive");
        const double rate = rot == 0 ? 0.0 : rot == 1 ? 0.8 : 6.5;
        if (rotLine[0].buf.empty()) for (auto& d : rotLine) d.setSize ((int) (sr * 0.01) + 8);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            rotPhase += rate / sr; if (rotPhase >= 1) rotPhase -= 1;
            const float s = (float) std::sin (rotPhase * juce::MathConstants<double>::twoPi);
            for (int c = 0; c < juce::jmin (2, b.getNumChannels()); ++c)
            {
                float x = b.getSample (c, i);
                if (drive > 0) x = std::tanh (x * (1 + drive * 6)) / (1 + drive * 2);
                rotLine[(size_t) c].push (x);
                const float sc = c == 0 ? s : -s;
                float y = rot == 0 ? x : rotLine[(size_t) c].read ((float) (sr * 0.001 * (1.5 + 1.0 * sc))) * (1.0f + 0.25f * sc);
                b.setSample (c, i, y);
            }
        }
    }
private:
    std::array<DelayLine, 2> rotLine;
    struct OVoice : Voice
    {
        explicit OVoice (OrganProcessor& o) : Voice (o), op (o) {}
        juce::ADSR::Parameters envParams() override { return { 0.004f, 0.01f, 1.0f, 0.03f }; }
        void onStart() override { for (auto& ph : phases) ph = 0; percAmp = op.p ("perc") > 0.5f ? 1.0f : 0.0f; percMul = std::exp (-6.9 / (0.25 * getSampleRate())); }
        void renderSample (float& l, float& r) override
        {
            static const double ratios[9] = { 0.5, 1.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0 };
            float s = 0;
            for (int k = 0; k < 9; ++k)
            {
                const float lvl = op.p (("db" + juce::String (k)).toRawUTF8()) / 8.0f;
                if (lvl > 0) s += (float) std::sin (phases[k] * juce::MathConstants<double>::twoPi) * lvl;
                phases[k] += freq * ratios[k] / getSampleRate(); if (phases[k] >= 1) phases[k] -= 1;
            }
            s += (float) std::sin (phases[4] * juce::MathConstants<double>::twoPi) * percAmp * 0.8f;
            percAmp *= (float) percMul;
            l = r = s * 0.15f;
        }
        OrganProcessor& op;
        double phases[9] {}, percMul = 1;
        float percAmp = 0;
    };
};

//==============================================================================
/** Plucked strings (Karplus-Strong): guitar, harp, bass, koto-ish. */
class PluckProcessor : public PolySynthBase
{
public:
    PluckProcessor() : PolySynthBase ("Gubee Pluck", layout()) { for (int i = 0; i < 12; ++i) synth.addVoice (new PVoice (*this)); }
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (cparam ("model", "String|Model", { "Nylon Guitar", "Steel Guitar", "Harp", "Plucked Bass", "Koto" }, 0));
        l.add (fparam ("bright", "String|Brightness", 0, 1, 0.5f));
        l.add (fparam ("decay", "String|Sustain", 0, 1, 0.6f));
        l.add (fparam ("pick", "String|Pick Position", 0.05f, 0.5f, 0.2f));
        l.add (fparam ("body", "Body|Body", 0, 1, 0.5f));
        l.add (fparam ("width", "Body|Stereo", 0, 1, 0.4f));
        l.add (fparam ("level", "Body|Level", 0, 1, 0.6f));
        return l;
    }
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Nylon", {} }, { "Steel Strum", { { "model", 1 }, { "bright", 0.7f } } }, { "Harp", { { "model", 2 }, { "decay", 0.8f } } },
                 { "Pluck Bass", { { "model", 3 }, { "bright", 0.3f } } }, { "Koto", { { "model", 4 }, { "pick", 0.1f } } } };
    }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override
    {
        PolySynthBase::processBlock (b, m);
        // simple body resonance
        const float body = p ("body");
        if (body <= 0.001f) return;
        auto c1 = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 110.0f, 1.2f, 1.0f + body * 2.5f);
        auto c2 = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 240.0f, 1.5f, 1.0f + body * 1.5f);
        for (int c = 0; c < juce::jmin (2, b.getNumChannels()); ++c)
        {
            *bodyF[(size_t) c][0].coefficients = *c1; *bodyF[(size_t) c][1].coefficients = *c2;
            auto* d = b.getWritePointer (c);
            for (int i = 0; i < b.getNumSamples(); ++i) d[i] = bodyF[(size_t) c][1].processSample (bodyF[(size_t) c][0].processSample (d[i]));
        }
    }
private:
    std::array<std::array<juce::dsp::IIR::Filter<float>, 2>, 2> bodyF;
    struct PVoice : Voice
    {
        explicit PVoice (PluckProcessor& o) : Voice (o), pp (o) {}
        juce::ADSR::Parameters envParams() override { return { 0.001f, 0.1f, 1.0f, 0.15f }; }
        void onStart() override
        {
            const double sr = getSampleRate();
            model = (int) pp.p ("model");
            const double f = model == 3 ? freq : freq;
            len = juce::jlimit (4, (int) line.size() - 2, (int) (sr / f));
            frac = (float) (sr / f - len);
            const float bright = pp.p ("bright") + (model == 1 ? 0.2f : model == 2 ? 0.1f : model == 3 ? -0.2f : model == 4 ? 0.3f : 0.0f);
            damp = juce::jlimit (0.05f, 0.95f, 0.15f + bright * 0.7f);
            const float sus = pp.p ("decay");
            loss = 0.990f + sus * 0.0098f - (model == 3 ? 0.002f : 0.0f);
            // excitation: filtered noise burst shaped by pick position
            juce::Random r;
            const int pickTap = juce::jmax (1, (int) (len * pp.p ("pick")));
            float prev = 0;
            for (int i = 0; i < len; ++i)
            {
                float n = r.nextFloat() * 2 - 1;
                prev = prev + (n - prev) * (0.2f + damp * 0.8f);
                line[(size_t) i] = prev * vel;
            }
            for (int i = len - 1; i >= pickTap; --i) line[(size_t) i] -= line[(size_t) (i - pickTap)] * 0.8f;
            pos = 0; last = 0;
            pan = (r.nextFloat() - 0.5f) * pp.p ("width");
        }
        void renderSample (float& l, float& r) override
        {
            const int i1 = (pos + 1) % len;
            float y = line[(size_t) pos] * (1.0f - frac) + line[(size_t) i1] * frac;
            float filtered = (y * damp + last * (1.0f - damp)) * loss;
            last = filtered;
            line[(size_t) pos] = filtered;
            pos = i1;
            l = y * 0.8f * (1.0f - pan);
            r = y * 0.8f * (1.0f + pan);
        }
        PluckProcessor& pp;
        std::array<float, 4096> line {};
        int len = 100, pos = 0, model = 0;
        float frac = 0, damp = 0.5f, loss = 0.996f, last = 0, pan = 0;
    };
};

//==============================================================================
/** Synthesized drum kit on the keyboard (GM-style note map). */
class DrumSynthProcessor : public PolySynthBase
{
public:
    DrumSynthProcessor() : PolySynthBase ("Gubee Drum Synth", layout()) { for (int i = 0; i < 16; ++i) synth.addVoice (new DVoice (*this)); }
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (fparam ("ktune", "Kick|Tune", 30, 90, 48, -1, "Hz"));
        l.add (fparam ("kdecay", "Kick|Decay", 0.1f, 2, 0.5f, 0.5f, "s"));
        l.add (fparam ("kpunch", "Kick|Punch", 0, 1, 0.6f));
        l.add (fparam ("stone", "Snare|Tone", 120, 300, 190, -1, "Hz"));
        l.add (fparam ("ssnap", "Snare|Snappy", 0, 1, 0.6f));
        l.add (fparam ("sdecay", "Snare|Decay", 0.05f, 0.8f, 0.22f, 0.2f, "s"));
        l.add (fparam ("htone", "Hats|Tone", 3000, 12000, 7500, 7000, "Hz"));
        l.add (fparam ("hdecay", "Hats|Closed Decay", 0.01f, 0.3f, 0.06f, 0.06f, "s"));
        l.add (fparam ("ohdecay", "Hats|Open Decay", 0.1f, 1.5f, 0.45f, 0.4f, "s"));
        l.add (fparam ("ttune", "Toms & Perc|Tom Tune", 60, 250, 110, -1, "Hz"));
        l.add (fparam ("cdecay", "Toms & Perc|Clap Decay", 0.05f, 0.8f, 0.25f, 0.2f, "s"));
        l.add (fparam ("drive", "Toms & Perc|Drive", 0, 1, 0.2f));
        l.add (fparam ("level", "Toms & Perc|Level", 0, 1, 0.8f));
        return l;
    }
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Classic Machine", {} }, { "Deep Trap", { { "ktune", 42 }, { "kdecay", 1.2f }, { "kpunch", 0.8f }, { "hdecay", 0.04f } } },
                 { "Tight House", { { "ktune", 55 }, { "kdecay", 0.35f }, { "sdecay", 0.15f }, { "drive", 0.4f } } },
                 { "Lo-fi Dusty", { { "htone", 4500 }, { "drive", 0.6f }, { "ssnap", 0.3f } } } };
    }
    static juce::String noteMapHelp() { return "C1 kick, C#1 rim, D1 snare, D#1 clap, F1 low tom, F#1 closed hat, A1 mid tom, A#1 open hat, C2 high tom, C#2 crash"; }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override
    {
        // choke: closed hat cuts open hat
        for (const auto ev : m)
        {
            auto msg = ev.getMessage();
            if (msg.isNoteOn() && msg.getNoteNumber() % 12 == 6 && msg.getNoteNumber() < 48)
                for (int i = 0; i < synth.getNumVoices(); ++i)
                    if (auto* v = dynamic_cast<DVoice*> (synth.getVoice (i))) if (v->kind == 6) v->choke = true;
        }
        PolySynthBase::processBlock (b, m);
        const float drive = p ("drive");
        if (drive > 0.01f)
            for (int c = 0; c < b.getNumChannels(); ++c)
            {
                auto* d = b.getWritePointer (c);
                for (int i = 0; i < b.getNumSamples(); ++i) d[i] = std::tanh (d[i] * (1 + drive * 5)) / std::tanh (1 + drive * 5) * 0.9f;
            }
    }
private:
    struct DVoice : Voice
    {
        explicit DVoice (DrumSynthProcessor& o) : Voice (o), dp (o) {}
        juce::ADSR::Parameters envParams() override { return { 0.0005f, 0.01f, 1.0f, 0.02f }; }
        void stopNote (float v, bool tail) override { juce::ignoreUnused (v, tail); }   // drums ring out
        void onStart() override
        {
            // 0 kick, 1 rim, 2 snare, 3 clap, 4 low tom, 5 closed hat, 6 open hat, 7 mid tom, 8 high tom, 9 crash
            static const int map[12] = { 0, 1, 2, 3, 2, 4, 5, 7, 5, 7, 6, 8 };
            kind = noteNum >= 48 && (noteNum % 12) == 1 ? 9 : map[noteNum % 12];
            if (kind == 6) kind = 6;
            t = 0; ph = 0; choke = false;
            const double sr = getSampleRate();
            const float k = (float) (1.0 / sr);
            juce::ignoreUnused (k);
            for (auto& f : bp) f.reset();
            auto setBP = [&] (float fc, float q) { auto c = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, juce::jmin (fc, (float) sr * 0.45f), q); *bp[0].coefficients = *c; };
            auto setHP = [&] (float fc) { auto c = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, juce::jmin (fc, (float) sr * 0.45f)); *bp[1].coefficients = *c; };
            switch (kind)
            {
                case 2: setBP (3500, 0.7f); setHP (800); break;
                case 3: setBP (1300, 1.5f); setHP (400); break;
                case 5: case 6: case 9: setBP (dp.p ("htone"), 0.6f); setHP (dp.p ("htone") * 0.8f); break;
                default: setBP (2000, 0.5f); setHP (100); break;
            }
        }
        void renderSample (float& l, float& r) override
        {
            const double sr = getSampleRate();
            const float tt = (float) t;
            float s = 0;
            auto noise = [&] { return rng.nextFloat() * 2 - 1; };
            switch (kind)
            {
                case 0:
                {
                    const float punch = dp.p ("kpunch");
                    const float f = dp.p ("ktune") * (1.0f + (2.5f + punch * 4.0f) * std::exp (-tt * 35.0f));
                    ph += f / sr;
                    s = (float) std::sin (ph * juce::MathConstants<double>::twoPi) * std::exp (-tt / dp.p ("kdecay") * 2.0f)
                        + noise() * 0.3f * punch * std::exp (-tt * 300.0f);
                    break;
                }
                case 1: ph += 1700.0 / sr; s = (float) std::sin (ph * juce::MathConstants<double>::twoPi) * std::exp (-tt * 90.0f) + noise() * 0.3f * std::exp (-tt * 250.0f); break;
                case 2:
                {
                    ph += dp.p ("stone") * (1.0f + 0.3f * std::exp (-tt * 40.0f)) / sr;
                    const float dec = dp.p ("sdecay");
                    float body = (float) std::sin (ph * juce::MathConstants<double>::twoPi) * std::exp (-tt / dec * 3.0f);
                    float sn = bp[1].processSample (bp[0].processSample (noise())) * std::exp (-tt / dec * 2.0f) * 2.5f;
                    s = body * (1.0f - dp.p ("ssnap") * 0.5f) + sn * dp.p ("ssnap");
                    break;
                }
                case 3:
                {
                    float env = 0;
                    for (float off : { 0.0f, 0.011f, 0.022f }) if (tt >= off) env = juce::jmax (env, std::exp (-(tt - off) * 180.0f));
                    if (tt >= 0.033f) env = juce::jmax (env, std::exp (-(tt - 0.033f) / dp.p ("cdecay") * 2.5f) * 0.7f);
                    s = bp[0].processSample (noise()) * env * 3.0f;
                    break;
                }
                case 4: case 7: case 8:
                {
                    const float base = dp.p ("ttune") * (kind == 4 ? 0.8f : kind == 7 ? 1.0f : 1.35f);
                    ph += base * (1.0f + 0.5f * std::exp (-tt * 25.0f)) / sr;
                    s = (float) std::sin (ph * juce::MathConstants<double>::twoPi) * std::exp (-tt * 6.0f);
                    break;
                }
                case 5: case 6: case 9:
                {
                    const float dec = kind == 5 ? dp.p ("hdecay") : kind == 6 ? dp.p ("ohdecay") : 1.6f;
                    // metallic: sum of square waves + noise through band/high pass
                    float metal = 0;
                    static const float fr[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
                    for (int k = 0; k < 6; ++k) { mph[k] += fr[k] * 2.0 / sr; if (mph[k] >= 1) mph[k] -= 1; metal += mph[k] < 0.5 ? 1.0f : -1.0f; }
                    float x = metal * 0.15f + noise() * 0.6f;
                    float env = std::exp (-tt / dec * 2.5f);
                    if (choke && kind == 6) env *= std::exp (-chokeT * 400.0f), chokeT += (float) (1.0 / sr);
                    s = bp[1].processSample (bp[0].processSample (x)) * env * 2.0f;
                    break;
                }
                default: break;
            }
            t += 1.0 / sr;
            if (t > 3.0 || (choke && chokeT > 0.03f)) { clearCurrentNote(); }
            l = r = s * 0.7f;
        }
        DrumSynthProcessor& dp;
        double t = 0, ph = 0;
        double mph[6] {};
        int kind = 0;
        bool choke = false;
        float chokeT = 0;
        std::array<juce::dsp::IIR::Filter<float>, 2> bp;
        juce::Random rng;
    };
};
}
