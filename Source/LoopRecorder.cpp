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
}

void LoopRecorder::clear()
{
    state = LoopState::Idle;
    writePos = 0;
    recordedLen = 0;
    readPos = 0.0;
    crossfading = false;
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

    if (armRequested && ! prevArm)
    {
        if (state == LoopState::Idle || state == LoopState::Playing)
            state = LoopState::Armed;
        else if (state == LoopState::Armed)
            state = LoopState::Idle; // cancel before the boundary lands
        else if (state == LoopState::Recording)
        {
            recordedLen = writePos;
            state = LoopState::Playing; // early punch-out, keep what's written
            startWrap();
        }
    }
    prevArm = armRequested;

    // Arming while the host transport is stopped shouldn't wait for a bar
    // boundary that can't happen until it starts -- begin recording the
    // instant playback does.
    const bool playStartEdge = transport.isPlaying && ! prevTransportPlaying;
    prevTransportPlaying = transport.isPlaying;
    if (state == LoopState::Armed && playStartEdge)
    {
        state = LoopState::Recording;
        writePos = 0;
    }

    const double loopQ = transport.loopQuarters (loopBars);

    int splitAt = numSamples;
    bool crossesBoundary = false;

    if (transport.isPlaying)
    {
        const double boundarySamples = Transport::samplesToBoundary (transport, loopQ, sampleRate);
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
        onBoundary();
        const int remaining = numSamples - splitAt;
        if (remaining > 0)
            renderRange (inL + splitAt, inR + splitAt, outL + splitAt, outR + splitAt,
                         remaining, playing, varispeedRatio, canWrite);
    }
}

void LoopRecorder::onBoundary()
{
    switch (state)
    {
        case LoopState::Armed:
            state = LoopState::Recording;
            writePos = 0;
            break;
        case LoopState::Recording:
            recordedLen = writePos;
            state = LoopState::Playing;
            startWrap();
            break;
        case LoopState::Playing:
            startWrap();
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

        if (state == LoopState::Recording || state == LoopState::Armed)
        {
            // monitor the live input while arming/recording, so you can hear
            // yourself against the other tracks while punching in
            ol = wl;
            orr = wr;
        }
        else if (state == LoopState::Playing && playing && recordedLen > 0
                 && readPos < (double) recordedLen)
        {
            readSample (ratio, ol, orr);
        }
        // else: silence -- either nothing recorded yet, or the read head ran
        // past the end of the material because tempo slowed down; it waits
        // here for the next boundary rather than wrapping early

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
