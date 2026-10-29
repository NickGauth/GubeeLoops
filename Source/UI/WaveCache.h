#pragma once
#include "AppContext.h"

namespace gubee
{
/** Min/max peak overview of audio files for fast waveform drawing. */
class WaveCache
{
public:
    struct Peaks { std::vector<float> mn, mx; int perBin = 256; int length = 0; double sampleRate = 44100; };
    using Ptr = std::shared_ptr<const Peaks>;

    static WaveCache& get() { static WaveCache w; return w; }

    Ptr peaks (SampleCache& sc, const juce::File& f)
    {
        auto key = f.getFullPathName() + "@" + juce::String (f.getLastModificationTime().toMilliseconds());
        if (auto it = cache.find (key); it != cache.end()) return it->second;
        auto data = sc.get (f);
        auto p = std::make_shared<Peaks>();
        if (data != nullptr) build (*p, data->buffer, data->sampleRate);
        cache[key] = p;
        return p;
    }

    static void build (Peaks& p, const juce::AudioBuffer<float>& b, double sr)
    {
        p.sampleRate = sr;
        p.length = b.getNumSamples();
        const int bins = juce::jmax (1, p.length / p.perBin + 1);
        p.mn.assign ((size_t) bins, 0.0f); p.mx.assign ((size_t) bins, 0.0f);
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            auto* d = b.getReadPointer (c);
            for (int i = 0; i < p.length; ++i)
            {
                auto bi = (size_t) (i / p.perBin);
                p.mn[bi] = juce::jmin (p.mn[bi], d[i]);
                p.mx[bi] = juce::jmax (p.mx[bi], d[i]);
            }
        }
    }

    /** Draws samples [s0, s1) of the file into r. */
    static void draw (juce::Graphics& g, juce::Rectangle<float> r, const Peaks& p, double s0, double s1, bool reversed = false)
    {
        if (p.length == 0 || s1 <= s0 || r.getWidth() < 1) return;
        const float mid = r.getCentreY(), h = r.getHeight() * 0.5f;
        juce::Path path;
        const int w = (int) r.getWidth();
        std::vector<float> top ((size_t) w), bot ((size_t) w);
        for (int x = 0; x < w; ++x)
        {
            double a = s0 + (s1 - s0) * x / w, b = s0 + (s1 - s0) * (x + 1) / w;
            if (reversed) { a = p.length - a; b = p.length - b; std::swap (a, b); }
            int b0 = juce::jlimit (0, (int) p.mn.size() - 1, (int) (a / p.perBin));
            int b1 = juce::jlimit (b0, (int) p.mn.size() - 1, (int) (b / p.perBin));
            float mn = 0, mx = 0;
            if (a < p.length && b > 0)
                for (int i = b0; i <= b1; ++i) { mn = juce::jmin (mn, p.mn[(size_t) i]); mx = juce::jmax (mx, p.mx[(size_t) i]); }
            top[(size_t) x] = mid - juce::jmin (1.0f, mx) * h;
            bot[(size_t) x] = mid - juce::jmax (-1.0f, mn) * h;
        }
        path.startNewSubPath (r.getX(), top[0]);
        for (int x = 1; x < w; ++x) path.lineTo (r.getX() + x, top[(size_t) x]);
        for (int x = w; --x >= 0;) path.lineTo (r.getX() + x, bot[(size_t) x]);
        path.closeSubPath();
        g.fillPath (path);
    }

private:
    std::map<juce::String, Ptr> cache;
};
}
