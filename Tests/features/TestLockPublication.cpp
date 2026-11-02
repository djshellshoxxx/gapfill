#include "../TestFramework.h"
#include "../support/AllocGuard.h"
#include "features/lock/LockPublication.h"
#include "features/lock/LockCommands.h"

#include <thread>

using namespace gapfill;
using namespace gftest;

namespace
{
    lock::LockSnapshot snapshot (std::uint32_t seed)
    {
        lock::LockSnapshot value;
        value.valid = true;
        value.recipe.seed = seed;
        value.extras.tape.version = 1;
        value.extras.tape.chosen = 1;
        value.extras.tape.subseed = seed;
        value.extras.tape.values[tapedamage::Amount] = 10000;
        value.extras.tape.values[tapedamage::Mix] = 10000;
        return value;
    }
}

GF_TEST (LockPublication, latest_audio_lock_survives_a_full_event_queue_and_the_last_callback)
{
    lock::LockPublication publication;
    FillEventQueue events;
    FillEvent ignored;
    for (int i = 0; i < FillEventQueue::capacity; ++i)
        events.push (ignored);
    const auto wanted = snapshot (42);
    {
        AllocGuard guard;
        GF_EXPECT (publication.publishAudio (wanted, publication.currentEpoch()));
        FillEvent hint;
        hint.type = FillEventType::LockSet;
        hint.recipe = wanted.recipe;
        hint.extras = wanted.extras;
        events.push (hint); // dropped, then audio stops
        GF_EXPECT (guard.count() == 0);
    }
    GF_EXPECT (events.getDroppedCount() == 1);
    lock::LockSnapshot latest;
    GF_EXPECT (publication.tryLatest (latest));
    GF_EXPECT (latest.valid && latest.recipe.seed == 42 && latest.extras == wanted.extras && latest.generation != 0);
}

GF_TEST (LockPublication, restore_baseline_is_immediately_persistent_and_bypasses_a_full_command_queue)
{
    lock::LockPublication publication;
    const auto oldEpoch = publication.currentEpoch();
    GF_EXPECT (publication.publishAudio (snapshot (1), oldEpoch));
    CommandQueue commands;
    Command old;
    old.type = CommandType::LockRecipe;
    old.lockEpoch = oldEpoch;
    for (int i = 0; i < CommandQueue::capacity; ++i)
        GF_EXPECT (commands.push (old));
    const auto wanted = snapshot (2);
    const auto epoch = publication.beginRestore (wanted);
    GF_EXPECT (epoch > oldEpoch);
    lock::LockSnapshot latest;
    GF_EXPECT (publication.tryLatest (latest) && latest.recipe.seed == 2 && latest.generation == 0);
    GF_EXPECT (publication.pendingRestore (oldEpoch, latest) && latest.restoreEpoch == epoch);
    GF_EXPECT (! publication.pendingRestore (epoch, latest));
    GF_EXPECT (! publication.publishAudio (snapshot (99), oldEpoch));
    GF_EXPECT (! lock::acceptsCommandEpoch (old, epoch));
    GF_EXPECT (publication.tryLatest (latest) && latest.recipe.seed == 2 && latest.extras == wanted.extras);
}

GF_TEST (LockPublication, clearing_and_later_audio_updates_keep_one_canonical_epoch)
{
    lock::LockPublication publication;
    const auto epoch = publication.beginRestore (snapshot (10));
    GF_EXPECT (publication.publishAudio (snapshot (11), epoch));
    lock::LockSnapshot latest;
    GF_EXPECT (publication.tryLatest (latest) && latest.recipe.seed == 11 && latest.restoreEpoch == epoch);
    const auto cleared = publication.beginRestore ({});
    GF_EXPECT (cleared > epoch);
    GF_EXPECT (publication.tryLatest (latest) && ! latest.valid && latest.restoreEpoch == cleared);
    GF_EXPECT (! hasRecipeExtras (latest.extras));
}

GF_TEST (LockPublication, invalid_publication_fails_capture_until_a_valid_state_recovers)
{
    lock::LockPublication publication;
    auto invalid = snapshot (10);
    invalid.extras.tape.version = 99;
    GF_EXPECT (publication.beginRestore (invalid) == 0);
    auto untouched = snapshot (20);
    GF_EXPECT (! publication.tryLatest (untouched) && untouched.recipe.seed == 20);
    GF_EXPECT (publication.beginRestore (snapshot (30)) != 0);
    GF_EXPECT (publication.tryLatest (untouched) && untouched.recipe.seed == 30);
}

GF_TEST (LockPublication, concurrent_host_readers_and_restore_writers_never_mix_recipe_metadata)
{
    lock::LockPublication publication;
    std::atomic<bool> stop { false }, intact { true };
    std::atomic<int> reads { 0 };
    auto read = [&]
    {
        while (! stop.load())
        {
            lock::LockSnapshot value;
            if (publication.tryLatest (value) && value.valid)
            {
                ++reads;
                if (value.recipe.seed != value.extras.tape.subseed || value.restoreEpoch == 0)
                    intact.store (false);
            }
        }
    };
    std::thread first (read), second (read);
    std::thread audio ([&]
    {
        for (std::uint32_t i = 1; i <= 10000; ++i)
            publication.publishAudio (snapshot (i), publication.currentEpoch());
    });
    auto restore = [&] (std::uint32_t firstSeed)
    {
        for (std::uint32_t i = 0; i < 200; ++i)
            if (publication.beginRestore (snapshot (firstSeed + i)) == 0)
                intact.store (false);
    };
    std::thread hostA (restore, 100000), hostB (restore, 200000);
    hostA.join(); hostB.join(); audio.join();
    stop.store (true);
    first.join(); second.join();
    lock::LockSnapshot latest;
    GF_EXPECT (intact.load() && reads.load() > 0 && publication.tryLatest (latest));
    GF_EXPECT (latest.recipe.seed == latest.extras.tape.subseed && latest.restoreEpoch == publication.currentEpoch());
}
