#include "../TestFramework.h"
#include <iostream>
#include "features/controller/ControllerState.h"
using namespace gapfill;
using namespace gftest;
GF_TEST (ControllerState, maximum_escaped_state_fits_8kib_without_duplicating_the_authoritative_maps)
{
    controller::ControllerState state;
    state.profile.extended = true; state.profile.name = juce::String::repeatedString ("&", 48);
    state.profile.parameterFeedback = true;
    for (int i = 0; i < 64; ++i)
        state.profile.params.push_back ({ std::string (29, 'x') + (i < 10 ? "00" : "0") + std::to_string (i), (std::uint8_t) i, 16, controller::TakeoverMode::Pickup });
    for (int i = 0; i < 64; ++i)
        state.profile.feedback.push_back ({ commands::FeedbackSource::SceneCurrent, 63, commands::MsgType::Note, 16, (std::uint8_t) (i % 16), 127, 127 });
    const auto tree = controller::encodeControllerState (state);
    GF_EXPECT (! tree.hasProperty ("__serialiseError"));
    const auto bytes = tree.toXmlString().getNumBytesAsUTF8();
    std::cout << "  Controllers max escaped XML bytes: " << bytes << "\n";
    GF_EXPECT (bytes <= (std::size_t) controller::kControllerStateBytes);
    controller::ControllerState restored;
    GF_EXPECT (controller::decodeControllerState (tree, restored));
    GF_EXPECT (restored.profile.params.size() == 64 && restored.profile.feedback.size() == 64);
    GF_EXPECT (! tree.hasProperty ("name") && ! tree.hasProperty ("device"));
    GF_EXPECT (! tree.getChild (0).hasProperty ("cc") && ! tree.getChild (0).hasProperty ("command"));
}
GF_TEST (ControllerState, malformed_state_rejects_atomically_and_missing_configuration_is_omitted)
{
    controller::ControllerState state;
    GF_EXPECT (! controller::encodeControllerState (state).isValid());
    state.profile.extended = true; state.profile.name = "Keep"; state.profile.txChannel = 2;
    auto tree = controller::encodeControllerState (state);
    for (const auto* key : { "v", "feedback", "tx", "timeout" })
    {
        auto invalid = tree.createCopy(); invalid.setProperty (key, "invalid", nullptr);
        controller::ControllerState unchanged = state;
        GF_EXPECT (! controller::decodeControllerState (invalid, unchanged) && unchanged.profile.txChannel == 2);
    }
    auto extra = tree.createCopy(); extra.appendChild (juce::ValueTree ("unknown"), nullptr);
    GF_EXPECT (! controller::decodeControllerState (extra, state));
    state.profile.params.push_back ({"wet", 74, 17, controller::TakeoverMode::Jump});
    GF_EXPECT (controller::encodeControllerState (state).hasProperty ("__serialiseError"));
}
