// GapFill
// Copyright © 2026 Sheldon Davidson.
// Licensed under AGPL-3.0-or-later. See LICENSE.
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginEditor.h"
#include <array>

// The standalone wrapper owns the audio and MIDI devices, so the options page
// can only offer device settings when we are running inside it. The macro is
// defined for the shared code whenever Standalone is one of the build FORMATS,
// and getInstance() returns null in every other host - which is exactly the
// test we want, since the VST3 built from these same sources must not offer it.
#if defined (JucePlugin_Build_Standalone) && JucePlugin_Build_Standalone
 // The standalone header is written to be pulled in by the wrapper, which has
 // already included these two; on its own it does not include them itself.
 #include <juce_audio_devices/juce_audio_devices.h>
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace gapfill
{

namespace
{
    constexpr int kTopBarHeight = 46;
    constexpr int kMixHeight    = 112;
    constexpr int kTabHeight    = 30;
    constexpr int kFooterHeight = 12;   // version marker, bottom-right
    constexpr int kMinWidth     = 820;
    constexpr int kMinHeight    = 540;

    constexpr int kFactoryOffset = 1;
    constexpr int kUserOffset    = 1000;

    bool isKeyboardInteractive (const juce::Component& component)
    {
        return dynamic_cast<const juce::Button*> (&component) != nullptr
            || dynamic_cast<const juce::Slider*> (&component) != nullptr
            || dynamic_cast<const juce::ComboBox*> (&component) != nullptr;
    }

    void setKeyboardFocusRecursively (juce::Component& root, bool enabled)
    {
        for (int i = 0; i < root.getNumChildComponents(); ++i)
        {
            if (auto* child = root.getChildComponent (i))
            {
                if (isKeyboardInteractive (*child))
                    child->setWantsKeyboardFocus (enabled);

                setKeyboardFocusRecursively (*child, enabled);
            }
        }
    }

    int countKeyboardFocusableRecursively (const juce::Component& root)
    {
        int count = 0;

        for (int i = 0; i < root.getNumChildComponents(); ++i)
        {
            if (auto* child = root.getChildComponent (i))
            {
                if (isKeyboardInteractive (*child) && child->getWantsKeyboardFocus())
                    ++count;

                count += countKeyboardFocusableRecursively (*child);
            }
        }

        return count;
    }

    bool hasAccessibleControlName (const juce::Component& component)
    {
        if (auto* button = dynamic_cast<const juce::Button*> (&component))
            return button->getButtonText().isNotEmpty()
                || component.getTitle().isNotEmpty()
                || component.getName().isNotEmpty();

        return component.getTitle().isNotEmpty();
    }

    int countUnnamedKeyboardFocusableRecursively (const juce::Component& root)
    {
        int count = 0;

        for (int i = 0; i < root.getNumChildComponents(); ++i)
        {
            if (auto* child = root.getChildComponent (i))
            {
                if (isKeyboardInteractive (*child)
                    && child->getWantsKeyboardFocus()
                    && ! hasAccessibleControlName (*child))
                    ++count;

                count += countUnnamedKeyboardFocusableRecursively (*child);
            }
        }

        return count;
    }
}

//==============================================================================
GapFillAudioProcessorEditor::GapFillAudioProcessorEditor (GapFillAudioProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      visualiser (p.getVisualisationFifo()),
      detectionPanel (p.getValueTreeState()),
      creativePanel (p.getValueTreeState()),
      weightsPanel (p.getValueTreeState()),
      spectralPanel (p.getValueTreeState()),
      protectionPanel (p.getValueTreeState()),
      mixPanel (p.getValueTreeState())
{
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (false);
    setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);

    // ---- top bar ----------------------------------------------------------
    logo.setText ("GAPFILL", juce::dontSendNotification);
    logo.setFont (juce::Font (juce::FontOptions (22.0f)).boldened().withExtraKerningFactor (0.18f));
    logo.setColour (juce::Label::textColourId, colours::text);
    logo.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (logo);

    tagline.setText ("ear candy in the spaces between", juce::dontSendNotification);
    tagline.setFont (juce::Font (juce::FontOptions (10.5f)).withExtraKerningFactor (0.08f));
    tagline.setColour (juce::Label::textColourId, colours::textDim);
    tagline.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (tagline);

    latencyLabel.setFont (juce::Font (juce::FontOptions (10.5f)));
    latencyLabel.setColour (juce::Label::textColourId, colours::textDim);
    latencyLabel.setJustificationType (juce::Justification::centredRight);
    latencyLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (latencyLabel);

    presetBox.setName ("Preset");
    presetBox.setTitle ("Preset");
    presetBox.setDescription ("Factory and user preset selector");
    presetBox.setHelpText ("Choose a factory or user preset.");
    presetBox.setTooltip ("Factory and user presets. Use < and > to step through them.");
    presetBox.setWantsKeyboardFocus (false);
    presetBox.onChange = [this] { presetSelected(); };
    addAndMakeVisible (presetBox);
    refreshPresetList();

    prevPreset.setTooltip ("Previous preset");
    nextPreset.setTooltip ("Next preset");
    fileButton.setTooltip ("Save, save as, open a preset file, or open the options page");
    abButton.setTooltip ("Switch between the A and B snapshots");
    copyButton.setTooltip ("Copy the current settings into the other snapshot");
    randomButton.setTooltip ("Generate a brand new fill character. Different every single press - "
                             "macros, algorithm weights and a fresh seed. Detector and mix are left alone.");
    randomAllButton.setTooltip ("Randomise creative, detection and protection settings within safe musical ranges.");
    resetButton.setTooltip ("Reset every parameter to its default value");
    helpButton.setTooltip ("Open the full help section");
    surpriseButton.setTooltip ("Surprise Me, safely: jump to one of eight curated creative profiles "
                               "(Drum Edits, Dub Echoes, Glitch Storm...) with fresh variation. Detector, "
                               "protection and mix are left exactly as they are.");
    undoButton.setTooltip ("Undo the last Random, Surprise, Reset, preset change or file load "
                           "(up to 32 steps).");
    bypassButton.setTooltip ("Bypass GapFill. Latency compensation stays correct.");

    prevPreset.onClick = [this]
    {
        const auto n = (int) getFactoryPresets().size();
        auto index = proc.getCurrentProgram() - 1;
        proc.selectPreset ((index + n) % n);
        presetBox.setSelectedId (proc.getCurrentProgram() + kFactoryOffset, juce::dontSendNotification);
    };

    nextPreset.onClick = [this]
    {
        const auto n = (int) getFactoryPresets().size();
        proc.selectPreset ((proc.getCurrentProgram() + 1) % n);
        presetBox.setSelectedId (proc.getCurrentProgram() + kFactoryOffset, juce::dontSendNotification);
    };

    fileButton.onClick = [this] { showFileMenu(); };
    abButton.onClick = [this] { toggleAB(); };
    copyButton.onClick = [this] { copyToOtherSlot(); };

    randomButton.onClick = [this]
    {
        proc.randomise (false);
        presetBox.setSelectedId (0, juce::dontSendNotification);
        presetBox.setText ("Random", juce::dontSendNotification);
    };

    randomAllButton.onClick = [this]
    {
        proc.randomise (true);
        presetBox.setSelectedId (0, juce::dontSendNotification);
        presetBox.setText ("Random All", juce::dontSendNotification);
    };

    surpriseButton.onClick = [this]
    {
        const auto name = proc.surprise();
        presetBox.setSelectedId (0, juce::dontSendNotification);
        presetBox.setText ("Surprise: " + name, juce::dontSendNotification);
    };

    undoButton.onClick = [this]
    {
        if (! proc.undo())
            return;

        if (proc.getCurrentPresetName() == "Custom")
        {
            presetBox.setSelectedId (0, juce::dontSendNotification);
            presetBox.setText ("Custom", juce::dontSendNotification);
        }
        else
        {
            presetBox.setSelectedId (proc.getCurrentProgram() + kFactoryOffset, juce::dontSendNotification);
        }
    };

    resetButton.onClick = [this]
    {
        proc.resetAllParameters();
        mixPanel.resetMeterPeaks();
        presetBox.setSelectedId (kFactoryOffset, juce::dontSendNotification);
    };

    helpButton.onClick = [this]
    {
        helpView.setVisible (! helpView.isVisible());

        if (helpView.isVisible())
        {
            helpView.toFront (false);
            helpView.resized();
        }
    };

    bypassButton.setButtonText ("BYPASS");
    bypassButton.onRightClick = [this]
    {
        showParameterMenu (bypassButton, proc.getValueTreeState(), "bypass");
    };
    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff5252));
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                            proc.getValueTreeState(), "bypass", bypassButton);

    // Spelled out because bypassButton is a ParamButton: with the element type
    // named, each pointer converts to the base rather than having to deduce a
    // single type from a mixed list.
    const std::initializer_list<juce::TextButton*> topBarButtons
        { &prevPreset, &nextPreset, &fileButton, &abButton, &copyButton,
          &randomButton, &randomAllButton, &surpriseButton, &undoButton,
          &resetButton, &helpButton, &bypassButton };

    for (auto* b : topBarButtons)
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    randomButton.setColour (juce::TextButton::buttonColourId, colours::accentWarm.withAlpha (0.35f));
    surpriseButton.setColour (juce::TextButton::buttonColourId, colours::accentGreen.withAlpha (0.30f));

    // ---- main areas --------------------------------------------------------
    addAndMakeVisible (mixPanel);

    // Feature panels are hosted by presentation-only tiles. The viewport owns
    // the page container, while the editor owns every panel and tile.
    pageContents.addAndMakeVisible (visualiser);
    pageContents.addAndMakeVisible (detectionTile);
    pageContents.addAndMakeVisible (creativeTile);
    pageContents.addAndMakeVisible (weightsTile);
    pageContents.addAndMakeVisible (spectralTile);
    pageContents.addAndMakeVisible (protectionTile);
    for (auto* tile : { &detectionTile, &creativeTile, &weightsTile, &spectralTile, &protectionTile })
        tile->onHeightChanged = [this] { resized(); };
    pageViewport.setViewedComponent (&pageContents, false);
    pageViewport.setScrollBarsShown (true, false, true, false);
    pageViewport.setScrollBarThickness (8);
    addAndMakeVisible (pageViewport);

    for (auto* b : { &tabDetection, &tabCreative, &tabWeights, &tabSpectral, &tabProtection })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1001);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    tabDetection.onClick = [this] { setTab (Tab::Detection); };
    tabCreative.onClick = [this] { setTab (Tab::Creative); };
    tabWeights.onClick  = [this] { setTab (Tab::Weights); };
    tabSpectral.onClick = [this] { setTab (Tab::Spectral); };
    tabProtection.onClick = [this] { setTab (Tab::Protection); };
    moreButton.onClick = [this] { showMoreMenu(); };
    moreButton.setTooltip ("Additional actions");
    moreButton.setWantsKeyboardFocus (false);
    addAndMakeVisible (moreButton);
    tabCreative.setToggleState (true, juce::dontSendNotification);
    updateSelectedPage();

    helpView.onClose = [this] { helpView.setVisible (false); };
    addChildComponent (helpView);

    optionsView.onClose = [this] { optionsView.setVisible (false); };

   #if defined (JucePlugin_Build_Standalone) && JucePlugin_Build_Standalone
    if (juce::StandalonePluginHolder::getInstance() != nullptr)
    {
        optionsView.onShowDeviceSettings = []
        {
            // Re-checked at click time: the holder outlives the editor, but
            // being defensive here costs nothing.
            if (auto* holder = juce::StandalonePluginHolder::getInstance())
                holder->showAudioSettingsDialog();
        };
    }
   #endif

    optionsView.onTooltipsChanged = [this] (bool on)
    {
        proc.setTooltipsEnabled (on);
        applyTooltipSetting();
    };

    optionsView.onReducedMotionChanged = [this] (bool on)
    {
        proc.setReducedMotionEnabled (on);
        gapfill::setReducedMotionEnabled (on);
    };

    optionsView.onKeyboardNavigationChanged = [this] (bool on)
    {
        proc.setKeyboardNavigationEnabled (on);
        applyKeyboardNavigationSetting();
    };

    addChildComponent (optionsView);

    applyTooltipSetting();
    gapfill::setReducedMotionEnabled (proc.isReducedMotionEnabled());
    applyKeyboardNavigationSetting();

    // ---- A/B snapshots -----------------------------------------------------
    slotA = proc.getValueTreeState().copyState().createCopy();
    slotB = slotA.createCopy();

    // Start every editor session with a clean visual history. Drain through
    // the FIFO's consumer API rather than resetting its indices: an audio
    // callback that observed the previous enabled state may still be finishing
    // one producer write while the old editor is being destroyed.
    {
        auto& fifo = proc.getVisualisationFifo();
        VizFrame stale[64];

        while (fifo.pop (stale, 64) > 0) {}

        fifo.setEnabled (true);
    }

    const auto saved = proc.getSavedEditorSize();
    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, 2600, 1800);
    setSize (juce::jlimit (kMinWidth, 2600, saved.x),
             juce::jlimit (kMinHeight, 1800, saved.y));

    if (proc.getCurrentPresetName() == "Custom")
    {
        presetBox.setSelectedId (0, juce::dontSendNotification);
        presetBox.setText ("Custom", juce::dontSendNotification);
    }
    else
    {
        presetBox.setSelectedId (proc.getCurrentProgram() + kFactoryOffset, juce::dontSendNotification);
    }

    startTimerHz (24);
}

