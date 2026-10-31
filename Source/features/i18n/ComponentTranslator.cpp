#include "ComponentTranslator.h"
#include "Strings.h"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <vector>

namespace gapfill::i18n
{
namespace
{
    const juce::Identifier userContentProperty { "gf.i18n.userContent" };
    constexpr size_t maxComponents = 4096;

    bool isUserText (const juce::Component& component)
    {
        for (auto* c = &component; c != nullptr; c = c->getParentComponent())
            if ((bool) c->getProperties()[userContentProperty] || dynamic_cast<const juce::TextEditor*> (c) != nullptr)
                return true;
        // A ComboBox owns its selected-text Label; translating it twice would
        // lose the original or touch a user-data choice after the box was skipped.
        return dynamic_cast<const juce::ComboBox*> (component.getParentComponent()) != nullptr;
    }

    struct Field
    {
        juce::String original, applied;
        Field() = default;
        explicit Field (juce::String value) : original (value), applied (std::move (value)) {}
    };

    template <typename Setter>
    void update (Field& field, const juce::String& current, bool languageChanged, Setter set)
    {
        if (! languageChanged && current == field.applied)
            return;
        if (current != field.applied)
            field.original = current;           // a genuine model/UI update, not our own translation
        const auto translated = text (field.original);
        if (current != translated)
            set (translated);
        field.applied = translated;
    }

