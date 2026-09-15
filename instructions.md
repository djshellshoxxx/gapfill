 You are the lead DSP engineer, JUCE/C++ developer, plugin architect, UI engineer, and QA engineer responsible for building a production-quality audio plugin called GapFill.
Your objective is not to make a prototype or mock-up. Build a complete, usable VST3 effect that can be loaded into a real DAW and used for music production.
Work autonomously. Inspect the repository and development environment before making assumptions. If this is an empty project, create the project properly. If code already exists, understand it before changing it. Do not repeatedly ask me questions about implementation details; make technically sound decisions consistent with this specification.
Use the tools available to you aggressively: inspect files, build the plugin, run tests, diagnose compiler errors, and iterate until the project genuinely works.
Do not claim something works merely because the code looks correct. Build it and test it. 
 Create “GapFill”, a creative audio effect whose central concept is:
DETECT THE MUSICAL EVENTS IN THE INPUT AUDIO, IDENTIFY THE USABLE SPACES BETWEEN THOSE EVENTS, AND CREATE MUSICAL EAR-CANDY INSIDE THOSE SPACES WHILE PRESERVING THE IMPORTANT ORIGINAL HITS.
Examples:
Input: KICK ——– SNARE ——– KICK ——– SNARE
GapFill: KICK [granular reverse] SNARE [stutter] KICK [pitching delay] SNARE
GapFill is intentionally different from a normal delay, glitcher, gate, transient shaper, reverb, or random multi-FX.
Its defining intelligence is that effects are generated specifically in appropriate spaces between meaningful events instead of continuously processing the entire signal.
The plugin should be immediately useful on:
drum loops
drum buses
percussion
vocals
bass lines
synths
melodic loops
sampled material
full stems
FX buses
Whole-master processing should also work as a special creative use case, but the main design target is inserts, tracks, buses, and stems.
GapFill should be particularly effective for genres such as:
drum & bass
breakcore
hard trance
techno
industrial
IDM
glitch
trip-hop
dubstep
experimental electronic music
It should also work subtly enough for pop, hip-hop, house, and general electronic production. 
 Build this in modern C++ using JUCE.
Primary target:
Windows x64 VST3
Use CMake if creating a new project unless the existing repository already has a clean JUCE build system that should be retained.
Do not add unnecessary dependencies when JUCE already provides suitable functionality.
Use modern, maintainable C++.
The plugin must function correctly at common sample rates including:
44.1 kHz 48 kHz 88.2 kHz 96 kHz 176.4 kHz 192 kHz
It should tolerate common host block sizes from approximately 32 through 2048 samples and must not assume a fixed block size.
Support mono and stereo main input when practical, with stereo being the primary target.
Support a stereo sidechain input where VST3 host routing permits it.
The audio processor must remain valid when no sidechain is connected. 
 Conceptually use this architecture:
MAIN INPUT | +————————————-> DRY PATH | +–> ANALYSIS | +–> Event Detector +–> Envelope Analysis +–> Transient Detection +–> Spectral Activity Analysis +–> Gap Detector | +–> Fill Scheduler | +–> Source Capture Buffer +–> Fill Generator +–> Effect Chain +–> Protection/Masking | +———————–> WET PATH
DRY + WET | OUTPUT
When sidechain detection is selected:
SIDECHAIN | ANALYSIS / EVENT DETECTION
MAIN INPUT | SOURCE CAPTURE / DRY AUDIO / MATERIAL USED TO CREATE FILLS
This distinction is important.
The sidechain tells GapFill WHEN space is available.
The main input provides WHAT AUDIO is manipulated to create the fill. 
 GapFill must understand a sequence of musical activity approximately like:
EVENT -> DECAY -> POSSIBLE GAP -> NEXT EVENT
It must not simply consider everything under a fixed dB threshold to be silence.
Implement an adaptive event/gap detection system using multiple useful measurements such as:
short-term envelope
RMS or similar energy measurement
fast transient envelope
attack slope
relative energy changes
adaptive local noise/energy floor
spectral activity when applicable
spectral flux or a similarly useful spectral-change measurement where appropriate
The exact DSP architecture is your engineering decision, but it should behave musically rather than act as a trivial noise gate.
A gap should become eligible only when GapFill has sufficient confidence that:
an event has occurred,
the meaningful body/attack of that event has passed,
the current region has enough space to accommodate a fill,
placing a fill will not unnecessarily obscure the next important event.
Do not require literal digital silence.
A quiet region relative to surrounding material may qualify.
A fill may begin after a configurable delay following the protected transient/body.
If another important event arrives, the fill should quickly and smoothly get out of its way. 
 Implement optional lookahead because knowing where an upcoming event begins substantially improves GapFill.
