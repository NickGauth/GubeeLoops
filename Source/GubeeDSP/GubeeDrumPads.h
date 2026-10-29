#pragma once
#include "GubeeSampler.h"

namespace gubee
{
/** 16-pad drum instrument. Pad i plays on MIDI note 36 + i. Each pad can be rendered to its own buffer
    so the host can route it to a separate mixer track (per-pad effects, like a drum rack). */
class DrumPadsProcessor : public GubeeProcessor
{
public:
    static constexpr int numPads = 16;
    static constexpr int baseNote = 36;

    struct Pad
    {
        SampleDataPtr sample;
        juce::Range<int> region;          // empty = whole sample
        float gain = 1.0f, pan = 0.0f, pitch = 0.0f;
        bool reverse = false;
        int choke = 0;                    // 0 = none; pads sharing a group cut each other (open/closed hats)
        int output = -1;                  // mixer track, -1 = channel's track
    };

    DrumPadsProcessor() : GubeeProcessor ("Gubee Drum Pads", true, layout()) {}

    static APVTS::ParameterLayout layout()
    {
        APVTS::ParameterLayout l;
        l.add (fparam ("gain", "Master", 0, 2, 1));
        l.add (fparam ("release", "Release", 0.001f, 1, 0.02f, 0.1f, "s"));
        return l;
    }

    void setPad (int i, Pad p)
    {
        if (i < 0 || i >= numPads) return;
        const juce::SpinLock::ScopedLockType sl (lock);
        pads[(size_t) i] = std::move (p);
    }

    Pad getPad (int i) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return pads[(size_t) juce::jlimit (0, numPads - 1, i)];
    }

    void prepareToPlay (double s, int bs) override
    {
        sr = s;
        for (auto& b : padOut) b.setSize (2, bs);
        for (auto& v : voices) v.active = false;
    }

    /** Renders each pad into padOutput(i). */
    void renderPads (juce::MidiBuffer& midi, int numSamples)
    {
        juce::ScopedNoDenormals nd;
        for (auto& b : padOut)
        {
            if (b.getNumSamples() < numSamples) b.setSize (2, numSamples, false, false, true);
            b.clear();
        }
        std::array<Pad, numPads> local;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            local = pads;
        }
        int pos = 0;
        for (const auto meta : midi)
        {
            render (local, pos, meta.samplePosition - pos);
            pos = meta.samplePosition;
            auto m = meta.getMessage();
            if (m.isNoteOn()) trigger (local, m.getNoteNumber() - baseNote, m.getFloatVelocity());
            else if (m.isAllSoundOff()) for (auto& v : voices) v.active = false;
        }
        render (local, pos, numSamples - pos);
    }

    juce::AudioBuffer<float>& padOutput (int i) { return padOut[(size_t) i]; }
    int padOutputTarget (int i) const { const juce::SpinLock::ScopedLockType sl (lock); return pads[(size_t) i].output; }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) override
    {
        renderPads (midi, b.getNumSamples());
        b.clear();
        for (auto& pb : padOut)
            for (int c = 0; c < b.getNumChannels(); ++c) b.addFrom (c, 0, pb, juce::jmin (c, 1), 0, b.getNumSamples());
    }

    /** Lets the UI flash pads that just played. */
    std::array<std::atomic<int>, numPads> hits {};

private:
    struct Voice
    {
        bool active = false;
        int pad = 0;
        double pos = 0, inc = 1;
        int startS = 0, endS = 0;
        float gl = 1, gr = 1, fade = 1, fadeStep = 0;
        bool reverse = false;
        SampleDataPtr data;
    };

    void trigger (const std::array<Pad, numPads>& local, int pad, float vel)
    {
        if (pad < 0 || pad >= numPads) return;
        const auto& pd = local[(size_t) pad];
        if (pd.sample == nullptr || pd.sample->buffer.getNumSamples() < 2) return;
        hits[(size_t) pad].fetch_add (1);
        if (pd.choke > 0)
            for (auto& v : voices)
                if (v.active && local[(size_t) v.pad].choke == pd.choke) v.fadeStep = 1.0f / 96.0f;

        Voice* v = nullptr;
        for (auto& c : voices) if (! c.active) { v = &c; break; }
        if (v == nullptr) { v = &voices[0]; for (auto& c : voices) if (c.pos > v->pos) v = &c; }
        const int total = pd.sample->buffer.getNumSamples();
        auto region = pd.region.isEmpty() ? juce::Range<int> (0, total) : pd.region.getIntersectionWith ({ 0, total });
        if (region.getLength() < 2) return;
        v->pad = pad;
        v->data = pd.sample;
        v->startS = region.getStart(); v->endS = region.getEnd();
        v->reverse = pd.reverse;
        v->pos = pd.reverse ? v->endS - 2 : v->startS;
        v->inc = std::pow (2.0, pd.pitch / 12.0) * pd.sample->sampleRate / sr;
        const float g = vel * pd.gain;
        const float pan = juce::jlimit (-1.0f, 1.0f, pd.pan);
        v->gl = g * std::sqrt (0.5f * (1.0f - pan)) * 1.4142f;
        v->gr = g * std::sqrt (0.5f * (1.0f + pan)) * 1.4142f;
        v->fade = 1.0f; v->fadeStep = 0.0f;
        v->active = true;
    }

    void render (const std::array<Pad, numPads>&, int start, int len)
    {
        if (len <= 0) return;
        const float masterGain = p ("gain");
        for (auto& v : voices)
        {
            if (! v.active) continue;
            auto& out = padOut[(size_t) v.pad];
            const auto& buf = v.data->buffer;
            const int ch = buf.getNumChannels();
            for (int i = start; i < start + len; ++i)
            {
                int i0 = (int) v.pos;
                if (v.reverse ? (i0 <= v.startS) : (i0 + 1 >= v.endS)) { v.active = false; v.data.reset(); break; }
                float fr = v.reverse ? 0.0f : (float) (v.pos - i0);
                int i1 = v.reverse ? i0 : i0 + 1;
                float l = buf.getSample (0, i0) + fr * (buf.getSample (0, i1) - buf.getSample (0, i0));
                float r = ch > 1 ? buf.getSample (1, i0) + fr * (buf.getSample (1, i1) - buf.getSample (1, i0)) : l;
                // short anti-click fade at region end
                int remain = v.reverse ? i0 - v.startS : v.endS - i0;
                float tail = remain < 64 ? remain / 64.0f : 1.0f;
                float g = v.fade * tail * masterGain;
                out.addSample (0, i, l * v.gl * g);
                out.addSample (1, i, r * v.gr * g);
                v.pos += v.reverse ? -v.inc : v.inc;
                if (v.fadeStep > 0) { v.fade -= v.fadeStep; if (v.fade <= 0) { v.active = false; v.data.reset(); break; } }
            }
        }
    }

    mutable juce::SpinLock lock;
    std::array<Pad, numPads> pads;
    std::array<juce::AudioBuffer<float>, numPads> padOut;
    std::array<Voice, 48> voices;
};
}
