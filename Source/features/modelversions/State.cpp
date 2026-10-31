#include "State.h"

namespace gapfill::models
{
juce::ValueTree toModelVersionsTree (ModelVersions versions)
{
    if (! versions.valid())
        versions = ModelVersions::legacy();
    juce::ValueTree tree (kTreeType);
    tree.setProperty ("v", 1, nullptr);
    tree.setProperty ("cloud", (int) versions.cloud, nullptr);
    tree.setProperty ("taste", (int) versions.taste, nullptr);
    tree.setProperty ("arrangement", (int) versions.arrangement, nullptr);
    return tree;
}

ModelVersions fromModelVersionsTree (const juce::ValueTree& tree) noexcept
{
    if (! tree.hasType (kTreeType) || tree.getNumChildren() != 0 || tree.getNumProperties() != 4
        || tree.getProperty ("v").toString() != "1")
        return ModelVersions::legacy();

    const auto read = [&] (const char* name) -> std::uint8_t
    {
        const auto text = tree.getProperty (name).toString();
        return text == "1" ? 1 : (text == "2" ? 2 : 0);
    };
    const ModelVersions versions { read ("cloud"), read ("taste"), read ("arrangement") };
    return versions.valid() ? versions : ModelVersions::legacy();
}
} // namespace gapfill::models
