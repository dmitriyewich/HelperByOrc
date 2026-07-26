#pragma once

#include "tags_module_impl.h"

#include "app_config.h"
#include "arizona_cef_dialogs.h"
#include "binder_module.h"
#include "debug_log.h"
#include "hotkey_utils.h"
#include "json_utils.h"
#include "notification_manager.h"
#include "samp_api.h"
#include "user_files_path.h"

#include <game_sa/CModelInfo.h>
#include <game_sa/CPlayerInfo.h>
#include <game_sa/CSprite.h>
#include <game_sa/CTheZones.h>
#include <game_sa/CVehicle.h>
#include <game_sa/CVehicleModelInfo.h>
#include <extensions/ScriptCommands.h>
#include <game_sa/eScriptCommands.h>
#include <game_sa/CPools.h>
#include <game_sa/common.h>
#include <RenderWare.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace tags_module_detail {
inline thread_local std::vector<TagsModule::OwnedEvaluationContext> g_activeContextStack;
}

using tags_module_detail::g_activeContextStack;

namespace {
constexpr std::string_view kTagsSectionName = "tags";
constexpr std::string_view kCustomVarsKey = "custom_vars";
constexpr std::string_view kExpandExternalTagsKey = "expand_external_tags";
constexpr int kRecursionLimit = 10;
constexpr int kKeyEmulateStartDelayMs = 20;
constexpr int kKeyEmulateTapMs = 35;
constexpr int kMaxSampPlayerId = 1003;
constexpr float kClosestScreenTargetZOffset = 0.9f;
constexpr double kClosestPlayerSlowQueryLogMs = 4.0;
constexpr std::uint64_t kClosestPlayerSlowQueryLogThrottleMs = 3000;
constexpr std::uint64_t kClosestPlayerPerfTelemetryWindowMs = 5000;
constexpr std::uint64_t kMyCarSnapshotSlowQueryLogMs = 10;
constexpr std::uint64_t kMyCarSnapshotSlowQueryLogThrottleMs = 3000;
constexpr std::uint64_t kMyCarPerfTelemetryWindowMs = 5000;
constexpr std::uint64_t kPlayerNamePerfTelemetryWindowMs = 5000;
constexpr std::uint64_t kTagsPerfTelemetryWindowMs = 5000;
constexpr std::uint64_t kExternalTagsPerfTelemetryWindowMs = 5000;
constexpr std::uint64_t kExternalTagsFailureLogThrottleMs = 3000;
constexpr std::uint64_t kTagExpansionSlowLogThrottleMs = 1000;
constexpr double kTagExpansionSlowLogMs = 4.0;
constexpr std::size_t kClipboardTagMaxLength = 4096;
constexpr unsigned int kAnsiCodePage = CP_ACP;
constexpr std::uintptr_t kTakeScreenshotAddress = 0x5D0820;
constexpr wchar_t kHelperScreensRelativePath[] = L"screens";
constexpr wchar_t kHelperSavedDialogsRelativePath[] = L"saved\\dialogs";
constexpr std::uint64_t kDialogWaitOpenTimeoutMs = 3000;
constexpr int kArzDialogQueryDefaultTimeoutMs = 500;
constexpr int kArzDialogQueryMaxTimeoutMs = 3000;
constexpr int kRandomMinInt = -2147483647;
constexpr int kRandomMaxInt = 2147483647;

enum class ScreenCaptureError {
    None,
    DocumentsUnavailable,
    InvalidFolder,
    CaptureFailed,
};

enum class TimeFormatError {
    None,
    MissingSemicolon,
    EmptyFormat,
    InvalidSpecifier,
    FormatFailed,
};

struct ScreenCaptureResult {
    ScreenCaptureError error = ScreenCaptureError::None;
    std::string detail{};
    std::filesystem::path savedPath{};

    bool Ok() const {
        return error == ScreenCaptureError::None;
    }
};

struct TimeFormatParseResult {
    TimeFormatError error = TimeFormatError::None;
    std::string format{};
    std::string invalidToken{};

    bool Ok() const {
        return error == TimeFormatError::None;
    }
};

struct DialogTextToken {
    int index = 0;
    std::string text{};
};

struct DialogTextItems {
    std::vector<DialogTextToken> flat{};
    std::vector<std::vector<DialogTextToken>> rows{};
};

struct DialogListItemInfo {
    int index0 = 0;
    int index1 = 1;
    std::string text{};
    std::string rawText{};
};

struct DialogListItems {
    std::vector<DialogListItemInfo> items{};
    std::string headerText{};
};

struct DialogResponseParams {
    bool valid = false;
    bool hasItemPart = false;
    bool hasTextPart = false;
    std::string_view button{};
    std::string_view item{};
    std::string_view text{};
};

struct ArzDialogSendRespondParams {
    bool valid = false;
    bool hasListPart = false;
    bool hasInputPart = false;
    std::string_view id{};
    std::string_view button{};
    std::string_view listItem{};
    std::string_view input{};
};

bool TryParseSampColorTag(std::string_view text, std::size_t offset, std::size_t& consumed, std::uint32_t* color = nullptr) {
    consumed = 0;
    if (offset >= text.size() || text[offset] != '{') {
        return false;
    }

    const std::size_t close = text.find('}', offset + 1);
    if (close == std::string_view::npos) {
        return false;
    }

    const std::size_t hexLength = close - offset - 1;
    if (hexLength != 6 && hexLength != 8) {
        return false;
    }

    std::uint32_t value = 0;
    for (std::size_t i = offset + 1; i < close; ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (!std::isxdigit(ch)) {
            return false;
        }
        value = static_cast<std::uint32_t>(value * 16 + static_cast<std::uint32_t>(
            std::isdigit(ch) ? (ch - '0') : (std::tolower(ch) - 'a' + 10)));
    }

    consumed = close - offset + 1;
    if (color) {
        *color = value;
    }
    return true;
}

struct ImGuiStringUserData {
    std::string* value = nullptr;
    ImGuiInputTextCallback chain = nullptr;
    void* chainUserData = nullptr;
};

int ImGuiStringResizeCallback(ImGuiInputTextCallbackData* data) {
    auto* userData = static_cast<ImGuiStringUserData*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        IM_ASSERT(userData && userData->value);
        userData->value->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = userData->value->data();
        return 0;
    }
    if (userData && userData->chain) {
        data->UserData = userData->chainUserData;
        return userData->chain(data);
    }
    return 0;
}

bool InputTextString(const char* label, std::string& value, ImGuiInputTextFlags flags = 0, std::size_t minBuffer = 256) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }

    ImGuiStringUserData userData{ &value, nullptr, nullptr };
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputText(label, value.data(), value.capacity() + 1, flags, ImGuiStringResizeCallback, &userData);
}

bool InputTextWithHintString(
    const char* label,
    const char* hint,
    std::string& value,
    ImGuiInputTextFlags flags = 0,
    std::size_t minBuffer = 256) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }

    ImGuiStringUserData userData{ &value, nullptr, nullptr };
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputTextWithHint(label, hint, value.data(), value.capacity() + 1, flags, ImGuiStringResizeCallback, &userData);
}

float ScaleUi(float value) {
    return UiSettings::Instance().Scale(value);
}

ImVec2 ScaleUi(float x, float y) {
    return UiSettings::Instance().Scale(ImVec2(x, y));
}

ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t);
}

bool DrawNavigationCardButton(
    const char* id,
    const char* title,
    const char* description,
    const char* actionLabel,
    const ImVec4& accent,
    float height) {
    const float cardHeight = ScaleUi(height);
    const ImVec2 screenMin = ImGui::GetCursorScreenPos();
    const ImVec2 screenMax(screenMin.x + ImGui::GetContentRegionAvail().x, screenMin.y + cardHeight);
    const bool hovered = ImGui::IsMouseHoveringRect(screenMin, screenMax);
    const bool held = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    const ImVec4 baseBg = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    const ImVec4 hoverBg = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
    const ImVec4 activeBg = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    const ImVec4 childBg = held
        ? LerpColor(baseBg, activeBg, 0.45f)
        : hovered ? LerpColor(baseBg, hoverBg, 0.35f) : baseBg;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, childBg);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ScaleUi(14.0f));
    const bool began = ImGui::BeginChild(id, ImVec2(0.0f, cardHeight), ImGuiChildFlags_FrameStyle);
    if (began) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 childMin = ImGui::GetWindowPos();
        const ImVec2 childMax(childMin.x + ImGui::GetWindowSize().x, childMin.y + ImGui::GetWindowSize().y);
        drawList->AddRectFilled(
            childMin,
            ImVec2(childMin.x + ScaleUi(6.0f), childMax.y),
            ImGui::GetColorU32(accent),
            ScaleUi(14.0f),
            ImDrawFlags_RoundCornersLeft);

        ImGui::SetCursorPos(ScaleUi(20.0f, 18.0f));
        ImGui::TextColored(accent, "%s", title);
        ImGui::Spacing();

        ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x - ScaleUi(20.0f));
        ImGui::TextWrapped("%s", description);
        ImGui::PopTextWrapPos();

        const ImVec2 actionSize = ImGui::CalcTextSize(actionLabel);
        ImGui::SetCursorPosX(std::max(ScaleUi(20.0f), ImGui::GetWindowWidth() - actionSize.x - ScaleUi(20.0f)));
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - ScaleUi(34.0f)));
        ImGui::TextDisabled("%s", actionLabel);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    return ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
}

