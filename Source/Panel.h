#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

/** The panel's visual language, shared with web/style.css so the plug-in and
    the browser prototype look like the same instrument. Colours are the
    Sungam palette: paper and ink are the constant, and every accent names a
    part of the circuit rather than decorating it. */
namespace panel
{

namespace colour
{
    const juce::Colour paper   { 0xfff0ece2 };
    const juce::Colour ink     { 0xff1a1a17 };
    const juce::Colour coral   { 0xffed8159 }; // the input stage / record path
    const juce::Colour teal    { 0xff52b0a4 }; // tape character and the FX bus
    const juce::Colour steel   { 0xff4f7ea8 }; // everything after the tape
    const juce::Colour violet  { 0xff6b5bc4 };
    const juce::Colour amber   { 0xffc08d16 };

    inline juce::Colour inkAlpha (float a) { return ink.withAlpha (a); }
}

/** Design-pixel geometry. The editor lays out at this size and scales as one
    block, so these numbers are also hit-test geometry. */
namespace metric
{
    constexpr int numTracks = 4;
    constexpr int trackW    = 150;  // one track column
    constexpr int trackGap  = 10;   // between track columns
    constexpr int globalW   = 272;
    constexpr int pad       = 24;
    constexpr int gap       = 16;   // track block <-> global divider

    constexpr int designW = pad * 2 + trackW * numTracks + trackGap * (numTracks - 1) + gap * 2 + 1 + globalW;
    constexpr int designH = 565;

    constexpr int knobD      = 46;   // standard knob diameter (global column)
    constexpr int knobBigD   = 60;   // varispeed
    constexpr int knobSmallD = 34;   // unused now that tracks are always compact; kept for reference
    constexpr int knobTinyD  = 26;   // every knob within a compact track column
    constexpr int cellH      = 70;   // label + knob + value (global column)
    constexpr int cellW      = 76;
    constexpr int cellHSmall = 58;
    constexpr int cellWSmall = 66;
    constexpr int cellHBig   = 82;
    constexpr int cellWBig   = 96;
    constexpr int cellHTiny  = 46;   // every cell within a compact track column
    constexpr int cellWTiny  = 44;

    constexpr float sweepDeg = 317.2f;
    constexpr float startDeg = 201.4f; // clockwise from 12 o'clock
}

/** The panel's typeface: Nunito, embedded (SIL OFL 1.1). Matches
    web/style.css's --font-panel token, so the plug-in and the browser
    prototype render in the same face. */
juce::Font panelFont (float height, bool bold = false);

/** Flat rotary: a background arc over the full sweep, a value arc, and a
    pointer. Bipolar knobs grow their arc from noon instead of from the
    anticlockwise stop, so "no change" reads as an empty arc. */
class KnobLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;
};

/** A knob with its name above and its value, in engineering units, below --
    the value text comes from the parameter itself, so it always matches what
    the host shows. */
class KnobCell final : public juce::Component,
                       private juce::Slider::Listener
{
public:
    KnobCell (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
              const juce::String& label, juce::Colour arcColour,
              bool bipolar = false, int diameter = metric::knobD);
    ~KnobCell() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Greys the cell out and stops it taking mouse input -- used for the
        input stage once a loop is playing back and it has nothing to act on. */
    void setInactive (bool shouldBeInactive);

private:
    void sliderValueChanged (juce::Slider*) override;
    void refreshValueText();

    juce::AudioProcessorValueTreeState& apvts;
    juce::String parameterId;
    juce::String name;
    juce::String valueText;
    int knobDiameter;
    bool inactive = false;

    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    KnobLookAndFeel lookAndFeel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobCell)
};

/** Segmented button row bound to a choice parameter (BARS, SRC). */
class SegmentedControl final : public juce::Component
{
public:
    SegmentedControl (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                      juce::Colour activeColour);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter* parameter = nullptr;
    juce::StringArray choices;
    int selectedIndex = 0;
    juce::Colour active;
    std::unique_ptr<juce::ParameterAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegmentedControl)
};

/** A latching or momentary button bound to a bool parameter. REC and PLAY
    latch; CLR is momentary -- it pulses the parameter so the audio thread
    sees a rising edge, then releases it. */
class LatchButton final : public juce::Component,
                          private juce::Timer
{
public:
    LatchButton (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                 const juce::String& label, juce::Colour onColour, bool momentary = false);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    juce::RangedAudioParameter* parameter = nullptr;
    juce::String text;
    juce::Colour on;
    bool isMomentary;
    bool state = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LatchButton)
};

/** A small indicator lamp -- driven by the panel state, not by a parameter. */
class Lamp final : public juce::Component
{
public:
    explicit Lamp (juce::Colour c) : colour (c) {}
    void paint (juce::Graphics&) override;
    void setOn (bool shouldBeOn);

private:
    juce::Colour colour;
    bool lit = false;
};

/** Horizontal peak meter. */
class Meter final : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void setLevel (float newLevel);

    /** Fills bottom-up instead of left-to-right -- for a slim meter sitting
        beside a knob rather than spanning a row. */
    void setVertical (bool shouldBeVertical) { vertical = shouldBeVertical; }

private:
    float level = 0.0f;
    bool vertical = false;
};

/** A titled hairline box -- the frames the global sections sit in. */
class FrameBox final : public juce::Component
{
public:
    // titleSize shrinks the label for boxes too narrow for the default size
    // (the vertical GLOBAL section columns) -- the label is measured and
    // drawn at whatever size is passed, so it never overflows the frame.
    FrameBox (const juce::String& t, juce::Colour c, float titleSize = 9.0f)
        : title (t), colour (c), titleFontSize (titleSize) {}
    void paint (juce::Graphics&) override;

private:
    juce::String title;
    juce::Colour colour;
    float titleFontSize;
};

} // namespace panel