GapFillAudioProcessorEditor::~GapFillAudioProcessorEditor()
{
    stopTimer();
    proc.getVisualisationFifo().setEnabled (false);
    setLookAndFeel (nullptr);
}

//==============================================================================
void GapFillAudioProcessorEditor::setTab (Tab newTab)
{
    currentTab = newTab;

    tabDetection.setToggleState (newTab == Tab::Detection, juce::dontSendNotification);
    tabCreative.setToggleState (newTab == Tab::Creative, juce::dontSendNotification);
    tabWeights.setToggleState (newTab == Tab::Weights, juce::dontSendNotification);
    tabSpectral.setToggleState (newTab == Tab::Spectral, juce::dontSendNotification);
    tabProtection.setToggleState (newTab == Tab::Protection, juce::dontSendNotification);

    updateSelectedPage();
    resized();
}

void GapFillAudioProcessorEditor::updateSelectedPage()
{
    detectionTile.setVisible (currentTab == Tab::Detection);
    creativeTile.setVisible (currentTab == Tab::Creative);
    weightsTile.setVisible (currentTab == Tab::Weights);
    spectralTile.setVisible (currentTab == Tab::Spectral);
    protectionTile.setVisible (currentTab == Tab::Protection);
}

void GapFillAudioProcessorEditor::showMoreMenu()
{
    enum { copy = 1, random, randomAll, surprise, undo, reset, help };
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);
    menu.addItem (copy, "Copy to other A/B slot");
    menu.addItem (random, "Random");
    menu.addItem (randomAll, "Random All");
    menu.addItem (surprise, "Surprise");
    menu.addItem (undo, "Undo", proc.canUndo());
    menu.addItem (reset, "Reset");
    menu.addSeparator();
    menu.addItem (help, "Help");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&moreButton),
                        [this] (int result)
    {
        switch (result)
        {
            case copy: copyButton.triggerClick(); break;
            case random: randomButton.triggerClick(); break;
            case randomAll: randomAllButton.triggerClick(); break;
            case surprise: surpriseButton.triggerClick(); break;
            case undo: undoButton.triggerClick(); break;
            case reset: resetButton.triggerClick(); break;
            case help: helpButton.triggerClick(); break;
            default: break;
        }
    });
}

