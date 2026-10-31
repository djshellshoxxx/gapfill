#pragma once

#include "PresetCatalog.h"

#include <memory>

namespace gapfill { class GapFillAudioProcessor; }
namespace gapfill::settings { class GlobalSettings; }

namespace gapfill::presets
{

/** Processor-owned navigation. Message thread only. Explicit steps/refreshes
    scan user files on a worker; pump never starts a periodic filesystem scan. */
class PresetNavigation
{
public:
    PresetNavigation();
    ~PresetNavigation();
    PresetNavigation (const PresetNavigation&) = delete;
    PresetNavigation& operator= (const PresetNavigation&) = delete;

    void requestStep (int delta) noexcept;
    void requestRefresh() noexcept;
    void pump (GapFillAudioProcessor&);

    /** Call after successful browser/file loads and saves, including while an
        editor is open. The shared identity and file then survive editor close. */
    void noteLoaded (const PresetEntry&);
    void noteFile (const juce::File&, const juce::String& displayName = {});
    const juce::File& currentFile() const noexcept;
    const juce::String& displayName() const noexcept;
    const juce::String& status() const noexcept;
    std::uint64_t revision() const noexcept;
    const PresetCatalog& catalog() const noexcept;
    int currentIndex (const GapFillAudioProcessor&) const;

   #ifdef GAPFILL_UNIT_TESTS
    /** Deterministic fixture sources: synchronous scan through the same bounded
        scanner, then step through the same load/Undo/favourites implementation. */
    bool stepNowForTests (GapFillAudioProcessor&, int delta, std::uint32_t nowMs,
                         const std::vector<ScanRoot>&, std::shared_ptr<settings::GlobalSettings>);
   #endif

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace gapfill::presets