Provide a Lookahead parameter approximately:
0–250 ms
Default approximately:
40 ms
When lookahead is active:
buffer the main signal appropriately,
analyze future audio,
schedule fills more intelligently,
terminate/fade fills before important upcoming events,
report the actual plugin latency correctly to the DAW using JUCE’s latency reporting.
Changing lookahead must not cause unmanaged buffer access, crashes, or corrupted audio.
If changing the latency dynamically requires host notification, handle it correctly.
Zero-lookahead operation should remain possible for lower-latency situations. 
 Provide three primary detector modes.
MODE 1: TRANSIENT
Designed primarily for:
drums percussion plucked sounds rhythmic bass rhythmic loops
Prioritize attacks and transient boundaries.
MODE 2: PHRASE
Designed primarily for:
vocals melodies synth phrases guitar longer musical material
Be less sensitive to tiny attacks and instead identify phrase-level activity and meaningful pauses.
MODE 3: SPECTRAL
Analyze independent frequency regions so GapFill can identify space in one part of the spectrum even while another part remains active.
A useful first-release implementation is three configurable bands:
LOW MID HIGH
Use proper crossover filters rather than destructive crude filtering.
Allow fills to target available spectral space.
Example:
A kick may make the low band busy while the high band is relatively empty.
GapFill may place a high-frequency glitch or texture in the high band without interfering with the kick’s low-frequency energy.
Provide Solo/Audition functionality for detection/bands where practical so users can understand what the detector sees. 
 Provide useful controls including:
Sensitivity Controls how easily meaningful events are detected.
Range suggestion: 0–100%
Default: 50%
Gap Threshold Determines how far energy must fall relative to the adaptive reference before a region is considered potential space.
Expose this in a musician-friendly form rather than requiring users to understand the internal algorithm.
Minimum Gap Approximately 10–500 ms.
Default approximately 60 ms.
Do not generate fills when the usable region is shorter than this value.
Maximum Fill Length Approximately 20 ms–2 seconds, with optional tempo-synced alternatives.
Default approximately 400 ms.
Release / Gap Confirmation Controls how quickly the plugin decides that an event has actually ended.
Protection Hold Provides a configurable safe region immediately after detected transients.
Retrigger Guard Prevents a single transient from being detected repeatedly.
Lookahead 0–250 ms.
Detector Source:
MAIN SIDECHAIN HYBRID
MAIN analyzes the processed track.
SIDECHAIN uses the sidechain solely for event/gap timing.
HYBRID considers both, intelligently protecting important main-track events even when the sidechain defines the rhythmic spaces. 
 Internally calculate a continuously varying gap confidence rather than relying entirely on a binary gate.
For example:
0.0 = definitely busy 1.0 = highly usable space
Use this confidence to influence:
whether a fill occurs,
fill amplitude,
allowed complexity,
fill duration,
masking against surrounding material.
Display gap confidence visually in the GUI.
Do not expose raw implementation details unless useful. 
 The Fill Engine is the creative heart of the plugin.
Fills should usually derive from recently captured input audio rather than sounding like unrelated canned samples.
Maintain a safe circular source-history buffer containing recent main-input audio.
When a gap is selected, GapFill can extract appropriate pieces of recent audio and transform them.
The plugin should be capable of the following core fill types. 
 AUTO