bool SelectableUseToken(
    const std::string& label,
    const std::string& token,
    std::string& searchQuery,
    const std::function<void(std::string_view)>& tokenAction = {}) {
    if (!ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
        return false;
    }

    if (tokenAction) {
        tokenAction(token);
    } else {
        ImGui::SetClipboardText(token.c_str());
    }
    searchQuery.clear();
    ImGui::CloseCurrentPopup();
    return true;
}

template <typename Items, typename SearchFn, typename LabelFn, typename TokenFn>
void DrawSearchableTokenList(
    const char* childId,
    const Items& items,
    std::string_view filter,
    UiText emptyText,
    std::string& searchQuery,
    SearchFn searchFn,
    LabelFn labelFn,
    TokenFn tokenFn,
    const std::function<void(std::string_view)>& tokenAction = {}) {
    bool hasMatches = false;
    if (ImGui::BeginChild(childId, ScaleUi(0.0f, 360.0f), ImGuiChildFlags_Borders)) {
        for (const auto& item : items) {
            if (!filter.empty() && searchFn(item).find(filter) == std::string::npos) {
                continue;
            }

            hasMatches = true;
            if (SelectableUseToken(labelFn(item), tokenFn(item), searchQuery, tokenAction)) {
                break;
            }
        }

        if (!hasMatches) {
            ImGui::TextDisabled("%s", UiSettings::Instance().Text(emptyText));
        }
    }
    ImGui::EndChild();
}

std::string ToLowerAscii(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const unsigned char ch : value) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

std::string_view TrimAsciiView(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(begin, end - begin);
}

std::string TrimAscii(std::string_view value) {
    return std::string(TrimAsciiView(value));
}

std::string MakeKeyEmulateTokenImpl(UINT keyCode) {
    return "[keyemulate(" + std::to_string(keyCode) + ")]";
}

bool IsBetterClosestCandidate(float candidateDistanceSq, int candidateId, float bestDistanceSq, int bestId) {
    constexpr float kDistanceEpsilon = 0.0001f;
    if (candidateId < 0) {
        return false;
    }
    if (bestId < 0) {
        return true;
    }
    if (candidateDistanceSq + kDistanceEpsilon < bestDistanceSq) {
        return true;
    }
    return std::fabs(candidateDistanceSq - bestDistanceSq) <= kDistanceEpsilon && candidateId < bestId;
}

std::wstring MultiByteToWide(std::string_view text, unsigned int codePage, DWORD flags = 0) {
    if (text.empty()) {
        return {};
    }

    const int wideLength = MultiByteToWideChar(
        codePage,
        flags,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (wideLength <= 0) {
        return {};
    }

    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(
            codePage,
            flags,
            text.data(),
            static_cast<int>(text.size()),
            wide.data(),
            wideLength)
        <= 0) {
        return {};
    }

    return wide;
}

std::wstring Utf8ToWide(std::string_view text) {
    return MultiByteToWide(text, CP_UTF8, MB_ERR_INVALID_CHARS);
}

std::string WideToMultiByte(std::wstring_view text, unsigned int codePage) {
    if (text.empty()) {
        return {};
    }

    const int length = WideCharToMultiByte(
        codePage,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (length <= 0) {
        return {};
    }

    std::string encoded(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(
            codePage,
            0,
            text.data(),
            static_cast<int>(text.size()),
            encoded.data(),
            length,
            nullptr,
            nullptr)
        <= 0) {
        return {};
    }

    return encoded;
}

std::filesystem::path GetHelperScreensRoot(const std::filesystem::path& helperDataPath) {
    return helperDataPath / kHelperScreensRelativePath;
}

std::filesystem::path GetHelperSavedDialogsRoot(const std::filesystem::path& helperDataPath) {
    return helperDataPath / kHelperSavedDialogsRelativePath;
}

std::string Unquote(std::string value) {
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }
    return value;
}

std::vector<std::string_view> SplitTopLevelDelimitedParts(std::string_view text, char delimiter) {
    std::vector<std::string_view> parts;
    std::size_t partStart = 0;
    int roundDepth = 0;
    int squareDepth = 0;
    int curlyDepth = 0;
    char quote = '\0';
    bool escaped = false;

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == quote) {
                quote = '\0';
            }
            continue;
        }

        if (ch == '"' || ch == '\'') {
            quote = ch;
            continue;
        }
        if (ch == '(') {
            ++roundDepth;
            continue;
        }
        if (ch == ')' && roundDepth > 0) {
            --roundDepth;
            continue;
        }
        if (ch == '[') {
            ++squareDepth;
            continue;
        }
        if (ch == ']' && squareDepth > 0) {
            --squareDepth;
            continue;
        }
        if (ch == '{') {
            ++curlyDepth;
            continue;
        }
        if (ch == '}' && curlyDepth > 0) {
            --curlyDepth;
            continue;
        }

        if (ch == delimiter && roundDepth == 0 && squareDepth == 0 && curlyDepth == 0) {
            parts.push_back(text.substr(partStart, i - partStart));
            partStart = i + 1;
        }
    }

    parts.push_back(text.substr(partStart));
    return parts;
}

DialogResponseParams ParseDialogResponseParams(std::string_view rawParam) {
    DialogResponseParams result;
    const std::vector<std::string_view> parts = SplitTopLevelDelimitedParts(rawParam, ';');
    if (parts.empty() || parts.size() > 3) {
        return result;
    }

    result.valid = true;
    result.button = parts[0];
    if (parts.size() >= 2) {
        result.hasItemPart = true;
        result.item = parts[1];
    }
    if (parts.size() >= 3) {
        result.hasTextPart = true;
        result.text = parts[2];
    }
    return result;
}

ArzDialogSendRespondParams ParseArzDialogSendRespondParams(std::string_view rawParam) {
    ArzDialogSendRespondParams result;
    std::vector<std::string_view> parts;
    parts.reserve(4);

    std::size_t partStart = 0;
    int roundDepth = 0;
    int squareDepth = 0;
    int curlyDepth = 0;
    char quote = '\0';
    bool escaped = false;

    for (std::size_t i = 0; i < rawParam.size(); ++i) {
        const char ch = rawParam[i];
        if (quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == quote) {
                quote = '\0';
            }
            continue;
        }

        if (ch == '"' || ch == '\'') {
            quote = ch;
            continue;
        }
        if (ch == '(') {
            ++roundDepth;
            continue;
        }
        if (ch == ')' && roundDepth > 0) {
            --roundDepth;
            continue;
        }
        if (ch == '[') {
            ++squareDepth;
            continue;
        }
        if (ch == ']' && squareDepth > 0) {
            --squareDepth;
            continue;
        }
        if (ch == '{') {
            ++curlyDepth;
            continue;
        }
        if (ch == '}' && curlyDepth > 0) {
            --curlyDepth;
            continue;
        }

        if (ch == ';' && roundDepth == 0 && squareDepth == 0 && curlyDepth == 0 && parts.size() < 3) {
            parts.push_back(rawParam.substr(partStart, i - partStart));
            partStart = i + 1;
        }
    }

    parts.push_back(rawParam.substr(partStart));
    if (parts.size() < 2 || parts.size() > 4) {
        return result;
    }

    result.valid = true;
    result.id = parts[0];
    result.button = parts[1];
    if (parts.size() >= 3) {
        result.hasListPart = true;
        result.listItem = parts[2];
    }
    if (parts.size() >= 4) {
        result.hasInputPart = true;
        result.input = parts[3];
    }
    return result;
}

std::uint32_t MakeRandomSeed(std::uintptr_t salt) {
    const std::uint64_t wideSalt = static_cast<std::uint64_t>(salt);
    std::uint32_t seed = static_cast<std::uint32_t>(std::time(nullptr));
    seed ^= static_cast<std::uint32_t>(std::clock()) * 0x9E3779B9u;
    seed ^= static_cast<std::uint32_t>(wideSalt);
    seed ^= static_cast<std::uint32_t>(wideSalt >> 32);
    return seed != 0 ? seed : 0xA341316Cu;
}

std::uint32_t& TagRandomState() {
    static std::uint32_t state = MakeRandomSeed(reinterpret_cast<std::uintptr_t>(&TagRandomState));
    return state;
}

