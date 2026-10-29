#pragma once
#include "GubeeBase.h"

namespace gubee
{
/** Immutable decoded audio shared between the sample cache, sampler and editors. */
struct SampleData
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 44100.0;
    juce::File file;
    double lengthSeconds() const { return buffer.getNumSamples() / sampleRate; }
};
using SampleDataPtr = std::shared_ptr<const SampleData>;

/** The built-in sampler: one-shot / keyboard playback, ADSR, reverse, slices. */
class SamplerProcessor : public GubeeProcessor
{
public:
    SamplerProcessor() : GubeeProcessor ("Gubee Sampler", true, layout()) {}

    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (fparam ("gain", "Gain", 0, 2, 1));
        l.add (fparam ("pitch", "Pitch", -24, 24, 0, -1, "st"));
        l.add (fparam ("fine", "Fine", -100, 100, 0, -1, "ct"));
        l.add (fparam ("attack", "Attack", 0, 2, 0.0f, 0.2f, "s"));
        l.add (fparam ("decay", "Decay", 0.001f, 4, 0.5f, 0.5f, "s"));
        l.add (fparam ("sustain", "Sustain", 0, 1, 1));
        l.add (fparam ("release", "Release", 0.001f, 4, 0.03f, 0.3f, "s"));
        l.add (fparam ("start", "Start", 0, 1, 0));
        l.add (bparam ("reverse", "Reverse", false));
        l.add (bparam ("oneshot", "One Shot", true));
        l.add (bparam ("cut", "Cut Itself", false));
        l.add (bparam ("keytrack", "Key Track", true));
        return l;
    }

    void setSample (SampleDataPtr s)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        sample = std::move (s);
    }

    SampleDataPtr getSample() const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return sample;
    }

    /** Slices are sample ranges in the source; note 60 + i plays slice i. Empty = whole-sample mode. */
    void setSlices (std::vector<juce::Range<int>> s)
    {
        auto ptr = std::make_shared<const std::vector<juce::Range<int>>> (std::move (s));
        const juce::SpinLock::ScopedLockType sl (lock);
        slices = std::move (ptr);
    }

    std::vector<juce::Range<int>> getSlices() const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return slices ? *slices : std::vector<juce::Range<int>> {};
    }

    int rootNote = 60;

    void prepareToPlay (double s, int) override
    {
        sr = s;
        for (auto& v : voices) { v.active = false; v.env.setSampleRate (s); }
    }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals nd;
        b.clear();
        SampleDataPtr smp;
        std::shared_ptr<const std::vector<juce::Range<int>>> sl;
        {
            const juce::SpinLock::ScopedLockType l (lock);
            smp = sample; sl = slices;
        }
        int pos = 0;
        for (const auto meta : midi)
        {
            renderVoices (b, pos, meta.samplePosition - pos);
            pos = meta.samplePosition;
            auto m = meta.getMessage();
            if (m.isNoteOn() && smp != nullptr) startVoice (m.getNoteNumber(), m.getFloatVelocity(), smp, sl.get());
            else if (m.isNoteOff())
            {
                if (p ("oneshot") < 0.5f)
                    for (auto& v : voices) if (v.active && v.note == m.getNoteNumber() && ! v.released) { v.env.noteOff(); v.released = true; }
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                for (auto& v : voices) if (v.active) { v.env.noteOff(); v.released = true; }
        }
        renderVoices (b, pos, b.getNumSamples() - pos);
    }

private:
    struct Voice
    {
        bool active = false, released = false, reverse = false;
        int note = 0;
        double pos = 0, inc = 1;
        int startS = 0, endS = 0;
        float gain = 1;
        juce::ADSR env;
        SampleDataPtr data;
        float fadeOut = 1, fadeStep = 0;
    };

    void startVoice (int note, float vel, const SampleDataPtr& smp, const std::vector<juce::Range<int>>* sl)
    {
        const int total = smp->buffer.getNumSamples();
        if (total < 2) return;
        juce::Range<int> region (0, total);
        bool sliceMode = sl != nullptr && ! sl->empty();
        if (sliceMode)
        {
            int idx = note - rootNote;
            if (idx < 0 || idx >= (int) sl->size()) return;
            region = (*sl)[(size_t) idx].getIntersectionWith ({ 0, total });
            if (region.getLength() < 2) return;
        }
        else
        {
            int st = (int) (p ("start") * (float) total);
            region.setStart (juce::jlimit (0, total - 2, st));
        }

        if (p ("cut") > 0.5f || sliceMode)
            for (auto& v : voices) if (v.active && (! sliceMode || p ("cut") > 0.5f || v.note == note)) { v.fadeStep = 1.0f / 64.0f; }

        Voice* v = nullptr;
        for (auto& cand : voices) if (! cand.active) { v = &cand; break; }
        if (v == nullptr)
        {
            v = &voices[0];
            for (auto& cand : voices) if (cand.pos > v->pos) v = &cand;
        }
        double semis = p ("pitch") + p ("fine") / 100.0;
        if (! sliceMode && p ("keytrack") > 0.5f) semis += note - rootNote;
        v->inc = std::pow (2.0, semis / 12.0) * smp->sampleRate / sr;
        v->reverse = p ("reverse") > 0.5f;
        v->startS = region.getStart(); v->endS = region.getEnd();
        v->pos = v->reverse ? (double) (v->endS - 1) : (double) v->startS;
        v->note = note;
        v->gain = vel * p ("gain");
        v->data = smp;
        v->released = false;
        v->fadeOut = 1.0f; v->fadeStep = 0.0f;
        v->env.setSampleRate (sr);
        v->env.setParameters ({ juce::jmax (0.0005f, p ("attack")), p ("decay"), p ("sustain"), p ("release") });
        v->env.reset();
        v->env.noteOn();
        v->active = true;
    }

    void renderVoices (juce::AudioBuffer<float>& b, int start, int len)
    {
        if (len <= 0) return;
        const int outCh = b.getNumChannels();
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const auto& buf = v.data->buffer;
            const int srcCh = buf.getNumChannels();
            for (int i = start; i < start + len; ++i)
            {
                int i0 = (int) v.pos;
                if ((! v.reverse && i0 + 1 >= v.endS) || (v.reverse && i0 <= v.startS)) { v.active = false; v.data.reset(); break; }
                float fr = (float) (v.pos - i0);
                int i1 = v.reverse ? i0 - 1 : i0 + 1;
                if (v.reverse) fr = 0.0f;
                float e = v.env.getNextSample() * v.gain * v.fadeOut;
                if (v.fadeStep > 0) { v.fadeOut -= v.fadeStep; if (v.fadeOut <= 0) { v.active = false; v.data.reset(); break; } }
                for (int c = 0; c < outCh; ++c)
                {
                    int sc = juce::jmin (c, srcCh - 1);
                    float a = buf.getSample (sc, i0), bb = buf.getSample (sc, i1);
                    b.addSample (c, i, (a + fr * (bb - a)) * e);
                }
                v.pos += v.reverse ? -v.inc : v.inc;
                if (! v.env.isActive()) { v.active = false; v.data.reset(); break; }
            }
        }
    }

    mutable juce::SpinLock lock;
    SampleDataPtr sample;
    std::shared_ptr<const std::vector<juce::Range<int>>> slices;
    std::array<Voice, 32> voices;
};
}
