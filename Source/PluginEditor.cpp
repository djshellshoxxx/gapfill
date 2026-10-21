// GapFill
// Copyright © 2026 Sheldon Davidson.
// Licensed under AGPL-3.0-or-later. See LICENSE.
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginEditor.h"
#include "features/info/ParamInfo.h"
#include "features/mutation/Gui.h"
#include "features/uiscale/UiScale.h"
#include "features/presetbrowser/PresetBrowser.h"

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
    constexpr int kMixHeight    = 124;
    constexpr int kTabHeight    = 26;
    constexpr int kFooterHeight = 22;   // status line (WP3 FooterStatus slot) + version marker, bottom-right
    constexpr int kEffectsBody  = 140;   // UI-0: unfolded Effects section, scrolls inside itself
    constexpr int kMinVisualiserHeight = 70;
    constexpr int kViewToggleWidth = 130;        // U-5 SIMPLE | ADVANCED, right end of the tab row
    constexpr int kPerformanceStripHeight = 72;   // UI-0: Slot::PerformanceStrip, only when a feature registered one
    constexpr int kMinWidth     = 1020;
    constexpr int kMinHeight    = 620;

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
      hoverStatus (p.getValueTreeState(), p.getUiState().status),
      settingsFile (settings::GlobalSettings::shared()),
      visualiser (p.getVisualisationFifo()),
      detectionPanel (p.getValueTreeState()),
      creativePanel (p.getValueTreeState()),
      weightsPanel (p.getValueTreeState()),
      spectralPanel (p.getValueTreeState()),
      protectionPanel (p.getValueTreeState()),
      mixPanel (p.getValueTreeState())
{
    controlScaleFactor = access::applyFromSettings (*settingsFile);      // F-D04: palette, focus ring, control size
    accessSignature = settingsFile->getInt (access::kKeyContrast, 0) * 100 + settingsFile->getInt (access::kKeyControlSize, 0) * 10
                        + settingsFile->getInt (access::kKeyFocusRing, 2);
    lookAndFeel.refreshColours();
    setLookAndFeel (&lookAndFeel);
    addMouseListener (&hoverStatus, true);
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
    presetBox.setDescription ("Current preset. Opens the preset browser: search, categories, favourites and user presets.");
    presetBox.setHelpText ("Open the preset browser.");
    presetBox.setTooltip ("Presets - click to browse, search and favourite. Use < and > to step through them.");
    presetBox.setWantsKeyboardFocus (false);
    presetBox.onClick = [this] { showPresetBrowser(); };
    addAndMakeVisible (presetBox);
    navCatalog.setFactory (presets::factoryEntries());

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
    bypassButton.setTooltip (info::tooltipFor ("bypass", "Bypass GapFill. Latency compensation stays correct."));

    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (+1); };

    fileButton.onClick = [this] { showFileMenu(); };
    abButton.onClick = [this] { toggleAB(); };
    copyButton.onClick = [this] { copyToOtherSlot(); };

    randomButton.onClick = [this]
    {
        proc.randomise (false);
        setPresetDisplay ("Random");
    };

    randomAllButton.onClick = [this]
    {
        proc.randomise (true);
        setPresetDisplay ("Random All");
    };

    surpriseButton.onClick = [this]
    {
        const auto name = proc.surprise();
        setPresetDisplay ("Surprise: " + name);
    };

    undoButton.onClick = [this]
    {
        if (! proc.undo())
            return;

        if (proc.getCurrentPresetName() == "Custom")
        {
            setPresetDisplay ("Custom");
        }
        else
        {
            setPresetDisplay (proc.getCurrentPresetName());
        }
    };

    resetButton.onClick = [this]
    {
        proc.resetAllParameters();
        mixPanel.resetMeterPeaks();
        setPresetDisplay (proc.getCurrentPresetName());
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
    tagParameter (bypassButton, "bypass");
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
    addAndMakeVisible (visualiser);
    addAndMakeVisible (detectionPanel);
    addAndMakeVisible (creativePanel);
    addChildComponent (weightsPanel);
    addChildComponent (spectralPanel);
    addAndMakeVisible (protectionPanel);
    addAndMakeVisible (mixPanel);

    slots = std::make_unique<SlotHost> (proc, *this);

    // U-6 Auto-Calibrate: the LEARN button is in the Detection panel header; the card floats over the visualiser.
    calibration = std::make_unique<calibrate::CalibrationSession> (proc);
    calibrateCard = std::make_unique<calibrate::CalibrateCard> (*calibration);
    addChildComponent (*calibrateCard);
    detectionPanel.onLearn = [this] { calibrateCard->begin(); calibrateCard->toFront (false); };

    slots->setSlotContainer (Slot::OptionsExtra, &optionsView.getExtraContent());

    if (slots->getNumComponents (Slot::PerformanceTab) > 0)
    {
        performancePanel = std::make_unique<StackPanel> (*slots, Slot::PerformanceTab, "Performance", colours::accent);
        addChildComponent (*performancePanel);
    }

    if (slots->getNumComponents (Slot::EffectsSection) > 0)
    {
        effectsDrawer = std::make_unique<EffectsDrawer> (*slots);
        effectsDrawer->onExpandedChanged = [this] { resized(); };
        addAndMakeVisible (*effectsDrawer);
    }

    appliedView = proc.getUiState().getViewMode();

    for (auto* b : { &tabCreative, &tabWeights, &tabSpectral, &tabPerformance })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1001);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    tabCreative.onClick = [this] { setTab (Tab::Creative); };
    tabWeights.onClick  = [this] { setTab (Tab::Weights); };
    tabSpectral.onClick = [this] { setTab (Tab::Spectral); };
    tabPerformance.onClick = [this] { setTab (Tab::Performance); };
    tabPerformance.setVisible (false);      // shown by resized() when it fits and the slot is populated
    tabCreative.setToggleState (true, juce::dontSendNotification);

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

    // WP3 Fix-it R12 ("Open Options") needs a way back to this page.
    proc.getExplainState().openOptions = [this] { showOptions(); };

    applyTooltipSetting();
    gapfill::setReducedMotionEnabled (proc.isReducedMotionEnabled());
    applyKeyboardNavigationSetting();

    // ---- A/B snapshots -----------------------------------------------------
    slotA = proc.getValueTreeState().copyState().createCopy();
    slotB = slotA.createCopy();
    proc.getUiState().editorActions.battleFromHeaderAb = [this] { return feedBattleFromHeaderAb(); };

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
    setSize (juce::jmax (kMinWidth, saved.x), juce::jmax (kMinHeight, saved.y));

    if (proc.getCurrentPresetName() == "Custom")
    {
        setPresetDisplay ("Custom");
    }
    else
    {
        setPresetDisplay (proc.getCurrentPresetName());
    }

    applyViewMode();
    applyScale();

    if (tourAutoShowEnabled())
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<GapFillAudioProcessorEditor> (this)]
        {
            if (safe != nullptr)
                safe->maybeAutoShowTour();
        });

    startTimerHz (24);
}

