#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "LoopView.h"
#include "Panel.h"
#include "PluginProcessor.h"

/** The Afrit panel. Laid out once at design size and scaled as a block,
    so the window is resizable without the geometry drifting. */
class AfritEditor final : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit AfritEditor (AfritProcessor&);
    ~AfritEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int kNumTracks = panel::metric::numTracks;

    void buildTrack (int trackIndex, int columnX);
    void buildGlobal();
    void timerCallback() override;

    AfritProcessor& afrit;

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

    // referenced after construction, so held as raw observers into `owned`,
    // one slot per track
    std::array<LoopView*, kNumTracks> loopView {};
    std::array<panel::Lamp*, kNumTracks> recLamp {};
    std::array<panel::Lamp*, kNumTracks> playLamp {};
    std::array<panel::Meter*, kNumTracks> inMeter {};
    std::array<panel::KnobCell*, kNumTracks> preampCell {};
    std::array<panel::KnobCell*, kNumTracks> inLowCell {};
    std::array<panel::KnobCell*, kNumTracks> inHighCell {};

    panel::Meter* meter = nullptr;       // master output meter -- global, not per-track
    panel::Lamp* limitLamp = nullptr;    // lit while the output limiter is actively reducing gain

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfritEditor)
};
