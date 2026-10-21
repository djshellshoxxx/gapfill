// The recipe drawer: Fill History strip, Lock, More Like That and recipe files
// (docs/dev/05 U-1, docs/dev/04 C-5 / C-11, docs/dev/11 "history drawer").
//
// Lives in Slot::VisualiserOverlay as ONE component sized to the visualiser. It
// draws nothing and intercepts no mouse events except on its small RECIPES handle
// and, while open, on the drawer itself, so the visualiser underneath keeps working.
// The editor is not edited: the slot rectangle is already reserved.
//
// Layout (open): three rows of 24 px
//   chips      the last 8 natural fills, newest first: family letter + describeRecipe
//   row 2      LOCK toggle (fillLock) | lock summary | AUDITION | LOCK THIS | MORE LIKE
//   row 3      SAVE... | LOAD... | EXPORT... | SET A | SET B | A/B | ...
//
// Everything is a real child component with a title and description, so keyboard
// focus (opt-in mode) and screen readers see the same controls as the mouse.

#include "RecipeServices.h"
#include "RecipeStatus.h"
#include "../audition/Heartbeat.h"
#include "../export/WavExport.h"
#include "../history/Describe.h"
#include "../history/RecipeFile.h"
#include "../lock/LikeBias.h"
#include "../../gui/Knob.h"
#include "../../gui/LookAndFeel.h"
#include "../../gui/SlotHost.h"
#include "../../PluginProcessor.h"

namespace gapfill
{

namespace
{
    constexpr int kRow = 24, kGap = 4, kPad = 8;
    constexpr int kHandleW = 96, kHandleH = 16;
    constexpr int kChips = RecipeHistory::capacity;

    juce::Colour familyColour (int algo)
    {
        if (algo < 0 || algo >= numAlgos)
            return colours::textMuted;

        switch (kAlgoFamilies[(size_t) algo])
        {
            case AlgoFamily::Rhythmic:    return colours::accentWarm;
            case AlgoFamily::Textural:    return colours::accent;
            case AlgoFamily::PitchTape:   return colours::accentAmber;
            case AlgoFamily::Destructive: return colours::clip;
        }

        return colours::textMuted;
    }

    //==========================================================================
    /** One history chip. Left click selects, right click opens the action menu. */
    class ChipButton : public juce::Button
    {
    public:
        ChipButton() : juce::Button ("Recipe") { setWantsKeyboardFocus (false); }

        void set (const FillRecipe& r, bool isSelected, bool isLocked)
        {
            recipe = r;
            selected = isSelected;
            locked = isLocked;
            const auto text = history::describeRecipe (r, 0.0);
            setButtonText (text);
            setTitle ("Recipe: " + text);
            setTooltip (text + (locked ? " (locked)" : "")
                        + ". Click to select, right-click for actions.");
            setDescription ("Fill recipe " + text);
            repaint();
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
            {
                if (onContext != nullptr)
                    onContext();

                return;
            }

            dragStarted = false;

            if (onDragPrepare != nullptr)
                onDragPrepare();

            juce::Button::mouseDown (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            juce::Button::mouseDrag (e);

            if (! dragStarted && e.mods.isLeftButtonDown() && e.getDistanceFromDragStart() > 8 && onDragOut != nullptr)
            {
                dragStarted = true;
                onDragOut();
            }
        }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            auto b = getLocalBounds().toFloat().reduced (0.5f);
            const auto colour = familyColour (recipe.algo);

            g.setColour (selected ? colour.withAlpha (0.22f) : (highlighted || down ? colours::panelRaised.brighter (0.1f) : colours::panelRaised));
            g.fillRoundedRectangle (b, metrics::controlRadius + 1.0f);
            g.setColour (selected ? colour : colours::edge);
            g.drawRoundedRectangle (b, metrics::controlRadius + 1.0f, selected ? 1.5f : 1.0f);

            auto inner = getLocalBounds().reduced (3, 0);
            auto badge = inner.removeFromLeft (14);

            // The letter is the non-colour cue for the algorithm family.
            g.setColour (colour);
            g.setFont (fonts::label (10.0f));
            g.drawText (juce::String::charToString ((juce::juce_wchar) history::familyLetter (recipe.algo)),
                        badge, juce::Justification::centred, false);

            g.setColour (selected ? colours::text : colours::textMuted);
            g.setFont (fonts::ui (10.0f));
            g.drawFittedText (getButtonText(), inner.withTrimmedRight (locked ? 10 : 0), juce::Justification::centredLeft, 1, 0.6f);

            if (locked)
            {
                g.setColour (colours::accentGreen);
                g.setFont (fonts::label (9.0f));
                g.drawText ("L", getLocalBounds().removeFromRight (12), juce::Justification::centred, false);
            }
        }

