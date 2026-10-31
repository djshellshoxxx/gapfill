#pragma once

// Status-detail row layout and ownership, separated from health presentation.
#include "Gui.h"
#include "../i18n/Strings.h"
#include <cmath>

namespace gapfill::health
{

class DetailsPanel::Rows : public juce::Component
{
public:
    explicit Rows (juce::Component& owner) : panel (owner) {}

    void addHeader (const juce::String& text)
    {
        auto l = std::make_unique<juce::Label> ("", i18n::text (text));
        l->setFont (fonts::sectionHeader (11.0f));
        l->setColour (juce::Label::textColourId, colours::accent);
        l->setInterceptsMouseClicks (false, false);
        plain << "\n" << l->getText() << "\n";
        add (std::move (l), 20);
    }

    void addText (const juce::String& text, juce::Colour colour = colours::text, float size = 11.5f)
    {
        auto l = std::make_unique<juce::Label> ("", i18n::text (text));
        l->setFont (fonts::ui (size));
        l->setColour (juce::Label::textColourId, colour);
        l->setMinimumHorizontalScale (1.0f);
        l->setJustificationType (juce::Justification::topLeft);
        l->setInterceptsMouseClicks (false, false);
        wrapped.push_back ({ l.get(), size });
        plain << l->getText() << "\n";
        add (std::move (l), 16);
    }

    void addToggle (const juce::String& text, bool state, std::function<void (bool)> onChange)
    {
        auto t = std::make_unique<juce::ToggleButton> (i18n::text (text));
        t->setToggleState (state, juce::dontSendNotification);
        t->setColour (juce::ToggleButton::textColourId, colours::text);
        t->setColour (juce::ToggleButton::tickColourId, colours::accent);
        auto* raw = t.get();
        t->onClick = [raw, onChange] { onChange (raw->getToggleState()); };
        add (std::move (t), 24);
        plain << "[" << (state ? "x" : " ") << "] " << raw->getButtonText() << "\n";
    }

    void addButtons (std::vector<std::pair<juce::String, std::function<void()>>> buttons)
    {
        auto row = std::make_unique<juce::Component>();

        for (auto& b : buttons)
        {
            auto btn = std::make_unique<juce::TextButton> (i18n::text (b.first));
            auto fn = b.second;
            btn->onClick = [fn] { fn(); };
            row->addAndMakeVisible (*btn);
            buttonsOwned.push_back (std::move (btn));
            plain << "<" << buttonsOwned.back()->getButtonText() << "> ";
        }

        plain << "\n";
        auto* raw = row.get();
        buttonRows.push_back ({ raw, (int) buttons.size(), (int) buttonsOwned.size() - (int) buttons.size() });
        add (std::move (row), metrics::buttonHeight);
    }

    int layoutFor (int width)
    {
        int y = 0;

        for (auto& e : entries)
        {
            int h = e.height;

            for (auto& w : wrapped)
                if (w.label == e.comp.get())
                {
                    juce::AttributedString as;
                    as.append (w.label->getText(), fonts::ui (w.size));
                    juce::TextLayout tl;
                    tl.createLayout (as, (float) width);
                    h = juce::jmax (e.height, (int) std::ceil (tl.getHeight()) + 2);
                }

            e.comp->setBounds (0, y, width, h);

            for (auto& br : buttonRows)
                if (br.row == e.comp.get())
                {
                    int x = 0;

                    for (int i = 0; i < br.count; ++i)
                    {
                        auto* btn = buttonsOwned[(size_t) (br.first + i)].get();
                        const auto bw = juce::jmax (60, btn->getButtonText().length() * 8 + 24);
                        btn->setBounds (x, 0, bw, h);
                        x += bw + 8;
                    }
                }

            y += h + kGap;
        }

        return y;
    }

    juce::String plain;

private:
    static constexpr int kGap = 6;
    struct Entry { std::unique_ptr<juce::Component> comp; int height; };
    struct Wrapped { juce::Label* label; float size; };
    struct ButtonRow { juce::Component* row; int count; int first; };

    void add (std::unique_ptr<juce::Component> c, int height)
    {
        addAndMakeVisible (*c);
        entries.push_back ({ std::move (c), height });
    }

    juce::Component& panel;
    std::vector<Entry> entries;
    std::vector<Wrapped> wrapped;
    std::vector<std::unique_ptr<juce::TextButton>> buttonsOwned;
    std::vector<ButtonRow> buttonRows;
};

} // namespace gapfill::health
