#include "Panel.h"

#include "BinaryData.h"

namespace panel
{

namespace
{
    /** Regular and Bold are separate embedded files, not one variable font --
        JUCE's Font::bold on a single weight just skews the strokes, which
        looks wrong next to a face that has a real bold. Loaded once and kept:
        Typeface::createSystemTypefaceFor parses the font on every call. */
    juce::Typeface::Ptr regularTypeface()
    {
        static juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor (
            BinaryData::NunitoRegular_ttf, (size_t) BinaryData::NunitoRegular_ttfSize);
        return t;
    }

    juce::Typeface::Ptr boldTypeface()
    {
        static juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor (
            BinaryData::NunitoBold_ttf, (size_t) BinaryData::NunitoBold_ttfSize);
        return t;
    }
}

juce::Font panelFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height).withTypeface (bold ? boldTypeface() : regularTypeface()));
}

// ---------------------------------------------------------------- knob

void KnobLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float rotaryStartAngle,
                                        float rotaryEndAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float lineW = 3.0f;
    const float arcR = radius - lineW * 0.5f;

    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);

    juce::Path background;
    background.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                              rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (colour::inkAlpha (0.16f));
    g.strokePath (background, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

    const float arcFrom = bipolar ? (rotaryStartAngle + rotaryEndAngle) * 0.5f : rotaryStartAngle;
    if (std::abs (angle - arcFrom) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                             juce::jmin (arcFrom, angle), juce::jmax (arcFrom, angle), true);
        g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
        g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    juce::Path pointer;
    pointer.startNewSubPath (0.0f, -radius * 0.30f);
    pointer.lineTo (0.0f, -radius * 0.92f);
    g.setColour (colour::ink);
    g.strokePath (pointer, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded),
                  juce::AffineTransform::rotation (angle).translated (centre));
}

KnobCell::KnobCell (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                    const juce::String& label, juce::Colour arcColour, bool bipolar, int diameter)
    : apvts (state), parameterId (paramId), name (label), knobDiameter (diameter)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::degreesToRadians (metric::startDeg),
                                juce::degreesToRadians (metric::startDeg + metric::sweepDeg),
                                true);
    slider.setColour (juce::Slider::rotarySliderFillColourId, arcColour);
    slider.getProperties().set ("bipolar", bipolar);
    slider.setLookAndFeel (&lookAndFeel);
    slider.addListener (this);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, parameterId, slider);

    refreshValueText();
}

KnobCell::~KnobCell()
{
    slider.removeListener (this);
    slider.setLookAndFeel (nullptr);
}

void KnobCell::resized()
{
    const int cx = getWidth() / 2;
    slider.setBounds (cx - knobDiameter / 2, 12, knobDiameter, knobDiameter);
}

void KnobCell::paint (juce::Graphics& g)
{
    const float alpha = inactive ? 0.3f : 1.0f;

    g.setColour (colour::inkAlpha (0.55f * alpha));
    g.setFont (panelFont (8.5f));
    g.drawText (name, getLocalBounds().removeFromTop (11), juce::Justification::centred);

    g.setColour (colour::inkAlpha (0.70f * alpha));
    g.setFont (panelFont (8.5f, true));
    g.drawText (valueText, getLocalBounds().removeFromBottom (12), juce::Justification::centred);
}

void KnobCell::setInactive (bool shouldBeInactive)
{
    if (inactive == shouldBeInactive)
        return;
    inactive = shouldBeInactive;
    slider.setEnabled (! inactive);
    slider.setAlpha (inactive ? 0.3f : 1.0f);
    repaint();
}

void KnobCell::sliderValueChanged (juce::Slider*)
{
    refreshValueText();
}

void KnobCell::refreshValueText()
{
    if (auto* p = apvts.getParameter (parameterId))
    {
        const auto text = p->getCurrentValueAsText();
        if (text != valueText)
        {
            valueText = text;
            repaint();
        }
    }
}

// ---------------------------------------------------------- segmented

SegmentedControl::SegmentedControl (juce::AudioProcessorValueTreeState& state,
                                    const juce::String& paramId, juce::Colour activeColour)
    : active (activeColour)
{
    parameter = state.getParameter (paramId);
    jassert (parameter != nullptr);

    if (parameter != nullptr)
    {
        choices = parameter->getAllValueStrings();
        attachment = std::make_unique<juce::ParameterAttachment> (
            *parameter,
            [this] (float newValue)
            {
                if (parameter != nullptr)
                {
                    selectedIndex = juce::roundToInt (parameter->convertTo0to1 (newValue)
                                                        * (float) juce::jmax (1, choices.size() - 1));
                    repaint();
                }
            });
        attachment->sendInitialUpdate();
    }
}