GapFillAudioProcessorEditor::~GapFillAudioProcessorEditor()
{
    stopTimer();
    calibrateCard.reset();
    calibration.reset();                // cancels the recording and stops the analysis thread
    removeMouseListener (&hoverStatus);
    proc.getUiState().lab.endPreview();                 // F-L17: a preview never outlives the editor
    proc.getUiState().status.clear (info::StatusKind::Learn);
    proc.getUiState().status.clear (info::StatusKind::Capture);
    proc.getExplainState().openOptions = nullptr;
    proc.getUiState().editorActions.battleFromHeaderAb = nullptr;
    proc.getVisualisationFifo().setEnabled (false);
    setLookAndFeel (nullptr);
}

//==============================================================================
bool& GapFillAudioProcessorEditor::tourAutoShowEnabled()
{
   #ifdef GAPFILL_UNIT_TESTS
    static bool enabled = false;          // tests opt in; otherwise every test editor would try to show a tour
   #else
    static bool enabled = true;
   #endif
    return enabled;
}

juce::Component* GapFillAudioProcessorEditor::findTourTarget (const juce::String& id)
{
    if (id == "visualiser")  return &visualiser;
    if (id == "surprise")    return &surpriseButton;
    if (id == "undo")        return &undoButton;
    if (id == "presets")     return &presetBox;

    std::vector<juce::Component*> stack { this };

    while (! stack.empty())
    {
        auto* c = stack.back();
        stack.pop_back();

        if (id == "fill" && c->getProperties()[kParamIdProperty].toString() == "fill" && dynamic_cast<GapKnob*> (c) != nullptr)
            return c;

        // E-1's Fill Audition control (not the detector monitor toggle, which is "AUDITION DETECTOR").
        if (id == "audition" && c->getTitle().startsWithIgnoreCase ("audition fill"))
            return c;

        for (auto* child : c->getChildren())
            stack.push_back (child);
    }

    return nullptr;
}

bool GapFillAudioProcessorEditor::maybeAutoShowTour()
{
    if (tourOverlay != nullptr || getWidth() < kMinWidth || getHeight() < kMinHeight)
        return false;

    tour::TourModel probe (*settingsFile, 3);

    if (! probe.shouldAutoShow (tour::shownInProcess()))
        return false;

    showTour (false);
    return true;
}

