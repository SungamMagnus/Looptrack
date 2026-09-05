#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include "ChannelStrip.h"
#include "InputStage.h"
#include "Limiter.h"
#include "LofiBus.h"
#include "LoopRecorder.h"
#include "Parameters.h"
#include "TapeCharacter.h"
#include "Transport.h"

/** Everything one track's panel strip animates, published from the audio
    thread once per block. Positions are in samples so the UI can scale them
    against whatever loopLengthSamples currently is, which itself moves with
    host tempo. */
struct TrackPanelState
{
    std::atomic<int> state { 0 };             // tape::LoopState
    std::atomic<float> readPos { 0.0f };       // playback head, valid while Playing
    std::atomic<float> writePos { 0.0f };      // record head, valid while Recording
    std::atomic<float> recordedLen { 0.0f };   // samples captured, once a pass has finished
    std::atomic<float> loopLengthSamples { 1.0f }; // current musical loop length at host tempo
    std::atomic<bool> inputStageActive { true };   // false once a loop is playing back
    std::atomic<float> inLevel { 0.0f };           // post-preamp/in-EQ peak, for the input meter
    std::atomic<bool> audible { true };            // false when muted or lost a solo contest
};

struct PanelState
{
    std::array<TrackPanelState, tape::kNumTracks> tracks;
    std::atomic<float> outLevel { 0.0f }; // post-fader master peak, for the meter
    std::atomic<bool> limiting { false }; // true while the output limiter is actively reducing gain
};

class AfritProcessor final : public juce::AudioProcessor
{
public:
    AfritProcessor();
    ~AfritProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Afrit"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; } // delay/reverb tails

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    PanelState panelState;

private:
    struct TrackParamPtrs
    {
        std::atomic<float>* bars = nullptr;
        std::atomic<float>* rec = nullptr;
        std::atomic<float>* clear = nullptr;
        std::atomic<float>* play = nullptr;
        std::atomic<float>* mute = nullptr;
        std::atomic<float>* solo = nullptr;
        std::atomic<float>* inLow = nullptr;
        std::atomic<float>* inHigh = nullptr;
        std::atomic<float>* preamp = nullptr;
        std::atomic<float>* volume = nullptr;
        std::atomic<float>* pan = nullptr;
        std::atomic<float>* eqLow = nullptr;
        std::atomic<float>* eqMid = nullptr;
        std::atomic<float>* eqHigh = nullptr;
        std::atomic<float>* filter = nullptr;
        std::atomic<float>* sendDelay = nullptr;
        std::atomic<float>* sendReverb = nullptr;
    };

    /** One track's whole signal chain, independent of the other three except
        for the varispeed ratio and wow/flutter (one shared "capstan" and
        transport, so they wobble together) and the two FX sends, which land
        on a bus shared by all four. */
    struct TrackEngine
    {
        TrackParamPtrs params;
        juce::RangedAudioParameter* recParameter = nullptr; // to reset the latch after a recording completes on its own

        tape::InputStage inputStage;
        tape::LoopRecorder loop;
        tape::TapeCharacter character;
        tape::ChannelStrip strip;
    };

    std::atomic<float>* outParam = nullptr;
    std::atomic<float>* limiterOnParam = nullptr;
    std::atomic<float>* speedParam = nullptr;
    std::atomic<float>* wowParam = nullptr;
    std::atomic<float>* wowRateParam = nullptr;
    std::atomic<float>* flutterParam = nullptr;
    std::atomic<float>* flutterRateParam = nullptr;
    std::atomic<float>* hissParam = nullptr;
    std::atomic<float>* dlyTimeParam = nullptr;
    std::atomic<float>* dlyFbParam = nullptr;
    std::atomic<float>* dlyToneParam = nullptr;
    std::atomic<float>* dlyRetParam = nullptr;
    std::atomic<float>* revSizeParam = nullptr;
    std::atomic<float>* revDampParam = nullptr;
    std::atomic<float>* revRetParam = nullptr;

    std::array<TrackEngine, tape::kNumTracks> tracks;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> speedSemis;

    tape::Transport transport;
    tape::LofiBus lofiBus;
    tape::Limiter limiter;

    // per-track scratch: a pristine copy of the block's input (every track
    // records from the same input, independently), the working buffer each
    // track's chain processes in place, and that track's own send
    // contributions before mute/solo decide whether they reach the shared bus
    juce::AudioBuffer<float> inputCopy, trackScratch, trackSendTmp;
    juce::AudioBuffer<float> sendDelayAcc, sendReverbAcc;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfritProcessor)
};
