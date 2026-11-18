#include "../support/LiveRig.h"
#include "features/bank/BankFile.h"
#include "features/state/StateSubtrees.h"
#include "features/modulation/ExtendedState.h"

using namespace gapfill;
using namespace gftest;

GF_TEST (MacroV3, project_and_portable_bank_keep_opt_in_and_valid_newest_schema_wins)
{
    GapFillAudioProcessor processor;
    auto& model = processor.getLiveControl().macroMappings();
    const auto legacy = model.toValueTree();
    GF_EXPECT (model.enableExtended());
    macros::Mapping mapping;
    mapping.target = 46;
    GF_EXPECT (model.addMapping (7, mapping) == macros::MacroModel::AddResult::Ok);
    const auto expected = model.toValueTree();
    juce::ValueTree root ("GAPFILL_STATE");
    root.appendChild (legacy, nullptr);
    GF_EXPECT (state::writeSubtrees (processor, root, state::reservedSubtrees()).ok);
    GF_EXPECT (! root.getChildWithName (macros::MacroModel::subtreeName).isValid());
    GF_EXPECT (! root.getChildWithName (macros::MacroModel::legacySubtreeName).isValid());
    GF_EXPECT (root.getChildWithName (modulation::kSubtreeName).isEquivalentTo (expected));
    juce::MemoryBlock bytes;
    processor.getStateInformation (bytes);
    GF_EXPECT (bytes.getSize() > 0);
    GapFillAudioProcessor reopened;
    reopened.setStateInformation (bytes.getData(), (int) bytes.getSize());
    GF_EXPECT (reopened.getLiveControl().macroMappings().isExtendedEnabled());
    GF_EXPECT (reopened.getLiveControl().macroMappings().toValueTree().isEquivalentTo (expected));
    scenes::SceneBank bank;
    scenes::Scene scene;
    scene.name = "One";
    GF_EXPECT (bank.store (0, scene));
    bank::ExportOptions options;
    options.includeMacros = true;
    options.macros = expected;
    const auto xml = juce::parseXML (bank::exportToString (bank, options, "test", "2026-10-31"));
    GF_EXPECT (xml != nullptr);
    xml->addChildElement (legacy.createXml().release());
    auto parsed = bank::parseString (xml->toString());
    GF_EXPECT (parsed.status == bank::ParseStatus::Ok && parsed.macros.hasType (modulation::kSubtreeName));
    xml->getChildByName (modulation::kSubtreeName)->setAttribute ("v", 99);
    parsed = bank::parseString (xml->toString());
    GF_EXPECT (parsed.status == bank::ParseStatus::Ok && parsed.macros.hasType (macros::MacroModel::subtreeName));
}
