#include "TapeCharacter.h"

namespace tape
{

namespace
{
    constexpr double kWowFreq1 = 0.5, kWowFreq2 = 0.87;
    constexpr double kFlutterFreq1 = 6.3, kFlutterFreq2 = 9.7;
    constexpr float kWowMaxCents = 35.0f;
    constexpr float kFlutterMaxCents = 12.0f;

    float centsToDelaySamples (float cents, double freqHz, double sampleRate)
    {
        const double deltaRatio = std::pow (2.0, (double) cents / 1200.0) - 1.0;
        return (float) (deltaRatio * sampleRate / (2.0 * kPi * freqHz));
    }
}

void TapeCharacter::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    delayCapacity = (int) std::ceil (sampleRate * 0.05); // 50 ms, holds a 20ms +-18ms delay
    delayBuf.setSize (2, delayCapacity, false, true, true);
    delayBuf.clear();
    delayWritePos = 0;
    nominalDelaySamples = sampleRate * 0.020;
    modLimitSamples = sampleRate * 0.018;

    wowPhase1 = wowPhase2 = flutterPhase1 = flutterPhase2 = 0.0;
    driftFilter.setCutoff (0.1, sampleRate);

    wowSmoothed.reset (sampleRate, 0.03);
    wowRateSmoothed.reset (sampleRate, 0.03);
    wowRateSmoothed.setCurrentAndTargetValue (1.0f);
    flutterSmoothed.reset (sampleRate, 0.03);
    flutterRateSmoothed.reset (sampleRate, 0.03);
    flutterRateSmoothed.setCurrentAndTargetValue (1.0f);
    hissSmoothed.reset (sampleRate, 0.03);
    hissGate.reset (sampleRate, 0.2);
    hissGate.setCurrentAndTargetValue (0.0f);

    hissHpL.setCutoff (800.0, sampleRate);
    hissHpR.setCutoff (800.0, sampleRate);
    hissLpL.setCutoff (14000.0, sampleRate);
    hissLpR.setCutoff (14000.0, sampleRate);
}

void TapeCharacter::process (const Params& p, float* l, float* r, int numSamples)
{
    wowSmoothed.setTargetValue (p.wowDepth);
    wowRateSmoothed.setTargetValue (p.wowRate);
    flutterSmoothed.setTargetValue (p.flutterDepth);
    flutterRateSmoothed.setTargetValue (p.flutterRate);
    hissSmoothed.setTargetValue (p.hissAmount);
    hissGate.setTargetValue (p.transportPlaying ? 1.0f : 0.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        const float wowD = wowSmoothed.getNextValue();
        const float wowRate = wowRateSmoothed.getNextValue();
        const float flutterD = flutterSmoothed.getNextValue();
        const float flutterRate = flutterRateSmoothed.getNextValue();
        const float hissAmt = hissSmoothed.getNextValue();
        const float gate = hissGate.getNextValue();

        const double wowFreq1 = kWowFreq1 * wowRate, wowFreq2 = kWowFreq2 * wowRate;
        const double flutterFreq1 = kFlutterFreq1 * flutterRate, flutterFreq2 = kFlutterFreq2 * flutterRate;

        const float ampWow1 = centsToDelaySamples (wowD * kWowMaxCents, wowFreq1, sampleRate);
        const float ampWow2 = centsToDelaySamples (wowD * kWowMaxCents, wowFreq2, sampleRate);
        const float ampFlutter1 = centsToDelaySamples (flutterD * kFlutterMaxCents, flutterFreq1, sampleRate);
        const float ampFlutter2 = centsToDelaySamples (flutterD * kFlutterMaxCents, flutterFreq2, sampleRate);
        const float ampDrift = 0.25f * ampWow1;

        wowPhase1 = std::fmod (wowPhase1 + 2.0 * kPi * wowFreq1 / sampleRate, 2.0 * kPi);
        wowPhase2 = std::fmod (wowPhase2 + 2.0 * kPi * wowFreq2 / sampleRate, 2.0 * kPi);
        flutterPhase1 = std::fmod (flutterPhase1 + 2.0 * kPi * flutterFreq1 / sampleRate, 2.0 * kPi);
        flutterPhase2 = std::fmod (flutterPhase2 + 2.0 * kPi * flutterFreq2 / sampleRate, 2.0 * kPi);

        const float driftState = driftFilter.lowpass (driftRng.nextBipolar());

        const float modSamples = ampWow1 * (float) std::sin (wowPhase1)
                                + ampWow2 * (float) std::sin (wowPhase2)
                                + ampFlutter1 * (float) std::sin (flutterPhase1)
                                + ampFlutter2 * (float) std::sin (flutterPhase2)
                                + ampDrift * driftState;

        const float modLimit = (float) modLimitSamples;
        const float clampedMod = modLimit * std::tanh (modSamples / modLimit);
        const double delaySamples = nominalDelaySamples + (double) clampedMod;

        auto* dL = delayBuf.getWritePointer (0);
        auto* dR = delayBuf.getWritePointer (1);
        dL[delayWritePos] = l[i];
        dR[delayWritePos] = r[i];

        const double readPos = (double) delayWritePos - delaySamples;
        const float wfL = cubicHermiteCircular (dL, delayCapacity, readPos);
        const float wfR = cubicHermiteCircular (dR, delayCapacity, readPos);

        delayWritePos = (delayWritePos + 1) % delayCapacity;

        const float hissGain = juce::Decibels::decibelsToGain (juce::jmap (hissAmt, 0.0f, 1.0f, -90.0f, -48.0f));
        const float hL = hissLpL.lowpass (hissHpL.highpass (hissRngL.nextBipolar())) * hissGain * gate;
        const float hR = hissLpR.lowpass (hissHpR.highpass (hissRngR.nextBipolar())) * hissGain * gate;

        l[i] = wfL + hL;
        r[i] = wfR + hR;
    }
}

} // namespace tape
