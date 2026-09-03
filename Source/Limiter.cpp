#include "Limiter.h"

#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>

namespace tape
{

namespace
{
    constexpr float kCeiling = 0.97f; // ~-0.26 dBFS, a hair of headroom below true 0
    constexpr double kReleaseSeconds = 0.15;
}

void Limiter::prepare (double sampleRate)
{
    releaseCoeff = (float) std::exp (-1.0 / (sampleRate * kReleaseSeconds));
    envelope = 1.0f;
}

bool Limiter::process (float* l, float* r, int numSamples)
{
    bool limiting = false;

    for (int i = 0; i < numSamples; ++i)
    {
        const float peak = juce::jmax (std::abs (l[i]), std::abs (r[i]));
        const float target = peak > kCeiling ? kCeiling / peak : 1.0f;

        // instant attack -- never let a peak through uncaught; smoothed
        // release so recovering from a squash doesn't audibly pump
        envelope = target < envelope ? target : target + (envelope - target) * releaseCoeff;

        l[i] *= envelope;
        r[i] *= envelope;

        if (envelope < 0.999f)
            limiting = true;
    }

    return limiting;
}

} // namespace tape
