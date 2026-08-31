#pragma once

#include <array>

#include <juce_audio_basics/juce_audio_basics.h>

#include "Dsp.h"

namespace tape
{

/** A shared aux bus fed by every track's mono send: a tempo-synced lofi delay
    and an 8-comb/4-allpass lofi reverb. Mono in (one accumulator per FX, half
    the cost of a per-track stereo send and true to the cassette-era units
    this emulates), stereo return out. */
class LofiBus
{
public:
    void prepare (double sampleRate);

    struct Params
    {
        double delayMs = 250.0; // free-running, not tempo-synced
        float delayFeedback = 0.45f;
        float delayTone = 0.5f; // 0 = darker/tighter, 1 = brighter/wider feedback band
        float delayReturnDb = 0.0f;
        float reverbSize = 0.55f;
        float reverbDamp = 0.5f;
        float reverbReturnDb = 0.0f;
    };

    /** sendDelayIn/sendReverbIn: mono accumulators the tracks wrote into this
        block (caller-owned, already summed). outL/outR: added into, not
        overwritten, so the bus return sits behind the dry track signal. */
    void process (const Params& p, const float* sendDelayIn, const float* sendReverbIn,
                  float* outL, float* outR, int numSamples);

private:
    static constexpr int kNumCombs = 8;
    static constexpr int kNumAllpasses = 4;

    struct Comb
    {
        juce::AudioBuffer<float> buf;
        int pos = 0;
        float filterStore = 0.0f;

        void prepare (int sizeSamples)
        {
            buf.setSize (1, juce::jmax (1, sizeSamples), false, true, true);
            buf.clear();
            pos = 0;
            filterStore = 0.0f;
        }

        float process (float x, float feedback, float damp)
        {
            auto* d = buf.getWritePointer (0);
            const float y = d[pos];
            filterStore = y * (1.0f - damp) + filterStore * damp;
            d[pos] = x + filterStore * feedback;
            if (++pos >= buf.getNumSamples())
                pos = 0;
            return y;
        }
    };

    struct Allpass
    {
        juce::AudioBuffer<float> buf;
        int pos = 0;

        void prepare (int sizeSamples)
        {
            buf.setSize (1, juce::jmax (1, sizeSamples), false, true, true);
            buf.clear();
            pos = 0;
        }

        float process (float x, float feedback = 0.5f)
        {
            auto* d = buf.getWritePointer (0);
            const float bufOut = d[pos];
            const float y = -x + bufOut;
            d[pos] = x + bufOut * feedback;
            if (++pos >= buf.getNumSamples())
                pos = 0;
            return y;
        }
    };

    double sampleRate = 48000.0;

    // --- lofi delay ---
    juce::AudioBuffer<float> delayBuf; // 2ch circular, up to 2s
    int delayCapacity = 0;
    int delayWritePos = 0;
    OnePole delayHpL, delayHpR, delayLpL, delayLpR;
    float heldL = 0.0f, heldR = 0.0f;
    int holdCounter = 0;
    int holdPeriodSamples = 1;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> delayTimeSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> delayReturnSmoothed;

    // --- lofi reverb ---
    std::array<Comb, kNumCombs> combsL, combsR;
    std::array<Allpass, kNumAllpasses> allpassesL, allpassesR;
    OnePole reverbInputLp;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> reverbReturnSmoothed;
};

} // namespace tape
