#include "LoopRecorder.h"

#include <cmath>

#include "Dsp.h"

namespace tape
{

void LoopRecorder::prepare (double sampleRate, int maxBars, double minSyncBpm)
{
    capacitySamples = (int) std::ceil (sampleRate * (60.0 / minSyncBpm) * (double) maxBars * 4.0);
    buffer.setSize (2, capacitySamples, false, true, true);
    buffer.clear();
    crossfadeLengthSamples = juce::jmax (1, (int) std::round (sampleRate * 0.005)); // 5 ms
    reset();
}

void LoopRecorder::reset()
{
    state = LoopState::Idle;
    writePos = 0;
    recordedLen = 0;
    readPos = 0.0;
    crossfading = false;
    prevArm = false;
    prevClear = false;
    prevTransportPlaying = false;
    nextBoundaryPpq = 0.0;
}

void LoopRecorder::clear()
{
    state = LoopState::Idle;
    writePos = 0;
    recordedLen = 0;
    readPos = 0.0;
    crossfading = false;
    nextBoundaryPpq = 0.0;
    buffer.clear();
}

void LoopRecorder::process (const TransportInfo& transport, int loopBars, bool armRequested, bool clearRequested,
                             bool playing, double varispeedRatio,
                             const float* inL, const float* inR,
                             float* outL, float* outR, int numSamples, double sampleRate)
{
    if (clearRequested && ! prevClear)
        clear();
    prevClear = clearRequested;

    const double loopQ = transport.loopQuarters (loopBars);

    // REC is a plain on/off latch, not a momentary pulse -- one click turns
    // it on, the next turns it off. So "start" has to react to the rising
    // edge and "stop early" to the falling edge; reacting to only one (as
    // this used to) means the other click does nothing.
    if (armRequested && ! prevArm && (state == LoopState::Idle || state == LoopState::Playing))
    {
        if (transport.isPlaying)
        {
            // the transport is already rolling -- start capturing right
            // now instead of waiting for the next bar line, and measure
            // this pass's one loopQ from this instant
            state = LoopState::Recording;
            writePos = 0;
            nextBoundaryPpq = transport.ppqPosition + loopQ;
        }
        else
        {
            state = LoopState::Armed; // wait for playback to start
        }
    }
    else if (! armRequested && prevArm)
    {
        if (state == LoopState::Armed)
            state = LoopState::Idle; // cancel before playback starts
        else if (state == LoopState::Recording)
        {
            recordedLen = writePos;
            state = LoopState::Playing; // early punch-out, keep what's written
            startWrap();
            // nextBoundaryPpq is left as-is: the pass just cut short still
            // wraps at the bar boundary it was already counting down to,
            // so playback keeps filling the full bar count with silence
            // after the short material, instead of shrinking the loop to
            // match what got captured.
        }
    }
    prevArm = armRequested;

    // Arming while the transport is stopped shouldn't wait for a bar
    // boundary that can't happen until it starts -- begin recording the
    // instant playback does, measuring this pass's one loopQ from that
    // moment. Also keeps the loop grid itself alive across the whole time
    // the transport rolls (regardless of state), so a later "arm while
    // already playing" always has a fresh nextBoundaryPpq relative to
    // *this* play-start to base its own immediate start on.
    const bool playStartEdge = transport.isPlaying && ! prevTransportPlaying;
    prevTransportPlaying = transport.isPlaying;
    if (playStartEdge)
    {
        nextBoundaryPpq = transport.ppqPosition + loopQ;
        if (state == LoopState::Armed)
        {
            state = LoopState::Recording;
            writePos = 0;
        }
    }

    int splitAt = numSamples;
    bool crossesBoundary = false;

    if (transport.isPlaying && state != LoopState::Idle)
    {
        const double boundarySamples = Transport::samplesToTarget (transport, nextBoundaryPpq, sampleRate);
        if (boundarySamples < (double) numSamples)
        {
            splitAt = juce::jlimit (0, numSamples, (int) std::llround (boundarySamples));
            crossesBoundary = true;
        }
    }

    // While the host isn't rolling, freeze recording rather than capturing
    // against a clock that isn't advancing. Playback is unaffected.
    const bool canWrite = transport.isPlaying;

    renderRange (inL, inR, outL, outR, splitAt, playing, varispeedRatio, canWrite);

    if (crossesBoundary)
    {
        onBoundary (loopQ);
        const int remaining = numSamples - splitAt;
        if (remaining > 0)
            renderRange (inL + splitAt, inR + splitAt, outL + splitAt, outR + splitAt,
                         remaining, playing, varispeedRatio, canWrite);
    }
}

void LoopRecorder::onBoundary (double loopQ)
{
    switch (state)
    {
        case LoopState::Armed:
            state = LoopState::Recording;
            writePos = 0;
            nextBoundaryPpq += loopQ;
            break;
        case LoopState::Recording:
            recordedLen = writePos;
            state = LoopState::Playing;
            startWrap();
            nextBoundaryPpq += loopQ;
            break;
        case LoopState::Playing:
            startWrap();
            nextBoundaryPpq += loopQ;
            break;
        case LoopState::Idle:
            break;
    }
}

void LoopRecorder::startWrap()
{
    oldReadPos = readPos;
    crossfading = true;
    crossfadeSamplesLeft = crossfadeLengthSamples;
    readPos = 0.0;
}

void LoopRecorder::renderRange (const float* inL, const float* inR, float* outL, float* outR,
                                 int count, bool playing, double ratio, bool canWrite)
{
    for (int i = 0; i < count; ++i)
    {
        const float wl = inL[i];
        const float wr = inR[i];

        if (state == LoopState::Recording && canWrite && writePos < capacitySamples)
        {
            buffer.setSample (0, writePos, wl);
            buffer.setSample (1, writePos, wr);
            ++writePos;
        }

        float ol = 0.0f, orr = 0.0f;

        // The tape owns the output only while it is actually playing material
        // back. Any other time -- idle, armed, recording, or with PLAY off --
        // the input is passed straight through, so what you are about to
        // record is always audible.
        const bool loopHasTheOutput = state == LoopState::Playing && playing && recordedLen > 0;

        if (! loopHasTheOutput)
        {
            ol = wl;
            orr = wr;
        }
        else if (readPos < (double) recordedLen)
        {
            readSample (ratio, ol, orr);
        }
        // else: silence -- the read head ran past the end of the material
        // because tempo slowed down, so the tape has run out and waits here
        // for the next boundary rather than wrapping early

        outL[i] = ol;
        outR[i] = orr;
    }
}

void LoopRecorder::readSample (double ratio, float& l, float& r)
{
    const float* L = buffer.getReadPointer (0);
    const float* R = buffer.getReadPointer (1);

    const float newL = cubicHermite (L, recordedLen, readPos);
    const float newR = cubicHermite (R, recordedLen, readPos);

    if (crossfading)
    {
        const float oldL = cubicHermite (L, recordedLen, oldReadPos);
        const float oldR = cubicHermite (R, recordedLen, oldReadPos);
        const float t = 1.0f - (float) crossfadeSamplesLeft / (float) crossfadeLengthSamples;
        l = oldL * (1.0f - t) + newL * t;
        r = oldR * (1.0f - t) + newR * t;

        oldReadPos += ratio;
        if (--crossfadeSamplesLeft <= 0)
            crossfading = false;
    }
    else
    {
        l = newL;
        r = newR;
    }

    readPos += ratio;
}

} // namespace tape
