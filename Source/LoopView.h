#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LoopRecorder.h"
#include "Panel.h"
#include "PluginProcessor.h"

/** The loop's length and playhead: a thin strip the width of the current
    musical loop, filled with what's actually been recorded, with a moving
    head -- the record head while capturing, the playback head once it's
    looping. Polls PanelState on a timer; the audio thread never touches
    this class. */
class LoopView final : public juce::Component, private juce::Timer
{
public:
    explicit LoopView (LooptrackProcessor& p, int track = 0) : processor (p), trackIndex (track)
    {
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour (juce::Colour (0xff101010));
        g.fillRect (bounds);

        const auto state = (tape::LoopState) panelState().state.load();
        const float loopLen = juce::jmax (1.0f, panelState().loopLengthSamples.load());
        const float recordedLen = panelState().recordedLen.load();
        const float writePos = panelState().writePos.load();
        const float readPos = panelState().readPos.load();

        const bool recording = state == tape::LoopState::Recording;

        // bar gridlines -- eight to a loop, so the strip reads as musical time
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        for (int i = 1; i < 8; ++i)
        {
            const float x = bounds.getWidth() * (float) i / 8.0f;
            g.drawLine (x, bounds.getY(), x, bounds.getBottom(), 1.0f);
        }

        // filled region: what's actually been captured, against the full loop
        const float filled = recording ? writePos : recordedLen;
        if (filled > 0.0f)
        {
            const float frac = juce::jlimit (0.0f, 1.0f, filled / loopLen);
            g.setColour (juce::Colour (recording ? 0xff7a3535 : 0xff2f6478));
            g.fillRect (bounds.withWidth (bounds.getWidth() * frac));
        }

        if (state == tape::LoopState::Playing || recording)
        {
            const float pos = recording ? writePos : readPos;
            const float frac = juce::jlimit (0.0f, 1.0f, pos / loopLen);
            const float x = bounds.getX() + bounds.getWidth() * frac;
            g.setColour (juce::Colour (recording ? 0xffe06060 : 0xff60e0a8));
            g.fillRect (juce::Rectangle<float> (x - 1.0f, bounds.getY(), 2.0f, bounds.getHeight()));
        }

        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (panel::panelFont (9.0f, true));
        g.drawText (stateLabel (state), bounds.reduced (6.0f, 0.0f), juce::Justification::centredLeft);

        g.setColour (panel::colour::inkAlpha (0.38f));
        g.drawRect (bounds, 1.0f);
    }

private:
    static const char* stateLabel (tape::LoopState s)
    {
        switch (s)
        {
            case tape::LoopState::Idle:      return "IDLE";
            case tape::LoopState::Armed:     return "ARMED";
            case tape::LoopState::Recording: return "REC";
            case tape::LoopState::Playing:   return "PLAYING";
        }
        return "";
    }

    const TrackPanelState& panelState() const { return processor.panelState.tracks[(size_t) trackIndex]; }

    void timerCallback() override { repaint(); }

    LooptrackProcessor& processor;
    int trackIndex;
};