//==============================================================================
juce::File GapFillAudioProcessorEditor::userPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("GapFill").getChildFile ("Presets");
}

void GapFillAudioProcessorEditor::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);
    userPresetFiles.clear();

    presetBox.addSectionHeading ("Factory");

    const auto& factory = getFactoryPresets();

    for (int i = 0; i < (int) factory.size(); ++i)
        presetBox.addItem (factory[(size_t) i].name, i + kFactoryOffset);

    auto dir = userPresetDirectory();

    if (dir.isDirectory())
    {
        for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.gapfill"))
            userPresetFiles.add (f);

        if (! userPresetFiles.isEmpty())
        {
            presetBox.addSectionHeading ("User");

            for (int i = 0; i < userPresetFiles.size(); ++i)
                presetBox.addItem (userPresetFiles[i].getFileNameWithoutExtension(), i + kUserOffset);
        }
    }
}

void GapFillAudioProcessorEditor::presetSelected()
{
    const auto id = presetBox.getSelectedId();

    if (id >= kUserOffset)
    {
        const auto index = id - kUserOffset;

        if (juce::isPositiveAndBelow (index, userPresetFiles.size()))
            loadPresetFromFile (userPresetFiles[index]);
    }
    else if (id >= kFactoryOffset)
    {
        proc.selectPreset (id - kFactoryOffset);
        currentPresetFile = juce::File();
    }
}

