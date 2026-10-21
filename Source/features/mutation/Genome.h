#pragma once

// F-L17 Mutation Lab: the gene table and the mutation operators (docs/dev/features/F-L17-mutation-lab.md).
//
// Pure and deterministic: the same (parent, pins, scope, rate, seed, generation, child index) always gives the
// same child on every platform. The "Gaussian" is the sum of 12 uniforms minus 6 (Irwin-Hall): only float adds in
// a fixed order, so no libm call can make two platforms disagree in the last bit.
//
// Genes live in normalised 0..1 parameter space. The table is append-only and versioned.

#include <array>
#include <cstdint>

namespace gapfill::mutation
{

inline constexpr int kGeneTableVersion = 1;
inline constexpr int kNumGenes = 29;
inline constexpr int kNumChildren = 4;

using Genes = std::array<float, kNumGenes>;

/** The gene parameter ids: fill, intensity, complexity, chaos, density, the 15 weights in Algo order,
    then for Low / Mid / High: Amt, Int, Prob. */
const std::array<const char*, kNumGenes>& geneIds() noexcept;

/** Scope 0 = Character (first 5), 1 = Character + Weights (20), 2 = All (29). */
int genesInScope (int scope) noexcept;

inline constexpr float kMinRate = 0.05f, kMaxRate = 0.5f, kDefaultRate = 0.15f;
inline constexpr float kMinChildDistance = 0.04f, kMinParentDistance = 0.02f;

/** Sum of 12 uniforms minus 6: mean 0, variance 1. */
float gaussian (std::uint64_t& rngState) noexcept;

struct ChildSet
{
    std::array<Genes, kNumChildren> child {};
    std::array<bool, kNumChildren> flagged {};     ///< true when distinctness could not be reached in 8 attempts
};

/** Four children of `parent`. Pinned genes copy the parent; genes outside `scope` copy the parent;
    weights are never all (nearly) zero. With `breedWith` each gene comes from the parent or that
    favourite with probability 0.5 and the Gaussian uses rate / 2. */
ChildSet makeChildren (const Genes& parent, std::uint32_t pins, int scope, float rate, std::uint64_t labSeed,
                       int generation, const Genes* breedWith = nullptr) noexcept;

/** Largest absolute difference between two gene vectors. */
float distanceInf (const Genes& a, const Genes& b) noexcept;

/** Index and signed difference (child - parent) of the `n` biggest changes, largest first. */
struct GeneDiff { int gene; float delta; };
int biggestDifferences (const Genes& parent, const Genes& child, GeneDiff* out, int n) noexcept;

} // namespace gapfill::mutation
