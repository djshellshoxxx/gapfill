#include "PresetNavigation.h"
#include "GlobalSettings.h"
#include "PresetFiles.h"
#include "StepBurst.h"
#include "../../PluginProcessor.h"
#include "../i18n/Strings.h"

#include <algorithm>
#include <chrono>
#include <future>

namespace gapfill::presets
{
struct PresetNavigation::Impl
{
    Impl() { catalog.setFactory (factoryEntries()); }

    int index (const GapFillAudioProcessor& proc) const
    {
        if (proc.getCurrentPresetName() != "Custom")
            return catalog.indexOfIdentity ("F:" + proc.getCurrentPresetName());
        if (file != juce::File())
            for (int i = 0; i < (int) catalog.entries().size(); ++i)
                if (catalog.entries()[(size_t) i].file == file)
                    return i;
        return -1;
    }

    void adopt (const PresetEntry& entry)
    {
        file = entry.origin == PresetEntry::Origin::User ? entry.file : juce::File();
        name = entry.name;
        ++serial;
    }

    bool step (GapFillAudioProcessor& proc, int delta, std::uint32_t now,
               const std::shared_ptr<settings::GlobalSettings>& preferences)
    {
        if (delta == 0)
            return false;
        bool fallback = false;
        int target = index (proc);
        const int direction = delta > 0 ? 1 : -1;
        const auto steps = std::min ((std::int64_t) 64, std::abs ((std::int64_t) delta));
        if (preferences->getInt (kKeyStepScope, 0) == 1)
        {
            const auto pool = catalog.favouriteIndices (preferences->getFavourites());
            fallback = pool.empty();
            for (std::int64_t i = 0; i < steps; ++i)
                target = fallback ? catalog.step (target, direction)
                                  : PresetCatalog::stepWithin (pool, target, direction);
        }
        else
            for (std::int64_t i = 0; i < steps; ++i)
                target = catalog.step (target, direction);
        if (target < 0)
            return false;
        const auto entry = catalog.entries()[(size_t) target];
        const bool continuation = burst.isContinuation (now, proc.getUndoPushCount());
        const auto before = proc.getUndoPushCount();
        juce::String error;
        if (! loadPreset (proc, entry, error))
        {
            message = i18n::format ("Could not open preset: {0}", { error });
            ++serial;
            return false;
        }
        if (continuation && proc.getUndoPushCount() > before)
            proc.discardNewestUndoSnapshot();
        burst.record (now, proc.getUndoPushCount());
        adopt (entry);
        message = fallback ? i18n::text ("No favourites yet - stepping all presets")
                           : i18n::format ("Preset {0}/{1}: {2}",
                               { juce::String (target + 1), juce::String ((int) catalog.entries().size()), entry.name });
        proc.getUiState().status.set (info::StatusKind::Preview, message);
        return true;
    }

    PresetCatalog catalog;
    StepBurst burst;
    juce::File file;
    juce::String name, message;
    std::uint64_t serial = 0;
    int pendingStep = 0;
    bool refreshRequested = false;
    std::future<ScanResult> scan;
    std::atomic<bool> resetRequested { false };
};

PresetNavigation::PresetNavigation() : impl (std::make_unique<Impl>()) {}
PresetNavigation::~PresetNavigation() = default; // joins any value-only scan before module destruction

void PresetNavigation::requestStep (int delta) noexcept
{
    impl->pendingStep = (int) std::clamp ((std::int64_t) impl->pendingStep + delta,
                                        (std::int64_t) -64, (std::int64_t) 64);
    impl->refreshRequested = true;
}

void PresetNavigation::requestRefresh() noexcept { impl->refreshRequested = true; }
void PresetNavigation::requestReset() noexcept { impl->resetRequested.store (true, std::memory_order_release); }
void PresetNavigation::forgetBurst() noexcept { impl->burst.forget(); }

void PresetNavigation::pump (GapFillAudioProcessor& proc)
{
    if (impl->resetRequested.exchange (false, std::memory_order_acq_rel))
    {
        impl->file = juce::File();
        impl->name.clear();
        impl->message.clear();
        impl->burst.forget();
        impl->pendingStep = 0;
        impl->refreshRequested = false;
        ++impl->serial;
    }
    if (impl->refreshRequested && ! impl->scan.valid())
    {
        impl->refreshRequested = false;
        const auto roots = defaultScanRoots();
        try
        {
            impl->scan = std::async (std::launch::async, [roots] { return scanUserPresets (roots); });
        }
        catch (const std::exception&)
        {
            impl->pendingStep = 0;
            impl->message = i18n::text ("Could not scan user presets");
            ++impl->serial;
        }
    }
    if (impl->scan.valid() && impl->scan.wait_for (std::chrono::seconds (0)) == std::future_status::ready)
    {
        ScanResult result;
        try
        {
            result = impl->scan.get();
        }
        catch (const std::exception&)
        {
            impl->pendingStep = 0;
            impl->message = i18n::text ("Could not scan user presets");
            ++impl->serial;
            return;
        }
        impl->catalog.setUser (std::move (result.entries));
        const auto delta = impl->pendingStep;
        impl->pendingStep = 0;
        impl->step (proc, delta, juce::Time::getMillisecondCounter(), settings::GlobalSettings::shared());
    }
    const auto name = proc.getCurrentPresetName();
    if (name != "Custom" && name != impl->name)
    {
        impl->file = juce::File();
        impl->name = name;
        ++impl->serial;
    }
}

void PresetNavigation::noteLoaded (const PresetEntry& entry)
{
    impl->adopt (entry);
    impl->burst.forget();
}

void PresetNavigation::noteFile (const juce::File& file, const juce::String& name)
{
    impl->file = file;
    impl->name = name.isNotEmpty() ? name : file.getFileNameWithoutExtension();
    impl->burst.forget();
    ++impl->serial;
}

const juce::File& PresetNavigation::currentFile() const noexcept { return impl->file; }
const juce::String& PresetNavigation::displayName() const noexcept { return impl->name; }
const juce::String& PresetNavigation::status() const noexcept { return impl->message; }
std::uint64_t PresetNavigation::revision() const noexcept { return impl->serial; }
const PresetCatalog& PresetNavigation::catalog() const noexcept { return impl->catalog; }
int PresetNavigation::currentIndex (const GapFillAudioProcessor& proc) const { return impl->index (proc); }

#ifdef GAPFILL_UNIT_TESTS
bool PresetNavigation::stepNowForTests (GapFillAudioProcessor& proc, int delta, std::uint32_t now,
    const std::vector<ScanRoot>& roots, std::shared_ptr<settings::GlobalSettings> preferences)
{
    impl->catalog.setUser (scanUserPresets (roots).entries);
    return impl->step (proc, delta, now, preferences);
}
#endif
} // namespace gapfill::presets
