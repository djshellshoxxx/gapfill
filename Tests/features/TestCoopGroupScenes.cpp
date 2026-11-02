#if defined (GF_COOP_STANDALONE)
 #include "CoopStandalone.h"
#else
 #include "../TestFramework.h"
#endif

#include "../../Source/features/coop/GroupScenes.h"

using namespace gapfill::coop::deterministic;

GF_TEST (CoopGroupScenes, latest_request_wins_without_crossing_groups)
{
    GroupScenes scenes;
    SceneCursor cursor;
    GroupSceneEvent event;
    GF_EXPECT (! scenes.poll (1, cursor, event));
    GF_EXPECT (scenes.request (1, 2, 8.0, 12));
    GF_EXPECT (scenes.request (1, 5, 12.0, 12));
    GF_EXPECT (! scenes.poll (2, cursor, event));
    cursor = {};
    GF_EXPECT (scenes.poll (1, cursor, event));
    GF_EXPECT (event.kind == GroupSceneEvent::Recall && event.sceneIndex == 5 && event.targetPpq == 12.0);
    GF_EXPECT (event.membershipGeneration == 12);
    GF_EXPECT (! scenes.poll (1, cursor, event));
}

GF_TEST (CoopGroupScenes, cancellation_prevents_old_request_from_reappearing)
{
    GroupScenes scenes;
    SceneCursor cursor;
    GroupSceneEvent event;
    GF_EXPECT (scenes.request (1, 2, 8.0, 12));
    scenes.cancel (1);
    GF_EXPECT (scenes.poll (1, cursor, event));
    GF_EXPECT (event.kind == GroupSceneEvent::Cancel);
    GF_EXPECT (! scenes.poll (1, cursor, event));
    GF_EXPECT (scenes.request (1, 4, 16.0, 12));
    GF_EXPECT (scenes.poll (1, cursor, event));
    GF_EXPECT (event.kind == GroupSceneEvent::Recall && event.sceneIndex == 4);
}

GF_TEST (CoopGroupScenes, a_request_after_cancel_is_not_discarded_by_a_lagging_reader)
{
    GroupScenes scenes;
    SceneCursor cursor;
    GroupSceneEvent event;
    scenes.cancel (1);
    GF_EXPECT (scenes.request (1, 7, 32.0, 5));
    GF_EXPECT (scenes.poll (1, cursor, event) && event.kind == GroupSceneEvent::Cancel);
    GF_EXPECT (scenes.poll (1, cursor, event) && event.kind == GroupSceneEvent::Recall);
    GF_EXPECT (event.sceneIndex == 7);
    GF_EXPECT (! scenes.poll (1, cursor, event));
}

GF_TEST (CoopGroupScenes, invalid_targets_and_unversioned_membership_are_rejected)
{
    GroupScenes scenes;
    GF_EXPECT (! scenes.request (0, 1, 8.0, 1));
    GF_EXPECT (! scenes.request (9, 1, 8.0, 1));
    GF_EXPECT (! scenes.request (1, -1, 8.0, 1));
    GF_EXPECT (! scenes.request (1, 64, 8.0, 1));
    GF_EXPECT (! scenes.request (1, 1, std::numeric_limits<double>::infinity(), 1));
    GF_EXPECT (! scenes.request (1, 1, 8.0, 0));
}
