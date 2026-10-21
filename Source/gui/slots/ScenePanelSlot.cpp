// F-L07 / N-VAL scene and bank panel (docs/dev/12, docs/dev/11): pick, step, store and quantise scenes,
// export and import a .gapbank file. Registered in Slot::PerformanceTab (Stack; fixed height).
//
// All state lives in live::LiveControl::scenes(); this component only calls its message-thread API.

#include "../Knob.h"
#include "../HelpRegistry.h"
#include "../SlotHost.h"
#include "../../PluginProcessor.h"
#include "../../features/bank/BankFile.h"

namespace gapfill
{

namespace
{
    class ScenePanel : public juce::Component, private juce::Timer
    {
    public:
        explicit ScenePanel (GapFillAudioProcessor& p) : proc (p)
        {
            setTitle ("Scenes and banks");

            sceneBox.setTitle ("Scene");
            sceneBox.setTooltip ("Pick a scene to recall. The change lands on the quantise boundary.");
            sceneBox.onChange = [this]
            {
                const int id = sceneBox.getSelectedId();
                if (! updating && id > 0)
                    scenes().gotoScene (id - 1);
            };

            quantBox.setTitle ("Scene quantise");
            quantBox.setTooltip ("When a scene change lands: right away, on the next beat, bar or phrase.");
            quantBox.addItem ("Immediate", 1);
            quantBox.addItem ("Beat", 2);
            quantBox.addItem ("Bar", 3);
            quantBox.addItem ("Phrase", 4);
            quantBox.onChange = [this]
            {
                if (! updating)
                    scenes().getBank().options.quant = quantFromId (quantBox.getSelectedId());
            };

            configure (prev, "PREV", "Recall the previous scene.", [this] { scenes().prev(); });
            configure (next, "NEXT", "Recall the next scene.", [this] { scenes().next(); });
            configure (cancel, "CANCEL", "Cancel a pending scene change.", [this] { scenes().cancel(); });
            configure (store, "STORE", "Overwrite the selected scene with the current settings.",
                       [this]
                       {
                           const int i = scenes().getCurrentIndex();
                           if (i >= 0 && i < scenes().getBank().size())
                               scenes().saveCurrent (i, scenes().getBank().at (i).name);
                           rebuild();
                       });
            configure (add, "NEW", "Store the current settings as a new scene.",
                       [this]
                       {
                           const int n = scenes().getBank().size();
                           scenes().saveCurrent (n, "Scene " + juce::String (n + 1));
                           rebuild();
                       });
            configure (exportBtn, "EXPORT", "Save the bank to a .gapbank file you can share.", [this] { doExport(); });
            configure (importBtn, "IMPORT", "Load scenes from a .gapbank file. Nothing changes until you recall a scene.",
                       [this] { doImport(); });
            configure (undoBtn, "UNDO IMPORT", "Restore the bank as it was before the last import.",
                       [this] { scenes().undoImport(); rebuild(); });

            status.setFont (fonts::ui (11.0f));
            status.setColour (juce::Label::textColourId, colours::textMuted);
            status.setMinimumHorizontalScale (0.8f);
            addAndMakeVisible (status);
            addAndMakeVisible (sceneBox);
            addAndMakeVisible (quantBox);

            setSize (400, 112);
            rebuild();
            startTimerHz (5);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto top = r.removeFromTop (28);
            sceneBox.setBounds (top.removeFromLeft (juce::jmax (120, top.getWidth() / 3)));
            top.removeFromLeft (4);
            quantBox.setBounds (top.removeFromLeft (96));
            top.removeFromLeft (4);
            layoutButtons (top, { &prev, &next, &cancel });

            r.removeFromTop (4);
            auto mid = r.removeFromTop (28);
            layoutButtons (mid, { &store, &add, &exportBtn, &importBtn, &undoBtn });
            r.removeFromTop (4);
            status.setBounds (r.removeFromTop (18));
        }

    private:
        scenes::SceneController& scenes() { return proc.getLiveControl().scenes(); }
        const scenes::SceneController& scenes() const { return proc.getLiveControl().scenes(); }

        static scenes::Quant quantFromId (int id)
        {
            switch (id) { case 1: return scenes::Quant::Immediate; case 2: return scenes::Quant::Beat;
                          case 4: return scenes::Quant::Phrase; default: return scenes::Quant::Bar; }
        }

        static int idFromQuant (scenes::Quant q)
        {
            switch (q) { case scenes::Quant::Immediate: return 1; case scenes::Quant::Beat: return 2;
                         case scenes::Quant::Phrase: return 4; default: return 3; }
        }

        void configure (juce::TextButton& b, const juce::String& text, const juce::String& tip, std::function<void()> fn)
        {
            b.setButtonText (text);
            b.setTitle (text);
            b.setTooltip (tip);
            b.onClick = std::move (fn);
            addAndMakeVisible (b);
        }

        static void layoutButtons (juce::Rectangle<int> area, std::initializer_list<juce::Button*> buttons)
        {
            const int w = juce::jmax (40, area.getWidth() / (int) buttons.size());
            for (auto* b : buttons)
                b->setBounds (area.removeFromLeft (w).reduced (1));
        }

