#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "LoopView.h"
#include "PluginProcessor.h"

/** Loop visualization on top, the generic parameter list below -- swapped
    for a real panel once the DSP is proven out (build checklist step 10). */
class LooptrackEditor final : public juce::AudioProcessorEditor
{
public:
    explicit LooptrackEditor (LooptrackProcessor& p)
        : juce::AudioProcessorEditor (p), loopView (p), paramList (p)
    {
        addAndMakeVisible (loopView);
        addAndMakeVisible (paramList);
        setSize (520, 520);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        loopView.setBounds (bounds.removeFromTop (80).reduced (8));
        paramList.setBounds (bounds);
    }

private:
    LoopView loopView;
    juce::GenericAudioProcessorEditor paramList;
};
