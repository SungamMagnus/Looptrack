/* Headless correctness checks for the loop engine: drives Transport and
   LoopRecorder directly with a fake playhead, no host required. Prints
   PASS/FAIL per check and exits non-zero if anything failed. */

#include <cmath>
#include <cstdio>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../Source/LoopRecorder.h"
#include "../Source/Transport.h"

using namespace tape;

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
        ts.numerator = tsNum;
        ts.denominator = tsDen;
        info.setTimeSignature (ts);
        return info;
    }

    void advance (int numSamples, double sampleRate)
    {
        ppq += (bpm / (60.0 * sampleRate)) * numSamples;
    }

    double bpm = 120.0;
    double ppq = 0.0;
    bool isPlaying = true;
    int tsNum = 4, tsDen = 4;
};

int failures = 0;

void check (bool cond, const juce::String& what)
{
    printf ("%s  %s\n", cond ? "PASS" : "FAIL", what.toRawUTF8());
    if (! cond)
        ++failures;
}

/** Feeds `inSignal` through the loop in `block`-sized chunks, advancing the
    fake playhead exactly as a host would between callbacks. */
std::vector<float> feed (LoopRecorder& loop, Transport& transport, FakePlayHead& ph,
                          double sr, int block, int bars, bool rec, bool clear, bool playing, double ratio,
                          const std::vector<float>& inSignal)
{
    std::vector<float> out (inSignal.size());
    std::vector<float> tmpL (block), tmpR (block);
    size_t pos = 0;
    while (pos < inSignal.size())
    {
        const int n = (int) std::min ((size_t) block, inSignal.size() - pos);
        auto t = transport.read (&ph, n);
        loop.process (t, bars, rec, clear, playing, ratio, inSignal.data() + pos, inSignal.data() + pos,
                      tmpL.data(), tmpR.data(), n, sr);
        ph.advance (n, sr);
        for (int i = 0; i < n; ++i)
            out[pos + (size_t) i] = tmpL[(size_t) i];
        pos += (size_t) n;
    }
    return out;
}

size_t peakIndexIn (const std::vector<float>& v, size_t start, size_t len)
{
    size_t best = start;
    float bestAbs = 0.0f;
    for (size_t i = start; i < start + len && i < v.size(); ++i)
    {
        const float a = std::abs (v[i]);
        if (a > bestAbs)
        {
            bestAbs = a;
            best = i;
        }
    }
    return best;
}

float peakValueIn (const std::vector<float>& v, size_t start, size_t len)
{
    float best = 0.0f;
    for (size_t i = start; i < start + len && i < v.size(); ++i)
        best = std::max (best, std::abs (v[i]));
    return best;
}

/** Zero-crossing period, in samples, over [start, start+len). */
double measurePeriod (const std::vector<float>& v, size_t start, size_t len)
{
    std::vector<size_t> crossings;
    for (size_t i = start + 1; i < start + len && i < v.size(); ++i)
        if (v[i - 1] <= 0.0f && v[i] > 0.0f)
            crossings.push_back (i);
    if (crossings.size() < 2)
        return 0.0;
    return double (crossings.back() - crossings.front()) / double (crossings.size() - 1);
}

} // namespace

