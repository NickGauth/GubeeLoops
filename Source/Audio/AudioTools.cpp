#include "AudioTools.h"

namespace gubee::tools
{
using Buffer = juce::AudioBuffer<float>;

Buffer timeStretch (const Buffer& in, double timeFactor, double sr)
{
    const int inLen = in.getNumSamples(), nch = juce::jmax (1, in.getNumChannels());
    if (inLen < 64 || std::abs (timeFactor - 1.0) < 1.0e-4) return in;

    const int frame = juce::jmax (256, (int) (sr * 0.040)) & ~1;
    const int hopOut = frame / 2;
    const double hopIn = hopOut / timeFactor;
    const int tol = (int) (sr * 0.012);
    const int outLen = (int) std::ceil (inLen * timeFactor);

    Buffer out (nch, outLen + frame);
    out.clear();
    std::vector<float> norm ((size_t) (outLen + frame), 0.0f);
    std::vector<float> win ((size_t) frame);
    for (int i = 0; i < frame; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (float) frame);

    // mono analysis signal
    std::vector<float> mono ((size_t) inLen, 0.0f);
    for (int c = 0; c < in.getNumChannels(); ++c)
    {
        auto* d = in.getReadPointer (c);
        for (int i = 0; i < inLen; ++i) mono[(size_t) i] += d[i];
    }
    auto at = [&] (int i) { return (i >= 0 && i < inLen) ? mono[(size_t) i] : 0.0f; };

    int prevSrc = 0;
    for (int k = 0;; ++k)
    {
        const int outPos = k * hopOut;
        if (outPos >= outLen) break;
        int nominal = (int) std::round (k * hopIn);
        int best = nominal;
        if (k > 0)
        {
            const int natural = prevSrc + hopOut;     // where the previous frame "would continue"
            double bestScore = -1.0e30;
            const int ov = hopOut;
            for (int d = -tol; d <= tol; d += 2)
            {
                int cand = nominal + d;
                if (cand < 0 || cand + frame >= inLen + frame) continue;
                double score = 0;
                for (int j = 0; j < ov; j += 3) score += at (cand + j) * at (natural + j);
                if (score > bestScore) { bestScore = score; best = cand; }
            }
        }
        best = juce::jmax (0, best);
        for (int c = 0; c < nch; ++c)
        {
            auto* src = in.getReadPointer (juce::jmin (c, in.getNumChannels() - 1));
            auto* dst = out.getWritePointer (c);
            for (int j = 0; j < frame; ++j)
            {
                int si = best + j;
                if (si >= inLen) break;
                dst[outPos + j] += src[si] * win[(size_t) j];
            }
        }
        for (int j = 0; j < frame; ++j) norm[(size_t) (outPos + j)] += win[(size_t) j];
        prevSrc = best;
    }
    for (int c = 0; c < nch; ++c)
    {
        auto* d = out.getWritePointer (c);
        for (int i = 0; i < outLen; ++i) d[i] /= juce::jmax (0.15f, norm[(size_t) i]);
    }
    out.setSize (nch, outLen, true, false, false);
    return out;
}

Buffer resample (const Buffer& in, double speedRatio)
{
    const int inLen = in.getNumSamples();
    if (std::abs (speedRatio - 1.0) < 1.0e-6 || inLen == 0) return in;
    const int outLen = juce::jmax (1, (int) std::floor (inLen / speedRatio));
    Buffer out (in.getNumChannels(), outLen);
    for (int c = 0; c < in.getNumChannels(); ++c)
    {
        // pad input so the interpolator never reads past the end
        std::vector<float> padded ((size_t) inLen + 8, 0.0f);
        std::copy (in.getReadPointer (c), in.getReadPointer (c) + inLen, padded.begin());
        juce::LagrangeInterpolator interp;
        interp.process (speedRatio, padded.data(), out.getWritePointer (c), outLen);
    }
    return out;
}

Buffer stretchAndPitch (const Buffer& in, double inRate, double outRate, double durationFactor, double semitones)
{
    const double pitchRatio = std::pow (2.0, semitones / 12.0);
    Buffer b = in;
    // stretch so that, after resampling by pitchRatio * (inRate/outRate), the duration equals durationFactor * original.
    const double stretch = durationFactor * pitchRatio;
    if (std::abs (stretch - 1.0) > 1.0e-4) b = timeStretch (b, stretch, inRate);
    const double speed = pitchRatio * (inRate / outRate);
    return resample (b, speed);
}

std::vector<int> detectTransients (const Buffer& b, double sr, float sensitivity, double minGapSeconds)
{
    std::vector<int> pts { 0 };
    const int n = b.getNumSamples();
    const int hop = 256;
    const int frames = n / hop;
    if (frames < 6) return pts;
    // log energy of the high-passed (first difference) signal per frame
    std::vector<float> e ((size_t) frames, 0.0f);
    for (int f = 0; f < frames; ++f)
    {
        double sum = 0;
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            auto* d = b.getReadPointer (c);
            for (int i = juce::jmax (1, f * hop); i < (f + 1) * hop; ++i) { float hp = d[i] - d[i - 1]; sum += hp * hp; }
        }
        e[(size_t) f] = (float) std::log10 (1.0e-9 + sum);
    }
    const float emax = *std::max_element (e.begin(), e.end());
    const float thr = 0.15f + (1.0f - juce::jlimit (0.0f, 1.0f, sensitivity)) * 0.6f;   // log10 units (1 = 10 dB)
    const int minGap = juce::jmax (1, (int) (minGapSeconds * sr / hop));
    int last = -minGap;
    auto peakAbs = [&] (int i) { float m = 0; for (int c = 0; c < b.getNumChannels(); ++c) m = juce::jmax (m, std::abs (b.getSample (c, i))); return m; };
    for (int f = 3; f < frames - 1; ++f)
    {
        const float prev = (e[(size_t) f - 1] + e[(size_t) f - 2] + e[(size_t) f - 3]) / 3.0f;
        const float flux = e[(size_t) f] - prev;
        if (flux > thr && e[(size_t) f] >= e[(size_t) f + 1] - 0.05f && e[(size_t) f] > emax - 3.5f && f - last >= minGap)
        {
            int g = f;
            while (g > 1 && e[(size_t) g - 1] < e[(size_t) g] - 0.05f && f - g < 3) --g;
            // refine to the first sample that rises above 20% of the local peak
            const int s0 = juce::jmax (0, (g - 1) * hop), s1 = juce::jmin (n, (f + 2) * hop);
            float local = 0, pre = 0;
            for (int i = s0; i < s1; ++i) local = juce::jmax (local, peakAbs (i));
            for (int i = juce::jmax (0, s0 - hop); i < s0; ++i) pre = juce::jmax (pre, peakAbs (i));
            const float level = juce::jmax (local * 0.2f, pre * 1.3f);
            int pos = g * hop;
            if (level < local)
                for (int i = s0; i < s1; ++i) if (peakAbs (i) > level) { pos = juce::jmax (0, i - 16); break; }
            if (pos > 0 && pos - pts.back() > hop) pts.push_back (pos);
            last = f;
        }
    }
    return pts;
}

