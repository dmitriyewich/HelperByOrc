#pragma once

#include "ui_settings.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class SampApi;
class BinderModule;

class TagsModule {
public:
    enum class TagKind {
        Simple,
        Function,
    };

    struct CatalogEntry {
        TagKind kind = TagKind::Simple;
        std::string name{};
        std::string token{};
        std::string example{};
        UiText descriptionText = UiText::Count;
    };

    struct VirtualKeyPickerEntry {
        unsigned int code = 0;
        std::string label{};
        std::string search{};
    };

    struct EvaluationContext {
        SampApi* sampApi = nullptr;
        std::string_view activationSource;
        std::string_view activationText;
        std::string_view bindCommand;
        bool allowSideEffects = true;
        std::uint64_t runningBindRuntimeId = 0;
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

    void OnProcessAttach();
    void Shutdown();
    void ReloadConfig();

    void SetSampApi(SampApi* sampApi);
    void SetBinderModule(BinderModule* binderModule);

    void PushContext(const EvaluationContext& context) const;
    void PopContext() const;

    std::optional<int> ConsumePendingBindDelayOverride(std::uint64_t runtimeId) const;
    void Tick();
    bool IsMiscHomePage() const;
    void DrawMiscTab();
    std::string ExpandText(std::string_view text) const;
    std::string ExpandText(std::string_view text, const EvaluationContext& context) const;
    std::string ExpandOutgoingText(
        std::string_view text,
        std::string_view activationSource,
        std::string_view activationText) const;
    const std::vector<CatalogEntry>& CatalogEntries() const;
    const std::vector<VirtualKeyPickerEntry>& VirtualKeyPickerEntries() const;
    static std::string MakeKeyEmulateToken(unsigned int keyCode);

private:
    struct TagEntry {
        using SimpleResolver = std::function<std::optional<std::string>(const TagsModule&, const EvaluationContext&)>;
        using FunctionResolver =
            std::function<std::optional<std::string>(const TagsModule&, std::string_view, const EvaluationContext&, int)>;

        TagKind kind = TagKind::Simple;
        std::string name{};
        std::string token{};
        std::string example{};
        UiText descriptionText = UiText::Count;
        SimpleResolver simpleResolver{};
        FunctionResolver functionResolver{};
    };

    class TagRegistry {
    public:
        void Clear();
        void RegisterSimple(
            std::string name,
            std::string token,
            std::string example,
            UiText descriptionText,
            TagEntry::SimpleResolver resolver);
        void RegisterFunction(
            std::string name,
            std::string token,
            std::string example,
            UiText descriptionText,
            TagEntry::FunctionResolver resolver);

        const std::vector<TagEntry>& Entries() const;
        const TagEntry* Find(TagKind kind, std::string_view name) const;
        const TagEntry* FindByIndex(int index) const;
        std::size_t Count(TagKind kind) const;

    private:
        std::vector<TagEntry> entries_{};
    };

    enum class MiscPage {
        Home = 0,
        Variables,
    };

    struct ActiveVirtualKeyHold {
        unsigned int keyCode = 0;
        std::uint64_t pressAtMs = 0;
        std::uint64_t releaseAtMs = 0;
        bool pressed = false;
    };

    struct PendingBindDelayOverride {
        std::uint64_t runtimeId = 0;
        int delayMs = 0;
    };

    struct PendingKeyHoldWait {
        std::uint64_t runtimeId = 0;
        unsigned int keyCode = 0;
        std::uint64_t releaseAtMs = 0;
    };

    struct TargetTrackerState {
        int currentId = -1;
        int lastId = -1;
        bool sessionActive = false;
    };

    enum class PendingDialogWaitKind {
        Open,
        Close,
        SpecificId,
    };

    struct PendingDialogWait {
        std::uint64_t runtimeId = 0;
        PendingDialogWaitKind kind = PendingDialogWaitKind::Open;
        std::uint64_t deadlineAtMs = 0;
        int expectedDialogId = -1;
    };

    struct ClosestPlayerQueryResult {
        int nearestId = -1;
        int nearestToCenterId = -1;
    };

    void InitializeRegistry();
    void LoadConfig();
    void SaveConfig() const;
    void DrawMiscHomePage();
    void DrawVariablesPage();
    void RefreshCatalogEntries();
    void OpenDialogItemPicker();
    void OpenDialogTextPicker();
    void DrawDialogItemPickerPopup();
    void DrawDialogTextPickerPopup();
    void ProcessPendingKeyHoldWaits();
    void ProcessPendingDialogWaits();
    void QueuePendingDialogWait(
        std::uint64_t runtimeId,
        PendingDialogWaitKind kind,
        std::uint64_t deadlineAtMs,
        int expectedDialogId = -1);
    void ClearPendingDialogWait(std::uint64_t runtimeId);
    void QueuePendingKeyHoldWait(std::uint64_t runtimeId, unsigned int keyCode, std::uint64_t releaseAtMs) const;
    void ClearPendingKeyHoldWaitsByKeyCode(unsigned int keyCode) const;
    bool HasPendingDialogWait(std::uint64_t runtimeId) const;
    bool HasPendingKeyHoldWait(std::uint64_t runtimeId) const;

