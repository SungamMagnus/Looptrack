#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

AfritProcessor::AfritProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", tape::createLayout())
{
    outParam = apvts.getRawParameterValue (tape::pid::out);
    limiterOnParam = apvts.getRawParameterValue (tape::pid::limiterOn);
    speedParam = apvts.getRawParameterValue (tape::global::speed);
    wowParam = apvts.getRawParameterValue (tape::global::wow);
    wowRateParam = apvts.getRawParameterValue (tape::global::wowRate);
    flutterParam = apvts.getRawParameterValue (tape::global::flutter);
    flutterRateParam = apvts.getRawParameterValue (tape::global::flutterRate);
    hissParam = apvts.getRawParameterValue (tape::global::hiss);
    dlyTimeParam = apvts.getRawParameterValue (tape::global::dlyTime);
    dlyFbParam = apvts.getRawParameterValue (tape::global::dlyFb);
    dlyToneParam = apvts.getRawParameterValue (tape::global::dlyTone);
    dlyRetParam = apvts.getRawParameterValue (tape::global::dlyRet);
    revSizeParam = apvts.getRawParameterValue (tape::global::revSize);
    revDampParam = apvts.getRawParameterValue (tape::global::revDamp);
    revRetParam = apvts.getRawParameterValue (tape::global::revRet);

    for (int t = 0; t < tape::kNumTracks; ++t)
    {
        auto& p = tracks[(size_t) t].params;
        p.bars = apvts.getRawParameterValue (tape::trackParamId (t, tape::track::bars));
        p.rec = apvts.getRawParameterValue (tape::trackParamId (t, tape::track::rec));
        tracks[(size_t) t].recParameter = apvts.getParameter (tape::trackParamId (t, tape::track::rec));
        p.clear = apvts.getRawParameterValue (tape::trackParamId (t, tape::track::clear));
        p.play = apvts.getRawParameterValue (tape::trackParamId (t, tape::track::play));
        p.mute = apvts.getRawParameterValue (tape::trackParamId (t, tape::track::mute));
        p.solo = apvts.getRawParameterValue (tape::trackParamId (t, tape::track::solo));
        p.inLow = apvts.getRawParameterValue (tape::trackParamId (t, tape::track::inLow));
        p.inHigh = apvts.getRawParameterValue (tape::trackParamId (t, tape::track::inHigh));
        p.preamp = apvts.getRawParameterValue (tape::trackParamId (t, tape::track::preamp));
        p.volume = apvts.getRawParameterValue (tape::trackParamId (t, tape::track::volume));
        p.pan = apvts.getRawParameterValue (tape::trackParamId (t, tape::track::pan));
        p.eqLow = apvts.getRawParameterValue (tape::trackParamId (t, tape::track::eqLow));
        p.eqMid = apvts.getRawParameterValue (tape::trackParamId (t, tape::track::eqMid));
        p.eqHigh = apvts.getRawParameterValue (tape::trackParamId (t, tape::track::eqHigh));
        p.filter = apvts.getRawParameterValue (tape::trackParamId (t, tape::track::filter));
        p.sendDelay = apvts.getRawParameterValue (tape::trackParamId (t, tape::track::sendDelay));
        p.sendReverb = apvts.getRawParameterValue (tape::trackParamId (t, tape::track::sendReverb));
    }
}

void AfritProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    outGain.reset (sampleRate, 0.02);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outParam->load()));

    speedSemis.reset (sampleRate, 0.05);
    speedSemis.setCurrentAndTargetValue (speedParam->load());

    transport.prepare (sampleRate);
    lofiBus.prepare (sampleRate);
    limiter.prepare (sampleRate);

    for (auto& t : tracks)
    {
        t.inputStage.prepare (sampleRate);
        t.loop.prepare (sampleRate);
        t.character.prepare (sampleRate);
        t.strip.prepare (sampleRate);
    }

    inputCopy.setSize (2, samplesPerBlock, false, true, true);
    trackScratch.setSize (2, samplesPerBlock, false, true, true);
    trackSendTmp.setSize (2, samplesPerBlock, false, true, true); // ch0 = delay, ch1 = reverb
    sendDelayAcc.setSize (1, samplesPerBlock, false, true, true);
    sendReverbAcc.setSize (1, samplesPerBlock, false, true, true);
}

