#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <vector>

namespace gapfill
{

//==============================================================================
/** What a help section may write. Implemented by the help view; the registry
    itself has no GUI dependency, so sections can be tested without a display. */
class HelpWriter
{
public:
    virtual ~HelpWriter() = default;

    virtual void title (const juce::String& text) = 0;
    virtual void versionLine (const juce::String& text) = 0;
    virtual void body (const juce::String& text) = 0;
    virtual void entry (const juce::String& term, const juce::String& description) = 0;
};

struct HelpSection
{
    int order = 0;                 ///< ascending; ties keep registration order
    juce::String title;            ///< heading; empty = the section writes its own title
    std::function<void (HelpWriter&)> write;
};

/** Registry of help sections (docs/dev/02-FOUNDATIONS.md F0-1).

    Sections register themselves at start-up (see HelpRegistrar); the help view
    reads them sorted by `order`. The sort happens at read time because the
    static-initialisation order between translation units is unspecified.

    Order bands: core sections 100-999, features 1000-8999 (each feature picks a
    unique value in its ticket), closing sections (Troubleshooting, About) 9000+.
    Append-only at start-up, read-only afterwards. */
class HelpRegistry
{
public:
    /** Simple form for features: a heading and plain paragraphs (one per line of `body`). */
    static void add (int order, juce::String title, juce::String body);

    /** Full form: the section writes its own content. */
    static void add (int order, juce::String title, std::function<void (HelpWriter&)> write);

    /** A snapshot of every registered section, stably sorted by `order`. */
    static std::vector<HelpSection> getSections();

    static int getNumSections();
};

/** Static-registration helper:  static gapfill::HelpRegistrar reg (1100, "Title", "Body"); */
struct HelpRegistrar
{
    HelpRegistrar (int order, juce::String title, juce::String body)
    {
        HelpRegistry::add (order, std::move (title), std::move (body));
    }

    HelpRegistrar (int order, juce::String title, std::function<void (HelpWriter&)> write)
    {
        HelpRegistry::add (order, std::move (title), std::move (write));
    }
};

} // namespace gapfill
