#include "InputStage.h"

namespace tape
{

namespace
{
    constexpr double kLowShelfFreq = 150.0, kLowShelfQ = 0.707;
    constexpr double kHighShelfFreq = 6000.0, kHighShelfQ = 0.707;
    constexpr float kMaxDriveDb = 18.0f; // preamp's positive headroom -- drive amount reaches 1.0 here

    /** Blends clean signal into a tanh saturator as `amount` (0-1) rises, so
        the onset is gradual rather than a hard switch into distortion. */
    float driveBlend (float x, float amount)
    {
        if (amount <= 0.0f)
            return x;
        const float driven = std::tanh (x * (1.0f + amount * 3.0f));
        return x * (1.0f - amount) + driven * amount;
    }
}

void InputStage::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    preampSmoothed.reset (sampleRate, 0.02);
}

void InputStage::process (const Params& p, float* l, float* r, int numSamples)
{
    lowShelfL.lowShelf (kLowShelfFreq, kLowShelfQ, p.lowShelfDb, sampleRate);
    lowShelfR.lowShelf (kLowShelfFreq, kLowShelfQ, p.lowShelfDb, sampleRate);
    highShelfL.highShelf (kHighShelfFreq, kHighShelfQ, p.highShelfDb, sampleRate);
    highShelfR.highShelf (kHighShelfFreq, kHighShelfQ, p.highShelfDb, sampleRate);

    preampSmoothed.setTargetValue (p.preampDb);

    for (int i = 0; i < numSamples; ++i)
    {
        float xl = highShelfL.process (lowShelfL.process (l[i]));
        float xr = highShelfR.process (lowShelfR.process (r[i]));

        const float preampDb = preampSmoothed.getNextValue();
        const float gain = juce::Decibels::decibelsToGain (preampDb);
        const float driveAmount = juce::jlimit (0.0f, 1.0f, preampDb / kMaxDriveDb);

        xl = driveBlend (xl * gain, driveAmount);
        xr = driveBlend (xr * gain, driveAmount);

        l[i] = xl;
        r[i] = xr;
    }
}

} // namespace tape
