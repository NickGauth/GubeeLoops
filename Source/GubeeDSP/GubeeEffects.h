#pragma once
#include "GubeeBase.h"

namespace gubee
{
//==============================================================================
class EQProcessor : public GubeeProcessor
{
public:
    EQProcessor() : GubeeProcessor ("Gubee EQ", false, layout()) {}
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Flat", {} }, { "Clean Low End", { { "lowcut", 35 }, { "midfreq", 300 }, { "midgain", -3 } } },
                 { "Bright Vocal", { { "lowcut", 100 }, { "midfreq", 3000 }, { "midgain", 3 }, { "highgain", 4 } } },
                 { "Kick Thump", { { "lowgain", 5 }, { "midfreq", 400 }, { "midgain", -5 }, { "midq", 2 } } },
                 { "Lo-fi", { { "lowcut", 200 }, { "highcut", 4000 } } } };
    }

    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (fparam ("lowcut", "Low Cut", 20, 1000, 20, 120, "Hz"));
        l.add (fparam ("lowgain", "Low", -18, 18, 0, -1, "dB"));
        l.add (fparam ("midfreq", "Mid Freq", 200, 8000, 1000, 1200, "Hz"));
        l.add (fparam ("midgain", "Mid", -18, 18, 0, -1, "dB"));
        l.add (fparam ("midq", "Mid Q", 0.3f, 6, 1, 1.2f));
        l.add (fparam ("highgain", "High", -18, 18, 0, -1, "dB"));
        l.add (fparam ("highcut", "High Cut", 1000, 20000, 20000, 6000, "Hz"));
        l.add (fparam ("out", "Output", -18, 18, 0, -1, "dB"));
        return l;
    }

    void prepareToPlay (double s, int) override { sr = s; for (auto& ch : f) for (auto& x : ch) x.reset(); last.fill (-1e9f); }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        std::array<float, 7> now { p ("lowcut"), p ("lowgain"), p ("midfreq"), p ("midgain"), p ("midq"), p ("highgain"), p ("highcut") };
        if (now != last)
        {
            last = now;
            using C = juce::dsp::IIR::Coefficients<float>;
            auto ny = (float) sr * 0.45f;
            juce::dsp::IIR::Coefficients<float>::Ptr cs[5] = {
                C::makeHighPass (sr, juce::jmin (now[0], ny), 0.707f),
                C::makeLowShelf (sr, 110.0f, 0.707f, juce::Decibels::decibelsToGain (now[1])),
                C::makePeakFilter (sr, juce::jmin (now[2], ny), now[4], juce::Decibels::decibelsToGain (now[3])),
                C::makeHighShelf (sr, juce::jmin (9000.0f, ny), 0.707f, juce::Decibels::decibelsToGain (now[5])),
                C::makeLowPass (sr, juce::jmin (now[6], ny), 0.707f) };
            for (auto& ch : f) for (int i = 0; i < 5; ++i) *ch[(size_t) i].coefficients = *cs[i];
        }
        const float gain = juce::Decibels::decibelsToGain (p ("out"));
        for (int c = 0; c < juce::jmin (2, b.getNumChannels()); ++c)
        {
            auto* d = b.getWritePointer (c);
            auto& fl = f[(size_t) c];
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                float x = d[i];
                for (auto& flt : fl) x = flt.processSample (x);
                d[i] = x * gain;
            }
        }
    }

private:
    std::array<std::array<juce::dsp::IIR::Filter<float>, 5>, 2> f;
    std::array<float, 7> last {};
};

