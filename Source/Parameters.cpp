#include "Parameters.h"

namespace tape
{

juce::String dbText (float db, int decimalPlaces)
{
    if (db <= -59.9f)
        return "OFF";
    // an explicit + on boosts, and no "-0.0" for a value that rounds to zero
    const float shown = std::abs (db) < 0.05f ? 0.0f : db;
    return (shown >= 0.0f ? "+" : "") + juce::String (shown, decimalPlaces) + " dB";
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto dbAttrs = [] (const juce::String& suffix)
    {
        return juce::AudioParameterFloatAttributes {}.withLabel (suffix);
    };

    auto pctAttrs = []
    {
        return juce::AudioParameterFloatAttributes {}.withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; });
    };

    for (int t = 0; t < kNumTracks; ++t)
    {
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { trackParamId (t, track::bars), 1 },
            "Track " + juce::String (t + 1) + " Loop Bars",
            juce::StringArray { "1", "2", "3", "4" }, 1));

        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { trackParamId (t, track::rec), 1 },
            "Track " + juce::String (t + 1) + " Record", false));

        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { trackParamId (t, track::play), 1 },
            "Track " + juce::String (t + 1) + " Play", true));

        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { trackParamId (t, track::clear), 1 },
            "Track " + juce::String (t + 1) + " Clear", false));

        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { trackParamId (t, track::mute), 1 },
            "Track " + juce::String (t + 1) + " Mute", false));

        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { trackParamId (t, track::solo), 1 },
            "Track " + juce::String (t + 1) + " Solo", false));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { trackParamId (t, track::inLow), 1 },
            "Track " + juce::String (t + 1) + " In Low",
            juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
            dbAttrs ("dB").withStringFromValueFunction (
                [] (float v, int) { return dbText (v); })));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { trackParamId (t, track::inHigh), 1 },
            "Track " + juce::String (t + 1) + " In High",
            juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
            dbAttrs ("dB").withStringFromValueFunction (
                [] (float v, int) { return dbText (v); })));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { trackParamId (t, track::preamp), 1 },
            "Track " + juce::String (t + 1) + " Preamp",
            juce::NormalisableRange<float> (-24.0f, 18.0f, 0.01f), 0.0f,
            dbAttrs ("dB").withStringFromValueFunction (
                [] (float v, int) { return dbText (v); })));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { trackParamId (t, track::volume), 1 },
            "Track " + juce::String (t + 1) + " Volume",
            juce::NormalisableRange<float> (-60.0f, 6.0f, 0.01f), 0.0f,
            dbAttrs ("dB").withStringFromValueFunction (
                [] (float v, int) { return dbText (v); })));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { trackParamId (t, track::pan), 1 },
            "Track " + juce::String (t + 1) + " Pan",
            juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f,
            juce::AudioParameterFloatAttributes {}.withStringFromValueFunction (
                [] (float v, int) {
                    if (std::abs (v) < 0.01f) return juce::String ("C");
                    return (v < 0.0f ? "L" : "R") + juce::String (juce::roundToInt (std::abs (v) * 100.0f));
                })));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { trackParamId (t, track::eqLow), 1 },
            "Track " + juce::String (t + 1) + " Low",
            juce::NormalisableRange<float> (-18.0f, 18.0f, 0.01f), 0.0f,
            dbAttrs ("dB").withStringFromValueFunction (
                [] (float v, int) { return dbText (v); })));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { trackParamId (t, track::eqMid), 1 },
            "Track " + juce::String (t + 1) + " Mid",
            juce::NormalisableRange<float> (-18.0f, 18.0f, 0.01f), 0.0f,
            dbAttrs ("dB").withStringFromValueFunction (
                [] (float v, int) { return dbText (v); })));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { trackParamId (t, track::eqHigh), 1 },
            "Track " + juce::String (t + 1) + " High",
            juce::NormalisableRange<float> (-18.0f, 18.0f, 0.01f), 0.0f,
            dbAttrs ("dB").withStringFromValueFunction (
                [] (float v, int) { return dbText (v); })));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { trackParamId (t, track::filter), 1 },
            "Track " + juce::String (t + 1) + " Filter",
            juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f,
            juce::AudioParameterFloatAttributes {}.withStringFromValueFunction (
                [] (float v, int) {
                    if (std::abs (v) < 0.02f) return juce::String ("OFF");
                    return v < 0.0f ? "LP" : juce::String ("HP");
                })));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { trackParamId (t, track::sendDelay), 1 },
            "Track " + juce::String (t + 1) + " Delay Send",
            juce::NormalisableRange<float> (-60.0f, 0.0f, 0.01f), -60.0f,
            dbAttrs ("dB").withStringFromValueFunction (
                [] (float v, int) { return dbText (v); })));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { trackParamId (t, track::sendReverb), 1 },
            "Track " + juce::String (t + 1) + " Reverb Send",
            juce::NormalisableRange<float> (-60.0f, 0.0f, 0.01f), -60.0f,
            dbAttrs ("dB").withStringFromValueFunction (
                [] (float v, int) { return dbText (v); })));
    }

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pid::out, 1 }, "Output",
        juce::NormalisableRange<float> (-60.0f, 6.0f, 0.01f), 0.0f,
        dbAttrs ("dB").withStringFromValueFunction (
            [] (float v, int) { return dbText (v); })));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { pid::limiterOn, 1 }, "Output Limiter", false));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { global::speed, 1 }, "Varispeed",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes {}.withLabel ("st").withStringFromValueFunction (
            [] (float v, int) {
                const float st = std::abs (v) < 0.05f ? 0.0f : v;
                return (st >= 0.0f ? "+" : "") + juce::String (st, 1)
                       + " st (" + juce::String (std::pow (2.0f, st / 12.0f), 2) + "x)";
            })));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { global::wow, 1 }, "Wow Depth",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.30f, pctAttrs()));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { global::wowRate, 1 }, "Wow Rate",
        juce::NormalisableRange<float> (0.25f, 4.0f, 0.01f, 0.5f), 1.0f,
        juce::AudioParameterFloatAttributes {}.withLabel ("x").withStringFromValueFunction (
            [] (float v, int) { return juce::String (v, 2) + "x"; })));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { global::flutter, 1 }, "Flutter Depth",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.25f, pctAttrs()));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { global::flutterRate, 1 }, "Flutter Rate",
        juce::NormalisableRange<float> (0.25f, 4.0f, 0.01f, 0.5f), 1.0f,
        juce::AudioParameterFloatAttributes {}.withLabel ("x").withStringFromValueFunction (
            [] (float v, int) { return juce::String (v, 2) + "x"; })));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { global::hiss, 1 }, "Hiss",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.25f, pctAttrs()));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { global::dlyTime, 1 }, "Delay Time",
        juce::NormalisableRange<float> (10.0f, 2000.0f, 1.0f, 0.4f), 250.0f,
        juce::AudioParameterFloatAttributes {}.withLabel ("ms").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; })));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { global::dlyFb, 1 }, "Delay Feedback",
        juce::NormalisableRange<float> (0.0f, 0.95f, 0.001f), 0.45f, pctAttrs()));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { global::dlyTone, 1 }, "Delay Tone",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, pctAttrs()));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { global::dlyRet, 1 }, "Delay Return",
        juce::NormalisableRange<float> (-60.0f, 6.0f, 0.01f), 0.0f,
        dbAttrs ("dB").withStringFromValueFunction (
            [] (float v, int) { return dbText (v); })));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { global::revSize, 1 }, "Reverb Size",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.55f, pctAttrs()));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { global::revDamp, 1 }, "Reverb Damp",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, pctAttrs()));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { global::revRet, 1 }, "Reverb Return",
        juce::NormalisableRange<float> (-60.0f, 6.0f, 0.01f), 0.0f,
        dbAttrs ("dB").withStringFromValueFunction (
            [] (float v, int) { return dbText (v); })));

    return { params.begin(), params.end() };
}

} // namespace tape