        void rebuild()
        {
            const juce::ScopedValueSetter<bool> guard (updating, true);
            const auto& bank = scenes().getBank();
            sceneBox.clear (juce::dontSendNotification);
            for (int i = 0; i < bank.size(); ++i)
                sceneBox.addItem (juce::String (i + 1) + "  " + bank.at (i).name, i + 1);
            sceneBox.setTextWhenNothingSelected (bank.size() == 0 ? "No scenes yet - press NEW" : "Scene...");
            shownCount = bank.size();
            shownCurrent = scenes().getCurrentIndex();
            if (shownCurrent >= 0 && shownCurrent < bank.size())
                sceneBox.setSelectedId (shownCurrent + 1, juce::dontSendNotification);
            quantBox.setSelectedId (idFromQuant (bank.options.quant), juce::dontSendNotification);
            shownNames = namesHash();
            updateButtons();
        }

        int namesHash() const
        {
            int h = 0;
            const auto& bank = scenes().getBank();
            for (int i = 0; i < bank.size(); ++i)
                h = h * 31 + bank.at (i).name.hashCode();
            return h;
        }

        void updateButtons()
        {
            const auto& bank = scenes().getBank();
            const bool some = bank.size() > 0;
            prev.setEnabled (some);
            next.setEnabled (some);
            store.setEnabled (scenes().getCurrentIndex() >= 0);
            add.setEnabled (bank.size() < scenes::kMaxScenes);
            exportBtn.setEnabled (some);
            undoBtn.setEnabled (scenes().canUndoImport());
            cancel.setEnabled (scenes().getPendingIndex() >= 0);
        }

        void timerCallback() override
        {
            const auto& bank = scenes().getBank();
            if (bank.size() != shownCount || scenes().getCurrentIndex() != shownCurrent || namesHash() != shownNames)
                rebuild();
            else
                updateButtons();

            juce::String text = scenes().getStatus();
            if (scenes().getPendingIndex() >= 0)
                text = "Next: scene " + juce::String (scenes().getPendingIndex() + 1) + " (waiting for the "
                       + quantBox.getText().toLowerCase() + ")";
            if (text != status.getText())
                status.setText (text, juce::dontSendNotification);
        }

        void doExport()
        {
            chooser = std::make_unique<juce::FileChooser> ("Export scene bank",
                                                           juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                               .getChildFile ("GapFill bank.gapbank"),
                                                           "*.gapbank");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      const auto f = fc.getResult();
                                      if (f == juce::File())
                                          return;
                                      bank::ExportOptions opt;
                                      const auto text = bank::exportToString (scenes().getBank(), opt,
                                                                             versionString(),
                                                                             juce::Time::getCurrentTime().toISO8601 (true));
                                      juce::String error;
                                      if (! bank::writeFile (f.withFileExtension ("gapbank"), text, true, error))
                                          status.setText ("Export failed: " + error, juce::dontSendNotification);
                                      else
                                          status.setText ("Exported " + juce::String (scenes().getBank().size()) + " scenes", juce::dontSendNotification);
                                  });
        }

        void doImport()
        {
            juce::PopupMenu m;
            m.addItem (1, "Replace the bank");
            m.addItem (2, "Add to the bank");
            m.addItem (3, "Merge by name");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (importBtn),
                             [this] (int result)
                             {
                                 if (result == 0)
                                     return;
                                 const auto mode = result == 1 ? bank::ImportMode::Replace
                                                 : result == 2 ? bank::ImportMode::Append : bank::ImportMode::MergeByName;
                                 chooser = std::make_unique<juce::FileChooser> ("Import scene bank", juce::File(), "*.gapbank");
                                 chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                                       [this, mode] (const juce::FileChooser& fc)
                                                       {
                                                           const auto f = fc.getResult();
                                                           if (f == juce::File())
                                                               return;
                                                           const auto report = bank::parseFile (f);
                                                           if (report.status != bank::ParseStatus::Ok)
                                                           {
                                                               status.setText (report.message, juce::dontSendNotification);
                                                               return;
                                                           }
                                                           const auto res = bank::applyImport (scenes(), report, mode);
                                                           status.setText (res.message, juce::dontSendNotification);
                                                           rebuild();
                                                       });
                             });
        }

        GapFillAudioProcessor& proc;
        juce::ComboBox sceneBox, quantBox;
        juce::TextButton prev, next, cancel, store, add, exportBtn, importBtn, undoBtn;
        juce::Label status;
        std::unique_ptr<juce::FileChooser> chooser;
        bool updating = false;
        int shownCount = -1, shownCurrent = -2, shownNames = 0;
    };

    const SlotRegistrar scenePanel ({ Slot::PerformanceTab, 200,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<ScenePanel> (proc);
        } });

    const HelpRegistrar helpScenePanel (6220, "Scenes and banks", [] (HelpWriter& w)
    {
        w.body ("A scene is a snapshot of the fill settings. NEW stores the current sound as a scene, STORE overwrites the selected one, "
                "and picking, PREV or NEXT recalls one. The change lands on the quantise boundary (Beat, Bar or Phrase) when the "
                "transport runs, and right away when it does not. CANCEL drops a pending change.");
        w.body ("EXPORT writes the whole bank to a .gapbank file; IMPORT reads one, as a replacement, an addition or a merge by name. "
                "Importing never changes the sound until you recall a scene, and UNDO IMPORT restores the previous bank.");
    });
}

} // namespace gapfill
