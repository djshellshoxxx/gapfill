// C-3 help (order 5180, WP5 band 5100-5199).

#include "../../gui/HelpRegistry.h"

namespace gapfill
{

namespace
{
    const HelpRegistrar helpMistakes (5180, "Controlled mistakes", [] (HelpWriter& w)
    {
        w.body ("Mistakes adds the small flaws of a human player. At 0 % nothing happens. At 100 % about one fill in "
                "five carries exactly one mistake, chosen from the ones that suit the fill.");
        w.entry ("Wobble", "a gentle tape-style vibrato of at most half a semitone.");
        w.entry ("Octave displacement", "the whole fill plays an octave up or down.");
        w.entry ("Missed repeat", "one stutter slice, delay tap or scramble slice drops out.");
        w.entry ("Brake", "the end of the fill slows down and fades like a lifted record.");
        w.entry ("Stereo bounce", "two or three quick swings across the stereo field (not on mono tracks).");
        w.body ("A mistake never makes a fill longer, never reaches past the gap and never clicks. The same seed and "
                "song position always give the same mistake.");
    });
}

} // namespace gapfill
