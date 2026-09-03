#pragma once

#include <cmath>

#include <juce_audio_processors/juce_audio_processors.h>

namespace tape
{

/** Host tempo/position for one block, or a free-running 120 BPM / 4-4 clock
    when there is no host to ask (Standalone with nothing loaded, dsp_check).
    The loop is anchored to this in musical time (PPQ), not a sample counter,
    so a tempo ramp, a locate, or a host stop/start all re-align the tape with
    no extra bookkeeping. */
struct TransportInfo
{
    double bpm = 120.0;
    double ppqPosition = 0.0;
    bool isPlaying = true;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;

    double loopQuarters (int bars) const
    {
        return (double) bars * timeSigNumerator * 4.0 / timeSigDenominator;
    }
};

class Transport
{
public:
    void prepare (double newSampleRate) { sampleRate = newSampleRate; }

    /** Call once per block, before rendering. */
    TransportInfo read (juce::AudioPlayHead* playHead, int numSamples)
    {
        TransportInfo t;
        bool havePosition = false;

        if (playHead != nullptr)
        {
            if (auto pos = playHead->getPosition())
            {
                if (auto bpm = pos->getBpm())
                    t.bpm = *bpm;
                if (auto ppq = pos->getPpqPosition())
                {
                    t.ppqPosition = *ppq;
                    havePosition = true;
                }
                if (auto ts = pos->getTimeSignature())
                {
                    t.timeSigNumerator = ts->numerator;
                    t.timeSigDenominator = ts->denominator;
                }
                t.isPlaying = pos->getIsPlaying();
            }
        }

        if (! havePosition)
        {
            t.bpm = 120.0;
            t.isPlaying = true;
            t.timeSigNumerator = 4;
            t.timeSigDenominator = 4;
            t.ppqPosition = freeRunPpq;
        }

        const double quartersPerSample = t.bpm / (60.0 * sampleRate);
        freeRunPpq += quartersPerSample * numSamples;

        return t;
    }

    /** Samples from t.ppqPosition to a specific target position in musical
        time. Used with a target anchored to when recording started (not an
        absolute bar-1 grid), so the loop always measures exactly one
        loopQ-length pass from record-start regardless of what beat the
        transport happened to be on when the user hit play -- a tempo change
        mid-pass still truncates/leaves-silence like a real tape, since the
        target stays fixed in musical time and only the sample-distance to
        it moves. Clamped to 0 rather than negative -- floating-point noise
        right at the target should read as "now", not "a whole pass away". */
    static double samplesToTarget (const TransportInfo& t, double targetPpq, double sampleRate)
    {
        const double quartersPerSample = t.bpm / (60.0 * sampleRate);
        const double remaining = (targetPpq - t.ppqPosition) / quartersPerSample;
        return remaining <= 0.0 ? 0.0 : remaining;
    }

private:
    double sampleRate = 44100.0;
    double freeRunPpq = 0.0;
};

} // namespace tape
