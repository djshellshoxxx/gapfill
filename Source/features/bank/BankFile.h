#pragma once

// N-VAL Scene Bank exchange: the portable `.gapbank` file (docs/dev/12 N-VAL).
//
// UTF-8 XML, at most 256 KiB and 64 scenes. Message thread only. Parsing is a
// pure function that never touches plugin state; applying is atomic (the whole
// accepted set replaces / extends the bank, or nothing changes) and remembers
// the previous bank for a one-step "Undo import". Import never changes live
// parameters; only recalling a scene does. The file never contains paths or
// host information.

#include "../scenes/SceneController.h"

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

namespace gapfill::bank
{

inline constexpr juce::int64 kMaxFileBytes = 256 * 1024;
inline constexpr int kFormatVersion = 1;
inline constexpr int kStateVersionInFile = 3;

enum class ParseStatus { Ok, TooLarge, Malformed, NewerVersion, Empty };

struct SkippedScene
{
    juce::String name;
    juce::String reason;
};

struct ImportReport
{
    ParseStatus status = ParseStatus::Malformed;
    juce::String message;                     ///< user-facing text for every non-Ok status
    juce::String bankName;
    std::vector<scenes::Scene> accepted;
    std::vector<SkippedScene> skipped;
    int unknownIds = 0;                       ///< ids this version does not know (ignored)
    bool digestMismatch = false;              ///< integrity warning, the user may continue
    bool digestPresent = false;
    juce::ValueTree macros;                   ///< optional <Macros>, validated by its owner
};

struct ExportOptions
{
    std::vector<int> selection;               ///< scene indices; empty = the whole bank
    bool includeMacros = false;
    juce::ValueTree macros;                   ///< written only when includeMacros
};

/** SHA-256 of the UTF-8 text as lower-case hex (integrity digest, FIPS 180-4). */
juce::String sha256 (const juce::String& text);

/** Canonical payload text used for the digest (names, scopes, quant, params). */
juce::String canonicalPayload (const std::vector<scenes::Scene>& selected, const juce::String& bankName);

/** XML text of the bank. `created` is an ISO-8601 string (injected for tests). */
juce::String exportToString (const scenes::SceneBank&, const ExportOptions&,
                             const juce::String& appVersion, const juce::String& created);

/** Pure parse + validation. Never throws, never allocates beyond the report. */
ImportReport parseString (const juce::String& text);

/** Reads `file` (size-capped before reading) and parses it. */
ImportReport parseFile (const juce::File& file);

enum class ImportMode { Replace, Append, MergeByName };

struct ApplyResult
{
    bool ok = false;
    int added = 0;
    int replaced = 0;
    int dropped = 0;                          ///< did not fit into 64 scenes
    juce::String message;
};

/** Atomic apply. Only `ParseStatus::Ok` reports can be applied. */
ApplyResult applyImport (scenes::SceneController&, const ImportReport&, ImportMode);

/** Writes the XML via a temp file + rename. Refuses to overwrite unless allowed. */
bool writeFile (const juce::File& target, const juce::String& text, bool allowOverwrite, juce::String& error);

/** Placeholder for future format versions: migrates scene payloads from `fromVersion`. */
void migrateBank (juce::XmlElement& root, int fromVersion);

} // namespace gapfill::bank
