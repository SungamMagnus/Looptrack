#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Dsp.h"

namespace tape
{

/** 3-band EQ, a DJ-style lo/hi-pass filter, pan, and the two FX sends -- in
    that order, so sweeping the filter sweeps what feeds the sends too. Level
    is set upstream at the input preamp (InputStage), not here -- this strip
    only shapes and routes what the loop already captured. */
class ChannelStrip
{
public:
    struct Params
    {
        float eqLowDb = 0.0f, eqMidDb = 0.0f, eqHighDb = 0.0f;
        float filterKnob = 0.0f; // -1 (full LP) .. 0 (bypass) .. +1 (full HP)
        float pan = 0.0f; // -1 (L) .. +1 (R)
        float sendDelayDb = -60.0f;
        float sendReverbDb = -60.0f;
    };

    void prepare (double sampleRate);

    /** l/r processed in place. Adds this track's mono send (post-EQ/filter,
        pre-pan) into sendDelayAcc/sendReverbAcc, which the caller has
        already zeroed for the block and shares across every track. */
    void process (const Params& p, float* l, float* r, int numSamples,
                  float* sendDelayAcc, float* sendReverbAcc);

private:
    double sampleRate = 48000.0;

    Biquad eqLowL, eqLowR, eqMidL, eqMidR, eqHighL, eqHighR;
    Svf filterL, filterR;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> panSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> filterSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> sendDelaySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> sendReverbSmoothed;
};

} // namespace tape
