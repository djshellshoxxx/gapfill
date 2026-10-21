#include "PresetBrowser.h"

#include "../../PluginProcessor.h"
#include "../../gui/Knob.h"
#include "../../gui/LookAndFeel.h"

namespace gapfill::presets
{

namespace
{
    constexpr int kRowHeight = 38;
    constexpr int kStarWidth = 30;

    int textWidth (const juce::Font& font, const juce::String& text)
    {
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, text));
    }
}

//==============================================================================
class PresetBrowser::Scan : public juce::Thread
{
public:
    explicit Scan (std::vector<ScanRoot> r) : juce::Thread ("GapFill preset scan"), roots (std::move (r)) {}

    void run() override
    {
        result = scanUserPresets (roots);
        done.store (true, std::memory_order_release);
    }

    bool isDone() const noexcept { return done.load (std::memory_order_acquire); }

    ScanResult result;

private:
    std::vector<ScanRoot> roots;
    std::atomic<bool> done { false };
};

//==============================================================================
PresetBrowser::PresetBrowser (GapFillAudioProcessor& p, std::shared_ptr<settings::GlobalSettings> s,
                              std::vector<ScanRoot> userRoots)
    : proc (p), settingsFile (std::move (s)), roots (std::move (userRoots))
{
    setTitle ("Preset browser");
    setDescription ("Search, filter and load factory and user presets");
    setSize (preferredWidth, preferredHeight);
    setWantsKeyboardFocus (true);

    catalog.setFactory (factoryEntries());

    search.setTextToShowWhenEmpty ("Search presets by name or description", colours::textMuted);
    search.setTitle ("Search presets");
    search.setDescription ("Type to filter the preset list by name or description");
    search.setColour (juce::TextEditor::backgroundColourId, colours::background);
    search.setColour (juce::TextEditor::textColourId, colours::text);
    search.setColour (juce::TextEditor::outlineColourId, colours::edge);
    search.setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
    search.setFont (fonts::ui (13.0f));
    search.addListener (this);
    addAndMakeVisible (search);

    buildChips();

    list.setRowHeight (kRowHeight);
    list.setColour (juce::ListBox::backgroundColourId, colours::background);
    list.setColour (juce::ListBox::outlineColourId, colours::edge);
    list.setOutlineThickness (1);
    list.setTitle ("Presets");
    list.setWantsKeyboardFocus (true);
    addAndMakeVisible (list);

    saveButton.setTooltip ("Save the current sound as a user preset with a name, category and description");
    saveButton.onClick = [this] { if (onSaveRequested != nullptr) onSaveRequested(); };
    addAndMakeVisible (saveButton);

    countLabel.setFont (fonts::ui (11.0f));
    countLabel.setColour (juce::Label::textColourId, colours::textMuted);
    countLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (countLabel);

    applyFilter();
    startBackgroundScan();
}

PresetBrowser::~PresetBrowser()
{
    stopTimer();

    if (scan != nullptr)
        scan->stopThread (5000);
}

void PresetBrowser::startBackgroundScan()
{
    scan = std::make_unique<Scan> (roots);
    scan->startThread (juce::Thread::Priority::low);
    startTimerHz (20);
}

void PresetBrowser::timerCallback()
{
    if (scan != nullptr && scan->isDone())
    {
        stopTimer();
        scan->stopThread (1000);
        auto entries = std::move (scan->result.entries);
        scan.reset();
        rebuildFromUser (std::move (entries));
    }
}

void PresetBrowser::refreshNow()
{
    stopTimer();

    if (scan != nullptr)
    {
        scan->stopThread (5000);
        scan.reset();
    }

    rebuildFromUser (scanUserPresets (roots).entries);
}

void PresetBrowser::rebuildFromUser (std::vector<PresetEntry> entries)
{
    catalog.setUser (std::move (entries));
    applyFilter();
}

//==============================================================================
void PresetBrowser::buildChips()
{
    chips.clear();

    juce::StringArray names { "All", kFavourites };
    names.addArray (presetCategories());
    names.add (kUncategorised);

    for (const auto& n : names)
    {
        auto* chip = chips.add (new juce::TextButton (n));
        chip->setClickingTogglesState (true);
        chip->setRadioGroupId (4410);
        chip->setTitle ("Category " + n);
        chip->setWantsKeyboardFocus (false);
        chip->onClick = [this, n] { setCategory (n == "All" ? juce::String() : n); };
        addAndMakeVisible (chip);
    }

    updateChipStates();
}

