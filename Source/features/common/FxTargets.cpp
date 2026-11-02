#include "FxTargets.h"

#include "../../dsp/EngineSettings.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace gapfill::fxTargets
{
namespace
{
    struct Row
    {
        const char* id;
        void (*set) (EngineSettings&, float) noexcept;
        bool bus = false;
    };

    int integer (float value, int low, int high) noexcept
    {
        return (int) std::lround (std::clamp (value, (float) low, (float) high));
    }

   #define PCT(id, field, bus) { id, +[] (EngineSettings& s, float v) noexcept { s.field = v / 100.0f; }, bus }
   #define REAL(id, field, bus) { id, +[] (EngineSettings& s, float v) noexcept { s.field = v; }, bus }
   #define INT(id, field, lo, hi, bus) { id, +[] (EngineSettings& s, float v) noexcept { s.field = integer (v, lo, hi); }, bus }
   #define FLAG(id, field, bus) { id, +[] (EngineSettings& s, float v) noexcept { s.field = v > 0.5f; }, bus }

    constexpr Row rows[] =
    {
        PCT ("swellAmount", fx.swell.amount01, false), INT ("swellCurve", fx.swell.curve, 0, 3, false),
        PCT ("swellFilter", fx.swell.filter01, false), INT ("swellTarget", fx.swell.target, 0, 2, false),
        PCT ("cloudAmount", fx.cloud.amount01, false), REAL ("cloudDensity", fx.cloud.densityHz, false),
        REAL ("cloudSize", fx.cloud.sizeMs, false), REAL ("cloudSpan", fx.cloud.spanMs, false),
        PCT ("cloudJitter", fx.cloud.jitter01, false), REAL ("cloudPitch", fx.cloud.pitchCents, false),
        PCT ("cloudSpread", fx.cloud.spread01, false),
        PCT ("ratAmount", fx.ratchet.amount01, false), INT ("ratDiv", fx.ratchet.division, 0, (int) Division::NumDivisions - 1, false),
        INT ("ratRepeats", fx.ratchet.repeats, 2, 16, false), PCT ("ratAccel", fx.ratchet.accel, false),
        PCT ("ratGate", fx.ratchet.gate01, false),
        PCT ("rcAmount", fx.rcrush.amount01, false), INT ("rcBits", fx.rcrush.bits, 1, 16, false),
        INT ("rcRate", fx.rcrush.rate, 1, 64, false), INT ("rcHold", fx.rcrush.hold, 0, (int) Division::NumDivisions, false),
        PCT ("rcTone", fx.rcrush.tone, false),
        PCT ("tdAmount", fx.tape.amount01, false), PCT ("tdWow", fx.tape.wow01, false),
        PCT ("tdFlutter", fx.tape.flutter01, false), PCT ("tdSat", fx.tape.sat01, false),
        PCT ("tdDropout", fx.tape.dropout01, false), PCT ("tdSplice", fx.tape.splice01, false),
        FLAG ("scrubOn", fx.scrub.on, false), PCT ("scrubPos", fx.scrub.pos01, false),
        REAL ("scrubWidth", fx.scrub.widthMs, false),
        FLAG ("bandLowFx", fx.bandFx.low, false), FLAG ("bandMidFx", fx.bandFx.mid, false), FLAG ("bandHighFx", fx.bandFx.high, false),
        PCT ("resAmount", fx.resonator.amount01, true), INT ("resNote", fx.resonator.note, 24, 96, true),
        INT ("resMode", fx.resonator.mode, 0, 1, true), PCT ("resDecay", fx.resonator.decay01, true),
        PCT ("resDamp", fx.resonator.damp01, true), PCT ("resSpread", fx.resonator.spread01, true),
        PCT ("fsdAmount", fx.fsdelay.amount01, true),
        { "fsdShift", +[] (EngineSettings& s, float v) noexcept { s.fx.fsdelay.shiftHz = std::clamp (v, -2000.0f, 2000.0f); }, true },
        INT ("fsdTime", fx.fsdelay.time, 0, (int) Division::NumDivisions - 1, true),
        { "fsdFeedback", +[] (EngineSettings& s, float v) noexcept { s.fx.fsdelay.feedback01 = std::clamp (std::round (v * 100.0f) / 10000.0f, 0.0f, 0.85f); }, true },
        PCT ("fsdDamp", fx.fsdelay.damp01, true), PCT ("fsdStereo", fx.fsdelay.stereo01, true),
        PCT ("dubAmount", fx.dub.amount01, true), INT ("dubDiv", fx.dub.div, 0, (int) Division::NumDivisions - 1, true),
        { "dubFeedback", +[] (EngineSettings& s, float v) noexcept { s.fx.dub.feedback01 = std::clamp (std::round (v * 100.0f) / 10000.0f, 0.0f, 0.9f); }, true },
        REAL ("dubLow", fx.dub.lowHz, true), REAL ("dubHigh", fx.dub.highHz, true),
        PCT ("dubMove", fx.dub.move01, true), PCT ("dubSat", fx.dub.sat01, true), FLAG ("dubPing", fx.dub.ping, true),
        PCT ("fmtAmount", fx.formant.amount01, true),
        { "fmtShift", +[] (EngineSettings& s, float v) noexcept { s.fx.formant.shiftSt = std::clamp (v, -12.0f, 12.0f); }, true },
        PCT ("fmtTilt", fx.formant.tilt01, true), INT ("fmtVowel", fx.formant.vowel, 0, 5, true),
        PCT ("sfzAmount", fx.spfreeze.amount01, true), REAL ("sfzLength", fx.spfreeze.lengthMs, true),
        PCT ("sfzDecay", fx.spfreeze.decay01, true), PCT ("sfzSmear", fx.spfreeze.smear01, true),
        PCT ("sfzMorph", fx.spfreeze.morph01, true),
        { "sfzPitch", +[] (EngineSettings& s, float v) noexcept { s.fx.spfreeze.pitchSt = std::clamp (v, -12.0f, 12.0f); }, true },
        PCT ("sfzWidth", fx.spfreeze.width01, true),
        PCT ("cvAmount", fx.convo.amount01, true), REAL ("cvSize", fx.convo.sizeMs, true), PCT ("cvDamp", fx.convo.damp01, true),
        { "fl20_amount", +[] (EngineSettings& s, float v) noexcept { s.voiceFormant.amount01 = std::clamp (v / 100.0f, 0.0f, 1.0f); s.voiceFormant.enabled = s.voiceFormant.amount01 > 0.0f; }, false },
        { "fl20_shift", +[] (EngineSettings& s, float v) noexcept { s.voiceFormant.shiftSt = std::clamp (v, -12.0f, 12.0f); }, false },
        { "fl20_tilt", +[] (EngineSettings& s, float v) noexcept { s.voiceFormant.tiltDbPerOct = std::clamp (v, -6.0f, 6.0f); }, false },
        INT ("fl20_vowel", voiceFormant.vowel, 0, 5, false),
        { "fl20_preserve", +[] (EngineSettings& s, float v) noexcept { s.voiceFormant.preserve01 = std::clamp (v / 100.0f, 0.0f, 1.0f); }, false },
        INT ("fl20_detail", voiceFormant.detail, 0, 2, false),
        PCT ("fm20_amount", fx.tapeExtra.amount01, true), PCT ("fm20_wow", fx.tapeExtra.wow01, true),
        PCT ("fm20_flutter", fx.tapeExtra.flutter01, true), PCT ("fm20_sat", fx.tapeExtra.sat01, true),
        PCT ("fm20_dropDensity", fx.tapeExtra.dropDensity01, true), PCT ("fm20_dropDepth", fx.tapeExtra.dropDepth01, true),
        PCT ("fm20_splice", fx.tapeExtra.splice01, true), PCT ("fm20_instab", fx.tapeExtra.instab01, true),
        PCT ("fm20_mix", fx.tapeExtra.mix01, true),
        FLAG ("fx01_mode", perf.pitchSync.enabled, false),
        { "fx01_intervals", +[] (EngineSettings& s, float value) noexcept
            { s.perf.pitchSync.intervals = (harmonic::psola::IntervalSet) integer (value, 0, 5); }, false },
        INT ("fx01_voices", perf.pitchSync.voices, 1, 4, false),
        PCT ("fx01_motion", perf.pitchSync.motion01, false), PCT ("fx01_scatter", perf.pitchSync.scatter01, false),
        PCT ("fx01_tone", perf.pitchSync.tone, false), PCT ("fx01_snap", perf.pitchSync.snap01, false)
    };

   #undef PCT
   #undef REAL
   #undef INT
   #undef FLAG

    static_assert (std::size (rows) < 256);
}

int indexForId (const char* id) noexcept
{
    for (int i = 0; i < (int) std::size (rows); ++i)
        if (std::strcmp (id, rows[i].id) == 0)
            return i;
    return -1;
}

void apply (EngineSettings& s, int index, float real) noexcept
{
    if (index >= 0 && index < (int) std::size (rows) && std::isfinite (real))
        rows[index].set (s, real);
}

bool isBusEffect (int index) noexcept
{
    return index >= 0 && index < (int) std::size (rows) && rows[index].bus;
}
}
