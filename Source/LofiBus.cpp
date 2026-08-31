#include "LofiBus.h"

namespace tape
{

namespace
{
    float bitcrush (float x, float steps) { return std::round (x * steps) / steps; }

    /** Cheap cubic soft clip: linear near zero, flattens to +-2/3 past +-1 --
        keeps runaway delay feedback from blowing up instead of exploding. */
    float softClipCubic (float x)
    {
        x = std::clamp (x, -1.5f, 1.5f);
        if (x <= -1.0f)
            return -2.0f / 3.0f;
        if (x >= 1.0f)
            return 2.0f / 3.0f;
        return x - (x * x * x) / 3.0f;
    }
}

void LofiBus::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    delayCapacity = (int) std::ceil (sampleRate * 2.0); // 2 s ceiling
    delayBuf.setSize (2, delayCapacity, false, true, true);
    delayBuf.clear();
    delayWritePos = 0;

    delayHpL.setCutoff (250.0, sampleRate);
    delayHpR.setCutoff (250.0, sampleRate);

    holdPeriodSamples = juce::jmax (1, (int) std::round (sampleRate / 22050.0));
    holdCounter = 0;
    heldL = heldR = 0.0f;

    delayTimeSmoothed.reset (sampleRate, 0.05);
    delayTimeSmoothed.setCurrentAndTargetValue ((float) (0.5 * (60.0 / 120.0) * sampleRate));
    delayReturnSmoothed.reset (sampleRate, 0.02);

    // Freeverb's classic 44.1kHz tunings, scaled to the running sample rate.
    static const int combTuningL[kNumCombs] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    static const int allpassTuningL[kNumAllpasses] = { 556, 441, 341, 225 };
    constexpr int stereoSpread = 23;

    const double scale = sampleRate / 44100.0;
    for (int i = 0; i < kNumCombs; ++i)
    {
        combsL[(size_t) i].prepare ((int) std::round (combTuningL[i] * scale));
        combsR[(size_t) i].prepare ((int) std::round ((combTuningL[i] + stereoSpread) * scale));
    }
    for (int i = 0; i < kNumAllpasses; ++i)
    {
        allpassesL[(size_t) i].prepare ((int) std::round (allpassTuningL[i] * scale));
        allpassesR[(size_t) i].prepare ((int) std::round ((allpassTuningL[i] + stereoSpread) * scale));
    }

    reverbInputLp.setCutoff (5500.0, sampleRate);
    reverbReturnSmoothed.reset (sampleRate, 0.02);
}

void LofiBus::process (const Params& p, double bpm, const float* sendDelayIn, const float* sendReverbIn,
                        float* outL, float* outR, int numSamples)
{
    const double delaySeconds = p.delayBeats * (60.0 / juce::jmax (1.0, bpm));
    const float delayTargetSamples =
        (float) juce::jlimit (1.0, (double) (delayCapacity - 8), delaySeconds * sampleRate);
    delayTimeSmoothed.setTargetValue (delayTargetSamples);
    delayReturnSmoothed.setTargetValue (juce::Decibels::decibelsToGain (p.delayReturnDb));
    reverbReturnSmoothed.setTargetValue (juce::Decibels::decibelsToGain (p.reverbReturnDb));

    const double toneLp = juce::jmap ((double) p.delayTone, 1800.0, 6000.0);
    delayLpL.setCutoff (toneLp, sampleRate);
    delayLpR.setCutoff (toneLp, sampleRate);

    const float combFeedback = juce::jmap (p.reverbSize, 0.70f, 0.98f);
    const float combDamp = p.reverbDamp;

    for (int i = 0; i < numSamples; ++i)
    {
        // ---- lofi delay: sample-and-hold input, band-limited/crushed/clipped
        // ping-pong feedback ----
        if (--holdCounter <= 0)
        {
            heldL = heldR = sendDelayIn[i];
            holdCounter = holdPeriodSamples;
        }

        auto* dL = delayBuf.getWritePointer (0);
        auto* dR = delayBuf.getWritePointer (1);

        const double delaySamples = (double) delayTimeSmoothed.getNextValue();
        const double readPos = (double) delayWritePos - delaySamples;
        const float rawL = cubicHermiteCircular (dL, delayCapacity, readPos);
        const float rawR = cubicHermiteCircular (dR, delayCapacity, readPos);

        const float procL = softClipCubic (bitcrush (delayLpL.lowpass (delayHpL.highpass (rawL)), 512.0f));
        const float procR = softClipCubic (bitcrush (delayLpR.lowpass (delayHpR.highpass (rawR)), 512.0f));

        const float fb = p.delayFeedback;
        dL[delayWritePos] = heldL + procR * fb; // ping-pong: each repeat crosses channels
        dR[delayWritePos] = heldR + procL * fb;

        delayWritePos = (delayWritePos + 1) % delayCapacity;

        const float delayReturn = delayReturnSmoothed.getNextValue();
        const float dlyOutL = procL * delayReturn;
        const float dlyOutR = procR * delayReturn;

        // ---- lofi reverb: 8 damped combs + 4 allpasses per channel,
        // offset tunings give a mono input a stereo return ----
        const float revIn = bitcrush (reverbInputLp.lowpass (sendReverbIn[i]), 2048.0f); // ~12-bit

        float accL = 0.0f, accR = 0.0f;
        for (auto& c : combsL)
            accL += c.process (revIn, combFeedback, combDamp);
        for (auto& c : combsR)
            accR += c.process (revIn, combFeedback, combDamp);
        for (auto& a : allpassesL)
            accL = a.process (accL);
        for (auto& a : allpassesR)
            accR = a.process (accR);

        const float reverbReturn = reverbReturnSmoothed.getNextValue();
        const float revOutL = accL * (1.0f / (float) kNumCombs) * reverbReturn;
        const float revOutR = accR * (1.0f / (float) kNumCombs) * reverbReturn;

        outL[i] += dlyOutL + revOutL;
        outR[i] += dlyOutR + revOutR;
    }
}

} // namespace tape