void GapFillAudioProcessorEditor::showTour (bool force)
{
    juce::ignoreUnused (force);
    tourOverlay.reset();

    const bool audition = findTourTarget ("audition") != nullptr;
    auto steps = tour::defaultSteps (audition);
    tourModel = std::make_unique<tour::TourModel> (*settingsFile, (int) steps.size());
    tourModel->start();
    tour::shownInProcess() = true;

    tourOverlay = std::make_unique<tour::TourOverlay> (std::move (steps), *tourModel);
    tourOverlay->findTarget = [this] (const juce::String& id) { return findTourTarget (id); };
    tourOverlay->onLoadExample = [this]
    {
        refreshPresetList();
        const auto idx = navCatalog.indexOfIdentity ("F:Drum Gap Glitch");

        if (idx >= 0 && loadCatalogEntry (navCatalog.entries()[(size_t) idx]) && tourOverlay != nullptr)
            tourOverlay->setExampleStatus ("Loaded Drum Gap Glitch. Undo goes back.");
    };
    tourOverlay->onClosed = [safe = juce::Component::SafePointer<GapFillAudioProcessorEditor> (this)]
    {
        // Deleting the overlay from inside its own button callback is unsafe: defer.
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr)
                safe->tourOverlay.reset();
        });
    };

    addAndMakeVisible (*tourOverlay);
    tourOverlay->setBounds (getLocalBounds());
    tourOverlay->toFront (true);
}

void GapFillAudioProcessorEditor::setScaleFactor (float hostScale)
{
    hostScaleValue = hostScale > 0.0f ? hostScale : 1.0f;
    applyScale();
}

void GapFillAudioProcessorEditor::applyScale()
{
    const auto requested = uiscale::snapScalePercent (settingsFile->getInt (settings::GlobalSettings::keyUiScale, 100));
    lastRequestedScale = requested;

    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = displays.getDisplayForRect (getScreenBounds());

    if (display == nullptr)
        display = displays.getPrimaryDisplay();

    const auto area = display != nullptr ? display->userBounds.toNearestInt() : juce::Rectangle<int>();
    const juce::Point<int> logical { juce::jmax (kMinWidth, getWidth()), juce::jmax (kMinHeight, getHeight()) };

    appliedScalePercent = uiscale::clampScalePercent (requested, area, logical, hostScaleValue * controlScaleFactor);
    proc.getUiState().scaleNote = appliedScalePercent < requested
                                    ? "Reduced to " + juce::String (appliedScalePercent) + " % to fit this screen"
                                    : juce::String();

    AudioProcessorEditor::setScaleFactor (uiscale::effectiveScale (hostScaleValue, appliedScalePercent) * controlScaleFactor);
}

void GapFillAudioProcessorEditor::applyAccessibility()
{
    controlScaleFactor = access::applyFromSettings (*settingsFile);
    accessSignature = settingsFile->getInt (access::kKeyContrast, 0) * 100 + settingsFile->getInt (access::kKeyControlSize, 0) * 10
                        + settingsFile->getInt (access::kKeyFocusRing, 2);
    lookAndFeel.refreshColours();
    sendLookAndFeelChange();
    applyScale();
    repaint();
}

void GapFillAudioProcessorEditor::applyViewMode()
{
    const auto mode = proc.getUiState().getViewMode();
    const bool simple = mode == ui::ViewMode::Simple;
    appliedView = mode;

    detectionPanel.setSimple (! viewmode::isVisible ("detection.rest", mode));
    protectionPanel.setSimple (! viewmode::isVisible ("protection.rest", mode));
    creativePanel.setSimple (! viewmode::isVisible ("creative.advancedRows", mode));

    // A hidden tab must not stay selected; the previous one comes back with Advanced.
    if (simple && (currentTab == Tab::Weights || currentTab == Tab::Spectral))
    {
        tabBeforeSimple = currentTab;
        setTab (Tab::Creative);
    }
    else if (! simple && tabBeforeSimple.has_value())
    {
        const auto restore = *tabBeforeSimple;
        tabBeforeSimple.reset();
        setTab (restore);
    }

    resized();

    if (auto* handler = getAccessibilityHandler())
        handler->postAnnouncement (simple ? "Simple view" : "Advanced view",
                                   juce::AccessibilityHandler::AnnouncementPriority::low);
}