        std::function<void()> onContext;
        std::function<void()> onDragPrepare, onDragOut;     // U-2: drag the exported WAV into the DAW
        FillRecipe recipe;

    private:
        bool selected = false, locked = false, dragStarted = false;
    };

    //==========================================================================
    class RecipeDrawer : public juce::Component,
                         private juce::Timer
    {
    public:
        explicit RecipeDrawer (GapFillAudioProcessor& p)
            : proc (p),
              lockToggle (p.getValueTreeState(), "fillLock", "LOCK",
                          "Lock the character of the latest fill: algorithm, band, modifiers and pitch repeat in every "
                          "gap that gets a fill. Fit rules: a locked fill shortens to fit a gap; it is skipped when it "
                          "would be shorter than 60 percent of its minimum length. A forced Fill Type overrides Lock.",
                          colours::accentGreen)
        {
            setInterceptsMouseClicks (false, true);
            setTitle ("Fill recipes");

            handle.setClickingTogglesState (true);
            handle.setWantsKeyboardFocus (false);
            handle.setTitle ("Recipes drawer");
            handle.setTooltip ("Open the last eight fills: audition, lock, repeat the character, save or load a recipe.");
            handle.onClick = [this] { setOpen (handle.getToggleState()); };
            addAndMakeVisible (handle);

            for (int i = 0; i < kChips; ++i)
            {
                chips[(size_t) i].setClickingTogglesState (false);
                chips[(size_t) i].onClick = [this, i] { select (i); };
                chips[(size_t) i].onContext = [this, i] { select (i); showMenu(); };
                chips[(size_t) i].onDragPrepare = [this, i] { select (i); beginExport (ExportPurpose::Drag); };
                chips[(size_t) i].onDragOut = [this] { dragOut(); };
                addChildComponent (chips[(size_t) i]);
            }

            addChildComponent (lockToggle);

            lockLabel.setFont (fonts::mono (9.5f));
            lockLabel.setColour (juce::Label::textColourId, colours::accentGreen);
            lockLabel.setMinimumHorizontalScale (0.7f);
            lockLabel.setInterceptsMouseClicks (false, false);
            addChildComponent (lockLabel);

            setup (auditionBtn, "AUDITION", "Audition this",
                   "Play the selected recipe through the preview voice (or a normal audition when none is selected).",
                   [this] { auditionSelected(); });
            setup (lockThisBtn, "LOCK THIS", "Lock this recipe",
                   "Lock the selected recipe and switch Lock on.", [this] { lockSelected(); });
            setup (likeBtn, "MORE LIKE", "More like this",
                   "Bias the next four fills towards the selected recipe's algorithm family (or the latest fill).",
                   [this] { likeSelected(); });
            setup (saveBtn, "SAVE...", "Save recipe",
                   "Save the selected recipe as a small .gfrecipe file.", [this] { saveSelected(); });
            setup (setABtn, "SET A", "Set recipe A",
                   "Use the selected recipe as slot A of the recipe A/B compare.", [this] { setSlot (0); });
            setup (setBBtn, "SET B", "Set recipe B",
                   "Use the selected recipe as slot B of the recipe A/B compare.", [this] { setSlot (1); });
            setup (abBtn, "A/B", "Audition recipe A or B",
                   "Alternate the audition between recipe A and recipe B. Each recipe's level is measured and the "
                   "same trim as the header A/B (up to 6 dB) keeps them equally loud.", [this] { auditionAB(); });
            setup (matchBtn, "MATCH", "Match levels",
                   "Match levels (header A/B): each side measures the loudness of the wet signal it produces and, once both "
                   "sides have 3 seconds of fills, switching trims the output by up to 6 dB so they sound equally loud. "
                   "The trim is never saved. Off by default.", [this] { toggleMatch(); });
            matchBtn.setClickingTogglesState (true);
            matchBtn.setColour (juce::TextButton::buttonOnColourId, colours::accentGreen);
            matchBtn.setColour (juce::TextButton::textColourOnId, colours::accentGreen);

            abLabel.setFont (fonts::mono (9.5f));
            abLabel.setColour (juce::Label::textColourId, colours::textMuted);
            abLabel.setMinimumHorizontalScale (0.7f);
            abLabel.setInterceptsMouseClicks (false, false);
            addChildComponent (abLabel);

            setup (exportBtn, "EXPORT...", "Export fill",
                   "Save the wet-only audio of the selected fill (or the latest fill or audition) as a 24-bit WAV: "
                   "only what GapFill added, after protect and width, before the dry/wet mix. Drag a chip into your DAW "
                   "to drop the same audio on a track. Fills older than eight seconds are no longer available.",
                   [this] { beginExport (ExportPurpose::File); });
            setup (loadBtn, "LOAD...", "Load recipe",
                   "Load a .gfrecipe file into Lock. Lock is switched off so you decide when to engage it.",
                   [this] { loadRecipe(); });

            heartbeat.poll (proc.getMeterState());
            lastGeneration = proc.getRecipeHistory().getGeneration() - 1;
            startTimerHz (15);
        }

        ~RecipeDrawer() override { stopTimer(); }

        bool hitTest (int x, int y) override
        {
            return handle.getBounds().contains (x, y) || (open && drawerBounds.contains (x, y));
        }

        void paint (juce::Graphics& g) override
        {
            paintStatusLine (g);

            if (! open)
                return;

            juce::Graphics::ScopedSaveState save (g);
            const auto b = drawerBounds.toFloat();
            GapFillLookAndFeel::drawShadow (g, b, metrics::panelRadius);
            g.setColour (colours::panel.withAlpha (0.97f));
            g.fillRoundedRectangle (b, metrics::panelRadius);
            g.setColour (colours::edge);
            g.drawRoundedRectangle (b.reduced (0.5f), metrics::panelRadius, 1.0f);

            if (proc.getRecipeHistory().size() == 0)
            {
                g.setColour (colours::textMuted);
                g.setFont (fonts::ui (11.0f));
                g.drawText ("No fills yet. The last eight fills appear here as soon as GapFill plays one.",
                            chipRow.toNearestInt(), juce::Justification::centred, true);
            }
        }

        void paintStatusLine (juce::Graphics& g)
        {
            if (statusLine.text.isEmpty())
                return;

            // One live message next to the handle (the footer belongs to the WP3 status component).
            const auto font = fonts::mono (9.5f);
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText (font, statusLine.text, 0.0f, 0.0f);
            const auto textWidth = glyphs.getBoundingBox (0, -1, false).getWidth();

            const auto x = (float) handle.getRight() + 8.0f;
            const auto width = juce::jmin (textWidth + 16.0f, (float) getWidth() - x - 12.0f);

            if (width < 40.0f)
                return;

            const juce::Rectangle<float> pill (x, (float) handle.getY(), width, (float) kHandleH);
            g.setColour (colours::background.withAlpha (0.82f));
            g.fillRoundedRectangle (pill, metrics::controlRadius + 2.0f);
            g.setColour (statusLine.warning ? colours::warning : colours::accentGreen);
            g.setFont (font);
            g.drawText (statusLine.text, pill.reduced (8.0f, 0.0f), juce::Justification::centredLeft, true);
        }

        void resized() override
        {
            const auto area = getLocalBounds();
            const auto height = juce::jmin (area.getHeight() - 8, 3 * kRow + 2 * kGap + 2 * kPad);
            drawerBounds = area.reduced (8).removeFromBottom (juce::jmax (0, height));

            if (open)
                handle.setBounds (drawerBounds.getX(), drawerBounds.getY() - kHandleH, kHandleW, kHandleH);
            else
                handle.setBounds (area.getX() + 10, area.getBottom() - 27, kHandleW, kHandleH);

            auto inner = drawerBounds.reduced (kPad);
            chipRow = inner.removeFromTop (kRow).toFloat();
            inner.removeFromTop (kGap);
            auto row2 = inner.removeFromTop (kRow);
            inner.removeFromTop (kGap);
            auto row3 = inner.removeFromTop (kRow);

            // chips: equal widths
            {
                auto r = chipRow.toNearestInt();
                const auto w = (r.getWidth() - kGap * (kChips - 1)) / kChips;

                for (int i = 0; i < kChips; ++i)
                {
                    chips[(size_t) i].setBounds (r.removeFromLeft (w));
                    r.removeFromLeft (kGap);
                }
            }

            // row 2: LOCK | summary (flex) | AUDITION | LOCK THIS | MORE LIKE
            lockToggle.setBounds (row2.removeFromLeft (64));
            row2.removeFromLeft (kGap);
            likeBtn.setBounds (row2.removeFromRight (92));
            row2.removeFromRight (kGap);
            lockThisBtn.setBounds (row2.removeFromRight (84));
            row2.removeFromRight (kGap);
            auditionBtn.setBounds (row2.removeFromRight (76));
            row2.removeFromRight (kGap);
            lockLabel.setBounds (row2);

            // row 3: SAVE | LOAD | ...
            saveBtn.setBounds (row3.removeFromLeft (68));
            row3.removeFromLeft (kGap);
            loadBtn.setBounds (row3.removeFromLeft (68));
            row3.removeFromLeft (kGap);
            exportBtn.setBounds (row3.removeFromLeft (84));
            row3.removeFromLeft (kGap);
            setABtn.setBounds (row3.removeFromLeft (52));
            row3.removeFromLeft (kGap);
            setBBtn.setBounds (row3.removeFromLeft (52));
            row3.removeFromLeft (kGap);
            abBtn.setBounds (row3.removeFromLeft (52));
            row3.removeFromLeft (kGap);
            matchBtn.setBounds (row3.removeFromLeft (64));
            row3.removeFromLeft (kGap);
            abLabel.setBounds (row3);
            extraRowArea = row3;
            layoutExtraRow();
        }

    private:
        void setup (juce::TextButton& b, const juce::String& text, const juce::String& title,
                    const juce::String& tooltip, std::function<void()> onClick)
        {
            b.setButtonText (text);
            b.setTitle (title);
            b.setDescription (tooltip);
            b.setTooltip (tooltip);
            b.setWantsKeyboardFocus (false);
            b.onClick = std::move (onClick);
            addChildComponent (b);
        }

        /** Hook for the export and A/B rows (added by U-2 and F-S06). */
        void layoutExtraRow() {}

        void setOpen (bool shouldOpen)
        {
            open = shouldOpen;
            handle.setToggleState (open, juce::dontSendNotification);
            resized();
            refresh (true);
            repaint();
        }

        //======================================================================
        int selectedIndex() const
        {
            const auto& h = proc.getRecipeHistory();

            for (int i = 0; i < h.size(); ++i)
                if (selectedSeed != 0 && h.get (i).seed == selectedSeed)
                    return i;

            return -1;
        }

        bool hasSelection() const    { return selectedIndex() >= 0; }

        void select (int index)
        {
            const auto& h = proc.getRecipeHistory();
            selectedSeed = index >= 0 && index < h.size() ? h.get (index).seed : 0;
            refresh (true);
        }

        FillRecipe selectedRecipe() const
        {
            const auto i = selectedIndex();
            return i >= 0 ? proc.getRecipeHistory().get (i) : FillRecipe {};
        }

        void timerCallback() override
        {
            heartbeat.poll (proc.getMeterState());
            proc.getRecipes().exportService.arm();         // the wet tap records while an editor with a drawer exists
            pollExport();

            const auto line = status.update();

            if (! (line == statusLine))
            {
                statusLine = line;
                repaint();
            }

            refresh (false);
        }

        void refresh (bool force)
        {
            const auto& h = proc.getRecipeHistory();
            const auto generation = h.getGeneration();
            const auto like = proc.getRecipes().likeRemaining.load (std::memory_order_relaxed);
            const auto summary = lockSummary();
            const auto lockKey = (int) proc.getRecipes().lockMirror.valid * 2
                               + (int) (proc.getValueTreeState().getRawParameterValue ("fillLock")->load() > 0.5f);

            handle.setButtonText ("RECIPES " + juce::String (h.size()));

            const auto abText = abSummary();

            if (! force && generation == lastGeneration && like == lastLike && lockKey == lastLockKey
                  && summary == lastSummary && open == lastOpen && abText == lastAbText)
                return;

            lastAbText = abText;

            lastGeneration = generation;
            lastLike = like;
            lastLockKey = lockKey;
            lastSummary = summary;
            lastOpen = open;

            const auto sel = selectedIndex();

            for (int i = 0; i < kChips; ++i)
            {
                auto& chip = chips[(size_t) i];
                const bool show = open && i < h.size();
                chip.setVisible (show);

                if (show)
                {
                    const auto& r = h.get (i);
                    chip.set (r, i == sel, (r.flags & FillRecipe::flagFromLock) != 0);
                }
            }

            for (auto* c : std::initializer_list<juce::Component*> { &lockToggle, &lockLabel, &auditionBtn, &lockThisBtn,
                                                                     &likeBtn, &saveBtn, &loadBtn, &exportBtn,
                                                                     &setABtn, &setBBtn, &abBtn, &matchBtn, &abLabel })
                c->setVisible (open);

            lockLabel.setText (summary, juce::dontSendNotification);
            lockThisBtn.setEnabled (sel >= 0);
            saveBtn.setEnabled (sel >= 0);
            exportBtn.setEnabled (h.size() > 0 || proc.getRecipes().lastPreviewSpan.isKnown());
            setABtn.setEnabled (sel >= 0);
            setBBtn.setEnabled (sel >= 0);
            abBtn.setEnabled (proc.getRecipes().recipeAB.isReady());
            matchBtn.setToggleState (proc.getRecipes().match.isEnabled(), juce::dontSendNotification);
            abLabel.setText (abSummary(), juce::dontSendNotification);
            likeBtn.setButtonText (like > 0 ? "LIKED x" + juce::String (like) : juce::String ("MORE LIKE"));
            repaint();
        }

        juce::String abSummary() const
        {
            auto& services = proc.getRecipes();
            const auto& ab = services.recipeAB;
            juce::String text;

            for (int slot = 0; slot < 2; ++slot)
            {
                if (ab.hasSlot (slot))
                    text << (slot == 0 ? "A: " : "   B: ") << history::describeRecipe (ab.getSlot (slot), 0.0);
            }

            if (services.match.isEnabled())
            {
                const auto trim = abcompare::trimText (services.match.getTrimDb());
                const auto have = juce::jmin (services.match.getSideSeconds (0), services.match.getSideSeconds (1));
                text << "   " << (trim.isNotEmpty() ? trim : have < abcompare::kMinDataSeconds ? juce::String ("matching: measuring") : juce::String ("0.0 dB match"));
            }

            return text.trim();
        }

        juce::String lockSummary() const
        {
            const auto lockOn = proc.getValueTreeState().getRawParameterValue ("fillLock")->load() > 0.5f;
            const auto& mirror = proc.getRecipes().lockMirror;
            const auto& t = history::strings();

            if (! lockOn)
                return mirror.valid ? "Lock off - kept: " + history::describeRecipe (mirror.recipe, 0.0) : juce::String();

            if (mirror.valid && ! proc.getRecipes().lockArmed.load (std::memory_order_relaxed))
                return juce::String (t.locked) + ": " + history::describeRecipe (mirror.recipe, 0.0);

            return juce::String (t.armed) + ": the next fill will be locked";
        }

        //======================================================================
        // Actions

        void stampedCommand (CommandType type, const FillRecipe* recipe)
        {
            Command c;
            c.type = type;
            c.velocity = 100;
            c.issuedMs = juce::jmax<std::uint32_t> (1u, juce::Time::getMillisecondCounter());

            if (recipe != nullptr)
                c.recipe = *recipe;

            proc.postCommand (c);
        }

        void auditionSelected()
        {
            heartbeat.poll (proc.getMeterState());

            if (! heartbeat.isRunning())
            {
                proc.getRecipes().postMessage ("Start playback to audition");
                return;
            }

            if (hasSelection())
            {
                const auto r = selectedRecipe();
                stampedCommand (CommandType::PreviewRecipe, &r);
            }
            else
            {
                stampedCommand (CommandType::Preview, nullptr);
            }
        }

        void lockSelected()
        {
            if (! hasSelection())
                return;

            const auto r = selectedRecipe();
            stampedCommand (CommandType::LockRecipe, &r);

            // The command is drained before the next block reads fillLock, so the explicit
            // recipe is in place when the parameter engages.
            if (auto* p = proc.getValueTreeState().getParameter ("fillLock"))
                p->setValueNotifyingHost (1.0f);

            proc.getRecipes().postMessage ("Locked: " + history::describeRecipe (r, 0.0));
        }

        void likeSelected()
        {
            if (hasSelection())
            {
                const auto r = selectedRecipe();
                stampedCommand (CommandType::LikeRecipe, &r);
            }
            else
            {
                stampedCommand (CommandType::LikeLatest, nullptr);
            }

            proc.getRecipes().postMessage ("More like that: the next four fills lean towards it");
        }

        void saveSelected()
        {
            if (! hasSelection())
                return;

            const auto r = selectedRecipe();
            auto dir = history::defaultRecipeDirectory();
            dir.createDirectory();
            const auto name = history::sanitiseRecipeName (history::describeRecipe (r, 0.0));

            chooser = std::make_unique<juce::FileChooser> ("Save recipe", dir.getChildFile (name + ".gfrecipe"), "*.gfrecipe");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                    | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [safe = juce::Component::SafePointer<RecipeDrawer> (this), r, name] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();

                if (safe == nullptr || file == juce::File())
                    return;

                file = file.withFileExtension ("gfrecipe");
                juce::String error;

                if (history::writeRecipeFile (file, r, name, error))
                    safe->proc.getRecipes().postMessage ("Saved " + file.getFileName(), 3000);
                else
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "GapFill", error);
            });
        }

        void loadRecipe()
        {
            auto dir = history::defaultRecipeDirectory();
            dir.createDirectory();

            chooser = std::make_unique<juce::FileChooser> ("Load recipe", dir, "*.gfrecipe");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safe = juce::Component::SafePointer<RecipeDrawer> (this)] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();

                if (safe == nullptr || file == juce::File())
                    return;

                const auto loaded = history::readRecipeFile (file);

                if (! loaded.ok)
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "GapFill", loaded.error);
                    return;
                }

                // Armed but off: the user decides when to engage it.
                if (auto* p = safe->proc.getValueTreeState().getParameter ("fillLock"))
                    p->setValueNotifyingHost (0.0f);

                safe->stampedCommand (CommandType::LockRecipe, &loaded.recipe);
                safe->proc.getRecipes().postMessage ("Recipe loaded - switch LOCK on to use it", 3500);
            });
        }

        void setSlot (int slot)
        {
            if (! hasSelection())
                return;

            const auto r = selectedRecipe();
            proc.getRecipes().recipeAB.setSlot (slot, r);
            proc.getRecipes().postMessage (juce::String (slot == 0 ? "A: " : "B: ") + history::describeRecipe (r, 0.0), 2500);
            refresh (true);
        }

        void auditionAB()
        {
            heartbeat.poll (proc.getMeterState());

            if (! heartbeat.isRunning())
            {
                proc.getRecipes().postMessage ("Start playback to audition");
                return;
            }

            Command c;

            if (! proc.getRecipes().recipeAB.next (c, juce::jmax<std::uint32_t> (1u, juce::Time::getMillisecondCounter())))
            {
                proc.getRecipes().postMessage ("Set recipe A and recipe B first (select a chip, then SET A / SET B)", 3500);
                return;
            }

            proc.postCommand (c);
        }

        void toggleMatch()
        {
            proc.getRecipes().match.setEnabled (matchBtn.getToggleState());
            refresh (true);
        }

        void showMenu()
        {
            const auto sel = selectedIndex();

            if (sel < 0)
                return;

            juce::PopupMenu menu;
            menu.addItem (1, "Audition this");
            menu.addItem (2, "Lock this");
            menu.addItem (3, "More like this");
            menu.addItem (4, "Save recipe...");
            menu.addItem (5, "Export fill...");
            menu.addItem (6, "Set as A");
            menu.addItem (7, "Set as B");

            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chips[(size_t) sel]),
                                [safe = juce::Component::SafePointer<RecipeDrawer> (this)] (int result)
            {
                if (safe == nullptr)
                    return;

                if (result == 1) safe->auditionSelected();
                else if (result == 2) safe->lockSelected();
                else if (result == 3) safe->likeSelected();
                else if (result == 4) safe->saveSelected();
                else if (result == 5) safe->beginExport (ExportPurpose::File);
                else if (result == 6) safe->setSlot (0);
                else if (result == 7) safe->setSlot (1);
            });
        }

        //======================================================================
        // U-2 export. The audio thread copies the segment (bounded per block); everything
        // below, including the file I/O, runs here on the message thread.

        enum class ExportPurpose { File, Drag };

        struct PendingExport
        {
            bool active = false;
            ExportPurpose purpose = ExportPurpose::File;
            std::uint32_t deadlineMs = 0;
            juce::String algo;
        };

        /** The span to export: the selected fill, else the more recent of the latest fill and audition. */
        bool chooseExportSpan (FillSpan& span, juce::String& algoLabel) const
        {
            auto& services = proc.getRecipes();
            const auto& h = proc.getRecipeHistory();
            const auto sel = selectedIndex();

            if (sel >= 0)
            {
                span = h.getSpan (sel);
                algoLabel = algoName ((Algo) juce::jlimit (0, numAlgos - 1, (int) h.get (sel).algo));
                return true;
            }

            const bool haveFill = h.size() > 0 && h.getSpan (0).isKnown();
            const bool havePreview = services.lastPreviewSpan.isKnown();

            if (havePreview && (! haveFill || services.lastPreviewSpan.startPos > h.getSpan (0).startPos))
            {
                span = services.lastPreviewSpan;
                algoLabel = algoName ((Algo) juce::jlimit (0, numAlgos - 1, (int) services.lastPreviewRecipe.algo));
                return true;
            }

            if (haveFill)
            {
                span = h.getSpan (0);
                algoLabel = algoName ((Algo) juce::jlimit (0, numAlgos - 1, (int) h.get (0).algo));
                return true;
            }

            return false;
        }

        void beginExport (ExportPurpose purpose)
        {
            auto& services = proc.getRecipes();
            FillSpan span;
            juce::String algo;

            if (! chooseExportSpan (span, algo) || ! span.isKnown())
            {
                services.postMessage ("That fill is too old", 3000);
                return;
            }

            services.exportService.arm();
            const auto result = services.exportService.request (span);

            if (result != exporting::RequestResult::Accepted)
            {
                services.postMessage (exporting::requestText (result), 3000);
                return;
            }

            pending.active = true;
            pending.purpose = purpose;
            pending.algo = algo;
            pending.deadlineMs = juce::Time::getMillisecondCounter()
                               + (std::uint32_t) (1000.0 * (double) span.lengthSamples / juce::jmax (1.0, services.exportService.getSampleRate())) + 3000u;
        }

        void pollExport()
        {
            if (! pending.active)
                return;

            auto& service = proc.getRecipes().exportService;
            exporting::Failure why = exporting::Failure::None;

            if (service.take (exported, why))
            {
                pending.active = false;
                finishExport();
            }
            else if (why != exporting::Failure::None)
            {
                pending.active = false;
                proc.getRecipes().postMessage (exporting::failureText (why), 3500);
            }
            else if ((std::int32_t) (juce::Time::getMillisecondCounter() - pending.deadlineMs) > 0)
            {
                service.cancelPending();
                pending.active = false;
                proc.getRecipes().postMessage ("Export timed out - is audio playing?", 3500);
            }
        }

        void finishExport()
        {
            auto& services = proc.getRecipes();
            const auto sr = services.exportService.getSampleRate();
            const auto name = exporting::defaultFileName (pending.algo, services.exportService.bpm.load(), juce::Time::getCurrentTime());

            if (pending.purpose == ExportPurpose::Drag)
            {
                dragFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("GapFill").getChildFile (name);
                juce::String error;

                if (! exporting::writeWav24 (dragFile, exported, sr, error))
                {
                    dragFile = juce::File();
                    services.postMessage (error, 4000);
                }

                return;
            }

            auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GapFill").getChildFile ("Exports");
            dir.createDirectory();

            chooser = std::make_unique<juce::FileChooser> ("Export fill", dir.getChildFile (name), "*.wav");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                    | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [safe = juce::Component::SafePointer<RecipeDrawer> (this), sr] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();

                if (safe == nullptr || file == juce::File())
                    return;

                file = file.withFileExtension ("wav");
                juce::String error;

                if (exporting::writeWav24 (file, safe->exported, sr, error))
                    safe->proc.getRecipes().postMessage ("Exported " + file.getFileName(), 3500);
                else
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "GapFill", error);
            });
        }

        void dragOut()
        {
            if (dragFile.existsAsFile())
                juce::DragAndDropContainer::performExternalDragDropOfFiles ({ dragFile.getFullPathName() }, false, this);
        }

        //======================================================================
        GapFillAudioProcessor& proc;
        recipes::RecipeStatus status { proc };
        recipes::RecipeStatus::Line statusLine;
        audition::Heartbeat heartbeat;
        PendingExport pending;
        juce::AudioBuffer<float> exported;
        juce::File dragFile;

        juce::TextButton handle { "RECIPES" };
        std::array<ChipButton, (size_t) kChips> chips;
        GapToggle lockToggle;
        juce::Label lockLabel;
        juce::TextButton auditionBtn, lockThisBtn, likeBtn, saveBtn, loadBtn, exportBtn, setABtn, setBBtn, abBtn, matchBtn;
        juce::Label abLabel;

        juce::Rectangle<int> drawerBounds, extraRowArea;
        juce::Rectangle<float> chipRow;
        bool open = false;
        std::uint32_t selectedSeed = 0;

        std::uint32_t lastGeneration = 0;
        int lastLike = -1, lastLockKey = -1;
        juce::String lastSummary, lastAbText;
        bool lastOpen = false;

        std::unique_ptr<juce::FileChooser> chooser;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecipeDrawer)
    };

    // Order 200: after the WP3 legend / timeline overlay (100). SlotHost gives every VisualiserOverlay
    // component the full rectangle, so both draw nothing but their own controls.
    const SlotRegistrar recipeDrawer ({ Slot::VisualiserOverlay, 200,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<RecipeDrawer> (proc);
        } });
}

} // namespace gapfill
