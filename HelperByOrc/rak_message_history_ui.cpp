#include "rak_message_history_ui.h"

#include "external/imgui/imgui.h"
#include "samp_rak_hooks.h"
#include "ui_settings.h"

#include <algorithm>
#include <limits>

namespace rak_message_history_ui {
namespace {

const char* SourceLabel(UiSettings& ui, IncomingMessageHistorySource source) {
    switch (source) {
    case IncomingMessageHistorySource::ServerMessage:
        return ui.Text(UiText::MessageHistorySourceServer);
    case IncomingMessageHistorySource::PlayerChat:
        return ui.Text(UiText::MessageHistorySourceChat);
    case IncomingMessageHistorySource::ChatBubble:
        return ui.Text(UiText::MessageHistorySourceBubble);
    }
    return ui.Text(UiText::MessageHistorySourceServer);
}

bool DrawEntry(UiSettings& ui, const IncomingMessageHistoryEntry& entry) {
    ImGui::PushID(static_cast<int>(entry.sequence & 0x7FFFFFFFu));
    ImGui::TextDisabled("%s", SourceLabel(ui, entry.source));
    if (entry.playerId >= 0) {
        ImGui::SameLine();
        if (!entry.playerName.empty() && entry.playerName != "UNKNOWN") {
            ImGui::TextDisabled("%s[%d]", entry.playerName.c_str(), entry.playerId);
        } else {
            ImGui::TextDisabled("ID %d", entry.playerId);
        }
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float wrapWidth = std::max(1.0f, availableWidth - style.FramePadding.x * 2.0f);
    const char* textBegin = entry.text.data();
    const char* textEnd = textBegin + entry.text.size();
    const ImVec2 textSize = ImGui::CalcTextSize(textBegin, textEnd, false, wrapWidth);
    const float rowHeight = std::max(ImGui::GetFrameHeight(), textSize.y + style.FramePadding.y * 2.0f);
    const bool selected = ImGui::Selectable(
        "##message",
        false,
        ImGuiSelectableFlags_None,
        ImVec2(availableWidth, rowHeight));

    const ImVec2 textPosition(
        ImGui::GetItemRectMin().x + style.FramePadding.x,
        ImGui::GetItemRectMin().y + style.FramePadding.y);
    ImGui::GetWindowDrawList()->AddText(
        ImGui::GetFont(),
        ImGui::GetFontSize(),
        textPosition,
        ImGui::GetColorU32(ImGuiCol_Text),
        textBegin,
        textEnd,
        wrapWidth);
    ImGui::PopID();
    return selected;
}

} // namespace

void RequestOpen(PickerState& state) {
    state.openRequested = true;
    state.scrollToBottom = true;
    state.snapshotRevision = std::numeric_limits<std::uint64_t>::max();
    state.entries.clear();
}

std::optional<std::string> DrawPicker(
    SampRakHooks* hooks,
    PickerState& state,
    const char* popupId,
    bool includeChatBubbles) {
    UiSettings& ui = UiSettings::Instance();
    const int language = static_cast<int>(ui.Language());
    if (state.titleLanguage != language || state.title.empty()) {
        state.titleLanguage = language;
        state.title = std::string(ui.Text(UiText::MessageHistoryTitle)) + "###" + popupId;
    }
    if (state.openRequested) {
        state.openRequested = false;
        ImGui::OpenPopup(state.title.c_str());
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 maximum(viewport->WorkSize.x * 0.92f, viewport->WorkSize.y * 0.92f);
    const ImVec2 preferredMinimum = ui.Scale(ImVec2(460.0f, 320.0f));
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(std::min(preferredMinimum.x, maximum.x), std::min(preferredMinimum.y, maximum.y)),
        maximum);
    ImGui::SetNextWindowSize(
        ImVec2(std::min(ui.Scale(680.0f), maximum.x), std::min(ui.Scale(500.0f), maximum.y)),
        ImGuiCond_Appearing);

    bool open = true;
    if (!ImGui::BeginPopupModal(state.title.c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
        return std::nullopt;
    }

    bool snapshotChanged = false;
    if (hooks) {
        snapshotChanged = hooks->CopyIncomingMessageHistoryIfChanged(
            state.snapshotRevision,
            state.snapshotRevision,
            state.entries);
    }

    ImGui::TextWrapped("%s", ui.Text(UiText::MessageHistoryHint));
    ImGui::Spacing();

    std::optional<std::string> selectedMessage;
    if (ImGui::BeginChild(
            "##message_history_list",
            ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()),
            ImGuiChildFlags_Borders)) {
        const float previousScrollY = ImGui::GetScrollY();
        const bool wasAtBottom = state.scrollToBottom
            || ImGui::GetScrollMaxY() - previousScrollY <= ui.Scale(1.0f);

        if (!hooks) {
            ImGui::TextDisabled("%s", ui.Text(UiText::MessageHistoryUnavailable));
        } else if (state.entries.empty()
            || (!includeChatBubbles
                && std::none_of(state.entries.begin(), state.entries.end(), [](const IncomingMessageHistoryEntry& entry) {
                    return entry.source != IncomingMessageHistorySource::ChatBubble;
                }))) {
            ImGui::TextDisabled("%s", ui.Text(UiText::MessageHistoryEmpty));
        } else {
            for (const IncomingMessageHistoryEntry& entry : state.entries) {
                if (!includeChatBubbles && entry.source == IncomingMessageHistorySource::ChatBubble) {
                    continue;
                }
                if (DrawEntry(ui, entry)) {
                    selectedMessage = entry.text;
                }
                ImGui::Separator();
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        if (snapshotChanged || state.scrollToBottom) {
            if (wasAtBottom) {
                ImGui::SetScrollHereY(1.0f);
            } else {
                ImGui::SetScrollY(previousScrollY);
            }
        }
        state.scrollToBottom = false;
    }
    ImGui::EndChild();

    if (selectedMessage) {
        ImGui::CloseCurrentPopup();
    } else if (ImGui::Button(ui.Text(UiText::Close), ui.Scale(ImVec2(120.0f, 0.0f)))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return selectedMessage;
}

} // namespace rak_message_history_ui
