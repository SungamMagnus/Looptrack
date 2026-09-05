#include "PluginEditor.h"

#include "Parameters.h"

using namespace panel;
using namespace panel::metric;

namespace
{
    // Layout cursors, in design pixels. Track columns run left to right
    // starting at `pad`; the global column sits to the right of all four.
    constexpr int trackY = 46;
    constexpr int trackBodyPad = 8; // inset from a track column's left/right edge
    constexpr int trackInnerW = trackW - trackBodyPad * 2;

    constexpr int globalX = pad + trackW * numTracks + trackGap * (numTracks - 1) + gap * 2 + 1;

    int trackColumnX (int index) { return pad + index * (trackW + trackGap); }
}

AfritEditor::AfritEditor (AfritProcessor& p)
    : juce::AudioProcessorEditor (p), afrit (p)
{
    addAndMakeVisible (content);
    content.setBounds (0, 0, designW, designH);

    for (int i = 0; i < kNumTracks; ++i)
        buildTrack (i, trackColumnX (i));
    buildGlobal();

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) designW / (double) designH);
    setResizeLimits (designW / 2, designH / 2, designW * 2, designH * 2);
    setSize (designW, designH);

    startTimerHz (24);
}

void AfritEditor::buildTrack (int trackIndex, int columnX)
{
    auto& apvts = afrit.apvts;
    auto t = [trackIndex] (const char* leaf) { return tape::trackParamId (trackIndex, leaf); };

    const int bodyX = columnX + trackBodyPad;

    // -- row 1: BARS and CLR sharing a row --
    const int y0 = trackY + 14;
    const int selY = y0;
    const int selH = 16;

    auto& bars = add<SegmentedControl> (apvts, t (tape::track::bars), colour::steel);
    bars.setBounds (bodyX, selY, 84, selH);
    auto& clr = add<LatchButton> (apvts, t (tape::track::clear), "CLR", colour::steel, true);
    clr.setBounds (bodyX + 90, selY, 44, selH);

    int y = selY + selH + 6;

    // -- REC / PLAY --
    auto& rec = add<LatchButton> (apvts, t (tape::track::rec), "REC", colour::coral);
    rec.setBounds (bodyX, y, 64, selH);
    auto& play = add<LatchButton> (apvts, t (tape::track::play), "PLAY", colour::teal);
    play.setBounds (bodyX + 70, y, 64, selH);
    y += selH + 4;

    // -- MUTE / SOLO -- mix-time controls, independent of what's on the tape
    auto& mute = add<LatchButton> (apvts, t (tape::track::mute), "MUTE", colour::amber);
    mute.setBounds (bodyX, y, 64, selH);
    auto& solo = add<LatchButton> (apvts, t (tape::track::solo), "SOLO", colour::violet);
    solo.setBounds (bodyX + 70, y, 64, selH);
    y += selH + 8;

    // -- loop strip, with the state lamps sitting inside it at the right --
    const int loopY = y;
    loopView[(size_t) trackIndex] = &add<LoopView> (afrit, trackIndex);
    loopView[(size_t) trackIndex]->setBounds (bodyX, loopY, trackInnerW, 22);

    recLamp[(size_t) trackIndex] = &add<Lamp> (colour::coral);
    recLamp[(size_t) trackIndex]->setBounds (bodyX + trackInnerW - 21, loopY + 7, 7, 7);
    playLamp[(size_t) trackIndex] = &add<Lamp> (colour::teal);
    playLamp[(size_t) trackIndex]->setBounds (bodyX + trackInnerW - 11, loopY + 7, 7, 7);

    y = loopY + 22 + 10;

    auto place = [] (KnobCell& c, int cx, int cy, int w, int h) { c.setBounds (cx, cy, w, h); };

    // -- INPUT: everything that shapes the signal before it reaches the tape,
    //    boxed with the meter that shows the level it will be recorded at --
    const int inputBoxH = 80;
    auto& inputBox = add<FrameBox> ("Input", colour::coral, 7.0f);
    inputBox.setBounds (bodyX, y, trackInnerW, inputBoxH);

    const int inW = cellWTiny * 3;
    const int inX = bodyX + (trackInnerW - inW) / 2;
    const int inY = y + 17;
    auto& preamp = add<KnobCell> (apvts, t (tape::track::preamp), "Preamp", colour::coral, false, knobTinyD);
    preampCell[(size_t) trackIndex] = &preamp;
    place (preamp, inX, inY, cellWTiny, cellHTiny);
    auto& inLow = add<KnobCell> (apvts, t (tape::track::inLow), "In Low", colour::coral, true, knobTinyD);
    inLowCell[(size_t) trackIndex] = &inLow;
    place (inLow, inX + cellWTiny, inY, cellWTiny, cellHTiny);
    auto& inHigh = add<KnobCell> (apvts, t (tape::track::inHigh), "In High", colour::coral, true, knobTinyD);
    inHighCell[(size_t) trackIndex] = &inHigh;
    place (inHigh, inX + cellWTiny * 2, inY, cellWTiny, cellHTiny);

    inMeter[(size_t) trackIndex] = &add<Meter>();
    inMeter[(size_t) trackIndex]->setBounds (bodyX + 8, inY + cellHTiny + 5, trackInnerW - 16, 6);

    y += inputBoxH + 8;

    // -- the tape's own EQ on the left, filter and sends on the right --
    const int colW = cellWTiny * 2;
    const int colLeftX = bodyX + (trackInnerW - colW) / 2;
    const int colRightX = colLeftX + cellWTiny;

    int ly = y;
    place (add<KnobCell> (apvts, t (tape::track::eqHigh), "High", colour::steel, true, knobTinyD), colLeftX, ly, cellWTiny, cellHTiny); ly += cellHTiny;
    place (add<KnobCell> (apvts, t (tape::track::eqMid), "Mid", colour::steel, true, knobTinyD), colLeftX, ly, cellWTiny, cellHTiny); ly += cellHTiny;
    place (add<KnobCell> (apvts, t (tape::track::eqLow), "Low", colour::steel, true, knobTinyD), colLeftX, ly, cellWTiny, cellHTiny); ly += cellHTiny;

    int ry = y;
    place (add<KnobCell> (apvts, t (tape::track::filter), "Filter", colour::steel, true, knobTinyD), colRightX, ry, cellWTiny, cellHTiny); ry += cellHTiny;
    place (add<KnobCell> (apvts, t (tape::track::sendDelay), "Dly Snd", colour::steel, false, knobTinyD), colRightX, ry, cellWTiny, cellHTiny); ry += cellHTiny;
    place (add<KnobCell> (apvts, t (tape::track::sendReverb), "Vrb Snd", colour::steel, false, knobTinyD), colRightX, ry, cellWTiny, cellHTiny); ry += cellHTiny;

    y = juce::jmax (ly, ry) + 6;

    // -- pan, alone and centred, in the row hiss used to occupy (hiss is now
    //    global -- one shared tape transport, so it lives in GLOBAL) --
    const int panX = bodyX + (trackInnerW - cellWTiny) / 2;
    place (add<KnobCell> (apvts, t (tape::track::pan), "Pan", colour::steel, true, knobTinyD), panX, y, cellWTiny, cellHTiny);
    y += cellHTiny + 6;

    // -- volume, alone and centred, at the Global Output knob's size since
    //    it's the other half of the same performance-time gesture --
    const int volX = bodyX + (trackInnerW - cellW) / 2;
    place (add<KnobCell> (apvts, t (tape::track::volume), "Volume", colour::steel, true), volX, y, cellW, cellH);
}