std::uint32_t NextRandomU32() {
    std::uint32_t& state = TagRandomState();
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

std::uint32_t RandomBounded(std::uint32_t bound) {
    if (bound == 0) {
        return 0;
    }

    const std::uint32_t threshold = (0u - bound) % bound;
    for (;;) {
        const std::uint32_t value = NextRandomU32();
        if (value >= threshold) {
            return value % bound;
        }
    }
}

std::size_t RandomIndex(std::size_t size) {
    return static_cast<std::size_t>(RandomBounded(static_cast<std::uint32_t>(size)));
}

int RandomIntInclusive(int minValue, int maxValue) {
    const auto span = static_cast<std::uint32_t>(
        static_cast<std::int64_t>(maxValue) - static_cast<std::int64_t>(minValue) + 1);
    return static_cast<int>(static_cast<std::int64_t>(minValue) + RandomBounded(span));
}

std::optional<std::int64_t> ParseTimeOffsetSeconds(std::string_view rawParam) {
    const std::vector<std::string_view> parts = SplitTopLevelDelimitedParts(rawParam, ':');
    if (parts.size() != 2 && parts.size() != 3) {
        return std::nullopt;
    }

    std::uint64_t values[3]{ 0, 0, 0 };
    const std::size_t offset = parts.size() == 2 ? 1 : 0;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const std::string_view trimmed = TrimAsciiView(parts[i]);
        if (trimmed.empty()) {
            return std::nullopt;
        }

        std::uint64_t parsed = 0;
        const auto [end, error] = std::from_chars(
            trimmed.data(),
            trimmed.data() + trimmed.size(),
            parsed,
            10);
        if (error != std::errc{} || end != trimmed.data() + trimmed.size()) {
            return std::nullopt;
        }

        values[offset + i] = parsed;
    }

    constexpr std::uint64_t kMaxSeconds =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (values[0] > kMaxSeconds / 3600
        || values[1] > kMaxSeconds / 60
        || values[2] > kMaxSeconds) {
        return std::nullopt;
    }
    const std::uint64_t hours = values[0] * 3600;
    const std::uint64_t minutes = values[1] * 60;
    if (hours > kMaxSeconds - minutes || hours + minutes > kMaxSeconds - values[2]) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(hours + minutes + values[2]);
}

std::optional<std::pair<int, int>> ParseRandomIntegerRange(std::string_view rawValue) {
    const std::string trimmed = TrimAscii(rawValue);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    for (std::size_t i = 1; i < trimmed.size(); ++i) {
        if (trimmed[i] != '-') {
            continue;
        }

        const std::string left = TrimAscii(trimmed.substr(0, i));
        const std::string right = TrimAscii(trimmed.substr(i + 1));
        if (left.empty() || right.empty()) {
            continue;
        }

        char* end = nullptr;
        const long leftValue = std::strtol(left.c_str(), &end, 10);
        if (!end || *end != '\0') {
            continue;
        }

        end = nullptr;
        const long rightValue = std::strtol(right.c_str(), &end, 10);
        if (!end || *end != '\0') {
            continue;
        }

        if (leftValue < std::numeric_limits<int>::min()
            || leftValue > std::numeric_limits<int>::max()
            || rightValue < std::numeric_limits<int>::min()
            || rightValue > std::numeric_limits<int>::max()) {
            continue;
        }

        int minValue = static_cast<int>(leftValue);
        int maxValue = static_cast<int>(rightValue);
        if (minValue > maxValue) {
            std::swap(minValue, maxValue);
        }
        return std::pair<int, int>{ minValue, maxValue };
    }

    return std::nullopt;
}

std::string FormatCurrentTimeForTimestamp(std::time_t timestamp, std::string_view format) {
    if (format.empty()) {
        return {};
    }

    std::tm localTime{};
    if (localtime_s(&localTime, &timestamp) != 0) {
        return {};
    }

    std::array<char, 256> formatBuffer{};
    std::string dynamicFormat;
    const char* formatText = nullptr;
    if (format.size() < formatBuffer.size()) {
        std::copy(format.begin(), format.end(), formatBuffer.begin());
        formatText = formatBuffer.data();
    } else {
        dynamicFormat.assign(format.begin(), format.end());
        formatText = dynamicFormat.c_str();
    }

    std::array<char, 256> stackBuffer{};
    if (const std::size_t written =
            std::strftime(stackBuffer.data(), stackBuffer.size(), formatText, &localTime);
        written != 0) {
        return std::string(stackBuffer.data(), written);
    }

    std::size_t bufferSize = std::max<std::size_t>(128, format.size() * 8 + 32);
    for (int attempt = 0; attempt < 6; ++attempt) {
        std::string buffer(bufferSize, '\0');
        const std::size_t written = std::strftime(buffer.data(), buffer.size(), formatText, &localTime);
        if (written != 0) {
            buffer.resize(written);
            return buffer;
        }
        bufferSize *= 2;
    }

    return {};
}

bool IsValidRelativeScreenFolder(const std::filesystem::path& relativePath) {
    if (relativePath.empty()) {
        return true;
    }
    if (relativePath.is_absolute() || relativePath.has_root_name() || relativePath.has_root_directory()) {
        return false;
    }

    constexpr std::wstring_view invalidChars = L"<>:\"|?*";
    for (const auto& part : relativePath) {
        const std::wstring component = part.native();
        if (component.empty() || component == L"." || component == L"..") {
            return false;
        }

        for (const wchar_t ch : component) {
            if (invalidChars.find(ch) != std::wstring_view::npos) {
                return false;
            }
        }
    }

    return true;
}

std::wstring TryGetShortPath(std::wstring_view path) {
    if (path.empty()) {
        return {};
    }

    const DWORD required = GetShortPathNameW(path.data(), nullptr, 0);
    if (required == 0) {
        return {};
    }

    std::wstring shortPath(static_cast<std::size_t>(required), L'\0');
    const DWORD written = GetShortPathNameW(path.data(), shortPath.data(), required);
    if (written == 0) {
        return {};
    }

    shortPath.resize(static_cast<std::size_t>(written));
    return shortPath;
}

std::wstring MakeScreenshotBaseName() {
    SYSTEMTIME localTime{};
    GetLocalTime(&localTime);

    wchar_t buffer[64]{};
    swprintf_s(
        buffer,
        L"%02u.%02u.%04u %02u.%02u.%02u.%03u",
        localTime.wDay,
        localTime.wMonth,
        localTime.wYear,
        localTime.wHour,
        localTime.wMinute,
        localTime.wSecond,
        localTime.wMilliseconds);
    return buffer;
}

std::filesystem::path MakeUniqueScreenshotPath(const std::filesystem::path& directory) {
    const std::wstring baseName = MakeScreenshotBaseName();

    std::filesystem::path candidate = directory / (baseName + L".png");
    int suffix = 1;
    while (std::filesystem::exists(candidate)) {
        candidate = directory / (baseName + L" (" + std::to_wstring(suffix++) + L").png");
    }
    return candidate;
}

ScreenCaptureResult CaptureGameScreenshot(std::string_view utf8Subfolder) {
    const std::optional<std::filesystem::path> helperDataPath = helper_paths::ResolveHelperDataDirectory();
    if (!helperDataPath.has_value()) {
        return ScreenCaptureResult{ ScreenCaptureError::DocumentsUnavailable };
    }

    std::filesystem::path targetDirectory = GetHelperScreensRoot(*helperDataPath);
    if (!utf8Subfolder.empty()) {
        std::filesystem::path relativePath(Utf8ToWide(utf8Subfolder));
        if (relativePath.empty() || !IsValidRelativeScreenFolder(relativePath)) {
            return ScreenCaptureResult{ ScreenCaptureError::InvalidFolder, std::string(utf8Subfolder) };
        }
        targetDirectory /= relativePath;
    }

    std::error_code error;
    std::filesystem::create_directories(targetDirectory, error);
    if (error) {
        return ScreenCaptureResult{ ScreenCaptureError::CaptureFailed };
    }

    const std::filesystem::path screenshotPath = MakeUniqueScreenshotPath(targetDirectory);
    const std::wstring shortDirectory = TryGetShortPath(targetDirectory.wstring());
    const std::wstring directoryForGame = shortDirectory.empty() ? targetDirectory.wstring() : shortDirectory;
    const std::wstring filename = screenshotPath.filename().wstring();
    const std::wstring gamePathWide = std::filesystem::path(directoryForGame).append(filename).wstring();
    const std::string gamePathAnsi = WideToMultiByte(gamePathWide, kAnsiCodePage);
    if (gamePathAnsi.empty()) {
        return ScreenCaptureResult{ ScreenCaptureError::CaptureFailed };
    }

    auto takeScreenshot = reinterpret_cast<void(__cdecl*)(std::uintptr_t, const char*)>(kTakeScreenshotAddress);
    if (!takeScreenshot) {
        return ScreenCaptureResult{ ScreenCaptureError::CaptureFailed };
    }

    takeScreenshot(0, gamePathAnsi.c_str());
    return ScreenCaptureResult{ ScreenCaptureError::None, {}, screenshotPath };
}

bool TakeGameCameraPhoto() {
    // MoonLoader takePhoto() maps to opcode 0A1E / TAKE_PHOTO.
    // The opcode itself performs a gallery-style photo capture and does not
    // require the player to actually hold the camera weapon.
    plugin::Command<plugin::Commands::TAKE_PHOTO>(true);
    return true;
}