void GapFillAudioProcessorEditor::saveUserPreset()
{
    auto* window = new juce::AlertWindow ("Save preset",
                                          "Name this preset:",
                                          juce::MessageBoxIconType::NoIcon);
    window->setLookAndFeel (&lookAndFeel);
    window->addTextEditor ("name", "My GapFill", {});
    window->addButton ("Save", 1);
    window->addButton ("Cancel", 0);

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, window] (int result)
        {
            const auto name = window->getTextEditorContents ("name").trim();
            window->setLookAndFeel (nullptr);
            delete window;

            if (result != 1 || name.isEmpty())
                return;

            auto dir = userPresetDirectory();

            if (! dir.isDirectory() && ! dir.createDirectory().wasOk())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Could not save preset",
                                                        "GapFill could not create the user preset folder.",
                                                        "OK",
                                                        this);
                return;
            }

            const auto legalName = juce::File::createLegalFileName (name);

            if (legalName.isEmpty())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Could not save preset",
                                                        "Please choose a preset name that contains valid filename characters.",
                                                        "OK",
                                                        this);
                return;
            }

            auto file = dir.getChildFile (legalName + ".gapfill");

            if (! savePresetToFile (file))
                return;

            for (int i = 0; i < userPresetFiles.size(); ++i)
                if (userPresetFiles[i] == file)
                    presetBox.setSelectedId (i + kUserOffset, juce::dontSendNotification);
        }), false);
}

