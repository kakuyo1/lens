// The constants both halves of the glass blur are baked from (docs/adr/0082, docs/adr/0111). Included by glass.frag and
// glass_hblur.frag so the slider's full scale, the tap spacing and the prefilter depth cannot drift apart.

/// The blur at 100%, in DIP: the prototype's largest blur.
const float kBlurAt100 = 60.0;
/// A tap spacing of this many sigma leaves the nine taps' second moment at one sigma.
const float kSpacingSigma = 0.95;
/// How much coarser the prefilter mip is than the tap spacing. A box of exactly one tap's width has a stopband that
/// folds fine detail back as a visible weave; three quarters of a level more flattens it and lands the same slider a
/// little softer than the old single pass (docs/adr/0082).
const float kLodBias = 0.75;

/// How much coarser the Smooth tier's prefilter mip is, in levels below the tap-spacing mip (docs/adr/0111). Its
/// seventeen taps sit at half the mip texel, so each tent spans twice the spacing and the kernel follows the Gaussian
/// arc between taps. The nine standard taps sit one texel apart under tents that merely touch, so the kernel there is
/// the straight chord between neighbouring weights -- the lump the strong edges of a text page draw out as stripes
/// along the blur axis, vertical from the horizontal pass and horizontal from the vertical one.
const float kDenseLodBias = 1.25;
/// The Smooth tier's tap spacing, in sigma: half the mip texel kDenseLodBias reads.
const float kDenseSpacing = kSpacingSigma * exp2(kDenseLodBias) * 0.5;

/// One tap of the Smooth tier's seventeen: a Gaussian at kDenseSpacing sigma, normalized by its integral. The train is
/// dense enough that the discrete weight sum equals the integral, so the weights need no runtime sum; they are baked
/// from kDenseSpacing, so the spacing and the weights cannot drift apart.
float denseWeight(int k)
{
    float a = float(k);
    return exp(-0.5 * kDenseSpacing * kDenseSpacing * a * a) * kDenseSpacing * 0.39894228;
}