//==============================================================================
class ReverbProcessor : public GubeeProcessor
{
public:
    ReverbProcessor() : GubeeProcessor ("Gubee Reverb", false, layout()) {}
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Room", { { "size", 0.35f }, { "mix", 0.2f } } }, { "Hall", { { "size", 0.8f }, { "mix", 0.3f } } },
                 { "Huge Space", { { "size", 0.97f }, { "damping", 0.3f }, { "mix", 0.45f } } }, { "Vocal Plate", { { "size", 0.6f }, { "damping", 0.2f }, { "lowcut", 300 }, { "mix", 0.25f } } } };
    }

    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (fparam ("size", "Size", 0, 1, 0.6f));
        l.add (fparam ("damping", "Damping", 0, 1, 0.5f));
        l.add (fparam ("width", "Width", 0, 1, 1));
        l.add (fparam ("mix", "Mix", 0, 1, 0.25f));
        l.add (fparam ("lowcut", "Low Cut", 20, 1000, 200, 200, "Hz"));
        return l;
    }

    void prepareToPlay (double s, int bs) override
    {
        sr = s;
        rev.prepare ({ s, (juce::uint32) bs, 2 });
        wet.setSize (2, bs);
        for (auto& h : hp) h.reset();
    }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        juce::dsp::Reverb::Parameters rp;
        rp.roomSize = p ("size"); rp.damping = p ("damping"); rp.width = p ("width");
        rp.wetLevel = 1.0f; rp.dryLevel = 0.0f;
        rev.setParameters (rp);
        const int n = b.getNumSamples(), nc = juce::jmin (2, b.getNumChannels());
        if (wet.getNumSamples() < n) wet.setSize (2, n, false, false, true);
        auto cut = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, p ("lowcut"));
        for (int c = 0; c < 2; ++c)
        {
            wet.copyFrom (c, 0, b, juce::jmin (c, nc - 1), 0, n);
            *hp[(size_t) c].coefficients = *cut;
            auto* d = wet.getWritePointer (c);
            for (int i = 0; i < n; ++i) d[i] = hp[(size_t) c].processSample (d[i]);
        }
        juce::dsp::AudioBlock<float> blk (wet);
        auto sub = blk.getSubBlock (0, (size_t) n);
        rev.process (juce::dsp::ProcessContextReplacing<float> (sub));
        const float mix = p ("mix");
        for (int c = 0; c < nc; ++c)
        {
            b.applyGain (c, 0, n, 1.0f - mix * 0.5f);
            b.addFrom (c, 0, wet, c, 0, n, mix);
        }
    }

private:
    juce::dsp::Reverb rev;
    juce::AudioBuffer<float> wet;
    std::array<juce::dsp::IIR::Filter<float>, 2> hp;
};

//==============================================================================
class DelayProcessor : public GubeeProcessor
{
public:
    DelayProcessor() : GubeeProcessor ("Gubee Delay", false, layout()) {}
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Dotted 8th", {} }, { "Ping-Pong 1/4", { { "division", 4 }, { "pingpong", 1 }, { "feedback", 0.45f } } },
                 { "Slapback", { { "sync", 0 }, { "time", 110 }, { "feedback", 0.1f }, { "mix", 0.35f } } },
                 { "Dub Echo", { { "division", 4 }, { "feedback", 0.75f }, { "tone", 2000 }, { "mix", 0.4f } } } };
    }

    static juce::StringArray divisions() { return { "1/16", "1/8 T", "1/8", "1/8 D", "1/4", "1/4 D", "1/2", "1 Bar" }; }
    static double divisionBeats (int i) { const double v[] = { 0.25, 1.0 / 3.0, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0 }; return v[juce::jlimit (0, 7, i)]; }

    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (bparam ("sync", "Tempo Sync", true));
        l.add (cparam ("division", "Division", divisions(), 3));
        l.add (fparam ("time", "Time (free)", 1, 2000, 375, 300, "ms"));
        l.add (fparam ("feedback", "Feedback", 0, 0.95f, 0.4f));
        l.add (fparam ("tone", "Tone", 500, 20000, 8000, 4000, "Hz"));
        l.add (bparam ("pingpong", "Ping-Pong", false));
        l.add (fparam ("mix", "Mix", 0, 1, 0.3f));
        return l;
    }

    void prepareToPlay (double s, int) override
    {
        sr = s;
        size = (int) (s * 4.5) + 4;
        line.setSize (2, size); line.clear();
        w = 0; lp[0] = lp[1] = 0;
        smoothed.reset (s, 0.08);
    }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        double secs = p ("sync") > 0.5f ? divisionBeats ((int) p ("division")) * 60.0 / hostBpm (*this)
                                          : p ("time") / 1000.0;
        smoothed.setTargetValue ((float) juce::jlimit (1.0, sr * 4.0, secs * sr));
        const float fb = p ("feedback"), mix = p ("mix");
        const bool ping = p ("pingpong") > 0.5f;
        const float a = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * p ("tone") / sr);
        const int n = b.getNumSamples(), nc = b.getNumChannels();
        auto* L = b.getWritePointer (0);
        auto* R = nc > 1 ? b.getWritePointer (1) : nullptr;
        auto* dl = line.getWritePointer (0); auto* dr = line.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            float d = smoothed.getNextValue();
            float rp = (float) w - d; if (rp < 0) rp += (float) size;
            int i0 = (int) rp; float fr = rp - (float) i0; int i1 = (i0 + 1) % size;
            float yl = dl[i0] + fr * (dl[i1] - dl[i0]);
            float yr = dr[i0] + fr * (dr[i1] - dr[i0]);
            lp[0] = (1 - a) * yl + a * lp[0]; lp[1] = (1 - a) * yr + a * lp[1];
            float inL = L[i], inR = R ? R[i] : L[i];
            if (ping) { dl[w] = (inL + inR) * 0.5f + lp[1] * fb; dr[w] = lp[0] * fb; }
            else      { dl[w] = inL + lp[0] * fb; dr[w] = inR + lp[1] * fb; }
            L[i] = inL + yl * mix;
            if (R) R[i] = inR + yr * mix;
            if (++w >= size) w = 0;
        }
    }

