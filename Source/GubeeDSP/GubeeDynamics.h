#pragma once
#include "GubeeBase.h"

namespace gubee
{
/** Feed-forward compressor gain computer with soft knee (dB domain). */
struct CompCore
{
    float env = 0.0f;   // dB of gain reduction (<= 0)
    float process (float levelAbs, float thresholdDb, float ratio, float kneeDb, float att, float rel)
    {
        const float x = juce::Decibels::gainToDecibels (levelAbs + 1.0e-9f);
        float over = x - thresholdDb, gr = 0.0f;
        if (kneeDb > 0 && std::abs (over) < kneeDb * 0.5f)
            gr = (1.0f / ratio - 1.0f) * (over + kneeDb * 0.5f) * (over + kneeDb * 0.5f) / (2.0f * kneeDb);
        else if (over > 0)
            gr = (1.0f / ratio - 1.0f) * over;
        env = gr < env ? gr + (env - gr) * att : gr + (env - gr) * rel;
        return juce::Decibels::decibelsToGain (env);
    }
};

//==============================================================================
class CompProcessor : public GubeeProcessor
{
public:
    CompProcessor() : GubeeProcessor ("Gubee Comp", false, layout(), true) {}

    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (fparam ("threshold", "Compressor|Threshold", -60, 0, -18, -1, "dB"));
        l.add (fparam ("ratio", "Compressor|Ratio", 1, 20, 4, 4, ":1"));
        l.add (fparam ("knee", "Compressor|Knee", 0, 18, 6, -1, "dB"));
        l.add (fparam ("attack", "Compressor|Attack", 0.1f, 100, 10, 10, "ms"));
        l.add (fparam ("release", "Compressor|Release", 10, 1000, 120, 150, "ms"));
        l.add (fparam ("makeup", "Output|Makeup", 0, 24, 0, -1, "dB"));
        l.add (fparam ("mix", "Output|Mix", 0, 1, 1));
        l.add (bparam ("sidechain", "Output|Use sidechain", false));
        return l;
    }

    std::vector<Preset> factoryPresets() const override
    {
        return { { "Gentle Glue", { { "threshold", -14.0f }, { "ratio", 2.0f }, { "attack", 30.0f }, { "release", 200.0f }, { "makeup", 2.0f } } },
                 { "Punchy Drums", { { "threshold", -20.0f }, { "ratio", 4.0f }, { "attack", 15.0f }, { "release", 80.0f }, { "makeup", 4.0f } } },
                 { "Vocal Leveler", { { "threshold", -22.0f }, { "ratio", 3.0f }, { "attack", 5.0f }, { "release", 150.0f }, { "makeup", 5.0f } } },
                 { "Smash (parallel)", { { "threshold", -40.0f }, { "ratio", 20.0f }, { "attack", 1.0f }, { "release", 60.0f }, { "makeup", 14.0f }, { "mix", 0.4f } } },
                 { "Sidechain Duck", { { "threshold", -30.0f }, { "ratio", 10.0f }, { "attack", 1.0f }, { "release", 180.0f }, { "sidechain", 1.0f } } } };
    }

    void prepareToPlay (double s, int) override { sr = s; core = {}; }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        auto sc = sidechainBuffer (b);
        const bool useSc = p ("sidechain") > 0.5f && sc.getNumChannels() > 0;
        const float thr = p ("threshold"), ratio = p ("ratio"), knee = p ("knee");
        const float att = coefFor (p ("attack") * 0.001, sr), rel = coefFor (p ("release") * 0.001, sr);
        const float mk = juce::Decibels::decibelsToGain (p ("makeup")), mix = p ("mix");
        const int nc = juce::jmin (2, b.getNumChannels()), n = b.getNumSamples();
        lastGr = 0;
        for (int i = 0; i < n; ++i)
        {
            float lvl = 0;
            if (useSc) for (int c = 0; c < sc.getNumChannels(); ++c) lvl = juce::jmax (lvl, std::abs (sc.getSample (c, i)));
            else for (int c = 0; c < nc; ++c) lvl = juce::jmax (lvl, std::abs (b.getSample (c, i)));
            const float g = core.process (lvl, thr, ratio, knee, att, rel);
            lastGr = juce::jmin (lastGr, core.env);
            for (int c = 0; c < nc; ++c)
            {
                float x = b.getSample (c, i);
                b.setSample (c, i, x * (1.0f - mix) + x * g * mk * mix);
            }
        }
    }
    float lastGr = 0;

