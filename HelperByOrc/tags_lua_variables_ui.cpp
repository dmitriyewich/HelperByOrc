#include "tags_module_impl.h"
#include "tags_module_detail.h"
#include "lua_bridge.h"
#include "ui_icons.h"

#include <shellapi.h>

namespace {

struct LuaVariablesVisualStyle {
    ImVec4 panelBg{};
    ImVec4 panelBorder{};
    ImVec4 headerText{};
    ImVec4 mutedText{};
    ImVec4 accent{};
    ImVec4 ok{};
    ImVec4 warning{};
    ImVec4 danger{};
};

ImVec4 WithAlpha(ImVec4 color, float alpha) {
    color.w = alpha;
    return color;
}

LuaVariablesVisualStyle LuaVariablesStyleTokens() {
    const ImVec4* colors = ImGui::GetStyle().Colors;
    const ImVec4& text = colors[ImGuiCol_Text];
    const ImVec4& textDisabled = colors[ImGuiCol_TextDisabled];
    const ImVec4& buttonActive = colors[ImGuiCol_ButtonActive];

    LuaVariablesVisualStyle style;
    style.panelBg = WithAlpha(
        LerpColor(colors[ImGuiCol_ChildBg], colors[ImGuiCol_WindowBg], 0.18f),
        colors[ImGuiCol_ChildBg].w);
    style.panelBorder = WithAlpha(colors[ImGuiCol_Border], 0.42f);
    style.headerText = text;
    style.mutedText = WithAlpha(LerpColor(textDisabled, text, 0.28f), 0.92f);
    style.accent = WithAlpha(buttonActive, 0.96f);
    style.ok = ImVec4(0.48f, 0.82f, 0.62f, 0.96f);
    style.warning = ImVec4(0.97f, 0.75f, 0.32f, 0.96f);
    style.danger = ImVec4(0.95f, 0.42f, 0.40f, 0.96f);
    return style;
}

bool BeginLuaVariablesPanel(const char* id, const LuaVariablesVisualStyle& style) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, style.panelBg);
    ImGui::PushStyleColor(ImGuiCol_Border, style.panelBorder);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ScaleUi(7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ScaleUi(12.0f, 10.0f));
    return ImGui::BeginChild(
        id,
        ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

void EndLuaVariablesPanel(const ImVec4& accent) {
    const ImVec2 panelMin = ImGui::GetWindowPos();
    const ImVec2 panelMax(panelMin.x + ImGui::GetWindowSize().x, panelMin.y + ImGui::GetWindowSize().y);
    ImGui::GetWindowDrawList()->AddRectFilled(
        panelMin,
        ImVec2(panelMin.x + ScaleUi(3.0f), panelMax.y),
        ImGui::GetColorU32(WithAlpha(accent, 0.82f)),
        ScaleUi(7.0f),
        ImDrawFlags_RoundCornersLeft);
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void DrawLuaVariablesBadge(const char* label, const ImVec4& color, const LuaVariablesVisualStyle& style) {
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(textSize.x + ScaleUi(10.0f), std::max(ImGui::GetFrameHeight() * 0.72f, textSize.y + ScaleUi(4.0f)));
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(pos, max, ImGui::GetColorU32(WithAlpha(color, 0.16f)), ScaleUi(4.0f));
    drawList->AddRect(pos, max, ImGui::GetColorU32(WithAlpha(color, 0.38f)), ScaleUi(4.0f), 0, ScaleUi(1.0f));
    drawList->AddText(
        ImVec2(pos.x + ScaleUi(5.0f), pos.y + std::floor((size.y - textSize.y) * 0.5f)),
        ImGui::GetColorU32(LerpColor(color, style.headerText, 0.18f)),
        label);
    ImGui::Dummy(size);
}

void DrawLuaVariablesWrappedText(const ImVec4& color, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
}

UiText LuaProviderStateText(codevars::ProviderState state) {
    switch (state) {
    case codevars::ProviderState::Disabled:
        return UiText::VariablesCodeStateDisabled;
    case codevars::ProviderState::WaitingForMoonLoader:
        return UiText::VariablesCodeStateWaiting;
    case codevars::ProviderState::Loading:
        return UiText::VariablesCodeStateLoading;
    case codevars::ProviderState::Ready:
        return UiText::VariablesCodeStateReady;
    case codevars::ProviderState::Conflict:
        return UiText::VariablesCodeStateConflict;
    case codevars::ProviderState::Faulted:
    default:
        return UiText::VariablesCodeStateFaulted;
    }
}

ImVec4 LuaProviderStateColor(codevars::ProviderState state, const LuaVariablesVisualStyle& style) {
    switch (state) {
    case codevars::ProviderState::Ready:
        return style.ok;
    case codevars::ProviderState::Loading:
        return style.accent;
    case codevars::ProviderState::WaitingForMoonLoader:
    case codevars::ProviderState::Conflict:
        return style.warning;
    case codevars::ProviderState::Faulted:
        return style.danger;
    case codevars::ProviderState::Disabled:
    default:
        return style.mutedText;
    }
}

ImVec4 LuaBackendColor(lua_bridge::Backend backend, const LuaVariablesVisualStyle& style) {
    switch (backend) {
    case lua_bridge::Backend::MoonLoader:
        return style.ok;
    case lua_bridge::Backend::Standalone:
        return style.accent;
    case lua_bridge::Backend::Waiting:
        return style.warning;
    case lua_bridge::Backend::Faulted:
    default:
        return style.danger;
    }
}

UiText LuaHostStateText(lua_bridge::HostState state) {
    switch (state) {
    case lua_bridge::HostState::Missing:
        return UiText::VariablesCodeHostStateMissing;
    case lua_bridge::HostState::Current:
        return UiText::VariablesCodeHostStateCurrent;
    case lua_bridge::HostState::Outdated:
        return UiText::VariablesCodeHostStateOutdated;
    case lua_bridge::HostState::Unavailable:
    default:
        return UiText::VariablesCodeHostStateUnavailable;
    }
}

ImVec4 LuaHostStateColor(lua_bridge::HostState state, const LuaVariablesVisualStyle& style) {
    switch (state) {
    case lua_bridge::HostState::Current:
        return style.ok;
    case lua_bridge::HostState::Missing:
    case lua_bridge::HostState::Outdated:
        return style.warning;
    case lua_bridge::HostState::Unavailable:
    default:
        return style.mutedText;
    }
}

bool ShouldShowLuaProviderDetail(codevars::ProviderState state, std::string_view detail) {
    return !detail.empty()
        && state != codevars::ProviderState::Disabled
        && state != codevars::ProviderState::Ready;
}

void DrawLuaProviderTooltip(const codevars::ProviderStatus& provider) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        return;
    }

    ImGui::BeginTooltip();
    ImGui::TextUnformatted(provider.displayName.c_str());
    ImGui::TextDisabled("%s", provider.id.c_str());
    if (!provider.detail.empty()) {
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
        ImGui::TextUnformatted(provider.detail.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndTooltip();
}

} // namespace

void TagsModule::Impl::DrawLuaVariablesTab() {
    UiSettings& ui = UiSettings::Instance();
    const LuaVariablesVisualStyle visual = LuaVariablesStyleTokens();

    ImGui::TextColored(visual.headerText, "%s", ui.Text(UiText::VariablesCodeTitle));
    ImGui::TextWrapped("%s", ui.Text(UiText::VariablesCodeDescription));
    ImGui::Spacing();

    const lua_bridge::Status bridgeStatus = lua_bridge::CurrentStatus();
    const std::string variablesRoot = WideToUtf8(bridgeStatus.variablesRoot.wstring());

    const auto drawRuntimeInfo = [&]() {
        ImGui::TextColored(visual.headerText, "%s", ui.Text(UiText::VariablesCodeRuntimeTitle));
        ImGui::Spacing();

        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", ui.Text(UiText::VariablesCodeBackendLabel));
        ImGui::SameLine();
        DrawLuaVariablesBadge(
            lua_bridge::BackendName(bridgeStatus.backend),
            LuaBackendColor(bridgeStatus.backend, visual),
            visual);
        if (!bridgeStatus.detail.empty()) {
            DrawLuaVariablesWrappedText(visual.mutedText, bridgeStatus.detail.c_str());
        }

        ImGui::Spacing();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", ui.Text(UiText::VariablesCodeHostLabel));
        ImGui::SameLine();
        DrawLuaVariablesBadge(
            ui.Text(LuaHostStateText(bridgeStatus.host)),
            LuaHostStateColor(bridgeStatus.host, visual),
            visual);
        if (!bridgeStatus.moonLoaderAvailable) {
            DrawLuaVariablesWrappedText(visual.mutedText, ui.Text(UiText::VariablesCodeHostUnavailable));
        } else if (bridgeStatus.host == lua_bridge::HostState::Outdated) {
            DrawLuaVariablesWrappedText(visual.mutedText, ui.Text(UiText::VariablesCodeHostOutdated));
        }

        if (bridgeStatus.backend == lua_bridge::Backend::Standalone && bridgeStatus.moonLoaderAvailable) {
            DrawLuaVariablesWrappedText(visual.warning, ui.Text(UiText::VariablesCodeHostRestartRequired));
        }

        ImGui::Spacing();
        ImGui::TextWrapped(
            "%s",
            ui.Format(UiText::VariablesCodePathFormat, variablesRoot.c_str()).c_str());

        ImGui::Spacing();
        const std::string warning = std::string(ui_icons::Bolt) + "  " + ui.Text(UiText::VariablesCodeTrustedWarning);
        DrawLuaVariablesWrappedText(visual.warning, warning.c_str());
    };

    const auto drawActions = [&]() {
        ImGui::TextColored(visual.headerText, "%s", ui.Text(UiText::VariablesCodeActionsTitle));
        ImGui::Spacing();

        const float buttonWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const std::string reloadLabel = std::string(ui_icons::RotateLeft) + "  " + ui.Text(UiText::VariablesCodeReload) + "##lua_reload";
        ImGui::PushStyleColor(ImGuiCol_Button, WithAlpha(visual.accent, 0.72f));
        if (ImGui::Button(reloadLabel.c_str(), ImVec2(buttonWidth, 0.0f))) {
            codevars::Runtime::Instance().RequestReload();
        }
        ImGui::PopStyleColor();

        const std::string openFolderLabel = std::string(ui_icons::Folder) + "  " + ui.Text(UiText::VariablesCodeOpenFolder) + "##lua_open_folder";
        if (ImGui::Button(openFolderLabel.c_str(), ImVec2(buttonWidth, 0.0f))) {
            std::error_code directoryError;
            std::filesystem::create_directories(bridgeStatus.variablesRoot, directoryError);
            if (!directoryError) {
                ShellExecuteW(nullptr, L"open", bridgeStatus.variablesRoot.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        }

        const bool hostNeedsAction = bridgeStatus.moonLoaderAvailable
            && bridgeStatus.host != lua_bridge::HostState::Unavailable
            && bridgeStatus.host != lua_bridge::HostState::Current;
        if (hostNeedsAction) {
            const UiText actionText = bridgeStatus.host == lua_bridge::HostState::Missing
                ? UiText::VariablesCodeInstallHost
                : UiText::VariablesCodeUpdateHost;
            const std::string hostActionLabel = std::string(ui_icons::Bolt) + "  " + ui.Text(actionText) + "##lua_host_action";
            if (ImGui::Button(hostActionLabel.c_str(), ImVec2(buttonWidth, 0.0f))) {
                std::string error;
                luaHostInstallFailed_ = !lua_bridge::InstallOrUpdateMoonLoaderHost(&error);
                luaHostInstallMessage_ = luaHostInstallFailed_
                    ? ui.Format(UiText::VariablesCodeHostInstallFailed, error.c_str())
                    : ui.Text(UiText::VariablesCodeHostInstalled);
            }
        }

        if (!luaHostInstallMessage_.empty()) {
            ImGui::Spacing();
            DrawLuaVariablesWrappedText(
                luaHostInstallFailed_ ? visual.danger : visual.ok,
                luaHostInstallMessage_.c_str());
        }
    };

    if (BeginLuaVariablesPanel("##lua_runtime_panel", visual)) {
        if (ImGui::GetContentRegionAvail().x >= ScaleUi(760.0f)
            && ImGui::BeginTable("##lua_runtime_layout", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("##runtime", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, ScaleUi(300.0f));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            drawRuntimeInfo();
            ImGui::TableSetColumnIndex(1);
            drawActions();
            ImGui::EndTable();
        } else {
            drawRuntimeInfo();
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            drawActions();
        }
    }
    EndLuaVariablesPanel(visual.accent);

    const std::vector<codevars::ProviderStatus> providers = codevars::Runtime::Instance().Providers();
    std::size_t enabledProviders = 0;
    std::size_t registeredVariables = 0;
    for (const codevars::ProviderStatus& provider : providers) {
        enabledProviders += provider.enabled ? 1u : 0u;
        registeredVariables += provider.registeredVariables;
    }

    ImGui::Spacing();
    ImGui::TextColored(visual.headerText, "%s", ui.Text(UiText::VariablesCodeProvidersTitle));
    const std::string providerCount = std::to_string(providers.size());
    const std::string enabledCount = std::to_string(enabledProviders);
    const std::string variableCount = std::to_string(registeredVariables);
    const std::string providersSummary = ui.Format(
        UiText::VariablesCodeProvidersSummary,
        providerCount.c_str(),
        enabledCount.c_str(),
        variableCount.c_str());
    DrawLuaVariablesWrappedText(
        visual.mutedText,
        providersSummary.c_str());
    ImGui::Spacing();

    if (providers.empty()) {
        if (BeginLuaVariablesPanel("##lua_providers_empty", visual)) {
            DrawLuaVariablesWrappedText(visual.mutedText, ui.Text(UiText::VariablesCodeEmpty));
        }
        EndLuaVariablesPanel(visual.mutedText);
        return;
    }

    for (const codevars::ProviderStatus& provider : providers) {
        ImGui::PushID(provider.id.c_str());
        const ImVec4 stateColor = LuaProviderStateColor(provider.state, visual);
        const std::string count = std::to_string(provider.registeredVariables);
        const std::string variableLabel = ui.Format(UiText::VariablesCodeProviderVariablesFormat, count.c_str());
        bool enabled = provider.enabled;
        const std::string enabledLabel = std::string(ui.Text(
            enabled ? UiText::VariablesCodeProviderEnabled : UiText::VariablesCodeProviderEnable)) + "##provider_enabled";

        if (BeginLuaVariablesPanel("##provider", visual)) {
            if (ImGui::GetContentRegionAvail().x >= ScaleUi(680.0f)
                && ImGui::BeginTable("##provider_layout", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
                ImGui::TableSetupColumn("##toggle", ImGuiTableColumnFlags_WidthFixed, ScaleUi(112.0f));
                ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("##state", ImGuiTableColumnFlags_WidthFixed, ScaleUi(250.0f));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Checkbox(enabledLabel.c_str(), &enabled)) {
                    codevars::Runtime::Instance().SetProviderEnabled(provider.id, enabled);
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(provider.displayName.c_str());
                DrawLuaProviderTooltip(provider);
                ImGui::TableSetColumnIndex(2);
                DrawLuaVariablesBadge(ui.Text(LuaProviderStateText(provider.state)), stateColor, visual);
                ImGui::SameLine(0.0f, ScaleUi(6.0f));
                ImGui::TextColored(visual.mutedText, "%s", variableLabel.c_str());
                ImGui::EndTable();
            } else {
                if (ImGui::Checkbox(enabledLabel.c_str(), &enabled)) {
                    codevars::Runtime::Instance().SetProviderEnabled(provider.id, enabled);
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(provider.displayName.c_str());
                DrawLuaProviderTooltip(provider);
                DrawLuaVariablesBadge(ui.Text(LuaProviderStateText(provider.state)), stateColor, visual);
                ImGui::SameLine(0.0f, ScaleUi(6.0f));
                ImGui::TextColored(visual.mutedText, "%s", variableLabel.c_str());
            }

            if (ShouldShowLuaProviderDetail(provider.state, provider.detail)) {
                ImGui::Spacing();
                DrawLuaVariablesWrappedText(stateColor, provider.detail.c_str());
            }
        }
        EndLuaVariablesPanel(stateColor);
        ImGui::PopID();
        ImGui::Spacing();
    }
}