private:
    juce::AudioBuffer<float> line;
    int size = 1, w = 0;
    float lp[2] {};
    juce::SmoothedValue<float> smoothed;
};

//==============================================================================
class FilterProcessor : public GubeeProcessor
{
public:
    FilterProcessor() : GubeeProcessor ("Gubee Filter", false, layout()) {}
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Low Pass Warm", { { "cutoff", 1500 } } }, { "Wobble (LFO)", { { "cutoff", 600 }, { "reso", 4 }, { "lforate", 3 }, { "lfodepth", 0.6f } } },
                 { "Telephone Band", { { "mode", 2 }, { "cutoff", 1500 }, { "reso", 2 } } }, { "High Pass Riser", { { "mode", 1 }, { "cutoff", 400 } } } };
    }

    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (cparam ("mode", "Mode", { "Low Pass", "High Pass", "Band Pass" }, 0));
        l.add (fparam ("cutoff", "Cutoff", 20, 20000, 2000, 1000, "Hz"));
        l.add (fparam ("reso", "Resonance", 0.5f, 10, 0.9f, 2));
        l.add (fparam ("drive", "Drive", 0, 24, 0, -1, "dB"));
        l.add (fparam ("lforate", "LFO Rate", 0.05f, 20, 1, 2, "Hz"));
        l.add (fparam ("lfodepth", "LFO Depth", 0, 1, 0));
        l.add (fparam ("mix", "Mix", 0, 1, 1));
        return l;
    }

    void prepareToPlay (double s, int bs) override
    {
        sr = s;
        svf.prepare ({ s, (juce::uint32) bs, 2 });
        phase = 0;
    }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        using T = juce::dsp::StateVariableTPTFilterType;
        int m = (int) p ("mode");
        svf.setType (m == 0 ? T::lowpass : m == 1 ? T::highpass : T::bandpass);
        svf.setResonance (p ("reso"));
        const float base = p ("cutoff"), depth = p ("lfodepth"), mix = p ("mix");
        const float drive = juce::Decibels::decibelsToGain (p ("drive"));
        const double inc = p ("lforate") / sr;
        const int n = b.getNumSamples(), nc = juce::jmin (2, b.getNumChannels());
        for (int start = 0; start < n; start += 32)
        {
            int len = juce::jmin (32, n - start);
            float lfo = (float) std::sin (phase * juce::MathConstants<double>::twoPi);
            phase += inc * len; phase -= std::floor (phase);
            float fc = juce::jlimit (20.0f, (float) sr * 0.45f, base * std::pow (2.0f, lfo * depth * 4.0f));
            svf.setCutoffFrequency (fc);
            for (int c = 0; c < nc; ++c)
            {
                auto* d = b.getWritePointer (c, start);
                for (int i = 0; i < len; ++i)
                {
                    float x = d[i];
                    float y = svf.processSample (c, drive > 1.001f ? std::tanh (x * drive) : x);
                    d[i] = x + (y - x) * mix;
                }
            }
        }
    }

private:
    juce::dsp::StateVariableTPTFilter<float> svf;
    double phase = 0;
};

