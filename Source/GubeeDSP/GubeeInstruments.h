#pragma once
#include "GubeeBase.h"

namespace gubee
{
//==============================================================================
/** Mono 808: sine with pitch "punch" envelope, glide, drive and tone. */
class Bass808Processor : public GubeeProcessor
{
public:
    Bass808Processor() : GubeeProcessor ("Gubee 808", true, layout()) {}

    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (fparam ("tune", "Tune", -24, 24, 0, -1, "st"));
        l.add (fparam ("decay", "Decay", 0.05f, 4, 1.2f, 0.8f, "s"));
        l.add (fparam ("glide", "Glide", 0, 0.5f, 0.05f, 0.08f, "s"));
        l.add (fparam ("punch", "Punch", 0, 36, 14, -1, "st"));
        l.add (fparam ("punchtime", "Punch Time", 0.005f, 0.2f, 0.035f, 0.04f, "s"));
        l.add (fparam ("drive", "Drive", 0, 1, 0.25f));
        l.add (fparam ("tone", "Tone", 100, 20000, 6000, 2000, "Hz"));
        l.add (fparam ("level", "Level", 0, 1.5f, 0.8f));
        return l;
    }

    void prepareToPlay (double s, int) override { sr = s; amp = 0; gate = false; lp = 0; }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals nd;
        b.clear();
        int pos = 0;
        for (const auto meta : midi)
        {
            render (b, pos, meta.samplePosition - pos);
            pos = meta.samplePosition;
            auto m = meta.getMessage();
            if (m.isNoteOn())
            {
                note = m.getNoteNumber();
                double target = juce::MidiMessage::getMidiNoteInHertz (note);
                if (! gate || amp < 0.001f) freq = target;
                targetFreq = target;
                vel = m.getFloatVelocity();
                gate = true; amp = 1.0f; pitchEnv = 1.0f; attack = 0.0f;
            }
            else if (m.isNoteOff() && m.getNoteNumber() == note) gate = false;
            else if (m.isAllNotesOff() || m.isAllSoundOff()) { gate = false; amp = 0; }
        }
        render (b, pos, b.getNumSamples() - pos);
    }

private:
    void render (juce::AudioBuffer<float>& b, int start, int len)
    {
        if (len <= 0 || amp <= 0.00001f) return;
        const float tune = std::pow (2.0f, p ("tune") / 12.0f);
        const float decayMul = (float) std::exp (-6.9 / (p ("decay") * sr));
        const float relMul = (float) std::exp (-6.9 / (0.05 * sr));
        const float punchMul = (float) std::exp (-6.9 / (p ("punchtime") * sr));
        const float punch = p ("punch");
        const double glideCoef = p ("glide") > 0.001f ? std::exp (-4.0 / (p ("glide") * sr)) : 0.0;
        const float drive = p ("drive"), level = p ("level");
        const float a = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * p ("tone") / sr);
        const float dg = 1.0f + drive * 10.0f, dn = std::tanh (dg);
        for (int i = start; i < start + len; ++i)
        {
            freq = targetFreq + (freq - targetFreq) * glideCoef;
            double f = freq * tune * std::pow (2.0, punch * pitchEnv / 12.0);
            phase += f / sr; phase -= std::floor (phase);
            float x = (float) std::sin (phase * juce::MathConstants<double>::twoPi);
            attack = juce::jmin (1.0f, attack + (float) (1.0 / (0.002 * sr)));
            float y = std::tanh (x * dg) / dn * amp * attack * vel * level;
            lp = (1 - a) * y + a * lp;
            for (int c = 0; c < b.getNumChannels(); ++c) b.addSample (c, i, lp);
            amp *= gate ? decayMul : relMul;
            pitchEnv *= punchMul;
        }
    }

    int note = -1;
    bool gate = false;
    double freq = 55, targetFreq = 55, phase = 0;
    float amp = 0, pitchEnv = 0, vel = 1, lp = 0, attack = 0;
};

//==============================================================================
/** Two-oscillator polyphonic subtractive synth. */
class SynthProcessor : public GubeeProcessor
{
public:
    SynthProcessor() : GubeeProcessor ("Gubee Synth", true, layout())
    {
        for (int i = 0; i < 12; ++i) synth.addVoice (new Voice (*this));
        synth.addSound (new Sound());
    }

