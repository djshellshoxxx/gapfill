#include "State.h"

namespace gapfill::models
{
juce::ValueTree toModelVersionsTree (ModelVersions versions)
{
    if (! versions.valid())
        versions = ModelVersions::legacy();
    juce::ValueTree tree (kTreeType);
    tree.setProperty ("v", 2, nullptr);
    tree.setProperty ("cloud", (int) versions.cloud, nullptr);
    tree.setProperty ("taste", (int) versions.taste, nullptr);
    tree.setProperty ("arrangement", (int) versions.arrangement, nullptr);
    tree.setProperty ("groove", (int) versions.groove, nullptr);
    return tree;
}

ModelVersions fromModelVersionsTree (const juce::ValueTree& tree) noexcept
{
    const auto schema = tree.getProperty ("v").toString();
    const bool legacySchema = schema == "1";
    if (! tree.hasType (kTreeType) || tree.getNumChildren() != 0
        || (! legacySchema && schema != "2") || tree.getNumProperties() != (legacySchema ? 4 : 5))
        return ModelVersions::legacy();

    const auto read = [&] (const char* name) -> std::uint8_t
    {
        const auto text = tree.getProperty (name).toString();
        return text == "1" ? 1 : (text == "2" ? 2 : 0);
    };
    const ModelVersions versions { read ("cloud"), read ("taste"), read ("arrangement"),
                                   legacySchema ? std::uint8_t { 1 } : read ("groove") };
    return versions.valid() ? versions : ModelVersions::legacy();
}
} // namespace gapfill::models
