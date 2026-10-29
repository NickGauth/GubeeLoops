#pragma once
#include <JuceHeader.h>
#include "GubeeDSP/GubeeSampler.h"

namespace gubee::tools
{
/** Time-stretch (WSOLA). timeFactor > 1 makes the audio longer. Pitch is preserved. */
juce::AudioBuffer<float> timeStretch (const juce::AudioBuffer<float>& in, double timeFactor, double sampleRate);

/** Resample by a speed ratio (ratio 2 = twice as fast / half length / octave up). */
juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& in, double speedRatio);

/** Stretch to a new duration while shifting pitch by semitones, converting inRate -> outRate. */
juce::AudioBuffer<float> stretchAndPitch (const juce::AudioBuffer<float>& in, double inRate, double outRate,
                                          double durationFactor, double semitones);

/** Onset detection. Returns sample positions (always starts with 0). sensitivity 0..1 */
std::vector<int> detectTransients (const juce::AudioBuffer<float>& b, double sr, float sensitivity, double minGapSeconds = 0.06);

/** Equal divisions ("regions"). */
std::vector<int> equalSlices (int numSamples, int count);

/** Slice points every beatsPerSlice beats at the given tempo. */
std::vector<int> gridSlices (int numSamples, double sr, double bpm, double beatsPerSlice);

/** Converts sorted slice start points to ranges. */
std::vector<juce::Range<int>> pointsToRanges (std::vector<int> points, int total);

/** Guess tempo of a loop from its length (assumes whole bars of 4/4). */
double guessLoopBpm (double lengthSeconds);

/** Tempo / key parsed from a file name, e.g. "Dark_Loop_140bpm_Cmin". 0 / empty if absent. */
double bpmFromName (const juce::String& name);
juce::String keyFromName (const juce::String& name);

// edit helpers
void reverseRange (juce::AudioBuffer<float>& b, juce::Range<int> r);
void fadeRange (juce::AudioBuffer<float>& b, juce::Range<int> r, bool fadeIn);
void gainRange (juce::AudioBuffer<float>& b, juce::Range<int> r, float gain);
void normalize (juce::AudioBuffer<float>& b, juce::Range<int> r, float targetPeak = 0.98f);
juce::AudioBuffer<float> extract (const juce::AudioBuffer<float>& b, juce::Range<int> r);
juce::AudioBuffer<float> removeRange (const juce::AudioBuffer<float>& b, juce::Range<int> r);
juce::AudioBuffer<float> insertAt (const juce::AudioBuffer<float>& b, int pos, const juce::AudioBuffer<float>& ins);

bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr, int bits = 24);
juce::File uniqueFile (const juce::File& dir, const juce::String& baseName, const juce::String& ext);

/** Writes the generated Gubee Starter Kit into dir (if missing). */
void generateStarterKit (const juce::File& dir);
}

namespace gubee
{
/** Loads and caches decoded audio files. Thread-safe. */
class SampleCache
{
public:
    SampleCache() { formats.registerBasicFormats(); }
    SampleDataPtr get (const juce::File& f);
    void invalidate (const juce::File& f);
    juce::AudioFormatManager formats;

private:
    juce::CriticalSection lock;
    std::map<juce::String, std::pair<juce::Time, SampleDataPtr>> cache;
};
}
