#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "LoopView.h"
#include "Panel.h"
#include "PluginProcessor.h"

/** The Looptrack panel. Laid out once at design size and scaled as a block,
    so the window is resizable without the geometry drifting. */
class LooptrackEditor final : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit LooptrackEditor (LooptrackProcessor&);
    ~LooptrackEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buildTrack();
    void buildGlobal();
    void timerCallback() override;

    LooptrackProcessor& looptrack;

    /** Everything lives on this child, laid out at design size; the editor
        scales it to whatever the window actually is. */
    juce::Component content;

    std::vector<std::unique_ptr<juce::Component>> owned;
    template <typename T, typename... Args>
    T& add (Args&&... args)
    {
        auto c = std::make_unique<T> (std::forward<Args> (args)...);
        auto& ref = *c;
        content.addAndMakeVisible (ref);
        owned.push_back (std::move (c));
        return ref;
    }

    // referenced after construction, so held as raw observers into `owned`
    LoopView* loopView = nullptr;
    panel::Lamp* recLamp = nullptr;
    panel::Lamp* playLamp = nullptr;
    panel::Meter* meter = nullptr;
    panel::Meter* inMeter = nullptr;
    panel::KnobCell* preampCell = nullptr;
    panel::KnobCell* inLowCell = nullptr;
    panel::KnobCell* inHighCell = nullptr;

    int outSectionY = 0; // set during layout, so paint() can label the meter

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooptrackEditor)
};