//==============================================================================
class CrushProcessor : public GubeeProcessor
{
public:
    CrushProcessor() : GubeeProcessor ("Gubee Crush", false, layout()) {}
    std::vector<Preset> factoryPresets() const override
    {
        return { { "8-bit", { { "bits", 8 }, { "downsample", 4 } } }, { "Lo-fi Dust", { { "bits", 12 }, { "downsample", 2 }, { "mix", 0.5f } } },
                 { "Destroyed", { { "bits", 3 }, { "downsample", 16 }, { "drive", 12 } } } };
    }

    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (fparam ("bits", "Bits", 1, 16, 8));
        l.add (fparam ("downsample", "Downsample", 1, 40, 1, 6, "x"));
        l.add (fparam ("drive", "Drive", 0, 24, 0, -1, "dB"));
        l.add (fparam ("mix", "Mix", 0, 1, 1));
        l.add (fparam ("out", "Output", -24, 6, 0, -1, "dB"));
        return l;
    }

    void prepareToPlay (double s, int) override { sr = s; counter = 0; held[0] = held[1] = 0; }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        const float levels = std::pow (2.0f, p ("bits") - 1.0f);
        const float ds = p ("downsample"), mix = p ("mix");
        const float drive = juce::Decibels::decibelsToGain (p ("drive")), out = juce::Decibels::decibelsToGain (p ("out"));
        const int n = b.getNumSamples(), nc = juce::jmin (2, b.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            counter += 1.0f;
            bool take = counter >= ds;
            if (take) counter -= ds;
            for (int c = 0; c < nc; ++c)
            {
                float x = b.getSample (c, i);
                if (take)
                {
                    float y = std::tanh (x * drive);
                    held[c] = std::round (y * levels) / levels;
                }
                b.setSample (c, i, (x + (held[c] - x) * mix) * out);
            }
        }
    }

private:
    float counter = 0, held[2] {};
};

//==============================================================================
class LimiterProcessor : public GubeeProcessor
{
public:
    LimiterProcessor() : GubeeProcessor ("Gubee Limiter", false, layout()) {}
    std::vector<Preset> factoryPresets() const override
    {
        return { { "Safe Master", {} }, { "Loud Master", { { "gain", 6 } } }, { "Very Loud", { { "gain", 10 }, { "release", 40 } } } };
    }

    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (fparam ("gain", "Input Gain", 0, 24, 0, -1, "dB"));
        l.add (fparam ("ceiling", "Ceiling", -24, 0, -0.3f, -1, "dB"));
        l.add (fparam ("release", "Release", 1, 500, 80, 80, "ms"));
        return l;
    }

    // 1.5 ms look-ahead peak limiter: never exceeds the ceiling.
    void prepareToPlay (double s, int) override
    {
        sr = s;
        look = juce::jmax (1, (int) (s * 0.0015));
        delay.setSize (2, look + 1); delay.clear();
        win.assign ((size_t) look + 1, 0.0f);
        pos = 0; gainState = 1.0f;
        setLatencySamples (look);
    }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        const float in = juce::Decibels::decibelsToGain (p ("gain"));
        const float ceiling = juce::Decibels::decibelsToGain (p ("ceiling"));
        const float rel = (float) std::exp (-1.0 / (p ("release") * 0.001 * sr));
        const int nc = juce::jmin (2, b.getNumChannels()), n = b.getNumSamples();
        const int size = look + 1;
        for (int i = 0; i < n; ++i)
        {
            float peak = 0;
            float x[2] = { 0, 0 };
            for (int c = 0; c < nc; ++c) { x[c] = b.getSample (c, i) * in; peak = juce::jmax (peak, std::abs (x[c])); }
            win[(size_t) pos] = peak;
            float mx = 0;
            for (auto v : win) mx = juce::jmax (mx, v);          // peak over the look-ahead window
            const float target = mx > ceiling ? ceiling / mx : 1.0f;
            gainState = target < gainState ? target : target + (gainState - target) * rel;
            const int rd = (pos + 1) % size;
            for (int c = 0; c < nc; ++c)
            {
                delay.setSample (c, pos, x[c]);
                float y = delay.getSample (c, rd) * gainState;
                b.setSample (c, i, juce::jlimit (-ceiling, ceiling, y));
            }
            pos = rd;
        }
    }

    double getTailLengthSeconds() const override { return 0.0; }

private:
    juce::AudioBuffer<float> delay;
    std::vector<float> win;
    int look = 64, pos = 0;
    float gainState = 1.0f;
};
}