int main()
{
    const double sr = 48000.0;
    const int block = 256;
    const double bpm = 120.0;
    const int bars = 1;
    const double loopSamplesD = bars * 4.0 * (60.0 / bpm) * sr; // 1 bar, 4/4, 120bpm
    const int loopSamples = (int) std::llround (loopSamplesD);

    // --- idle: nothing recorded yet, so the tape has nothing to play and the
    // live input passes straight through -- you can always hear what you are
    // about to record
    {
        Transport transport;
        transport.prepare (sr);
        LoopRecorder loop;
        loop.prepare (sr);
        FakePlayHead ph;
        ph.bpm = bpm;

        std::vector<float> in (2000, 0.5f);
        auto out = feed (loop, transport, ph, sr, block, bars, false, false, true, 1.0, in);
        bool passesThrough = true;
        for (size_t i = 0; i < out.size(); ++i)
            if (std::abs (out[i] - in[i]) > 1e-6f)
                passesThrough = false;
        check (passesThrough, "idle track passes the input through");
    }

    // --- PLAY off while a loop exists: the tape is not driving the output,
    // so monitoring comes back
    {
        Transport transport;
        transport.prepare (sr);
        LoopRecorder loop;
        loop.prepare (sr);
        FakePlayHead ph;
        ph.bpm = bpm;

        std::vector<float> rec ((size_t) loopSamples * 2, 0.3f);
        feed (loop, transport, ph, sr, block, bars, true, false, true, 1.0, rec);
        check (loop.getState() == LoopState::Playing, "monitor test: loop recorded and playing");

        std::vector<float> in ((size_t) block * 4, 0.42f);
        auto out = feed (loop, transport, ph, sr, block, bars, false, false, /*playing*/ false, 1.0, in);
        bool monitors = true;
        for (size_t i = 0; i < out.size(); ++i)
            if (std::abs (out[i] - in[i]) > 1e-6f)
                monitors = false;
        check (monitors, "PLAY off passes the input through instead of the loop");
    }

    // --- arm before/at playback start: recording should begin immediately,
    // not wait for a bar boundary that hasn't happened yet
    {
        Transport transport;
        transport.prepare (sr);
        LoopRecorder loop;
        loop.prepare (sr);
        FakePlayHead ph;
        ph.bpm = bpm;

        const size_t total = (size_t) loopSamples * 3; // record pass + 2 playback passes
        std::vector<float> in (total, 0.0f);
        const int burstLen = (int) (0.05 * sr); // 50 ms, 1 kHz, right at the start
        for (int i = 0; i < burstLen; ++i)
            in[(size_t) i] = 0.5f * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sr);

        // rec is already true on the very first block, at the same instant
        // playback starts -- this is the "armed while stopped, then hit play" case
        auto out = feed (loop, transport, ph, sr, block, bars, true, false, true, 1.0, in);

        check (loop.getState() == LoopState::Playing, "immediate-start: reaches Playing after one recorded pass");

        for (int pass = 0; pass < 2; ++pass)
        {
            const size_t windowStart = (size_t) loopSamples * (size_t) (1 + pass);
            const size_t peak = peakIndexIn (out, windowStart, (size_t) loopSamples);
            const size_t offset = peak - windowStart;
            check (offset < 300, "immediate-start: loop pass " + juce::String (pass + 1)
                                      + " repeats the burst near the loop start (offset "
                                      + juce::String ((int) offset) + " samples)");
        }
    }

    // --- arm while already playing: should still quantize to the next bar,
    // not jump straight into recording
    {
        Transport transport;
        transport.prepare (sr);
        LoopRecorder loop;
        loop.prepare (sr);
        FakePlayHead ph;
        ph.bpm = bpm;

        // prime: get playback running with rec off, so the play-start edge
        // is already behind us by the time we arm
        std::vector<float> silence ((size_t) block * 4, 0.0f);
        feed (loop, transport, ph, sr, block, bars, false, false, true, 1.0, silence);
        check (loop.getState() == LoopState::Idle, "quantized-arm: still idle after priming playback");

        // arm mid-stream: should go Armed and stay there for a while, not
        // jump straight to Recording
        std::vector<float> shortBurst ((size_t) block * 2, 0.1f);
        feed (loop, transport, ph, sr, block, bars, true, false, true, 1.0, shortBurst);
        check (loop.getState() == LoopState::Armed,
               "quantized-arm: stays Armed just after arming mid-stream, doesn't jump straight to Recording");

        // run out the rest of the bar -- should now be Recording
        std::vector<float> rest ((size_t) loopSamples, 0.2f);
        feed (loop, transport, ph, sr, block, bars, true, false, true, 1.0, rest);
        check (loop.getState() == LoopState::Recording, "quantized-arm: reaches Recording at the bar boundary");
    }

    // --- clear: wipes the loop and returns to Idle, even mid-loop
    {
        Transport transport;
        transport.prepare (sr);
        LoopRecorder loop;
        loop.prepare (sr);
        FakePlayHead ph;
        ph.bpm = bpm;

        std::vector<float> in ((size_t) loopSamples * 2, 0.3f);
        feed (loop, transport, ph, sr, block, bars, true, false, true, 1.0, in);
        check (loop.getState() == LoopState::Playing, "clear test: looping before clear is requested");

        std::vector<float> tail ((size_t) block * 2, 0.3f);
        auto out = feed (loop, transport, ph, sr, block, bars, false, true, true, 1.0, tail);

        check (loop.getState() == LoopState::Idle, "clear: returns to Idle");
        check (loop.getRecordedLength() == 0, "clear: recorded length reset to 0");
        // with the tape wiped there is nothing to play back, so the output
        // falls through to the live input rather than to silence
        bool monitorsAfterClear = true;
        for (size_t i = 0; i < out.size(); ++i)
            if (std::abs (out[i] - tail[i]) > 1e-6f)
                monitorsAfterClear = false;
        check (monitorsAfterClear, "clear: the loop is gone and the input is monitored again");
    }

    // --- varispeed: same recorded tone, played back at 1x vs -12 semitones
    {
        auto recordThenPlay = [&] (double ratio) -> std::vector<float>
        {
            Transport transport;
            transport.prepare (sr);
            LoopRecorder loop;
            loop.prepare (sr);
            FakePlayHead ph;
            ph.bpm = bpm;

            const size_t total = (size_t) loopSamples * 2; // record pass + 1 playback pass
            std::vector<float> in (total, 0.0f);
            for (size_t i = 0; i < (size_t) loopSamples; ++i)
                in[i] = 0.4f * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * (double) i / sr);

            return feed (loop, transport, ph, sr, block, bars, true, false, true, ratio, in);
        };

        auto out1x = recordThenPlay (1.0);
        auto outHalf = recordThenPlay (std::pow (2.0, -12.0 / 12.0)); // one octave down

        // measure well inside the playback window, clear of the wrap crossfade
        const size_t measureStart = (size_t) loopSamples + 2000;
        const size_t measureLen = (size_t) loopSamples / 2;

        const double period1x = measurePeriod (out1x, measureStart, measureLen);
        const double periodHalf = measurePeriod (outHalf, measureStart, measureLen);

        check (period1x > 0.0 && periodHalf > 0.0, "varispeed test produced a measurable tone in both cases");
        if (period1x > 0.0 && periodHalf > 0.0)
        {
            const double ratio = periodHalf / period1x;
            check (ratio > 1.8 && ratio < 2.2,
                   "-12 semitones halves the frequency (period ratio " + juce::String (ratio, 2) + ", want ~2.0)");
        }

        bool finite = true;
        for (auto v : outHalf)
            if (! std::isfinite (v))
                finite = false;
        check (finite, "varispeed output stays finite");
    }

    printf ("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
