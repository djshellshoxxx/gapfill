// F-L07 / N-VAL: a paged setlist, message-thread editing and bounded bank exchange.
#include "../Knob.h"
#include "../HelpRegistry.h"
#include "../SlotHost.h"
#include "../../PluginProcessor.h"
#include "../../features/bank/BankFile.h"
#include "../../features/i18n/ComponentTranslator.h"
#include "../../features/i18n/Strings.h"

namespace gapfill
{
namespace
{
    class SceneSlotButton final : public juce::TextButton
    {
    public:
        std::function<void()> onMenu, onStore, onDelete;
        std::function<void(int)> onMoveFocus;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) { if (onMenu) onMenu(); }
            else juce::TextButton::mouseDown (e);
        }
        bool keyPressed (const juce::KeyPress& key) override
        {
            const auto code = key.getKeyCode();
            if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey
                || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
            {
                if (onMoveFocus) onMoveFocus (code == juce::KeyPress::leftKey ? -1 : code == juce::KeyPress::rightKey ? 1
                                             : code == juce::KeyPress::upKey ? -4 : 4);
                return true;
            }
            if (key.getKeyCode() == juce::KeyPress::F10Key && key.getModifiers().isShiftDown())
            { if (onMenu) onMenu(); return true; }
            if (key.getKeyCode() == juce::KeyPress::returnKey && key.getModifiers().isShiftDown())
            { if (onStore) onStore(); return true; }
            if (key.getKeyCode() == juce::KeyPress::deleteKey)
            { if (onDelete) onDelete(); return true; }
            return juce::TextButton::keyPressed (key);
        }
    };

    class ScenePanel final : public juce::Component, private juce::Timer
    {
    public:
        explicit ScenePanel (GapFillAudioProcessor& p) : proc (p)
        {
            setTitle ("Scenes and banks");
            bankName.setTitle ("Bank name");
            bankName.setTooltip ("Double-click to rename this bank.");
            bankName.setEditable (false, true, false);
            i18n::markUserContent (bankName);
            bankName.onTextChange = [this]
            {
                if (! updating) { scenes().getBank().setName (bankName.getText()); rebuild(); }
            };
            addAndMakeVisible (bankName);
            configure (pagePrev, "< PAGE", "Show the previous eight scenes.", [this] { page = juce::jmax (0, page - 1); rebuild(); });
            configure (pageNext, "PAGE >", "Show the next eight scenes.", [this] { page = juce::jmin (7, page + 1); rebuild(); });
            pageText.setTitle ("Scene page");
            pageText.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (pageText);
            for (int slot = 0; slot < 8; ++slot)
            {
                auto& button = slots[slot];
                i18n::markUserContent (button);
                button.onClick = [this, slot]
                {
                    const int index = page * 8 + slot;
                    if (index < scenes().getBank().size()) { selected = index; scenes().gotoScene (index); rebuild(); }
                    else if (index == scenes().getBank().size()) storeAt (index);
                };
                button.onMenu = [this, slot] { showSceneMenu (page * 8 + slot, slots[slot]); };
                button.onStore = [this, slot] { storeAt (page * 8 + slot); };
                button.onDelete = [this, slot] { deleteScene (page * 8 + slot); };
                button.onMoveFocus = [this, slot] (int delta)
                {
                    const int target = juce::jlimit (0, juce::jmin (63, scenes().getBank().size()), page * 8 + slot + delta);
                    selected = target < scenes().getBank().size() ? target : -1;
                    page = target / 8;
                    rebuild();
                    slots[target % 8].grabKeyboardFocus();
                };
                addAndMakeVisible (button);
            }
            sceneBox.setTitle ("Scene");
            i18n::markUserContent (sceneBox);
            sceneBox.setTooltip ("Recall any scene in the bank at the selected quantise boundary.");
            sceneBox.onChange = [this]
            {
                const int id = sceneBox.getSelectedId();
                if (! updating && id > 0) { selected = id - 1; page = selected / 8; scenes().gotoScene (selected); rebuild(); }
            };
            addAndMakeVisible (sceneBox);
            quantBox.setTitle ("Scene quantise");
            quantBox.setTooltip ("When a scene lands: immediately, next beat, bar or phrase.");
            quantBox.addItemList ({ "Immediate", "Beat", "Bar", "Phrase" }, 1);
            quantBox.onChange = [this]
            { if (! updating) scenes().getBank().options.quant = (scenes::Quant) juce::jlimit (1, 4, quantBox.getSelectedId()); };
            addAndMakeVisible (quantBox);
            configure (prev, "PREV", "Recall the previous scene.", [this] { scenes().prev(); });
            configure (next, "NEXT", "Recall the next scene.", [this] { scenes().next(); });
            configure (cancel, "CANCEL", "Cancel the pending recall (also Escape).", [this] { scenes().cancel(); });
            configure (store, "STORE", "Overwrite the selected scene with the current sound.", [this] { storeAt (selected); });
            configure (add, "NEW", "Append the current sound as a new scene.", [this] { storeAt (scenes().getBank().size()); });
            configure (bankBtn, "BANK...", "Rename, duplicate, move or delete the selected scene; bank options.",
                       [this] { showSceneMenu (selected, bankBtn); });
            configure (exportBtn, "EXPORT", "Export the bank or selected scene, optionally with macro mappings.", [this] { exportMenu(); });
            configure (importBtn, "IMPORT", "Preview a bank before replacing, adding or merging its scenes.", [this] { importMenu(); });
            configure (undoBtn, "UNDO IMPORT", "Undo the previous bank import, including mappings if imported.",
                       [this] { scenes().undoImport(); selected = scenes().getCurrentIndex(); rebuild(); setStatus ("Import undone"); });
            status.setFont (fonts::ui (11));
            status.setColour (juce::Label::textColourId, colours::textMuted);
            status.setMinimumHorizontalScale (.65f);
            addAndMakeVisible (status);
            setSize (400, 190);
            rebuild();
            startTimerHz (5);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto header = r.removeFromTop (26);
            pageNext.setBounds (header.removeFromRight (65).reduced (1));
            pageText.setBounds (header.removeFromRight (58));
            pagePrev.setBounds (header.removeFromRight (65).reduced (1));
            bankName.setBounds (header);
            for (int row = 0; row < 2; ++row)
            {
                auto line = r.removeFromTop (28);
                for (int column = 0; column < 4; ++column)
                    slots[row * 4 + column].setBounds (line.removeFromLeft (line.getWidth() / (4 - column)).reduced (1));
            }
            auto navigation = r.removeFromTop (28);
            sceneBox.setBounds (navigation.removeFromLeft (juce::jmax (100, navigation.getWidth() / 3)).reduced (1));
            quantBox.setBounds (navigation.removeFromLeft (90).reduced (1));
            layoutButtons (navigation, { &prev, &next, &cancel });
            layoutButtons (r.removeFromTop (28), { &store, &add, &bankBtn });
            layoutButtons (r.removeFromTop (28), { &exportBtn, &importBtn, &undoBtn });
            status.setBounds (r.removeFromTop (22));
        }

        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key.getKeyCode() == juce::KeyPress::escapeKey) { scenes().cancel(); return true; }
            return juce::Component::keyPressed (key);
        }

    private:
        using Safe = juce::Component::SafePointer<ScenePanel>;
        scenes::SceneController& scenes() { return proc.getLiveControl().scenes(); }
        const scenes::SceneController& scenes() const { return proc.getLiveControl().scenes(); }
        void configure (juce::TextButton& b, const juce::String& text, const juce::String& tip, std::function<void()> action)
        {
            b.setButtonText (text); b.setTitle (text); b.setTooltip (tip); b.onClick = std::move (action); addAndMakeVisible (b);
        }
        static void layoutButtons (juce::Rectangle<int> line, std::initializer_list<juce::Button*> buttons)
        {
            int remaining = (int) buttons.size();
            for (auto* button : buttons) button->setBounds (line.removeFromLeft (line.getWidth() / remaining--).reduced (1));
        }
        void setStatus (const juce::String& message)
        { transientStatus = i18n::text (message); statusUntil = juce::Time::getMillisecondCounterHiRes() + 8000; status.setText (transientStatus, juce::dontSendNotification); }
        bool validIndex (int index) const { return index >= 0 && index < scenes().getBank().size(); }
        void rebuild()
        {
            const juce::ScopedValueSetter<bool> guard (updating, true);
            const auto& bank = scenes().getBank();
            if (selected >= bank.size()) selected = bank.size() - 1;
            bankName.setText (bank.getName().isEmpty() ? i18n::text ("Untitled bank") : bank.getName(), juce::dontSendNotification);
            sceneBox.clear (juce::dontSendNotification);
            for (int i = 0; i < bank.size(); ++i) sceneBox.addItem (juce::String (i + 1) + "  " + bank.at (i).name, i + 1);
            sceneBox.setTextWhenNothingSelected (bank.size() == 0 ? i18n::text ("No scenes - press NEW") : i18n::text ("Scene..."));
            if (validIndex (selected)) sceneBox.setSelectedId (selected + 1, juce::dontSendNotification);
            quantBox.setSelectedId ((int) bank.options.quant, juce::dontSendNotification);
            pageText.setText (juce::String (page + 1) + " / 8", juce::dontSendNotification);
            pagePrev.setEnabled (page > 0); pageNext.setEnabled (page < 7);
            shownNames = namesHash(); shownName = bank.getName(); shownCurrent = scenes().getCurrentIndex(); shownLanguage = i18n::languageGeneration();
            refreshButtons();
        }
        void refreshButtons()
        {
            const auto& bank = scenes().getBank();
            for (int slot = 0; slot < 8; ++slot)
            {
                const int index = page * 8 + slot;
                const bool exists = index < bank.size();
                const bool pending = index == scenes().getPendingIndex();
                const bool current = index == scenes().getCurrentIndex();
                auto text = i18n::format ("{0}  {1}", { juce::String (index + 1), exists ? bank.at (index).name : i18n::text ("Empty") });
                if (pending) text = i18n::format ("NEXT {0}", { text });
                else if (current) text = i18n::format ("> {0}", { text });
                slots[slot].setButtonText (text);
                const auto number = juce::String (index + 1);
                slots[slot].setTitle (! exists ? i18n::format ("Scene {0}, empty", { number })
                    : pending ? i18n::format ("Scene {0}, {1}, stored, pending", { number, bank.at (index).name })
                    : current ? i18n::format ("Scene {0}, {1}, stored, active", { number, bank.at (index).name })
                              : i18n::format ("Scene {0}, {1}, stored", { number, bank.at (index).name }));
                slots[slot].setTooltip (exists ? "Recall; right-click or Shift+F10 for scene actions. Shift+Enter stores; Delete removes."
                                               : "Store the current sound in the next empty scene.");
                slots[slot].setColour (juce::TextButton::buttonColourId, current ? colours::primary : colours::panel);
                slots[slot].setEnabled (exists || (index == bank.size() && index < scenes::kMaxScenes));
            }
            prev.setEnabled (bank.size() > 0); next.setEnabled (bank.size() > 0);
            store.setEnabled (validIndex (selected)); add.setEnabled (bank.size() < scenes::kMaxScenes);
            exportBtn.setEnabled (bank.size() > 0); cancel.setEnabled (scenes().getPendingIndex() >= 0);
            undoBtn.setEnabled (scenes().canUndoImport());
        }
        std::uint64_t namesHash() const
        {
            std::uint64_t hash = (std::uint64_t) scenes().getBank().size();
            for (int i = 0; i < scenes().getBank().size(); ++i)
                hash = hash * 1099511628211ull ^ (std::uint64_t) scenes().getBank().at (i).name.hashCode64();
            return hash;
        }
        void timerCallback() override
        {
            const auto& bank = scenes().getBank();
            if (shownNames != namesHash() || shownName != bank.getName() || shownCurrent != scenes().getCurrentIndex()
                || shownLanguage != i18n::languageGeneration())
            {
                if (scenes().getCurrentIndex() != shownCurrent && scenes().getCurrentIndex() >= 0)
                { selected = scenes().getCurrentIndex(); page = selected / 8; }
                rebuild();
            }
            else refreshButtons();
            auto message = scenes().getStatus();
            if (scenes().getPendingIndex() >= 0) message = i18n::format ("Next: scene {0} (waiting for boundary)", { juce::String (scenes().getPendingIndex() + 1) });
            else if (juce::Time::getMillisecondCounterHiRes() < statusUntil) message = transientStatus;
            if (message != status.getText()) status.setText (message, juce::dontSendNotification);
        }
        bool unchangedScene (int index, const scenes::Scene& expected)
        {
            if (validIndex (index) && scenes().getBank().at (index) == expected) return true;
            setStatus ("The scene changed while the dialog was open; action cancelled.");
            return false;
        }
        void storeAt (int index)
        {
            if (index < 0 || index > scenes().getBank().size() || index >= scenes::kMaxScenes) return;
            const auto name = validIndex (index) ? scenes().getBank().at (index).name : "Scene " + juce::String (index + 1);
            const auto scope = scopeSelectionFor == index || scopeSelectionFor == -1 ? captureScope
                : validIndex (index) ? scenes().getBank().at (index).scope : scenes::kDefaultScope;
            if (! validIndex (index))
            { scenes().saveCurrent (index, name, scope); selected = index; page = index / 8; rebuild(); return; }
            const auto expected = scenes().getBank().at (index);
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                .withTitle (i18n::text ("Overwrite scene")).withMessage (i18n::format ("Replace the stored sound in {0}?", { name }))
                .withButton ("Overwrite").withButton (i18n::text ("Cancel")).withAssociatedComponent (this),
                [safe = Safe (this), index, name, expected, scope] (int result)
                { if (safe != nullptr && result == 1 && safe->unchangedScene (index, expected)) { safe->scenes().saveCurrent (index, name, scope); safe->selected = index; safe->rebuild(); } });
        }
        void renameScene (int index)
        {
            if (! validIndex (index)) return;
            const auto expected = scenes().getBank().at (index);
            auto* window = new juce::AlertWindow (i18n::text ("Rename scene"), i18n::text ("Scene name (up to 32 characters)"), juce::MessageBoxIconType::NoIcon, this);
            window->addTextEditor ("name", scenes().getBank().at (index).name);
            window->addButton (i18n::text ("Rename"), 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton (i18n::text ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            window->enterModalState (true, juce::ModalCallbackFunction::create (
                [safe = Safe (this), dialog = juce::Component::SafePointer<juce::AlertWindow> (window), index, expected] (int result)
                { if (safe != nullptr && dialog != nullptr && result == 1 && safe->unchangedScene (index, expected)) { safe->scenes().renameScene (index, dialog->getTextEditorContents ("name")); safe->rebuild(); } }), true);
        }
        void deleteScene (int index)
        {
            if (! validIndex (index)) return;
            const auto expected = scenes().getBank().at (index);
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle (i18n::text ("Delete scene")).withMessage (i18n::format ("Delete {0}?", { scenes().getBank().at (index).name }))
                .withButton (i18n::text ("Delete")).withButton (i18n::text ("Cancel")).withAssociatedComponent (this),
                [safe = Safe (this), index, expected] (int result)
                { if (safe != nullptr && result == 1 && safe->unchangedScene (index, expected)) { safe->scenes().removeScene (index); safe->rebuild(); } });
        }
        void showSceneMenu (int index, juce::Component& target)
        {
            juce::PopupMenu menu;
            const bool exists = validIndex (index);
            const auto expected = exists ? scenes().getBank().at (index) : scenes::Scene {};
            menu.addItem (1, "Recall scene", exists); menu.addItem (2, "Store current sound here", exists);
            menu.addItem (3, "Rename scene...", exists); menu.addItem (4, "Duplicate scene", exists && scenes().getBank().size() < scenes::kMaxScenes);
            menu.addItem (5, "Move up", exists && index > 0); menu.addItem (6, "Move down", exists && index + 1 < scenes().getBank().size());
            menu.addItem (7, "Delete scene...", exists); menu.addSeparator();
            menu.addItem (8, "Wrap next/previous", true, scenes().getBank().options.wrap);
            menu.addItem (9, "Apply now without transport", true, scenes().getBank().options.immediateWithoutTransport);
            juce::PopupMenu scopeMenu;
            constexpr const char* scopeLabels[] = { "Creative", "Weights", "Effects", "Musical", "Macro values", "Detector", "Protection", "Bands" };
            const auto scope = scopeSelectionFor == index ? captureScope : exists ? scenes().getBank().at (index).scope : scenes::kDefaultScope;
            for (int bit = 0; bit < 8; ++bit) scopeMenu.addItem (100 + bit, scopeLabels[bit], true, (scope & (1u << bit)) != 0);
            menu.addSubMenu ("Scope for next STORE", scopeMenu);
            juce::PopupMenu sceneQuant;
            constexpr const char* quantLabels[] = { "Inherit bank quantise", "Immediate", "Beat", "Bar", "Phrase" };
            for (int quant = 0; quant < 5; ++quant)
                sceneQuant.addItem (200 + quant, quantLabels[quant], exists, exists && (int) scenes().getBank().at (index).quant == quant);
            menu.addSubMenu ("Scene quantise override", sceneQuant, exists);
            i18n::translateMenu (menu);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [safe = Safe (this), index, expected] (int choice)
            {
                if (safe == nullptr) return;
                auto& panel = *safe;
                if (choice >= 1 && choice <= 7 && ! panel.unchangedScene (index, expected)) return;
                if (choice >= 100 && choice < 108)
                {
                    const auto current = panel.scopeSelectionFor == index ? panel.captureScope
                        : panel.validIndex (index) ? panel.scenes().getBank().at (index).scope : scenes::kDefaultScope;
                    panel.captureScope = (std::uint16_t) (current ^ (1u << (choice - 100)));
                    panel.scopeSelectionFor = index;
                    panel.setStatus ("Scope changed for the next STORE; stored scenes are unchanged.");
                }
                if (choice >= 200 && choice < 205 && panel.unchangedScene (index, expected))
                    panel.scenes().setSceneQuant (index, (scenes::Quant) (choice - 200));
                switch (choice)
                {
                    case 1: panel.selected = index; panel.scenes().gotoScene (index); break;
                    case 2: panel.storeAt (index); break; case 3: panel.renameScene (index); break;
                    case 4: panel.scenes().duplicateScene (index); break;
                    case 5: if (panel.scenes().moveScene (index, index - 1)) panel.selected = index - 1; break;
                    case 6: if (panel.scenes().moveScene (index, index + 1)) panel.selected = index + 1; break;
                    case 7: panel.deleteScene (index); break;
                    case 8: panel.scenes().getBank().options.wrap = ! panel.scenes().getBank().options.wrap; break;
                    case 9: panel.scenes().getBank().options.immediateWithoutTransport = ! panel.scenes().getBank().options.immediateWithoutTransport; break;
                    default: break;
                }
                panel.rebuild();
            });
        }
        void exportMenu()
        {
            juce::PopupMenu menu;
            menu.addItem (1, "Whole bank"); menu.addItem (2, "Selected scene", validIndex (selected));
            menu.addItem (3, "Whole bank with macro mappings"); menu.addItem (4, "Selected scene with macro mappings", validIndex (selected));
            i18n::translateMenu (menu);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (exportBtn), [safe = Safe (this)] (int choice)
            { if (safe != nullptr && choice > 0) safe->chooseExport (choice == 2 || choice == 4, choice >= 3); });
        }
        void chooseExport (bool selectedOnly, bool withMacros)
        {
            bank::ExportOptions options;
            if (selectedOnly && validIndex (selected)) options.selection.push_back (selected);
            options.includeMacros = withMacros;
            if (withMacros) options.macros = proc.getLiveControl().macroMappings().toValueTree();
            const auto text = bank::exportToString (scenes().getBank(), options, versionString(), juce::Time::getCurrentTime().toISO8601 (true));
            if (text.isEmpty()) { setStatus ("Export failed: invalid or oversized bank; no file was written."); return; }
            chooser = std::make_unique<juce::FileChooser> (i18n::text ("Export scene bank"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GapFill bank.gapbank"), "*.gapbank");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                [safe = Safe (this), text] (const juce::FileChooser& dialog)
                {
                    if (safe == nullptr || dialog.getResult() == juce::File()) return;
                    const auto file = dialog.getResult().withFileExtension ("gapbank");
                    if (file.existsAsFile())
                    {
                        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                            .withTitle (i18n::text ("Replace bank file")).withMessage (i18n::format ("Replace {0}?", { file.getFileName() }))
                            .withButton (i18n::text ("Replace")).withButton (i18n::text ("Cancel")).withAssociatedComponent (safe),
                            [safe, file, text] (int result) { if (safe != nullptr && result == 1) safe->writeExport (file, text, true); });
                    }
                    else safe->writeExport (file, text, false);
                });
        }
        void writeExport (const juce::File& file, const juce::String& text, bool allowOverwrite)
        {
            juce::String error;
            if (! bank::writeFile (file, text, allowOverwrite, error)) setStatus (i18n::format ("Export failed: {0}", { i18n::text (error) }));
            else setStatus ("Scene bank exported");
        }
        void importMenu()
        {
            juce::PopupMenu menu;
            menu.addItem (1, "Replace the bank"); menu.addItem (2, "Add to the bank"); menu.addItem (3, "Merge by name");
            i18n::translateMenu (menu);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (importBtn), [safe = Safe (this)] (int choice)
            { if (safe != nullptr && choice > 0) safe->chooseImport (choice == 1 ? bank::ImportMode::Replace : choice == 2 ? bank::ImportMode::Append : bank::ImportMode::MergeByName); });
        }
        void chooseImport (bank::ImportMode mode)
        {
            chooser = std::make_unique<juce::FileChooser> (i18n::text ("Import scene bank"), juce::File(), "*.gapbank");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [safe = Safe (this), mode] (const juce::FileChooser& dialog)
                {
                    if (safe == nullptr || dialog.getResult() == juce::File()) return;
                    auto report = bank::parseFile (dialog.getResult());
                    if (report.status != bank::ParseStatus::Ok) { safe->setStatus (report.message); return; }
                    safe->previewImport (std::move (report), mode);
                });
        }
        void previewImport (bank::ImportReport report, bank::ImportMode mode)
        {
            juce::String message = i18n::format ("{0} valid scenes, {1} skipped.", { juce::String ((int) report.accepted.size()), juce::String ((int) report.skipped.size()) }) + "\n";
            if (report.unknownIds > 0) message << i18n::format ("{0} unknown parameter IDs ignored.", { juce::String (report.unknownIds) }) << '\n';
            if (report.digestMismatch) message << i18n::text ("WARNING: the file's integrity digest does not match its contents.") << '\n';
            for (const auto& skipped : report.skipped) message << i18n::format ("{0}: {1}", { skipped.name, i18n::text (skipped.reason) }) << '\n';
            if (report.macros.isValid()) message << i18n::text ("This bank also contains macro mappings. Importing them can change active macros.") << '\n';
            const bool hasMappings = report.macros.isValid();
            auto* window = new juce::AlertWindow (i18n::text ("Review scene bank import"), i18n::text ("Review the complete report before importing."),
                report.digestMismatch ? juce::MessageBoxIconType::WarningIcon : juce::MessageBoxIconType::QuestionIcon, this);
            window->addTextBlock (message);  // JUCE's read-only multiline block has scrollbars.
            window->addButton (i18n::text ("Scenes only"), 1);
            if (hasMappings) window->addButton (i18n::text ("Scenes + mappings"), 2);
            window->addButton (i18n::text ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            window->enterModalState (true, juce::ModalCallbackFunction::create (
                [safe = Safe (this), report = std::move (report), mode, hasMappings] (int result)
            {
                if (safe == nullptr || result <= 0 || (! hasMappings && result != 1)) return;
                auto& panel = *safe;
                auto& mappings = panel.proc.getLiveControl().macroMappings();
                const auto before = mappings.toValueTree();
                const auto applied = bank::applyImport (panel.scenes(), report, mode);
                if (applied.ok && hasMappings && result == 2)
                {
                    mappings.restore (report.macros);
                    panel.scenes().setImportUndoAction ([model = &mappings, before] { model->restore (before); });
                }
                panel.selected = panel.scenes().getCurrentIndex();
                panel.rebuild(); panel.setStatus (applied.message);
            }), true);
        }

        GapFillAudioProcessor& proc;
        juce::Label bankName, pageText, status;
        SceneSlotButton slots[8];
        juce::ComboBox sceneBox, quantBox;
        juce::TextButton pagePrev, pageNext, prev, next, cancel, store, add, bankBtn, exportBtn, importBtn, undoBtn;
        std::unique_ptr<juce::FileChooser> chooser;
        juce::String shownName, transientStatus;
        std::uint64_t shownNames = 0, shownLanguage = 0;
        double statusUntil = 0;
        int page = 0, selected = -1, shownCurrent = -2, scopeSelectionFor = -2;
        std::uint16_t captureScope = scenes::kDefaultScope;
        bool updating = false;
    };
    const SlotRegistrar scenePanel ({ Slot::PerformanceTab, 200,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component> { return std::make_unique<ScenePanel> (proc); } });
    const HelpRegistrar helpScenePanel (6220, "Scenes and banks", [] (HelpWriter& w)
    {
        w.body ("Eight scene buttons are shown per page, across eight pages. Pick a stored scene to recall it at the quantise boundary. "
                "NEXT marks a pending recall; > marks the active scene. CANCEL or Escape cancels pending recall. "
                "NEW appends the current sound; STORE overwrites with confirmation. Double-click the bank name to rename the bank.");
        w.body ("BANK or a scene's right-click menu provides rename, duplicate, move and delete, wrap and no-transport options. "
                "Scope for next STORE chooses the groups captured when you next store this scene; existing scenes keep their scope until stored. "
                "Shift+F10 opens scene actions, Shift+Enter stores, Delete asks before removal. "
                "EXPORT supports the selected scene or whole bank and optional macro mappings. IMPORT previews accepted/skipped scenes "
                "and integrity warnings before asking whether to include mappings. UNDO IMPORT restores the bank and any imported mappings.");
    });
}
} // namespace gapfill
