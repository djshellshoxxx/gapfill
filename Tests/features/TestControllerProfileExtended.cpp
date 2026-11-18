#include "../TestFramework.h"
#include "features/commands/ControllerProfile.h"
using namespace gapfill;
using namespace gapfill::commands;
using namespace gftest;
namespace
{
constexpr const char* extendedProfile = R"({"name":"My controller","version":3,"params":[{"id":"wet","cc":74,"channel":2,"takeover":"pickup"}],"parameterFeedback":true,"txChannel":3,"bindings":[{"command":"Panic","type":"note","number":36}],"feedback":[{"source":"panic","type":"note","channel":3,"number":36}]})";
}
GF_TEST (ControllerProfileExtended, schema_three_roundtrips_parameter_channel_pickup_and_feedback_configuration)
{
    const auto parsed = parseProfile (extendedProfile);
    GF_EXPECT_MSG (parsed.ok, parsed.error);
    GF_EXPECT (parsed.profile.extended && parsed.profile.params.size() == 1);
    GF_EXPECT (parsed.profile.params[0].id == "wet" && parsed.profile.params[0].cc == 74);
    GF_EXPECT (parsed.profile.params[0].channel == 2 && parsed.profile.params[0].takeover == controller::TakeoverMode::Pickup);
    GF_EXPECT (parsed.profile.parameterFeedback && parsed.profile.txChannel == 3);
    const auto again = parseProfile (profileToJson (parsed.profile));
    GF_EXPECT_MSG (again.ok, again.error);
    GF_EXPECT (again.profile.params[0].id == "wet" && again.profile.parameterFeedback && again.profile.txChannel == 3);
}
GF_TEST (ControllerProfileExtended, malformed_extended_fields_and_overlapping_ownership_reject_the_entire_profile)
{
    const juce::String original (extendedProfile);
    for (const auto& replacement : { original.replace ("\"channel\":2", "\"channel\":17"),
                                    original.replace ("\"pickup\"", "\"catch\""),
                                    original.replace ("\"cc\":74", "\"cc\":-1"),
                                    original.replace ("\"parameterFeedback\":true", "\"parameterFeedback\":1"),
                                    original.replace ("\"txChannel\":3", "\"txChannel\":0"),
                                    original.replace ("\"id\":\"wet\"", "\"id\":\"bad id\""),
                                    original.replace ("\"version\":3", "\"version\":2"),
                                    original.replace ("\"type\":\"note\",\"number\":36", "\"type\":\"cc\",\"number\":74,\"channel\":2") })
        GF_EXPECT_MSG (! parseProfile (replacement).ok, replacement);
    GF_EXPECT (parseProfile (original.replace ("\"type\":\"note\",\"number\":36", "\"type\":\"cc\",\"number\":74,\"channel\":1")).ok);
    GF_EXPECT (! parseProfile (original.replace ("\"params\":[", "\"params\":[{\"id\":\"density\",\"cc\":74},")).ok);
}
GF_TEST (ControllerProfileExtended, schema_three_caps_and_legacy_schema_selection_are_explicit)
{
    Profile profile;
    profile.name = "Max knobs"; profile.extended = true;
    for (int i = 0; i < controller::kMaxParameterBindings; ++i)
        profile.params.push_back ({ "param_" + std::to_string (i), (std::uint8_t) i, 0, controller::TakeoverMode::Jump });
    const auto text = profileToJson (profile);
    GF_EXPECT (text.getNumBytesAsUTF8() < (std::size_t) kMaxProfileBytes);
    GF_EXPECT (parseProfile (text).ok);
    profile.params.push_back ({ "overflow", 100, 0, controller::TakeoverMode::Jump });
    GF_EXPECT (! parseProfile (profileToJson (profile)).ok);
    const auto legacy = parseProfile (R"({"name":"Old","version":1})");
    GF_EXPECT (legacy.ok && legacy.profile.params.empty() && ! legacy.profile.parameterFeedback && ! legacy.profile.extended);
    GF_EXPECT ((int) juce::JSON::parse (profileToJson (legacy.profile)).getProperty ("version", 0) == 1);
    GF_EXPECT (! parseProfile (juce::String (extendedProfile).replace ("\"version\":3", "\"version\":4")).ok);
}
