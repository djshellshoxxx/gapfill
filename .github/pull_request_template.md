## Ticket
<!-- e.g. E-1 Fill Audition - link the spec section in docs/dev/ -->

## What changed
<!-- Short summary. One ticket per PR. -->

## Neutral at default?
- [ ] Golden scenarios unchanged (or this PR is titled `[golden]` and explains which feature intentionally changed the sound)
- [ ] Stage identity / RNG-untouched test added where applicable

## Tests added
<!-- Names, matching the ticket's test list. Paste local `tools/dev/check.sh` result. -->

## Hot files touched
<!-- Expected: none. If any, label `hot-file-ok` and justify. -->

## Self-review checklist (docs/dev/06-ENGINEERING-STANDARDS.md section 9)
- [ ] No new RNG draws on the legacy stream; sub-RNG salt registered
- [ ] Audio thread: no alloc/lock/IO (AllocGuard test where relevant)
- [ ] Resets handled: reset(), releaseResources, sample-rate change, bypass, transport jump
- [ ] 192 kHz and 32-sample block considered
- [ ] State: versioned/validated; old-state fixture loads; corrupt data fails soft
- [ ] New param IDs appended in their group + lock file; classified for Random/Undo
- [ ] UI: tooltip, accessibility name, keyboard path, Reduced Motion, min-size, no colour-only cue
- [ ] Help section + ParamInfo entries; COMPLIANCE/README updated if needed
- [ ] Zero warnings on new files; CI green on Linux and Windows

## Not verified (be honest)
<!-- Real DAW, hardware MIDI, macOS, CI unavailable, etc. List what was NOT run. -->
