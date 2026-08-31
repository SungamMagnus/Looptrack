#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Dsp.h"

namespace tape
{

/** Wow/flutter (a modulated delay line reading the just-played signal) and
    cassette hiss. Sits after the loop's playback head and before the EQ, so
    it colours live input, imported samples and recorded loops identically,
    and so the hiss itself gets shaped by the EQ/filter downstream like any
    other signal on the tape. */
class TapeCharacter
{
public:
    void prepare (double sampleRate);

    struct Params
    {
        float wowDepth = 0.3f, wowRate = 1.0f;         // rate: multiplier on the 0.5/0.87 Hz LFOs
        float flutterDepth = 0.25f, flutterRate = 1.0f; // rate: multiplier on the 6.3/9.7 Hz LFOs
        float hissAmount = 0.25f;
        bool transportPlaying = true; // gates the hiss -- real tape hiss stops when the tape stops
    };

    void process (const Params& p, float* l, float* r, int numSamples);

private:
    double sampleRate = 48000.0;

    // modulated delay line, shared modulation for both channels (one tape
    // transport, so both channels wobble together)
    juce::AudioBuffer<float> delayBuf; // 2ch circular
    int delayCapacity = 0;
    int delayWritePos = 0;
    double nominalDelaySamples = 0.0;
    double modLimitSamples = 0.0;

    double wowPhase1 = 0.0, wowPhase2 = 0.0;
    double flutterPhase1 = 0.0, flutterPhase2 = 0.0;
    OnePole driftFilter;
    Rng driftRng { 0x9e3779b1u };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wowSmoothed, wowRateSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> flutterSmoothed, flutterRateSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> hissSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> hissGate; // transport on/off, 200 ms

    Rng hissRngL { 0x1234567u };
    Rng hissRngR { 0x89abcdefu };
    OnePole hissHpL, hissHpR, hissLpL, hissLpR;
};

} // namespace tape