GapFill selects from enabled fill algorithms based on available gap length, Complexity, Intensity, spectral room, probability, and musical context.
REVERSE
Capture a recent fragment and play it backward into the available gap.
Controls should allow useful source sizes and fades.
Avoid clicks at slice boundaries.
STUTTER
Repeat a short captured piece rhythmically.
Support musically useful subdivisions.
Examples:
1/4 1/8 1/16 1/32 triplet variants where appropriate
Allow accelerating or decelerating repeat patterns when Complexity is sufficiently high.
DELAY
Generate short echo patterns specifically inside the detected gap.
Allow normal and ping-pong stereo behavior.
Terminate/fade echoes before protected material when required.
GRANULAR
Create a short granular cloud using recent input.
Parameters internally influenced by macros can include:
grain size density position variation pitch variation stereo spread reverse probability
Do not create uncontrolled CPU usage.
FREEZE
Capture a short region and temporarily freeze/sustain it in the gap.
Use proper crossfading/windowing.
PITCH RISE
Create a fragment whose pitch rises through the gap.
PITCH FALL
Create a fragment whose pitch falls through the gap.
FILTER SWEEP
Animate low-pass, high-pass, or band-pass filtering across a captured fill.
NOISE/TEXTURE
Generate a short texture or noise fill shaped by the analyzed source envelope/spectrum so it feels related to the track rather than like generic white noise pasted on top.
BIT/CRUSH
Create controlled digital degradation of a captured fragment.
DISTORT
Apply saturation/distortion to a fill while keeping the protected original event clean.
BUFFER SCRAMBLE
Rearrange very small regions of captured source material.
Keep timing intentionally musical rather than completely random.
SPECTRAL SMEAR
Provide an FFT-based smear/freeze-style transformation appropriate for creating ghostly transitional material.
Ensure overlap-add/window handling is correct.
Do not allow spectral processing artifacts such as uncontrolled level buildup or invalid output.
COMB/RESONANCE
Optionally allow short resonant/comb-like fills at musical intensities.
This should be a creative fill effect and not a permanently ringing feedback network. 
 AUTO should not behave as completely uncontrolled randomization.
Build a weighted, context-aware fill selector.
Base decisions on information such as:
available gap duration
input energy
transient strength
detector mode
tempo
Complexity
Intensity
user-enabled algorithms
frequency availability
recent algorithm history
Avoid selecting the exact same fill repeatedly unless repetition is musically appropriate.
Avoid selecting an algorithm that cannot fit inside the available gap.
Examples:
Very short gap: micro-stutter tiny reverse short filtered click/grain
Medium gap: reverse delay pitch fragment granular fill
Long gap: granular cloud freeze evolving delay spectral smear
Use weighted randomness with deterministic behavior tied to the project timeline/seed where possible so playback is reproducible rather than changing unpredictably every time the user presses Play.
Provide a user Seed control and a Randomize button. 
 Provide several prominent musical macro controls.
FILL
0–100%
This is the primary amount/probability control.
At approximately:
0% = no generated fills
10–25% = occasional subtle ear candy
25–50% = clearly noticeable fills while preserving the original groove
50–75% = active transformation
75–100% = aggressively fill most eligible spaces
Do not make Fill merely equivalent to wet/dry.
It should affect the probability and/or degree to which eligible gaps are filled.
INTENSITY
0–100%
Controls how strongly individual selected fills are processed.
Low: subtle echoes/reverses/filtering.
High: large pitch movement, greater distortion, stronger glitching, wider movement, more dramatic modulation.
COMPLEXITY
0–100%
Controls how sophisticated fills are allowed to become.
Low: single simple transformation.
Examples: reverse delay simple filter.
Medium: one or two combined transformations.
High: multiple carefully chosen transformations, mutation, granular/spectral processing, rhythmic evolution.
Complexity must not merely increase loudness.
CHAOS
0–100%
Controls variation and unpredictability.
0: stable/repeatable behavior.
100: larger variation in algorithm selection, timing, pitch, grain behavior, stereo movement, etc.
Even at 100%, remain musically constrained and bounded.
DENSITY
Controls how many eligible spaces actually receive fills independently of overall wet level.
WET
0–100%
Traditional generated-effect wet amount.
DRY
0–100%
Dry-signal level.
OUTPUT
Useful output trim. 
 Obtain tempo, time signature, PPQ/project position, transport state, and other valid host timing information using JUCE host-position APIs.
Do not assume host timing information always exists.
Provide sensible fallback behavior.
Tempo-sync should be available for relevant controls.
Useful divisions include:
1/1 1/2 1/4 1/8 1/16 1/32
and dotted/triplet values where useful.
Use host timing to make fills feel musically connected to the project.
Do not quantize everything so rigidly that live/organic material stops working. 
 GapFill must have a strong protection system because preserving important material is one of its main selling points.