std::vector<int> equalSlices (int numSamples, int count)
{
    std::vector<int> pts;
    count = juce::jmax (1, count);
    for (int i = 0; i < count; ++i) pts.push_back ((int) ((juce::int64) numSamples * i / count));
    return pts;
}

std::vector<int> gridSlices (int numSamples, double sr, double bpm, double beatsPerSlice)
{
    std::vector<int> pts;
    const double step = beatsPerSlice * 60.0 / juce::jmax (1.0, bpm) * sr;
    for (double p = 0; p < numSamples - 16; p += step) pts.push_back ((int) p);
    if (pts.empty()) pts.push_back (0);
    return pts;
}

std::vector<juce::Range<int>> pointsToRanges (std::vector<int> points, int total)
{
    std::sort (points.begin(), points.end());
    points.erase (std::unique (points.begin(), points.end()), points.end());
    std::vector<juce::Range<int>> r;
    for (size_t i = 0; i < points.size(); ++i)
    {
        int s = juce::jlimit (0, total, points[i]);
        int e = i + 1 < points.size() ? juce::jlimit (0, total, points[i + 1]) : total;
        if (e - s > 32) r.push_back ({ s, e });
    }
    return r;
}

double guessLoopBpm (double len)
{
    if (len <= 0.2) return 0;
    double best = 0, bestScore = 1.0e9;
    for (int bars : { 1, 2, 4, 8, 16 })
        for (double beatsPerBar : { 4.0 })
        {
            double bpm = 60.0 * bars * beatsPerBar / len;
            if (bpm < 60 || bpm > 200) continue;
            double score = std::abs (bpm - 125.0);
            if (score < bestScore) { bestScore = score; best = bpm; }
        }
    return best > 0 ? std::round (best * 100.0) / 100.0 : 0.0;
}

