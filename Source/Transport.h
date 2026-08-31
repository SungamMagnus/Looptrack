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

    /** Samples from t.ppqPosition to the next loop boundary, for a loop of
        loopQ quarter-notes. Always in (0, loop-length-in-samples]. */
    static double samplesToBoundary (const TransportInfo& t, double loopQ, double sampleRate)
    {
        const double quartersPerSample = t.bpm / (60.0 * sampleRate);
        double phase = std::fmod (t.ppqPosition, loopQ);
        if (phase < 0.0)
            phase += loopQ;
        const double remaining = (loopQ - phase) / quartersPerSample;
        return remaining <= 0.0 ? loopQ / quartersPerSample : remaining;
    }

private:
    double sampleRate = 44100.0;
    double freeRunPpq = 0.0;
};

} // namespace tape