Implement the following.
TRANSIENT PROTECT
0–100%
At high values, strong detected attacks should remain substantially unaffected by generated fills.
GapFill should delay, duck, fade, or truncate fills to avoid masking them.
LOW PROTECT
Provide a configurable high-pass boundary for generated material.
Example range:
Off 20 Hz–500 Hz
A common use case is protecting kick and sub energy below roughly 80–150 Hz.
CENTER PROTECT
0–100%
Allows generated fills to be pushed progressively away from the stereo center, protecting centered kick, snare, bass, and vocal information.
At higher settings, wet fills can favor the sides while the dry signal remains untouched.
NEXT EVENT PROTECT
0–100%
Uses lookahead/detection to fade or duck the fill as the next important event approaches.
This should be extremely smooth and click-free.
SIDECHAIN PROTECT
When sidechain detection is active, strong sidechain events must be able to force generated material out of the way.
Think of this as intelligent event-aware masking rather than conventional static sidechain compression. 
 Spectral mode should expose LOW, MID, and HIGH processing lanes.
Use configurable crossover points with sensible defaults, for example approximately:
Low/Mid: 180 Hz Mid/High: 3.5 kHz
The exact allowed ranges should prevent invalid crossover configurations.
Each lane should support:
Enable Fill Amount Intensity Probability Solo/Audition
Gap detection should occur per band sufficiently independently that the plugin can identify:
LOW BUSY / MID EMPTY / HIGH EMPTY
or any other combination.
Where practical, generated fills should be constrained to the available region instead of being broadband.
Avoid major crossover phase or reconstruction problems.
Use proper JUCE DSP crossover/filtering techniques. 
 The sidechain is a major feature, not an afterthought.
Example 1:
GapFill inserted on bass. Drum bus routed into GapFill sidechain.
Result: The drum pattern defines available timing windows. GapFill creates manipulated bass fragments between drum events.
Example 2:
GapFill inserted on vocal. Drums routed to sidechain.
Result: Vocal echoes, grains, reverses, and glitches appear rhythmically around drum activity.
Example 3:
GapFill inserted on synth. Kick routed to sidechain.
Result: Generated synth material stays out of the kick while appearing between kick events.
Example 4:
GapFill on percussion. Main detector.
Result: The plugin automatically adds fills between percussion hits without needing another track.
Make these workflows function naturally. 
 Every generated fill must use click-free envelopes.
Provide short adaptive fade-in/fade-out behavior.
If an upcoming protected event suddenly requires a fill to stop, apply an appropriate rapid smooth fade rather than abruptly clearing a buffer.
Prevent DC offsets.
Prevent runaway feedback.
Prevent denormals.
Do not output NaN or infinity under any valid operating condition. 
 Provide useful stereo processing.
Generated fills may use:
stereo spread ping-pong micro-offset grain panning mid/side positioning
However:
maintain mono compatibility where practical,
do not arbitrarily alter the dry stereo image,
protect the center according to Center Protect,
do not generate uncontrolled antiphase output.
Include optional MONO wet mode if straightforward. 
 Add intelligent randomization.
Provide:
Seed Randomize Fill Randomize All
Randomize Fill should modify creative fill behavior without destroying detector settings.
Randomize All may randomize a broader set of safe musical parameters.
Never randomize:
output into dangerous level ranges latency into extreme unexpected states parameters into invalid combinations
Seeded behavior should be reproducible. 
 Implement a real preset system appropriate to the architecture.
Factory presets should demonstrate the concept immediately.
Include useful examples conceptually similar to:
Subtle Ear Candy Drum Gap Glitch DnB Micro Fills Breakcore Destroyer Ghost Reverses Industrial Debris Vocal Ghosts Vocal Delay Fills Bass Between Kicks Granular Spaces Trance Transitions Trip Hop Dust Spectral High Fill Spectral Mid Mutation Sparse Texture Extreme Chaos
Factory presets should have actually useful settings rather than all being superficial parameter variations.
Plugin state must save and recall correctly inside a DAW project. 
 Use JUCE AudioProcessorValueTreeState or an equivalently robust architecture.
Every automatable parameter must have a stable permanent ID.
Never use display names as the sole persistence mechanism.
Support DAW automation correctly.
Smooth continuously changing DSP parameters.
Avoid zipper noise.
Do not access GUI objects from the audio thread.
Do not make unsafe APVTS/state calls from real-time DSP code if they can lock or allocate.
Cache/read parameters safely. 
 processBlock must be real-time safe.