double bpmFromName (const juce::String& nameIn)
{
    auto name = nameIn.toLowerCase();
    // "140bpm", "140 bpm", "bpm140", "_140_"
    for (int i = 0; i < name.length(); ++i)
    {
        if (! juce::CharacterFunctions::isDigit (name[i])) continue;
        int j = i;
        while (j < name.length() && juce::CharacterFunctions::isDigit (name[j])) ++j;
        auto num = name.substring (i, j).getIntValue();
        auto after = name.substring (j).trimStart();
        auto before = name.substring (0, i).trimEnd();
        bool tagged = after.startsWith ("bpm") || before.endsWith ("bpm");
        bool delimited = (i == 0 || ! juce::CharacterFunctions::isLetterOrDigit (name[i - 1]))
                         && (j >= name.length() || ! juce::CharacterFunctions::isLetterOrDigit (name[j]));
        if (num >= 60 && num <= 200 && (tagged || (delimited && j - i == 3) || (delimited && num >= 70 && num <= 99 && j - i == 2)))
            return num;
        i = j;
    }
    return 0;
}

juce::String keyFromName (const juce::String& name)
{
    static const char* notes[] = { "C#", "Db", "D#", "Eb", "F#", "Gb", "G#", "Ab", "A#", "Bb", "C", "D", "E", "F", "G", "A", "B" };
    auto tokens = juce::StringArray::fromTokens (name.replaceCharacters ("-.()[]", "______"), "_ ", {});
    for (auto t : tokens)
    {
        for (auto* n : notes)
        {
            juce::String ns (n);
            if (! t.startsWith (ns)) continue;
            auto rest = t.substring (ns.length()).toLowerCase();
            if (rest == "m" || rest == "min" || rest == "minor") return ns + "m";
            if (rest.isEmpty() || rest == "maj" || rest == "major") return ns;
        }
    }
    return {};
}

//==============================================================================
void reverseRange (Buffer& b, juce::Range<int> r)
{
    r = r.getIntersectionWith ({ 0, b.getNumSamples() });
    for (int c = 0; c < b.getNumChannels(); ++c)
        std::reverse (b.getWritePointer (c) + r.getStart(), b.getWritePointer (c) + r.getEnd());
}

void fadeRange (Buffer& b, juce::Range<int> r, bool fadeIn)
{
    r = r.getIntersectionWith ({ 0, b.getNumSamples() });
    if (r.getLength() < 2) return;
    for (int c = 0; c < b.getNumChannels(); ++c)
        if (fadeIn) b.applyGainRamp (c, r.getStart(), r.getLength(), 0.0f, 1.0f);
        else        b.applyGainRamp (c, r.getStart(), r.getLength(), 1.0f, 0.0f);
}

void gainRange (Buffer& b, juce::Range<int> r, float gain)
{
    r = r.getIntersectionWith ({ 0, b.getNumSamples() });
    b.applyGain (r.getStart(), r.getLength(), gain);
}

void normalize (Buffer& b, juce::Range<int> r, float target)
{
    r = r.getIntersectionWith ({ 0, b.getNumSamples() });
    float peak = b.getMagnitude (r.getStart(), r.getLength());
    if (peak > 1.0e-6f) gainRange (b, r, target / peak);
}

Buffer extract (const Buffer& b, juce::Range<int> r)
{
    r = r.getIntersectionWith ({ 0, b.getNumSamples() });
    Buffer out (b.getNumChannels(), juce::jmax (1, r.getLength()));
    out.clear();
    for (int c = 0; c < b.getNumChannels(); ++c) out.copyFrom (c, 0, b, c, r.getStart(), r.getLength());
    return out;
}

Buffer removeRange (const Buffer& b, juce::Range<int> r)
{
    r = r.getIntersectionWith ({ 0, b.getNumSamples() });
    const int n = b.getNumSamples() - r.getLength();
    Buffer out (b.getNumChannels(), juce::jmax (1, n));
    out.clear();
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        out.copyFrom (c, 0, b, c, 0, r.getStart());
        if (b.getNumSamples() - r.getEnd() > 0)
            out.copyFrom (c, r.getStart(), b, c, r.getEnd(), b.getNumSamples() - r.getEnd());
    }
    return out;
}

