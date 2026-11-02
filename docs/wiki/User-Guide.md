# User Guide

- **Install:** use the release assets described in the README "Download" section (Windows installer, portable zip, or VST3/CLAP-only zips; Linux zips). The release appears only after the automated build and validation pass; until then, treat Actions artifacts as beta candidates.
- **Use:** insert on a track. GapFill listens to the input or sidechain, finds gaps, and generates fill material only there. The original hits are protected.
- **Surprise:** the top-bar SURPRISE button selects one of eight curated profiles.
- **Help:** the in-app Help view reads `HelpRegistry`; core sections live in `Source/gui/HelpSections.cpp`, and feature modules register their own sections. `docs/HELP.md` is a separate documentation reference.
- **Troubleshooting:** `docs/TROUBLESHOOTING.md`.
- **Network:** see README "Privacy and network behavior".