Inside the real-time audio path:
NO heap allocation NO file I/O NO console logging NO mutex waiting NO UI work NO blocking operations NO creation/destruction of large objects NO unpredictable operations that can stall the audio thread
Preallocate required buffers in prepareToPlay or controlled non-real-time paths.
Use circular buffers/ring buffers designed for real-time access.
Use parameter smoothing.
Use ScopedNoDenormals or equivalent where appropriate.
Handle:
empty buffers mono/stereo layouts sidechain disconnected host transport stopped tempo unavailable sample-rate changes block-size changes plugin reset preset/state changes
without crashing. 
 Dry and generated paths must remain correctly time aligned.
If lookahead introduces latency:
delay dry audio to match,
report latency to the host,
account for latency during bypass where appropriate,
avoid comb filtering caused by unintended path misalignment.
Test this explicitly. 
 Implement bypass cleanly.
When bypassed, audio should remain valid and predictable.
Avoid clicks when entering/leaving bypass if feasible.
Do not continue injecting fill audio after bypass in a way that surprises the user. 
 Create a polished custom GUI rather than a default JUCE collection of generic sliders.
Aim approximately for a resizable interface around:
1100 x 700
with sensible minimum dimensions.
The design should feel like a modern creative audio-production plugin.
Do not copy another company’s GUI.
The core visual concept should make the spaces between sounds visible. 
 Use approximately this hierarchy.
TOP BAR
GapFill logo/name Preset browser Previous/Next preset Save preset if implemented A/B comparison if practical Randomize Settings Bypass
MAIN VISUALIZER
A large horizontally scrolling waveform/activity display occupying a significant part of the interface.
Display:
incoming audio energy/waveform detected event markers protected transient regions eligible gaps active fill regions lookahead region where useful
Suggested visual semantics:
Original audio/activity = neutral waveform Detected hits = obvious vertical markers Potential gaps = subtly highlighted Chosen fills = stronger highlighted blocks Protection areas = clearly distinguishable Fill playhead/activity = animated but lightweight
Do not require the visualization for DSP operation.
UI visualization data must be transferred from audio thread using a lock-free or otherwise real-time-safe technique.
DETECTION SECTION
Controls:
Mode Detector Source Sensitivity Minimum Gap Gap Confirmation/Release Lookahead Gap Threshold
CREATIVE SECTION
Large macro controls:
Fill Intensity Complexity Chaos Density
Fill Type selector: AUTO / REVERSE / STUTTER / DELAY / GRANULAR / FREEZE / PITCH / FILTER / TEXTURE / CRUSH / SCRAMBLE / SPECTRAL
When Auto is used, expose a panel where individual algorithm categories can be enabled/disabled or weighted.
PROTECTION SECTION
Transient Protect Low Protect Center Protect Next Event Protect Sidechain Protect
MIX SECTION
Dry Wet Output
SPECTRAL TAB/PANEL
When Spectral mode is active, show three horizontal or vertical lanes:
LOW MID HIGH
with crossover positions and activity meters.
Each lane should visually reveal when that frequency region is:
busy available currently being filled.
Do not overwhelm the primary workflow with every advanced parameter simultaneously.
Use expandable Advanced panels or context-specific controls where appropriate. 
 The plugin should be usable immediately without reading a manual.
The most important workflow is:
Insert GapFill.
Audio plays.
GapFill visibly identifies events and gaps.
Turn up Fill.
Interesting material appears between events.
Increase Complexity/Intensity for more extreme behavior.
Use Protect controls if needed.
Optionally choose a particular Fill Type.
Optionally route a sidechain for more advanced timing.
Tooltips should briefly explain unfamiliar controls.
Double-click should reset knobs to sensible defaults if your control framework supports it cleanly.
Modifier-based fine adjustment is desirable.
The GUI must not steal keyboard focus unnecessarily from the DAW.
Use scalable/responsive layout logic rather than absolute assumptions that break when resized. 
 Provide:
input level output level wet/fill activity gap-confidence visualization
Meters should have reasonable decay behavior.
Meter communication between processor and GUI must be real-time safe. 
 Do not let the GUI compromise audio performance.