void GapFillAudioProcessorEditor::setTab (Tab newTab)
{
    currentTab = newTab;
    creativePanel.setVisible (newTab == Tab::Creative);
    weightsPanel.setVisible (newTab == Tab::Weights);
    spectralPanel.setVisible (newTab == Tab::Spectral);

    if (performancePanel != nullptr)
        performancePanel->setVisible (newTab == Tab::Performance);

    if (effectsDrawer != nullptr)
        effectsDrawer->setVisible (newTab == Tab::Creative);

    if (slots != nullptr)
        slots->setSlotVisible (Slot::CreativeExtra, newTab == Tab::Creative);

    tabCreative.setToggleState (newTab == Tab::Creative, juce::dontSendNotification);
    tabWeights.setToggleState (newTab == Tab::Weights, juce::dontSendNotification);
    tabSpectral.setToggleState (newTab == Tab::Spectral, juce::dontSendNotification);
    tabPerformance.setToggleState (newTab == Tab::Performance, juce::dontSendNotification);

    resized();
}

//==============================================================================
juce::File GapFillAudioProcessorEditor::userPresetDirectory()
{
    return presets::userPresetRoot();
}

void GapFillAudioProcessorEditor::setPresetDisplay (const juce::String& text)
{
    presetBox.setButtonText (text);
    presetBox.setTitle ("Preset: " + text);
    presetBox.repaint();
}

void GapFillAudioProcessorEditor::refreshPresetList()
{
    // Factory entries are fixed; user presets are rescanned (message thread, capped at 2000 files).
    navCatalog.setFactory (presets::factoryEntries());
    navCatalog.setUser (presets::scanUserPresets (presets::defaultScanRoots()).entries);
}

int GapFillAudioProcessorEditor::currentPresetIndex() const
{
    if (currentPresetFile != juce::File())
    {
        const auto& all = navCatalog.entries();

        for (int i = 0; i < (int) all.size(); ++i)
            if (all[(size_t) i].file == currentPresetFile)
                return i;
    }

    if (proc.getCurrentPresetName() != "Custom")
        return navCatalog.indexOfIdentity ("F:" + proc.getCurrentPresetName());

    return -1;
}

void GapFillAudioProcessorEditor::stepPreset (int delta)
{
    refreshPresetList();

    // F-S11: "Favourites only" steps through the starred presets; with none saved it falls back to all of them.
    int index;
    bool fellBack = false;

    if (settingsFile->getInt (presets::kKeyStepScope, 0) == 1)
    {
        const auto pool = navCatalog.favouriteIndices (settingsFile->getFavourites());
        fellBack = pool.empty();
        index = fellBack ? navCatalog.step (currentPresetIndex(), delta)
                         : presets::PresetCatalog::stepWithin (pool, currentPresetIndex(), delta);
    }
    else
    {
        index = navCatalog.step (currentPresetIndex(), delta);
    }

    if (index < 0)
        return;

    // Undo coalescing: a burst of steps (each within 3 s of the previous, nothing else pushed an Undo entry in between)
    // keeps only the snapshot from before the burst, so browsing 20 presets does not push real work out of the 32 steps.
    const auto now = juce::Time::getMillisecondCounter();
    const bool inBurst = lastPresetStepMs != 0 && now - lastPresetStepMs <= 3000u
                         && proc.getUndoPushCount() == undoPushesAfterLastStep;
    const auto pushesBefore = proc.getUndoPushCount();

    if (! loadCatalogEntry (navCatalog.entries()[(size_t) index]))
        return;

    if (inBurst && proc.getUndoPushCount() > pushesBefore)
        proc.discardNewestUndoSnapshot();

    lastPresetStepMs = juce::jmax<juce::uint32> (1u, now);
    undoPushesAfterLastStep = proc.getUndoPushCount();

    const auto total = (int) navCatalog.entries().size();
    showPresetStatus (fellBack ? juce::String ("No favourites yet - stepping all presets")
                               : "Preset " + juce::String (index + 1) + "/" + juce::String (total) + ": "
                                     + navCatalog.entries()[(size_t) index].name);
}

void GapFillAudioProcessorEditor::showPresetStatus (const juce::String& text)
{
    proc.getUiState().status.set (info::StatusKind::Preview, text);
    presetStatusUntilMs = juce::jmax<juce::uint32> (1u, juce::Time::getMillisecondCounter() + 3000u);
}

juce::String GapFillAudioProcessorEditor::currentPresetIdentity() const
{
    const auto index = currentPresetIndex();
    return index >= 0 ? navCatalog.entries()[(size_t) index].identity : juce::String();
}

