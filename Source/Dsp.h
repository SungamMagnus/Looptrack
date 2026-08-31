#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tape
{

constexpr double kPi = 3.14159265358979323846;

/** xorshift32 white noise, -1..1. Cheap, no audible period at audio sample
    rates, and each instance is independent given a distinct seed -- used to
    decorrelate the L/R hiss generators. */
struct Rng
{
    explicit Rng (uint32_t seed) : state (seed) {}

    float nextBipolar()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (int32_t) state / 2147483648.0f;
    }

    uint32_t state;
};

/** One-pole low/high-pass. The two share state deliberately: highpass(x) is
    x - lowpass(x) through the same running average, which is the standard
    one-pole HP derivation and means one instance can serve as either. */
struct OnePole
{
    void setCutoff (double hz, double sampleRate)
    {
        coeff = 1.0f - std::exp ((float) (-2.0 * kPi * hz / sampleRate));
    }

    float lowpass (float x)
    {
        state += coeff * (x - state);
        return state;
    }

    float highpass (float x) { return x - lowpass (x); }

    float state = 0.0f;
    float coeff = 0.5f;
};

/** 4-point cubic Hermite interpolation, reading `data[0..len-1]` at a
    fractional position. Positions outside the buffer clamp to the nearest
    edge sample rather than wrapping or reading garbage -- used both for the
    loop playback head and, later, the wow/flutter delay line. */
inline float cubicHermite (const float* data, int len, double pos)
{
    if (len <= 0)
        return 0.0f;

    const int i1 = (int) std::floor (pos);
    const double frac = pos - (double) i1;

    auto at = [data, len] (int i) -> float
    {
        return data[std::clamp (i, 0, len - 1)];
    };

    const float y0 = at (i1 - 1);
    const float y1 = at (i1);
    const float y2 = at (i1 + 1);
    const float y3 = at (i1 + 2);

    const float c0 = y1;
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

    const float f = (float) frac;
    return ((c3 * f + c2) * f + c1) * f + c0;
}

/** RBJ cookbook biquad: shelves for the low/high EQ bands, a bell for mid.
    Transposed Direct Form II. */
struct Biquad
{
    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x + z2 - a1 * y;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void lowShelf (double freq, double q, double gainDb, double sampleRate)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * kPi * freq / sampleRate;
        const double cosw0 = std::cos (w0), sinw0 = std::sin (w0);
        const double alpha = sinw0 / (2.0 * q);
        const double twoSqrtAalpha = 2.0 * std::sqrt (A) * alpha;

        setCoeffs (A * ((A + 1) - (A - 1) * cosw0 + twoSqrtAalpha),
                   2 * A * ((A - 1) - (A + 1) * cosw0),
                   A * ((A + 1) - (A - 1) * cosw0 - twoSqrtAalpha),
                   (A + 1) + (A - 1) * cosw0 + twoSqrtAalpha,
                   -2 * ((A - 1) + (A + 1) * cosw0),
                   (A + 1) + (A - 1) * cosw0 - twoSqrtAalpha);
    }

    void highShelf (double freq, double q, double gainDb, double sampleRate)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * kPi * freq / sampleRate;
        const double cosw0 = std::cos (w0), sinw0 = std::sin (w0);
        const double alpha = sinw0 / (2.0 * q);
        const double twoSqrtAalpha = 2.0 * std::sqrt (A) * alpha;

        setCoeffs (A * ((A + 1) + (A - 1) * cosw0 + twoSqrtAalpha),
                   -2 * A * ((A - 1) + (A + 1) * cosw0),
                   A * ((A + 1) + (A - 1) * cosw0 - twoSqrtAalpha),
                   (A + 1) - (A - 1) * cosw0 + twoSqrtAalpha,
                   2 * ((A - 1) - (A + 1) * cosw0),
                   (A + 1) - (A - 1) * cosw0 - twoSqrtAalpha);
    }

    void peak (double freq, double q, double gainDb, double sampleRate)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * kPi * freq / sampleRate;
        const double cosw0 = std::cos (w0), sinw0 = std::sin (w0);
        const double alpha = sinw0 / (2.0 * q);

        setCoeffs (1 + alpha * A, -2 * cosw0, 1 - alpha * A,
                   1 + alpha / A, -2 * cosw0, 1 - alpha / A);
    }

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

private:
    void setCoeffs (double b0_, double b1_, double b2_, double a0_, double a1_, double a2_)
    {
        b0 = (float) (b0_ / a0_);
        b1 = (float) (b1_ / a0_);
        b2 = (float) (b2_ / a0_);
        a1 = (float) (a1_ / a0_);
        a2 = (float) (a2_ / a0_);
    }
};

/** TPT (topology-preserving transform) state-variable filter -- stays stable
    under per-sample cutoff modulation, unlike a direct-form biquad, which is
    what the DJ filter needs since its cutoff sweeps continuously. */
struct Svf
{
    void setCutoff (double fc, double q, double sampleRate)
    {
        const double g = std::tan (kPi * fc / sampleRate);
        const double k = 1.0 / q;
        a1 = (float) (1.0 / (1.0 + g * (g + k)));
        a2 = (float) (g * a1);
        a3 = (float) (g * a2);
        kVal = (float) k;
    }

    void process (float x, float& lowpassOut, float& highpassOut)
    {
        const float v3 = x - ic2eq;
        const float v1 = a1 * ic1eq + a2 * v3;
        const float v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;
        lowpassOut = v2;
        highpassOut = x - kVal * v1 - v2;
    }

    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, kVal = 1.0f;
    float ic1eq = 0.0f, ic2eq = 0.0f;
};

/** Same interpolation, but for a circular buffer of `capacity` samples --
    used by delay-line style reads (wow/flutter, later the lofi delay) where
    the read position wraps instead of clamping at an edge. */
inline float cubicHermiteCircular (const float* data, int capacity, double pos)
{
    auto wrap = [capacity] (int i)
    {
        i %= capacity;
        return i < 0 ? i + capacity : i;
    };

    const int i1 = (int) std::floor (pos);
    const double frac = pos - (double) i1;

    const float y0 = data[wrap (i1 - 1)];
    const float y1 = data[wrap (i1)];
    const float y2 = data[wrap (i1 + 1)];
    const float y3 = data[wrap (i1 + 2)];

    const float c0 = y1;
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

    const float f = (float) frac;
    return ((c3 * f + c2) * f + c1) * f + c0;
}

} // namespace tape
