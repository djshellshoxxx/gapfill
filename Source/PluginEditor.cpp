#include "PluginEditor.h"

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
    constexpr int kFooterHeight = 12;   // version marker, bottom-right
    constexpr int kMinWidth     = 880;
    constexpr int kMinHeight    = 620;

    constexpr int kFactoryOffset = 1;
    constexpr int kUserOffset    = 1000;
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
          &randomButton, &randomAllButton, &resetButton, &helpButton, &bypassButton };

    for (auto* b : topBarButtons)
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    randomButton.setColour (juce::TextButton::buttonColourId, colours::accentWarm.withAlpha (0.35f));

    // ---- main areas --------------------------------------------------------
    addAndMakeVisible (visualiser);
    addAndMakeVisible (detectionPanel);
    addAndMakeVisible (creativePanel);
    addChildComponent (weightsPanel);
    addChildComponent (spectralPanel);
    addAndMakeVisible (protectionPanel);
    addAndMakeVisible (mixPanel);

    for (auto* b : { &tabCreative, &tabWeights, &tabSpectral })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1001);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    tabCreative.onClick = [this] { setTab (Tab::Creative); };
    tabWeights.onClick  = [this] { setTab (Tab::Weights); };
    tabSpectral.onClick = [this] { setTab (Tab::Spectral); };
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
    addChildComponent (optionsView);

    applyTooltipSetting();

    // ---- A/B snapshots -----------------------------------------------------
    slotA = proc.getValueTreeState().copyState().createCopy();
    slotB = slotA.createCopy();

    proc.getVisualisationFifo().setEnabled (true);

    const auto saved = proc.getSavedEditorSize();
    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, 2600, 1800);
    setSize (juce::jmax (kMinWidth, saved.x), juce::jmax (kMinHeight, saved.y));

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
    creativePanel.setVisible (newTab == Tab::Creative);
    weightsPanel.setVisible (newTab == Tab::Weights);
    spectralPanel.setVisible (newTab == Tab::Spectral);

    tabCreative.setToggleState (newTab == Tab::Creative, juce::dontSendNotification);
    tabWeights.setToggleState (newTab == Tab::Weights, juce::dontSendNotification);
    tabSpectral.setToggleState (newTab == Tab::Spectral, juce::dontSendNotification);

    resized();
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
        {
            if (auto xml = juce::XmlDocument::parse (userPresetFiles[index]))
            {
                auto tree = juce::ValueTree::fromXml (*xml);

                if (tree.isValid() && tree.hasType (proc.getValueTreeState().state.getType()))
                {
                    proc.getValueTreeState().replaceState (tree);
                    currentPresetFile = userPresetFiles[index];
                }
            }
        }
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
                return;

            auto file = dir.getChildFile (juce::File::createLegalFileName (name) + ".gapfill");

            if (auto xml = proc.getValueTreeState().copyState().createXml())
                xml->writeTo (file);

            currentPresetFile = file;
            refreshPresetList();

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

void GapFillAudioProcessorEditor::savePresetToFile (const juce::File& file)
{
    if (file == juce::File())
        return;

    if (auto xml = proc.getValueTreeState().copyState().createXml())
    {
        if (xml->writeTo (file))
        {
            currentPresetFile = file;

            // A file saved into the user preset folder should show up in the
            // dropdown straight away.
            refreshPresetList();
        }
    }
}

void GapFillAudioProcessorEditor::loadPresetFromFile (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr)
        return;

    auto tree = juce::ValueTree::fromXml (*xml);

    if (! tree.isValid() || ! tree.hasType (proc.getValueTreeState().state.getType()))
        return;

    proc.getValueTreeState().replaceState (tree);
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
    optionsView.refresh (proc.isTooltipsEnabled());
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

    // Status line: what the detector is doing right now.
    juce::String status;

    if (meters.sidechainRequested.load() && ! meters.sidechainConnected.load())
        status = "Sidechain selected but not connected - using main detection";
    else if (meters.sidechainConnected.load() && meters.sidechainRequested.load())
        status = "Sidechain detection active";
    else
        status = "Main detection";

    const auto algo = meters.currentAlgo.load();

    if (algo >= 0 && algo < numAlgos)
        status += juce::String ("  |  filling: ") + algoName ((Algo) algo);

    mixPanel.setStatusText (status);
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
    const auto controlHeight = juce::jlimit (250, 360, (int) ((float) b.getHeight() * 0.54f));
    auto controls = b.removeFromBottom (controlHeight);
    b.removeFromBottom (8);

    const auto sideWidth = juce::jlimit (230, 320, controls.getWidth() / 4);
    detectionPanel.setBounds (controls.removeFromLeft (sideWidth));
    controls.removeFromLeft (8);
    protectionPanel.setBounds (controls.removeFromRight (juce::jlimit (215, 290, sideWidth - 20)));
    controls.removeFromRight (8);

    // Tabs sit above the middle panel.
    auto tabs = controls.removeFromTop (kTabHeight);
    const auto tabWidth = juce::jmin (130, tabs.getWidth() / 3);
    tabCreative.setBounds (tabs.removeFromLeft (tabWidth).reduced (1));
    tabWeights.setBounds (tabs.removeFromLeft (tabWidth + 20).reduced (1));
    tabSpectral.setBounds (tabs.removeFromLeft (tabWidth).reduced (1));

    creativePanel.setBounds (controls);
    weightsPanel.setBounds (controls);
    spectralPanel.setBounds (controls);

    // ---- visualiser takes whatever is left --------------------------------
    visualiser.setBounds (b);

    helpView.setBounds (getLocalBounds().withTrimmedTop (kTopBarHeight).reduced (24, 16));

    if (helpView.isVisible())
        helpView.resized();

    // The options page is a small panel rather than a full overlay.
    optionsView.setBounds (juce::Rectangle<int> (480, 300)
                             .withCentre (getLocalBounds().getCentre()));
}

} // namespace gapfill