    static APVTS::ParameterLayout layout()
    {
        juce::StringArray waves { "Saw", "Square", "Triangle", "Sine" };
        APVTS::ParameterLayout l;
        l.add (cparam ("wave1", "Osc 1", waves, 0));
        l.add (cparam ("wave2", "Osc 2", waves, 1));
        l.add (fparam ("detune", "Detune", 0, 50, 8, -1, "ct"));
        l.add (fparam ("oscmix", "Osc Mix", 0, 1, 0.5f));
        l.add (fparam ("octave2", "Osc 2 Oct", -2, 2, 0));
        l.add (fparam ("cutoff", "Cutoff", 40, 18000, 3000, 1200, "Hz"));
        l.add (fparam ("reso", "Resonance", 0.5f, 8, 1, 2));
        l.add (fparam ("envamt", "Filter Env", 0, 1, 0.3f));
        l.add (fparam ("attack", "Attack", 0.001f, 4, 0.005f, 0.3f, "s"));
        l.add (fparam ("decay", "Decay", 0.01f, 4, 0.4f, 0.5f, "s"));
        l.add (fparam ("sustain", "Sustain", 0, 1, 0.7f));
        l.add (fparam ("release", "Release", 0.005f, 6, 0.3f, 0.6f, "s"));
        l.add (fparam ("level", "Level", 0, 1, 0.5f));
        return l;
    }

    void prepareToPlay (double s, int bs) override
    {
        sr = s;
        synth.setCurrentPlaybackSampleRate (s);
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<Voice*> (synth.getVoice (i))) v->prepare (s, bs);
    }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals nd;
        b.clear();
        synth.renderNextBlock (b, midi, 0, b.getNumSamples());
    }

private:
    struct Sound : juce::SynthesiserSound
    {
        bool appliesToNote (int) override    { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    static float polyBlep (double t, double dt)
    {
        if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }

    static float osc (int wave, double ph, double dt)
    {
        switch (wave)
        {
            case 0: return (float) (2.0 * ph - 1.0) - polyBlep (ph, dt);
            case 1: { float v = ph < 0.5 ? 1.0f : -1.0f; v += polyBlep (ph, dt); v -= polyBlep (std::fmod (ph + 0.5, 1.0), dt); return v; }
            case 2: return (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);
            default: return (float) std::sin (ph * juce::MathConstants<double>::twoPi);
        }
    }

    struct Voice : juce::SynthesiserVoice
    {
        explicit Voice (SynthProcessor& o) : owner (o) {}
        void prepare (double s, int bs)
        {
            filter.prepare ({ s, (juce::uint32) bs, 1 });
            filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
            env.setSampleRate (s);
        }
        bool canPlaySound (juce::SynthesiserSound*) override { return true; }
        void startNote (int note, float v, juce::SynthesiserSound*, int) override
        {
            freq = juce::MidiMessage::getMidiNoteInHertz (note);
            vel = v;
            juce::ADSR::Parameters ps { owner.p ("attack"), owner.p ("decay"), owner.p ("sustain"), owner.p ("release") };
            env.setParameters (ps);
            env.noteOn();
            filter.reset();
        }
        void stopNote (float, bool allowTail) override
        {
            if (allowTail) env.noteOff();
            else { env.reset(); clearCurrentNote(); }
        }
        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}
        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
        {
            if (! isVoiceActive()) return;
            const double s = getSampleRate();
            const int w1 = (int) owner.p ("wave1"), w2 = (int) owner.p ("wave2");
            const double det = std::pow (2.0, owner.p ("detune") / 1200.0) * std::pow (2.0, std::round (owner.p ("octave2")));
            const float mix = owner.p ("oscmix"), level = owner.p ("level"), envAmt = owner.p ("envamt");
            const float cutoff = owner.p ("cutoff");
            filter.setResonance (owner.p ("reso"));
            const double dt1 = freq / s, dt2 = freq * det / s;
            for (int i = 0; i < num; ++i)
            {
                float e = env.getNextSample();
                if ((i & 15) == 0)
                    filter.setCutoffFrequency (juce::jlimit (30.0f, (float) s * 0.45f, cutoff * std::pow (2.0f, e * envAmt * 6.0f)));
                float x = osc (w1, ph1, dt1) * (1.0f - mix) + osc (w2, ph2, dt2) * mix;
                ph1 += dt1; if (ph1 >= 1.0) ph1 -= 1.0;
                ph2 += dt2; if (ph2 >= 1.0) ph2 -= 1.0;
                float y = filter.processSample (0, x) * e * vel * level * 0.5f;
                for (int c = 0; c < out.getNumChannels(); ++c) out.addSample (c, start + i, y);
                if (! env.isActive()) { clearCurrentNote(); break; }
            }
        }
        SynthProcessor& owner;
        juce::ADSR env;
        juce::dsp::StateVariableTPTFilter<float> filter;
        double freq = 440, ph1 = 0, ph2 = 0.25;
        float vel = 1;
    };

    juce::Synthesiser synth;
};
}
