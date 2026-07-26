#pragma once

#include "ui_settings.h"
#include "variables_picker_ui.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class SampApi;
class BinderModule;
class NotificationManager;
class ArizonaCefDialogs;
class SampRakHooks;

class TagsModule {
public:
    enum class TagKind {
        Simple,
        Function,
    };

    enum class CatalogCategory {
        Player,
        Target,
        Vehicle,
        World,
        Time,
        SampDialog,
        Arizona,
        Binder,
        Text,
        Actions,
        Custom,
    };

    struct CatalogEntry {
        TagKind kind = TagKind::Simple;
        CatalogCategory category = CatalogCategory::Text;
        bool action = false;
        std::string name{};
        std::string token{};
        std::string example{};
        UiText descriptionText = UiText::Count;
        std::string description{};
    };

    struct VirtualKeyPickerEntry {
        unsigned int code = 0;
        std::string label{};
        std::string search{};
    };

    struct CursorRange {
        int start = -1;
        int finish = -1;
        bool valid = false;
    };

    struct CursorIntents {
        CursorRange sampChat{};
        CursorRange arizonaChat{};
        CursorRange sampDialog{};
        CursorRange arizonaDialog{};
    };

    struct ExpandedText {
        std::string text{};
        CursorIntents cursors{};
    };

    enum class DialogInputBackend {
        None,
        Samp,
        ArizonaCef,
    };

    enum class ExternalTextPath {
        SendCommand,
        SendChat,
        LocalChat,
    };

    struct DialogInputSetResult {
        bool ok = false;
        DialogInputBackend backend = DialogInputBackend::None;
        std::string error{};
    };

    struct EvaluationContext {
        SampApi* sampApi = nullptr;
        std::string_view activationSource;
        std::string_view activationText;
        std::string_view bindCommand;
        bool allowSideEffects = true;
        std::uint64_t runningBindRuntimeId = 0;
        CursorIntents* cursorIntents = nullptr;
    };

    struct OwnedEvaluationContext {
        SampApi* sampApi = nullptr;
        std::string activationSource;
        std::string activationText;
        std::string bindCommand;
        bool allowSideEffects = true;
        std::uint64_t runningBindRuntimeId = 0;
    };

    TagsModule();
    ~TagsModule();

    TagsModule(const TagsModule&) = delete;
    TagsModule& operator=(const TagsModule&) = delete;
    TagsModule(TagsModule&&) noexcept;
    TagsModule& operator=(TagsModule&&) noexcept;

    void OnProcessAttach();
    void Shutdown();
    void ReloadConfig();

    void SetSampApi(SampApi* sampApi);
    void SetSampRakHooks(SampRakHooks* sampRakHooks);
    void SetBinderModule(BinderModule* binderModule);
    void SetNotificationManager(NotificationManager* notificationManager);
    void SetArizonaCefDialogs(ArizonaCefDialogs* arizonaCefDialogs);

    void PushContext(const EvaluationContext& context) const;
    void PopContext() const;

    std::optional<int> ConsumePendingBindDelayOverride(std::uint64_t runtimeId) const;
    bool ConsumeCurrentDispatchBlocked(std::uint64_t runtimeId) const;
    void Tick(bool sampReady);
    void SetManualTargetId(int playerId, const void* currentAimPed);
    bool ExpandExternalTagsEnabled() const;
    void SetExpandExternalTagsEnabled(bool enabled);
    bool ProcessExternalText(std::string& text, ExternalTextPath path) const;
    bool IsMiscHomePage() const;
    void DrawMiscTab();
    std::string ExpandText(std::string_view text) const;
    std::string ExpandText(std::string_view text, const EvaluationContext& context) const;
    ExpandedText ExpandTextWithCursorIntents(std::string_view text) const;
    ExpandedText ExpandTextWithCursorIntents(std::string_view text, const EvaluationContext& context) const;
    DialogInputSetResult SetActiveDialogInputTextAuto(
        std::string_view text,
        const CursorIntents* cursorIntents,
        bool alreadyDecoded) const;
    std::string ExpandHudText(std::string_view text) const;
    std::string ExpandOutgoingText(
        std::string_view text,
        std::string_view activationSource,
        std::string_view activationText) const;
    const std::vector<CatalogEntry>& CatalogEntries() const;
    static variables_picker::Category ToPickerCategory(CatalogCategory category);
    const std::vector<std::pair<std::string, std::string>>& CustomVariables() const;
    const std::vector<VirtualKeyPickerEntry>& VirtualKeyPickerEntries() const;
    static std::string MakeKeyEmulateToken(unsigned int keyCode);
    void OpenKeyEmulatePicker();
    void OpenDialogItemPicker();
    void OpenArizonaDialogItemPicker();
    void OpenSampDialogTextPicker();
    void OpenArizonaDialogTextPicker();
    void OpenBindSelectorBuilder(std::string_view action);
    void DrawVariableHelperPopups(std::function<void(std::string_view)> tokenAction = {});
    std::vector<variables_picker::Entry> BuildVariablePickerEntriesForInsert() const;
    void HandleVariablePickerUtilityRequest(const variables_picker::Request& request);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
