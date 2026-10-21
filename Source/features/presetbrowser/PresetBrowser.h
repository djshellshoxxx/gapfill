#pragma once

// U-4 preset browser popover: search box, category chips, list (name + one-line
// description + favourite star), "Save as user preset...". Opened from the preset
// name in the top bar through a CallOutBox. Message thread only; the user-folder scan
// runs on a background thread and its result is applied on the message thread.

#include "GlobalSettings.h"
#include "PresetCatalog.h"
#include "PresetFiles.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <memory>

namespace gapfill
{
class GapFillAudioProcessor;
}

namespace gapfill::presets
{

class PresetBrowser : public juce::Component,
                      private juce::ListBoxModel,
                      private juce::TextEditor::Listener,
                      private juce::Timer
{
public:
    PresetBrowser (GapFillAudioProcessor&, std::shared_ptr<settings::GlobalSettings>,
                   std::vector<ScanRoot> userRoots = defaultScanRoots());
    ~PresetBrowser() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Called after a preset was loaded successfully. */
    std::function<void (const PresetEntry&)> onLoaded;
    /** Called when the browser wants to be dismissed. */
    std::function<void()> onCloseRequested;
    /** Called when the user asks to save the current sound (the editor shows the dialog). */
    std::function<void()> onSaveRequested;

    //--- model access (tests, F-M22) -----------------------------------------------
    const PresetCatalog& getCatalog() const noexcept       { return catalog; }
    /** Synchronous user-folder scan (the popover itself scans in the background). */
    void refreshNow();
    void setSearch (const juce::String&);
    void setCategory (const juce::String&);
    int getNumVisible() const noexcept                      { return (int) visibleRows.size(); }
    const PresetEntry& getVisibleEntry (int row) const      { return catalog.entries()[(size_t) visibleRows[(size_t) row]]; }
    bool loadVisibleRow (int row);
    void toggleFavouriteRow (int row);

    /** F-M22: up to three suggested categories shown above the list with the measured numbers. */
    void setSuggestion (const juce::StringArray& categories, const juce::String& measured);
    juce::StringArray getSuggestedCategories() const        { return suggested; }

    juce::TextEditor& getSearchBox() noexcept               { return search; }

    static constexpr int preferredWidth = 540;
    static constexpr int preferredHeight = 460;

private:
    class Scan;

    int getNumRows() override                               { return (int) visibleRows.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override   { loadVisibleRow (row); }
    void returnKeyPressed (int row) override                { loadVisibleRow (row); }
    juce::String getNameForRow (int row) override;
    void textEditorTextChanged (juce::TextEditor&) override { applyFilter(); }
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;
    void timerCallback() override;

    void applyFilter();
    void rebuildFromUser (std::vector<PresetEntry>);
    void startBackgroundScan();
    void showRowMenu (int row);
    void buildChips();
    void updateChipStates();

    GapFillAudioProcessor& proc;
    std::shared_ptr<settings::GlobalSettings> settingsFile;
    std::vector<ScanRoot> roots;

    PresetCatalog catalog;
    std::vector<int> visibleRows;
    juce::String categoryFilter;
    juce::StringArray suggested;
    juce::String suggestionText;

    juce::TextEditor search;
    juce::OwnedArray<juce::TextButton> chips;
    juce::ListBox list { "Presets", this };
    juce::TextButton saveButton { "SAVE AS USER PRESET..." };
    juce::Label countLabel;

    std::unique_ptr<Scan> scan;
    juce::Rectangle<int> suggestionBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowser)
};

} // namespace gapfill::presets
