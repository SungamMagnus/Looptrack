#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ChannelStrip.h"
#include "InputStage.h"
#include "LofiBus.h"
#include "LoopRecorder.h"
#include "Parameters.h"
#include "TapeCharacter.h"
#include "Transport.h"

/** Everything the panel animates, published from the audio thread once per
    block. Positions are in samples so the UI can scale them against whatever
    loopLengthSamples currently is, which itself moves with host tempo. */
struct PanelState
{
    std::atomic<int> state { 0 };             // tape::LoopState
    std::atomic<float> readPos { 0.0f };       // playback head, valid while Playing
    std::atomic<float> writePos { 0.0f };      // record head, valid while Recording
    std::atomic<float> recordedLen { 0.0f };   // samples captured, once a pass has finished
    std::atomic<float> loopLengthSamples { 1.0f }; // current musical loop length at host tempo
    std::atomic<bool> inputStageActive { true };   // false once a loop is playing back
    std::atomic<float> outLevel { 0.0f };          // post-fader peak, for the meter
};

class LooptrackProcessor final : public juce::AudioProcessor
{
public:
    LooptrackProcessor();
    ~LooptrackProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Looptrack"; }
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
        std::atomic<float>* source = nullptr;
        std::atomic<float>* rec = nullptr;
        std::atomic<float>* clear = nullptr;
        std::atomic<float>* play = nullptr;
        std::atomic<float>* inLow = nullptr;
        std::atomic<float>* inHigh = nullptr;
        std::atomic<float>* preamp = nullptr;
        std::atomic<float>* volume = nullptr;
        std::atomic<float>* pan = nullptr;
        std::atomic<float>* wow = nullptr;
        std::atomic<float>* wowRate = nullptr;
        std::atomic<float>* flutter = nullptr;
        std::atomic<float>* flutterRate = nullptr;
        std::atomic<float>* hiss = nullptr;
        std::atomic<float>* eqLow = nullptr;
        std::atomic<float>* eqMid = nullptr;
        std::atomic<float>* eqHigh = nullptr;
        std::atomic<float>* filter = nullptr;
        std::atomic<float>* sendDelay = nullptr;
        std::atomic<float>* sendReverb = nullptr;
    };

    std::atomic<float>* outParam = nullptr;
    std::atomic<float>* speedParam = nullptr;
    std::atomic<float>* dlyTimeParam = nullptr;
    std::atomic<float>* dlyFbParam = nullptr;
    std::atomic<float>* dlyToneParam = nullptr;
    std::atomic<float>* dlyRetParam = nullptr;
    std::atomic<float>* revSizeParam = nullptr;
    std::atomic<float>* revDampParam = nullptr;
    std::atomic<float>* revRetParam = nullptr;

    TrackParamPtrs t0;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> speedSemis;

    tape::Transport transport;
    tape::InputStage inputStage;
    tape::LoopRecorder loop;
    tape::TapeCharacter character;
    tape::ChannelStrip strip;
    tape::LofiBus lofiBus;

    juce::AudioBuffer<float> sendDelayAcc, sendReverbAcc;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooptrackProcessor)
};
