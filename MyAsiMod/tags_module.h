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

    void SetSampApi(SampApi* sampApi);
    void SetBinderModule(BinderModule* binderModule);

    void PushContext(const EvaluationContext& context) const;
    void PopContext() const;

    std::optional<int> ConsumePendingBindDelayOverride(std::uint64_t runtimeId) const;
    void Tick();
    void DrawMiscTab();
    std::string ExpandText(std::string_view text) const;
    std::string ExpandText(std::string_view text, const EvaluationContext& context) const;
    std::string ExpandOutgoingText(
        std::string_view text,
        std::string_view activationSource,
        std::string_view activationText) const;

private:
    struct TagEntry {
        using SimpleResolver = std::function<std::optional<std::string>(const TagsModule&, const EvaluationContext&)>;
        using FunctionResolver =
            std::function<std::optional<std::string>(const TagsModule&, std::string_view, const EvaluationContext&)>;

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

    void InitializeRegistry();
    void LoadConfig();
    void SaveConfig() const;
    void DrawMiscHomePage();
    void DrawVariablesPage();

    std::string ExpandTextRecursive(std::string_view text, const EvaluationContext& context, int depth) const;
    std::string ExpandFunctionTags(std::string_view text, const EvaluationContext& context, int depth) const;
    std::string ExpandSimpleTags(std::string_view text, const EvaluationContext& context) const;

    std::optional<std::string> ResolveSimpleTag(std::string_view name, const EvaluationContext& context) const;
    std::optional<std::string> ResolveFunctionTag(
        std::string_view name,
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinIdTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinNickTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinThisbindTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinBindStopAllTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinNickRpTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinNameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinSurnameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTimeTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTimeNoSecTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinNickFunctionTag(std::string_view param, const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinParamcmdFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinKeyEmulateFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMathFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinWaitFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBinderActionFunctionTag(
        std::string_view action,
        std::string_view param,
        const EvaluationContext& context) const;

    EvaluationContext ResolveActiveContext(std::string_view defaultSource = {}, std::string_view defaultText = {}) const;
    void OpenKeyEmulatePicker();
    void DrawKeyEmulatePickerPopup();
    std::string ResolvePlayerNickById(int id, const EvaluationContext& context) const;
    std::string ResolveLocalNick(const EvaluationContext& context) const;
    static std::string FormatCurrentTime(const char* format);
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
    void QueueVirtualKeyHold(unsigned int keyCode, int startDelayMs, int holdDurationMs) const;
    void ReleaseVirtualKeyHold(ActiveVirtualKeyHold& hold) const;
    void QueuePendingBindDelayOverride(std::uint64_t runtimeId, int delayMs) const;

    SampApi* sampApi_ = nullptr;
    BinderModule* binderModule_ = nullptr;
    std::string searchQuery_{};
    int selectedTagIndex_ = 0;
    MiscPage currentPage_ = MiscPage::Home;
    TagRegistry tagRegistry_{};
    std::vector<std::pair<std::string, std::string>> customVariables_{};
    mutable std::vector<ActiveVirtualKeyHold> activeVirtualKeyHolds_{};
    mutable std::vector<PendingBindDelayOverride> pendingBindDelayOverrides_{};
    std::string keyPickerSearchQuery_{};
    bool keyPickerHoverTriggered_ = false;
};
