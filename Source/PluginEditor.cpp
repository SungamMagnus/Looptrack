#include "PluginEditor.h"

#include "Parameters.h"

using namespace panel;
using namespace panel::metric;

namespace
{
    // Layout cursors for the track column, in design pixels.
    constexpr int trackX = pad;
    constexpr int trackY = 46;
    constexpr int trackBodyX = trackX + 14;
    constexpr int trackInnerW = trackW - 28;

    constexpr int globalX = pad + trackW + gap * 2 + 1;

    juce::String t1 (const char* leaf) { return tape::trackParamId (0, leaf); }
}

LooptrackEditor::LooptrackEditor (LooptrackProcessor& p)
    : juce::AudioProcessorEditor (p), looptrack (p)
{
    addAndMakeVisible (content);
    content.setBounds (0, 0, designW, designH);

    buildTrack();
    buildGlobal();

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) designW / (double) designH);
    setResizeLimits (designW / 2, designH / 2, designW * 2, designH * 2);
    setSize (designW, designH);

    startTimerHz (24);
}

void LooptrackEditor::buildTrack()
{
    auto& apvts = looptrack.apvts;
    int y = trackY + 18;

    // -- BARS selector and the state lamps --
    auto& bars = add<SegmentedControl> (apvts, t1 (tape::track::bars), colour::steel);
    bars.setBounds (trackBodyX, y, 88, 20);
    recLamp = &add<Lamp> (colour::coral);
    recLamp->setBounds (trackBodyX + trackInnerW - 24, y + 6, 9, 9);
    playLamp = &add<Lamp> (colour::teal);
    playLamp->setBounds (trackBodyX + trackInnerW - 11, y + 6, 9, 9);
    y += 20 + 10;

    // -- REC / PLAY / CLR --
    auto& rec = add<LatchButton> (apvts, t1 (tape::track::rec), "REC", colour::coral);
    rec.setBounds (trackBodyX, y, 46, 18);
    auto& play = add<LatchButton> (apvts, t1 (tape::track::play), "PLAY", colour::teal);
    play.setBounds (trackBodyX + 52, y, 46, 18);
    auto& clr = add<LatchButton> (apvts, t1 (tape::track::clear), "CLR", colour::steel, true);
    clr.setBounds (trackBodyX + 104, y, 46, 18);
    y += 18 + 10;

    // -- loop strip --
    loopView = &add<LoopView> (looptrack);
    loopView->setBounds (trackBodyX, y, trackInnerW, 30);
    y += 30 + 14;

    auto place = [] (KnobCell& c, int cx, int cy) { c.setBounds (cx, cy, cellW, cellH); };

    // -- INPUT: everything that shapes the signal before it reaches the tape,
    //    boxed with the meter that shows the level it will be recorded at --
    const int inputBoxH = cellH + 32;
    auto& inputBox = add<FrameBox> ("Input", colour::coral);
    inputBox.setBounds (trackBodyX, y, trackInnerW, inputBoxH);

    const int inW = cellW * 3;
    const int inX = trackBodyX + (trackInnerW - inW) / 2;
    preampCell = &add<KnobCell> (apvts, t1 (tape::track::preamp), "Preamp", colour::coral);
    place (*preampCell, inX, y + 14);
    inLowCell = &add<KnobCell> (apvts, t1 (tape::track::inLow), "In Low", colour::coral, true);
    place (*inLowCell, inX + cellW, y + 14);
    inHighCell = &add<KnobCell> (apvts, t1 (tape::track::inHigh), "In High", colour::coral, true);
    place (*inHighCell, inX + cellW * 2, y + 14);

    inMeter = &add<Meter>();
    inMeter->setBounds (trackBodyX + 16, y + inputBoxH - 15, trackInnerW - 32, 8);

    y += inputBoxH + 14;

    // -- the tape's own EQ on the left, filter and sends on the right --
    const int colLeftX = trackBodyX + 20;
    const int colRightX = trackBodyX + trackInnerW - 20 - cellW;

    int ly = y;
    place (add<KnobCell> (apvts, t1 (tape::track::eqHigh), "High", colour::steel, true), colLeftX, ly); ly += cellH;
    place (add<KnobCell> (apvts, t1 (tape::track::eqMid), "Mid", colour::steel, true), colLeftX, ly); ly += cellH;
    place (add<KnobCell> (apvts, t1 (tape::track::eqLow), "Low", colour::steel, true), colLeftX, ly); ly += cellH;

    int ry = y;
    place (add<KnobCell> (apvts, t1 (tape::track::filter), "Filter", colour::steel, true), colRightX, ry); ry += cellH;
    place (add<KnobCell> (apvts, t1 (tape::track::sendDelay), "Dly Send", colour::steel), colRightX, ry); ry += cellH;
    place (add<KnobCell> (apvts, t1 (tape::track::sendReverb), "Verb Send", colour::steel), colRightX, ry); ry += cellH;

    y = juce::jmax (ly, ry) + 8;

    // -- hiss / pan / volume --
    const int bottomW = cellW * 3;
    const int bottomX = trackBodyX + (trackInnerW - bottomW) / 2;
    place (add<KnobCell> (apvts, t1 (tape::track::hiss), "Hiss", colour::teal), bottomX, y);
    place (add<KnobCell> (apvts, t1 (tape::track::pan), "Pan", colour::steel, true), bottomX + cellW, y);
    place (add<KnobCell> (apvts, t1 (tape::track::volume), "Volume", colour::steel, true), bottomX + cellW * 2, y);
}

