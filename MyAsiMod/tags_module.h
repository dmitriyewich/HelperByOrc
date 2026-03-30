#pragma once

#include "ui_settings.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class SampApi;

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
    };

    struct OwnedEvaluationContext {
        SampApi* sampApi = nullptr;
        std::string activationSource;
        std::string activationText;
        std::string bindCommand;
    };

    TagsModule();

    void OnProcessAttach();
    void Shutdown();

    void SetSampApi(SampApi* sampApi);

    void PushContext(const EvaluationContext& context) const;
    void PopContext() const;

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
    std::optional<std::string> ResolveBuiltinNickFunctionTag(std::string_view param, const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinParamcmdFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;

    EvaluationContext ResolveActiveContext(std::string_view defaultSource = {}, std::string_view defaultText = {}) const;
    std::string ResolvePlayerNickById(int id, const EvaluationContext& context) const;

    static std::string Trim(std::string_view value);
    static std::string ToLower(std::string_view value);
    static bool StartsWith(std::string_view value, std::string_view prefix);
    static std::vector<std::string> SplitCommandArgs(std::string_view value);
    static std::optional<int> ParseInteger(std::string_view value);
    static OwnedEvaluationContext MakeOwnedContext(const EvaluationContext& context, SampApi* fallbackSampApi);
    static EvaluationContext MakeViewContext(const OwnedEvaluationContext& context);

    SampApi* sampApi_ = nullptr;
    std::string searchQuery_{};
    std::string previewTemplate_ = "{id}";
    std::string previewBindCommand_ = "/test";
    std::string previewCommandText_ = "/test first second third";
    int previewLaunchSource_ = 1;
    int selectedTagIndex_ = 0;
    MiscPage currentPage_ = MiscPage::Home;
    TagRegistry tagRegistry_{};
    std::vector<std::pair<std::string, std::string>> customVariables_{};
};