//==============================================================================
void GapFillAudioProcessorEditor::showFileMenu()
{
    enum { kSave = 1, kSaveAs, kOpen, kSavePreset, kOptions };

    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);

    // "Save" only means something once there is a file to save over.
    menu.addItem (kSave, currentPresetFile != juce::File()
                             ? "Save (" + currentPresetFile.getFileName() + ")"
                             : juce::String ("Save"),
                  currentPresetFile != juce::File());
    menu.addItem (kSaveAs, "Save As...");
    menu.addItem (kOpen, "Open...");
    menu.addSeparator();
    menu.addItem (kSavePreset, "Save as user preset...");
    menu.addSeparator();
    menu.addItem (kOptions, "Options...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fileButton),
                        [this] (int result)
    {
        switch (result)
        {
            case kSave:       doSave(); break;
            case kSaveAs:     doSaveAs(); break;
            case kOpen:       doOpen(); break;
            case kSavePreset: saveUserPreset(); break;
            case kOptions:    showOptions(); break;
            default: break;
        }
    });
}

bool GapFillAudioProcessorEditor::savePresetToFile (const juce::File& file)
{
    if (file == juce::File())
        return false;

    juce::String error;

    if (! proc.saveParameterStateToFile (file, error))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Could not save preset",
                                                error,
                                                "OK",
                                                this);
        return false;
    }

    currentPresetFile = file;

    // A file saved into the user preset folder should show up in the dropdown
    // straight away.
    refreshPresetList();
    return true;
}

void GapFillAudioProcessorEditor::loadPresetFromFile (const juce::File& file)
{
    juce::String error;

    if (! proc.loadParameterStateFromFile (file, error))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Could not open preset",
                                                error,
                                                "OK",
                                                this);

        // The current sound was deliberately left untouched, so do not leave
        // the browser visually claiming that the rejected file is active.
        presetBox.setSelectedId (0, juce::dontSendNotification);
        presetBox.setText (proc.getCurrentPresetName(), juce::dontSendNotification);
        return;
    }

    currentPresetFile = file;
    presetBox.setSelectedId (0, juce::dontSendNotification);
    presetBox.setText (file.getFileNameWithoutExtension(), juce::dontSendNotification);
}

