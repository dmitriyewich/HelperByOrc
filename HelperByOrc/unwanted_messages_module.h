#pragma once

#include "json_utils.h"
#include "rak_message_history_ui.h"
#include "text_pattern_builder.h"
#include "text_pattern_constructor_ui.h"
#include "text_pattern_engine.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct ImVec2;

enum class UnwantedMessageSource {
    CChatAddEntry,
    CChatAddMessage,
    CChatAddChatMessage,
    RakClientMessage,
    RakChat,
    RakChatBubble,
};

struct UnwantedMessageContext {
    UnwantedMessageSource source = UnwantedMessageSource::CChatAddEntry;
    int chatType = -1;
    int playerId = -1;
    std::string playerName;
    std::string text;
    std::string prefix;
    std::uint32_t textColor = 0;
    std::uint32_t prefixColor = 0;
};

class UnwantedMessagesModule {
public:
    enum class RuleType {
        Literal,
        Regex,
    };

    void OnProcessAttach();
    void Shutdown();
    void ReloadConfig();
    void SetSampRakHooks(SampRakHooks* hooks);
    void SetChatAsiCompatibilityHandler(std::function<void(bool)> handler);

    bool ShouldBlock(const UnwantedMessageContext& context);

    bool IsMiscPageOpen() const;
    bool DrawMiscCard();
    void DrawMainPage();

private:
    enum class Page {
        Closed,
        Rules,
        Editor,
    };

    struct NormalizerSettings {
        bool stripColors = false;
        bool collapseWhitespace = false;
        bool trim = false;
    };

    struct Settings {
        bool enabled = true;
        bool chatAsiCompatibility = true;
        NormalizerSettings normalizer{};
        int maxPatternLength = 2048;
    };

    enum class ValidationError {
        None,
        Empty,
        TooLong,
        UnknownType,
        RegexCompile,
    };

    struct RuleValidation {
        ValidationError error = ValidationError::None;
        std::size_t pcreErrorOffset = 0;
        bool unanchored = false;
        bool broadWildcard = false;
        bool matchesEmpty = false;
    };

    struct PreparedRule {
        RuleType type = RuleType::Literal;
        std::string text;
        std::string literalNeedle;
        bool nocase = false;
        bool wholeWord = false;
        std::unique_ptr<unwanted_regex::Program> compiledRegex;
    };

    struct Rule {
        std::string id;
        std::string name;
        std::string sample;
        bool enabled = true;
        RuleType type = RuleType::Literal;
        std::string text;
        bool nocase = false;
        bool wholeWord = false;
        std::string rawType;
        bool invalidType = false;
        RuleValidation validation{};
        std::string error;
        std::string warning;
        std::shared_ptr<PreparedRule> prepared;
        std::string searchBlobLower;
        std::string sortNameLower;
        std::string exactDisplayText;
        bool duplicate = false;
    };

    struct RuntimeRule {
        std::string id;
        std::shared_ptr<PreparedRule> prepared;
    };

    struct RuntimeSnapshot {
        Settings settings{};
        std::vector<RuntimeRule> rules{};
        std::size_t regexRules = 0;
        bool hasNoCaseLiteral = false;
    };

    struct RuntimeWarningState {
        std::string status;
        std::uint64_t lastLogMs = 0;
    };

    struct RegexLogEvent {
        std::string ruleId;
        std::string status;
        int errorCode = 0;
    };

    struct MatchResult {
        bool matched = false;
        std::string ruleId;
        std::string ruleText;
        std::string candidate;
        UnwantedMessageSource source = UnwantedMessageSource::CChatAddEntry;
    };

    struct PerfStats {
        std::uint64_t windowStartMs = 0;
        std::uint64_t messages = 0;
        std::uint64_t blocked = 0;
        std::uint64_t candidates = 0;
        std::uint64_t ruleChecks = 0;
        double totalMs = 0.0;
        double waitMs = 0.0;
        double matchMs = 0.0;
        double maxTotalMs = 0.0;
        double maxWaitMs = 0.0;
        double maxMatchMs = 0.0;
        std::size_t maxCandidates = 0;
        std::size_t maxRules = 0;
        std::size_t maxRegexRules = 0;
    };

