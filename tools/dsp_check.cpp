/* Headless correctness checks for the loop engine: drives Transport and
   LoopRecorder directly with a fake playhead, no host required. Prints
   PASS/FAIL per check and exits non-zero if anything failed. */

#include <cmath>
#include <cstdio>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../Source/LoopRecorder.h"
#include "../Source/PluginProcessor.h"
#include "../Source/TapeCharacter.h"
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

    // --- arm while already playing: should start capturing immediately,
    // not wait for the next bar line
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
        check (loop.getState() == LoopState::Idle, "immediate-arm-while-playing: still idle after priming playback");

        // arm mid-stream: should go straight to Recording on this very block
        std::vector<float> shortBurst ((size_t) block * 2, 0.1f);
        feed (loop, transport, ph, sr, block, bars, true, false, true, 1.0, shortBurst);
        check (loop.getState() == LoopState::Recording,
               "immediate-arm-while-playing: jumps straight to Recording, no wait for the bar line");

        // run for the rest of the bar -- should still be Recording until
        // exactly one loopQ from the arm point has elapsed
        std::vector<float> rest ((size_t) loopSamples - (size_t) block * 2 - 1, 0.2f);
        feed (loop, transport, ph, sr, block, bars, true, false, true, 1.0, rest);
        check (loop.getState() == LoopState::Recording,
               "immediate-arm-while-playing: still recording just before one loopQ has elapsed");

        std::vector<float> tail ((size_t) block * 2, 0.2f);
        feed (loop, transport, ph, sr, block, bars, true, false, true, 1.0, tail);
        check (loop.getState() == LoopState::Playing,
               "immediate-arm-while-playing: wraps to Playing exactly one loopQ after the arm point");
    }

    // --- REC as a toggle: turning it back off mid-recording should punch
    // out immediately (not wait for the bar boundary), keep what was
    // captured, and let the rest of the bar play back as silence rather
    // than shrinking the loop to match the short recording
    {
        Transport transport;
        transport.prepare (sr);
        LoopRecorder loop;
        loop.prepare (sr);
        FakePlayHead ph;
        ph.bpm = bpm;

        const size_t punchInLen = (size_t) (loopSamples * 0.3);
        std::vector<float> firstPart (punchInLen, 0.4f);
        feed (loop, transport, ph, sr, block, bars, true, false, true, 1.0, firstPart);
        check (loop.getState() == LoopState::Recording, "punch-out: recording immediately after arming while playing");

        // click REC again (falling edge) -- should punch out right here
        std::vector<float> oneBlock ((size_t) block, 0.4f);
        feed (loop, transport, ph, sr, block, bars, false, false, true, 1.0, oneBlock);
        check (loop.getState() == LoopState::Playing, "punch-out: turning REC back off stops recording immediately");
        const int recordedLen = loop.getRecordedLength();
        check (recordedLen > 0 && recordedLen < loopSamples / 2,
               "punch-out: recorded length is short (" + juce::String (recordedLen) + " of "
                   + juce::String (loopSamples) + " samples)");

        // run out most of the rest of the original bar -- still Playing
        // (the loop didn't shrink to match the short recording) and the
        // tail should be silence, not passthrough or stale material
        const size_t elapsed = punchInLen + (size_t) block;
        const size_t remaining = (size_t) loopSamples - elapsed - 1000;
        std::vector<float> rest (remaining, 0.4f);
        auto out = feed (loop, transport, ph, sr, block, bars, false, false, true, 1.0, rest);

        check (loop.getState() == LoopState::Playing,
               "punch-out: still Playing right up to the original bar boundary, loop didn't shrink");
        const float tailPeak = peakValueIn (out, remaining - 500, 500);
        check (tailPeak < 1e-6f, "punch-out: silence fills the rest of the bar after the short recording (peak "
                                     + juce::String (tailPeak, 6) + ")");
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

    // --- record-start anchoring: hitting play mid-bar (not on the absolute
    // bar-1 grid) must still produce a loop exactly one bar long, not a
    // short pass with a silence gap and not a doubled pass -- the loop
    // measures from when recording started, not from the nearest downbeat
    {
        Transport transport;
        transport.prepare (sr);
        LoopRecorder loop;
        loop.prepare (sr);
        FakePlayHead ph;
        ph.bpm = bpm;
        ph.ppq = 2.37; // well off any bar-1-relative boundary

        const size_t total = (size_t) loopSamples * 4; // record pass + 3 playback passes
        std::vector<float> in (total, 0.3f);
        auto out = feed (loop, transport, ph, sr, block, bars, true, false, true, 1.0, in);

        check (loop.getState() == LoopState::Playing, "off-grid start: reaches Playing after one recorded pass");
        check (std::abs (loop.getRecordedLength() - loopSamples) <= 2,
               "off-grid start: recorded exactly one bar (" + juce::String (loop.getRecordedLength())
                   + " samples, want " + juce::String (loopSamples) + ")");

        // every subsequent pass should be silence-free (no gap from a short
        // recording) and the loop should not have drifted to a different
        // length -- check the peak value is present near the end of each
        // of the next two passes, not silence from running out of tape
        for (int pass = 1; pass <= 2; ++pass)
        {
            const size_t windowStart = (size_t) loopSamples * (size_t) (1 + pass) - 500;
            const float peak = peakValueIn (out, windowStart, 400);
            check (peak > 0.2f, "off-grid start: pass " + juce::String (pass + 1)
                                     + " still has signal just before the wrap (no early silence gap)");
        }
    }

    // --- playback survives a host stop: a track that's already looping
    // must not go silent (or lose its place) just because the host
    // transport pauses -- e.g. while the user stops to arm another track.
    // Only the track's own PLAY switch should silence it.
    {
        Transport transport;
        transport.prepare (sr);
        LoopRecorder loop;
        loop.prepare (sr);
        FakePlayHead ph;
        ph.bpm = bpm;

        const int burstLen = (int) (0.05 * sr);
        std::vector<float> recSignal ((size_t) loopSamples, 0.0f);
        for (int i = 0; i < burstLen; ++i)
            recSignal[(size_t) i] = 0.5f * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sr);
        feed (loop, transport, ph, sr, block, bars, true, false, true, 1.0, recSignal);
        check (loop.getState() == LoopState::Playing, "stop-survival: recorded and looping before the stop");

        // host stops: process 1.5 loop-lengths' worth of blocks with the
        // playhead frozen (isPlaying=false, ppq held constant), exactly
        // like a real DAW while paused
        ph.isPlaying = false;
        std::vector<float> silentIn ((size_t) block, 0.0f);
        std::vector<float> tmpL ((size_t) block), tmpR ((size_t) block);
        const int stoppedBlocks = (int) ((loopSamples * 3 / 2) / block);
        for (int b = 0; b < stoppedBlocks; ++b)
        {
            auto t = transport.read (&ph, block); // ppq frozen -- FakePlayHead.advance() not called
            loop.process (t, bars, false, false, true, 1.0, silentIn.data(), silentIn.data(),
                          tmpL.data(), tmpR.data(), block, sr);
        }
        check (loop.getState() == LoopState::Playing, "stop-survival: still Playing after a long stop, not stuck/reset");
        check (std::abs (loop.getReadPosition()) < 1e-6, "stop-survival: read head froze during the stop instead of drifting");

        // host resumes -- the loop should still be producing the recorded
        // material, not permanent silence from having run past its content
        // with no wrap ever triggered
        ph.isPlaying = true;
        std::vector<float> resumeIn ((size_t) loopSamples, 0.0f);
        auto resumed = feed (loop, transport, ph, sr, block, bars, false, false, true, 1.0, resumeIn);
        const float peakAfterResume = peakValueIn (resumed, 0, (size_t) loopSamples);
        check (peakAfterResume > 0.3f, "stop-survival: audible again after resuming, not silenced by the stop (peak "
                                            + juce::String (peakAfterResume, 3) + ")");
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

    // --- wow/flutter: a modulated delay line should visibly detune a
    // sustained tone. Feed a fixed 800 Hz tone through TapeCharacter at
    // full depth and compare the zero-crossing period near the start of
    // the wow LFO's cycle against a window near its quarter-cycle (where
    // the delay's rate of change, and so the pitch shift, peaks) -- with
    // depth off the two windows should read the same tone.
    {
        auto renderTone = [&] (float wowDepth, float flutterDepth) -> std::vector<float>
        {
            TapeCharacter character;
            character.prepare (sr);

            const double toneHz = 800.0;
            const int totalSamples = (int) (sr * 2.5); // 2.5s -- past the 0.5Hz LFO's quarter-cycle at ~0.5s
            std::vector<float> l ((size_t) totalSamples), r ((size_t) totalSamples);
            for (int i = 0; i < totalSamples; ++i)
                l[(size_t) i] = r[(size_t) i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * toneHz * i / sr);

            TapeCharacter::Params p;
            p.wowDepth = wowDepth;
            p.wowRate = 1.0f;
            p.flutterDepth = flutterDepth;
            p.flutterRate = 1.0f;
            p.hissAmount = 0.0f;
            p.transportPlaying = true;

            // process in blocks, like a real host would deliver it, rather
            // than one giant call
            const int block = 256;
            int pos = 0;
            while (pos < totalSamples)
            {
                const int n = std::min (block, totalSamples - pos);
                character.process (p, l.data() + pos, r.data() + pos, n);
                pos += n;
            }
            return l;
        };

        auto measureAt = [] (const std::vector<float>& v, double seconds, double sr) -> double
        {
            const size_t start = (size_t) (seconds * sr);
            return measurePeriod (v, start, (size_t) (sr * 0.05)); // 50ms window
        };

        auto wowOff = renderTone (0.0f, 0.0f);
        auto wowOn = renderTone (1.0f, 0.0f);

        const double offEarly = measureAt (wowOff, 0.05, sr);
        const double offLate = measureAt (wowOff, 0.5, sr);
        const double onEarly = measureAt (wowOn, 0.05, sr);
        const double onLate = measureAt (wowOn, 0.5, sr);

        check (offEarly > 0.0 && offLate > 0.0 && onEarly > 0.0 && onLate > 0.0,
               "wow/flutter test produced a measurable tone in all cases");

        if (offEarly > 0.0 && offLate > 0.0)
        {
            const double driftOff = std::abs (offLate - offEarly) / offEarly;
            check (driftOff < 0.001, "wow depth 0: period is stable across the LFO cycle (drift "
                                          + juce::String (driftOff * 100.0, 3) + "%)");
        }
        if (onEarly > 0.0 && onLate > 0.0)
        {
            const double driftOn = std::abs (onLate - onEarly) / onEarly;
            check (driftOn > 0.005, "wow depth 1: period visibly shifts across the LFO cycle (drift "
                                         + juce::String (driftOn * 100.0, 3) + "%, want > 0.5%)");
        }
    }

    // --- wow/flutter through the real plugin, on looped playback --
    // reported as inaudible during loop playback specifically, even though
    // TapeCharacter tested clean in isolation above. Drive the actual
    // LooptrackProcessor::processBlock through record-then-loop with wow
    // maxed, exactly like a user arming, recording a bar, and listening to
    // it loop, and check the played-back tone still drifts.
    {
        auto setValue = [] (LooptrackProcessor& p, const juce::String& id, float value)
        {
            if (auto* param = p.apvts.getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (value));
        };

        auto renderLoop = [&] (float wowDepth) -> std::vector<float>
        {
            LooptrackProcessor proc;
            FakePlayHead ph;
            ph.bpm = bpm;
            proc.setRateAndBufferSizeDetails (sr, block);
            proc.setPlayHead (&ph);
            proc.prepareToPlay (sr, block);

            setValue (proc, "t1.bars", 0.0f);     // 1 bar
            setValue (proc, "t1.play", 1.0f);
            setValue (proc, "t1.wow", wowDepth);
            setValue (proc, "t1.wowrate", 1.0f);
            setValue (proc, "t1.flutter", 0.0f);
            setValue (proc, "t1.hiss", 0.0f);
            setValue (proc, "t1.preamp", 0.0f);
            setValue (proc, "t1.senddly", -60.0f);
            setValue (proc, "t1.sendrev", -60.0f);
            // isolate track 1 -- the other three default to an idle
            // passthrough (with their own default wow/flutter) which would
            // otherwise bleed into the same master mix and confuse the
            // measurement
            setValue (proc, "t2.mute", 1.0f);
            setValue (proc, "t3.mute", 1.0f);
            setValue (proc, "t4.mute", 1.0f);

            const int totalSamples = loopSamples * 3; // 1 record pass + 2 playback passes
            juce::AudioBuffer<float> buf (2, block);
            std::vector<float> out ((size_t) totalSamples);

            bool recSet = false;
            int pos = 0;
            while (pos < totalSamples)
            {
                const int n = std::min (block, totalSamples - pos);
                buf.setSize (2, n, false, false, true);
                for (int i = 0; i < n; ++i)
                {
                    const double t = (double) (pos + i) / sr;
                    const float s = (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 800.0 * t));
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, s);
                }

                if (! recSet)
                {
                    setValue (proc, "t1.rec", 1.0f); // arm before the transport starts, like the app does
                    recSet = true;
                }

                juce::MidiBuffer midi;
                proc.processBlock (buf, midi);

                for (int i = 0; i < n; ++i)
                    out[(size_t) (pos + i)] = buf.getSample (0, i);

                ph.advance (n, sr);
                pos += n;
            }
            return out;
        };

        auto outOff = renderLoop (0.0f);
        auto outOn = renderLoop (1.0f);

        // measure well inside the second playback pass, clear of the wrap
        // crossfade, same window used for the varispeed check above
        const size_t measureStart = (size_t) loopSamples * 2 + 2000;

        const double offEarly = measurePeriod (outOff, measureStart, 2000);
        const double offLate = measurePeriod (outOff, measureStart + 20000, 2000);
        const double onEarly = measurePeriod (outOn, measureStart, 2000);
        const double onLate = measurePeriod (outOn, measureStart + 20000, 2000);

        check (offEarly > 0.0 && offLate > 0.0 && onEarly > 0.0 && onLate > 0.0,
               "full-pipeline wow test produced a measurable looped tone in all cases");
        if (offEarly > 0.0 && offLate > 0.0)
        {
            const double driftOff = std::abs (offLate - offEarly) / offEarly;
            check (driftOff < 0.001, "full pipeline, wow off: looped tone period is stable (drift "
                                          + juce::String (driftOff * 100.0, 3) + "%)");
        }
        if (onEarly > 0.0 && onLate > 0.0)
        {
            const double driftOn = std::abs (onLate - onEarly) / onEarly;
            check (driftOn > 0.005, "full pipeline, wow on: looped tone period visibly shifts (drift "
                                         + juce::String (driftOn * 100.0, 3) + "%, want > 0.5%)");
        }
    }

    // --- 4 tracks: each one records and loops independently, and mute/solo
    // decide what reaches the master mix without touching what's actually
    // recorded or how it loops
    {
        auto setValue = [] (LooptrackProcessor& p, const juce::String& id, float value)
        {
            if (auto* param = p.apvts.getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (value));
        };

        LooptrackProcessor proc;
        FakePlayHead ph;
        ph.bpm = bpm;
        proc.setRateAndBufferSizeDetails (sr, block);
        proc.setPlayHead (&ph);
        proc.prepareToPlay (sr, block);

        // give each track a distinct, easy-to-tell-apart tone and a
        // silent idle passthrough (mute everyone first, un-mute per check)
        for (int t = 1; t <= 4; ++t)
        {
            setValue (proc, "t" + juce::String (t) + ".bars", 0.0f);
            setValue (proc, "t" + juce::String (t) + ".play", 1.0f);
            setValue (proc, "t" + juce::String (t) + ".preamp", 0.0f);
            setValue (proc, "t" + juce::String (t) + ".hiss", 0.0f);
            setValue (proc, "t" + juce::String (t) + ".wow", 0.0f);
            setValue (proc, "t" + juce::String (t) + ".flutter", 0.0f);
            setValue (proc, "t" + juce::String (t) + ".senddly", -60.0f);
            setValue (proc, "t" + juce::String (t) + ".sendrev", -60.0f);
            setValue (proc, "t" + juce::String (t) + ".mute", 1.0f); // silent until recorded+unmuted below
        }

        auto recordTrack = [&] (int track, float amplitude)
        {
            juce::AudioBuffer<float> buf (2, block);
            bool recSet = false;
            int pos = 0;
            while (pos < loopSamples)
            {
                const int n = std::min (block, loopSamples - pos);
                buf.setSize (2, n, false, false, true);
                for (int i = 0; i < n; ++i)
                {
                    buf.setSample (0, i, amplitude);
                    buf.setSample (1, i, amplitude);
                }
                if (! recSet)
                {
                    setValue (proc, "t" + juce::String (track) + ".rec", 1.0f);
                    recSet = true;
                }
                juce::MidiBuffer midi;
                proc.processBlock (buf, midi);
                ph.advance (n, sr);
                pos += n;
            }
        };

        // record 0.2 into track 1 and 0.5 into track 2, back to back (each
        // call leaves the transport wherever it stopped, so track 2 starts
        // mid-loop -- exercises independent per-track loop grids too)
        recordTrack (1, 0.2f);
        recordTrack (2, 0.5f);

        // centre pan is a constant-power law (-3dB/channel, ~0.7071x), so
        // what lands on either channel is amplitude * 0.7071
        constexpr float kCentrePan = 0.70710678f;

        auto renderMasterPeak = [&] () -> float
        {
            juce::AudioBuffer<float> buf (2, block);
            buf.clear();
            juce::MidiBuffer midi;
            proc.processBlock (buf, midi);
            float peak = 0.0f;
            for (int i = 0; i < block; ++i)
                peak = juce::jmax (peak, std::abs (buf.getSample (0, i)));
            return peak;
        };

        // everyone still muted -- silence
        check (renderMasterPeak() < 1e-6f, "4-track mix: all muted is silent");

        // unmute track 1 only -- should hear ~0.2 (post pan-law), not track 2's 0.5
        setValue (proc, "t1.mute", 0.0f);
        const float onlyT1 = renderMasterPeak();
        const float expectT1 = 0.2f * kCentrePan;
        check (onlyT1 > expectT1 - 0.05f && onlyT1 < expectT1 + 0.05f,
               "4-track mix: unmuted track 1 alone reads ~" + juce::String (expectT1, 3) + " ("
                   + juce::String (onlyT1, 3) + ")");

        // unmute track 2 as well -- both audible, peak should reflect track
        // 2's louder 0.5 material at some point in its loop
        setValue (proc, "t2.mute", 0.0f);
        float bothPeak = 0.0f;
        for (int i = 0; i < 20; ++i)
            bothPeak = juce::jmax (bothPeak, renderMasterPeak());
        check (bothPeak > 0.3f, "4-track mix: unmuting track 2 too brings its louder material into the mix ("
                                     + juce::String (bothPeak, 3) + ")");

        // solo track 1 -- even though track 2 isn't muted, solo should
        // exclude it, leaving just track 1's ~0.2
        setValue (proc, "t1.solo", 1.0f);
        const float soloT1 = renderMasterPeak();
        check (soloT1 > expectT1 - 0.05f && soloT1 < expectT1 + 0.05f,
               "4-track mix: soloing track 1 excludes track 2 despite it being unmuted (" + juce::String (soloT1, 3) + ")");
    }

    // --- the reported bug, reproduced directly: record track 1, stop the
    // host (as a user does to go arm another track), record track 2 while
    // the host is running again, and confirm track 1's recording survived
    // the whole thing untouched.
    {
        auto setValue = [] (LooptrackProcessor& p, const juce::String& id, float value)
        {
            if (auto* param = p.apvts.getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (value));
        };

        LooptrackProcessor proc;
        FakePlayHead ph;
        ph.bpm = bpm;
        proc.setRateAndBufferSizeDetails (sr, block);
        proc.setPlayHead (&ph);
        proc.prepareToPlay (sr, block);

        for (int t = 1; t <= 4; ++t)
        {
            setValue (proc, "t" + juce::String (t) + ".bars", 0.0f);
            setValue (proc, "t" + juce::String (t) + ".play", 1.0f);
            setValue (proc, "t" + juce::String (t) + ".preamp", 0.0f);
            setValue (proc, "t" + juce::String (t) + ".mute", 1.0f);
        }

        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;

        // record track 1 (host playing throughout)
        setValue (proc, "t1.rec", 1.0f);
        int pos = 0;
        while (pos < loopSamples)
        {
            const int n = std::min (block, loopSamples - pos);
            buf.setSize (2, n, false, false, true);
            for (int i = 0; i < n; ++i)
            {
                buf.setSample (0, i, 0.35f);
                buf.setSample (1, i, 0.35f);
            }
            proc.processBlock (buf, midi);
            ph.advance (n, sr);
            pos += n;
        }
        check ((tape::LoopState) proc.panelState.tracks[0].state.load() == tape::LoopState::Playing,
               "recording-independence: track 1 finished recording and is looping");

        // host stops -- user is now going to go arm track 2. Process a
        // stretch of blocks with the playhead frozen.
        ph.isPlaying = false;
        buf.setSize (2, block, false, false, true);
        buf.clear();
        for (int b = 0; b < 40; ++b)
            proc.processBlock (buf, midi);

        // host resumes, arm and record track 2
        ph.isPlaying = true;
        setValue (proc, "t2.rec", 1.0f);
        pos = 0;
        while (pos < loopSamples)
        {
            const int n = std::min (block, loopSamples - pos);
            buf.setSize (2, n, false, false, true);
            for (int i = 0; i < n; ++i)
            {
                buf.setSample (0, i, 0.6f);
                buf.setSample (1, i, 0.6f);
            }
            proc.processBlock (buf, midi);
            ph.advance (n, sr);
            pos += n;
        }

        // isolate track 1 and check it's still exactly what was recorded --
        // not silence, not track 2's material
        setValue (proc, "t1.mute", 0.0f);
        buf.setSize (2, block, false, false, true);
        buf.clear();
        float peak = 0.0f;
        for (int b = 0; b < 20; ++b)
        {
            proc.processBlock (buf, midi);
            for (int i = 0; i < block; ++i)
                peak = juce::jmax (peak, std::abs (buf.getSample (0, i)));
        }
        const float expected = 0.35f * 0.70710678f; // centre-pan constant-power law
        check (peak > expected - 0.05f && peak < expected + 0.05f,
               "recording-independence: track 1 survives a host stop + recording track 2 (peak "
                   + juce::String (peak, 3) + ", want ~" + juce::String (expected, 3) + ")");
    }

    // --- output limiter: off lets a hot signal through uncaught, on keeps
    // it under the ceiling, and the panel-facing "limiting" flag only lights
    // up when it's actually doing something
    {
        auto setValue = [] (LooptrackProcessor& p, const juce::String& id, float value)
        {
            if (auto* param = p.apvts.getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (value));
        };

        auto renderHot = [&] (bool limiterOn) -> float
        {
            LooptrackProcessor proc;
            FakePlayHead ph;
            ph.bpm = bpm;
            proc.setRateAndBufferSizeDetails (sr, block);
            proc.setPlayHead (&ph);
            proc.prepareToPlay (sr, block);

            setValue (proc, "t1.bars", 0.0f);
            setValue (proc, "t1.play", 1.0f);
            setValue (proc, "t1.preamp", 0.0f);
            setValue (proc, "out", 18.0f); // deliberately hot, to force limiting
            setValue (proc, "limiter.on", limiterOn ? 1.0f : 0.0f);

            juce::AudioBuffer<float> buf (2, block);
            float peak = 0.0f;
            bool recSet = false;
            int pos = 0;
            const int total = loopSamples + block * 4; // one recorded pass + a few playback blocks
            while (pos < total)
            {
                const int n = std::min (block, total - pos);
                buf.setSize (2, n, false, false, true);
                for (int i = 0; i < n; ++i)
                {
                    const double t = (double) (pos + i) / sr;
                    const float s = (float) (0.8 * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * t));
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, s);
                }
                if (! recSet)
                {
                    setValue (proc, "t1.rec", 1.0f);
                    recSet = true;
                }
                juce::MidiBuffer midi;
                proc.processBlock (buf, midi);
                for (int i = 0; i < n; ++i)
                    peak = juce::jmax (peak, std::abs (buf.getSample (0, i)));
                ph.advance (n, sr);
                pos += n;
            }
            return peak;
        };

        const float peakOff = renderHot (false);
        const float peakOn = renderHot (true);

        check (peakOff > 1.0f, "limiter off: a hot signal (+18dB output) clips right through ("
                                    + juce::String (peakOff, 3) + ")");
        check (peakOn <= 0.98f, "limiter on: the same hot signal stays under the ceiling ("
                                     + juce::String (peakOn, 3) + ")");
    }

    // --- REC latch auto-resets once a recording completes on its own, so
    // a later click reads as "start" again instead of a no-op "stop" --
    // otherwise the parameter would stay stuck on true forever after the
    // first hands-off recording
    {
        auto setValue = [] (LooptrackProcessor& p, const juce::String& id, float value)
        {
            if (auto* param = p.apvts.getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (value));
        };

        LooptrackProcessor proc;
        FakePlayHead ph;
        ph.bpm = bpm;
        proc.setRateAndBufferSizeDetails (sr, block);
        proc.setPlayHead (&ph);
        proc.prepareToPlay (sr, block);
        setValue (proc, "t1.bars", 0.0f);
        setValue (proc, "t1.play", 1.0f);

        juce::AudioBuffer<float> buf (2, block);
        buf.clear();
        juce::MidiBuffer midi;

        setValue (proc, "t1.rec", 1.0f); // arm while already playing -- starts immediately

        int pos = 0;
        while (pos < loopSamples + block) // run past the natural boundary
        {
            proc.processBlock (buf, midi);
            ph.advance (block, sr);
            pos += block;
        }

        auto* recParam = proc.apvts.getParameter ("t1.rec");
        check (recParam != nullptr && recParam->getValue() < 0.5f,
               "REC latch resets to off once the recording completes on its own");
    }

    printf ("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