void GapFillAudioProcessorEditor::toggleCurrentFavourite()
{
    refreshPresetList();
    const auto id = currentPresetIdentity();

    if (id.isEmpty())
    {
        showPresetStatus ("Load a preset first: a modified sound cannot be a favourite");
        return;
    }

    const bool now = ! settingsFile->isFavourite (id);
    settingsFile->setFavourite (id, now);
    showPresetStatus (now ? "Added to favourites: " + proc.getCurrentPresetName() : "Removed from favourites: " + proc.getCurrentPresetName());
}

bool GapFillAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();

    if (! mods.isCommandDown() || mods.isAltDown())
        return false;

    if (key.getKeyCode() == 'D' || key.getKeyCode() == 'd')
    {
        toggleCurrentFavourite();
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::upKey || key.getKeyCode() == juce::KeyPress::downKey)
    {
        stepPreset (key.getKeyCode() == juce::KeyPress::upKey ? -1 : +1);
        return true;
    }

    return false;
}

bool GapFillAudioProcessorEditor::loadCatalogEntry (const presets::PresetEntry& entry)
{
    juce::String error;

    if (! presets::loadPreset (proc, entry, error))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Could not open preset", error, "OK", this);
        setPresetDisplay (proc.getCurrentPresetName());
        return false;
    }

    currentPresetFile = entry.origin == presets::PresetEntry::Origin::User ? entry.file : juce::File();
    setPresetDisplay (entry.name);
    return true;
}

void GapFillAudioProcessorEditor::showMutationLab()
{
    auto panel = std::make_unique<mutation::LabPanel> (proc);
    juce::CallOutBox::launchAsynchronously (std::move (panel), fileButton.getScreenBounds(), this);
}

void GapFillAudioProcessorEditor::showPresetBrowser()
{
    auto browser = std::make_unique<presets::PresetBrowser> (proc, settings::GlobalSettings::shared());
    browser->onLoaded = [this] (const presets::PresetEntry& entry)
    {
        currentPresetFile = entry.origin == presets::PresetEntry::Origin::User ? entry.file : juce::File();
        setPresetDisplay (entry.name);
    };
    // The call-out box is created after the browser, so the callbacks reach it through a shared handle.
    auto boxHandle = std::make_shared<juce::Component::SafePointer<juce::CallOutBox>>();
    browser->onCloseRequested = [boxHandle] { if (auto* box = boxHandle->getComponent()) box->dismiss(); };
    browser->onSaveRequested = [this, boxHandle]
    {
        if (auto* box = boxHandle->getComponent())
            box->dismiss();

        saveUserPreset();
    };

    auto& box = juce::CallOutBox::launchAsynchronously (std::move (browser), presetBox.getScreenBounds(), this);
    box.setDismissalMouseClicksAreAlwaysConsumed (false);
    *boxHandle = &box;
}

void GapFillAudioProcessorEditor::saveUserPreset()
{
    auto* window = new juce::AlertWindow ("Save user preset",
                                          "Name, category and an optional description:",
                                          juce::MessageBoxIconType::NoIcon);
    gapfill::applyPaletteTo (*window);
    window->addTextEditor ("name", "My GapFill", "Name");
    window->addComboBox ("category", [] { auto c = presets::presetCategories(); c.add (presets::kUncategorised); return c; }(), "Category");
    window->addTextEditor ("description", {}, "Description");
    window->addButton ("Save", 1);
    window->addButton ("Cancel", 0);

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, window] (int result)
        {
            presets::PresetMeta meta;
            meta.name = window->getTextEditorContents ("name");
            meta.category = window->getComboBoxComponent ("category")->getText();
            meta.description = window->getTextEditorContents ("description");
            delete window;

            if (result != 1)
                return;

            const auto name = presets::sanitisePresetName (meta.name);

            if (name.isEmpty())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Could not save preset",
                                                        "Please choose a preset name that contains valid filename characters.",
                                                        "OK", this);
                return;
            }

            meta.name = name;
            const auto file = presets::presetPathFor (presets::userPresetRoot(), meta.category, name);

            const auto write = [this, file, meta]
            {
                juce::String error;

                if (! presets::writeUserPreset (proc, file, meta, error))
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                            "Could not save preset", error, "OK", this);
                    return;
                }

                currentPresetFile = file;
                refreshPresetList();
                setPresetDisplay (meta.name);
            };

            if (file.existsAsFile())
            {
                juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Replace preset?",
                                                    "A preset called \"" + name + "\" already exists in that category. Replace it?",
                                                    "Replace", "Cancel", this,
                                                    juce::ModalCallbackFunction::create ([write] (int r) { if (r == 1) write(); }));
                return;
            }

            write();
        }), false);
}