std::string DescribeScreenCaptureError(ScreenCaptureError error, std::string_view detail) {
    UiSettings& ui = UiSettings::Instance();
    switch (error) {
    case ScreenCaptureError::DocumentsUnavailable:
        return ui.Text(UiText::ToastScreenDocumentsUnavailable);
    case ScreenCaptureError::InvalidFolder:
        return ui.Format(UiText::ToastScreenInvalidFolder, std::string(detail).c_str());
    case ScreenCaptureError::CaptureFailed:
        return ui.Text(UiText::ToastScreenCaptureFailed);
    case ScreenCaptureError::None:
    default:
        return {};
    }
}

std::string_view TrimAsciiWhitespace(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(begin, end - begin);
}

bool IsSupportedTimefSpecifier(char specifier) {
    switch (specifier) {
    case 'a':
    case 'A':
    case 'b':
    case 'B':
    case 'c':
    case 'd':
    case 'H':
    case 'I':
    case 'M':
    case 'm':
    case 'p':
    case 'S':
    case 'w':
    case 'x':
    case 'X':
    case 'Y':
    case 'y':
    case '%':
        return true;
    default:
        return false;
    }
}

bool IsAsciiWordBoundary(char ch) {
    return std::isspace(static_cast<unsigned char>(ch)) != 0
        || ch == '(' || ch == ')' || ch == '[' || ch == ']'
        || ch == '{' || ch == '}' || ch == '<' || ch == '>'
        || ch == '=' || ch == '!' || ch == '?' || ch == ':';
}



TimeFormatParseResult ParseTimefFormat(std::string_view rawParam) {
    const std::string_view trimmed = TrimAsciiWhitespace(rawParam);
    if (trimmed.empty() || trimmed.back() != ';') {
        return TimeFormatParseResult{ TimeFormatError::MissingSemicolon };
    }

    const std::string format(trimmed.substr(0, trimmed.size() - 1));
    if (format.empty()) {
        return TimeFormatParseResult{ TimeFormatError::EmptyFormat };
    }

    for (std::size_t i = 0; i < format.size(); ++i) {
        if (format[i] != '%') {
            continue;
        }

        if (i + 1 >= format.size()) {
            return TimeFormatParseResult{ TimeFormatError::InvalidSpecifier, {}, "%" };
        }

        const char specifier = format[i + 1];
        if (!IsSupportedTimefSpecifier(specifier)) {
            return TimeFormatParseResult{
                TimeFormatError::InvalidSpecifier,
                {},
                "%" + std::string(1, specifier),
            };
        }
        ++i;
    }

    return TimeFormatParseResult{ TimeFormatError::None, format };
}

std::string DescribeTimeFormatError(TimeFormatError error, std::string_view detail) {
    UiSettings& ui = UiSettings::Instance();
    switch (error) {
    case TimeFormatError::MissingSemicolon:
        return ui.Text(UiText::ToastTimefMissingSemicolon);
    case TimeFormatError::EmptyFormat:
        return ui.Text(UiText::ToastTimefEmptyFormat);
    case TimeFormatError::InvalidSpecifier:
        return ui.Format(UiText::ToastTimefInvalidSpecifier, std::string(detail).c_str());
    case TimeFormatError::FormatFailed:
        return ui.Text(UiText::ToastTimefFormatFailed);
    case TimeFormatError::None:
    default:
        return {};
    }
}

struct IfAndOrSplitResult {
    bool valid = false;
    std::string_view condition{};
    std::string_view whenTrue{};
    std::string_view whenFalse{};
};

struct ConditionValue {
    enum class Kind {
        Number,
        String,
        Boolean,
    };

    Kind kind = Kind::String;
    double number = 0.0;
    bool boolean = false;
    std::string text{};

    static ConditionValue FromNumber(double value, std::string rawText) {
        ConditionValue result;
        result.kind = Kind::Number;
        result.number = value;
        result.text = std::move(rawText);
        return result;
    }

    static ConditionValue FromString(std::string value) {
        ConditionValue result;
        result.kind = Kind::String;
        result.text = std::move(value);
        return result;
    }

    static ConditionValue FromBoolean(bool value) {
        ConditionValue result;
        result.kind = Kind::Boolean;
        result.boolean = value;
        result.text = value ? "true" : "false";
        return result;
    }
};

std::optional<double> ParseConditionNumber(std::string_view text) {
    const std::string trimmed(TrimAsciiWhitespace(text));
    if (trimmed.empty()) {
        return std::nullopt;
    }

    char* end = nullptr;
    const double value = std::strtod(trimmed.c_str(), &end);
    if (!end || end != trimmed.c_str() + trimmed.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> TryCoerceConditionNumber(const ConditionValue& value) {
    if (value.kind == ConditionValue::Kind::Number) {
        return value.number;
    }
    if (value.kind == ConditionValue::Kind::String) {
        return ParseConditionNumber(value.text);
    }
    return std::nullopt;
}

std::string_view TrimConditionView(std::string_view value) {
    return TrimAsciiWhitespace(value);
}

bool IsConditionWrappedByOuterParentheses(std::string_view text) {
    text = TrimConditionView(text);
    if (text.size() < 2 || text.front() != '(' || text.back() != ')') {
        return false;
    }

    int roundDepth = 0;
    int squareDepth = 0;
    int curlyDepth = 0;
    char quote = '\0';
    bool escaped = false;

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == quote) {
                quote = '\0';
            }
            continue;
        }

        if (ch == '"' || ch == '\'') {
            quote = ch;
            continue;
        }
        if (ch == '(') {
            ++roundDepth;
        } else if (ch == ')' && roundDepth > 0) {
            --roundDepth;
            if (roundDepth == 0 && i + 1 != text.size()) {
                return false;
            }
        } else if (ch == '[') {
            ++squareDepth;
        } else if (ch == ']' && squareDepth > 0) {
            --squareDepth;
        } else if (ch == '{') {
            ++curlyDepth;
        } else if (ch == '}' && curlyDepth > 0) {
            --curlyDepth;
        }
    }

    return roundDepth == 0 && squareDepth == 0 && curlyDepth == 0;
}

std::string_view StripConditionOuterParentheses(std::string_view text) {
    text = TrimConditionView(text);
    while (IsConditionWrappedByOuterParentheses(text)) {
        text = TrimConditionView(text.substr(1, text.size() - 2));
    }
    return text;
}

std::optional<std::size_t> FindTopLevelConditionLogicalWord(std::string_view text, std::string_view word) {
    int roundDepth = 0;
    int squareDepth = 0;
    int curlyDepth = 0;
    char quote = '\0';
    bool escaped = false;

    for (std::size_t i = 0; i + word.size() <= text.size(); ++i) {
        const char ch = text[i];
        if (quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == quote) {
                quote = '\0';
            }
            continue;
        }

        if (ch == '"' || ch == '\'') {
            quote = ch;
            continue;
        }
        if (ch == '(') {
            ++roundDepth;
            continue;
        }
        if (ch == ')' && roundDepth > 0) {
            --roundDepth;
            continue;
        }
        if (ch == '[') {
            ++squareDepth;
            continue;
        }
        if (ch == ']' && squareDepth > 0) {
            --squareDepth;
            continue;
        }
        if (ch == '{') {
            ++curlyDepth;
            continue;
        }
        if (ch == '}' && curlyDepth > 0) {
            --curlyDepth;
            continue;
        }
        if (roundDepth != 0 || squareDepth != 0 || curlyDepth != 0) {
            continue;
        }

        const std::string_view candidate = text.substr(i, word.size());
        if (ToLowerAscii(candidate) != ToLowerAscii(word)) {
            continue;
        }

        const bool leftBoundary = i == 0 || IsAsciiWordBoundary(text[i - 1]);
        const bool rightBoundary = i + word.size() == text.size() || IsAsciiWordBoundary(text[i + word.size()]);
        if (leftBoundary && rightBoundary) {
            return i;
        }
    }

    return std::nullopt;
}

struct ConditionComparisonOp {
    std::size_t pos = 0;
    std::string_view op{};
};

std::optional<ConditionComparisonOp> FindTopLevelConditionComparison(std::string_view text) {
    int roundDepth = 0;
    int squareDepth = 0;
    int curlyDepth = 0;
    char quote = '\0';
    bool escaped = false;

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == quote) {
                quote = '\0';
            }
            continue;
        }

        if (ch == '"' || ch == '\'') {
            quote = ch;
            continue;
        }
        if (ch == '(') {
            ++roundDepth;
            continue;
        }
        if (ch == ')' && roundDepth > 0) {
            --roundDepth;
            continue;
        }
        if (ch == '[') {
            ++squareDepth;
            continue;
        }
        if (ch == ']' && squareDepth > 0) {
            --squareDepth;
            continue;
        }
        if (ch == '{') {
            ++curlyDepth;
            continue;
        }
        if (ch == '}' && curlyDepth > 0) {
            --curlyDepth;
            continue;
        }
        if (roundDepth != 0 || squareDepth != 0 || curlyDepth != 0) {
            continue;
        }

        if ((ch == '=' || ch == '!') && i + 1 < text.size() && text[i + 1] == '=') {
            return ConditionComparisonOp{ i, text.substr(i, 2) };
        }
        if ((ch == '<' || ch == '>') && i + 1 < text.size() && text[i + 1] == '=') {
            return ConditionComparisonOp{ i, text.substr(i, 2) };
        }
        if (ch == '<' || ch == '>') {
            return ConditionComparisonOp{ i, text.substr(i, 1) };
        }
    }

    return std::nullopt;
}