void PresetBrowser::updateChipStates()
{
    for (auto* chip : chips)
    {
        const auto isAll = chip->getButtonText() == "All";
        const bool on = isAll ? categoryFilter.isEmpty() : categoryFilter == chip->getButtonText();
        chip->setToggleState (on, juce::dontSendNotification);
    }
}

void PresetBrowser::setSearch (const juce::String& text)
{
    search.setText (text, juce::dontSendNotification);
    applyFilter();
}

void PresetBrowser::setCategory (const juce::String& category)
{
    categoryFilter = category;
    updateChipStates();
    applyFilter();
}

void PresetBrowser::applyFilter()
{
    visibleRows = catalog.filter (search.getText(), categoryFilter, settingsFile->getFavourites());
    list.updateContent();
    list.repaint();

    countLabel.setText (juce::String ((int) visibleRows.size()) + " of " + juce::String ((int) catalog.entries().size()),
                        juce::dontSendNotification);
}

void PresetBrowser::setSuggestion (const juce::StringArray& categories, const juce::String& measured)
{
    suggested = categories;
    suggestionText = measured;
    resized();
    repaint();
}

//==============================================================================
void PresetBrowser::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::edge);
    g.drawRect (getLocalBounds(), 1);

    if (! suggestionBounds.isEmpty())
    {
        g.setColour (colours::accent.withAlpha (0.14f));
        g.fillRoundedRectangle (suggestionBounds.toFloat(), 3.0f);
        g.setColour (colours::text);
        g.setFont (fonts::ui (11.5f));
        g.drawText ("Suggested: " + suggested.joinIntoString (", ") + "   |   " + suggestionText,
                    suggestionBounds.reduced (6, 0), juce::Justification::centredLeft, true);
    }
}

void PresetBrowser::resized()
{
    auto b = getLocalBounds().reduced (10);

    search.setBounds (b.removeFromTop (28));
    b.removeFromTop (6);

    // Category chips wrap onto as many rows as needed.
    {
        constexpr int chipHeight = 22, gap = 4;
        int x = b.getX(), y = b.getY();

        for (auto* chip : chips)
        {
            const auto w = juce::jmax (44, textWidth (fonts::ui (11.0f), chip->getButtonText()) + 20);

            if (x + w > b.getRight())
            {
                x = b.getX();
                y += chipHeight + gap;
            }

            chip->setBounds (x, y, w, chipHeight);
            x += w + gap;
        }

        b.removeFromTop (y + chipHeight - b.getY() + 8);
    }

    // F-M22 suggestion strip.
    suggestionBounds = {};

    if (suggested.size() > 0)
    {
        suggestionBounds = b.removeFromTop (22);
        b.removeFromTop (6);
    }

    auto bottom = b.removeFromBottom (28);
    saveButton.setBounds (bottom.removeFromLeft (200));
    countLabel.setBounds (bottom);
    b.removeFromBottom (6);

    list.setBounds (b);
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) visibleRows.size()))
        return;

    const auto& e = catalog.entries()[(size_t) visibleRows[(size_t) row]];

    if (selected)
    {
        g.setColour (colours::accent.withAlpha (0.18f));
        g.fillRect (0, 0, w, h);
    }

    g.setColour (colours::edge);
    g.drawHorizontalLine (h - 1, 0.0f, (float) w);

    const bool fav = settingsFile->isFavourite (e.identity);
    auto star = juce::Rectangle<float> (4.0f, 0.0f, (float) kStarWidth, (float) h).withSizeKeepingCentre (14.0f, 14.0f);

    // A filled star = favourite; the outline form keeps the state readable without colour.
    juce::Path starPath;
    starPath.addStar (star.getCentre(), 5, 3.2f, 7.2f);
    g.setColour (fav ? colours::warning : colours::textMuted);

    if (fav)
        g.fillPath (starPath);
    else
        g.strokePath (starPath, juce::PathStrokeType (1.0f));

    auto text = juce::Rectangle<int> (kStarWidth + 8, 0, w - kStarWidth - 16, h).reduced (0, 3);
    auto top = text.removeFromTop (16);

    g.setColour (colours::text);
    g.setFont (fonts::ui (13.0f).boldened());
    const auto tag = e.origin == PresetEntry::Origin::User ? e.category + "  |  user" : e.category;
    const auto tagWidth = textWidth (fonts::ui (10.5f), tag) + 6;
    g.drawText (e.name, top.withTrimmedRight (tagWidth), juce::Justification::centredLeft, true);

    g.setColour (colours::textMuted);
    g.setFont (fonts::ui (10.5f));
    g.drawText (tag, top.removeFromRight (tagWidth), juce::Justification::centredRight, true);

    g.setColour (colours::textDim);
    g.setFont (fonts::ui (11.0f));
    g.drawText (e.description, text, juce::Justification::centredLeft, true);
}