Avoid excessively repainting the entire window.
Use sensible timer rates.
Do not perform FFT analysis purely for decorative visualization if the DSP already provides appropriate data.
Reuse analysis information where safe.
No OpenGL requirement is necessary unless the repository already has a proven graphical architecture requiring it. 
 GapFill must follow several musical rules.
RULE 1: The original hit is more important than the fill.
RULE 2: A fill should sound intentional rather than like continuous random effects.
RULE 3: Silence is not required. Relative musical space is enough.
RULE 4: Short gaps receive short fills.
RULE 5: Long gaps may receive more elaborate fills.
RULE 6: When the next important event arrives, the fill should gracefully get out of the way.
RULE 7: Low Fill/Complexity settings must be genuinely subtle.
RULE 8: High settings should become dramatically creative without causing instability.
RULE 9: Auto mode must produce variation but remain reproducible using a seed.
RULE 10: The dry material should remain recognizable unless the user deliberately pushes the plugin toward extreme settings. 

A four-on-the-floor drum loop enters GapFill.
Sensitivity detects kick, clap, kick, clap.
Minimum Gap = 80 ms. Fill = 30%. Complexity = 20%.
Expected: Most original drums remain untouched. Some gaps receive short reverse or filtered-delay fragments. The pattern remains recognizable.
A DnB drum loop enters GapFill.
Fill = 70%. Complexity = 70%. Chaos = 45%. Transient Protect = 90%.
Expected: Transient attacks remain hard and defined. Spaces between attacks contain short stutters, pitch fragments, reverses, granular debris, and occasional delays. The output sounds edited/glitched but the important drum hits retain impact.
GapFill is inserted on a sustained bass/synth track.
Drums are connected to the sidechain.
Detector Source = Sidechain. Fill Type = Auto. Low Protect = 100 Hz.
Expected: The drum rhythm determines when generated bass/synth fragments appear. Generated material avoids important drum events. Sub frequencies remain relatively clean.
GapFill is inserted on vocals.
Detector Mode = Phrase. Fill = 35%. Complexity = 45%.
Expected: GapFill avoids chopping every consonant. It identifies phrase endings and meaningful pauses. Small reverse echoes, granular tails, filtered repeats, and pitch fragments appear primarily after phrases.
A full drum bus enters Spectral mode.
Kick creates strong low-frequency activity. High-frequency region contains space.
Expected: GapFill may insert a short high-frequency granular/stutter effect without significantly interfering with kick energy.

 Use clean DSP modules rather than putting the entire plugin inside processBlock.
A sensible architecture may resemble:
GapFillAudioProcessor ParameterManager AnalysisEngine EnvelopeDetector TransientDetector GapDetector SpectralAnalyzer FillScheduler SourceHistoryBuffer FillEngine ReverseFill StutterFill DelayFill GranularFill FreezeFill PitchFill FilterFill TextureFill CrusherFill ScrambleFill SpectralFill ProtectionEngine BandProcessor TempoManager LookaheadBuffer WetDryMixer MeteringState VisualizationState GapFillAudioProcessorEditor CustomLookAndFeel WaveformGapView DetectionPanel CreativePanel ProtectionPanel SpectralPanel
This is an architectural suggestion, not a requirement to create classes with these exact names.
Do not create abstractions solely for the sake of abstraction.
Keep components cohesive and testable. 
 For pitch-shifted fill modes, choose an implementation appropriate for short creative fragments.
Priorities are:
musical usefulness stability real-time safety acceptable CPU use
A lightweight granular/resampling approach is acceptable for glitch/pitch fills because GapFill is a creative processor rather than a transparent vocal pitch corrector.
Avoid introducing a huge third-party dependency solely for pitch shifting unless clearly justified. 
 If FFT processing is implemented for spectral analysis/smearing:
use correct windowing,
use overlap/add where reconstruction is required,
preallocate buffers,
avoid allocations inside processBlock,
ensure normalized output,
prevent spectral level buildup,
test multiple sample rates and block sizes.
Use JUCE DSP FFT functionality unless there is a compelling reason otherwise. 
 Testing is mandatory.