bool AfritProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto in = layouts.getMainInputChannelSet();
    auto out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void AfritProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const double sampleRate = getSampleRate();
    const auto transportInfo = transport.read (getPlayHead(), numSamples);

    speedSemis.setTargetValue (speedParam->load());
    const float semis = speedSemis.skip (numSamples);
    const double varispeedRatio = std::exp2 ((double) semis / 12.0);

    // one capstan drives all four tracks, so every track's mix contribution
    // is decided the same way: audible unless muted, or unless something
    // else is soloed and this track isn't it
    bool anySolo = false;
    for (auto& t : tracks)
        anySolo = anySolo || (t.params.solo->load() > 0.5f);

    inputCopy.setSize (2, numSamples, false, false, true);
    inputCopy.copyFrom (0, 0, buffer, 0, 0, numSamples);
    inputCopy.copyFrom (1, 0, buffer, buffer.getNumChannels() > 1 ? 1 : 0, 0, numSamples);

    buffer.clear();
    sendDelayAcc.setSize (1, numSamples, false, false, true);
    sendReverbAcc.setSize (1, numSamples, false, false, true);
    sendDelayAcc.clear();
    sendReverbAcc.clear();

    auto* masterL = buffer.getWritePointer (0);
    auto* masterR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : masterL;
    auto* sendDelayPtr = sendDelayAcc.getWritePointer (0);
    auto* sendReverbPtr = sendReverbAcc.getWritePointer (0);

    for (int ti = 0; ti < tape::kNumTracks; ++ti)
    {
        auto& track = tracks[(size_t) ti];
        auto& p = track.params;

        trackScratch.setSize (2, numSamples, false, false, true);
        trackScratch.copyFrom (0, 0, inputCopy, 0, 0, numSamples);
        trackScratch.copyFrom (1, 0, inputCopy, 1, 0, numSamples);
        auto* chL = trackScratch.getWritePointer (0);
        auto* chR = trackScratch.getWritePointer (1);

        const int loopBars = 1 + (int) std::lround (p.bars->load());
        const bool armRequested = p.rec->load() > 0.5f;
        const bool clearRequested = p.clear->load() > 0.5f;
        const bool playing = p.play->load() > 0.5f;
        const bool muted = p.mute->load() > 0.5f;
        const bool solo = p.solo->load() > 0.5f;
        const bool audible = ! muted && (! anySolo || solo);

        // Input stage: 2-band shelf EQ then preamp/drive. It is a
        // record-chain stage -- it shapes what goes onto the tape and what
        // you hear while getting a level. It only has something to act on
        // while the input is actually reaching the output, which is any
        // time this track's tape is not driving it.
        const bool inputStageActive = ! track.loop.loopOwnsOutput (playing);
        float inPeak = 0.0f;
        if (inputStageActive)
        {
            tape::InputStage::Params inputParams;
            inputParams.lowShelfDb = p.inLow->load();
            inputParams.highShelfDb = p.inHigh->load();
            inputParams.preampDb = p.preamp->load();
            track.inputStage.process (inputParams, chL, chR, numSamples);

            // metered here: post-preamp and post-in-EQ, so the VU shows what
            // is actually hitting the tape rather than what arrived at the
            // plug-in
            for (int i = 0; i < numSamples; ++i)
                inPeak = juce::jmax (inPeak, std::abs (chL[i]), std::abs (chR[i]));
        }

        auto& tps = panelState.tracks[(size_t) ti];
        const float previousIn = tps.inLevel.load();
        tps.inLevel.store (juce::jmax (inPeak, previousIn * 0.85f));

        track.loop.process (transportInfo, loopBars, armRequested, clearRequested, playing, varispeedRatio,
                             chL, chR, chL, chR, numSamples, sampleRate);

        // REC is a plain latch: if a recording runs to the bar boundary on
        // its own (rather than being punched out by the user turning REC
        // back off), the button would otherwise stay lit -- and the next
        // click would then read as "turn off" instead of "start", since the
        // parameter never actually went back to 0. Reset it once the loop
        // has moved on.
        if (armRequested)
        {
            const auto state = track.loop.getState();
            if (state != tape::LoopState::Recording && state != tape::LoopState::Armed
                && track.recParameter != nullptr)
                track.recParameter->setValueNotifyingHost (0.0f);
        }

        tape::TapeCharacter::Params characterParams;
        characterParams.wowDepth = wowParam->load();
        characterParams.wowRate = wowRateParam->load();
        characterParams.flutterDepth = flutterParam->load();
        characterParams.flutterRate = flutterRateParam->load();
        characterParams.hissAmount = hissParam->load();
        characterParams.transportPlaying = transportInfo.isPlaying;
        track.character.process (characterParams, chL, chR, numSamples);

        trackSendTmp.setSize (2, numSamples, false, false, true);
        trackSendTmp.clear();
        auto* trackSendDelay = trackSendTmp.getWritePointer (0);
        auto* trackSendReverb = trackSendTmp.getWritePointer (1);

        tape::ChannelStrip::Params stripParams;
        stripParams.eqLowDb = p.eqLow->load();
        stripParams.eqMidDb = p.eqMid->load();
        stripParams.eqHighDb = p.eqHigh->load();
        stripParams.filterKnob = p.filter->load();
        stripParams.volumeDb = p.volume->load();
        stripParams.pan = p.pan->load();
        stripParams.sendDelayDb = p.sendDelay->load();
        stripParams.sendReverbDb = p.sendReverb->load();

        track.strip.process (stripParams, chL, chR, numSamples, trackSendDelay, trackSendReverb);

        if (audible)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                masterL[i] += chL[i];
                masterR[i] += chR[i];
                sendDelayPtr[i] += trackSendDelay[i];
                sendReverbPtr[i] += trackSendReverb[i];
            }
        }

        const double loopQ = transportInfo.loopQuarters (loopBars);
        const double loopLenSamples = loopQ * (60.0 / juce::jmax (1.0, transportInfo.bpm)) * sampleRate;
        tps.state.store ((int) track.loop.getState());
        tps.readPos.store ((float) track.loop.getReadPosition());
        tps.writePos.store ((float) track.loop.getWritePosition());
        tps.recordedLen.store ((float) track.loop.getRecordedLength());
        tps.loopLengthSamples.store ((float) juce::jmax (1.0, loopLenSamples));
        tps.inputStageActive.store (inputStageActive);
        tps.audible.store (audible);
    }

    tape::LofiBus::Params busParams;
    busParams.delayMs = dlyTimeParam->load();
    busParams.delayFeedback = dlyFbParam->load();
    busParams.delayTone = dlyToneParam->load();
    busParams.delayReturnDb = dlyRetParam->load();
    busParams.reverbSize = revSizeParam->load();
    busParams.reverbDamp = revDampParam->load();
    busParams.reverbReturnDb = revRetParam->load();

    lofiBus.process (busParams, sendDelayPtr, sendReverbPtr, masterL, masterR, numSamples);

    outGain.setTargetValue (juce::Decibels::decibelsToGain (outParam->load()));
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outGain.getNextValue();
        masterL[i] *= g;
        if (masterR != masterL)
            masterR[i] *= g;
    }

    const bool limiterOn = limiterOnParam->load() > 0.5f;
    const bool isLimiting = limiterOn && limiter.process (masterL, masterR, numSamples);
    panelState.limiting.store (isLimiting);

    // meter reads the final signal -- after the limiter, since that's what
    // actually leaves the plug-in
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = juce::jmax (peak, std::abs (masterL[i]), std::abs (masterR[i]));

    // decay the published peak rather than replacing it, so the meter falls
    // smoothly instead of flickering between blocks
    const float previous = panelState.outLevel.load();
    panelState.outLevel.store (juce::jmax (peak, previous * 0.85f));
}

juce::AudioProcessorEditor* AfritProcessor::createEditor()
{
    return new AfritEditor (*this);
}

void AfritProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
}

void AfritProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AfritProcessor();
}