    std::string ExpandTextRecursive(std::string_view text, const EvaluationContext& context, int depth) const;
    std::string ExpandFunctionTags(std::string_view text, const EvaluationContext& context, int depth) const;
    std::string ExpandSimpleTags(std::string_view text, const EvaluationContext& context) const;

    std::optional<std::string> ResolveSimpleTag(std::string_view name, const EvaluationContext& context) const;
    std::optional<std::string> ResolveFunctionTag(
        std::string_view name,
        std::string_view param,
        const EvaluationContext& context,
        int depth) const;
    std::optional<std::string> ResolveBuiltinIdTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinNickTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinThisbindTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinThiscategoryTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinBindStopAllTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTargetIdTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTargetNickTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTargetRpNickTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTargetNameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTargetSurnameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTargetHealthTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTargetArmourTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinClosestIdTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinClosestIdToCenterTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinClosestNameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinClosestSurnameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArmourTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinHealthTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDateTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMySkinTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyMoneyTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinFpsTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinGetVehTypeTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinScreenTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTPhotoTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinNickRpTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinNameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinSurnameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTimeTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTimeNoSecTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogActiveTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogCaptionTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogGetSelectedItemTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogEditboxTextTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogSelectedIndexTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogWaitOpenTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogWaitCloseTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogGetIdTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinNickFunctionTag(std::string_view param, const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinRpNickFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinNameFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinSurnameFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinParamcmdFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinKeyEmulateFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMathFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinNumberWithDotsFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArmourFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinHealthFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinSkinFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinKeyDownFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinStrLowFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinAddTimeFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinRandomFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinIfAndOrFunctionTag(
        std::string_view rawParam,
        const EvaluationContext& context,
        int depth) const;
    std::optional<std::string> ResolveBuiltinTimefFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinGetVehTypeFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinScreenFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinWaitFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogCloseFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogSetTextFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogItemFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogSelectFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogWaitIdFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogResponseFunctionTag(
        std::string_view rawParam,
        const EvaluationContext& context,
        int depth) const;
    std::optional<std::string> ResolveBuiltinDialogTextFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinSaveDialogFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBinderActionFunctionTag(
        std::string_view action,
        std::string_view param,
        const EvaluationContext& context) const;

    EvaluationContext ResolveActiveContext(std::string_view defaultSource = {}, std::string_view defaultText = {}) const;
    void OpenKeyEmulatePicker();
    void DrawKeyEmulatePickerPopup();
    void UpdateTargetTracker();
    void ResetTargetTracker();
    ClosestPlayerQueryResult QueryClosestPlayers(const EvaluationContext& context) const;
    std::string ResolvePlayerNickById(int id, const EvaluationContext& context) const;
    std::string ResolveLocalNick(const EvaluationContext& context) const;
    std::string ResolveLastTargetNick(const EvaluationContext& context) const;
    static std::string FormatCurrentTime(std::string_view format);
    static std::string FormatWholeStatValue(float value);
    static std::string MakeRpNick(std::string_view nick);
    static std::string ExtractName(std::string_view nick);
    static std::string ExtractSurname(std::string_view nick);

    static std::string Trim(std::string_view value);
    static std::string ToLower(std::string_view value);
    static bool StartsWith(std::string_view value, std::string_view prefix);
    static std::vector<std::string> SplitCommandArgs(std::string_view value);
    static std::optional<int> ParseInteger(std::string_view value);
    static OwnedEvaluationContext MakeOwnedContext(const EvaluationContext& context, SampApi* fallbackSampApi);
    static EvaluationContext MakeViewContext(const OwnedEvaluationContext& context);
    std::uint64_t QueueVirtualKeyHold(unsigned int keyCode, int startDelayMs, int holdDurationMs) const;
    void ReleaseVirtualKeyHold(ActiveVirtualKeyHold& hold) const;
    void QueuePendingBindDelayOverride(std::uint64_t runtimeId, int delayMs) const;

    SampApi* sampApi_ = nullptr;
    BinderModule* binderModule_ = nullptr;
    std::string searchQuery_{};
    int selectedTagIndex_ = 0;
    MiscPage currentPage_ = MiscPage::Home;
    TagRegistry tagRegistry_{};
    std::vector<CatalogEntry> catalogEntries_{};
    std::vector<std::pair<std::string, std::string>> customVariables_{};
    mutable std::vector<ActiveVirtualKeyHold> activeVirtualKeyHolds_{};
    mutable std::vector<PendingBindDelayOverride> pendingBindDelayOverrides_{};
    mutable std::vector<PendingKeyHoldWait> pendingKeyHoldWaits_{};
    std::vector<PendingDialogWait> pendingDialogWaits_{};
    TargetTrackerState targetTracker_{};
    std::string keyPickerSearchQuery_{};
    std::string dialogItemPickerSearchQuery_{};
    std::string dialogTextPickerSearchQuery_{};
    bool keyPickerHoverTriggered_ = false;
    bool dialogItemPickerOpenPending_ = false;
    bool dialogTextPickerOpenPending_ = false;
};
