// The constants both halves of the glass blur are baked from (docs/adr/0082, docs/adr/0111, docs/adr/0201). Included by
// glass.frag and glass_hblur.frag so the slider's full scale, the prefilter depth and the tap train cannot drift apart.

/// The blur at 100%, in DIP: the prototype's largest blur.
const float kBlurAt100 = 60.0;
/// The mip texel the taps read, in sigma. The prefilter is the mip chain itself: a box about this wide, which stops the
/// desktop's fine detail before it can fold back into the blur as a weave (docs/adr/0082). It also sets the softness:
/// with the train below, one pass lands at about 1.3 sigma of effective blur, close to what the old nine-tap chain
/// delivered on the Smooth tier (docs/adr/0201).
const float kPrefilterSigma = 2.26;
/// One tap's step, in mip texels. The train spans three texels of a prefilter one texel wide, so neighbouring bilinear
/// tents overlap four to one instead of merely touching: the kernel between taps is a curve, not the straight chord
/// that used to draw the tap grid out along the blur axis as stripes (docs/adr/0111, docs/adr/0201).
const float kTapStepTexels = 0.25;

/// One tap of the train: a Gaussian sampled at half its sigma, normalized so the thirteen weights sum to one. The
/// sampling is dense enough that its own grid carries nothing at the frequencies a screen shows, at every slider
/// position (docs/adr/0201).
float tapWeight(int k)
{
    float a = float(k);
    return exp(-0.125 * a * a) * 0.1996756;
}
