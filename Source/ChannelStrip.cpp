#include "ChannelStrip.h"

namespace tape
{

namespace
{
    constexpr double kLowShelfFreq = 120.0, kLowShelfQ = 0.707;
    constexpr double kMidFreq = 900.0, kMidQ = 0.9;
    constexpr double kHighShelfFreq = 3500.0, kHighShelfQ = 0.707;

    constexpr float kFilterDeadZone = 0.02f;
    constexpr float kFilterMinQ = 0.707f, kFilterMaxQ = 2.0f;
}

void ChannelStrip::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    volumeSmoothed.reset (sampleRate, 0.02);
    panSmoothed.reset (sampleRate, 0.02);
    filterSmoothed.reset (sampleRate, 0.02);
    sendDelaySmoothed.reset (sampleRate, 0.02);
    sendReverbSmoothed.reset (sampleRate, 0.02);
}

void ChannelStrip::process (const Params& p, float* l, float* r, int numSamples,
                             float* sendDelayAcc, float* sendReverbAcc)
{
    // Coefficients are cheap enough to redesign every block; a knob rarely
    // moves at audio rate, so per-block is plenty and keeps this simple.
    eqLowL.lowShelf (kLowShelfFreq, kLowShelfQ, p.eqLowDb, sampleRate);
    eqLowR.lowShelf (kLowShelfFreq, kLowShelfQ, p.eqLowDb, sampleRate);
    eqMidL.peak (kMidFreq, kMidQ, p.eqMidDb, sampleRate);
    eqMidR.peak (kMidFreq, kMidQ, p.eqMidDb, sampleRate);
    eqHighL.highShelf (kHighShelfFreq, kHighShelfQ, p.eqHighDb, sampleRate);
    eqHighR.highShelf (kHighShelfFreq, kHighShelfQ, p.eqHighDb, sampleRate);

    filterSmoothed.setTargetValue (p.filterKnob);
    volumeSmoothed.setTargetValue (juce::Decibels::decibelsToGain (p.volumeDb));
    panSmoothed.setTargetValue (p.pan);
    sendDelaySmoothed.setTargetValue (juce::Decibels::decibelsToGain (p.sendDelayDb));
    sendReverbSmoothed.setTargetValue (juce::Decibels::decibelsToGain (p.sendReverbDb));

    for (int i = 0; i < numSamples; ++i)
    {
        float xl = l[i];
        float xr = r[i];

        xl = eqHighL.process (eqMidL.process (eqLowL.process (xl)));
        xr = eqHighR.process (eqMidR.process (eqLowR.process (xr)));

        const float k = filterSmoothed.getNextValue();
        const float absK = std::abs (k);
        if (absK >= kFilterDeadZone)
        {
            const float q = juce::jmap (absK, kFilterDeadZone, 1.0f, kFilterMinQ, kFilterMaxQ);
            const float shaped = (absK - kFilterDeadZone) / (1.0f - kFilterDeadZone);
            const float fc = k < 0.0f
                ? 18000.0f * std::pow (200.0f / 18000.0f, shaped)
                : 20.0f * std::pow (6000.0f / 20.0f, shaped);

            filterL.setCutoff (fc, q, sampleRate);
            filterR.setCutoff (fc, q, sampleRate);

            float lpL, hpL, lpR, hpR;
            filterL.process (xl, lpL, hpL);
            filterR.process (xr, lpR, hpR);
            xl = k < 0.0f ? lpL : hpL;
            xr = k < 0.0f ? lpR : hpR;
        }

        const float vol = volumeSmoothed.getNextValue();
        xl *= vol;
        xr *= vol;

        const float sendMono = 0.5f * (xl + xr);
        sendDelayAcc[i] += sendMono * sendDelaySmoothed.getNextValue();
        sendReverbAcc[i] += sendMono * sendReverbSmoothed.getNextValue();

        const float pan = panSmoothed.getNextValue();
        const float panAngle = (pan * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;
        l[i] = xl * std::cos (panAngle);
        r[i] = xr * std::sin (panAngle);
    }
}

} // namespace tape