Create meaningful automated tests for the important DSP behavior.
Tests should validate actual logic, not merely instantiate classes.
At minimum test:
Plugin initializes at supported sample rates.
Plugin processes common block sizes without exceptions/crashes.
Silence remains finite and stable.
No NaN or infinity output from any major mode.
Event detector correctly identifies synthetic transient patterns.
Gap detector identifies obvious synthetic gaps.
Minimum Gap prevents fills in regions that are too short.
Transient Protect reduces generated activity around transients.
Next Event Protect causes fills to end before upcoming events.
Seeded Auto behavior is deterministic.
Different seeds can produce different valid fill decisions.
Circular history-buffer wraparound is correct.
Lookahead behaves safely at multiple sample rates.
Reported latency corresponds to actual lookahead path delay.
Mono processing remains valid if supported.
Stereo processing preserves valid channel behavior.
Sidechain absent does not crash.
Sidechain present can drive detection.
Preset/state serialization restores parameters.
Automation/state changes do not result in invalid DSP parameters.
Extreme parameter settings remain finite.
Spectral crossover processing does not produce gross discontinuities.
Repeated processing over a substantial simulated duration does not cause runaway level growth.
Bypass is safe.
Transport start/stop and missing host timing information are handled.
Also create useful offline render tests using generated test signals such as:
impulses
impulse trains
sine bursts
drum-like exponential-decay transients
alternating loud/quiet phrases
broadband noise bursts
low-frequency kick-like bursts plus high-frequency gaps
Where useful, inspect rendered WAV results numerically.
Testing must not only assert that “something happened.” Test expected timing and signal properties. 
 After automated testing, perform practical plugin validation as far as the environment allows.
Use the JUCE AudioPluginHost if available or another appropriate local VST3 validation mechanism.
If Steinberg’s validator, pluginval, or an equivalent validator is available, run it.
If one can be added safely and reasonably for development/testing, do so.
Validate at least:
plugin scan plugin creation editor creation/destruction audio processing state save/restore parameter enumeration bus layouts sidechain layout repeated opening/closing
Do not consider compilation alone sufficient. 
 The project must build from a clean configuration.
Provide sensible build configurations including Debug and Release.
Release should be optimized appropriately.
Do not enable unsafe floating-point optimizations that could break DSP correctness merely to gain tiny performance benefits.
The resulting VST3 should be easy to locate after building.
If a JUCE post-build copy mechanism is enabled, ensure it is valid for Windows and does not cause compilation to fail solely because a system plugin directory cannot be written.
Prefer a deterministic artifact/output directory inside the project/build tree. 
 Build with useful warnings.
Fix warnings caused by our code rather than suppressing them globally.
Do not spend time fixing warnings originating exclusively inside JUCE third-party sources unless necessary.
Use appropriate casts and types for sample counts, channel counts, and buffer indices.
Avoid undefined behavior. 
 GapFill must be efficient enough for normal DAW use.
Do not optimize blindly, but design for efficiency from the start.
Important areas:
no real-time allocations
efficient ring-buffer operations
reuse FFT calculations where practical
avoid unnecessary copying
only run expensive fill algorithms while required
only perform spectral calculations needed by the active mode
reduce inactive GUI work
precompute suitable window tables
avoid per-sample parameter tree lookups
If practical, add lightweight timing/performance tests or instrumentation available only in development builds.
Do not leave debug logging running from the audio thread. 
 Strictly separate:
AUDIO THREAD GUI THREAD BACKGROUND/NON-REAL-TIME WORK
Use atomics or lock-free transfer structures for simple meter/visualization state.
Never make the audio thread wait for the UI.
State loading must not expose partially constructed DSP state to processBlock.
Any expensive recomputation caused by a parameter change should be handled safely. 
 If analysis cannot confidently find an eligible gap, GapFill should simply not create a fill.
Never create garbage merely to demonstrate activity.
If host tempo is unavailable, use time-based operation.
If sidechain is selected but disconnected, gracefully fall back to main detection or clearly indicate the unavailable sidechain without breaking audio.
If a complex effect cannot fit into an available region, choose a shorter effect or skip it. 
 Save all required state.
A reopened DAW project should recreate the sound predictably, including the random Seed and relevant enabled algorithms.
State loading must tolerate missing/new parameters to allow future versions to evolve.
Do not make state restoration dependent on pointers, memory addresses, or runtime object order. 
 This must not be a “demo that compiles.”
