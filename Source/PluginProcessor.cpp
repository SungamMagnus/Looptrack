#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

LooptrackProcessor::LooptrackProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", tape::createLayout())
{
    outParam = apvts.getRawParameterValue (tape::pid::out);
    speedParam = apvts.getRawParameterValue (tape::global::speed);
    dlyTimeParam = apvts.getRawParameterValue (tape::global::dlyTime);
    dlyFbParam = apvts.getRawParameterValue (tape::global::dlyFb);
    dlyToneParam = apvts.getRawParameterValue (tape::global::dlyTone);
    dlyRetParam = apvts.getRawParameterValue (tape::global::dlyRet);
    revSizeParam = apvts.getRawParameterValue (tape::global::revSize);
    revDampParam = apvts.getRawParameterValue (tape::global::revDamp);
    revRetParam = apvts.getRawParameterValue (tape::global::revRet);

    t0.bars = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::bars));
    t0.source = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::source));
    t0.rec = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::rec));
    t0.clear = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::clear));
    t0.play = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::play));
    t0.inLow = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::inLow));
    t0.inHigh = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::inHigh));
    t0.preamp = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::preamp));
    t0.volume = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::volume));
    t0.pan = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::pan));
    t0.wow = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::wow));
    t0.wowRate = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::wowRate));
    t0.flutter = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::flutter));
    t0.flutterRate = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::flutterRate));
    t0.hiss = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::hiss));
    t0.eqLow = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::eqLow));
    t0.eqMid = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::eqMid));
    t0.eqHigh = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::eqHigh));
    t0.filter = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::filter));
    t0.sendDelay = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::sendDelay));
    t0.sendReverb = apvts.getRawParameterValue (tape::trackParamId (0, tape::track::sendReverb));
}

void LooptrackProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    outGain.reset (sampleRate, 0.02);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outParam->load()));

    speedSemis.reset (sampleRate, 0.05);
    speedSemis.setCurrentAndTargetValue (speedParam->load());

    transport.prepare (sampleRate);
    inputStage.prepare (sampleRate);
    loop.prepare (sampleRate);
    character.prepare (sampleRate);
    strip.prepare (sampleRate);
    lofiBus.prepare (sampleRate);

    sendDelayAcc.setSize (1, samplesPerBlock, false, true, true);
    sendReverbAcc.setSize (1, samplesPerBlock, false, true, true);
}

bool LooptrackProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto in = layouts.getMainInputChannelSet();
    auto out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void LooptrackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const double sampleRate = getSampleRate();
    const auto transportInfo = transport.read (getPlayHead(), numSamples);

    speedSemis.setTargetValue (speedParam->load());
    const float semis = speedSemis.skip (numSamples);
    const double varispeedRatio = std::exp2 ((double) semis / 12.0);

    const int loopBars = 1 + (int) std::lround (t0.bars->load());
    const bool armRequested = t0.rec->load() > 0.5f;
    const bool clearRequested = t0.clear->load() > 0.5f;
    const bool playing = t0.play->load() > 0.5f;

    auto* chL = buffer.getWritePointer (0);
    auto* chR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : chL;

    // Input stage: 2-band shelf EQ then preamp/drive. It is a record-chain
    // stage -- it shapes what goes onto the tape and what you monitor while
    // getting a level, and goes inert once a loop is playing back, because by
    // then the sound is already committed to tape and an input trim has
    // nothing left to act on.
    const bool inputStageActive = loop.getState() != tape::LoopState::Playing;
    if (inputStageActive)
    {
        tape::InputStage::Params inputParams;
        inputParams.lowShelfDb = t0.inLow->load();
        inputParams.highShelfDb = t0.inHigh->load();
        inputParams.preampDb = t0.preamp->load();
        inputStage.process (inputParams, chL, chR, numSamples);
    }

    loop.process (transportInfo, loopBars, armRequested, clearRequested, playing, varispeedRatio,
                  chL, chR, chL, chR, numSamples, sampleRate);

    tape::TapeCharacter::Params characterParams;
    characterParams.wowDepth = t0.wow->load();
    characterParams.wowRate = t0.wowRate->load();
    characterParams.flutterDepth = t0.flutter->load();
    characterParams.flutterRate = t0.flutterRate->load();
    characterParams.hissAmount = t0.hiss->load();
    characterParams.transportPlaying = transportInfo.isPlaying;
    character.process (characterParams, chL, chR, numSamples);

    sendDelayAcc.clear();
    sendReverbAcc.clear();
    auto* sendDelayPtr = sendDelayAcc.getWritePointer (0);
    auto* sendReverbPtr = sendReverbAcc.getWritePointer (0);

    tape::ChannelStrip::Params stripParams;
    stripParams.eqLowDb = t0.eqLow->load();
    stripParams.eqMidDb = t0.eqMid->load();
    stripParams.eqHighDb = t0.eqHigh->load();
    stripParams.filterKnob = t0.filter->load();
    stripParams.volumeDb = t0.volume->load();
    stripParams.pan = t0.pan->load();
    stripParams.sendDelayDb = t0.sendDelay->load();
    stripParams.sendReverbDb = t0.sendReverb->load();

    strip.process (stripParams, chL, chR, numSamples, sendDelayPtr, sendReverbPtr);

    tape::LofiBus::Params busParams;
    busParams.delayMs = dlyTimeParam->load();
    busParams.delayFeedback = dlyFbParam->load();
    busParams.delayTone = dlyToneParam->load();
    busParams.delayReturnDb = dlyRetParam->load();
    busParams.reverbSize = revSizeParam->load();
    busParams.reverbDamp = revDampParam->load();
    busParams.reverbReturnDb = revRetParam->load();

    lofiBus.process (busParams, sendDelayPtr, sendReverbPtr, chL, chR, numSamples);

    outGain.setTargetValue (juce::Decibels::decibelsToGain (outParam->load()));
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outGain.getNextValue();
        chL[i] *= g;
        if (chR != chL)
            chR[i] *= g;
    }

    const double loopQ = transportInfo.loopQuarters (loopBars);
    const double loopLenSamples = loopQ * (60.0 / juce::jmax (1.0, transportInfo.bpm)) * sampleRate;
    panelState.state.store ((int) loop.getState());
    panelState.readPos.store ((float) loop.getReadPosition());
    panelState.writePos.store ((float) loop.getWritePosition());
    panelState.recordedLen.store ((float) loop.getRecordedLength());
    panelState.loopLengthSamples.store ((float) juce::jmax (1.0, loopLenSamples));
    panelState.inputStageActive.store (inputStageActive);

    juce::ignoreUnused (t0.source);
}

juce::AudioProcessorEditor* LooptrackProcessor::createEditor()
{
    return new LooptrackEditor (*this);
}

void LooptrackProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
}

void LooptrackProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LooptrackProcessor();
}