void GapFillAudioProcessorEditor::doSave()
{
    if (currentPresetFile == juce::File())
    {
        doSaveAs();
        return;
    }

    savePresetToFile (currentPresetFile);
}

void GapFillAudioProcessorEditor::doSaveAs()
{
    auto start = currentPresetFile != juce::File() ? currentPresetFile
                                                   : userPresetDirectory().getChildFile ("My GapFill.gapfill");

    chooser = std::make_unique<juce::FileChooser> ("Save GapFill settings", start, "*.gapfill");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();

        if (file == juce::File())
            return;

        savePresetToFile (file.hasFileExtension ("gapfill") ? file
                                                            : file.withFileExtension ("gapfill"));
    });
}

void GapFillAudioProcessorEditor::doOpen()
{
    auto start = currentPresetFile != juce::File() ? currentPresetFile.getParentDirectory()
                                                   : userPresetDirectory();

    chooser = std::make_unique<juce::FileChooser> ("Open GapFill settings", start, "*.gapfill");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        if (fc.getResult() != juce::File())
            loadPresetFromFile (fc.getResult());
    });
}

void GapFillAudioProcessorEditor::showOptions()
{
    optionsView.refresh (proc.isTooltipsEnabled(),
                         proc.isReducedMotionEnabled(),
                         proc.isKeyboardNavigationEnabled());
    optionsView.setVisible (true);
    optionsView.toFront (false);
    optionsView.resized();
}

void GapFillAudioProcessorEditor::applyTooltipSetting()
{
    if (proc.isTooltipsEnabled())
    {
        if (tooltips == nullptr)
            tooltips = std::make_unique<juce::TooltipWindow> (nullptr, metrics::tooltipDelayMs);
    }
    else
    {
        tooltips.reset();
    }
}

void GapFillAudioProcessorEditor::applyKeyboardNavigationSetting()
{
    const auto enabled = proc.isKeyboardNavigationEnabled();

    if (keyboardNavigationApplied && enabled == lastKeyboardNavigationState)
        return;

    keyboardNavigationApplied = true;
    lastKeyboardNavigationState = enabled;

    // The editor itself deliberately remains non-focusable so opening the
    // plugin cannot steal DAW shortcuts. Users who opt in get focus traversal
    // only across actual interactive controls.
    setWantsKeyboardFocus (false);
    setKeyboardFocusRecursively (*this, enabled);

    // Overlay controls are included by the recursive pass even when hidden;
    // once an overlay is shown JUCE's normal Tab traversal can reach them.
}

#ifdef GAPFILL_UNIT_TESTS
int GapFillAudioProcessorEditor::getKeyboardFocusableControlCountForTests() const
{
    return countKeyboardFocusableRecursively (*this);
}

int GapFillAudioProcessorEditor::getUnnamedKeyboardFocusableControlCountForTests() const
{
    return countUnnamedKeyboardFocusableRecursively (*this);
}
#endif

//==============================================================================
void GapFillAudioProcessorEditor::toggleAB()
{
    auto& state = proc.getValueTreeState();

    // Store what is on screen into the slot we are leaving, then load the other.
    (usingSlotB ? slotB : slotA) = state.copyState().createCopy();
    usingSlotB = ! usingSlotB;

    auto& target = usingSlotB ? slotB : slotA;

    if (target.isValid())
        state.replaceState (target.createCopy());

    abButton.setButtonText (usingSlotB ? "B" : "A");
    presetBox.setSelectedId (0, juce::dontSendNotification);
    presetBox.setText (usingSlotB ? "B" : "A", juce::dontSendNotification);
}

void GapFillAudioProcessorEditor::copyToOtherSlot()
{
    auto current = proc.getValueTreeState().copyState().createCopy();
    (usingSlotB ? slotA : slotB) = current;
    (usingSlotB ? slotB : slotA) = current.createCopy();
}

