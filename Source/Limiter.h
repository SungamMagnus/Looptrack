#pragma once

namespace tape
{

/** A very simple output safety limiter: a fixed ceiling just under 0dBFS,
    instant attack (no lookahead -- a transient this fast is inaudible as a
    click, not worth the latency a real lookahead buffer would cost), and a
    smoothed release so the gain reduction doesn't pump audibly. No exposed
    threshold/release -- it's a safety net, not a mix tool. */
class Limiter
{
public:
    void prepare (double sampleRate);

    /** l/r processed in place. Returns true if gain reduction was applied
        anywhere in this block, so the panel can light an indicator. */
    bool process (float* l, float* r, int numSamples);

private:
    float envelope = 1.0f;
    float releaseCoeff = 0.999f;
};

} // namespace tape
