#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** Parameter IDs, layout, and engineering-unit conversions.
    Per-track IDs are prefixed from day one (trackParamId) so presets survive
    the day track count grows past one. */
namespace tape
{

constexpr int kNumTracks = 4;

namespace pid
{
    constexpr const char* out = "out";
    constexpr const char* limiterOn = "limiter.on";
}

namespace global
{
    constexpr const char* speed    = "speed"; // varispeed, semitones. id kept for preset compatibility
    // wow/flutter/hiss: one shared tape transport, so the tape character is
    // a global property, not a per-track one -- ids kept from their
    // original per-track home ("t1.wow" etc.) for preset compatibility
    constexpr const char* wow      = "t1.wow";
    constexpr const char* wowRate  = "t1.wowrate";
    constexpr const char* flutter  = "t1.flutter";
    constexpr const char* flutterRate = "t1.flutrate";
    constexpr const char* hiss     = "t1.hiss";
    constexpr const char* dlyTime  = "dly.ms";
    constexpr const char* dlyFb    = "dly.fb";
    constexpr const char* dlyTone  = "dly.tone";
    constexpr const char* dlyRet   = "dly.ret";
    constexpr const char* revSize  = "rev.size";
    constexpr const char* revDamp  = "rev.damp";
    constexpr const char* revRet   = "rev.ret";
}

/** "t1.gain" etc. track is zero-based internally, one-based in the id. */
inline juce::String trackParamId (int track, const char* leaf)
{
    return "t" + juce::String (track + 1) + "." + leaf;
}

namespace track
{
    constexpr const char* bars     = "bars";
    constexpr const char* rec      = "rec";
    constexpr const char* clear    = "clear";
    constexpr const char* play     = "play";
    constexpr const char* mute     = "mute";
    constexpr const char* solo     = "solo";
    constexpr const char* inLow    = "inlo";    // input-stage 2-band shelf, before the preamp
    constexpr const char* inHigh   = "inhi";
    constexpr const char* preamp   = "preamp";  // input trim, before the tape -- drives above 0dB
    constexpr const char* volume   = "volume";  // output fader, after the tape -- post-filter, pre-send
    constexpr const char* pan      = "pan";
    constexpr const char* eqLow    = "eqlo";
    constexpr const char* eqMid    = "eqmid";
    constexpr const char* eqHigh   = "eqhi";
    constexpr const char* filter   = "filter";
    constexpr const char* sendDelay  = "senddly";
    constexpr const char* sendReverb = "sendrev";
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/** dB readout with an "OFF" floor, shared by every gain-shaped knob. */
juce::String dbText (float db, int decimalPlaces = 1);

} // namespace tape
