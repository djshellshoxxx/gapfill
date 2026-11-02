#pragma once

#include "TapeMeta.h"
#include "TapeSettings.h"
#include <span>

namespace gapfill::tapedamage
{
/** Exact28-byte LE codec. Failure leaves output untouched; never allocates. */
bool encodeTapeMeta (const TapeMeta&, std::span<std::uint8_t>) noexcept;
bool decodeTapeMeta (std::span<const std::uint8_t>, TapeMeta&) noexcept;

/** Quantise all controls before both probability selection and original DSP.
    Amount0 returns canonical absent metadata, with no random draws. */
TapeMeta latchTapeSettings (const fx::TapeExtraSettings&, std::uint64_t subseed) noexcept;
fx::TapeExtraSettings settingsFromTapeMeta (const TapeMeta&) noexcept;
} // namespace gapfill::tapedamage