    struct PerfLogSnapshot {
        std::uint64_t windowMs = 0;
        std::uint64_t messages = 0;
        std::uint64_t blocked = 0;
        std::uint64_t candidates = 0;
        std::uint64_t ruleChecks = 0;
        double avgTotalMs = 0.0;
        double avgWaitMs = 0.0;
        double avgMatchMs = 0.0;
        double maxTotalMs = 0.0;
        double maxWaitMs = 0.0;
        double maxMatchMs = 0.0;
        std::size_t maxCandidates = 0;
        std::size_t maxRules = 0;
        std::size_t maxRegexRules = 0;
    };

    enum class RuleFilter {
        All,
        Enabled,
        Disabled,
        Problems,
    };

    enum class RuleTypeFilter {
        All,
        Literal,
        Regex,
    };

    enum class RuleSort {
        Stored,
        Name,
        Type,
        Status,
    };

    struct RuleDraft {
        bool active = false;
        bool createMode = false;
        bool nameAutoGenerated = false;
        std::string id;
        std::string name;
        std::string sample;
        bool enabled = true;
        RuleType type = RuleType::Literal;
        std::string text;
        bool nocase = false;
        bool wholeWord = false;
        bool dirty = false;
    };

    struct DraftValidationCache {
        std::string text;
        RuleType type = RuleType::Literal;
        bool nocase = false;
        int maxPatternLength = 0;
        int language = -1;
        bool valid = false;
        bool ready = false;
        std::string error;
        std::string warning;
    };

    enum class TesterResult {
        Neutral,
        Match,
        NoMatch,
        RuleError,
    };

    struct DraftTesterCache {
        std::string testText;
        std::string pattern;
        RuleType type = RuleType::Literal;
        bool nocase = false;
        bool wholeWord = false;
        NormalizerSettings normalizer{};
        bool ready = false;
        TesterResult result = TesterResult::Neutral;
        std::string normalized;
    };

    jsonutil::JsonValue SerializeConfig() const;
    void LoadFromConfig(const jsonutil::JsonObject& section);
    void SaveConfig() const;
    void SaveSettings() const;
    void ApplyChatAsiCompatibilitySetting() const;
    void CompileRules();
    void CompileRule(Rule& rule);
    void FormatRuleDiagnostics(Rule& rule) const;
    void RefreshLocalizedDiagnostics();
    void RebuildRuleViewCache();
    void PublishRuntimeSnapshot();
    std::string AllocateRuleId();

    std::vector<std::string> BuildCandidates(const UnwantedMessageContext& context, const Settings& settings) const;
    std::string NormalizeCandidate(std::string_view text, const Settings& settings) const;
    std::string NormalizeCandidate(std::string_view text) const;
    bool MatchCandidates(
        const RuntimeSnapshot& snapshot,
        const std::vector<std::string>& candidates,
        const std::vector<std::string>& foldedCandidates,
        UnwantedMessageSource source,
        MatchResult* result,
        std::size_t& actualChecks,
        std::vector<unsigned char>& evaluationState,
        std::vector<unwanted_regex::MatchResult>& runtimeErrors) const;
    unwanted_regex::MatchResult MatchRule(
        const RuntimeRule& rule,
        std::string_view candidate,
        std::string_view foldedCandidate) const;
    bool MatchLiteral(const PreparedRule& rule, std::string_view candidate, std::string_view foldedCandidate) const;
    std::optional<PerfLogSnapshot> AccumulatePerfStats(
        double totalMs,
        double waitMs,
        double matchMs,
        std::size_t candidateCount,
        std::size_t enabledRules,
        std::size_t regexRules,
        std::size_t actualChecks,
        bool blocked);
    void ApplyRuntimeEvaluation(
        const RuntimeSnapshot& snapshot,
        const std::vector<unsigned char>& evaluationState,
        const std::vector<unwanted_regex::MatchResult>& runtimeErrors,
        std::vector<RegexLogEvent>& logs);

    void DrawRulesPage();
    void DrawEditorPage();
    void DrawRulesHeader();
    void DrawRulesSummary();
    void DrawUnsavedConfirmPopup();
    void DrawRulesPane(const ImVec2& size);
    void DrawRulesTable(const std::vector<std::size_t>& visibleIndices);
    void DrawRuleTypeFilter(float width);
    void DrawRuleMessageStep();
    void DrawRuleResultStep();
    void DrawRuleAdvancedSettings();
    void DrawEditorFooter();
    void DrawTesterPanel();
    void DrawRegexReferencePanel();
    void RefreshRegexReferenceFilter();
    void DrawSettingsPopup();
    void DrawDeleteConfirmPopup();

