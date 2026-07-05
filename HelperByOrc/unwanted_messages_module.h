#pragma once

#include "json_utils.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>
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

    bool ShouldBlock(const UnwantedMessageContext& context);

    bool IsMiscPageOpen() const;
    bool DrawMiscCard();
    void DrawMainPage();

private:
    struct NormalizerSettings {
        bool stripColors = false;
        bool collapseWhitespace = false;
        bool trim = false;
    };

    struct Settings {
        bool enabled = true;
        NormalizerSettings normalizer{};
        int maxPatternLength = 2048;
    };

    struct Rule {
        std::string id;
        bool enabled = true;
        RuleType type = RuleType::Literal;
        std::string text;
        bool nocase = false;
        bool wholeWord = false;
        std::string error;
        std::string warning;
        std::optional<std::regex> compiledRegex;
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
        double maxMs = 0.0;
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
        double avgMs = 0.0;
        double maxMs = 0.0;
        std::size_t maxCandidates = 0;
        std::size_t maxRules = 0;
        std::size_t maxRegexRules = 0;
    };

    enum class RuleFilter {
        All,
        Enabled,
        Disabled,
        Regex,
        Literal,
        Errors,
    };

    struct RuleDraft {
        bool active = false;
        bool createMode = false;
        std::string id;
        bool enabled = true;
        RuleType type = RuleType::Literal;
        std::string text;
        bool nocase = false;
        bool wholeWord = false;
        bool dirty = false;
    };

    jsonutil::JsonValue SerializeConfig() const;
    void LoadFromConfig(const jsonutil::JsonObject& section);
    void SaveConfig() const;
    void CompileRules();
    std::string AllocateRuleId();

    std::vector<std::string> BuildCandidates(const UnwantedMessageContext& context) const;
    std::string NormalizeCandidate(std::string_view text) const;
    bool MatchCandidates(const std::vector<std::string>& candidates, UnwantedMessageSource source, MatchResult* result) const;
    bool MatchRule(const Rule& rule, std::string_view candidate) const;
    bool MatchLiteral(const Rule& rule, std::string_view candidate) const;
    std::optional<PerfLogSnapshot> AccumulatePerfStats(
        double elapsedMs,
        std::size_t candidateCount,
        std::size_t enabledRules,
        std::size_t regexRules,
        bool blocked);

    void DrawHeader(bool& reload);
    void DrawRulesPane(const ImVec2& size);
    void DrawInspectorPane(const ImVec2& size);
    void DrawRulesTable(const std::vector<std::size_t>& visibleIndices);
    void DrawRuleEditor();
    void DrawTesterPanel();
    void DrawRegexHelperWizard();
    void DrawSettingsPopup();
    void DrawDeleteConfirmPopup();

    std::string AddRule(RuleType type, std::string text, bool nocase, bool wholeWord);
    void DeleteRuleByIndex(std::size_t index);
    void DeleteSelectedRules();
    void SetAllRulesEnabled(bool enabled);
    void SetSelectedRulesEnabled(bool enabled);
    void RemoveDuplicateRules();
    void SortRulesByType();
    void SortRulesByText();
    bool IsRuleSelected(std::string_view id) const;
    void SetRuleSelected(std::string_view id, bool selected);
    void ClearSelection();
    void SelectAllRules();
    void EnsureActiveRule();
    int FindRuleIndexById(std::string_view id) const;
    Rule* FindRuleById(std::string_view id);
    const Rule* FindRuleById(std::string_view id) const;
    void StartCreateRule(std::string text = {}, RuleType type = RuleType::Literal);
    void StartEditRule(std::string_view id);
    bool SaveDraftRule();
    bool RuleMatchesFilter(const Rule& rule, std::string_view loweredSearch) const;
    void RegenerateHelperOutput();

    std::string GenerateExactRegex(std::string_view sample) const;
    std::string GenerateGeneralizedRegex(std::string_view sample) const;
    std::string GenerateContainsRegex(std::string_view sample) const;
    std::string GenerateGeneralizedRegex(std::string_view sample, bool anchors) const;

    mutable std::mutex mutex_;
    Settings settings_{};
    std::vector<Rule> rules_{};
    std::uint64_t nextRuleSerial_ = 1;
    bool miscPageOpen_ = false;
    std::set<std::string, std::less<>> selectedRuleIds_{};
    std::string activeRuleId_{};
    RuleDraft ruleDraft_{};
    RuleFilter ruleFilter_ = RuleFilter::All;
    std::string ruleSearch_{};
    bool deleteSelectedConfirmOpen_ = false;

    std::uint64_t blockedCount_ = 0;
    MatchResult lastBlocked_{};
    MatchResult lastTesterMatch_{};
    PerfStats perfStats_{};

    std::string testText_{};
    std::string helperSample_{};
    bool helperAnchors_ = true;
    bool helperColors_ = true;
    bool helperNumbers_ = true;
    bool helperMoney_ = true;
    bool helperTime_ = true;
    bool helperNick_ = true;
    bool helperPlayerId_ = true;
    bool helperDomain_ = true;
    bool helperBracketTag_ = true;
    std::string helperExact_{};
    std::string helperGeneralized_{};
    std::string helperContains_{};
};