void AfritEditor::buildGlobal()
{
    auto& apvts = afrit.apvts;

    // -- three section columns side by side: Tape Character / Delay / Reverb.
    //    Wow/flutter/hiss are all properties of the shared tape transport --
    //    every track rides the same reel -- so they're global, not per-track. --
    const int colW = 84;
    const int colGap = 10;
    const int col1X = globalX;
    const int col2X = globalX + colW + colGap;
    const int col3X = globalX + (colW + colGap) * 2;
    const int sectionsY = trackY + 16;
    const int knobX0 = (colW - cellWTiny) / 2;
    const int knobStep = cellHTiny + 4;

    auto stackColumn = [&] (const juce::String& title, int colX, int count) -> int
    {
        auto& box = add<FrameBox> (title, colour::teal, 7.0f);
        const int h = 14 + count * cellHTiny + (count - 1) * 4 + 8;
        box.setBounds (colX, sectionsY, colW, h);
        return sectionsY + h;
    };

    auto knobAt = [&] (int colX) { return colX + knobX0; };

    int charBottom = 0, dlyBottom = 0, revBottom = 0;

    {
        charBottom = stackColumn ("Tape Character", col1X, 5);
        int ky = sectionsY + 14;
        add<KnobCell> (apvts, tape::global::wow, "Wow", colour::teal, false, knobTinyD).setBounds (knobAt (col1X), ky, cellWTiny, cellHTiny); ky += knobStep;
        add<KnobCell> (apvts, tape::global::wowRate, "Wow Rate", colour::teal, false, knobTinyD).setBounds (knobAt (col1X), ky, cellWTiny, cellHTiny); ky += knobStep;
        add<KnobCell> (apvts, tape::global::flutter, "Flutter", colour::teal, false, knobTinyD).setBounds (knobAt (col1X), ky, cellWTiny, cellHTiny); ky += knobStep;
        add<KnobCell> (apvts, tape::global::flutterRate, "Flut Rate", colour::teal, false, knobTinyD).setBounds (knobAt (col1X), ky, cellWTiny, cellHTiny); ky += knobStep;
        add<KnobCell> (apvts, tape::global::hiss, "Hiss", colour::teal, false, knobTinyD).setBounds (knobAt (col1X), ky, cellWTiny, cellHTiny);
    }

    {
        dlyBottom = stackColumn ("Delay", col2X, 4);
        int ky = sectionsY + 14;
        add<KnobCell> (apvts, tape::global::dlyTime, "Time", colour::teal, false, knobTinyD).setBounds (knobAt (col2X), ky, cellWTiny, cellHTiny); ky += knobStep;
        add<KnobCell> (apvts, tape::global::dlyFb, "Fb", colour::teal, false, knobTinyD).setBounds (knobAt (col2X), ky, cellWTiny, cellHTiny); ky += knobStep;
        add<KnobCell> (apvts, tape::global::dlyTone, "Tone", colour::teal, false, knobTinyD).setBounds (knobAt (col2X), ky, cellWTiny, cellHTiny); ky += knobStep;
        add<KnobCell> (apvts, tape::global::dlyRet, "Return", colour::teal, true, knobTinyD).setBounds (knobAt (col2X), ky, cellWTiny, cellHTiny);
    }

    {
        revBottom = stackColumn ("Reverb", col3X, 3);
        int ky = sectionsY + 14;
        add<KnobCell> (apvts, tape::global::revSize, "Size", colour::teal, false, knobTinyD).setBounds (knobAt (col3X), ky, cellWTiny, cellHTiny); ky += knobStep;
        add<KnobCell> (apvts, tape::global::revDamp, "Damp", colour::teal, false, knobTinyD).setBounds (knobAt (col3X), ky, cellWTiny, cellHTiny); ky += knobStep;
        add<KnobCell> (apvts, tape::global::revRet, "Return", colour::teal, true, knobTinyD).setBounds (knobAt (col3X), ky, cellWTiny, cellHTiny);
    }

    int y = juce::jmax (charBottom, juce::jmax (dlyBottom, revBottom)) + 30;

    // -- master row: Varispeed (big -- it's the knob you reach for most while
    //    performing) beside the Output cluster (LIMIT + its lamp stacked
    //    above Output, the VU meter inline with the knob itself). Varispeed's
    //    bottom lines up with Output's bottom. --
    const int speedW = 110, speedH = cellH + 14;
    const int meterW = 10, meterH = cellH;
    const int rowGap1 = 14, rowGap2 = 10;
    const int limitH = 16, limitLampGap = 4, lampD = 9, lampOutGap = 6;
    const int outputColH = limitH + limitLampGap + lampD + lampOutGap + cellH;

    const int rowW = speedW + rowGap1 + cellW + rowGap2 + meterW;
    const int rowX = globalX + (globalW - rowW) / 2;

    const int speedX = rowX;
    const int outX = speedX + speedW + rowGap1;
    const int meterX = outX + cellW + rowGap2;
    const int outY = y + limitH + limitLampGap + lampD + lampOutGap;

    auto& speed = add<KnobCell> (apvts, tape::global::speed, "Varispeed", colour::coral, true, knobBigD);
    speed.setBounds (speedX, outY + cellH - speedH, speedW, speedH);

    auto& limitBtn = add<LatchButton> (apvts, tape::pid::limiterOn, "LIMIT", colour::amber);
    limitBtn.setBounds (outX + (cellW - 40) / 2, y, 40, limitH);
    limitLamp = &add<Lamp> (colour::coral);
    limitLamp->setBounds (outX + (cellW - lampD) / 2, y + limitH + limitLampGap, lampD, lampD);

    auto& out = add<KnobCell> (apvts, tape::pid::out, "Output", colour::steel, true);
    out.setBounds (outX, outY, cellW, cellH);

    meter = &add<Meter>();
    meter->setVertical (true);
    meter->setBounds (meterX, outY, meterW, meterH);

    y += outputColH + 10;

}

