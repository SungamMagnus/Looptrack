#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Transport.h"

namespace tape
{

enum class LoopState { Idle, Armed, Recording, Playing };

/** One track's tape loop: a record head that always writes at 1x, and a
    playback head that reads at a caller-supplied varispeed ratio, wrapped
    every host-musical loop boundary rather than every N samples -- so a
    tempo change mid-loop truncates or leaves silence exactly the way a real
    tape would, instead of drifting off the beat. Arming before the host
    transport starts rolling doesn't wait for a bar boundary that hasn't
    happened yet -- recording begins the instant playback starts. */
class LoopRecorder
{
public:
    void prepare (double sampleRate, int maxBars = 4, double minSyncBpm = 40.0);
    void reset();

    /** Erases the recorded/loaded content and returns to Idle. Safe to call
        from any state. */
    void clear();

    /** loopBars: 1-4, user-selected. armRequested/clearRequested: the "rec"
        and "clear" switches' raw state (edge-detected internally). playing:
        the track's play switch. varispeedRatio: 2^(semis/12), already
        smoothed by the caller. inL/inR and outL/outR may alias the same
        buffer (in-place effect processing) -- each sample's input is read
        before that sample's output is written, so aliasing is safe. */
    void process (const TransportInfo& transport, int loopBars, bool armRequested, bool clearRequested,
                  bool playing, double varispeedRatio,
                  const float* inL, const float* inR,
                  float* outL, float* outR, int numSamples, double sampleRate);

    LoopState getState() const { return state; }
    int getRecordedLength() const { return recordedLen; }

    /** True when the tape is driving the output, so the live input is muted.
        Any other time the input is passed through and the input stage in
        front of the recorder is doing something audible. */
    bool loopOwnsOutput (bool playEnabled) const
    {
        return state == LoopState::Playing && playEnabled && recordedLen > 0;
    }
    double getReadPosition() const { return readPos; }
    int getWritePosition() const { return writePos; }

private:
    void onBoundary();
    void startWrap();
    void renderRange (const float* inL, const float* inR, float* outL, float* outR,
                       int count, bool playing, double ratio, bool canWrite);
    void readSample (double ratio, float& l, float& r);

    LoopState state = LoopState::Idle;

    juce::AudioBuffer<float> buffer; // 2ch x capacitySamples
    int capacitySamples = 0;

    int writePos = 0;
    int recordedLen = 0;

    double readPos = 0.0;

    bool crossfading = false;
    double oldReadPos = 0.0;
    int crossfadeSamplesLeft = 0;
    int crossfadeLengthSamples = 0;

    bool prevArm = false;
    bool prevClear = false;
    bool prevTransportPlaying = false;
};

} // namespace tape
