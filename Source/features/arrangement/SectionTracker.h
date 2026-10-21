#pragma once

// F-X04 Multi-section arrangement awareness (docs/dev/features/F-X04): the section tracker (X04a).
//
// Pure and allocation-free. It consumes one feature vector per 0.5 s frame (the F-L22 frame
// features), keeps a 128-frame self-similarity matrix (Foote 2000), finds boundaries with a
// Gaussian-tapered checkerboard novelty and a peak picker, follows the energy of the current
// section relative to the others, and predicts the next boundary when sections repeat in length.

#include "../context/EnergyTracker.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gapfill::arrangement
{

inline constexpr int kFrames = 128;                ///< ring length (64 s)
inline constexpr int kDims = 6;
inline constexpr int kKernelHalf = 4;              ///< L: half-width of the checkerboard kernel in frames
inline constexpr int kMaxSections = 16;
inline constexpr int kRefractoryFrames = 12;
inline constexpr int kLearningFrames = 16;

/** What the scheduler and the editor see. */
struct ArrangementState
{
    std::uint32_t sectionIndex = 0;
    std::uint16_t framesInto = 0;
    std::int16_t  expectedIn = -1;                 ///< frames to the predicted boundary, -1 unknown
    float level01 = 0.5f;                          ///< absolute level of this section (mean of loudness and onset density, 0..1)
    float energy01 = 0.5f;                         ///< rank of this section's energy among the sections heard
    float trend = 0.0f;                            ///< -1 (dropping) .. +1 (building)
    float confidence = 0.0f;                       ///< 0..1
    bool  learning = true;
};

class SectionTracker
{
public:
    void reset() noexcept;

    /** One frame of F-L22 features (call exactly once per 0.5 s frame). `sensitivity01` 0..1. */
    void pushFrame (const context::FrameFeatures&, float sensitivity01) noexcept;

    const ArrangementState& state() const noexcept { return st; }
    int frames() const noexcept { return framesObserved; }
    int boundaries() const noexcept { return boundaryCount; }
    int lastBoundaryFrame() const noexcept { return lastBoundary; }          ///< frame index of the newest boundary, -1 none
    int boundaryFrame (int i) const noexcept { return boundaryList[(std::size_t) i]; }
    float similarity (int frameA, int frameB) const noexcept;                ///< both inside the ring, else 0
    float noveltyAt (int frame) const noexcept;                              ///< raw checkerboard value, 0 outside

    static float kernel (int a, int b) noexcept;                             ///< a, b in [-L+1, L] (public for tests)
    static float kernelSum() noexcept;                                       ///< sum of |K|

private:
    void normalised (const context::FrameFeatures&, float (&out)[kDims]) noexcept;
    float novelty (int t) const noexcept;
    void updateSections (int boundaryFrameIndex, bool boundary) noexcept;

    std::array<std::array<float, kDims>, kFrames> feat {};
    std::array<std::array<float, kFrames>, kFrames> ssm {};
    std::array<float, kFrames> raw {};                       // per-frame (loudness + onset) / 2, unnormalised
    std::array<float, kFrames> noveltySeries {};             // novelty at frame t, ring indexed
    std::array<float, kDims> rawMin {}, rawMax {};
    std::array<std::array<float, kDims>, kFrames> rawFeat {};

    int framesObserved = 0;
    int lastBoundary = -1;
    int boundaryCount = 0;
    std::array<int, 64> boundaryList {};

    // sections
    int sectionStartFrame = 0;
    double sectionSum = 0.0;
    int sectionFrames = 0;
    std::array<float, kMaxSections> sectionMeans {};
    std::array<int, kMaxSections> sectionLengths {};
    int completed = 0;

    ArrangementState st;
};

} // namespace gapfill::arrangement
