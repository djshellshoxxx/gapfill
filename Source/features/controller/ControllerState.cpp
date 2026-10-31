#include "ControllerState.h"

namespace gapfill::controller
{
namespace
{
    bool integer (const juce::ValueTree& tree, const char* key, int low, int high, int& value, int fallback)
    {
        const auto text = tree.getProperty (key, fallback).toString().trim();
        const auto candidate = text.getLargeIntValue();
        if (text != juce::String (candidate) || candidate < low || candidate > high) return false;
        value = (int) candidate; return true;
    }
    bool printable (const juce::String& text, int maximum)
    {
        if (text.length() > maximum) return false;
        for (auto c : text) if (c < 32 || c == 127) return false;
        return true;
    }
}
bool decodeControllerState (const juce::ValueTree& tree, ControllerState& output) noexcept
{
    if (! tree.hasType ("Controllers")) return false;
    auto xml = tree.createXml();
    if (xml == nullptr || xml->toString().getNumBytesAsUTF8() > (std::size_t) kControllerStateBytes) return false;
    ControllerState candidate;
    int version = 0, enabled = 0, tx = 0;
    if (! integer (tree, "v", 1, 1, version, 0) || ! integer (tree, "feedback", 0, 1, enabled, 0)
        || ! integer (tree, "tx", 1, 16, tx, 1) || ! integer (tree, "timeout", 1000, 30000, candidate.profile.momentaryTimeoutMs, 8000)) return false;
    candidate.profile.extended = true; candidate.profile.parameterFeedback = enabled != 0;
    candidate.profile.txChannel = (std::uint8_t) tx;
    candidate.profile.name = tree.getProperty ("name", "Controller settings").toString();
    candidate.device = tree.getProperty ("device").toString();
    if (! printable (candidate.profile.name, 48) || ! printable (candidate.device, 96)) return false;
    for (const auto& row : tree)
    {
        if (row.hasType ("p"))
        {
            if (candidate.profile.params.size() >= kMaxParameterBindings) return false;
            const auto id = row.getProperty ("id").toString();
            int channel = 0, takeover = 0;
            if (id.isEmpty() || id.length() > 32 || ! id.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")
                || ! integer (row, "ch", 0, 16, channel, 0) || ! integer (row, "t", 0, 1, takeover, 0)) return false;
            for (const auto& other : candidate.profile.params) if (other.id == id.toStdString()) return false;
            candidate.profile.params.push_back ({ id.toStdString(), 0, (std::uint8_t) channel, (TakeoverMode) takeover });
        }
        else if (row.hasType ("i"))
        {
            if (candidate.profile.feedback.size() >= kMaxIndicators) return false;
            int source = 0, arg = 0, kind = 0, channel = 0, number = 0, on = 0, off = 0;
            if (! integer (row, "s", 0, 6, source, 0) || ! integer (row, "a", 0, 63, arg, 0)
                || ! integer (row, "k", 0, 1, kind, 1) || ! integer (row, "ch", 1, 16, channel, 1)
                || ! integer (row, "n", 0, 127, number, 0) || ! integer (row, "on", 0, 127, on, 127)
                || ! integer (row, "off", 0, 127, off, 0)) return false;
            candidate.profile.feedback.push_back ({ (commands::FeedbackSource) source, arg,
                kind == 0 ? commands::MsgType::CC : commands::MsgType::Note, (std::uint8_t) channel,
                (std::uint8_t) number, (std::uint8_t) on, (std::uint8_t) off });
        }
        else return false;
    }
    output = std::move (candidate);
    return true;
}
juce::ValueTree encodeControllerState (const ControllerState& state)
{
    if (! state.profile.extended) return {};
    juce::ValueTree tree ("Controllers");
    tree.setProperty ("v", 1, nullptr); tree.setProperty ("name", state.profile.name, nullptr);
    tree.setProperty ("feedback", state.profile.parameterFeedback ? 1 : 0, nullptr);
    tree.setProperty ("tx", (int) state.profile.txChannel, nullptr);
    tree.setProperty ("timeout", state.profile.momentaryTimeoutMs, nullptr);
    if (state.device.isNotEmpty()) tree.setProperty ("device", state.device, nullptr);
    for (const auto& binding : state.profile.params)
    {
        juce::ValueTree row ("p"); row.setProperty ("id", juce::String (binding.id), nullptr);
        row.setProperty ("ch", (int) binding.channel, nullptr); row.setProperty ("t", (int) binding.takeover, nullptr);
        tree.appendChild (row, nullptr);
    }
    for (const auto& indicator : state.profile.feedback)
    {
        juce::ValueTree row ("i");
        row.setProperty ("s", (int) indicator.source, nullptr); row.setProperty ("a", indicator.arg, nullptr);
        row.setProperty ("k", indicator.type == commands::MsgType::CC ? 0 : 1, nullptr);
        row.setProperty ("ch", (int) indicator.channel, nullptr); row.setProperty ("n", (int) indicator.number, nullptr);
        row.setProperty ("on", (int) indicator.onValue, nullptr); row.setProperty ("off", (int) indicator.offValue, nullptr);
        tree.appendChild (row, nullptr);
    }
    ControllerState verified;
    if (! decodeControllerState (tree, verified))
    {
        juce::ValueTree error ("Controllers");
        error.setProperty ("__serialiseError", "Controller settings exceed their bounds or contain an invalid mapping.", nullptr);
        return error;
    }
    return tree;
}
}
