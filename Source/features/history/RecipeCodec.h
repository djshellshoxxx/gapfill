#pragma once

// Text encoding of a FillRecipe for state subtrees and .gfrecipe files
// (docs/dev/04 C-5 "State", docs/dev/05 U-1).
//
//   base64 ( 16 recipe bytes (FillRecipe toBytes layout) + 1 checksum byte )
//
// decode() accepts untrusted text: it checks the length, the checksum and
// isValid() and never allocates more than the input needs.

#include "../../dsp/FillRecipe.h"
#include "../../dsp/RecipeExtras.h"

#include <juce_core/juce_core.h>

namespace gapfill::recipecodec
{

/** One-byte checksum over the 16 recipe bytes. */
std::uint8_t checksum (const std::uint8_t (&bytes)[FillRecipe::serialisedBytes]) noexcept;

juce::String encode (const FillRecipe&);

/** False for bad base64, wrong size, wrong checksum or an out-of-range recipe. */
bool decode (const juce::String& text, FillRecipe& out);

/** Optional bounded sidecar (44 canonical bytes plus checksum, 60 base64 chars).
    Empty legacy metadata encodes to an empty string and is omitted by callers. */
juce::String encodeExtras (const RecipeExtras&);
bool decodeExtras (const juce::String&, RecipeExtras&);

} // namespace gapfill::recipecodec