private:
    CompCore core;
};

//==============================================================================
class GraphicEQProcessor : public GubeeProcessor
{
public:
    static constexpr int numBands = 10;
    GraphicEQProcessor() : GubeeProcessor ("Gubee Graphic EQ", false, layout()) {}

    static const float* freqs() { static const float f[] = { 31, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 }; return f; }
    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        const char* names[] = { "31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k" };
        for (int i = 0; i < numBands; ++i) l.add (fparam ("b" + juce::String (i), juce::String ("Bands|") + names[i], -12, 12, 0, -1, "dB"));
        l.add (fparam ("q", "Output|Width (Q)", 0.5f, 4, 1.4f, 1.4f));
        l.add (fparam ("out", "Output|Output", -12, 12, 0, -1, "dB"));
        return l;
    }

    std::vector<Preset> factoryPresets() const override
    {
        return { { "Flat", {} },
                 { "Smile (more lows + highs)", { { "b0", 4 }, { "b1", 3 }, { "b2", 1.5f }, { "b5", -1.5f }, { "b7", 1.5f }, { "b8", 3 }, { "b9", 4 } } },
                 { "Warm", { { "b1", 2 }, { "b2", 2.5f }, { "b3", 1 }, { "b8", -2 }, { "b9", -3 } } },
                 { "Telephone", { { "b0", -12 }, { "b1", -12 }, { "b2", -8 }, { "b5", 6 }, { "b6", 6 }, { "b8", -12 }, { "b9", -12 } } },
                 { "Vocal Presence", { { "b2", -2 }, { "b3", -1.5f }, { "b6", 2 }, { "b7", 3 }, { "b8", 2 } } } };
    }

    void prepareToPlay (double s, int) override { sr = s; for (auto& ch : f) for (auto& x : ch) x.reset(); last.fill (-99); }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        std::array<float, numBands + 1> now {};
        for (int i = 0; i < numBands; ++i) now[(size_t) i] = p (("b" + juce::String (i)).toRawUTF8());
        now[numBands] = p ("q");
        if (now != last)
        {
            last = now;
            for (int i = 0; i < numBands; ++i)
            {
                auto c = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, juce::jmin (freqs()[i], (float) sr * 0.45f), now[numBands],
                                                                             juce::Decibels::decibelsToGain (now[(size_t) i]));
                for (auto& ch : f) *ch[(size_t) i].coefficients = *c;
            }
        }
        const float g = juce::Decibels::decibelsToGain (p ("out"));
        for (int c = 0; c < juce::jmin (2, b.getNumChannels()); ++c)
        {
            auto* d = b.getWritePointer (c);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                float x = d[i];
                for (int k = 0; k < numBands; ++k) if (last[(size_t) k] != 0.0f) x = f[(size_t) c][(size_t) k].processSample (x);
                d[i] = x * g;
            }
        }
    }

private:
    std::array<std::array<juce::dsp::IIR::Filter<float>, numBands>, 2> f;
    std::array<float, numBands + 1> last {};
};

