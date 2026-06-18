#pragma once

#include "conditions_module.h"
#include "hotkey_utils.h"

#include <windows.h>

#include <cstdint>
#include <optional>
#include <regex>
#include <string>
#include <vector>

inline constexpr UINT kBinderDefaultConfirmKey = '1';
inline constexpr UINT kBinderDefaultCancelKey = '2';

enum class InputMode {
    Text,
    ButtonsList,
    ButtonsListText,
};

struct HotkeyMessage {
    std::string text;
    int intervalMs = 0;
    int method = 0;
};

struct InputButton {
    std::string label;
    std::string text;
    std::string hint;
    std::string when;
};

struct HotkeyInput {
    std::string key;
    std::string label;
    std::string hint;
    InputMode mode = InputMode::Text;
    std::vector<InputButton> buttons;
    bool multiSelect = false;
    std::string multiSeparator = ", ";
    std::string cascadeParentKey;
};

struct TextTrigger {
    std::string text;
    bool enabled = false;
    bool pattern = false;

    std::string runtimeCacheText;
    bool runtimeCachePattern = false;
    bool runtimeCacheReady = false;
    bool runtimeRegexInvalid = false;
    std::string runtimeNormalizedText;
    std::optional<std::regex> runtimeRegex;

    void InvalidateRuntimeCache() {
        runtimeCacheText.clear();
        runtimeCachePattern = false;
        runtimeCacheReady = false;
        runtimeRegexInvalid = false;
        runtimeNormalizedText.clear();
        runtimeRegex.reset();
    }
};

struct TextConfirmation {
    bool enabled = false;
    UINT key = kBinderDefaultConfirmKey;
    UINT cancelKey = kBinderDefaultCancelKey;
    bool waitForResolution = true;
};

struct CommandConfirmation {
    bool enabled = false;
    bool waitForResolution = true;
};

struct HotkeyEntry {
    std::string label;
    std::string iconId;
    std::vector<UINT> keys;
    HotkeyMode hotkeyMode = HotkeyMode::ModifierTrigger;
    std::vector<HotkeyMessage> messages;
    std::vector<HotkeyInput> inputs;
    TextTrigger textTrigger;
    TextConfirmation textConfirmation;
    CommandConfirmation commandConfirmation;
    std::vector<bool> conditions;
    ConditionCombineMode conditionsCombine = ConditionCombineMode::RequireAny;
    bool repeatMode = false;
    int repeatIntervalMs = 0;
    bool enabled = true;
    bool quickMenu = false;
    std::string command;
    bool commandEnabled = false;
    std::string categoryId;
    std::vector<std::string> folderPath;
    std::string orderId;
    std::uint64_t runtimeId = 0;

    int number = 0;
    bool comboActive = false;
    std::vector<UINT> lastRepeatPressed;
    double lastActivatedAtMs = 0.0;
    double debounceUntilMs = 0.0;
    bool awaitingInput = false;
    bool waitingTextConfirmation = false;
    double textConfirmationDeadlineMs = 0.0;
    UINT pendingConfirmationKey = kBinderDefaultConfirmKey;
    UINT pendingConfirmationCancelKey = kBinderDefaultCancelKey;
    std::string pendingTriggerText;
    std::string pendingTriggerSource;
};

struct ButtonsTextParseStats {
    int total = 0;
    int used = 0;
    int ignored = 0;
    int extraPipes = 0;
};

struct ButtonsBulkPreviewState {
    bool active = false;
    int count = 0;
    ButtonsTextParseStats stats{};
};