std::string UnquoteConditionText(std::string_view text) {
    const std::string_view trimmed = TrimConditionView(text);
    if (trimmed.size() >= 2
        && ((trimmed.front() == '"' && trimmed.back() == '"')
            || (trimmed.front() == '\'' && trimmed.back() == '\''))) {
        std::string result;
        result.reserve(trimmed.size() - 2);
        bool escaped = false;
        for (std::size_t i = 1; i + 1 < trimmed.size(); ++i) {
            const char ch = trimmed[i];
            if (escaped) {
                result.push_back(ch);
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else {
                result.push_back(ch);
            }
        }
        return result;
    }
    return std::string(trimmed);
}

ConditionValue ParseConditionOperandValue(std::string_view text) {
    const std::string_view stripped = StripConditionOuterParentheses(text);
    const std::string lowered = ToLowerAscii(stripped);
    if (lowered == "true") {
        return ConditionValue::FromBoolean(true);
    }
    if (lowered == "false") {
        return ConditionValue::FromBoolean(false);
    }

    const std::string unquoted = UnquoteConditionText(stripped);
    if (const std::optional<double> number = ParseConditionNumber(unquoted); number.has_value()) {
        return ConditionValue::FromNumber(*number, unquoted);
    }
    return ConditionValue::FromString(unquoted);
}

bool CompareConditionOperandValues(const ConditionValue& lhs, const ConditionValue& rhs, std::string_view op) {
    if (op == "==" || op == "!=") {
        bool result = false;
        if (lhs.kind == ConditionValue::Kind::Boolean && rhs.kind == ConditionValue::Kind::Boolean) {
            result = lhs.boolean == rhs.boolean;
        } else if (const std::optional<double> lhsNumber = TryCoerceConditionNumber(lhs),
                   rhsNumber = TryCoerceConditionNumber(rhs);
                   lhsNumber.has_value() && rhsNumber.has_value()) {
            result = std::fabs(*lhsNumber - *rhsNumber) <= 1e-9;
        } else {
            result = lhs.text == rhs.text;
        }
        return op == "==" ? result : !result;
    }

    if (const std::optional<double> lhsNumber = TryCoerceConditionNumber(lhs),
        rhsNumber = TryCoerceConditionNumber(rhs);
        lhsNumber.has_value() && rhsNumber.has_value()) {
        if (op == ">") {
            return *lhsNumber > *rhsNumber;
        }
        if (op == ">=") {
            return *lhsNumber >= *rhsNumber;
        }
        if (op == "<") {
            return *lhsNumber < *rhsNumber;
        }
        if (op == "<=") {
            return *lhsNumber <= *rhsNumber;
        }
        return false;
    }

    if (op == ">") {
        return lhs.text > rhs.text;
    }
    if (op == ">=") {
        return lhs.text >= rhs.text;
    }
    if (op == "<") {
        return lhs.text < rhs.text;
    }
    if (op == "<=") {
        return lhs.text <= rhs.text;
    }
    return false;
}

std::optional<bool> EvaluateConditionExpression(std::string_view text, std::string& error) {
    text = StripConditionOuterParentheses(text);
    if (text.empty()) {
        error = "условие пустое";
        return std::nullopt;
    }

    const std::string lowered = ToLowerAscii(text);
    if (lowered == "true") {
        return true;
    }
    if (lowered == "false") {
        return false;
    }

    if (const std::optional<std::size_t> orPos = FindTopLevelConditionLogicalWord(text, "or"); orPos.has_value()) {
        const std::optional<bool> lhs = EvaluateConditionExpression(text.substr(0, *orPos), error);
        if (!lhs.has_value()) {
            return std::nullopt;
        }
        const std::optional<bool> rhs = EvaluateConditionExpression(text.substr(*orPos + 2), error);
        if (!rhs.has_value()) {
            return std::nullopt;
        }
        return *lhs || *rhs;
    }

    if (const std::optional<std::size_t> andPos = FindTopLevelConditionLogicalWord(text, "and"); andPos.has_value()) {
        const std::optional<bool> lhs = EvaluateConditionExpression(text.substr(0, *andPos), error);
        if (!lhs.has_value()) {
            return std::nullopt;
        }
        const std::optional<bool> rhs = EvaluateConditionExpression(text.substr(*andPos + 3), error);
        if (!rhs.has_value()) {
            return std::nullopt;
        }
        return *lhs && *rhs;
    }

    if (const std::optional<std::size_t> notPos = FindTopLevelConditionLogicalWord(text, "not");
        notPos.has_value() && *notPos == 0) {
        const std::optional<bool> value = EvaluateConditionExpression(text.substr(3), error);
        if (!value.has_value()) {
            return std::nullopt;
        }
        return !*value;
    }

    if (const std::optional<ConditionComparisonOp> comparison = FindTopLevelConditionComparison(text);
        comparison.has_value()) {
        const ConditionValue lhs = ParseConditionOperandValue(text.substr(0, comparison->pos));
        const ConditionValue rhs =
            ParseConditionOperandValue(text.substr(comparison->pos + comparison->op.size()));
        return CompareConditionOperandValues(lhs, rhs, comparison->op);
    }

    error = "ожидался оператор сравнения";
    return std::nullopt;
}

IfAndOrSplitResult SplitIfAndOrParam(std::string_view raw) {
    std::size_t questionPos = std::string_view::npos;
    std::size_t colonPos = std::string_view::npos;
    int roundDepth = 0;
    int squareDepth = 0;
    int curlyDepth = 0;
    char quote = '\0';
    bool escaped = false;

    for (std::size_t i = 0; i < raw.size(); ++i) {
        const char ch = raw[i];
        if (quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == quote) {
                quote = '\0';
            }
            continue;
        }

        if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '(') {
            ++roundDepth;
        } else if (ch == ')' && roundDepth > 0) {
            --roundDepth;
        } else if (ch == '[') {
            ++squareDepth;
        } else if (ch == ']' && squareDepth > 0) {
            --squareDepth;
        } else if (ch == '{') {
            ++curlyDepth;
        } else if (ch == '}' && curlyDepth > 0) {
            --curlyDepth;
        } else if (ch == '?' && questionPos == std::string_view::npos
                   && roundDepth == 0 && squareDepth == 0 && curlyDepth == 0) {
            questionPos = i;
        } else if (ch == ':' && questionPos != std::string_view::npos && colonPos == std::string_view::npos
                   && roundDepth == 0 && squareDepth == 0 && curlyDepth == 0) {
            colonPos = i;
            break;
        }
    }

    if (questionPos == std::string_view::npos || colonPos == std::string_view::npos) {
        return {};
    }

    return IfAndOrSplitResult{
        true,
        raw.substr(0, questionPos),
        raw.substr(questionPos + 1, colonPos - questionPos - 1),
        raw.substr(colonPos + 1),
    };
}

const std::vector<TagsModule::VirtualKeyPickerEntry>& GetVirtualKeyPickerEntries() {
    static const std::vector<TagsModule::VirtualKeyPickerEntry> entries = [] {
        std::vector<TagsModule::VirtualKeyPickerEntry> built;
        built.reserve(255);
        for (UINT keyCode = 1; keyCode <= 0xFF; ++keyCode) {
            const std::string name = hotkeys::KeyName(keyCode);
            const std::string label = std::to_string(keyCode) + " - " + name;
            built.push_back(TagsModule::VirtualKeyPickerEntry{
                keyCode,
                label,
                ToLowerAscii(label + " " + name),
            });
        }
        return built;
    }();
    return entries;
}

bool IsExtendedVirtualKey(UINT keyCode) {
    switch (keyCode) {
    case VK_RMENU:
    case VK_RCONTROL:
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_NUMLOCK:
    case VK_DIVIDE:
    case VK_CANCEL:
    case VK_SNAPSHOT:
        return true;
    default:
        return false;
    }
}