Buffer insertAt (const Buffer& b, int pos, const Buffer& ins)
{
    pos = juce::jlimit (0, b.getNumSamples(), pos);
    const int nch = juce::jmax (b.getNumChannels(), ins.getNumChannels());
    Buffer out (nch, b.getNumSamples() + ins.getNumSamples());
    out.clear();
    for (int c = 0; c < nch; ++c)
    {
        int bc = juce::jmin (c, b.getNumChannels() - 1), ic = juce::jmin (c, ins.getNumChannels() - 1);
        out.copyFrom (c, 0, b, bc, 0, pos);
        out.copyFrom (c, pos, ins, ic, 0, ins.getNumSamples());
        out.copyFrom (c, pos + ins.getNumSamples(), b, bc, pos, b.getNumSamples() - pos);
    }
    return out;
}

bool writeWav (const juce::File& f, const Buffer& b, double sr, int bits)
{
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) return false;
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (b.getNumChannels()).withBitsPerSample (bits);
    auto writer = wav.createWriterFor (os, opts);
    if (writer == nullptr) return false;
    return writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

juce::File uniqueFile (const juce::File& dir, const juce::String& baseName, const juce::String& ext)
{
    dir.createDirectory();
    auto safe = juce::File::createLegalFileName (baseName);
    auto f = dir.getChildFile (safe + ext);
    for (int i = 2; f.exists(); ++i) f = dir.getChildFile (safe + " " + juce::String (i) + ext);
    return f;
}