void LooptrackEditor::buildGlobal()
{
    auto& apvts = looptrack.apvts;
    int y = trackY + 16;

    // -- varispeed --
    auto& speed = add<KnobCell> (apvts, tape::global::speed, "Varispeed", colour::coral, true, knobBigD);
    speed.setBounds (globalX + (globalW - 110) / 2, y, 110, cellH + 14);
    y += cellH + 14 + 10;

    auto frameRow = [&] (const juce::String& title, int height) -> juce::Rectangle<int>
    {
        auto& box = add<FrameBox> (title, colour::teal);
        box.setBounds (globalX, y, globalW, height);
        const auto inner = juce::Rectangle<int> (globalX, y + 14, globalW, height - 18);
        y += height + 10;
        return inner;
    };

    auto spread = [] (juce::Rectangle<int> area, int count, int index)
    {
        const int totalW = cellW * count;
        const int x0 = area.getX() + (area.getWidth() - totalW) / 2;
        return juce::Rectangle<int> (x0 + cellW * index, area.getY(), cellW, cellH);
    };

    {
        const auto area = frameRow ("Tape Character", cellH + 22);
        add<KnobCell> (apvts, t1 (tape::track::wow), "Wow", colour::teal).setBounds (spread (area, 4, 0));
        add<KnobCell> (apvts, t1 (tape::track::wowRate), "Wow Rate", colour::teal).setBounds (spread (area, 4, 1));
        add<KnobCell> (apvts, t1 (tape::track::flutter), "Flutter", colour::teal).setBounds (spread (area, 4, 2));
        add<KnobCell> (apvts, t1 (tape::track::flutterRate), "Flut Rate", colour::teal).setBounds (spread (area, 4, 3));
    }

    {
        const auto area = frameRow ("Delay", cellH + 22);
        add<KnobCell> (apvts, tape::global::dlyTime, "Time", colour::teal).setBounds (spread (area, 4, 0));
        add<KnobCell> (apvts, tape::global::dlyFb, "Fb", colour::teal).setBounds (spread (area, 4, 1));
        add<KnobCell> (apvts, tape::global::dlyTone, "Tone", colour::teal).setBounds (spread (area, 4, 2));
        add<KnobCell> (apvts, tape::global::dlyRet, "Return", colour::teal, true).setBounds (spread (area, 4, 3));
    }

    {
        const auto area = frameRow ("Reverb", cellH + 22);
        add<KnobCell> (apvts, tape::global::revSize, "Size", colour::teal).setBounds (spread (area, 3, 0));
        add<KnobCell> (apvts, tape::global::revDamp, "Damp", colour::teal).setBounds (spread (area, 3, 1));
        add<KnobCell> (apvts, tape::global::revRet, "Return", colour::teal, true).setBounds (spread (area, 3, 2));
    }

    // -- output: meter and terminals on the left, level on the right --
    outSectionY = y;
    meter = &add<Meter>();
    meter->setBounds (globalX, y + 14, 150, 9);

    auto& termL = add<Terminal> ("OUT L");
    termL.setBounds (globalX, y + 32, 32, 26);
    auto& termR = add<Terminal> ("OUT R");
    termR.setBounds (globalX + 38, y + 32, 32, 26);

    auto& out = add<KnobCell> (apvts, tape::pid::out, "Output", colour::steel, true);
    out.setBounds (globalX + globalW - cellW, y + 4, cellW, cellH);
}

void LooptrackEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xffdedad0));

    const auto scale = (float) getWidth() / (float) designW;
    juce::Graphics::ScopedSaveState save (g);
    g.addTransform (juce::AffineTransform::scale (scale));

    const auto page = juce::Rectangle<int> (0, 0, designW, designH).toFloat().reduced (8.0f);
    g.setColour (colour::paper);
    g.fillRect (page);
    g.setColour (colour::inkAlpha (0.45f));
    g.drawRect (page, 1.4f);

    // header
    g.setColour (colour::inkAlpha (0.45f));
    g.setFont (monoFont (9.0f, true));
    g.drawText ("4-TRACK CASSETTE EMULATION - TRACK 1", juce::Rectangle<int> (pad, 20, designW - pad * 2, 14),
                juce::Justification::centredRight);

    // section rules
    g.setColour (colour::coral);
    g.setFont (monoFont (9.0f, true));
    g.drawText ("TRACK 1", juce::Rectangle<int> (trackX + 6, trackY - 6, 80, 12), juce::Justification::centredLeft);
    g.setColour (colour::inkAlpha (0.22f));
    g.drawRect (juce::Rectangle<float> ((float) trackX, (float) trackY, (float) trackW,
                                        (float) (designH - trackY - pad)), 1.2f);

    g.setColour (colour::inkAlpha (0.18f));
    g.drawLine ((float) (globalX - gap), (float) trackY,
                (float) (globalX - gap), (float) (designH - pad), 1.0f);

    g.setColour (colour::inkAlpha (0.78f));
    g.setFont (monoFont (11.0f, true));
    g.drawText ("GLOBAL", juce::Rectangle<int> (globalX, trackY - 8, 120, 14), juce::Justification::centredLeft);
    g.setColour (colour::inkAlpha (0.18f));
    g.drawLine ((float) globalX, (float) (trackY + 8), (float) (globalX + globalW), (float) (trackY + 8), 1.0f);

    // wordmark
    g.setColour (colour::inkAlpha (0.85f));
    g.setFont (monoFont (16.0f, true));
    g.drawText ("LOOPTRACK", juce::Rectangle<int> (globalX, designH - pad - 34, globalW, 20),
                juce::Justification::centredRight);
    g.setColour (colour::inkAlpha (0.45f));
    g.setFont (monoFont (7.5f));
    g.drawText ("TAPE MULTITRACK", juce::Rectangle<int> (globalX, designH - pad - 16, globalW, 10),
                juce::Justification::centredRight);

    g.setColour (colour::inkAlpha (0.45f));
    g.setFont (monoFont (7.0f));
    g.drawText ("OUT LEVEL", juce::Rectangle<int> (globalX, outSectionY + 2, 100, 10),
                juce::Justification::centredLeft);
}

void LooptrackEditor::resized()
{
    const auto scale = (float) getWidth() / (float) designW;
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, designW, designH);
}

void LooptrackEditor::timerCallback()
{
    const auto state = (tape::LoopState) looptrack.panelState.state.load();
    if (recLamp != nullptr)
        recLamp->setOn (state == tape::LoopState::Armed || state == tape::LoopState::Recording);
    if (playLamp != nullptr)
        playLamp->setOn (state == tape::LoopState::Playing);
    if (meter != nullptr)
        meter->setLevel (looptrack.panelState.outLevel.load());
    if (inMeter != nullptr)
        inMeter->setLevel (looptrack.panelState.inLevel.load());

    const bool inputOff = ! looptrack.panelState.inputStageActive.load();
    if (preampCell != nullptr) preampCell->setInactive (inputOff);
    if (inLowCell != nullptr)  inLowCell->setInactive (inputOff);
    if (inHighCell != nullptr) inHighCell->setInactive (inputOff);
}