    std::string AddRule(
        RuleType type,
        std::string text,
        bool enabled,
        bool nocase,
        bool wholeWord,
        std::string name = {},
        std::string sample = {});
    void DeleteSelectedRules();
    void SetSelectedRulesEnabled(bool enabled);
    bool IsRuleSelected(std::string_view id) const;
    void SetRuleSelected(std::string_view id, bool selected);
    void ClearSelection();
    void SelectVisibleRules(const std::vector<std::size_t>& visibleIndices, bool selected);
    void RetainVisibleSelection(const std::vector<std::size_t>& visibleIndices);
    bool RuleHasProblem(const Rule& rule) const;
    std::string RuleDisplayTitle(const Rule& rule) const;
    std::string RuleDisplaySummary(const Rule& rule) const;
    void DuplicateRule(std::string_view id);
    int FindRuleIndexById(std::string_view id) const;
    Rule* FindRuleById(std::string_view id);
    const Rule* FindRuleById(std::string_view id) const;
    void StartCreateRule(std::string sample = {}, RuleType type = RuleType::Literal);
    void StartEditRule(std::string_view id);
    bool SaveDraftRule();
    bool RuleMatchesFilter(const Rule& rule, std::string_view loweredSearch) const;
    void RegenerateHelperOutput();
    void ApplyAutomaticHelperToDraft();
    void RefreshDraftTester();
    bool ValidateDraft(std::string& error, std::string& warning) const;
    bool IsDuplicateRule(std::size_t index) const;

    Settings settings_{};
    std::vector<Rule> rules_{};
    std::uint64_t nextRuleSerial_ = 1;
    Page page_ = Page::Closed;
    std::set<std::string, std::less<>> selectedRuleIds_{};
    std::string activeRuleId_{};
    std::string scrollToRuleId_{};
    RuleDraft ruleDraft_{};
    RuleFilter ruleFilter_ = RuleFilter::All;
    RuleTypeFilter ruleTypeFilter_ = RuleTypeFilter::All;
    RuleSort ruleSort_ = RuleSort::Stored;
    std::string ruleSearch_{};
    bool selectionMode_ = false;
    bool focusRuleSearch_ = false;
    bool deleteSelectedConfirmOpen_ = false;
    bool unsavedConfirmOpen_ = false;
    bool reloadRequested_ = false;
    double settingsSavedUntil_ = 0.0;
    std::function<void(bool)> chatAsiCompatibilityHandler_;
    SampRakHooks* sampRakHooks_ = nullptr;
    bool reloadAfterDiscard_ = false;
    Page pendingPageAfterDiscard_ = Page::Rules;

    std::uint64_t blockedCount_ = 0;
    MatchResult lastBlocked_{};
    PerfStats perfStats_{};
    std::shared_ptr<const RuntimeSnapshot> runtimeSnapshot_{};
    mutable std::mutex snapshotMutex_{};
    mutable std::mutex matchMutex_{};
    mutable std::mutex statusMutex_{};
    std::unordered_map<std::string, RuntimeWarningState> runtimeWarnings_{};
    std::unordered_map<std::string, std::string> runtimeWarningsView_{};
    int diagnosticsLanguage_ = -1;
    mutable DraftValidationCache draftValidationCache_{};
    DraftTesterCache draftTesterCache_{};

    std::string testText_{};
    std::string helperSample_{};
    int helperWorkspaceMode_ = 0;
    std::string helperGeneralized_{};
    std::vector<unwanted_regex_builder::Token> helperTokens_{};
    text_pattern_constructor_ui::State helperConstructor_{};
    rak_message_history_ui::PickerState messageHistoryPicker_{};
    std::string helperWarning_{};
    bool helperGeneralizedValid_ = false;
    std::string regexReferenceSearch_{};
    int regexReferenceFilterLanguage_ = -1;
    std::string regexReferenceFilterSearch_{};
    std::vector<std::size_t> regexReferenceVisibleItems_{};
};