//==============================================================================
class MultibandProcessor : public GubeeProcessor
{
public:
    MultibandProcessor() : GubeeProcessor ("Gubee Multiband", false, layout()) {}

    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (fparam ("xlow", "Crossover|Low / Mid", 40, 1000, 180, 200, "Hz"));
        l.add (fparam ("xhigh", "Crossover|Mid / High", 1000, 12000, 3500, 3500, "Hz"));
        for (auto [id, nm] : { std::pair<const char*, const char*> { "lo", "Low" }, { "mi", "Mid" }, { "hi", "High" } })
        {
            l.add (fparam (juce::String (id) + "thr", juce::String (nm) + "|Threshold", -48, 0, -18, -1, "dB"));
            l.add (fparam (juce::String (id) + "ratio", juce::String (nm) + "|Ratio", 1, 10, 3, 3, ":1"));
            l.add (fparam (juce::String (id) + "gain", juce::String (nm) + "|Gain", -12, 12, 0, -1, "dB"));
        }
        l.add (fparam ("attack", "Master|Attack", 0.5f, 100, 10, 10, "ms"));
        l.add (fparam ("release", "Master|Release", 10, 800, 150, 150, "ms"));
        l.add (fparam ("drive", "Master|Drive", 0, 18, 0, -1, "dB"));
        l.add (fparam ("ceiling", "Master|Ceiling", -12, 0, -0.3f, -1, "dB"));
        return l;
    }

    std::vector<Preset> factoryPresets() const override
    {
        return { { "Master Glue", { { "lothr", -18 }, { "mithr", -16 }, { "hithr", -18 }, { "loratio", 2 }, { "miratio", 2 }, { "hiratio", 2 }, { "drive", 2 } } },
                 { "Loud Master", { { "lothr", -24 }, { "mithr", -22 }, { "hithr", -24 }, { "loratio", 4 }, { "miratio", 3 }, { "hiratio", 3 }, { "drive", 6 }, { "logain", 2 }, { "higain", 2 } } },
                 { "Tight Low End", { { "lothr", -26 }, { "loratio", 6 }, { "xlow", 140 }, { "logain", 2 } } },
                 { "De-harsh", { { "hithr", -30 }, { "hiratio", 5 }, { "xhigh", 4500 } } } };
    }

    void prepareToPlay (double s, int bs) override
    {
        sr = s;
        juce::dsp::ProcessSpec spec { s, (juce::uint32) bs, 2 };
        for (auto* f : { &lp1, &hp1, &lp2, &hp2, &ap }) { f->prepare (spec); f->reset(); }
        lp1.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
        hp1.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
        lp2.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
        hp2.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
        ap.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
        for (auto& c : cores) c = {};
        limGain = 1.0f;
    }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        const float xl = p ("xlow"), xh = juce::jmax (p ("xhigh"), xl * 1.5f);
        lp1.setCutoffFrequency (xl); hp1.setCutoffFrequency (xl); ap.setCutoffFrequency (xh);
        lp2.setCutoffFrequency (xh); hp2.setCutoffFrequency (xh);
        const float att = coefFor (p ("attack") * 0.001, sr), rel = coefFor (p ("release") * 0.001, sr);
        const char* ids[3] = { "lo", "mi", "hi" };
        float thr[3], ratio[3], gain[3];
        for (int k = 0; k < 3; ++k)
        {
            thr[k] = p ((juce::String (ids[k]) + "thr").toRawUTF8());
            ratio[k] = p ((juce::String (ids[k]) + "ratio").toRawUTF8());
            gain[k] = juce::Decibels::decibelsToGain (p ((juce::String (ids[k]) + "gain").toRawUTF8()));
        }
        const float drive = juce::Decibels::decibelsToGain (p ("drive")), ceiling = juce::Decibels::decibelsToGain (p ("ceiling"));
        const float limRel = coefFor (0.08, sr);
        const int nc = juce::jmin (2, b.getNumChannels()), n = b.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            float band[3][2] = {};
            for (int c = 0; c < nc; ++c)
            {
                float x = b.getSample (c, i);
                float low = lp1.processSample (c, x);
                float rest = hp1.processSample (c, x);
                low = ap.processSample (c, low);
                band[0][c] = low;
                band[1][c] = lp2.processSample (c, rest);
                band[2][c] = hp2.processSample (c, rest);
            }
            float out[2] = {};
            for (int k = 0; k < 3; ++k)
            {
                float lvl = 0;
                for (int c = 0; c < nc; ++c) lvl = juce::jmax (lvl, std::abs (band[k][c]));
                const float g = cores[(size_t) k].process (lvl, thr[k], ratio[k], 6.0f, att, rel) * gain[k];
                for (int c = 0; c < nc; ++c) out[c] += band[k][c] * g;
            }
            float pk = 0;
            for (int c = 0; c < nc; ++c) { out[c] *= drive; pk = juce::jmax (pk, std::abs (out[c])); }
            const float target = pk > ceiling ? ceiling / pk : 1.0f;
            limGain = target < limGain ? target : target + (limGain - target) * limRel;
            for (int c = 0; c < nc; ++c) b.setSample (c, i, juce::jlimit (-ceiling, ceiling, out[c] * limGain));
        }
    }

