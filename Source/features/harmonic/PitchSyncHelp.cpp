#include "../../gui/HelpRegistry.h"
namespace gapfill::harmonic
{
namespace
{
    const HelpRegistrar pitchSyncHelp (1700, "Harmonic grains", [] (HelpWriter& help)
    {
        help.body ("PITCH SYNC in Advanced view opens an optional pitch-synchronous Granular engine. Mode Off preserves the original Harmonic Amount, Voices, Motion and Tone controls. "
                   "Mode Harmonic replaces that path for new Granular fills. Values are sampled at fill start; other fill algorithms are unchanged.");
        help.entry ("Intervals / Voices", "Choose Octaves, Open fifths, Major, Minor, Suspended or Scale, and one to four layers. "
                    "Scale uses notes of the current known key; without a known key it uses Open fifths. Key Snap moves the pitches toward the key continuously.");
        help.entry ("Motion / Scatter / Tone", "Motion moves slowly through source epochs and adds subtle pitch variation. Scatter adds repeatable variation to source positions and onset timing. "
                    "Tone darkens or brightens the layers. The recorded recipe seed preserves variation on replay.");
        help.body ("A clearly pitched 50-1000 Hz source is required. The detector warms from the actual source material; noise, drums, too-short captures or irregular pitch marks use plain Granular. "
                   "The readout distinguishes a pitched fill from these fallbacks. Mono source stays mono even when the wet output has two channels.");
        help.body ("Capture keeps the pitch of the held audio. Recapture and Scan use the same selected source window for both pitch and sound. "
                   "If Pitch Sync is first enabled after Capture is already held, the old unseen source has no pitch frames: use Recapture after live analysis has warmed. "
                   "Global FX Send blends this engine with plain Granular; zero skips pitch analysis at fill start. Saved recipes retain their recorded engine decision, "
                   "pitch, interval set and voice count. As with other recipes, source audio, Motion, Scatter, Tone, Key Snap, key and FX Send use the current settings.");
    });
}
}