//==============================================================================
void generateStarterKit (const juce::File& dir)
{
    if (dir.getChildFile ("Kick.wav").existsAsFile()) return;
    dir.createDirectory();
    const double sr = 44100.0;
    juce::Random rnd (1234);
    auto make = [&] (double secs) { Buffer b (1, (int) (secs * sr)); b.clear(); return b; };
    auto finish = [&] (Buffer& b, const juce::String& name)
    {
        normalize (b, { 0, b.getNumSamples() }, 0.9f);
        fadeRange (b, { b.getNumSamples() - 64, b.getNumSamples() }, false);
        writeWav (dir.getChildFile (name + ".wav"), b, sr);
    };
    auto filtered = [&] (Buffer& b, juce::IIRCoefficients co)
    {
        juce::IIRFilter f; f.setCoefficients (co);
        f.processSamples (b.getWritePointer (0), b.getNumSamples());
    };

    { // Kick
        auto b = make (0.6); double ph = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            double t = i / sr;
            double f = 45 + 110 * std::exp (-t * 28.0);
            ph += f / sr;
            float env = (float) std::exp (-t * 6.5);
            float click = (float) (std::exp (-t * 400.0) * 0.5);
            b.setSample (0, i, (float) std::tanh (std::sin (ph * juce::MathConstants<double>::twoPi) * 1.6) * env + click);
        }
        finish (b, "Kick");
    }
    { // Snare
        auto b = make (0.35); double ph = 0;
        auto noise = make (0.35);
        for (int i = 0; i < noise.getNumSamples(); ++i) noise.setSample (0, i, rnd.nextFloat() * 2 - 1);
        filtered (noise, juce::IIRCoefficients::makeBandPass (sr, 3500, 0.7));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            double t = i / sr;
            ph += (180 + 60 * std::exp (-t * 40)) / sr;
            float tone = (float) (std::sin (ph * juce::MathConstants<double>::twoPi) * std::exp (-t * 30));
            b.setSample (0, i, tone * 0.6f + noise.getSample (0, i) * (float) std::exp (-t * 14) * 1.4f);
        }
        finish (b, "Snare");
    }
    { // Clap
        auto b = make (0.45);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            double t = i / sr;
            float env = 0;
            for (double off : { 0.0, 0.011, 0.022 }) if (t >= off) env = juce::jmax (env, (float) std::exp (-(t - off) * 180));
            if (t >= 0.033) env = juce::jmax (env, (float) std::exp (-(t - 0.033) * 11) * 0.7f);
            b.setSample (0, i, (rnd.nextFloat() * 2 - 1) * env);
        }
        filtered (b, juce::IIRCoefficients::makeBandPass (sr, 1300, 1.2));
        finish (b, "Clap");
    }
    auto hat = [&] (double secs, double decay, const juce::String& name)
    {
        auto b = make (secs);
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, (rnd.nextFloat() * 2 - 1) * (float) std::exp (-(i / sr) * decay));
        filtered (b, juce::IIRCoefficients::makeHighPass (sr, 7500));
        filtered (b, juce::IIRCoefficients::makeHighPass (sr, 7500));
        finish (b, name);
    };
    hat (0.09, 55, "Hat Closed");
    hat (0.5, 7, "Hat Open");
    hat (1.6, 2.4, "Crash");
    { // Rim
        auto b = make (0.08); double ph = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            double t = i / sr; ph += 1700 / sr;
            b.setSample (0, i, (float) (std::sin (ph * juce::MathConstants<double>::twoPi) * std::exp (-t * 90) + (rnd.nextFloat() - 0.5) * std::exp (-t * 300)));
        }
        finish (b, "Rim");
    }
    auto tom = [&] (double f0, const juce::String& name)
    {
        auto b = make (0.5); double ph = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            double t = i / sr; ph += (f0 + f0 * 0.6 * std::exp (-t * 25)) / sr;
            b.setSample (0, i, (float) (std::sin (ph * juce::MathConstants<double>::twoPi) * std::exp (-t * 8)));
        }
        finish (b, name);
    };
    tom (95, "Tom Low");
    tom (160, "Tom High");
    { // Shaker
        auto b = make (0.15);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            double t = i / sr;
            float env = (float) (std::sin (juce::jmin (1.0, t / 0.15) * juce::MathConstants<double>::pi));
            b.setSample (0, i, (rnd.nextFloat() * 2 - 1) * env * env);
        }
        filtered (b, juce::IIRCoefficients::makeHighPass (sr, 5000));
        finish (b, "Shaker");
    }
    { // Perc (cowbell-ish)
        auto b = make (0.3); double p1 = 0, p2 = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            double t = i / sr; p1 += 540 / sr; p2 += 800 / sr;
            float sq = (p1 - std::floor (p1) < 0.5 ? 1.0f : -1.0f) + (p2 - std::floor (p2) < 0.5 ? 1.0f : -1.0f);
            b.setSample (0, i, sq * (float) std::exp (-t * 14) * 0.5f);
        }
        filtered (b, juce::IIRCoefficients::makeBandPass (sr, 900, 1.5));
        finish (b, "Perc");
    }
    { // Snap
        auto b = make (0.2);
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, (rnd.nextFloat() * 2 - 1) * (float) std::exp (-(i / sr) * 45));
        filtered (b, juce::IIRCoefficients::makeBandPass (sr, 2400, 2.0));
        finish (b, "Snap");
    }
    { // 808 (F1)
        auto b = make (1.8); double ph = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            double t = i / sr;
            double f = 43.65 * std::pow (2.0, 1.0 * std::exp (-t * 30));
            ph += f / sr;
            float env = (float) (std::exp (-t * 1.6) * juce::jmin (1.0, t / 0.002));
            b.setSample (0, i, (float) std::tanh (std::sin (ph * juce::MathConstants<double>::twoPi) * 2.0) * env);
        }
        finish (b, "808 F");
    }
}
}

//==============================================================================
namespace gubee
{
SampleDataPtr SampleCache::get (const juce::File& f)
{
    if (! f.existsAsFile()) return nullptr;
    const auto key = f.getFullPathName();
    const auto mod = f.getLastModificationTime();
    {
        const juce::ScopedLock sl (lock);
        auto it = cache.find (key);
        if (it != cache.end() && it->second.first == mod) return it->second.second;
    }
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr) return nullptr;
    const auto len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60 * 20));
    auto data = std::make_shared<SampleData>();
    data->buffer.setSize ((int) juce::jlimit (1u, 2u, reader->numChannels), juce::jmax (1, len));
    data->buffer.clear();
    reader->read (&data->buffer, 0, len, 0, true, data->buffer.getNumChannels() > 1);
    data->sampleRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    data->file = f;
    SampleDataPtr result = data;
    const juce::ScopedLock sl (lock);
    cache[key] = { mod, result };
    return result;
}

void SampleCache::invalidate (const juce::File& f)
{
    const juce::ScopedLock sl (lock);
    cache.erase (f.getFullPathName());
}
}