private:
    juce::dsp::LinkwitzRileyFilter<float> lp1, hp1, lp2, hp2, ap;
    std::array<CompCore, 3> cores;
    float limGain = 1.0f;
};

//==============================================================================
class GateProcessor : public GubeeProcessor
{
public:
    GateProcessor() : GubeeProcessor ("Gubee Gate", false, layout(), true) {}

    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (fparam ("threshold", "Gate|Threshold", -80, 0, -40, -1, "dB"));
        l.add (fparam ("range", "Gate|Range", -80, 0, -80, -1, "dB"));
        l.add (fparam ("attack", "Gate|Attack", 0.05f, 50, 1, 5, "ms"));
        l.add (fparam ("hold", "Gate|Hold", 0, 500, 30, 50, "ms"));
        l.add (fparam ("release", "Gate|Release", 5, 1000, 100, 100, "ms"));
        l.add (bparam ("sidechain", "Gate|Use sidechain", false));
        return l;
    }

    std::vector<Preset> factoryPresets() const override
    {
        return { { "Noise Cleanup", { { "threshold", -50 }, { "release", 150 } } },
                 { "Tight Drums", { { "threshold", -30 }, { "hold", 10 }, { "release", 40 } } },
                 { "Trance Gate (sidechain)", { { "threshold", -30 }, { "hold", 0 }, { "release", 20 }, { "sidechain", 1 } } } };
    }

    void prepareToPlay (double s, int) override { sr = s; gain = 0; holdLeft = 0; }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        auto sc = sidechainBuffer (b);
        const bool useSc = p ("sidechain") > 0.5f && sc.getNumChannels() > 0;
        const float thr = juce::Decibels::decibelsToGain (p ("threshold"));
        const float floorG = juce::Decibels::decibelsToGain (p ("range"), -80.0f);
        const float att = 1.0f - coefFor (p ("attack") * 0.001, sr), rel = 1.0f - coefFor (p ("release") * 0.001, sr);
        const int holdS = (int) (p ("hold") * 0.001 * sr);
        const int nc = juce::jmin (2, b.getNumChannels());
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float lvl = 0;
            if (useSc) for (int c = 0; c < sc.getNumChannels(); ++c) lvl = juce::jmax (lvl, std::abs (sc.getSample (c, i)));
            else for (int c = 0; c < nc; ++c) lvl = juce::jmax (lvl, std::abs (b.getSample (c, i)));
            if (lvl > thr) holdLeft = holdS;
            const bool open = lvl > thr || holdLeft-- > 0;
            gain += ((open ? 1.0f : 0.0f) - gain) * (open ? att : rel);
            const float g = floorG + (1.0f - floorG) * gain;
            for (int c = 0; c < nc; ++c) b.setSample (c, i, b.getSample (c, i) * g);
        }
    }

private:
    float gain = 0; int holdLeft = 0;
};