//==============================================================================
void GapFillAudioProcessorEditor::showFileMenu()
{
    enum { kSave = 1, kSaveAs, kOpen, kSavePreset, kOptions, kPerformance, kBrowse, kTour, kLab, kAudition, kHistory, kExportFill, kFavourite, kStepScope };

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
    menu.addItem (kBrowse, "Preset browser...");

    // F-S11: favourite the current factory preset from the menu (the browser stars every preset, user presets included).
    refreshPresetList();
    const auto favouriteId = currentPresetIdentity();
    const bool knownPreset = favouriteId.isNotEmpty();
    const bool isFavourite = knownPreset && settingsFile->isFavourite (favouriteId);
    menu.addItem (kFavourite, isFavourite ? "Remove preset from favourites  (Ctrl/Cmd+D)" : "Add preset to favourites  (Ctrl/Cmd+D)", knownPreset);
    menu.addItem (kStepScope, "Arrows step through favourites only", true,
                  settingsFile->getInt (presets::kKeyStepScope, 0) == 1);
    menu.addItem (kSavePreset, "Save as user preset...");
    menu.addSeparator();

    // Fills: the same actions as the AUDITION button and the RECIPES drawer (greyed out while those do not exist).
    const auto& actions = proc.getUiState().editorActions;
    menu.addItem (kAudition, "Audition a fill", actions.auditionNow != nullptr);
    menu.addItem (kHistory, "Fill history...", actions.openHistory != nullptr);
    menu.addItem (kExportFill, "Export last fill...", actions.exportLatestFill != nullptr);
    menu.addSeparator();
    menu.addItem (kOptions, "Options...");
    menu.addItem (kLab, "Mutation Lab...");
    menu.addItem (kTour, "Show quick tour");

    // UI-0: when a fourth tab does not fit, the Performance section lives here.
    if (performancePanel != nullptr && ! performanceAsTab)
        menu.addItem (kPerformance, "Performance...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fileButton),
                        [this] (int result)
    {
        switch (result)
        {
            case kSave:       doSave(); break;
            case kSaveAs:     doSaveAs(); break;
            case kOpen:       doOpen(); break;
            case kSavePreset: saveUserPreset(); break;
            case kBrowse:     showPresetBrowser(); break;
            case kFavourite:  toggleCurrentFavourite(); break;
            case kStepScope:  settingsFile->setInt (presets::kKeyStepScope, settingsFile->getInt (presets::kKeyStepScope, 0) == 1 ? 0 : 1);
                              break;
            case kTour:       showTour (true); break;
            case kLab:        showMutationLab(); break;
            case kAudition:   if (auto f = proc.getUiState().editorActions.auditionNow) f(); break;
            case kHistory:    if (auto f = proc.getUiState().editorActions.openHistory) f(); break;
            case kExportFill: if (auto f = proc.getUiState().editorActions.exportLatestFill) f(); break;
            case kOptions:    showOptions(); break;
            case kPerformance: setTab (Tab::Performance); break;
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
        setPresetDisplay (proc.getCurrentPresetName());
        return;
    }

    currentPresetFile = file;
    setPresetDisplay (file.getFileNameWithoutExtension());
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
        proc.replaceParameterStateKeepingPerformance (target);      // A/B never flips performance state (docs/dev/02 F0-1)

    proc.getRecipes().noteHeaderAbSide (usingSlotB);        // F-S06: level matching follows the sounding side
    abButton.setButtonText (usingSlotB ? "B" : "A");
    setPresetDisplay (usingSlotB ? "B" : "A");
}

/** F-M18: the header A/B snapshots become Beat Battle styles A and B (the side on screen is captured first). */
bool GapFillAudioProcessorEditor::feedBattleFromHeaderAb()
{
    auto& state = proc.getValueTreeState();
    (usingSlotB ? slotB : slotA) = state.copyState().createCopy();

    auto& battles = proc.getLiveControl().battles();
    const bool a = battles.setStyleFromState (0, state, slotA);
    const bool b = battles.setStyleFromState (1, state, slotB);
    return a && b;
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

    // F-D04: contrast / control size / focus ring changed in Options (or by another instance).
    if (settingsFile->getInt (access::kKeyContrast, 0) * 100 + settingsFile->getInt (access::kKeyControlSize, 0) * 10
            + settingsFile->getInt (access::kKeyFocusRing, 2) != accessSignature)
        applyAccessibility();

    // U-8: the Options combo writes the per-user setting; apply it here (also picks up another instance's change).
    if (uiscale::snapScalePercent (settingsFile->getInt (settings::GlobalSettings::keyUiScale, 100)) != lastRequestedScale)
        applyScale();

    // U-5: the view mode can change from the toggle slot or a restored project state.
    if (proc.getUiState().getViewMode() != appliedView)
        applyViewMode();

    // U-4: previous / next preset requests from MIDI commands (X-1) or other threads.
    if (const auto step = proc.getUiState().takePresetStep(); step != 0)
        for (int i = 0; i < juce::jmin (8, std::abs (step)); ++i)
            stepPreset (step > 0 ? 1 : -1);

    // U-9: the footer shows the highest-priority message (learn > ... > capture here).
    auto& board = proc.getUiState().status;

    if (presetStatusUntilMs != 0 && (juce::int32) (juce::Time::getMillisecondCounter() - presetStatusUntilMs) >= 0)
    {
        presetStatusUntilMs = 0;
        board.clear (info::StatusKind::Preview);
    }

    board.set (info::StatusKind::Learn, midiLearn.isLearning() ? status : juce::String());
    board.set (info::StatusKind::Capture, meters.captureActive.load() && ! midiLearn.isLearning()
                                              ? juce::String ("Capture is on - fills use the captured source")
                                              : juce::String());

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
    auto b = getLocalBounds();
    proc.setSavedEditorSize ({ getWidth(), getHeight() });

    // ---- top bar ----------------------------------------------------------
    {
        auto top = b.removeFromTop (kTopBarHeight).reduced (10, 6);
        top.removeFromLeft (10);

        auto brand = top.removeFromLeft (150);
        logo.setBounds (brand.removeFromTop (juce::jmax (18, brand.getHeight() - 11)));
        tagline.setBounds (brand);

        top.removeFromLeft (12);

        bypassButton.setBounds (top.removeFromRight (78).reduced (2, 0));
        top.removeFromRight (4);
        helpButton.setBounds (top.removeFromRight (62).reduced (2, 0));
        resetButton.setBounds (top.removeFromRight (64).reduced (2, 0));
        undoButton.setBounds (top.removeFromRight (58).reduced (2, 0));
        surpriseButton.setBounds (top.removeFromRight (84).reduced (2, 0));
        randomAllButton.setBounds (top.removeFromRight (92).reduced (2, 0));
        randomButton.setBounds (top.removeFromRight (80).reduced (2, 0));
        top.removeFromRight (6);
        copyButton.setBounds (top.removeFromRight (52).reduced (2, 0));
        abButton.setBounds (top.removeFromRight (34).reduced (2, 0));
        top.removeFromRight (8);

        prevPreset.setBounds (top.removeFromLeft (26).reduced (1, 0));
        nextPreset.setBounds (top.removeFromLeft (26).reduced (1, 0));
        top.removeFromLeft (4);
        fileButton.setBounds (top.removeFromRight (56).reduced (2, 0));
        presetBox.setBounds (top.removeFromLeft (juce::jmin (230, juce::jmax (110, top.getWidth() - 70))).reduced (1, 1));
        latencyLabel.setBounds (top.reduced (6, 0));
    }

    b.reduce (10, 8);
    b.removeFromBottom (kFooterHeight);

    // ---- mix strip --------------------------------------------------------
    mixPanel.setBounds (b.removeFromBottom (kMixHeight));
    b.removeFromBottom (8);

    // ---- control row ------------------------------------------------------
    auto controlHeight = juce::jlimit (250, 360, (int) ((float) b.getHeight() * 0.54f));

    // UI-0: the Effects section (Creative tab only) takes its header, and its body
    // when unfolded, from the visualiser; it never shrinks the existing controls.
    int effectsExtra = 0;

    if (effectsDrawer != nullptr && currentTab == Tab::Creative)
    {
        effectsExtra = EffectsDrawer::headerHeight + (effectsDrawer->isExpanded() ? kEffectsBody : 0);
        effectsExtra = juce::jmin (effectsExtra, juce::jmax (EffectsDrawer::headerHeight,
                                                              b.getHeight() - 8 - controlHeight - kMinVisualiserHeight));
        controlHeight += effectsExtra;
    }

    auto controls = b.removeFromBottom (controlHeight);
    b.removeFromBottom (8);

    const bool simple = proc.getUiState().getViewMode() == ui::ViewMode::Simple;
    const auto sideWidth = simple ? 150 : juce::jlimit (230, 320, controls.getWidth() / 4);
    detectionPanel.setBounds (controls.removeFromLeft (sideWidth));
    controls.removeFromLeft (8);
    protectionPanel.setBounds (controls.removeFromRight (simple ? 150 : juce::jlimit (215, 290, sideWidth - 20)));
    controls.removeFromRight (8);

    // Tabs sit above the middle panel.
    auto tabs = controls.removeFromTop (kTabHeight);

    // U-5: the SIMPLE | ADVANCED switch (Slot::TopBarRight) sits at the right end of the tab row.
    const auto toggleWidth = simple ? kViewToggleWidth + 170 : kViewToggleWidth;
    slots->setSlotBounds (Slot::TopBarRight, tabs.removeFromRight (juce::jmin (toggleWidth, tabs.getWidth() / 2)).reduced (0, 2));
    tabs.removeFromRight (6);

    const auto tabWidth = juce::jmin (130, (tabs.getWidth() - 20) / 3);
    tabCreative.setBounds (tabs.removeFromLeft (tabWidth).reduced (1));
    tabWeights.setVisible (! simple);
    tabSpectral.setVisible (! simple);

    if (! simple)
    {
        tabWeights.setBounds (tabs.removeFromLeft (tabWidth + 20).reduced (1));
        tabSpectral.setBounds (tabs.removeFromLeft (tabWidth).reduced (1));
    }

    const auto plan = planTabs (simple ? tabs.getWidth() + tabWidth + 2 * 130 + 20 : tabs.getWidth() + tabWidth * 2 + tabWidth + 20,
                                performancePanel != nullptr);
    performanceAsTab = plan.performanceAsTab;
    tabPerformance.setVisible (performanceAsTab);

    if (performanceAsTab)
        tabPerformance.setBounds (tabs.removeFromLeft (plan.performanceTabWidth).reduced (1));
    else if (currentTab == Tab::Performance && performancePanel == nullptr)
        setTab (Tab::Creative);              // the tab vanished (window narrowed): fall back safely

    if (effectsDrawer != nullptr)
        effectsDrawer->setBounds (controls.removeFromBottom (effectsExtra));

    creativePanel.setBounds (controls);
    weightsPanel.setBounds (controls);
    spectralPanel.setBounds (controls);

    if (performancePanel != nullptr)
        performancePanel->setBounds (controls);

    // One reserved rectangle per slot. Slots no feature has claimed yet stay
    // empty (their components are hidden); a feature reserves its rectangle
    // here the first time it registers a component for that slot.
    slots->setSlotBounds (Slot::CreativeExtra,
                          creativePanel.getExtraSlotBounds().translated (creativePanel.getX(), creativePanel.getY()));

    // ---- visualiser takes whatever is left --------------------------------
    // Performance strip (e.g. freeze & scrub): carved from the visualiser, which keeps >= 70 px.
    if (slots->getNumComponents (Slot::PerformanceStrip) > 0 && b.getHeight() >= kPerformanceStripHeight + kMinVisualiserHeight + 6)
    {
        slots->setSlotBounds (Slot::PerformanceStrip, b.removeFromBottom (kPerformanceStripHeight));
        b.removeFromBottom (6);
    }
    else
    {
        slots->setSlotBounds (Slot::PerformanceStrip, {});
    }

    visualiser.setBounds (b);
    slots->setSlotBounds (Slot::VisualiserOverlay, b);

    if (calibrateCard != nullptr)
    {
        const auto w = juce::jmin (calibrate::CalibrateCard::preferredWidth, b.getWidth() - 16);
        calibrateCard->setBounds (juce::Rectangle<int> (w, calibrate::CalibrateCard::preferredHeight)
                                    .withCentre ({ b.getCentreX(), b.getBottom() - calibrate::CalibrateCard::preferredHeight / 2 - 12 }));
    }
    slots->setSlotBounds (Slot::FooterStatus, footerBounds());

    helpView.setBounds (getLocalBounds().withTrimmedTop (kTopBarHeight).reduced (24, 16));

    if (tourOverlay != nullptr)
        tourOverlay->setBounds (getLocalBounds());

    if (helpView.isVisible())
        helpView.resized();

    // The options page is a small panel rather than a full overlay; the OptionsExtra slot (interface size,
    // accessibility) lives in the free area under the device row, so the panel grew by 140 px.
    optionsView.setBounds (juce::Rectangle<int> (520, juce::jmin (640, getHeight() - 16))
                             .withCentre (getLocalBounds().getCentre()));

    // The extra rows scroll inside the panel's viewport; each keeps its own height (Stack slot).
    for (int pass = 0; pass < 2; ++pass)      // the second pass accounts for a scroll bar that the first one made necessary
        slots->setSlotBounds (Slot::OptionsExtra, { 0, 0, juce::jmax (1, optionsView.getExtraViewport().getMaximumVisibleWidth()),
                                                    juce::jmax (1, slots->getStackHeight (Slot::OptionsExtra)) });
}

} // namespace gapfill
