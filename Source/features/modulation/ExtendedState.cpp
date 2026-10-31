#include "ExtendedState.h"
#include "../macros/MacroModel.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace gapfill::modulation
{
namespace
{
    static_assert (sizeof (float) == 4 && std::numeric_limits<float>::is_iec559);
    constexpr int kRecordBytes = 16;
    constexpr int kMaxRecordBytes = kRecordBytes * macros::kMaxMappings;

    bool validMapping (const macros::Mapping& mapping)
    {
        return mapping.target < kTargetCount && mapping.curve <= 3 && mapping.bipolar <= 1 && mapping.pad == 0
            && std::isfinite (mapping.min01) && mapping.min01 >= 0 && mapping.min01 <= 1
            && std::isfinite (mapping.max01) && mapping.max01 >= 0 && mapping.max01 <= 1
            && std::isfinite (mapping.smoothMs) && mapping.smoothMs >= 0 && mapping.smoothMs <= 500;
    }

    void writeFloat (std::uint8_t* destination, float value)
    {
        const auto bits = std::bit_cast<std::uint32_t> (value);
        for (int byte = 0; byte < 4; ++byte) destination[byte] = (std::uint8_t) (bits >> (8 * byte));
    }

    float readFloat (const std::uint8_t* source)
    {
        std::uint32_t bits = 0;
        for (int byte = 0; byte < 4; ++byte) bits |= (std::uint32_t) source[byte] << (8 * byte);
        return std::bit_cast<float> (bits);
    }

    bool readMappings (const juce::String& encoded, ExtendedConfig& result, int macro)
    {
        if (encoded.length() > 172) return false;
        juce::MemoryOutputStream bytes;
        if (! juce::Base64::convertFromBase64 (bytes, encoded)) return false;
        const auto size = bytes.getDataSize();
        if (size > kMaxRecordBytes || size % kRecordBytes != 0) return false;
        result.count[macro] = (std::uint8_t) (size / kRecordBytes);
        const auto* data = (const std::uint8_t*) bytes.getData();
        for (int index = 0; index < result.count[macro]; ++index)
        {
            const auto* record = data + kRecordBytes * index;
            macros::Mapping mapping;
            mapping.target = record[0]; mapping.curve = record[1];
            mapping.bipolar = record[2]; mapping.pad = record[3];
            mapping.min01 = readFloat (record + 4);
            mapping.max01 = readFloat (record + 8);
            mapping.smoothMs = readFloat (record + 12);
            if (! validMapping (mapping)) return false;
            result.map[macro][index] = mapping;
        }
        return true;
    }
}

juce::ValueTree toExtendedTree (const ExtendedConfig& config)
{
    juce::ValueTree tree (kSubtreeName);
    tree.setProperty ("v", 3, nullptr);
    tree.setProperty ("enc", "m16le", nullptr);
    const auto legacy = macros::MacroModel::legacyConfigToTree (config.retainedLegacy);
    if (! macros::MacroModel::validateTree (legacy)) return {};
    tree.appendChild (legacy, nullptr);
    for (int macro = 0; macro < kMacroCount; ++macro)
    {
        if (config.count[macro] > macros::kMaxMappings
            || config.names[macro][macros::kNameBytes] != 0) return {};
        const auto name = juce::String::fromUTF8 (config.names[macro]);
        if (name.isEmpty() || macros::MacroModel::sanitiseName (name, {}) != name) return {};
        juce::ValueTree child ("M");
        child.setProperty ("i", macro, nullptr);
        child.setProperty ("n", name, nullptr);
        std::array<std::uint8_t, kMaxRecordBytes> data {};
        for (int index = 0; index < config.count[macro]; ++index)
        {
            const auto& mapping = config.map[macro][index];
            if (! validMapping (mapping)) return {};
            auto* record = data.data() + kRecordBytes * index;
            record[0] = mapping.target; record[1] = mapping.curve;
            record[2] = mapping.bipolar;
            writeFloat (record + 4, mapping.min01);
            writeFloat (record + 8, mapping.max01);
            writeFloat (record + 12, mapping.smoothMs);
        }
        child.setProperty ("d", juce::Base64::toBase64 (data.data(), (size_t) config.count[macro] * kRecordBytes), nullptr);
        tree.appendChild (child, nullptr);
    }
    return tree.toXmlString().getNumBytesAsUTF8() <= kMaxStateBytes ? tree : juce::ValueTree {};
}

bool fromExtendedTree (const juce::ValueTree& tree, ExtendedConfig& output)
{
    if (! tree.hasType (kSubtreeName) || tree.getNumProperties() != 2 || tree["v"].toString() != "3"
        || tree["enc"].toString() != "m16le" || tree.getNumChildren() != kMacroCount + 1
        || tree.toXmlString().getNumBytesAsUTF8() > kMaxStateBytes) return false;
    const auto legacy = tree.getChildWithName (macros::MacroModel::subtreeName);
    if (! legacy.isValid() || ! macros::MacroModel::validateTree (legacy)) return false;
    macros::MacroModel retained;
    retained.restore (legacy);
    auto result = extendedFromLegacy (retained.get());
    bool seen[kMacroCount] {};
    for (const auto& child : tree)
    {
        if (child == legacy) continue;
        const auto index = child["i"].toString();
        if (! child.hasType ("M") || child.getNumChildren() != 0 || child.getNumProperties() != 3
            || index.length() != 1 || ! index.containsOnly ("01234567")) return false;
        const auto macro = index.getIntValue();
        const auto name = child["n"].toString();
        if (seen[macro] || name.isEmpty() || macros::MacroModel::sanitiseName (name, {}) != name) return false;
        seen[macro] = true;
        std::memset (result.names[macro], 0, sizeof (result.names[macro]));
        std::memcpy (result.names[macro], name.toRawUTF8(), name.getNumBytesAsUTF8());
        for (auto& mapping : result.map[macro]) mapping = {};
        if (! readMappings (child["d"].toString(), result, macro)) return false;
    }
    output = result;
    return true;
}

bool validateExtendedTree (const juce::ValueTree& tree)
{
    ExtendedConfig ignored;
    return fromExtendedTree (tree, ignored);
}
} // namespace gapfill::modulation