void SegmentedControl::paint (juce::Graphics& g)
{
    if (choices.isEmpty())
        return;

    const auto bounds = getLocalBounds().toFloat();
    const float segW = bounds.getWidth() / (float) choices.size();

    for (int i = 0; i < choices.size(); ++i)
    {
        const auto seg = juce::Rectangle<float> (bounds.getX() + segW * (float) i, bounds.getY(),
                                                 segW, bounds.getHeight());
        const bool isOn = i == selectedIndex;

        if (isOn)
        {
            g.setColour (colour::ink);
            g.fillRect (seg);
        }

        g.setColour (isOn ? colour::paper : colour::inkAlpha (0.62f));
        g.setFont (panelFont (8.5f, true));
        g.drawText (choices[i], seg, juce::Justification::centred);

        if (i > 0)
        {
            g.setColour (colour::inkAlpha (0.18f));
            g.drawLine (seg.getX(), seg.getY(), seg.getX(), seg.getBottom(), 1.0f);
        }
    }

    g.setColour (colour::inkAlpha (0.32f));
    g.drawRect (bounds, 1.2f);
}

void SegmentedControl::mouseDown (const juce::MouseEvent& e)
{
    if (parameter == nullptr || choices.isEmpty())
        return;

    const int index = juce::jlimit (0, choices.size() - 1,
                                    (int) ((float) e.x / (float) getWidth() * (float) choices.size()));
    const float norm = choices.size() > 1 ? (float) index / (float) (choices.size() - 1) : 0.0f;
    attachment->setValueAsCompleteGesture (parameter->convertFrom0to1 (norm));
}

// -------------------------------------------------------------- latch

LatchButton::LatchButton (juce::AudioProcessorValueTreeState& s, const juce::String& paramId,
                          const juce::String& label, juce::Colour onColour, bool momentary)
    : text (label), on (onColour), isMomentary (momentary)
{
    parameter = s.getParameter (paramId);
    jassert (parameter != nullptr);

    if (parameter != nullptr)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (
            *parameter,
            [this] (float newValue)
            {
                state = newValue > 0.5f;
                repaint();
            });
        attachment->sendInitialUpdate();
    }
}

void LatchButton::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    if (state)
    {
        g.setColour (on);
        g.fillRect (bounds);
    }

    g.setColour (state ? on : colour::inkAlpha (0.32f));
    g.drawRect (bounds, 1.2f);

    g.setColour (state ? juce::Colours::white : colour::inkAlpha (0.62f));
    g.setFont (panelFont (7.5f, true));
    g.drawText (text, bounds, juce::Justification::centred);
}

void LatchButton::mouseDown (const juce::MouseEvent&)
{
    if (parameter == nullptr)
        return;

    if (isMomentary)
    {
        // pulse: the audio thread edge-detects the rise, so it has to stay
        // high long enough to be seen by at least one block
        attachment->setValueAsCompleteGesture (1.0f);
        startTimer (120);
    }
    else
    {
        attachment->setValueAsCompleteGesture (state ? 0.0f : 1.0f);
    }
}

void LatchButton::timerCallback()
{
    stopTimer();
    if (parameter != nullptr)
        attachment->setValueAsCompleteGesture (0.0f);
}

// --------------------------------------------------------------- bits

void Lamp::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (lit ? colour : colour::inkAlpha (0.13f));
    g.fillEllipse (bounds);
    g.setColour (lit ? colour : colour::inkAlpha (0.32f));
    g.drawEllipse (bounds, 1.0f);
}

void Lamp::setOn (bool shouldBeOn)
{
    if (lit != shouldBeOn)
    {
        lit = shouldBeOn;
        repaint();
    }
}

void Meter::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (colour::inkAlpha (0.13f));
    g.fillRect (bounds);
    g.setColour (colour::steel);
    g.fillRect (bounds.withWidth (bounds.getWidth() * juce::jlimit (0.0f, 1.0f, level)));
    g.setColour (colour::inkAlpha (0.32f));
    g.drawRect (bounds, 1.0f);
}

void Meter::setLevel (float newLevel)
{
    if (std::abs (newLevel - level) > 0.005f)
    {
        level = newLevel;
        repaint();
    }
}

void FrameBox::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.6f);
    g.setColour (colour::inkAlpha (0.22f));
    g.drawRect (bounds, 1.2f);

    g.setFont (panelFont (9.0f, true));
    const juce::String upper = title.toUpperCase();
    // measure what's actually drawn, not the mixed-case source string -- a
    // proportional face's capitals run wider than its lowercase, so the two
    // can disagree (a monospace face never surfaced this, same width either way)
    const int textW = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), upper) + 10;
    const juce::Rectangle<int> label (10, 0, textW, 12);

    g.setColour (colour::paper);
    g.fillRect (label);
    g.setColour (colour);
    g.drawText (upper, label, juce::Justification::centred);
}

void Terminal::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    const auto dot = bounds.removeFromTop (14).withSizeKeepingCentre (14, 14).toFloat();

    g.setColour (juce::Colour (0xff222222));
    g.fillEllipse (dot);
    g.setColour (colour::inkAlpha (0.45f));
    g.drawEllipse (dot, 1.0f);

    g.setColour (colour::inkAlpha (0.45f));
    g.setFont (panelFont (6.5f));
    g.drawText (label, bounds, juce::Justification::centredTop);
}

} // namespace panel
