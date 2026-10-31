#include "../support/TestRig.h"
#include "UiTestUtil.h"

#include "PluginEditor.h"
#include "features/explain/OverlaySlot.h"
#include "features/i18n/Strings.h"
#include "features/presetbrowser/GlobalSettings.h"
#include "features/timeline/Gui.h"

#include <iostream>

using namespace gapfill;
using namespace gapfill::timeline;
using namespace gftest;

namespace
{
    struct VisualSettings
    {
        VisualSettings()
        {
            file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("gapfill-timeline-" + juce::Uuid().toString() + ".xml");
            settings::GlobalSettings::useFileForTests (file);
            const auto wanted = juce::SystemStats::getEnvironmentVariable ("GF_GUI_LANGUAGE", "en");
            language = std::make_unique<i18n::ScopedLanguage> (wanted);
            GF_EXPECT (i18n::currentLanguage() == wanted);
            settings::GlobalSettings::shared()->setString (settings::GlobalSettings::keyLanguage, wanted);
            settings::GlobalSettings::shared()->setBool (tour::kKeySuppress, true);
        }
        ~VisualSettings()
        {
            language.reset();
            settings::GlobalSettings::useFileForTests (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                         .getChildFile ("gapfill-timeline-reset.xml"));
            file.deleteFile();
        }
        juce::File file;
        std::unique_ptr<i18n::ScopedLanguage> language;
    };

    template <typename Type>
    Type* find (juce::Component& root)
    {
        for (auto* child : root.getChildren())
        {
            if (auto* matched = dynamic_cast<Type*> (child))
                return matched;
            if (auto* matched = find<Type> (*child))
                return matched;
        }
        return nullptr;
    }

    /** Deterministic display fixture. These are synthetic engine-shaped records,
        not evidence that an audio producer or a DAW was exercised. */
    void populate (GapFillAudioProcessor& proc)
    {
        auto& model = proc.getExplainState().timelineModel;
        constexpr double rate = 192000.0;
        constexpr int columns = FrameHistory::maxColumns, period = 128;
        model.frames.configure (rate, 16.0);
        proc.getExplainState().timelineZoomSeconds.store (16);
        const auto end = proc.getSourceHistoryWritePosition() - proc.getActiveLatencySamples();
        for (int i = 0; i < columns; ++i)
        {
            VizFrame frame;
            const auto position = end - (std::int64_t) (columns - i - 1) * period;
            frame.posLo = (std::uint32_t) position;
            frame.periodSamples = period;
            frame.sampleRateHz = (std::uint32_t) rate;
            frame.peak = 0.2f + 0.6f * std::abs (std::sin ((float) i * 0.013f));
            frame.energy = frame.peak * 0.7f;
            frame.conf = 0.5f + 0.5f * std::sin ((float) i * 0.021f);
            frame.bandConf[0] = frame.conf;
            frame.bandConf[1] = 1.0f - frame.conf;
            frame.bandConf[2] = frame.conf * 0.6f;
            frame.flags = VizFrame::flagProtected | (frame.conf > 0.65f ? VizFrame::flagGapEligible : 0);
            frame.protectionKinds = 7;
            frame.ppqTick = i * 2;
            frame.timeSigNumerator = frame.timeSigDenominator = 4;
            GF_EXPECT (model.pushFrame (frame, end));
        }
        for (int i = 0; i < TimelineModel::capacity; ++i)
        {
            TimelineEvent event;
            event.pos = end - (std::int64_t) (TimelineModel::capacity - i - 1) * 5900;
            event.type = i % 3 == 0 ? EventType::Onset : i % 3 == 1 ? EventType::GapEval : EventType::FillStart;
            event.reason = (std::uint8_t) (i % numRejectReasons);
            event.availMs = 38;
            event.needMs = 60;
            event.lenMs = 100;
            event.algo = (std::uint8_t) (i % numAlgos);
            event.protHoldMs = 35;
            event.lookaheadMs = 40;
            model.push (event);
        }
    }

