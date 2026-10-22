// The constants both halves of the glass blur are baked from (docs/adr/0082). Included by glass.frag and
// glass_hblur.frag so the slider's full scale, the tap spacing and the prefilter depth cannot drift apart.

/// The blur at 100%, in DIP: the prototype's largest blur.
const float kBlurAt100 = 60.0;
/// A tap spacing of this many sigma leaves the nine taps' second moment at one sigma.
const float kSpacingSigma = 0.95;
/// How much coarser the prefilter mip is than the tap spacing. A box of exactly one tap's width has a stopband that
/// folds fine detail back as a visible weave; three quarters of a level more flattens it and lands the same slider a
/// little softer than the old single pass (docs/adr/0082).
const float kLodBias = 0.75;