void AfritEditor::paint (juce::Graphics& g)
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

    // one frame + number per track column
    for (int i = 0; i < kNumTracks; ++i)
    {
        const int tx = trackColumnX (i);
        g.setColour (colour::inkAlpha (0.22f));
        g.drawRect (juce::Rectangle<float> ((float) tx, (float) trackY, (float) trackW,
                                            (float) (designH - trackY - pad)), 1.2f);

        // paper-coloured backing breaks the border line behind the number,
        // the same trick FrameBox uses for its title
        const juce::Rectangle<int> label (tx + 6, trackY - 6, 16, 12);
        g.setColour (colour::paper);
        g.fillRect (label);
        g.setColour (colour::coral);
        g.setFont (panelFont (9.0f, true));
        g.drawText (juce::String (i + 1), label, juce::Justification::centred);
    }

    g.setColour (colour::inkAlpha (0.18f));
    g.drawLine ((float) (globalX - gap), (float) trackY,
                (float) (globalX - gap), (float) (designH - pad), 1.0f);

    g.setColour (colour::inkAlpha (0.78f));
    g.setFont (panelFont (11.0f, true));
    g.drawText ("GLOBAL", juce::Rectangle<int> (globalX, trackY - 8, 120, 14), juce::Justification::centredLeft);
    g.setColour (colour::inkAlpha (0.18f));
    g.drawLine ((float) globalX, (float) (trackY + 8), (float) (globalX + globalW), (float) (trackY + 8), 1.0f);

    // wordmark -- anchored to the page's actual bottom-right corner
    g.setColour (colour::inkAlpha (0.85f));
    g.setFont (panelFont (24.0f, true));
    g.drawText ("AFRIT", juce::Rectangle<int> (globalX, designH - pad - 42, globalW, 28),
                juce::Justification::centredRight);
    g.setColour (colour::inkAlpha (0.45f));
    g.setFont (panelFont (10.0f));
    g.drawText ("TAPE MULTITRACK", juce::Rectangle<int> (globalX, designH - pad - 16, globalW, 14),
                juce::Justification::centredRight);
}