juce::String PresetBrowser::getNameForRow (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) visibleRows.size()))
        return {};

    const auto& e = catalog.entries()[(size_t) visibleRows[(size_t) row]];
    return e.name + ", " + e.category + (settingsFile->isFavourite (e.identity) ? ", favourite" : "");
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, (int) visibleRows.size()))
        return;

    if (e.mods.isPopupMenu())
    {
        showRowMenu (row);
        return;
    }

    if (e.x < kStarWidth + 6)
    {
        toggleFavouriteRow (row);
        return;
    }

    loadVisibleRow (row);
}

void PresetBrowser::toggleFavouriteRow (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) visibleRows.size()))
        return;

    const auto& id = catalog.entries()[(size_t) visibleRows[(size_t) row]].identity;
    settingsFile->setFavourite (id, ! settingsFile->isFavourite (id));
    applyFilter();                                   // the Favourites filter may now exclude the row
}

bool PresetBrowser::loadVisibleRow (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) visibleRows.size()))
        return false;

    const auto entry = catalog.entries()[(size_t) visibleRows[(size_t) row]];
    juce::String error;

    if (! loadPreset (proc, entry, error))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Could not open preset", error, "OK", this);
        return false;
    }

    if (onLoaded != nullptr)
        onLoaded (entry);

    return true;
}

void PresetBrowser::showRowMenu (int row)
{
    const auto entry = catalog.entries()[(size_t) visibleRows[(size_t) row]];
    const bool user = entry.origin == PresetEntry::Origin::User;

    juce::PopupMenu menu;
    menu.addItem (1, settingsFile->isFavourite (entry.identity) ? "Remove from favourites" : "Add to favourites");

    if (user)
    {
        menu.addSeparator();
        menu.addItem (2, "Rename...");
        menu.addItem (3, "Delete...");
        menu.addItem (4, "Reveal in file manager");
    }

    juce::Component::SafePointer<PresetBrowser> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options(), [safe, entry, row] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        if (result == 1)
        {
            safe->settingsFile->setFavourite (entry.identity, ! safe->settingsFile->isFavourite (entry.identity));
            safe->applyFilter();
        }
        else if (result == 4)
        {
            entry.file.revealToUser();
        }
        else if (result == 2)
        {
            auto* window = new juce::AlertWindow ("Rename preset", "New name:", juce::MessageBoxIconType::NoIcon);
            window->addTextEditor ("name", entry.name, {});
            window->addButton ("Rename", 1);
            window->addButton ("Cancel", 0);
            gapfill::applyPaletteTo (*window);

            window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, entry, window] (int r)
            {
                const auto name = window->getTextEditorContents ("name");
                delete window;

                if (safe == nullptr || r != 1)
                    return;

                juce::String error;

                if (! renameUserPreset (entry, name, error))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not rename preset", error, "OK", safe.getComponent());

                safe->refreshNow();
            }), false);
        }
        else if (result == 3)
        {
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete preset",
                                                "Delete \"" + entry.name + "\"? This cannot be undone.", "Delete", "Cancel", safe.getComponent(),
                                                juce::ModalCallbackFunction::create ([safe, entry] (int r)
            {
                if (safe == nullptr || r != 1)
                    return;

                juce::String error;

                if (! deleteUserPreset (entry, error))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not delete preset", error, "OK", safe.getComponent());

                safe->refreshNow();
            }));
        }
    });
}

bool PresetBrowser::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onCloseRequested != nullptr)
            onCloseRequested();

        return true;
    }

    return false;
}

void PresetBrowser::textEditorEscapeKeyPressed (juce::TextEditor&)
{
    if (onCloseRequested != nullptr)
        onCloseRequested();
}

} // namespace gapfill::presets
