#pragma once

// F-L07 / N-VAL scene data model (docs/dev/12 F-L07, N-VAL). Message thread
// only; nothing here is touched by the audio thread (it only sees the overlay,
// see SceneOverlay.h).
//
// A Scene is a sparse parameter set: only the in-scope parameters whose value
// differs from the parameter default are stored. Recalling a scene sets EVERY
// in-scope parameter (stored value, else its default), so a scene means the
// same thing whatever the current sound is.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>

#include <cstdint>
#include <vector>

namespace gapfill::scenes
{

inline constexpr int kMaxScenes = 64;
inline constexpr int kMaxSceneParams = 128;
inline constexpr int kMaxSceneNameLen = 32;
inline constexpr int kMaxBankNameLen = 48;

enum Scope : std::uint16_t
{
    ScopeCreative = 1, ScopeWeights = 2, ScopeFx = 4, ScopeMusical = 8,
    ScopeMacros = 16, ScopeDetector = 32, ScopeProtection = 64, ScopeBands = 128
};

inline constexpr std::uint16_t kDefaultScope = ScopeCreative | ScopeWeights | ScopeFx | ScopeMusical | ScopeMacros;
inline constexpr std::uint16_t kAllScopes = 255;

enum class Quant : std::uint8_t { Inherit = 0, Immediate, Beat, Bar, Phrase };

/** Builds the parameter-id tables. Message thread; call once before use
    (idempotent). */
void initTables();

/** Ids a scope mask covers, minus sceneNeverIDs(), in parameter order. */
const std::vector<juce::String>& idsForScope (std::uint16_t scopeMask);

/** dry, wet, output, bypass, lookahead, seed, wetSolo, wetSafety, qualityMode,
    capture, audition, coop*, midiOut*. Never stored, never recalled. */
bool isNeverId (const juce::String& id);

/** Position in allParameterIDs() (stable: ids are append-only), or -1. */
int paramIndexOf (const juce::String& id);
const juce::String& paramIdAt (int index);
int numParams();

struct SceneParam
{
    std::uint16_t index = 0;
    float value01 = 0.0f;       ///< normalised 0..1
};

struct Scene
{
    juce::String name;
    std::uint16_t scope = kDefaultScope;
    Quant quant = Quant::Inherit;
    std::vector<SceneParam> params;      ///< sparse: differs from the default

    bool operator== (const Scene& o) const noexcept;
};

/** Reads the in-scope parameters from `apvts`. */
Scene captureScene (juce::AudioProcessorValueTreeState& apvts, const juce::String& name, std::uint16_t scope);

/** One resolved parameter move: index into allParameterIDs() and the REAL
    (denormalised) value. Every in-scope parameter appears exactly once. */
struct ResolvedParam { std::uint16_t index; float real; };

/** Resolves a scene to real values; false (and `why`) when the scene is invalid
    (unknown id, non-finite value, never-id, too many entries). */
bool resolveScene (juce::AudioProcessorValueTreeState& apvts, const Scene& scene,
                   std::vector<ResolvedParam>& out, juce::String& why);

bool isSceneValid (const Scene& scene, juce::String& why);

//==============================================================================
struct BankOptions
{
    bool wrap = false;                    ///< Next/Prev wrap instead of clamping
    Quant quant = Quant::Bar;             ///< bank default quantise mode
    bool immediateWithoutTransport = true;
};

/** Ordered list of up to 64 scenes. */
class SceneBank
{
public:
    int size() const noexcept                        { return (int) scenes.size(); }
    const Scene& at (int i) const                    { return scenes[(size_t) i]; }
    Scene& at (int i)                                { return scenes[(size_t) i]; }

    /** Stores `s` at `index` (index == size() appends). False when full/bad index. */
    bool store (int index, Scene s);
    bool remove (int index);
    bool duplicate (int index);
    bool move (int from, int to);
    bool rename (int index, const juce::String& newName);

    const juce::String& getName() const noexcept     { return name; }
    void setName (const juce::String& n)             { name = n.substring (0, kMaxBankNameLen); }

    BankOptions options;
    int current = -1;                                // last committed scene

    void clear();

    /** Replaces the whole content (used by import/undo). */
    void assign (const SceneBank& other);

    //------------------------------------------------------------------------
    // <Scenes> subtree (<= 24 KiB).
    static constexpr const char* subtreeName = "Scenes";

    /** Serialises scenes in order until the cap would be exceeded; the rest are
        left out and `truncated` is set (the bank can still be exported). */
    juce::ValueTree toValueTree (std::size_t maxBytes, bool* truncated = nullptr) const;
    static bool validateTree (const juce::ValueTree&);
    void restore (const juce::ValueTree&);

    /** Canonical text of the scene payloads for digests / equality in tests. */
    juce::String canonicalPayload() const;

private:
    juce::String name;
    std::vector<Scene> scenes;
};

} // namespace gapfill::scenes