bool SendKeyboardEvent(UINT keyCode, bool keyUp) {
    INPUT input{};
    const WORD virtualKey = static_cast<WORD>(keyCode);
    const WORD scanCode = static_cast<WORD>(MapVirtualKeyW(keyCode, MAPVK_VK_TO_VSC));
    const DWORD extendedFlag = IsExtendedVirtualKey(keyCode) ? KEYEVENTF_EXTENDEDKEY : 0;

    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtualKey;
    input.ki.wScan = scanCode;
    input.ki.dwFlags = extendedFlag | (keyUp ? KEYEVENTF_KEYUP : 0);

    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

bool SendMouseEvent(DWORD flag, DWORD mouseData = 0) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flag;
    input.mi.mouseData = mouseData;
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

bool SendVirtualKeyEvent(UINT keyCode, bool keyUp) {
    if (keyCode == 0 || keyCode > 0xFF) {
        return false;
    }

    switch (keyCode) {
    case VK_LBUTTON:
        return SendMouseEvent(keyUp ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_LEFTDOWN);
    case VK_RBUTTON:
        return SendMouseEvent(keyUp ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_RIGHTDOWN);
    case VK_MBUTTON:
        return SendMouseEvent(keyUp ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_MIDDLEDOWN);
    case VK_XBUTTON1:
        return SendMouseEvent(keyUp ? MOUSEEVENTF_XUP : MOUSEEVENTF_XDOWN, XBUTTON1);
    case VK_XBUTTON2:
        return SendMouseEvent(keyUp ? MOUSEEVENTF_XUP : MOUSEEVENTF_XDOWN, XBUTTON2);
    default:
        return SendKeyboardEvent(keyCode, keyUp);
    }
}

class MathExpressionParser {
public:
    explicit MathExpressionParser(std::string_view expression)
        : expression_(expression) {}

    std::optional<double> Evaluate() {
        const std::optional<double> value = ParseExpression();
        SkipWhitespace();
        if (!value.has_value() || pos_ != expression_.size()) {
            return std::nullopt;
        }
        return value;
    }

private:
    std::optional<double> ParseExpression() {
        std::optional<double> value = ParseTerm();
        if (!value.has_value()) {
            return std::nullopt;
        }

        while (true) {
            SkipWhitespace();
            if (Match('+')) {
                const std::optional<double> rhs = ParseTerm();
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                *value += *rhs;
            } else if (Match('-')) {
                const std::optional<double> rhs = ParseTerm();
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                *value -= *rhs;
            } else {
                return value;
            }
        }
    }

    std::optional<double> ParseTerm() {
        std::optional<double> value = ParseFactor();
        if (!value.has_value()) {
            return std::nullopt;
        }

        while (true) {
            SkipWhitespace();
            if (Match('*')) {
                const std::optional<double> rhs = ParseFactor();
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                *value *= *rhs;
            } else if (Match('/')) {
                const std::optional<double> rhs = ParseFactor();
                if (!rhs.has_value() || std::fabs(*rhs) < 1e-12) {
                    return std::nullopt;
                }
                *value /= *rhs;
            } else if (Match('%')) {
                const std::optional<double> rhs = ParseFactor();
                if (!rhs.has_value() || std::fabs(*rhs) < 1e-12) {
                    return std::nullopt;
                }
                *value = std::fmod(*value, *rhs);
            } else {
                return value;
            }
        }
    }

    std::optional<double> ParseFactor() {
        SkipWhitespace();
        if (Match('+')) {
            return ParseFactor();
        }
        if (Match('-')) {
            if (std::optional<double> value = ParseFactor(); value.has_value()) {
                return -*value;
            }
            return std::nullopt;
        }
        if (Match('(')) {
            std::optional<double> value = ParseExpression();
            SkipWhitespace();
            if (!value.has_value() || !Match(')')) {
                return std::nullopt;
            }
            return value;
        }
        return ParseNumber();
    }

    std::optional<double> ParseNumber() {
        SkipWhitespace();
        const std::size_t start = pos_;

        bool hasDigits = false;
        while (pos_ < expression_.size() && std::isdigit(static_cast<unsigned char>(expression_[pos_])) != 0) {
            hasDigits = true;
            ++pos_;
        }

        if (pos_ < expression_.size() && expression_[pos_] == '.') {
            ++pos_;
            while (pos_ < expression_.size() && std::isdigit(static_cast<unsigned char>(expression_[pos_])) != 0) {
                hasDigits = true;
                ++pos_;
            }
        }

        if (!hasDigits) {
            pos_ = start;
            return std::nullopt;
        }

        if (pos_ < expression_.size() && (expression_[pos_] == 'e' || expression_[pos_] == 'E')) {
            const std::size_t exponentPos = pos_++;
            if (pos_ < expression_.size() && (expression_[pos_] == '+' || expression_[pos_] == '-')) {
                ++pos_;
            }

            const std::size_t digitsStart = pos_;
            while (pos_ < expression_.size() && std::isdigit(static_cast<unsigned char>(expression_[pos_])) != 0) {
                ++pos_;
            }
            if (digitsStart == pos_) {
                pos_ = exponentPos;
            }
        }

        const std::string token(expression_.substr(start, pos_ - start));
        char* endPtr = nullptr;
        const double value = std::strtod(token.c_str(), &endPtr);
        if (!endPtr || endPtr != token.c_str() + token.size()) {
            return std::nullopt;
        }
        return value;
    }

    void SkipWhitespace() {
        while (pos_ < expression_.size() && std::isspace(static_cast<unsigned char>(expression_[pos_])) != 0) {
            ++pos_;
        }
    }

    bool Match(char expected) {
        if (pos_ >= expression_.size() || expression_[pos_] != expected) {
            return false;
        }
        ++pos_;
        return true;
    }

    std::string_view expression_{};
    std::size_t pos_ = 0;
};

std::string FormatMathResult(double value) {
    if (!std::isfinite(value)) {
        return {};
    }

    const double rounded = std::round(value);
    if (std::fabs(value - rounded) < 1e-9
        && rounded >= static_cast<double>(std::numeric_limits<long long>::min())
        && rounded <= static_cast<double>(std::numeric_limits<long long>::max())) {
        return std::to_string(static_cast<long long>(rounded));
    }

    std::ostringstream stream;
    stream << std::setprecision(12) << std::defaultfloat << value;
    std::string result = stream.str();
    if (result.find_first_of("eE") == std::string::npos) {
        if (const std::size_t dotPos = result.find('.'); dotPos != std::string::npos) {
            while (!result.empty() && result.back() == '0') {
                result.pop_back();
            }
            if (!result.empty() && result.back() == '.') {
                result.pop_back();
            }
        }
    }
    return result;
}

std::string FormatNumberWithDots(std::string_view rawValue) {
    const std::string value(rawValue);
    if (value.empty()) {
        return value;
    }

    std::size_t pos = 0;
    std::string sign;
    if (value[pos] == '+' || value[pos] == '-') {
        sign.push_back(value[pos]);
        ++pos;
        if (pos >= value.size()) {
            return value;
        }
    }

    const std::size_t integerStart = pos;
    while (pos < value.size() && std::isdigit(static_cast<unsigned char>(value[pos])) != 0) {
        ++pos;
    }

    if (pos == integerStart) {
        return value;
    }

    std::string fractionalPart;
    if (pos < value.size()) {
        if (value[pos] != '.') {
            return value;
        }

        const std::size_t fractionStart = pos + 1;
        std::size_t fractionPos = fractionStart;
        while (fractionPos < value.size() && std::isdigit(static_cast<unsigned char>(value[fractionPos])) != 0) {
            ++fractionPos;
        }
        if (fractionPos != value.size()) {
            return value;
        }

        fractionalPart.assign(value.substr(pos));
    }

    const std::string integerPart = value.substr(integerStart, pos - integerStart);
    std::string formattedInteger;
    formattedInteger.reserve(integerPart.size() + (integerPart.size() / 3));

    for (std::size_t i = 0; i < integerPart.size(); ++i) {
        formattedInteger.push_back(integerPart[i]);
        const std::size_t remaining = integerPart.size() - i - 1;
        if (remaining != 0 && remaining % 3 == 0) {
            formattedInteger.push_back('.');
        }
    }

    return sign + formattedInteger + fractionalPart;
}

std::string GetVehicleTypeName(int modelId) {
    if (CModelInfo::IsBoatModel(modelId)) {
        return "Boat";
    }
    if (CModelInfo::IsCarModel(modelId)) {
        return "Car";
    }
    if (CModelInfo::IsTrainModel(modelId)) {
        return "Train";
    }
    if (CModelInfo::IsHeliModel(modelId)) {
        return "Heli";
    }
    if (CModelInfo::IsPlaneModel(modelId)) {
        return "Plane";
    }
    if (CModelInfo::IsBikeModel(modelId)) {
        return "Bike";
    }
    if (CModelInfo::IsFakePlaneModel(modelId)) {
        return "FakePlane";
    }
    if (CModelInfo::IsMonsterTruckModel(modelId)) {
        return "MonsterTruck";
    }
    if (CModelInfo::IsQuadBikeModel(modelId)) {
        return "QuadBike";
    }
    if (CModelInfo::IsBmxModel(modelId)) {
        return "Bicycle";
    }
    if (CModelInfo::IsTrailerModel(modelId)) {
        return "Trailer";
    }
    return "unknown";
}

std::string GetWeaponDisplayName(eWeaponType weaponType) {
    switch (weaponType) {
    case WEAPONTYPE_UNARMED: return "Fist";
    case WEAPONTYPE_BRASSKNUCKLE: return "Brass Knuckles";
    case WEAPONTYPE_GOLFCLUB: return "Golf Club";
    case WEAPONTYPE_NIGHTSTICK: return "Nightstick";
    case WEAPONTYPE_KNIFE: return "Knife";
    case WEAPONTYPE_BASEBALLBAT: return "Baseball Bat";
    case WEAPONTYPE_SHOVEL: return "Shovel";
    case WEAPONTYPE_POOLCUE: return "Pool Cue";
    case WEAPONTYPE_KATANA: return "Katana";
    case WEAPONTYPE_CHAINSAW: return "Chainsaw";
    case WEAPONTYPE_DILDO1: return "Dildo";
    case WEAPONTYPE_DILDO2: return "Dildo";
    case WEAPONTYPE_VIBE1: return "Vibrator";
    case WEAPONTYPE_VIBE2: return "Vibrator";
    case WEAPONTYPE_FLOWERS: return "Flowers";
    case WEAPONTYPE_CANE: return "Cane";
    case WEAPONTYPE_GRENADE: return "Grenade";
    case WEAPONTYPE_TEARGAS: return "Tear Gas";
    case WEAPONTYPE_MOLOTOV: return "Molotov";
    case WEAPONTYPE_ROCKET: return "Rocket";
    case WEAPONTYPE_ROCKET_HS: return "Rocket HS";
    case WEAPONTYPE_FREEFALL_BOMB: return "Freefall Bomb";
    case WEAPONTYPE_PISTOL: return "Pistol";
    case WEAPONTYPE_PISTOL_SILENCED: return "Silenced Pistol";
    case WEAPONTYPE_DESERT_EAGLE: return "Desert Eagle";
    case WEAPONTYPE_SHOTGUN: return "Shotgun";
    case WEAPONTYPE_SAWNOFF: return "Sawn-off";
    case WEAPONTYPE_SPAS12: return "SPAS-12";
    case WEAPONTYPE_MICRO_UZI: return "Micro Uzi";
    case WEAPONTYPE_MP5: return "MP5";
    case WEAPONTYPE_AK47: return "AK-47";
    case WEAPONTYPE_M4: return "M4";
    case WEAPONTYPE_TEC9: return "Tec-9";
    case WEAPONTYPE_COUNTRYRIFLE: return "Rifle";
    case WEAPONTYPE_SNIPERRIFLE: return "Sniper Rifle";
    case WEAPONTYPE_RLAUNCHER: return "Rocket Launcher";
    case WEAPONTYPE_RLAUNCHER_HS: return "Heat Seeker";
    case WEAPONTYPE_FTHROWER: return "Flamethrower";
    case WEAPONTYPE_MINIGUN: return "Minigun";
    case WEAPONTYPE_SATCHEL_CHARGE: return "Satchel";
    case WEAPONTYPE_DETONATOR: return "Detonator";
    case WEAPONTYPE_SPRAYCAN: return "Spray Can";
    case WEAPONTYPE_EXTINGUISHER: return "Extinguisher";
    case WEAPONTYPE_CAMERA: return "Camera";
    case WEAPONTYPE_NIGHTVISION: return "Night Vision";
    case WEAPONTYPE_INFRARED: return "Thermal Vision";
    case WEAPONTYPE_PARACHUTE: return "Parachute";
    default: return "Unknown";
    }
}

std::string FormatSampColorTag(std::uint32_t color) {
    // SA:MP player colors are stored as 0xRRGGBBAA; Helper markup needs {RRGGBB}.
    const unsigned int red = (color >> 24) & 0xFFu;
    const unsigned int green = (color >> 16) & 0xFFu;
    const unsigned int blue = (color >> 8) & 0xFFu;
    char buffer[10]{};
    std::snprintf(buffer, sizeof(buffer), "{%02X%02X%02X}", red, green, blue);
    return buffer;
}

const CWeapon* FindLocalWeapon() {
    CPed* const playerPed = FindPlayerPed();
    if (!playerPed || playerPed->m_nSelectedWepSlot >= std::size(playerPed->m_aWeapons)) {
        return nullptr;
    }
    return playerPed->GetWeapon();
}

const CPed* FindPlayerPedBySampId(SampApi& sampApi, int id) {
    if (id < 0) {
        return nullptr;
    }

    if (const void* resolvedPed = sampApi.GetPlayerPedPointer(id)) {
        return reinterpret_cast<const CPed*>(resolvedPed);
    }

    const int localId = sampApi.Local_ID();
    if (localId >= 0 && id == localId) {
        return FindPlayerPed();
    }

    return nullptr;
}

bool IsVehiclePointerValid(const CVehicle* vehicle) {
    if (!vehicle) {
        return false;
    }

    auto* const vehiclePool = CPools::ms_pVehiclePool;
    return vehiclePool != nullptr && vehiclePool->IsObjectValid(const_cast<CVehicle*>(vehicle));
}

std::optional<std::string> ResolveVehicleTypeForPed(const CPed* ped) {
    if (!ped || !IsVehiclePointerValid(ped->m_pVehicle)) {
        return std::string();
    }

    return GetVehicleTypeName(ped->m_pVehicle->m_nModelIndex);
}

std::string StripDialogColorCodes(std::string_view text) {
    std::string cleaned;
    cleaned.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        std::size_t consumed = 0;
        if (TryParseSampColorTag(text, i, consumed)) {
            i += consumed;
            continue;
        }
        cleaned.push_back(text[i]);
        ++i;
    }
    return cleaned;
}

