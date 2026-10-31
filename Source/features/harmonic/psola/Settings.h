#pragma once

#include <cstdint>

namespace gapfill::harmonic::psola
{
enum class IntervalSet : std::uint8_t { Octaves, OpenFifths, Major, Minor, Suspended, Scale };

/** New opt-in HGEXT path. Defaults are neutral. Released hg* settings and the
    old HarmonicGrains engine remain independent when enabled is false. */
struct Settings
{
    bool enabled = false;
    IntervalSet intervals = IntervalSet::OpenFifths;
    int voices = 2;
    float motion01 = 0.3f;
    float scatter01 = 0.2f;
    float tone = 0.0f;
    float snap01 = 0.0f;
    float mix01 = 1.0f; // internal global-FX-send blend, never a host parameter
};

/** Absolute pitch-class mask (bit0 C through bit11 B). rootPc=-1 means no key. */
struct Scale
{
    std::uint16_t pitchClasses = 0x0fff;
    int rootPc = -1;
};

/** Latched at fill start, only after the source-coordinate pitch query. */
struct Setup
{
    Settings settings;
    Scale scale;
    float f0Hz = 0.0f;
    float clarity = 0.0f;
    bool voiced = false;
};

inline constexpr int kInputHop = 32;
inline constexpr int kLagsPerHop = 16;
inline constexpr int kDifferenceWindow = 512;
inline constexpr int kDecimatedRing = 1024;
inline constexpr int kMaxPitchLag = 256;
inline constexpr int kPitchFrames = 256;
inline constexpr int kEpochs = 512;
inline constexpr int kMaxVoices = 4;
inline constexpr int kGrainsPerVoice = 8;  // 2*T source windows overlap eight times at ratio4
inline constexpr int kGrainCap = kMaxVoices * kGrainsPerVoice;
inline constexpr int kMaxCaptureSamples = 61440; // 0.32s at192kHz
} // namespace gapfill::harmonic::psola