//==============================================================================
void GapFillAudioProcessorEditor::timerCallback()
{
    auto& meters = proc.getMeterState();

    mixPanel.setLevels (meters.inputLevel.load(), meters.outputLevel.load(), meters.wetLevel.load());
    mixPanel.setConfidence (meters.confidence.load());

    spectralPanel.setBandConfidence (0, meters.bandConf0.load());
    spectralPanel.setBandConfidence (1, meters.bandConf1.load());
    spectralPanel.setBandConfidence (2, meters.bandConf2.load());

    auto& state = proc.getValueTreeState();

    const auto modeIndex = (int) std::lround (state.getRawParameterValue ("detMode")->load());
    visualiser.setSpectralMode (modeIndex == (int) DetMode::Spectral);
    visualiser.setLookaheadMs (state.getRawParameterValue ("lookahead")->load());

    const auto latency = meters.latencySamples.load();
    latencyLabel.setText ("latency " + juce::String (latency) + " smp", juce::dontSendNotification);

    // Status line: MIDI learn takes priority while armed so the user always
    // knows the plugin is waiting for a controller, even after the popup closes.
    juce::String status;
    auto& midiLearn = proc.getMidiLearnManager();

    if (midiLearn.isLearning())
    {
        const auto parameterID = midiLearn.getLearningParameterID();
        auto displayName = parameterID;

        if (auto* parameter = state.getParameter (parameterID))
            displayName = parameter->getName (32);

        const auto ticks = juce::jmax (0, midiLearn.getLearningTicksRemaining());
        const auto seconds = (ticks + MidiLearnManager::learningTimerHz - 1)
                           / MidiLearnManager::learningTimerHz;

        status = "MIDI LEARN  |  " + displayName
               + "  |  move a controller  |  " + juce::String (seconds)
               + "s  |  right-click again to cancel";
    }
    else
    {
        if (meters.sidechainRequested.load() && ! meters.sidechainConnected.load())
            status = "Sidechain selected but not connected - using main detection";
        else if (meters.sidechainConnected.load() && meters.sidechainRequested.load())
            status = "Sidechain detection active";
        else
            status = "Main detection";

        const auto algo = meters.currentAlgo.load();

        if (algo >= 0 && algo < numAlgos)
            status += juce::String ("  |  filling: ") + algoName ((Algo) algo);

        if (meters.captureActive.load())
            status += "  |  CAPTURED source";
    }

    undoButton.setEnabled (proc.canUndo());

    mixPanel.setStatusText (status);

    // Keep the control animation policy in sync with project-state restores
    // that may occur while this editor is already open.
    gapfill::setReducedMotionEnabled (proc.isReducedMotionEnabled());
    applyKeyboardNavigationSetting();
}

//==============================================================================
void GapFillAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // Top bar backdrop
    auto top = getLocalBounds().removeFromTop (kTopBarHeight).toFloat();
    g.setColour (colours::panel);
    g.fillRect (top);
    g.setColour (colours::outline);
    g.drawHorizontalLine (kTopBarHeight - 1, 0.0f, (float) getWidth());

    g.setColour (colours::accent);
    g.fillRoundedRectangle (14.0f, top.getCentreY() - 11.0f, 3.0f, 22.0f, 1.5f);

    // --- identity markers (theme.md) ---------------------------------------
    GapFillLookAndFeel::drawSignatureNotch (g, getLocalBounds().toFloat());

    g.setColour (colours::textMuted);
    g.setFont (fonts::mono (9.0f));
    g.drawText ("v" + versionString(), footerBounds(),
                juce::Justification::centredRight, false);
}

/** The strip along the bottom that resized() keeps clear for the version. */
juce::Rectangle<int> GapFillAudioProcessorEditor::footerBounds() const
{
    return getLocalBounds().withTrimmedTop (kTopBarHeight).reduced (10, 8)
                           .removeFromBottom (kFooterHeight);
}

void GapFillAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    proc.setSavedEditorSize ({ getWidth(), getHeight() });

    // The header progressively moves secondary commands into MORE as space
    // tightens. Bypass, preset selection, A/B and file access remain visible.
    const bool showAllActions = getWidth() >= 1180;
    auto top = bounds.removeFromTop (kTopBarHeight).reduced (10, 6);
    top.removeFromLeft (10);

    auto brand = top.removeFromLeft (145);
    logo.setBounds (brand.removeFromTop (juce::jmax (18, brand.getHeight() - 11)));
    tagline.setBounds (brand);
    top.removeFromLeft (6);

    bypassButton.setBounds (top.removeFromRight (74).reduced (2, 0));
    moreButton.setVisible (! showAllActions);
    const std::array<juce::TextButton*, 7> secondaryActions
        { &copyButton, &randomButton, &randomAllButton, &surpriseButton,
          &undoButton, &resetButton, &helpButton };
    if (showAllActions)
    {
        moreButton.setBounds ({ });
        for (auto* button : secondaryActions)
            button->setVisible (true);

        helpButton.setBounds (top.removeFromRight (58).reduced (2, 0));
        resetButton.setBounds (top.removeFromRight (58).reduced (2, 0));
        undoButton.setBounds (top.removeFromRight (54).reduced (2, 0));
        surpriseButton.setBounds (top.removeFromRight (78).reduced (2, 0));
        randomAllButton.setBounds (top.removeFromRight (86).reduced (2, 0));
        randomButton.setBounds (top.removeFromRight (74).reduced (2, 0));
        top.removeFromRight (4);
        copyButton.setBounds (top.removeFromRight (48).reduced (2, 0));
    }
    else
    {
        for (auto* button : secondaryActions)
            button->setVisible (false);
        moreButton.setBounds (top.removeFromRight (58).reduced (2, 0));
    }

    abButton.setBounds (top.removeFromRight (34).reduced (1, 0));
    top.removeFromRight (3);
    nextPreset.setBounds (top.removeFromRight (24).reduced (1, 0));
    prevPreset.setBounds (top.removeFromRight (24).reduced (1, 0));
    top.removeFromRight (3);
    fileButton.setBounds (top.removeFromRight (50).reduced (1, 0));
    presetBox.setBounds (top.removeFromLeft (juce::jmin (230, juce::jmax (90, top.getWidth() - 4))).reduced (1, 1));
    latencyLabel.setVisible (showAllActions);
    latencyLabel.setBounds (showAllActions ? top.reduced (4, 0) : juce::Rectangle<int>());

    bounds.reduce (10, 6);
    auto tabs = bounds.removeFromTop (kTabHeight).reduced (0, 1);
    const auto tabCount = 5;
    const auto gap = 3;
    const auto tabWidth = juce::jmax (1, (tabs.getWidth() - gap * (tabCount - 1)) / tabCount);
    tabDetection.setBounds (tabs.removeFromLeft (tabWidth));
    tabs.removeFromLeft (gap);
    tabCreative.setBounds (tabs.removeFromLeft (tabWidth));
    tabs.removeFromLeft (gap);
    tabWeights.setBounds (tabs.removeFromLeft (tabWidth));
    tabs.removeFromLeft (gap);
    tabSpectral.setBounds (tabs.removeFromLeft (tabWidth));
    tabs.removeFromLeft (gap);
    tabProtection.setBounds (tabs);

    bounds.removeFromBottom (kFooterHeight + 4);
    mixPanel.setBounds (bounds.removeFromBottom (kMixHeight));
    bounds.removeFromBottom (6);
    pageViewport.setBounds (bounds);

    const auto pageWidth = juce::jmax (1, pageViewport.getWidth() - pageViewport.getScrollBarThickness());
    auto* selectedTile = [&]() -> DisclosureTile*
    {
        switch (currentTab)
        {
            case Tab::Detection: return &detectionTile;
            case Tab::Creative: return &creativeTile;
            case Tab::Weights: return &weightsTile;
            case Tab::Spectral: return &spectralTile;
            case Tab::Protection: return &protectionTile;
        }
        return &creativeTile;
    }();

    visualiser.setBounds (0, 0, pageWidth, 98);
    selectedTile->setBounds (0, 106, pageWidth, selectedTile->getPreferredHeight());
    pageContents.setSize (pageWidth, 114 + selectedTile->getPreferredHeight());

    helpView.setBounds (getLocalBounds().withTrimmedTop (kTopBarHeight).reduced (24, 16));
    if (helpView.isVisible())
        helpView.resized();

    optionsView.setBounds (juce::Rectangle<int> (520, 440)
                             .withCentre (getLocalBounds().getCentre()));
}
} // namespace gapfill
