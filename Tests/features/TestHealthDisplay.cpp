#include "../TestFramework.h"

#include "features/events/EventRecorder.h"
#include "features/fixit/Fixit.h"
#include "features/health/DisplayText.h"
#include "features/i18n/Strings.h"
#include "features/telemetry/Telemetry.h"

using namespace gapfill;

GF_TEST (HealthDisplay, english_presentation_matches_the_unchanged_core_wording)
{
    i18n::ScopedLanguage language ("en");
    health::DescribeContext context;
    context.cpu01 = 0.82f;
    context.fallbackBpm = 137.0;
    for (int code = 0; code <= health::numHealthCodes; ++code)
    {
        const auto source = health::describe ((health::HealthCode) code, context);
        const auto display = health::displayDescription ((health::HealthCode) code, context);
        GF_EXPECT (display.message == source.message);
        GF_EXPECT (display.explanation == source.explanation);
        GF_EXPECT (display.action == source.action);
    }
    GF_EXPECT (health::telemetryText (1920, 48000.0, 0.82f, true) == telemetry::describeTelemetry (1920, 48000.0, 0.82f, true));
    events::EventRecorder recorder;
    GF_EXPECT (health::recordingText (recorder) == "Not recording");
}

GF_TEST (HealthDisplay, pseudo_translates_numeric_health_text_without_changing_core_codes_or_numbers)
{
    health::DescribeContext context;
    context.fallbackBpm = 137.0;
    const auto unchanged = health::describe (health::HealthCode::NoTransport, context).message;
    i18n::ScopedLanguage language ("pseudo");
    GF_EXPECT (i18n::currentLanguage() == "pseudo");
    const auto display = health::displayDescription (health::HealthCode::NoTransport, context);
    GF_EXPECT (display.message != unchanged && display.message.contains ("137"));
    GF_EXPECT (health::describe (health::HealthCode::NoTransport, context).message == unchanged);
}

GF_TEST (HealthDisplay, structured_fixit_display_preserves_the_parameter_action_and_exact_numeric_targets)
{
    i18n::ScopedLanguage language ("en");
    fixit::Proposal proposal;
    proposal.changes.push_back ({ "minGap", "Min Gap", " ms", {}, 120.0f, 85.0f });
    proposal.text = "Min Gap 120 -> 85 ms";
    GF_EXPECT (health::proposalText (proposal) == proposal.text);
    GF_EXPECT (health::proposalSpoken (proposal) == "Suggestion: change Min Gap 120 -> 85 ms. Press Apply to accept.");
    {
        i18n::ScopedLanguage pseudo ("pseudo");
        const auto display = health::proposalText (proposal);
        GF_EXPECT (display != proposal.text && display.contains ("120") && display.contains ("85"));
    }
    GF_EXPECT (proposal.changes[0].paramId == "minGap" && proposal.changes[0].to == 85.0f);
    GF_EXPECT (proposal.text == "Min Gap 120 -> 85 ms");
}
