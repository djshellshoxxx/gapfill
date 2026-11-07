#include "../../gui/HelpRegistry.h"

namespace gapfill::glitch
{
namespace
{
    // F-G01: the GLITCH tab. Placed after the routing/system sections (64xx).
    const HelpRegistrar glitchHelp (6500, "GLITCH tab", [] (HelpWriter& writer)
    {
        writer.body ("The GLITCH tab is a rack of rhythmic effects: Stutter, STAB, Reverse, Microgator, Sweep filter, Phaser "
                     "and Phase/pan flip, always in that order. Switch the rack on with GLITCH ON. Every module is off by default, "
                     "and while the rack is off the sound is exactly what it was before. Target chooses what the rack processes: "
                     "Fills only (the generated material, before the wet safety limiter) or Whole output (the final mix). "
                     "Mix blends the rack against its input. No latency is added.");
        writer.body ("Timing: tempo-locked modules follow the host bar and beat while the transport runs; when it is stopped they "
                     "keep running at the last tempo, and they re-align after a seek or loop. Swing delays every second step of each "
                     "module's grid, up to 75 %. Timing variation pushes and pulls each step start by up to 10 % of a step; it is "
                     "repeatable, so a loop plays the same way every time and never drifts.");
        writer.body ("Stutter cuts the sound into slices. In each window of Repeats slices it may (Probability) capture the first "
                     "slice and repeat it. Reverse plays some repeats backwards, Variation repeats half or quarter slices, Slice pick "
                     "repeats an older slice, Gate shortens each repeat and Decay lowers each one. Smoothing crossfades every boundary.");
        writer.body ("STAB grabs the sound the moment you press it and repeats it. With Infinite off, one press plays Repeats repeats "
                     "and stops, however long you hold. With Infinite on, it repeats until you release STAB (or switch Infinite off). "
                     "A new press starts a fresh grab. Release fades back to the live sound. Memory use is fixed however long it runs; "
                     "the slice length is taken at the press, so tempo changes and transport stops do not disturb it.");
        writer.body ("Reverse plays each segment backwards during the next one, so reversed audio always arrives one segment late "
                     "(like a reverse delay). Probability leaves some segments forwards; Fade softens the segment edges.");
        writer.body ("Microgator chops the level on a fast grid. Length is how long each step stays open, Depth how far it closes, "
                     "Attack and Release shape each opening, Pattern picks the steps, Variation flips some, and Stereo offsets the "
                     "right side for ping-pong gating.");
        writer.body ("Sweep filter moves a low-, high- or band-pass cutoff between Minimum and Maximum on a tempo cycle or a free "
                     "rate. Ramp shapes sweep one way; Square and Random glide over 5 ms so the cutoff never jumps. Phaser moves "
                     "all-pass notches around Centre; Feedback sharpens them and is safety-limited.");
        writer.body ("Phase/pan: polarity inversion turns a channel upside down. It is not a time delay and it is not a pan. "
                     "Inverting only one side cancels when the mix is summed to mono; inverting both is mono-safe. Pan inversion "
                     "mirrors the stereo image (100 % swaps left and right). Rhythmic flip swaps or inverts on chosen grid steps, "
                     "with a 5 ms glide so it never clicks.");
    });
}
} // namespace gapfill::glitch