Before you consider GapFill complete, it should feel like the beginning of a commercially usable plugin.
That means:
meaningful DSP
musically intelligent behavior
stable host interaction
professional parameter handling
visually understandable GUI
reproducible random behavior
real presets
sidechain support
proper latency handling
no obvious clicks
no unsafe real-time code
automated tests
successful clean build
plugin validation 
 Proceed systematically.
First inspect:
repository contents
git status
existing architecture
JUCE version
CMake/build system
available compilers
existing tests
existing CLAUDE.md/project instructions if present
Never speculate about files you have not inspected.
Then form an internal implementation plan and begin implementing it.
Do not stop after writing a plan.
Continue through implementation, compilation, testing, debugging, and validation.
You may use parallel/subagent work where Claude Code supports it and where tasks are genuinely independent, especially for:
DSP architecture review GUI implementation test development real-time safety review
However, you remain responsible for integrating and verifying everything yourself. Do not simply trust a subagent’s claim that something works.
Use git as a checkpoint system where appropriate.
Do not perform destructive git operations or discard existing unrelated user work.
If the repository already contains modifications, preserve them unless they directly conflict with this task.
Do not push anything remotely unless I explicitly request it.
If temporary test/debug files are created, remove unnecessary temporary files when finished. 
 Prioritize in this order when engineering decisions conflict:
audio stability
correct real-time behavior
preservation of important source material
gap/event detection quality
musical usefulness
timing correctness
fill sound quality
host compatibility
CPU efficiency
GUI polish
Do not sacrifice the first six merely to make the interface prettier. 
 GapFill passes the conceptual acceptance test if I can do the following:
Load the VST3 onto a drum loop.
Press Play.
Immediately see that the plugin recognizes individual drum events and spaces between them.
Turn FILL from 0% to approximately 30%.
Hear occasional short effects specifically between hits rather than hearing continuous processing over the drums.
Increase FILL, INTENSITY, and COMPLEXITY.
Hear increasingly elaborate reverses, repeats, grains, pitch movements, freezes, delays, and glitches.
Increase TRANSIENT PROTECT.
Hear the original attacks remain more defined.
Set LOW PROTECT around 100 Hz.
Hear the kick/sub region remain cleaner.
Choose SPECTRAL mode.
Observe that low/mid/high availability can differ and that fills can occupy specific spectral spaces.
Route drums into GapFill’s sidechain while GapFill itself is inserted on a bass/synth/vocal track.
Set Detector Source to SIDECHAIN.
Hear fills made from the processed track appear in spaces defined by the drums.
Save the DAW project.
Reload it.
Receive substantially the same behavior because plugin state and seed were restored.
That is the experience the implementation must deliver. 
 Before considering the implementation finished:
Build the project from a clean state.
Run the entire automated test suite.
Run available plugin validation tools.
Investigate every failure rather than bypassing it.
Check Debug and Release builds where practical.
Review processBlock and everything it calls for real-time safety.
Check parameter/state restoration.
Check lookahead and reported latency.
Check sidechain behavior.
Check at least 44.1, 48, 96, and 192 kHz.
Check multiple block sizes including at least one very small and one large block.
Exercise every fill algorithm.
Exercise extreme parameter settings.
Exercise rapid parameter changes.
Exercise opening and closing the editor repeatedly.
Exercise playback start/stop.
Exercise missing tempo information.
Confirm that audio remains finite.
Confirm there are no obvious clicks caused by slice boundaries.
Confirm build artifacts exist where expected.
Fix issues found during this verification.
Tests exist to verify implementation quality; never hard-code behavior merely to make tests pass.
If a test exposes a genuine implementation issue, fix the implementation.
Do not remove or weaken meaningful tests to obtain a green result. 
 Work until GapFill is implemented and verified, rather than stopping after scaffolding or producing instructions for me to finish it.
Make reasonable engineering decisions autonomously when this specification leaves implementation details open.
At completion, give me a concise report containing:
what was implemented
build result
test result
validation result
resulting VST3 location
any genuine remaining limitation that could not be resolved
Do not report a feature as complete unless it is actually implemented.
The central vision must remain intact throughout development:
GapFill is an intelligent creative effect that recognizes the spaces between meaningful musical events and turns those spaces into controlled, musically useful ear candy while protecting the original material. 
Begin by inspecting the current repository/environment, then implement GapFill completely.