    void snapshot (GapFillAudioProcessorEditor& editor, const juce::String& name, int requestedScale)
    {
        const auto directory = juce::SystemStats::getEnvironmentVariable ("GF_SCREENSHOT_DIR", {});
        if (directory.isEmpty())
            return;
        GF_EXPECT_MSG (editor.getAppliedScalePercentForTests() == requestedScale,
                       "Capture requires the requested scale; use a 6000x4000 virtual display");
        const auto rasterScale = (float) requestedScale / 100.0f;
        const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, rasterScale);
        GF_EXPECT (image.getWidth() == juce::roundToInt ((float) editor.getWidth() * rasterScale));
        juce::File target (directory);
        GF_EXPECT (target.createDirectory().wasOk());
        juce::FileOutputStream stream (target.getChildFile (name + ".png"));
        GF_EXPECT (stream.openedOk());
        stream.setPosition (0);
        stream.truncate();
        GF_EXPECT (juce::PNGImageFormat().writeImageToStream (image, stream));
    }

    void safetyControlsRemainAccessible (GapFillAudioProcessorEditor& editor,
                                         explain::VisualiserOverlayComponent& overlay)
    {
        auto* mix = find<MixPanel> (editor);
        GF_EXPECT (mix != nullptr && visibleInTree (*mix, editor));
        const auto expanded = editor.getLocalArea (&overlay, overlay.getLocalBounds());
        GF_EXPECT (! expanded.intersects (editor.getLocalArea (mix, mix->getLocalBounds())));
        std::vector<juce::Component*> components;
        collectAll (editor, components);
        bool bypassFound = false, panicFound = false;
        for (auto* component : components)
        {
            const bool bypass = dynamic_cast<juce::Button*> (component) != nullptr
                                && component->getProperties()[kParamIdProperty].toString() == "bypass";
            const bool panic = component->getTitle() == i18n::text ("Panic: silence GapFill effects now");
            if (! bypass && ! panic)
                continue;
            GF_EXPECT (visibleInTree (*component, editor) && component->isEnabled());
            const auto point = editor.getLocalPoint (component, juce::Point<int> (juce::jmin (75, component->getWidth() / 2), component->getHeight() / 2));
            GF_EXPECT (! expanded.contains (point));
            GF_EXPECT (editor.getComponentAt (point) == component);
            bypassFound |= bypass;
            panicFound |= panic;
        }
        GF_EXPECT (bypassFound && panicFound);
    }
}

GF_TEST (TimelineVisual, expanded_keyboard_views_keep_mix_and_safety_controls_across_size_scale_matrix)
{
    VisualSettings settingsScope;
    for (const auto size : { juce::Point<int> (1020, 620), { 1100, 700 }, { 1800, 1000 } })
        for (const int scale : { 100, 150, 200 })
        {
            settings::GlobalSettings::shared()->setInt (settings::GlobalSettings::keyUiScale, scale);
            GapFillAudioProcessor proc;
            prepareProcessor (proc, 192000.0, 512, false);
            proc.setKeyboardNavigationEnabled (true);
            proc.getUiState().setViewMode (ui::ViewMode::Advanced);
            GapFillAudioProcessorEditor editor (proc);
            editor.setSize (size.x, size.y);
            editor.applyScaleForTests();
            editor.applyViewModeForTests();
            editor.applyKeyboardNavigationSettingForTests();
            auto* overlay = find<explain::VisualiserOverlayComponent> (editor);
            GF_EXPECT (overlay != nullptr);
            populate (proc);
            const auto originalSize = editor.getLocalBounds();
            overlay->setTimelineExpanded (true);
            editor.resized();
            auto& view = overlay->getTimeline();
            view.refreshForTests();
            GF_EXPECT (editor.getLocalBounds() == originalSize);
            GF_EXPECT (visibleInTree (view, editor) && view.getHeight() > 100);
            safetyControlsRemainAccessible (editor, *overlay);
            const auto name = "timeline-" + i18n::currentLanguage() + "-" + juce::String (size.x) + "x" + juce::String (size.y)
                            + "-scale" + juce::String (scale);
            snapshot (editor, name + "-live-fixture", scale);
            GF_EXPECT (view.keyPressed (juce::KeyPress ('f')));
            GF_EXPECT (view.keyPressed (juce::KeyPress ('i')));
            GF_EXPECT (view.keyPressed (juce::KeyPress (juce::KeyPress::leftKey)));
            snapshot (editor, name + "-inspect-fixture", scale);
            GF_EXPECT (view.keyPressed (juce::KeyPress ('l')) && view.isEventListShown());
            snapshot (editor, name + "-list-fixture", scale);
            safetyControlsRemainAccessible (editor, *overlay);
        }
}