//==============================================================================
class TransientProcessor : public GubeeProcessor
{
public:
    TransientProcessor() : GubeeProcessor ("Gubee Transient", false, layout()) {}

    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (fparam ("attack", "Shape|Attack", -100, 100, 0, -1, "%"));
        l.add (fparam ("sustain", "Shape|Sustain", -100, 100, 0, -1, "%"));
        l.add (fparam ("speed", "Shape|Speed", 0, 1, 0.5f));
        l.add (fparam ("out", "Shape|Output", -12, 12, 0, -1, "dB"));
        return l;
    }

    std::vector<Preset> factoryPresets() const override
    {
        return { { "More Snap", { { "attack", 60 } } }, { "Less Room", { { "sustain", -60 } } },
                 { "Fat & Long", { { "attack", -20 }, { "sustain", 60 } } }, { "Punch", { { "attack", 80 }, { "sustain", -30 } } } };
    }

    void prepareToPlay (double s, int) override { sr = s; fast = slow = 0; }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        const float sp = p ("speed");
        const float fa = coefFor (0.0005 + 0.002 * (1 - sp), sr), sa = coefFor (0.02 + 0.06 * (1 - sp), sr), rel = coefFor (0.12, sr);
        const float atk = p ("attack") / 100.0f, sus = p ("sustain") / 100.0f;
        const float out = juce::Decibels::decibelsToGain (p ("out"));
        const int nc = juce::jmin (2, b.getNumChannels());
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float lvl = 0;
            for (int c = 0; c < nc; ++c) lvl = juce::jmax (lvl, std::abs (b.getSample (c, i)));
            fast = lvl > fast ? lvl + (fast - lvl) * fa : lvl + (fast - lvl) * rel;
            slow = lvl > slow ? lvl + (slow - lvl) * sa : lvl + (slow - lvl) * rel;
            const float diffDb = juce::Decibels::gainToDecibels (fast + 1e-6f) - juce::Decibels::gainToDecibels (slow + 1e-6f);
            const float t = juce::jlimit (0.0f, 1.0f, diffDb / 12.0f);   // 1 during attacks
            const float gDb = atk * 12.0f * t + sus * 12.0f * (1.0f - t) * juce::jlimit (0.0f, 1.0f, slow * 8.0f);
            const float g = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 18.0f, gDb)) * out;
            for (int c = 0; c < nc; ++c) b.setSample (c, i, b.getSample (c, i) * g);
        }
    }

private:
    float fast = 0, slow = 0;
};

//==============================================================================
class ClipperProcessor : public GubeeProcessor
{
public:
    ClipperProcessor() : GubeeProcessor ("Gubee Clipper", false, layout()) {}

    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (fparam ("drive", "Clipper|Input", 0, 24, 0, -1, "dB"));
        l.add (fparam ("ceiling", "Clipper|Ceiling", -12, 0, -0.3f, -1, "dB"));
        l.add (fparam ("knee", "Clipper|Softness", 0, 1, 0.5f));
        l.add (fparam ("mix", "Clipper|Mix", 0, 1, 1));
        return l;
    }

    std::vector<Preset> factoryPresets() const override
    {
        return { { "Transparent", { { "drive", 2 }, { "knee", 0.8f } } }, { "Loud Drums", { { "drive", 6 }, { "knee", 0.4f } } },
                 { "808 Grit", { { "drive", 12 }, { "knee", 0.1f } } } };
    }

    void prepareToPlay (double s, int bs) override
    {
        sr = s;
        os = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
        os->initProcessing ((size_t) bs);
        dry.setSize (2, bs);
    }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        const float drive = juce::Decibels::decibelsToGain (p ("drive")), c = juce::Decibels::decibelsToGain (p ("ceiling"));
        const float knee = juce::jmax (0.001f, p ("knee")), mix = p ("mix");
        const int nc = juce::jmin (2, b.getNumChannels()), n = b.getNumSamples();
        if (dry.getNumSamples() < n) dry.setSize (2, n, false, false, true);
        for (int ch = 0; ch < nc; ++ch) dry.copyFrom (ch, 0, b, ch, 0, n);
        juce::dsp::AudioBlock<float> blk (b);
        auto sub = blk.getSubsetChannelBlock (0, (size_t) nc);
        auto up = os->processSamplesUp (sub);
        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
        {
            auto* d = up.getChannelPointer (ch);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                float x = d[i] * drive / c;
                // blend between hard clip and tanh by "softness"
                // smooth saturation curve with unity small-signal gain; harder as softness -> 0
                const float sh = 2.0f + (1.0f - knee) * 10.0f;
                float y = x / std::pow (1.0f + std::pow (std::abs (x), sh), 1.0f / sh);
                d[i] = juce::jlimit (-1.0f, 1.0f, y) * c;
            }
        }
        os->processSamplesDown (sub);
        for (int ch = 0; ch < nc; ++ch)
        {
            b.applyGain (ch, 0, n, mix);
            b.addFrom (ch, 0, dry, ch, 0, n, 1.0f - mix);
        }
    }