std::string NormalizeDialogCaptionVisibleText(std::string_view text) {
    return TrimAscii(StripDialogColorCodes(text));
}

std::string NormalizeDialogVisibleText(std::string_view text) {
    return TrimAscii(StripDialogColorCodes(text));
}

std::string NormalizeDialogRawText(std::string_view rawText) {
    std::string text = StripDialogColorCodes(rawText);
    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            normalized.push_back('\n');
        } else {
            normalized.push_back(ch);
        }
    }
    return normalized;
}

std::vector<std::string> SplitDialogLineTokens(std::string_view line) {
    std::string text;
    text.reserve(line.size() * 2);
    for (const char ch : line) {
        if (ch == '\t') {
            text.push_back(' ');
            continue;
        }
        if (ch == '[' || ch == ']' || ch == '(' || ch == ')' || ch == '{' || ch == '}') {
            text.push_back(' ');
            text.push_back(ch);
            text.push_back(' ');
            continue;
        }
        text.push_back(ch);
    }

    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
            ++pos;
        }
        if (pos >= text.size()) {
            break;
        }

        std::size_t end = pos;
        while (end < text.size() && std::isspace(static_cast<unsigned char>(text[end])) == 0) {
            ++end;
        }
        out.emplace_back(text.substr(pos, end - pos));
        pos = end;
    }

    return out;
}

