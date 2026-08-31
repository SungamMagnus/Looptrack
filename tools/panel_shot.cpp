// Offscreen render of the real editor. Dev tool: verifies the panel without a
// host or screen-recording permission. Writes panel.png into the directory
// given as argv[1].
#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Panel.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{

struct FakePlayHead : public juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setIsPlaying (isPlaying);
        juce::AudioPlayHead::TimeSignature ts;
        ts.numerator = 4;
        ts.denominator = 4;
        info.setTimeSignature (ts);
        return info;
    }

    double bpm = 120.0;
    double ppq = 0.0;
    bool isPlaying = true;
};

void setNorm (LooptrackProcessor& p, const juce::String& id, float norm)
{
    if (auto* param = p.apvts.getParameter (id))
        param->setValueNotifyingHost (norm);
}

void setValue (LooptrackProcessor& p, const juce::String& id, float value)
{
    if (auto* param = p.apvts.getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::String ("."));

    const double sr = 48000.0;
    const int blockSize = 256;

    LooptrackProcessor proc;
    FakePlayHead playHead;
    proc.setPlayHead (&playHead);
    proc.setRateAndBufferSizeDetails (sr, blockSize);
    proc.prepareToPlay (sr, blockSize);

    /* A working patch: some character dialed in so every part of the panel
       has something to show. */
    setValue (proc, tape::trackParamId (0, tape::track::wow), 0.5f);
    setValue (proc, tape::trackParamId (0, tape::track::flutter), 0.4f);
    setValue (proc, tape::trackParamId (0, tape::track::hiss), 0.3f);
    setValue (proc, tape::trackParamId (0, tape::track::eqLow), 3.0f);
    setValue (proc, tape::trackParamId (0, tape::track::eqHigh), -2.0f);
    setValue (proc, tape::trackParamId (0, tape::track::preamp), 6.0f);
    setNorm (proc, tape::trackParamId (0, tape::track::bars), 0.33f); // "2"

    /* Record one bar's worth of a burst, then let it loop for a couple of
       passes, so the loop view shows a filled region and a moving head. */
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    const double loopSamples = 2.0 * 4.0 * (60.0 / playHead.bpm) * sr; // 2 bars, 4/4, 120bpm
    const int totalBlocks = (int) std::ceil ((loopSamples * 2.5) / blockSize);

    bool armed = false;
    for (int block = 0; block < totalBlocks; ++block)
    {
        if (! armed)
        {
            setNorm (proc, tape::trackParamId (0, tape::track::rec), 1.0f);
            armed = true;
        }

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < blockSize; ++i)
                d[i] = 0.3f * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0
                                         * (block * blockSize + i) / sr);
        }

        proc.processBlock (buffer, midi);
        playHead.ppq += (playHead.bpm / (60.0 * sr)) * blockSize;
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    editor->setSize (panel::metric::designW, panel::metric::designH);

    juce::Image img (juce::Image::ARGB, editor->getWidth() * 2, editor->getHeight() * 2, true);
    juce::Graphics g (img);
    g.addTransform (juce::AffineTransform::scale (2.0f));
    editor->paintEntireComponent (g, false);

    const auto out = outDir.getChildFile ("panel.png");
    juce::FileOutputStream stream (out);
    stream.setPosition (0);
    stream.truncate();
    juce::PNGImageFormat().writeImageToStream (img, stream);
    std::printf ("%s\n", out.getFullPathName().toRawUTF8());

    proc.setPlayHead (nullptr);
    return 0;
}