private:
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::AudioBuffer<float> dry;
};

//==============================================================================
/** Volume "pump": tempo-synced ducking, or follows a sidechain (e.g. the kick). */
class PumpProcessor : public GubeeProcessor
{
public:
    PumpProcessor() : GubeeProcessor ("Gubee Pump", false, layout(), true) {}

    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (cparam ("mode", "Pump|Trigger", { "Tempo", "Sidechain" }, 0));
        l.add (cparam ("rate", "Pump|Every", syncDivisions(), 8));
        l.add (fparam ("depth", "Pump|Depth", 0, 1, 0.8f));
        l.add (fparam ("curve", "Pump|Curve", 0.2f, 5, 1.5f, 1.5f));
        l.add (fparam ("duck", "Pump|Duck Time", 0.05f, 1, 0.45f));
        l.add (fparam ("threshold", "Sidechain|Threshold", -60, 0, -30, -1, "dB"));
        l.add (fparam ("release", "Sidechain|Release", 20, 1000, 200, 200, "ms"));
        return l;
    }

    std::vector<Preset> factoryPresets() const override
    {
        return { { "House Pump (1/4)", { { "rate", 8 }, { "depth", 0.85f }, { "curve", 1.5f } } },
                 { "Half-time Swell", { { "rate", 10 }, { "depth", 0.7f }, { "curve", 2.5f }, { "duck", 0.6f } } },
                 { "Fast 1/8 Chop", { { "rate", 5 }, { "depth", 0.9f }, { "curve", 0.8f }, { "duck", 0.5f } } },
                 { "Kick Sidechain", { { "mode", 1 }, { "depth", 0.85f }, { "threshold", -24 }, { "release", 180 } } } };
    }

    void prepareToPlay (double s, int) override { sr = s; phase = 0; scEnv = 0; }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        const int n = b.getNumSamples(), nc = juce::jmin (2, b.getNumChannels());
        const float depth = p ("depth"), curve = p ("curve"), duck = p ("duck");
        if ((int) p ("mode") == 1)
        {
            auto sc = sidechainBuffer (b);
            const float thr = juce::Decibels::decibelsToGain (p ("threshold"));
            const float rel = coefFor (p ("release") * 0.001, sr);
            for (int i = 0; i < n; ++i)
            {
                float lvl = 0;
                for (int c = 0; c < sc.getNumChannels(); ++c) lvl = juce::jmax (lvl, std::abs (sc.getSample (c, i)));
                const float trig = lvl > thr ? 1.0f : 0.0f;
                scEnv = trig > scEnv ? trig : scEnv * rel;
                const float g = 1.0f - depth * std::pow (scEnv, 1.0f / curve);
                for (int c = 0; c < nc; ++c) b.setSample (c, i, b.getSample (c, i) * g);
            }
            return;
        }
        const double beats = syncBeats ((int) p ("rate"));
        const double bpm = hostBpm (*this);
        const double hb = hostBeats (*this);
        if (hb >= 0) phase = std::fmod (hb, beats) / beats;
        const double inc = bpm / 60.0 / sr / beats;
        for (int i = 0; i < n; ++i)
        {
            // fully ducked at the start of each cycle, back up by "duck" fraction of the cycle
            const float t = (float) juce::jlimit (0.0, 1.0, phase / juce::jmax (0.01f, duck));
            const float g = 1.0f - depth * std::pow (1.0f - t, curve);
            const float smooth = phase < 0.004 ? (float) (phase / 0.004) : 1.0f;   // tiny attack to avoid clicks
            const float gg = 1.0f - (1.0f - g) * smooth;
            for (int c = 0; c < nc; ++c) b.setSample (c, i, b.getSample (c, i) * gg);
            phase += inc;
            if (phase >= 1.0) phase -= 1.0;
        }
    }

private:
    double phase = 0;
    float scEnv = 0;
};
}
