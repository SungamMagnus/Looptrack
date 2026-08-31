#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LoopRecorder.h"
#include "PluginProcessor.h"

/** Shows the loop's length and playhead: a bar the width of the current
    musical loop length, filled with what's actually been recorded, with a
    moving head -- the record head while capturing, the playback head once
    it's looping. Polls PanelState on a timer; the audio thread never touches
    this class. */
class LoopView final : public juce::Component, private juce::Timer
{
public:
    explicit LoopView (LooptrackProcessor& p) : processor (p)
    {
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (2.0f);

        g.setColour (juce::Colour (0xff101010));
        g.fillRoundedRectangle (bounds, 4.0f);

        const auto state = (tape::LoopState) panelState().state.load();
        const float loopLen = juce::jmax (1.0f, panelState().loopLengthSamples.load());
        const float recordedLen = panelState().recordedLen.load();
        const float writePos = panelState().writePos.load();
        const float readPos = panelState().readPos.load();

        // filled region: what's actually been captured, against the full loop length
        const float filledSamples = state == tape::LoopState::Recording ? writePos : recordedLen;
        if (filledSamples > 0.0f)
        {
            const float frac = juce::jlimit (0.0f, 1.0f, filledSamples / loopLen);
            g.setColour (juce::Colour (state == tape::LoopState::Recording ? 0xff7a3535 : 0xff2f6478));
            g.fillRoundedRectangle (bounds.withWidth (bounds.getWidth() * frac), 4.0f);
        }

        // playhead
        if (state == tape::LoopState::Playing || state == tape::LoopState::Recording)
        {
            const float pos = state == tape::LoopState::Recording ? writePos : readPos;
            const float frac = juce::jlimit (0.0f, 1.0f, pos / loopLen);
            const float x = bounds.getX() + bounds.getWidth() * frac;
            g.setColour (state == tape::LoopState::Recording ? juce::Colour (0xffe06060)
                                                               : juce::Colour (0xff60e0a8));
            g.fillRect (juce::Rectangle<float> (x - 1.0f, bounds.getY(), 2.0f, bounds.getHeight()));
        }

        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText (stateLabel (state), bounds.reduced (6.0f), juce::Justification::topLeft);

        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
    }

private:
    static const char* stateLabel (tape::LoopState s)
    {
        switch (s)
        {
            case tape::LoopState::Idle: return "IDLE";
            case tape::LoopState::Armed: return "ARMED";
            case tape::LoopState::Recording: return "REC";
            case tape::LoopState::Playing: return "PLAYING";
        }
        return "";
    }

    const PanelState& panelState() const { return processor.panelState; }

    void timerCallback() override { repaint(); }

    LooptrackProcessor& processor;
};
