#pragma once

// RNG salts (docs/dev/02-FOUNDATIONS.md F0-2 rule 2, docs/dev/06 section 2).
//
// A feature never draws from the legacy scheduler Rng. Each new random
// decision uses its own sub-generator:
//
//     Rng sub;  sub.seed (hashCombine (ctx.key, kSalt_<Feature>));
//
// so features can merge in any order without perturbing each other's or the
// legacy decisions. Values are NEVER reused, renumbered or changed once
// released; claim the next unused constant for a new feature.

#include <cstdint>

namespace gapfill
{

inline constexpr std::uint64_t kSalt_Preview     = 0x9E3779B97F4A7C15ULL;
inline constexpr std::uint64_t kSalt_Mistakes    = 0xC2B2AE3D27D4EB4FULL;
inline constexpr std::uint64_t kSalt_Lock        = 0x165667B19E3779F9ULL;
inline constexpr std::uint64_t kSalt_Envelope    = 0xD6E8FEB86659FD93ULL;
inline constexpr std::uint64_t kSalt_Breathing   = 0xA0761D6478BD642FULL;
inline constexpr std::uint64_t kSalt_Phrase      = 0xE7037ED1A0B428DBULL;
inline constexpr std::uint64_t kSalt_Evolve      = 0x8EBC6AF09C88C6E3ULL;
inline constexpr std::uint64_t kSalt_BandType    = 0x589965CC75374CC3ULL;
inline constexpr std::uint64_t kSalt_Personality = 0x1D8E4E27C47D124FULL;
inline constexpr std::uint64_t kSalt_Macros      = 0xF1357AEA2E62A9C5ULL;
inline constexpr std::uint64_t kSalt_Sync        = 0xB492B66FBE98F273ULL;
inline constexpr std::uint64_t kSalt_Like        = 0x9AE16A3B2F90404FULL;
// docs/dev/12 section 2.4
inline constexpr std::uint64_t kSalt_Swell      = 0x5E11A001ULL;
inline constexpr std::uint64_t kSalt_Cloud      = 0xC10D0002ULL;
inline constexpr std::uint64_t kSalt_Scale      = 0x5CA1E003ULL;
inline constexpr std::uint64_t kSalt_Ratchet    = 0x4A7C0004ULL;
inline constexpr std::uint64_t kSalt_TapeDestr  = 0x7A9E0005ULL;
inline constexpr std::uint64_t kSalt_Resonator  = 0x4E500006ULL;
inline constexpr std::uint64_t kSalt_Groove     = 0x6200E007ULL;
inline constexpr std::uint64_t kSalt_Call       = 0xCA110008ULL;
inline constexpr std::uint64_t kSalt_Energy     = 0xE4E60009ULL;
inline constexpr std::uint64_t kSalt_Tension    = 0x7E45000AULL;
inline constexpr std::uint64_t kSalt_Battle     = 0xBA77000BULL;
inline constexpr std::uint64_t kSalt_Budget     = 0xB0D6000CULL;
inline constexpr std::uint64_t kSalt_Roulette   = 0x4001000DULL;
inline constexpr std::uint64_t kSalt_Storm      = 0x5702000EULL;
inline constexpr std::uint64_t kSalt_HGran      = 0x46A4000FULL;
inline constexpr std::uint64_t kSalt_Learn      = 0x1EA40010ULL;
inline constexpr std::uint64_t kSalt_Coop       = 0xC0090011ULL;
inline constexpr std::uint64_t kSalt_SpFreeze   = 0x5F2E0012ULL;
inline constexpr std::uint64_t kSalt_Dub        = 0xD0B00013ULL;
inline constexpr std::uint64_t kSalt_Fsd        = 0xF5D00014ULL;

} // namespace gapfill