void AfritEditor::resized()
{
    const auto scale = (float) getWidth() / (float) designW;
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, designW, designH);
}

void AfritEditor::timerCallback()
{
    if (meter != nullptr)
        meter->setLevel (afrit.panelState.outLevel.load());
    if (limitLamp != nullptr)
        limitLamp->setOn (afrit.panelState.limiting.load());

    for (int i = 0; i < kNumTracks; ++i)
    {
        const auto& ts = afrit.panelState.tracks[(size_t) i];
        const auto state = (tape::LoopState) ts.state.load();

        if (recLamp[(size_t) i] != nullptr)
            recLamp[(size_t) i]->setOn (state == tape::LoopState::Armed || state == tape::LoopState::Recording);
        if (playLamp[(size_t) i] != nullptr)
            playLamp[(size_t) i]->setOn (state == tape::LoopState::Playing);
        if (inMeter[(size_t) i] != nullptr)
            inMeter[(size_t) i]->setLevel (ts.inLevel.load());

        const bool inputOff = ! ts.inputStageActive.load();
        if (preampCell[(size_t) i] != nullptr) preampCell[(size_t) i]->setInactive (inputOff);
        if (inLowCell[(size_t) i] != nullptr)  inLowCell[(size_t) i]->setInactive (inputOff);
        if (inHighCell[(size_t) i] != nullptr) inHighCell[(size_t) i]->setInactive (inputOff);
    }
}