    struct Node
    {
        explicit Node (juce::Component& c) : component (&c), title (c.getTitle()), description (c.getDescription()), help (c.getHelpText())
        {
            tooltipClient = dynamic_cast<juce::SettableTooltipClient*> (&c);
            if (tooltipClient != nullptr)
                tooltip = Field (tooltipClient->getTooltip());
            if (auto* label = dynamic_cast<juce::Label*> (&c))
            {
                kind = Label;
                content = Field (label->getText());
                labelFont = label->getFont();
                appliedFont = labelFont;
            }
            else if (auto* button = dynamic_cast<juce::Button*> (&c))
            {
                kind = Button;
                content = Field (button->getButtonText());
            }
            else if (dynamic_cast<juce::ComboBox*> (&c) != nullptr)
                kind = Combo;
        }
        juce::Component::SafePointer<juce::Component> component;
        enum Kind { Other, Label, Button, Combo } kind = Other;
        juce::SettableTooltipClient* tooltipClient = nullptr;
        Field title, description, help, tooltip, content;
        std::map<int, Field> choices;
        std::optional<juce::Font> labelFont, appliedFont;
        bool initial = true;
    };
}

void markUserContent (juce::Component& component)
{
    component.getProperties().set (userContentProperty, true);
}

juce::Font fontForText (juce::Font preferred, const juce::String& displayed)
{
    const auto face = preferred.getTypefacePtr();
    for (auto cursor = displayed.getCharPointer(); ! cursor.isEmpty();)
    {
        const auto ch = cursor.getAndAdvance();
        if (ch <= 32)
            continue;
        const auto glyph = face != nullptr ? face->getNominalGlyphForCodepoint (ch) : std::nullopt;
        if (! glyph.has_value() || *glyph == 0)
        {
            auto fallback = juce::Font (juce::FontOptions (preferred.getHeight()));
            if (preferred.isBold()) fallback = fallback.boldened();
            if (preferred.isItalic()) fallback = fallback.italicised();
            return fallback.withHorizontalScale (preferred.getHorizontalScale());
        }
    }
    return preferred;
}

struct ComponentTranslator::Impl
{
    explicit Impl (juce::Component& c) : root (&c) {}
    juce::Component::SafePointer<juce::Component> root;
    std::vector<Node> nodes;
    std::uint64_t generation = std::numeric_limits<std::uint64_t>::max();
    bool scanNeeded = true;
};

ComponentTranslator::ComponentTranslator (juce::Component& root) : impl (std::make_unique<Impl> (root))
{
    languageChanges().addChangeListener (this);
    refresh();
}

ComponentTranslator::~ComponentTranslator()
{
    languageChanges().removeChangeListener (this);
    for (const auto& node : impl->nodes)
        if (node.component != nullptr)
            node.component->removeComponentListener (this);
}

void ComponentTranslator::refresh()
{
    if (impl->scanNeeded)
    {
        impl->scanNeeded = false;
        impl->nodes.erase (std::remove_if (impl->nodes.begin(), impl->nodes.end(),
                          [] (const Node& node) { return node.component == nullptr; }), impl->nodes.end());
        const auto visit = [this] (auto&& self, juce::Component& c) -> void
        {
            if (impl->nodes.size() >= maxComponents)
                return;
            if (std::none_of (impl->nodes.begin(), impl->nodes.end(),
                             [&c] (const Node& n) { return n.component.getComponent() == &c; }))
            {
                impl->nodes.emplace_back (c);
                c.addComponentListener (this);
            }
            for (auto* child : c.getChildren())
                self (self, *child);
        };
        if (impl->root != nullptr)
            visit (visit, *impl->root);
    }
    const auto generation = languageGeneration();
    const auto changed = generation != impl->generation;
    if (! changed && currentLanguage() == "en")
        return;
    impl->generation = generation;
    for (auto& node : impl->nodes)
    {
        auto* component = node.component.getComponent();
        if (component == nullptr)
            continue;
        const auto fieldChanged = changed || node.initial;
        node.initial = false;
        update (node.title, component->getTitle(), fieldChanged, [component] (const juce::String& v) { component->setTitle (v); });
        update (node.description, component->getDescription(), fieldChanged, [component] (const juce::String& v) { component->setDescription (v); });
        update (node.help, component->getHelpText(), fieldChanged, [component] (const juce::String& v) { component->setHelpText (v); });
        if (node.tooltipClient != nullptr)
            update (node.tooltip, node.tooltipClient->getTooltip(), fieldChanged, [&node] (const juce::String& v) { node.tooltipClient->setTooltip (v); });
        if (isUserText (*component))
            continue;
        switch (node.kind)
        {
            case Node::Label:
            {
                auto* label = static_cast<juce::Label*> (component);
                update (node.content, label->getText(), fieldChanged, [label, &node] (const juce::String& v)
                {
                    if (node.appliedFont && label->getFont() != *node.appliedFont)
                        node.labelFont = label->getFont();
                    const auto font = fontForText (node.labelFont.value_or (label->getFont()), v);
                    label->setFont (font);
                    node.appliedFont = font;
                    label->setText (v, juce::dontSendNotification);
                });
                break;
            }
            case Node::Button:
            {
                auto* button = static_cast<juce::Button*> (component);
                update (node.content, button->getButtonText(), fieldChanged, [button] (const juce::String& v) { button->setButtonText (v); });
                break;
            }
            case Node::Combo:
            {
                auto* combo = static_cast<juce::ComboBox*> (component);
                // JUCE changeItemText updates the menu only. getSelectedId()
                // becomes zero while the label still contains the old text.
                const auto selectedId = combo->getSelectedId();
                bool itemChanged = false;
                for (int i = 0; i < combo->getNumItems(); ++i)
                {
                    const auto id = combo->getItemId (i);
                    const auto current = combo->getItemText (i);
                    auto found = node.choices.find (id);
                    const auto inserted = found == node.choices.end();
                    if (found == node.choices.end())
                        found = node.choices.emplace (id, Field (current)).first;
                    update (found->second, current, fieldChanged || inserted, [combo, id, &itemChanged] (const juce::String& v)
                    {
                        combo->changeItemText (id, v);
                        itemChanged = true;
                    });
                }
                if (itemChanged && selectedId != 0)
                    combo->setSelectedId (selectedId, juce::dontSendNotification);
                break;
            }
            case Node::Other: break;
        }
    }
}

void ComponentTranslator::componentChildrenChanged (juce::Component&) { impl->scanNeeded = true; }
void ComponentTranslator::componentBeingDeleted (juce::Component& component)
{
    component.removeComponentListener (this);
    for (auto& node : impl->nodes)
        if (node.component.getComponent() == &component)
            node.component = nullptr;
    impl->scanNeeded = true;
}
void ComponentTranslator::changeListenerCallback (juce::ChangeBroadcaster*) { refresh(); }

void translateMenu (juce::PopupMenu& menu, const std::function<bool(int)>& preserveItem)
{
    juce::PopupMenu::MenuItemIterator items (menu, true);
    while (items.next())
    {
        auto& item = items.getItem();
        if (! preserveItem || ! preserveItem (item.itemID))
            item.text = text (item.text);
    }
}
}