GF_TEST (TimelineVisual, reduced_motion_sets_the_actual_timeline_timer_to_four_hz)
{
    VisualSettings settingsScope;
    GapFillAudioProcessor proc;
    TimelineView view (proc);
    view.setSize (1000, 300);
    proc.setReducedMotionEnabled (true);
    view.visibilityChanged();
    GF_EXPECT (view.getRefreshIntervalMs() == 250);
    proc.setReducedMotionEnabled (false);
    view.visibilityChanged();
    GF_EXPECT (view.getRefreshIntervalMs() == 50);
}

GF_TEST (TimelineVisual, dense_paint_budget_reports_real_timings_when_explicitly_requested)
{
    if (juce::SystemStats::getEnvironmentVariable ("GF_TIMELINE_PAINT_BUDGET", {}).isEmpty())
        return;
    VisualSettings settingsScope;
    settings::GlobalSettings::shared()->setInt (settings::GlobalSettings::keyUiScale, 100);
    for (const auto size : { juce::Point<int> (1020, 620), { 1100, 700 }, { 1800, 1000 } })
    {
        GapFillAudioProcessor proc;
        prepareProcessor (proc, 192000.0, 512, false);
        setParams (proc, { { "detMode", (float) DetMode::Spectral } });
        proc.getUiState().setViewMode (ui::ViewMode::Advanced);
        GapFillAudioProcessorEditor editor (proc);
        editor.setSize (size.x, size.y);
        editor.applyViewModeForTests();
        auto* overlay = find<explain::VisualiserOverlayComponent> (editor);
        GF_EXPECT (overlay != nullptr);
        populate (proc);
        overlay->setTimelineExpanded (true);
        editor.resized();
        auto& view = overlay->getTimeline();
        view.refreshForTests();
        for (const auto scale : { 1.0f, 1.5f, 2.0f })
        {
            juce::Image image (juce::Image::ARGB, juce::roundToInt ((float) view.getWidth() * scale),
                               juce::roundToInt ((float) view.getHeight() * scale), true);
            juce::Graphics graphics (image);
            graphics.addTransform (juce::AffineTransform::scale (scale));
            for (int warmup = 0; warmup < 10; ++warmup)
                view.paintEntireComponent (graphics, true);
            std::array<double, 100> milliseconds {};
            for (auto& elapsed : milliseconds)
            {
                const auto start = juce::Time::getHighResolutionTicks();
                view.paintEntireComponent (graphics, true);
                elapsed = 1000.0 * juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);
            }
            std::sort (milliseconds.begin(), milliseconds.end());
            std::cout << "  TIMELINE_PAINT fixture columns=24000 events=512 logical=" << view.getWidth() << "x" << view.getHeight() << " scale=" << scale
                      << " p50_ms=" << milliseconds[50] << " p95_ms=" << milliseconds[95] << " max_ms=" << milliseconds.back() << std::endl;
            GF_EXPECT_MSG (milliseconds.back() < 2.0, "Expanded timeline paint exceeded the unchanged 2 ms target");
        }
    }
}
