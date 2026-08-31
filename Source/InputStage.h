#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Dsp.h"

namespace tape
{

/** What hits the tape: a 2-band shelf EQ, then a preamp. Below 0dB the
    preamp is clean gain (or attenuation); driven above 0dB it blends in a
    tanh waveshaper, so pushing it hot adds tape-style warmth rather than
    just getting louder. Sits before the loop recorder, so both what gets
    written to the loop and what you hear while monitoring/recording carry
    this colour -- the same as setting input trim on a real 4-track before
    you hit record. */
class InputStage
{
public:
    void prepare (double sampleRate);

    struct Params
    {
        float lowShelfDb = 0.0f;
        float highShelfDb = 0.0f;
        float preampDb = 0.0f;
    };

    void process (const Params& p, float* l, float* r, int numSamples);

private:
    double sampleRate = 48000.0;

    Biquad lowShelfL, lowShelfR, highShelfL, highShelfR;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> preampSmoothed;
};

} // namespace tape