DialogTextItems CollectDialogTextItems(std::string_view rawText) {
    DialogTextItems result;
    const std::string normalized = NormalizeDialogRawText(rawText);
    std::size_t lineStart = 0;
    int nextIndex = 0;
    while (lineStart <= normalized.size()) {
        const std::size_t lineEnd = normalized.find('\n', lineStart);
        const std::size_t count = lineEnd == std::string::npos ? normalized.size() - lineStart : lineEnd - lineStart;
        const std::string_view line(normalized.data() + lineStart, count);
        const std::vector<std::string> tokens = SplitDialogLineTokens(line);
        if (!tokens.empty()) {
            std::vector<DialogTextToken> row;
            row.reserve(tokens.size());
            for (const std::string& token : tokens) {
                DialogTextToken item{ nextIndex++, token };
                result.flat.push_back(item);
                row.push_back(std::move(item));
            }
            result.rows.push_back(std::move(row));
        }

        if (lineEnd == std::string::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }
    return result;
}

std::string NormalizeDialogListItemText(std::string_view rawText) {
    const std::string stripped = StripDialogColorCodes(rawText);
    std::string normalized;
    normalized.reserve(stripped.size() + 8);
    for (const char ch : stripped) {
        if (ch == '\t') {
            normalized += " | ";
        } else if (ch != '\r' && ch != '\n') {
            normalized.push_back(ch);
        }
    }
    return TrimAscii(normalized);
}

std::string ToLowerUtf8(std::string_view value) {
    if (value.empty()) {
        return {};
    }

    constexpr unsigned int kCp1251 = 1251;
    unsigned int outputCodePage = CP_UTF8;
    std::wstring wide = Utf8ToWide(value);
    if (wide.empty()) {
        wide = MultiByteToWide(value, kCp1251);
        outputCodePage = kCp1251;
    }

    if (wide.empty()) {
        return std::string(value);
    }

    const int mappedLength = ::LCMapStringEx(
        LOCALE_NAME_INVARIANT,
        LCMAP_LOWERCASE,
        wide.data(),
        static_cast<int>(wide.size()),
        nullptr,
        0,
        nullptr,
        nullptr,
        0);
    bool mappedSuccessfully = false;
    if (mappedLength > 0) {
        std::wstring mapped(static_cast<std::size_t>(mappedLength), L'\0');
        const int written = ::LCMapStringEx(
            LOCALE_NAME_INVARIANT,
            LCMAP_LOWERCASE,
            wide.data(),
            static_cast<int>(wide.size()),
            mapped.data(),
            mappedLength,
            nullptr,
            nullptr,
            0);
        if (written > 0) {
            mapped.resize(static_cast<std::size_t>(written));
            wide = std::move(mapped);
            mappedSuccessfully = true;
        }
    }
    if (!mappedSuccessfully) {
        ::CharLowerBuffW(wide.data(), static_cast<DWORD>(wide.size()));
    }

    const std::string lowered = WideToMultiByte(wide, outputCodePage);
    return lowered.empty() ? std::string(value) : lowered;
}

std::string NormalizeDialogItemSearchText(std::string_view rawText) {
    std::string text = StripDialogColorCodes(rawText);
    for (char& ch : text) {
        if (ch == '\t' || ch == '\r' || ch == '\n') {
            ch = ' ';
        }
    }

    text = TrimAscii(text);
    if (!text.empty() && text.front() == '[') {
        const std::size_t closing = text.find(']');
        if (closing != std::string::npos) {
            text = TrimAscii(text.substr(closing + 1));
        }
    }
    return text;
}

std::vector<std::string> BuildDialogItemSearchVariants(std::string_view rawText) {
    std::vector<std::string> out;
    auto pushUnique = [&out](std::string value) {
        if (value.empty()) {
            return;
        }
        if (std::find(out.begin(), out.end(), value) == out.end()) {
            out.push_back(std::move(value));
        }
    };

    const std::string raw(rawText);
    const std::string normalized = NormalizeDialogItemSearchText(rawText);
    pushUnique(raw);
    pushUnique(ToLowerUtf8(raw));
    pushUnique(normalized);
    pushUnique(ToLowerUtf8(normalized));
    return out;
}

int GetDialogItemHeaderLinesToSkipForStyle(int style) {
    return style == SampApi::DIALOG_STYLE_TABLIST_HEADERS ? 1 : 0;
}

int GetDialogItemHeaderLinesToSkip(SampApi* sampApi) {
    if (!sampApi || !sampApi->isDialogActive()) {
        return 0;
    }
    return GetDialogItemHeaderLinesToSkipForStyle(sampApi->GetCurrentDialogStyle());
}

DialogListItems CollectDialogListItems(std::string_view rawText, int headerLinesToSkip) {
    DialogListItems result;
    const std::string normalized = NormalizeDialogRawText(rawText);
    const int skip = std::max(headerLinesToSkip, 0);
    std::size_t lineStart = 0;
    int rawLineIndex = 0;
    std::vector<std::string> headerLines;
    while (lineStart <= normalized.size()) {
        const std::size_t lineEnd = normalized.find('\n', lineStart);
        const std::size_t count = lineEnd == std::string::npos ? normalized.size() - lineStart : lineEnd - lineStart;
        const std::string_view line(normalized.data() + lineStart, count);
        if (rawLineIndex < skip) {
            const std::string header = NormalizeDialogListItemText(line);
            if (!header.empty()) {
                headerLines.push_back(header);
            }
        } else {
            result.items.push_back(DialogListItemInfo{
                rawLineIndex - skip,
                rawLineIndex - skip + 1,
                NormalizeDialogListItemText(line),
                std::string(line),
            });
        }

        ++rawLineIndex;
        if (lineEnd == std::string::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }

    for (std::size_t i = 0; i < headerLines.size(); ++i) {
        if (i != 0) {
            result.headerText += " | ";
        }
        result.headerText += headerLines[i];
    }

    return result;
}

DialogListItems CollectDialogListItemsFromTexts(const std::vector<std::string>& rawItems) {
    DialogListItems result;
    result.items.reserve(rawItems.size());
    for (std::size_t i = 0; i < rawItems.size(); ++i) {
        result.items.push_back(DialogListItemInfo{
            static_cast<int>(i),
            static_cast<int>(i + 1),
            NormalizeDialogListItemText(rawItems[i]),
            rawItems[i],
        });
    }
    return result;
}

std::optional<DialogListItems> ParseDialogListItemsJson(std::string_view rawJson) {
    if (TrimAscii(rawJson).empty()) {
        return std::nullopt;
    }

    std::string error;
    const std::optional<jsonutil::JsonValue> parsed = jsonutil::ParseJson(rawJson, error);
    if (!parsed.has_value()) {
        return std::nullopt;
    }

    const jsonutil::JsonArray* array = parsed->TryArray();
    if (!array) {
        return std::nullopt;
    }

    std::vector<std::string> rawItems;
    rawItems.reserve(array->size());
    for (const jsonutil::JsonValue& value : *array) {
        if (const std::string* text = value.TryString()) {
            rawItems.push_back(*text);
        }
    }

    return CollectDialogListItemsFromTexts(rawItems);
}

std::optional<DialogListItems> ReadActiveDialogListItems(SampApi* sampApi, std::string& error) {
    error.clear();
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        error = "no_samp";
        return std::nullopt;
    }
    if (!sampApi->isDialogActive()) {
        error = "no_dialog";
        return std::nullopt;
    }
    if (!sampApi->isDialogListStyle(sampApi->GetCurrentDialogStyle())) {
        error = "not_list";
        return std::nullopt;
    }

    const std::string dialogText = sampApi->sampGetDialogText();
    if (dialogText.empty()) {
        error = "read_fail";
        return std::nullopt;
    }

    DialogListItems items = CollectDialogListItems(dialogText, GetDialogItemHeaderLinesToSkip(sampApi));
    const int count = sampApi->GetCurrentDialogListboxItemsCount();
    if (count >= 0 && count < static_cast<int>(items.items.size())) {
        items.items.resize(static_cast<std::size_t>(count));
    }
    return items;
}

std::optional<DialogTextItems> ReadActiveDialogTextItems(SampApi* sampApi, std::string& error) {
    error.clear();
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        error = "no_samp";
        return std::nullopt;
    }
    if (!sampApi->isDialogActive()) {
        error = "no_dialog";
        return std::nullopt;
    }

    const std::string dialogText = sampApi->sampGetDialogText();
    if (dialogText.empty()) {
        error = "read_fail";
        return std::nullopt;
    }

    return CollectDialogTextItems(dialogText);
}

std::optional<int> FindDialogItemIndexByText(const DialogListItems& items, std::string_view query) {
    const std::vector<std::string> needles = BuildDialogItemSearchVariants(query);
    if (needles.empty()) {
        return std::nullopt;
    }

    for (const DialogListItemInfo& item : items.items) {
        const std::vector<std::string> haystacks = BuildDialogItemSearchVariants(item.rawText);
        for (const std::string& haystack : haystacks) {
            for (const std::string& needle : needles) {
                if (haystack.find(needle) != std::string::npos) {
                    return item.index0;
                }
            }
        }
    }

    return std::nullopt;
}

std::wstring SanitizeFileStem(std::wstring stem) {
    constexpr std::wstring_view invalidChars = L"<>:\"/\\|?*";
    for (wchar_t& ch : stem) {
        if (invalidChars.find(ch) != std::wstring_view::npos || ch < 32) {
            ch = L'_';
        }
    }

    while (!stem.empty() && (stem.back() == L' ' || stem.back() == L'.')) {
        stem.pop_back();
    }
    while (!stem.empty() && (stem.front() == L' ' || stem.front() == L'.')) {
        stem.erase(stem.begin());
    }

    return stem;
}

std::filesystem::path MakeUniqueTextFilePath(const std::filesystem::path& directory, std::wstring stem) {
    stem = SanitizeFileStem(std::move(stem));
    if (stem.empty()) {
        stem = L"dialog";
    }

    std::filesystem::path candidate = directory / (stem + L".txt");
    int suffix = 1;
    while (std::filesystem::exists(candidate)) {
        candidate = directory / (stem + L" (" + std::to_wstring(suffix++) + L").txt");
    }
    return candidate;
}

std::string DialogStyleName(int style) {
    switch (style) {
    case SampApi::DIALOG_STYLE_MSGBOX:
        return "MSGBOX";
    case SampApi::DIALOG_STYLE_INPUT:
        return "INPUT";
    case SampApi::DIALOG_STYLE_LIST:
        return "LIST";
    case SampApi::DIALOG_STYLE_PASSWORD:
        return "PASSWORD";
    case SampApi::DIALOG_STYLE_TABLIST:
        return "TABLIST";
    case SampApi::DIALOG_STYLE_TABLIST_HEADERS:
        return "TABLIST_HEADERS";
    default:
        return "UNKNOWN";
    }
}
} // namespace
