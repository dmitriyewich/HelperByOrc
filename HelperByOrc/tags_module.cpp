#include "tags_module.h"

#include "app_config.h"
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
#include <game_sa/CVehicle.h>
#include <game_sa/CVehicleModelInfo.h>
#include <extensions/ScriptCommands.h>
#include <game_sa/eScriptCommands.h>
#include <game_sa/CPools.h>
#include <game_sa/common.h>
#include <RenderWare.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <random>
#include <vector>

namespace {

constexpr std::string_view kTagsSectionName = "tags";
constexpr std::string_view kCustomVarsKey = "custom_vars";
constexpr int kRecursionLimit = 10;
constexpr int kKeyEmulateStartDelayMs = 20;
constexpr int kKeyEmulateTapMs = 35;
constexpr float kClosestScreenTargetZOffset = 0.9f;
constexpr unsigned int kAnsiCodePage = CP_ACP;
constexpr std::uintptr_t kTakeScreenshotAddress = 0x5D0820;
constexpr wchar_t kHelperScreensRelativePath[] = L"screens";
constexpr wchar_t kHelperSavedDialogsRelativePath[] = L"saved\\dialogs";
constexpr std::uint64_t kDialogWaitOpenTimeoutMs = 3000;
constexpr int kRandomMinInt = -2147483647;
constexpr int kRandomMaxInt = 2147483647;
thread_local std::vector<TagsModule::OwnedEvaluationContext> g_activeContextStack;

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

const char* TagKindLabel(TagsModule::TagKind kind, UiSettings& ui) {
    return ui.Text(kind == TagsModule::TagKind::Simple ? UiText::TagsKindSimple : UiText::TagsKindFunction);
}

void DrawStatBlock(const char* label, const std::string& value, const ImVec4& accent) {
    ImGui::BeginGroup();
    ImGui::TextColored(accent, "%s", value.c_str());
    ImGui::TextDisabled("%s", label);
    ImGui::EndGroup();
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

bool SelectableCopyToken(const std::string& label, const std::string& token, std::string& searchQuery) {
    if (!ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
        return false;
    }

    ImGui::SetClipboardText(token.c_str());
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
    TokenFn tokenFn) {
    bool hasMatches = false;
    if (ImGui::BeginChild(childId, ScaleUi(0.0f, 360.0f), ImGuiChildFlags_Borders)) {
        for (const auto& item : items) {
            if (!filter.empty() && searchFn(item).find(filter) == std::string::npos) {
                continue;
            }

            hasMatches = true;
            if (SelectableCopyToken(labelFn(item), tokenFn(item), searchQuery)) {
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

std::string TrimAscii(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
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

std::mt19937& TagRandomEngine() {
    static std::mt19937 rng(std::random_device{}());
    return rng;
}

std::optional<std::int64_t> ParseTimeOffsetSeconds(std::string_view rawParam) {
    const std::vector<std::string_view> parts = SplitTopLevelDelimitedParts(rawParam, ':');
    if (parts.size() != 2 && parts.size() != 3) {
        return std::nullopt;
    }

    std::int64_t values[3]{ 0, 0, 0 };
    const std::size_t offset = parts.size() == 2 ? 1 : 0;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const std::string trimmed = TrimAscii(parts[i]);
        if (trimmed.empty()) {
            return std::nullopt;
        }

        std::int64_t parsed = 0;
        for (const unsigned char ch : trimmed) {
            if (std::isdigit(ch) == 0) {
                return std::nullopt;
            }
            parsed = parsed * 10 + static_cast<std::int64_t>(ch - '0');
        }

        values[offset + i] = parsed;
    }

    return values[0] * 3600 + values[1] * 60 + values[2];
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

    const std::string formatString(format);
    std::size_t bufferSize = std::max<std::size_t>(128, formatString.size() * 8 + 32);
    for (int attempt = 0; attempt < 6; ++attempt) {
        std::string buffer(bufferSize, '\0');
        const std::size_t written = std::strftime(buffer.data(), buffer.size(), formatString.c_str(), &localTime);
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
    const unsigned int red = (color >> 16) & 0xFFu;
    const unsigned int green = (color >> 8) & 0xFFu;
    const unsigned int blue = color & 0xFFu;
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

    auto* const pedPool = CPools::ms_pPedPool;
    if (!pedPool || pedPool->m_nSize <= 0) {
        return nullptr;
    }

    for (int index = 0; index < pedPool->m_nSize; ++index) {
        CPed* const candidatePed = pedPool->GetAt(index);
        if (!candidatePed) {
            continue;
        }

        const auto [matched, matchedId] = sampApi.getPedID(candidatePed);
        if (matched && matchedId == id) {
            return candidatePed;
        }
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

std::optional<std::string> ResolveVehicleNameForPed(const CPed* ped) {
    if (!ped || !IsVehiclePointerValid(ped->m_pVehicle)) {
        return std::string();
    }

    const int modelId = ped->m_pVehicle->m_nModelIndex;
    if (CModelInfo::IsVehicleModelType(modelId) >= 0) {
        if (auto* modelInfo = static_cast<CVehicleModelInfo*>(CModelInfo::GetModelInfo(modelId))) {
            if (modelInfo->m_szGameName[0] != '\0') {
                return std::string(modelInfo->m_szGameName);
            }
        }
    }
    return GetVehicleTypeName(modelId);
}

} // namespace

TagsModule::TagsModule() = default;

const std::vector<TagsModule::CatalogEntry>& TagsModule::CatalogEntries() const {
    return catalogEntries_;
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

    unsigned int outputCodePage = CP_UTF8;
    std::wstring wide = Utf8ToWide(value);
    if (wide.empty()) {
        wide = MultiByteToWide(value, CP_ACP);
        outputCodePage = CP_ACP;
    }

    if (wide.empty()) {
        return ToLowerAscii(value);
    }

    for (wchar_t& ch : wide) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }

    const std::string lowered = WideToMultiByte(wide, outputCodePage);
    return lowered.empty() ? ToLowerAscii(value) : lowered;
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

int GetDialogItemHeaderLinesToSkip(SampApi* sampApi) {
    if (!sampApi || !sampApi->isDialogActive()) {
        return 0;
    }
    return sampApi->GetCurrentDialogStyle() == SampApi::DIALOG_STYLE_TABLIST_HEADERS ? 1 : 0;
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

const std::vector<TagsModule::VirtualKeyPickerEntry>& TagsModule::VirtualKeyPickerEntries() const {
    return GetVirtualKeyPickerEntries();
}

std::string TagsModule::MakeKeyEmulateToken(unsigned int keyCode) {
    return MakeKeyEmulateTokenImpl(static_cast<UINT>(keyCode));
}

void TagsModule::TagRegistry::Clear() {
    entries_.clear();
}

void TagsModule::TagRegistry::RegisterSimple(
    std::string name,
    std::string token,
    std::string example,
    UiText descriptionText,
    TagEntry::SimpleResolver resolver) {
    entries_.push_back(TagEntry{
        TagKind::Simple,
        std::move(name),
        std::move(token),
        std::move(example),
        descriptionText,
        std::move(resolver),
        {},
    });
}

void TagsModule::TagRegistry::RegisterFunction(
    std::string name,
    std::string token,
    std::string example,
    UiText descriptionText,
    TagEntry::FunctionResolver resolver) {
    entries_.push_back(TagEntry{
        TagKind::Function,
        std::move(name),
        std::move(token),
        std::move(example),
        descriptionText,
        {},
        std::move(resolver),
    });
}

const std::vector<TagsModule::TagEntry>& TagsModule::TagRegistry::Entries() const {
    return entries_;
}

const TagsModule::TagEntry* TagsModule::TagRegistry::Find(TagKind kind, std::string_view name) const {
    const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const TagEntry& entry) {
        return entry.kind == kind && entry.name == name;
    });
    return it == entries_.end() ? nullptr : &(*it);
}

const TagsModule::TagEntry* TagsModule::TagRegistry::FindByIndex(int index) const {
    if (index < 0 || index >= static_cast<int>(entries_.size())) {
        return nullptr;
    }
    return &entries_[static_cast<std::size_t>(index)];
}

std::size_t TagsModule::TagRegistry::Count(TagKind kind) const {
    return static_cast<std::size_t>(std::count_if(entries_.begin(), entries_.end(), [&](const TagEntry& entry) {
        return entry.kind == kind;
    }));
}

void TagsModule::RefreshCatalogEntries() {
    catalogEntries_.clear();
    catalogEntries_.reserve(tagRegistry_.Entries().size());
    for (const TagEntry& entry : tagRegistry_.Entries()) {
        catalogEntries_.push_back(CatalogEntry{
            entry.kind,
            entry.name,
            entry.token,
            entry.example,
            entry.descriptionText,
        });
    }
}

void TagsModule::InitializeRegistry() {
    tagRegistry_.Clear();

    tagRegistry_.RegisterSimple(
        "id",
        "{id}",
        "{id}",
        UiText::TagsBuiltinIdDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinIdTag(context);
        });

    tagRegistry_.RegisterSimple(
        "nick",
        "{nick}",
        "{nick}",
        UiText::TagsBuiltinNickDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinNickTag(context);
        });

    tagRegistry_.RegisterSimple(
        "thisbind",
        "{thisbind}",
        "{thisbind}",
        UiText::TagsBuiltinThisbindDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinThisbindTag(context);
        });

    tagRegistry_.RegisterSimple(
        "thiscategory",
        "{thiscategory}",
        "{thiscategory}",
        UiText::TagsBuiltinThiscategoryDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinThiscategoryTag(context);
        });

    tagRegistry_.RegisterFunction(
        "thiscategory",
        "[thiscategory]",
        "[thiscategory]",
        UiText::TagsBuiltinThiscategoryDescription,
        [](const TagsModule& module, std::string_view, const EvaluationContext& context, int) {
            return module.ResolveBuiltinThiscategoryTag(context);
        });

    tagRegistry_.RegisterSimple(
        "bindstopall",
        "{bindstopall}",
        "{bindstopall}",
        UiText::TagsBuiltinBindStopAllDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinBindStopAllTag(context);
        });

    tagRegistry_.RegisterSimple(
        "targetid",
        "{targetid}",
        "{targetid}",
        UiText::TagsBuiltinTargetIdDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinTargetIdTag(context);
        });

    tagRegistry_.RegisterSimple(
        "targetnick",
        "{targetnick}",
        "{targetnick}",
        UiText::TagsBuiltinTargetNickDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinTargetNickTag(context);
        });

    tagRegistry_.RegisterSimple(
        "targetrpnick",
        "{targetrpnick}",
        "{targetrpnick}",
        UiText::TagsBuiltinTargetRpNickDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinTargetRpNickTag(context);
        });

    tagRegistry_.RegisterSimple(
        "targetname",
        "{targetname}",
        "{targetname}",
        UiText::TagsBuiltinTargetNameDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinTargetNameTag(context);
        });

    tagRegistry_.RegisterSimple(
        "targetsurname",
        "{targetsurname}",
        "{targetsurname}",
        UiText::TagsBuiltinTargetSurnameDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinTargetSurnameTag(context);
        });

    tagRegistry_.RegisterSimple(
        "targethealth",
        "{targethealth}",
        "{targethealth}",
        UiText::TagsBuiltinTargetHealthDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinTargetHealthTag(context);
        });

    tagRegistry_.RegisterSimple(
        "targetarmour",
        "{targetarmour}",
        "{targetarmour}",
        UiText::TagsBuiltinTargetArmourDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinTargetArmourTag(context);
        });

    tagRegistry_.RegisterSimple(
        "closestid",
        "{closestid}",
        "{closestid}",
        UiText::TagsBuiltinClosestIdDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinClosestIdTag(context);
        });

    tagRegistry_.RegisterSimple(
        "closestidtocenter",
        "{closestidtocenter}",
        "{closestidtocenter}",
        UiText::TagsBuiltinClosestIdToCenterDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinClosestIdToCenterTag(context);
        });

    tagRegistry_.RegisterSimple(
        "closestname",
        "{closestname}",
        "{closestname}",
        UiText::TagsBuiltinClosestNameDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinClosestNameTag(context);
        });

    tagRegistry_.RegisterSimple(
        "closestsurname",
        "{closestsurname}",
        "{closestsurname}",
        UiText::TagsBuiltinClosestSurnameDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinClosestSurnameTag(context);
        });

    tagRegistry_.RegisterSimple(
        "armour",
        "{armour}",
        "{armour}",
        UiText::TagsBuiltinArmourDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinArmourTag(context);
        });

    tagRegistry_.RegisterSimple(
        "health",
        "{health}",
        "{health}",
        UiText::TagsBuiltinHealthDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinHealthTag(context);
        });

    tagRegistry_.RegisterSimple(
        "date",
        "{date}",
        "{date}",
        UiText::TagsBuiltinDateDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinDateTag(context);
        });

    tagRegistry_.RegisterSimple(
        "myskin",
        "{myskin}",
        "{myskin}",
        UiText::TagsBuiltinMySkinDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMySkinTag(context);
        });

    tagRegistry_.RegisterSimple(
        "myweapon",
        "{myweapon}",
        "{myweapon}",
        UiText::TagsBuiltinMyWeaponDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMyWeaponTag(context);
        });

    tagRegistry_.RegisterSimple(
        "myweaponid",
        "{myweaponid}",
        "{myweaponid}",
        UiText::TagsBuiltinMyWeaponIdDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMyWeaponIdTag(context);
        });

    tagRegistry_.RegisterSimple(
        "myweaponclip",
        "{myweaponclip}",
        "{myweaponclip}",
        UiText::TagsBuiltinMyWeaponClipDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMyWeaponClipTag(context);
        });

    tagRegistry_.RegisterSimple(
        "mymoney",
        "{mymoney}",
        "{mymoney}",
        UiText::TagsBuiltinMyMoneyDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMyMoneyTag(context);
        });

    tagRegistry_.RegisterSimple(
        "fps",
        "{fps}",
        "{fps}",
        UiText::TagsBuiltinFpsDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinFpsTag(context);
        });

    tagRegistry_.RegisterSimple(
        "getvehtype",
        "{getvehtype}",
        "{getvehtype}",
        UiText::TagsBuiltinGetVehTypeDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinGetVehTypeTag(context);
        });

    tagRegistry_.RegisterSimple(
        "screen",
        "{screen}",
        "{screen}",
        UiText::TagsBuiltinScreenDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinScreenTag(context);
        });

    tagRegistry_.RegisterSimple(
        "tphoto",
        "{tphoto}",
        "{tphoto}",
        UiText::TagsBuiltinTPhotoDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinTPhotoTag(context);
        });

    tagRegistry_.RegisterSimple(
        "nickrp",
        "{nickrp}",
        "{nickrp}",
        UiText::TagsBuiltinNickRpDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinNickRpTag(context);
        });

    tagRegistry_.RegisterSimple(
        "name",
        "{name}",
        "{name}",
        UiText::TagsBuiltinNameDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinNameTag(context);
        });

    tagRegistry_.RegisterSimple(
        "surname",
        "{surname}",
        "{surname}",
        UiText::TagsBuiltinSurnameDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinSurnameTag(context);
        });

    tagRegistry_.RegisterSimple(
        "time",
        "{time}",
        "{time}",
        UiText::TagsBuiltinTimeDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinTimeTag(context);
        });

    tagRegistry_.RegisterSimple(
        "timenosec",
        "{timenosec}",
        "{timenosec}",
        UiText::TagsBuiltinTimeNoSecDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinTimeNoSecTag(context);
        });

    tagRegistry_.RegisterSimple(
        "dialogactive",
        "{dialogactive}",
        "{dialogactive}",
        UiText::TagsBuiltinDialogActiveDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinDialogActiveTag(context);
        });

    tagRegistry_.RegisterSimple(
        "dialogcaption",
        "{dialogcaption}",
        "{dialogcaption}",
        UiText::TagsBuiltinDialogCaptionDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinDialogCaptionTag(context);
        });

    tagRegistry_.RegisterSimple(
        "dialoggetselecteditem",
        "{dialoggetselecteditem}",
        "{dialoggetselecteditem}",
        UiText::TagsBuiltinDialogGetSelectedItemDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinDialogGetSelectedItemTag(context);
        });

    tagRegistry_.RegisterSimple(
        "dialogeditboxtext",
        "{dialogeditboxtext}",
        "{dialogeditboxtext}",
        UiText::TagsBuiltinDialogEditboxTextDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinDialogEditboxTextTag(context);
        });

    tagRegistry_.RegisterSimple(
        "dialogselectedindex",
        "{dialogselectedindex}",
        "{dialogselectedindex}",
        UiText::TagsBuiltinDialogSelectedIndexDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinDialogSelectedIndexTag(context);
        });

    tagRegistry_.RegisterSimple(
        "dialogwaitopen",
        "{dialogwaitopen}",
        "{dialogwaitopen}",
        UiText::TagsBuiltinDialogWaitOpenDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinDialogWaitOpenTag(context);
        });

    tagRegistry_.RegisterSimple(
        "dialogwaitclose",
        "{dialogwaitclose}",
        "{dialogwaitclose}",
        UiText::TagsBuiltinDialogWaitCloseDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinDialogWaitCloseTag(context);
        });

    tagRegistry_.RegisterSimple(
        "dialoggetid",
        "{dialoggetid}",
        "{dialoggetid}",
        UiText::TagsBuiltinDialogGetIdDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinDialogGetIdTag(context);
        });

    tagRegistry_.RegisterFunction(
        "nick",
        "[nick(...)]",
        "[nick(15)]",
        UiText::TagsBuiltinNickFunctionDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinNickFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "rpnick",
        "[rpnick(...)]",
        "[rpnick(15)]",
        UiText::TagsBuiltinRpNickFunctionDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinRpNickFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "name",
        "[name(...)]",
        "[name(15)]",
        UiText::TagsBuiltinNameFunctionDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinNameFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "surname",
        "[surname(...)]",
        "[surname(15)]",
        UiText::TagsBuiltinSurnameFunctionDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinSurnameFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "paramcmd",
        "[paramcmd(...)]",
        "[paramcmd(1+)]",
        UiText::TagsBuiltinParamcmdDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinParamcmdFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "keyemulate",
        "[keyemulate(...)]",
        "[keyemulate(87)]",
        UiText::TagsBuiltinKeyEmulateDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinKeyEmulateFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "math",
        "[math(...)]",
        "[math(2+2)]",
        UiText::TagsBuiltinMathDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinMathFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "numberwithdots",
        "[numberwithdots(...)]",
        "[numberwithdots([math(100*10)])]",
        UiText::TagsBuiltinNumberWithDotsDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinNumberWithDotsFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "armour",
        "[armour(...)]",
        "[armour(15)]",
        UiText::TagsBuiltinArmourFunctionDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinArmourFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "health",
        "[health(...)]",
        "[health(15)]",
        UiText::TagsBuiltinHealthFunctionDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinHealthFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "skin",
        "[skin(...)]",
        "[skin(15)]",
        UiText::TagsBuiltinSkinFunctionDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinSkinFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "nickcolor",
        "[nickcolor(...)]",
        "[nickcolor(15)]",
        UiText::TagsBuiltinNickColorFunctionDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinNickColorFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "car",
        "[car(...)]",
        "[car(15)]",
        UiText::TagsBuiltinCarFunctionDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinCarFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "carhealth",
        "[carhealth(...)]",
        "[carhealth(15)]",
        UiText::TagsBuiltinCarHealthFunctionDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinCarHealthFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "keydown",
        "[keydown(...)]",
        "[keydown(87;1000)]",
        UiText::TagsBuiltinKeyDownDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinKeyDownFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "strlow",
        "[strlow(...)]",
        "[strlow(TeSt)]",
        UiText::TagsBuiltinStrLowDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinStrLowFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "addtime",
        "[addtime(...)]",
        "[addtime(10:10:10)]",
        UiText::TagsBuiltinAddTimeDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinAddTimeFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "random",
        "[random(...)]",
        "[random(20-30)]",
        UiText::TagsBuiltinRandomDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinRandomFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "ifandor",
        "[ifandor(...)]",
        "[ifandor({id}==74?[bindstart(\"10\" \"folder\")]:[bindstart(\"11\" \"folder\")])]",
        UiText::TagsBuiltinIfAndOrDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int depth) {
            return module.ResolveBuiltinIfAndOrFunctionTag(param, context, depth);
        });

    tagRegistry_.RegisterFunction(
        "timef",
        "[timef(...)]",
        "[timef(%c;)]",
        UiText::TagsBuiltinTimefDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinTimefFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "getvehtype",
        "[getvehtype(...)]",
        "[getvehtype(15)]",
        UiText::TagsBuiltinGetVehTypeFunctionDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinGetVehTypeFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "screen",
        "[screen(...)]",
        "[screen(Пример)]",
        UiText::TagsBuiltinScreenFunctionDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinScreenFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "wait",
        "[wait(...)]",
        "[wait(1000)]",
        UiText::TagsBuiltinWaitDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinWaitFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "dialogclose",
        "[dialogclose(...)]",
        "[dialogclose(1)]",
        UiText::TagsBuiltinDialogCloseDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinDialogCloseFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "dialogsettext",
        "[dialogsettext(...)]",
        "[dialogsettext(Пример)]",
        UiText::TagsBuiltinDialogSetTextDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinDialogSetTextFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "dialogitem",
        "[dialogitem(...)]",
        "[dialogitem(1)]",
        UiText::TagsBuiltinDialogItemDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinDialogItemFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "dialogselect",
        "[dialogselect(...)]",
        "[dialogselect(1)]",
        UiText::TagsBuiltinDialogSelectDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinDialogSelectFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "dialogwaitid",
        "[dialogwaitid(...)]",
        "[dialogwaitid(722)]",
        UiText::TagsBuiltinDialogWaitIdDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinDialogWaitIdFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "dialogresponse",
        "[dialogresponse(...)]",
        "[dialogresponse(1;1;)]",
        UiText::TagsBuiltinDialogResponseDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int depth) {
            return module.ResolveBuiltinDialogResponseFunctionTag(param, context, depth);
        });

    tagRegistry_.RegisterFunction(
        "dialogtext",
        "[dialogtext(...)]",
        "[dialogtext(0)]",
        UiText::TagsBuiltinDialogTextDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinDialogTextFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "save_dialog",
        "[save_dialog(...)]",
        "[save_dialog()]",
        UiText::TagsBuiltinSaveDialogDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinSaveDialogFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "binddisable",
        "[binddisable(...)]",
        "[binddisable({thisbind})]",
        UiText::TagsBuiltinBindDisableDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("disable", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindenable",
        "[bindenable(...)]",
        "[bindenable(\"10\" \"folder\")]",
        UiText::TagsBuiltinBindEnableDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("enable", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindstart",
        "[bindstart(...)]",
        "[bindstart(\"10\" \"folder\")]",
        UiText::TagsBuiltinBindStartDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("start", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindstop",
        "[bindstop(...)]",
        "[bindstop({thisbind})]",
        UiText::TagsBuiltinBindStopDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("stop", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindpause",
        "[bindpause(...)]",
        "[bindpause({thisbind})]",
        UiText::TagsBuiltinBindPauseDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("pause", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindunpause",
        "[bindunpause(...)]",
        "[bindunpause({thisbind})]",
        UiText::TagsBuiltinBindUnpauseDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("unpause", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindfastmenu",
        "[bindfastmenu(...)]",
        "[bindfastmenu(\"10\" \"folder\")]",
        UiText::TagsBuiltinBindFastMenuDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("fastmenu", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindunfastmenu",
        "[bindunfastmenu(...)]",
        "[bindunfastmenu(\"10\" \"folder\")]",
        UiText::TagsBuiltinBindUnfastMenuDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("unfastmenu", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindrandom",
        "[bindrandom(...)]",
        "[bindrandom(\"folder\")]",
        UiText::TagsBuiltinBindRandomDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("random", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindended",
        "[bindended(...)]",
        "[bindended({thisbind})]",
        UiText::TagsBuiltinBindEndedDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("ended", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindpopup",
        "[bindpopup(...)]",
        "[bindpopup(\"10\" \"folder\")]",
        UiText::TagsBuiltinBindPopupDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("popup", param, context);
        });

    RefreshCatalogEntries();
}

void TagsModule::OnProcessAttach() {
    debuglog::WriteInfo("TagsModule::OnProcessAttach begin");
    InitializeRegistry();
    LoadConfig();
    ResetTargetTracker();
    currentPage_ = MiscPage::Home;
    if (selectedTagIndex_ < 0 || selectedTagIndex_ >= static_cast<int>(tagRegistry_.Entries().size())) {
        selectedTagIndex_ = 0;
    }
    debuglog::WriteInfo(
        "TagsModule::OnProcessAttach done tags=%llu customVars=%llu",
        static_cast<unsigned long long>(tagRegistry_.Entries().size()),
        static_cast<unsigned long long>(customVariables_.size()));
}

void TagsModule::Shutdown() {
    debuglog::WriteInfo("TagsModule::Shutdown begin");
    for (ActiveVirtualKeyHold& hold : activeVirtualKeyHolds_) {
        ReleaseVirtualKeyHold(hold);
    }
    activeVirtualKeyHolds_.clear();
    pendingBindDelayOverrides_.clear();
    pendingKeyHoldWaits_.clear();
    pendingDialogWaits_.clear();
    searchQuery_.clear();
    dialogItemPickerSearchQuery_.clear();
    dialogTextPickerSearchQuery_.clear();
    ResetTargetTracker();
    currentPage_ = MiscPage::Home;
    g_activeContextStack.clear();
    debuglog::WriteInfo("TagsModule::Shutdown done");
}

void TagsModule::ReloadConfig() {
    debuglog::WriteInfo("TagsModule::ReloadConfig begin");
    Shutdown();
    InitializeRegistry();
    LoadConfig();
    if (selectedTagIndex_ < 0 || selectedTagIndex_ >= static_cast<int>(tagRegistry_.Entries().size())) {
        selectedTagIndex_ = 0;
    }
    debuglog::WriteInfo(
        "TagsModule::ReloadConfig done tags=%llu customVars=%llu",
        static_cast<unsigned long long>(tagRegistry_.Entries().size()),
        static_cast<unsigned long long>(customVariables_.size()));
}

void TagsModule::SetSampApi(SampApi* sampApi) {
    sampApi_ = sampApi;
    debuglog::WriteInfo("TagsModule::SetSampApi assigned=%d", sampApi_ ? 1 : 0);
}

void TagsModule::SetBinderModule(BinderModule* binderModule) {
    binderModule_ = binderModule;
    debuglog::WriteInfo("TagsModule::SetBinderModule assigned=%d", binderModule_ ? 1 : 0);
}

void TagsModule::SetNotificationManager(NotificationManager* notificationManager) {
    notificationManager_ = notificationManager;
    debuglog::WriteInfo("TagsModule::SetNotificationManager assigned=%d", notificationManager_ ? 1 : 0);
}

void TagsModule::NotifyTagError(std::string_view text, double durationMs) const {
    if (!notificationManager_ || text.empty()) {
        return;
    }

    notificationManager_->Notify(NotificationGroup::TagErrors, NotificationSeverity::Error, text, durationMs);
}

void TagsModule::NotifyDialogError(std::string_view text, double durationMs) const {
    if (!notificationManager_ || text.empty()) {
        return;
    }

    notificationManager_->Notify(NotificationGroup::SampDialogErrors, NotificationSeverity::Error, text, durationMs);
}

void TagsModule::NotifySuccess(std::string_view text, double durationMs) const {
    if (!notificationManager_ || text.empty()) {
        return;
    }

    notificationManager_->Notify(NotificationGroup::Success, NotificationSeverity::Success, text, durationMs);
}

TagsModule::OwnedEvaluationContext TagsModule::MakeOwnedContext(const EvaluationContext& context, SampApi* fallbackSampApi) {
    OwnedEvaluationContext owned;
    owned.sampApi = context.sampApi ? context.sampApi : fallbackSampApi;
    owned.activationSource = std::string(context.activationSource);
    owned.activationText = std::string(context.activationText);
    owned.bindCommand = std::string(context.bindCommand);
    owned.allowSideEffects = context.allowSideEffects;
    owned.runningBindRuntimeId = context.runningBindRuntimeId;
    return owned;
}

TagsModule::EvaluationContext TagsModule::MakeViewContext(const OwnedEvaluationContext& context) {
    return EvaluationContext{
        context.sampApi,
        context.activationSource,
        context.activationText,
        context.bindCommand,
        context.allowSideEffects,
        context.runningBindRuntimeId,
    };
}

void TagsModule::PushContext(const EvaluationContext& context) const {
    g_activeContextStack.push_back(MakeOwnedContext(context, sampApi_));
}

void TagsModule::PopContext() const {
    if (!g_activeContextStack.empty()) {
        g_activeContextStack.pop_back();
    }
}

std::optional<int> TagsModule::ConsumePendingBindDelayOverride(std::uint64_t runtimeId) const {
    if (runtimeId == 0) {
        return std::nullopt;
    }

    const auto it = std::find_if(
        pendingBindDelayOverrides_.begin(),
        pendingBindDelayOverrides_.end(),
        [&](const PendingBindDelayOverride& entry) { return entry.runtimeId == runtimeId; });
    if (it == pendingBindDelayOverrides_.end()) {
        return std::nullopt;
    }

    const int delayMs = it->delayMs;
    pendingBindDelayOverrides_.erase(it);
    return delayMs;
}

void TagsModule::Tick() {
    UpdateTargetTracker();
    ProcessPendingDialogWaits();
    if (!activeVirtualKeyHolds_.empty()) {
        const std::uint64_t now = GetTickCount64();
        for (auto it = activeVirtualKeyHolds_.begin(); it != activeVirtualKeyHolds_.end();) {
            ActiveVirtualKeyHold& hold = *it;
            if (!hold.pressed && now >= hold.pressAtMs) {
                hold.pressed = SendVirtualKeyEvent(hold.keyCode, false);
                if (!hold.pressed) {
                    ClearPendingKeyHoldWaitsByKeyCode(hold.keyCode);
                    it = activeVirtualKeyHolds_.erase(it);
                    continue;
                }
            }

            if (hold.pressed && now >= hold.releaseAtMs) {
                ReleaseVirtualKeyHold(hold);
                it = activeVirtualKeyHolds_.erase(it);
                continue;
            }

            ++it;
        }
    }

    ProcessPendingKeyHoldWaits();
}

void TagsModule::QueuePendingDialogWait(
    std::uint64_t runtimeId,
    PendingDialogWaitKind kind,
    std::uint64_t deadlineAtMs,
    int expectedDialogId) {
    if (runtimeId == 0) {
        return;
    }

    if (auto it = std::find_if(
            pendingDialogWaits_.begin(),
            pendingDialogWaits_.end(),
            [&](const PendingDialogWait& wait) { return wait.runtimeId == runtimeId; });
        it != pendingDialogWaits_.end()) {
        it->kind = kind;
        it->deadlineAtMs = deadlineAtMs;
        it->expectedDialogId = expectedDialogId;
        return;
    }

    pendingDialogWaits_.push_back(PendingDialogWait{ runtimeId, kind, deadlineAtMs, expectedDialogId });
}

void TagsModule::ClearPendingDialogWait(std::uint64_t runtimeId) {
    if (runtimeId == 0) {
        return;
    }

    pendingDialogWaits_.erase(
        std::remove_if(pendingDialogWaits_.begin(), pendingDialogWaits_.end(), [&](const PendingDialogWait& wait) {
            return wait.runtimeId == runtimeId;
        }),
        pendingDialogWaits_.end());
}

void TagsModule::QueuePendingKeyHoldWait(std::uint64_t runtimeId, unsigned int keyCode, std::uint64_t releaseAtMs) const {
    if (runtimeId == 0 || keyCode == 0 || releaseAtMs == 0) {
        return;
    }

    pendingKeyHoldWaits_.push_back(PendingKeyHoldWait{ runtimeId, keyCode, releaseAtMs });
}

void TagsModule::ClearPendingKeyHoldWaitsByKeyCode(unsigned int keyCode) const {
    if (keyCode == 0 || pendingKeyHoldWaits_.empty()) {
        return;
    }

    std::vector<std::uint64_t> runtimesToResume;
    for (const PendingKeyHoldWait& wait : pendingKeyHoldWaits_) {
        if (wait.keyCode == keyCode && wait.runtimeId != 0) {
            runtimesToResume.push_back(wait.runtimeId);
        }
    }

    pendingKeyHoldWaits_.erase(
        std::remove_if(
            pendingKeyHoldWaits_.begin(),
            pendingKeyHoldWaits_.end(),
            [&](const PendingKeyHoldWait& wait) { return wait.keyCode == keyCode; }),
        pendingKeyHoldWaits_.end());

    if (!binderModule_) {
        return;
    }

    for (const std::uint64_t runtimeId : runtimesToResume) {
        if (runtimeId == 0
            || !binderModule_->IsRuntimeActive(runtimeId)
            || !binderModule_->IsRuntimePaused(runtimeId)
            || HasPendingKeyHoldWait(runtimeId)
            || HasPendingDialogWait(runtimeId)) {
            continue;
        }

        binderModule_->ResumeRuntime(runtimeId);
    }
}

bool TagsModule::HasPendingDialogWait(std::uint64_t runtimeId) const {
    return runtimeId != 0
        && std::any_of(
            pendingDialogWaits_.begin(),
            pendingDialogWaits_.end(),
            [&](const PendingDialogWait& wait) { return wait.runtimeId == runtimeId; });
}

bool TagsModule::HasPendingKeyHoldWait(std::uint64_t runtimeId) const {
    return runtimeId != 0
        && std::any_of(
            pendingKeyHoldWaits_.begin(),
            pendingKeyHoldWaits_.end(),
            [&](const PendingKeyHoldWait& wait) { return wait.runtimeId == runtimeId; });
}

void TagsModule::ProcessPendingKeyHoldWaits() {
    if (pendingKeyHoldWaits_.empty() || !binderModule_) {
        return;
    }

    const std::uint64_t now = GetTickCount64();
    for (auto it = pendingKeyHoldWaits_.begin(); it != pendingKeyHoldWaits_.end();) {
        const std::uint64_t runtimeId = it->runtimeId;
        if (runtimeId == 0 || !binderModule_->IsRuntimeActive(runtimeId)) {
            it = pendingKeyHoldWaits_.erase(it);
            continue;
        }

        if (now < it->releaseAtMs) {
            if (!binderModule_->IsRuntimePaused(runtimeId)) {
                binderModule_->PauseRuntime(runtimeId);
            }
            ++it;
            continue;
        }

        it = pendingKeyHoldWaits_.erase(it);
        if (binderModule_->IsRuntimeActive(runtimeId)
            && binderModule_->IsRuntimePaused(runtimeId)
            && !HasPendingKeyHoldWait(runtimeId)
            && !HasPendingDialogWait(runtimeId)) {
            binderModule_->ResumeRuntime(runtimeId);
        }
    }
}

void TagsModule::ProcessPendingDialogWaits() {
    if (pendingDialogWaits_.empty() || !binderModule_) {
        return;
    }

    const bool dialogActive = sampApi_ && sampApi_->sampModule() && sampApi_->isSupportedVersion() && sampApi_->isDialogActive();
    const int activeDialogId = dialogActive ? sampApi_->SAMP_DIALOG_ID() : -1;
    const std::uint64_t now = GetTickCount64();
    for (auto it = pendingDialogWaits_.begin(); it != pendingDialogWaits_.end();) {
        const std::uint64_t runtimeId = it->runtimeId;
        if (runtimeId == 0 || !binderModule_->IsRuntimeActive(runtimeId)) {
            it = pendingDialogWaits_.erase(it);
            continue;
        }

        if (it->kind == PendingDialogWaitKind::Open) {
            if (dialogActive) {
                if (binderModule_->IsRuntimePaused(runtimeId) && !HasPendingKeyHoldWait(runtimeId)) {
                    binderModule_->ResumeRuntime(runtimeId);
                }
                it = pendingDialogWaits_.erase(it);
                continue;
            }

            if (it->deadlineAtMs != 0 && now >= it->deadlineAtMs) {
                NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogWaitOpenTimedOut), 3000.0);
                binderModule_->StopRuntime(runtimeId);
                it = pendingDialogWaits_.erase(it);
                continue;
            }

            if (!binderModule_->IsRuntimePaused(runtimeId)) {
                binderModule_->PauseRuntime(runtimeId);
            }
            ++it;
            continue;
        }

        if (it->kind == PendingDialogWaitKind::SpecificId) {
            if (dialogActive && activeDialogId == it->expectedDialogId) {
                if (binderModule_->IsRuntimePaused(runtimeId) && !HasPendingKeyHoldWait(runtimeId)) {
                    binderModule_->ResumeRuntime(runtimeId);
                }
                it = pendingDialogWaits_.erase(it);
                continue;
            }

            if (it->deadlineAtMs != 0 && now >= it->deadlineAtMs) {
                NotifyDialogError(
                    UiSettings::Instance().Format(
                        UiText::ToastDialogWaitIdTimedOut,
                        std::to_string(std::max(it->expectedDialogId, 0)).c_str()),
                    3000.0);
                binderModule_->StopRuntime(runtimeId);
                it = pendingDialogWaits_.erase(it);
                continue;
            }

            if (!binderModule_->IsRuntimePaused(runtimeId)) {
                binderModule_->PauseRuntime(runtimeId);
            }
            ++it;
            continue;
        }

        if (!dialogActive) {
            if (binderModule_->IsRuntimePaused(runtimeId) && !HasPendingKeyHoldWait(runtimeId)) {
                binderModule_->ResumeRuntime(runtimeId);
            }
            it = pendingDialogWaits_.erase(it);
            continue;
        }

        if (!binderModule_->IsRuntimePaused(runtimeId)) {
            binderModule_->PauseRuntime(runtimeId);
        }
        ++it;
    }
}

void TagsModule::LoadConfig() {
    debuglog::WriteInfo("TagsModule::LoadConfig begin");
    customVariables_.clear();

    const jsonutil::JsonObject section = AppConfig::Instance().ReadSectionObject(kTagsSectionName);
    const jsonutil::JsonObject* customVars = jsonutil::JsonObjectOrNull(&section, kCustomVarsKey.data());
    if (!customVars) {
        return;
    }

    for (const auto& [key, value] : *customVars) {
        if (const std::string* text = value.TryString()) {
            customVariables_.emplace_back(key, *text);
        }
    }

    std::sort(customVariables_.begin(), customVariables_.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    debuglog::WriteInfo("TagsModule::LoadConfig done customVars=%llu", static_cast<unsigned long long>(customVariables_.size()));
}

void TagsModule::SaveConfig() const {
    debuglog::WriteInfo("TagsModule::SaveConfig queued customVars=%llu", static_cast<unsigned long long>(customVariables_.size()));
    jsonutil::JsonObject section;
    jsonutil::JsonObject customVars;
    for (const auto& [name, value] : customVariables_) {
        customVars[name] = value;
    }
    section[std::string(kCustomVarsKey)] = jsonutil::JsonValue(std::move(customVars));
    AppConfig::Instance().QueueSectionReplace(std::string(kTagsSectionName), jsonutil::JsonValue(std::move(section)));
}

std::string TagsModule::Trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
}

std::string TagsModule::ToLower(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const unsigned char ch : value) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

bool TagsModule::StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::vector<std::string> TagsModule::SplitCommandArgs(std::string_view value) {
    std::vector<std::string> result;
    std::size_t pos = 0;
    while (pos < value.size()) {
        while (pos < value.size() && std::isspace(static_cast<unsigned char>(value[pos])) != 0) {
            ++pos;
        }
        if (pos >= value.size()) {
            break;
        }

        std::size_t end = pos;
        while (end < value.size() && std::isspace(static_cast<unsigned char>(value[end])) == 0) {
            ++end;
        }
        result.emplace_back(value.substr(pos, end - pos));
        pos = end;
    }
    return result;
}

std::optional<int> TagsModule::ParseInteger(std::string_view value) {
    const std::string trimmed = Trim(value);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    int sign = 1;
    std::size_t pos = 0;
    if (trimmed.front() == '+') {
        pos = 1;
    } else if (trimmed.front() == '-') {
        sign = -1;
        pos = 1;
    }

    if (pos >= trimmed.size()) {
        return std::nullopt;
    }

    int parsed = 0;
    for (; pos < trimmed.size(); ++pos) {
        const unsigned char ch = static_cast<unsigned char>(trimmed[pos]);
        if (std::isdigit(ch) == 0) {
            return std::nullopt;
        }
        parsed = parsed * 10 + static_cast<int>(ch - '0');
    }

    return parsed * sign;
}

TagsModule::EvaluationContext TagsModule::ResolveActiveContext(
    std::string_view defaultSource,
    std::string_view defaultText) const {
    if (!g_activeContextStack.empty()) {
        return MakeViewContext(g_activeContextStack.back());
    }

    return EvaluationContext{
        sampApi_,
        defaultSource,
        defaultText,
        {},
        true,
        0,
    };
}

std::string TagsModule::ResolvePlayerNickById(int id, const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || id < 0) {
        return std::string();
    }

    const std::string nick = sampApi->GetNameID(id);
    return nick.empty() || nick == "UNKNOWN" ? std::string() : nick;
}

std::string TagsModule::ResolveLocalNick(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return {};
    }
    return ResolvePlayerNickById(sampApi->Local_ID(), context);
}

std::string TagsModule::ResolveLastTargetNick(const EvaluationContext& context) const {
    if (targetTracker_.lastId < 0) {
        return {};
    }
    return ResolvePlayerNickById(targetTracker_.lastId, context);
}

void TagsModule::ResetTargetTracker() {
    targetTracker_ = TargetTrackerState{};
}

TagsModule::ClosestPlayerQueryResult TagsModule::QueryClosestPlayers(const EvaluationContext& context) const {
    ClosestPlayerQueryResult result;

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return result;
    }

    const int localId = sampApi->Local_ID();
    CPed* const localPed = FindPlayerPed();
    auto* const pedPool = CPools::ms_pPedPool;
    if (localId < 0 || !localPed || !pedPool || pedPool->m_nSize <= 0) {
        return result;
    }

    const CVector localPosition = localPed->GetPosition();
    const bool hasScreenMetrics = RsGlobal.maximumWidth > 0 && RsGlobal.maximumHeight > 0;
    const float screenCenterX = static_cast<float>(RsGlobal.maximumWidth) * 0.5f;
    const float screenCenterY = static_cast<float>(RsGlobal.maximumHeight) * 0.5f;

    float bestDistanceSq = std::numeric_limits<float>::max();
    float bestCenterDistanceSq = std::numeric_limits<float>::max();

    for (int index = 0; index < pedPool->m_nSize; ++index) {
        CPed* const candidatePed = pedPool->GetAt(index);
        if (!candidatePed || candidatePed == localPed) {
            continue;
        }

        const auto [matched, id] = sampApi->getPedID(candidatePed);
        if (!matched || id < 0 || id == localId || !sampApi->IsConnected(id)) {
            continue;
        }

        const CVector& position = candidatePed->GetPosition();
        const float dx = position.x - localPosition.x;
        const float dy = position.y - localPosition.y;
        const float dz = position.z - localPosition.z;
        const float distanceSq = dx * dx + dy * dy + dz * dz;

        if (IsBetterClosestCandidate(distanceSq, id, bestDistanceSq, result.nearestId)) {
            bestDistanceSq = distanceSq;
            result.nearestId = id;
        }

        if (!hasScreenMetrics) {
            continue;
        }

        RwV3d worldPosition = {
            position.x,
            position.y,
            position.z + kClosestScreenTargetZOffset,
        };
        RwV3d screenPosition{};
        float width = 0.0f;
        float height = 0.0f;
        if (!CSprite::CalcScreenCoors(worldPosition, &screenPosition, &width, &height, true, true)) {
            continue;
        }

        const float screenDx = screenPosition.x - screenCenterX;
        const float screenDy = screenPosition.y - screenCenterY;
        const float centerDistanceSq = screenDx * screenDx + screenDy * screenDy;
        if (IsBetterClosestCandidate(
                centerDistanceSq,
                id,
                bestCenterDistanceSq,
                result.nearestToCenterId)) {
            bestCenterDistanceSq = centerDistanceSq;
            result.nearestToCenterId = id;
        }
    }

    return result;
}

void TagsModule::UpdateTargetTracker() {
    SampApi* sampApi = sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        if (targetTracker_.sessionActive) {
            ResetTargetTracker();
        }
        return;
    }

    CPlayerPed* playerPed = FindPlayerPed();
    const int localId = sampApi->Local_ID();
    const bool sessionReady = playerPed != nullptr && localId >= 0;
    if (!sessionReady) {
        if (targetTracker_.sessionActive) {
            ResetTargetTracker();
        }
        return;
    }

    if (!targetTracker_.sessionActive) {
        ResetTargetTracker();
        targetTracker_.sessionActive = true;
    }

    targetTracker_.currentId = -1;

    CPed* targetedPed = playerPed->m_pPlayerTargettedPed;
    if (!targetedPed) {
        return;
    }

    const auto [resolved, targetId] = sampApi->getPedID(targetedPed);
    if (!resolved || targetId < 0) {
        return;
    }

    targetTracker_.currentId = targetId;
    targetTracker_.lastId = targetId;
}

std::uint64_t TagsModule::QueueVirtualKeyHold(unsigned int keyCode, int startDelayMs, int holdDurationMs) const {
    if (keyCode == 0 || keyCode > 0xFF) {
        return 0;
    }

    const std::uint64_t now = GetTickCount64();
    const std::uint64_t pressAtMs = now + static_cast<std::uint64_t>(std::max(startDelayMs, 0));
    const std::uint64_t releaseAtMs = pressAtMs + static_cast<std::uint64_t>(std::max(holdDurationMs, 1));

    for (auto it = activeVirtualKeyHolds_.begin(); it != activeVirtualKeyHolds_.end();) {
        if (it->keyCode == keyCode) {
            ReleaseVirtualKeyHold(*it);
            ClearPendingKeyHoldWaitsByKeyCode(keyCode);
            it = activeVirtualKeyHolds_.erase(it);
            continue;
        }
        ++it;
    }

    activeVirtualKeyHolds_.push_back(ActiveVirtualKeyHold{
        keyCode,
        pressAtMs,
        releaseAtMs,
        false,
    });
    return releaseAtMs;
}

void TagsModule::QueuePendingBindDelayOverride(std::uint64_t runtimeId, int delayMs) const {
    if (runtimeId == 0) {
        return;
    }

    if (auto it = std::find_if(
            pendingBindDelayOverrides_.begin(),
            pendingBindDelayOverrides_.end(),
            [&](const PendingBindDelayOverride& entry) { return entry.runtimeId == runtimeId; });
        it != pendingBindDelayOverrides_.end()) {
        it->delayMs = delayMs;
        return;
    }

    pendingBindDelayOverrides_.push_back(PendingBindDelayOverride{ runtimeId, delayMs });
}

void TagsModule::ReleaseVirtualKeyHold(ActiveVirtualKeyHold& hold) const {
    if (!hold.pressed) {
        return;
    }
    SendVirtualKeyEvent(hold.keyCode, true);
    hold.pressed = false;
}

std::string TagsModule::FormatCurrentTime(std::string_view format) {
    if (format.empty()) {
        return {};
    }

    std::time_t now = std::time(nullptr);
    std::tm localTime{};
    if (localtime_s(&localTime, &now) != 0) {
        return {};
    }

    const std::string formatString(format);
    std::size_t bufferSize = std::max<std::size_t>(128, formatString.size() * 8 + 32);
    for (int attempt = 0; attempt < 6; ++attempt) {
        std::string buffer(bufferSize, '\0');
        const std::size_t written = std::strftime(buffer.data(), buffer.size(), formatString.c_str(), &localTime);
        if (written != 0) {
            buffer.resize(written);
            return buffer;
        }
        bufferSize *= 2;
    }
    return {};
}

std::string TagsModule::FormatWholeStatValue(float value) {
    const long long rounded = std::llround(std::max(0.0f, value));
    return std::to_string(rounded);
}

std::string TagsModule::MakeRpNick(std::string_view nick) {
    std::string result;
    result.reserve(nick.size());
    for (const char ch : nick) {
        result.push_back(ch == '_' ? ' ' : ch);
    }
    return result;
}

std::string TagsModule::ExtractName(std::string_view nick) {
    const std::size_t separator = nick.find('_');
    return std::string(nick.substr(0, separator));
}

std::string TagsModule::ExtractSurname(std::string_view nick) {
    const std::size_t separator = nick.find('_');
    if (separator == std::string_view::npos || separator + 1 >= nick.size()) {
        return {};
    }
    return std::string(nick.substr(separator + 1));
}

std::optional<std::string> TagsModule::ResolveBuiltinIdTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }
    return std::to_string(sampApi->Local_ID());
}

std::optional<std::string> TagsModule::ResolveBuiltinNickTag(const EvaluationContext& context) const {
    return ResolveLocalNick(context);
}

std::optional<std::string> TagsModule::ResolveBuiltinThisbindTag(const EvaluationContext& context) const {
    if (!binderModule_ || context.runningBindRuntimeId == 0) {
        return std::string();
    }
    return binderModule_->GetThisbindTagValue(context.runningBindRuntimeId);
}

std::optional<std::string> TagsModule::ResolveBuiltinThiscategoryTag(const EvaluationContext& context) const {
    if (!binderModule_ || context.runningBindRuntimeId == 0) {
        return std::string();
    }
    return binderModule_->GetThiscategoryTagValue(context.runningBindRuntimeId);
}

std::optional<std::string> TagsModule::ResolveBuiltinBindStopAllTag(const EvaluationContext& context) const {
    if (!context.allowSideEffects || !binderModule_) {
        return std::string();
    }
    static_cast<void>(binderModule_->ExecuteTagAction("stopall", {}, context.runningBindRuntimeId));
    return std::string();
}

std::optional<std::string> TagsModule::ResolveBuiltinTargetIdTag(const EvaluationContext&) const {
    if (targetTracker_.lastId < 0) {
        return std::string();
    }
    return std::to_string(targetTracker_.lastId);
}

std::optional<std::string> TagsModule::ResolveBuiltinTargetNickTag(const EvaluationContext& context) const {
    return ResolveLastTargetNick(context);
}

std::optional<std::string> TagsModule::ResolveBuiltinTargetRpNickTag(const EvaluationContext& context) const {
    return MakeRpNick(ResolveLastTargetNick(context));
}

std::optional<std::string> TagsModule::ResolveBuiltinTargetNameTag(const EvaluationContext& context) const {
    return ExtractName(ResolveLastTargetNick(context));
}

std::optional<std::string> TagsModule::ResolveBuiltinTargetSurnameTag(const EvaluationContext& context) const {
    return ExtractSurname(ResolveLastTargetNick(context));
}

std::optional<std::string> TagsModule::ResolveBuiltinTargetHealthTag(const EvaluationContext& context) const {
    if (targetTracker_.lastId < 0) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }

    const SampApi::HealthAndArmour stats = sampApi->GetHealthAndArmour(targetTracker_.lastId);
    if (!stats.valid) {
        return std::string();
    }

    return FormatWholeStatValue(stats.health);
}

std::optional<std::string> TagsModule::ResolveBuiltinTargetArmourTag(const EvaluationContext& context) const {
    if (targetTracker_.lastId < 0) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }

    const SampApi::HealthAndArmour stats = sampApi->GetHealthAndArmour(targetTracker_.lastId);
    if (!stats.valid) {
        return std::string();
    }

    return FormatWholeStatValue(stats.armour);
}

std::optional<std::string> TagsModule::ResolveBuiltinClosestIdTag(const EvaluationContext& context) const {
    const ClosestPlayerQueryResult result = QueryClosestPlayers(context);
    if (result.nearestId < 0) {
        return std::string();
    }
    return std::to_string(result.nearestId);
}

std::optional<std::string> TagsModule::ResolveBuiltinClosestIdToCenterTag(const EvaluationContext& context) const {
    const ClosestPlayerQueryResult result = QueryClosestPlayers(context);
    if (result.nearestToCenterId < 0) {
        return std::string();
    }
    return std::to_string(result.nearestToCenterId);
}

std::optional<std::string> TagsModule::ResolveBuiltinClosestNameTag(const EvaluationContext& context) const {
    const ClosestPlayerQueryResult result = QueryClosestPlayers(context);
    if (result.nearestId < 0) {
        return std::string();
    }
    return ExtractName(ResolvePlayerNickById(result.nearestId, context));
}

std::optional<std::string> TagsModule::ResolveBuiltinClosestSurnameTag(const EvaluationContext& context) const {
    const ClosestPlayerQueryResult result = QueryClosestPlayers(context);
    if (result.nearestId < 0) {
        return std::string();
    }
    return ExtractSurname(ResolvePlayerNickById(result.nearestId, context));
}

std::optional<std::string> TagsModule::ResolveBuiltinArmourTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string("0");
    }

    const int localId = sampApi->Local_ID();
    if (localId < 0) {
        return std::string("0");
    }

    const SampApi::HealthAndArmour stats = sampApi->GetHealthAndArmour(localId);
    if (!stats.valid) {
        return std::string("0");
    }
    return FormatWholeStatValue(stats.armour);
}

std::optional<std::string> TagsModule::ResolveBuiltinHealthTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string("0");
    }

    const int localId = sampApi->Local_ID();
    if (localId < 0) {
        return std::string("0");
    }

    const SampApi::HealthAndArmour stats = sampApi->GetHealthAndArmour(localId);
    if (!stats.valid) {
        return std::string("0");
    }
    return FormatWholeStatValue(stats.health);
}

std::optional<std::string> TagsModule::ResolveBuiltinDateTag(const EvaluationContext&) const {
    return FormatCurrentTime("%d.%m.%Y");
}

std::optional<std::string> TagsModule::ResolveBuiltinMySkinTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }

    CPed* playerPed = FindPlayerPed();
    if (!playerPed) {
        return std::string();
    }
    return std::to_string(playerPed->m_nModelIndex);
}

std::optional<std::string> TagsModule::ResolveBuiltinMyWeaponTag(const EvaluationContext&) const {
    const CWeapon* const weapon = FindLocalWeapon();
    if (!weapon) {
        return std::string();
    }
    return GetWeaponDisplayName(weapon->m_eWeaponType);
}

std::optional<std::string> TagsModule::ResolveBuiltinMyWeaponIdTag(const EvaluationContext&) const {
    const CWeapon* const weapon = FindLocalWeapon();
    if (!weapon) {
        return std::string();
    }
    return std::to_string(static_cast<unsigned int>(weapon->m_eWeaponType));
}

std::optional<std::string> TagsModule::ResolveBuiltinMyWeaponClipTag(const EvaluationContext&) const {
    const CWeapon* const weapon = FindLocalWeapon();
    if (!weapon) {
        return std::string("0");
    }
    return std::to_string(weapon->m_nAmmoInClip);
}

std::optional<std::string> TagsModule::ResolveBuiltinMyMoneyTag(const EvaluationContext&) const {
    CPlayerPed* const playerPed = FindPlayerPed();
    if (!playerPed) {
        return std::string();
    }

    CPlayerInfo* const playerInfo = playerPed->GetPlayerInfoForThisPlayerPed();
    if (!playerInfo) {
        return std::string();
    }

    return std::to_string(playerInfo->m_nMoney);
}

std::optional<std::string> TagsModule::ResolveBuiltinFpsTag(const EvaluationContext&) const {
    if (!ImGui::GetCurrentContext()) {
        return std::string("0");
    }

    const float fps = std::max(0.0f, ImGui::GetIO().Framerate);
    return std::to_string(std::lround(fps));
}

std::optional<std::string> TagsModule::ResolveBuiltinGetVehTypeTag(const EvaluationContext&) const {
    if (CVehicle* const vehicle = FindPlayerVehicle(-1, false); IsVehiclePointerValid(vehicle)) {
        return GetVehicleTypeName(vehicle->m_nModelIndex);
    }

    return ResolveVehicleTypeForPed(FindPlayerPed());
}

std::optional<std::string> TagsModule::ResolveBuiltinScreenTag(const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    const ScreenCaptureResult result = CaptureGameScreenshot({});
    if (!result.Ok() && binderModule_) {
        NotifyTagError(DescribeScreenCaptureError(result.error, result.detail), 2800.0);
    }
    return std::string();
}

std::optional<std::string> TagsModule::ResolveBuiltinTPhotoTag(const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    if (!TakeGameCameraPhoto() && binderModule_) {
        NotifyTagError(UiSettings::Instance().Text(UiText::ToastTPhotoFailed), 2800.0);
    }
    return std::string();
}

std::optional<std::string> TagsModule::ResolveBuiltinNickRpTag(const EvaluationContext& context) const {
    return MakeRpNick(ResolveLocalNick(context));
}

std::optional<std::string> TagsModule::ResolveBuiltinNameTag(const EvaluationContext& context) const {
    return ExtractName(ResolveLocalNick(context));
}

std::optional<std::string> TagsModule::ResolveBuiltinSurnameTag(const EvaluationContext& context) const {
    return ExtractSurname(ResolveLocalNick(context));
}

std::optional<std::string> TagsModule::ResolveBuiltinTimeTag(const EvaluationContext&) const {
    return FormatCurrentTime("%H:%M:%S");
}

std::optional<std::string> TagsModule::ResolveBuiltinTimeNoSecTag(const EvaluationContext&) const {
    return FormatCurrentTime("%H:%M");
}

std::optional<std::string> TagsModule::ResolveBuiltinDialogActiveTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string("false");
    }

    return sampApi->isDialogActive() ? std::string("true") : std::string("false");
}

std::optional<std::string> TagsModule::ResolveBuiltinDialogCaptionTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        return std::string();
    }

    return NormalizeDialogCaptionVisibleText(sampApi->get_dialog_caption());
}

std::optional<std::string> TagsModule::ResolveBuiltinDialogGetSelectedItemTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        return std::string();
    }

    const SampApi::DialogSelectionText selection = sampApi->getDialogSelectedItemText();
    if (!selection.found) {
        return std::string();
    }

    return NormalizeDialogVisibleText(selection.text);
}

std::optional<std::string> TagsModule::ResolveBuiltinDialogEditboxTextTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        return std::string();
    }

    const int style = sampApi->GetCurrentDialogStyle();
    if (style < 0 || !sampApi->isDialogInputStyle(style) || !sampApi->pDialogInput_pEditBox_active_func()) {
        return std::string();
    }

    return sampApi->sampGetDialogEditboxText();
}

std::optional<std::string> TagsModule::ResolveBuiltinDialogSelectedIndexTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        return std::string();
    }

    const int style = sampApi->GetCurrentDialogStyle();
    if (style < 0 || !sampApi->isDialogListStyle(style)) {
        return std::string();
    }

    const int selectedIndex = sampApi->GetCurrentDialogListItem();
    if (selectedIndex < 0) {
        return std::string();
    }

    return std::to_string(selectedIndex);
}

std::optional<std::string> TagsModule::ResolveBuiltinDialogWaitOpenTag(const EvaluationContext& context) const {
    if (!context.allowSideEffects || context.runningBindRuntimeId == 0 || !binderModule_) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    const bool dialogActive = sampApi && sampApi->sampModule() && sampApi->isSupportedVersion() && sampApi->isDialogActive();
    if (dialogActive) {
        return std::string();
    }

    binderModule_->PauseRuntime(context.runningBindRuntimeId);
    const std::uint64_t deadlineAtMs = GetTickCount64() + kDialogWaitOpenTimeoutMs;
    const_cast<TagsModule*>(this)->QueuePendingDialogWait(
        context.runningBindRuntimeId,
        PendingDialogWaitKind::Open,
        deadlineAtMs);
    return std::string();
}

std::optional<std::string> TagsModule::ResolveBuiltinDialogWaitCloseTag(const EvaluationContext& context) const {
    if (!context.allowSideEffects || context.runningBindRuntimeId == 0 || !binderModule_) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    const bool dialogActive = sampApi && sampApi->sampModule() && sampApi->isSupportedVersion() && sampApi->isDialogActive();
    if (!dialogActive) {
        return std::string();
    }

    binderModule_->PauseRuntime(context.runningBindRuntimeId);
    const_cast<TagsModule*>(this)->QueuePendingDialogWait(
        context.runningBindRuntimeId,
        PendingDialogWaitKind::Close,
        0);
    return std::string();
}

std::optional<std::string> TagsModule::ResolveBuiltinDialogGetIdTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        return std::string();
    }

    const int dialogId = sampApi->SAMP_DIALOG_ID();
    if (dialogId < 0) {
        return std::string();
    }

    return std::to_string(dialogId);
}

std::optional<std::string> TagsModule::ResolveBuiltinNickFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }
    return ResolvePlayerNickById(*id, context);
}

std::optional<std::string> TagsModule::ResolveBuiltinRpNickFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }
    return MakeRpNick(ResolvePlayerNickById(*id, context));
}

std::optional<std::string> TagsModule::ResolveBuiltinNameFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }
    return ExtractName(ResolvePlayerNickById(*id, context));
}

std::optional<std::string> TagsModule::ResolveBuiltinSurnameFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }
    return ExtractSurname(ResolvePlayerNickById(*id, context));
}

std::optional<std::string> TagsModule::ResolveBuiltinParamcmdFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (context.activationSource != "command") {
        return std::string();
    }

    std::string command = Trim(context.bindCommand);
    std::string input = Trim(context.activationText);
    if (!command.empty() && command.front() != '/') {
        command.insert(command.begin(), '/');
    }
    if (!input.empty() && input.front() != '/') {
        input.insert(input.begin(), '/');
    }

    if (command.empty() || input.empty() || !StartsWith(input, command)) {
        return std::string();
    }
    if (input.size() > command.size()
        && std::isspace(static_cast<unsigned char>(input[command.size()])) == 0) {
        return std::string();
    }

    const std::string argsText = Trim(input.substr(command.size()));
    const std::vector<std::string> args = SplitCommandArgs(argsText);
    if (args.empty()) {
        return std::string();
    }

    std::string selector = Trim(param);
    selector.erase(
        std::remove_if(selector.begin(), selector.end(), [](const unsigned char ch) {
            return std::isspace(ch) != 0;
        }),
        selector.end());
    if (selector.empty()) {
        return std::string();
    }

    const auto parsePositiveIndex = [](std::string_view raw) -> int {
        if (raw.empty()) {
            return -1;
        }
        int value = 0;
        for (const unsigned char ch : raw) {
            if (std::isdigit(ch) == 0) {
                return -1;
            }
            value = value * 10 + static_cast<int>(ch - '0');
        }
        return value;
    };

    if (const int single = parsePositiveIndex(selector); single >= 0) {
        if (single < 1 || single > static_cast<int>(args.size())) {
            return std::string();
        }
        return args[static_cast<std::size_t>(single - 1)];
    }

    if (selector.back() == '+' && selector.size() > 1) {
        const int from = parsePositiveIndex(std::string_view(selector).substr(0, selector.size() - 1));
        if (from < 1 || from > static_cast<int>(args.size())) {
            return std::string();
        }

        std::string joined;
        for (int i = from - 1; i < static_cast<int>(args.size()); ++i) {
            if (!joined.empty()) {
                joined += ' ';
            }
            joined += args[static_cast<std::size_t>(i)];
        }
        return joined;
    }

    if (selector.back() == '-' && selector.size() > 1) {
        const int upto = parsePositiveIndex(std::string_view(selector).substr(0, selector.size() - 1));
        if (upto < 1) {
            return std::string();
        }

        const int clamped = std::min(upto, static_cast<int>(args.size()));
        std::string joined;
        for (int i = 0; i < clamped; ++i) {
            if (!joined.empty()) {
                joined += ' ';
            }
            joined += args[static_cast<std::size_t>(i)];
        }
        return joined;
    }

    if (const std::size_t dashPos = selector.find('-'); dashPos != std::string::npos) {
        const int from = parsePositiveIndex(std::string_view(selector).substr(0, dashPos));
        const int to = parsePositiveIndex(std::string_view(selector).substr(dashPos + 1));
        if (from < 1 || to < from || from > static_cast<int>(args.size())) {
            return std::string();
        }

        const int clampedTo = std::min(to, static_cast<int>(args.size()));
        std::string joined;
        for (int i = from - 1; i < clampedTo; ++i) {
            if (!joined.empty()) {
                joined += ' ';
            }
            joined += args[static_cast<std::size_t>(i)];
        }
        return joined;
    }

    return std::string();
}

std::optional<std::string> TagsModule::ResolveBuiltinKeyEmulateFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    const std::optional<int> keyCode = ParseInteger(param);
    if (!keyCode.has_value() || *keyCode < 1 || *keyCode > 0xFF) {
        return std::string();
    }

    if (context.allowSideEffects) {
        QueueVirtualKeyHold(
            static_cast<UINT>(*keyCode),
            kKeyEmulateStartDelayMs,
            kKeyEmulateTapMs);
    }
    return std::string();
}

std::optional<std::string> TagsModule::ResolveBuiltinMathFunctionTag(
    std::string_view param,
    const EvaluationContext&) const {
    const std::string expression = Trim(param);
    if (expression.empty()) {
        return std::string();
    }

    MathExpressionParser parser(expression);
    const std::optional<double> result = parser.Evaluate();
    if (!result.has_value()) {
        return std::string();
    }
    return FormatMathResult(*result);
}

std::optional<std::string> TagsModule::ResolveBuiltinNumberWithDotsFunctionTag(
    std::string_view param,
    const EvaluationContext&) const {
    return FormatNumberWithDots(param);
}

std::optional<std::string> TagsModule::ResolveBuiltinArmourFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }

    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }

    const SampApi::HealthAndArmour stats = sampApi->GetHealthAndArmour(*id);
    if (!stats.valid) {
        return std::string();
    }
    return FormatWholeStatValue(stats.armour);
}

std::optional<std::string> TagsModule::ResolveBuiltinHealthFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }

    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }

    const SampApi::HealthAndArmour stats = sampApi->GetHealthAndArmour(*id);
    if (!stats.valid) {
        return std::string();
    }
    return FormatWholeStatValue(stats.health);
}

std::optional<std::string> TagsModule::ResolveBuiltinSkinFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }

    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }

    const CPed* const ped = FindPlayerPedBySampId(*sampApi, *id);
    if (!ped) {
        return std::string();
    }

    return std::to_string(ped->m_nModelIndex);
}

std::optional<std::string> TagsModule::ResolveBuiltinNickColorFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string("{FFFFFF}");
    }
    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value() || !sampApi->IsConnected(*id)) {
        return std::string("{FFFFFF}");
    }

    const std::optional<std::uint32_t> color = sampApi->GetPlayerColor(*id);
    return color.has_value() ? FormatSampColorTag(*color) : std::string("{FFFFFF}");
}

std::optional<std::string> TagsModule::ResolveBuiltinCarFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }

    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }

    return ResolveVehicleNameForPed(FindPlayerPedBySampId(*sampApi, *id));
}

std::optional<std::string> TagsModule::ResolveBuiltinCarHealthFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }

    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }

    const CPed* const ped = FindPlayerPedBySampId(*sampApi, *id);
    if (!ped || !IsVehiclePointerValid(ped->m_pVehicle)) {
        return std::string();
    }
    return FormatWholeStatValue(ped->m_pVehicle->m_fHealth);
}

std::optional<std::string> TagsModule::ResolveBuiltinKeyDownFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    const std::vector<std::string_view> parts = SplitTopLevelDelimitedParts(param, ';');
    if (parts.size() != 2) {
        if (context.allowSideEffects && binderModule_) {
            NotifyTagError(UiSettings::Instance().Text(UiText::ToastKeyDownInvalidFormat), 2800.0);
        }
        return std::string();
    }

    const std::optional<int> keyCode = ParseInteger(parts[0]);
    if (!keyCode.has_value() || *keyCode < 1 || *keyCode > 0xFF) {
        if (context.allowSideEffects && binderModule_) {
            NotifyTagError(UiSettings::Instance().Text(UiText::ToastKeyDownInvalidKey), 2800.0);
        }
        return std::string();
    }

    const std::optional<int> durationMs = ParseInteger(parts[1]);
    if (!durationMs.has_value() || *durationMs < 1) {
        if (context.allowSideEffects && binderModule_) {
            NotifyTagError(UiSettings::Instance().Text(UiText::ToastKeyDownInvalidDuration), 2800.0);
        }
        return std::string();
    }

    if (context.allowSideEffects) {
        const std::uint64_t releaseAtMs =
            QueueVirtualKeyHold(static_cast<UINT>(*keyCode), 0, *durationMs);
        if (releaseAtMs != 0 && context.runningBindRuntimeId != 0 && binderModule_) {
            binderModule_->PauseRuntime(context.runningBindRuntimeId);
            QueuePendingKeyHoldWait(context.runningBindRuntimeId, static_cast<unsigned int>(*keyCode), releaseAtMs);
        }
    }
    return std::string();
}

std::optional<std::string> TagsModule::ResolveBuiltinStrLowFunctionTag(
    std::string_view param,
    const EvaluationContext&) const {
    return ToLowerUtf8(param);
}

std::optional<std::string> TagsModule::ResolveBuiltinAddTimeFunctionTag(
    std::string_view param,
    const EvaluationContext&) const {
    const std::optional<std::int64_t> deltaSeconds = ParseTimeOffsetSeconds(param);
    if (!deltaSeconds.has_value()) {
        return std::string();
    }

    const std::time_t now = std::time(nullptr);
    return FormatCurrentTimeForTimestamp(now + static_cast<std::time_t>(*deltaSeconds), "%H:%M:%S");
}

std::optional<std::string> TagsModule::ResolveBuiltinRandomFunctionTag(
    std::string_view param,
    const EvaluationContext&) const {
    const std::vector<std::string_view> rawOptions = SplitTopLevelDelimitedParts(param, ';');
    if (rawOptions.size() > 1) {
        std::uniform_int_distribution<std::size_t> distribution(0, rawOptions.size() - 1);
        return Unquote(Trim(rawOptions[distribution(TagRandomEngine())]));
    }

    const std::string rawValue = Unquote(Trim(param));
    if (rawValue.empty()) {
        std::uniform_int_distribution<int> distribution(kRandomMinInt, kRandomMaxInt);
        return std::to_string(distribution(TagRandomEngine()));
    }

    if (const std::optional<std::pair<int, int>> range = ParseRandomIntegerRange(rawValue); range.has_value()) {
        std::uniform_int_distribution<int> distribution(range->first, range->second);
        return std::to_string(distribution(TagRandomEngine()));
    }

    if (const std::optional<int> parsed = ParseInteger(rawValue); parsed.has_value()) {
        if (*parsed == 0) {
            return std::string("0");
        }

        int minValue = 1;
        int maxValue = *parsed;
        if (maxValue < minValue) {
            std::swap(minValue, maxValue);
        }

        std::uniform_int_distribution<int> distribution(minValue, maxValue);
        return std::to_string(distribution(TagRandomEngine()));
    }

    return rawValue;
}

std::optional<std::string> TagsModule::ResolveBuiltinIfAndOrFunctionTag(
    std::string_view rawParam,
    const EvaluationContext& context,
    int depth) const {
    const IfAndOrSplitResult split = SplitIfAndOrParam(rawParam);
    if (!split.valid) {
        if (context.allowSideEffects && binderModule_) {
            NotifyTagError(UiSettings::Instance().Text(UiText::ToastIfAndOrInvalidSyntax), 2800.0);
        }
        return std::string();
    }

    const std::string trimmedCondition(TrimAsciiWhitespace(split.condition));
    if (trimmedCondition.empty()) {
        if (context.allowSideEffects && binderModule_) {
            NotifyTagError(UiSettings::Instance().Text(UiText::ToastIfAndOrEmptyCondition), 2800.0);
        }
        return std::string();
    }

    EvaluationContext conditionContext = context;
    conditionContext.allowSideEffects = false;
    const std::string expandedCondition = ExpandTextRecursive(trimmedCondition, conditionContext, depth + 1);
    std::string conditionError;
    const std::optional<bool> conditionResult = EvaluateConditionExpression(expandedCondition, conditionError);
    if (!conditionResult.has_value()) {
        if (context.allowSideEffects && binderModule_) {
            NotifyTagError(
                UiSettings::Instance().Format(UiText::ToastIfAndOrConditionFailed, conditionError.c_str()),
                3200.0);
        }
        return std::string();
    }

    const std::string_view branch = *conditionResult ? split.whenTrue : split.whenFalse;
    return ExpandTextRecursive(branch, context, depth + 1);
}

std::optional<std::string> TagsModule::ResolveBuiltinTimefFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    const TimeFormatParseResult parsed = ParseTimefFormat(param);
    if (!parsed.Ok()) {
        if (context.allowSideEffects && binderModule_) {
            NotifyTagError(DescribeTimeFormatError(parsed.error, parsed.invalidToken), 2800.0);
        }
        return std::string();
    }

    const std::string formatted = FormatCurrentTime(parsed.format);
    if (formatted.empty()) {
        if (context.allowSideEffects && binderModule_) {
            NotifyTagError(DescribeTimeFormatError(TimeFormatError::FormatFailed, {}), 2800.0);
        }
        return std::string();
    }
    return formatted;
}

std::optional<std::string> TagsModule::ResolveBuiltinGetVehTypeFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }

    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }

    return ResolveVehicleTypeForPed(FindPlayerPedBySampId(*sampApi, *id));
}

std::optional<std::string> TagsModule::ResolveBuiltinScreenFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    std::string folder = Unquote(Trim(param));
    const ScreenCaptureResult result = CaptureGameScreenshot(folder);
    if (!result.Ok() && binderModule_) {
        NotifyTagError(DescribeScreenCaptureError(result.error, result.detail), 2800.0);
    }
    return std::string();
}

std::optional<std::string> TagsModule::ResolveBuiltinWaitFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    const std::optional<int> delayMs = ParseInteger(param);
    if (!delayMs.has_value() || *delayMs < 0) {
        return std::string();
    }

    if (context.allowSideEffects && context.runningBindRuntimeId != 0) {
        QueuePendingBindDelayOverride(context.runningBindRuntimeId, *delayMs);
    }
    return std::string();
}

std::optional<std::string> TagsModule::ResolveBuiltinDialogCloseFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogCloseNoActive), 2800.0);
        }
        return std::string();
    }

    const std::string rawValue = Unquote(Trim(param));
    const std::optional<int> button = ParseInteger(rawValue);
    if (!button.has_value() || (*button != 0 && *button != 1)) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogCloseInvalidButton), 2800.0);
        }
        return std::string();
    }

    const SampApi::DialogSubmitResult result = sampApi->submitCurrentDialog(*button);
    if (!result.ok && binderModule_) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogCloseFailed), 2800.0);
    }

    return std::string();
}

std::optional<std::string> TagsModule::ResolveBuiltinDialogSetTextFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogSetTextNoActive), 2800.0);
        }
        return std::string();
    }

    const int style = sampApi->GetCurrentDialogStyle();
    if (style < 0 || !sampApi->isDialogInputStyle(style) || !sampApi->pDialogInput_pEditBox_active_func()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogSetTextNoEditbox), 2800.0);
        }
        return std::string();
    }

    const std::string text = Unquote(std::string(param));
    if (!sampApi->sampSetDialogEditboxText(text, false) && binderModule_) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogSetTextFailed), 2800.0);
    }

    return std::string();
}

std::optional<std::string> TagsModule::ResolveBuiltinDialogWaitIdFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!context.allowSideEffects || context.runningBindRuntimeId == 0 || !binderModule_) {
        return std::string();
    }

    const std::string rawValue = Unquote(Trim(param));
    const std::optional<int> dialogId = ParseInteger(rawValue);
    if (!dialogId.has_value() || *dialogId < 0) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogWaitIdInvalidId), 2800.0);
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    const bool dialogMatches = sampApi && sampApi->sampModule() && sampApi->isSupportedVersion() && sampApi->isDialogActive()
        && sampApi->SAMP_DIALOG_ID() == *dialogId;
    if (dialogMatches) {
        return std::string();
    }

    binderModule_->PauseRuntime(context.runningBindRuntimeId);
    const_cast<TagsModule*>(this)->QueuePendingDialogWait(
        context.runningBindRuntimeId,
        PendingDialogWaitKind::SpecificId,
        GetTickCount64() + kDialogWaitOpenTimeoutMs,
        *dialogId);
    return std::string();
}

std::optional<std::string> TagsModule::ResolveBuiltinDialogItemFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogItemNoActive), 2800.0);
        }
        return std::string();
    }

    const std::string rawValue = Unquote(Trim(param));
    if (rawValue.empty()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogItemEmptyParam), 2800.0);
        }
        return std::string();
    }

    std::string error;
    const std::optional<DialogListItems> items = ReadActiveDialogListItems(sampApi, error);
    if (!items.has_value()) {
        if (binderModule_) {
            const UiText textId = error == "not_list" ? UiText::ToastDialogItemNotList : UiText::ToastDialogItemReadFailed;
            NotifyDialogError(UiSettings::Instance().Text(textId), 2800.0);
        }
        return std::string();
    }

    int targetIndex = -1;
    if (const std::optional<int> parsed = ParseInteger(rawValue); parsed.has_value()) {
        targetIndex = *parsed >= 1 ? (*parsed - 1) : *parsed;
    } else if (const std::optional<int> foundIndex = FindDialogItemIndexByText(*items, rawValue); foundIndex.has_value()) {
        targetIndex = *foundIndex;
    } else {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogItemNotFound), 2800.0);
        }
        return std::string();
    }

    if (targetIndex < 0 || targetIndex >= static_cast<int>(items->items.size())) {
        if (binderModule_) {
            NotifyDialogError(
                UiSettings::Instance().Format(UiText::ToastDialogItemOutOfRange, std::to_string(targetIndex + 1).c_str()),
                2800.0);
        }
        return std::string();
    }

    const SampApi::DialogSubmitResult result = sampApi->submitCurrentDialog(1, targetIndex);
    if (!result.ok && binderModule_) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogItemFailed), 2800.0);
    }

    return std::string();
}

std::optional<std::string> TagsModule::ResolveBuiltinDialogSelectFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogSelectNoActive), 2800.0);
        }
        return std::string();
    }

    const std::string rawValue = Unquote(Trim(param));
    if (rawValue.empty()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogSelectEmptyParam), 2800.0);
        }
        return std::string();
    }

    std::string error;
    const std::optional<DialogListItems> items = ReadActiveDialogListItems(sampApi, error);
    if (!items.has_value()) {
        if (binderModule_) {
            const UiText textId = error == "not_list" ? UiText::ToastDialogSelectNotList : UiText::ToastDialogSelectReadFailed;
            NotifyDialogError(UiSettings::Instance().Text(textId), 2800.0);
        }
        return std::string();
    }

    int targetIndex = -1;
    if (const std::optional<int> parsed = ParseInteger(rawValue); parsed.has_value()) {
        targetIndex = *parsed >= 1 ? (*parsed - 1) : *parsed;
    } else if (const std::optional<int> foundIndex = FindDialogItemIndexByText(*items, rawValue); foundIndex.has_value()) {
        targetIndex = *foundIndex;
    } else {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogSelectNotFound), 2800.0);
        }
        return std::string();
    }

    if (targetIndex < 0 || targetIndex >= static_cast<int>(items->items.size())) {
        if (binderModule_) {
            NotifyDialogError(
                UiSettings::Instance().Format(
                    UiText::ToastDialogSelectOutOfRange,
                    std::to_string(targetIndex + 1).c_str()),
                2800.0);
        }
        return std::string();
    }

    if (!sampApi->SetCurrentDialogListItem(targetIndex) && binderModule_) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogSelectFailed), 2800.0);
    }

    return std::string();
}

std::optional<std::string> TagsModule::ResolveBuiltinDialogResponseFunctionTag(
    std::string_view rawParam,
    const EvaluationContext& context,
    int depth) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogResponseNoActive), 2800.0);
        }
        return std::string();
    }

    const DialogResponseParams parsed = ParseDialogResponseParams(rawParam);
    if (!parsed.valid) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogResponseInvalidFormat), 3000.0);
        }
        return std::string();
    }

    EvaluationContext dataContext = context;
    dataContext.allowSideEffects = false;

    const std::string buttonValue = Unquote(TrimAscii(ExpandTextRecursive(parsed.button, dataContext, depth + 1)));
    const std::optional<int> button = ParseInteger(buttonValue);
    if (!button.has_value() || (*button != 0 && *button != 1)) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogResponseInvalidButton), 3000.0);
        }
        return std::string();
    }

    const int style = sampApi->GetCurrentDialogStyle();
    std::optional<int> listItem = std::nullopt;
    std::optional<std::string> inputText = std::nullopt;

    if (*button == 1 && parsed.hasItemPart && style >= 0 && sampApi->isDialogListStyle(style)) {
        const std::string itemValue = Unquote(TrimAscii(ExpandTextRecursive(parsed.item, dataContext, depth + 1)));
        if (!itemValue.empty()) {
            std::string error;
            const std::optional<DialogListItems> items = ReadActiveDialogListItems(sampApi, error);
            if (!items.has_value()) {
                if (binderModule_) {
                    const UiText textId =
                        error == "not_list" ? UiText::ToastDialogResponseItemNotList : UiText::ToastDialogResponseReadFailed;
                    NotifyDialogError(UiSettings::Instance().Text(textId), 3000.0);
                }
                return std::string();
            }

            int targetIndex = -1;
            if (const std::optional<int> parsedIndex = ParseInteger(itemValue); parsedIndex.has_value()) {
                targetIndex = *parsedIndex >= 1 ? (*parsedIndex - 1) : *parsedIndex;
            } else if (const std::optional<int> foundIndex = FindDialogItemIndexByText(*items, itemValue); foundIndex.has_value()) {
                targetIndex = *foundIndex;
            } else {
                if (binderModule_) {
                    NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogResponseItemNotFound), 3000.0);
                }
                return std::string();
            }

            if (targetIndex < 0 || targetIndex >= static_cast<int>(items->items.size())) {
                if (binderModule_) {
                    NotifyDialogError(
                        UiSettings::Instance().Format(
                            UiText::ToastDialogResponseItemOutOfRange,
                            std::to_string(targetIndex + 1).c_str()),
                        3000.0);
                }
                return std::string();
            }

            listItem = targetIndex;
        }
    }

    if (*button == 1 && parsed.hasTextPart && style >= 0 && sampApi->isDialogInputStyle(style)) {
        inputText = Unquote(TrimAscii(ExpandTextRecursive(parsed.text, dataContext, depth + 1)));
    }

    const SampApi::DialogSubmitResult result = sampApi->submitCurrentDialog(*button, listItem, inputText, false);
    if (!result.ok && binderModule_) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogResponseFailed), 3000.0);
    }

    return std::string();
}

std::optional<std::string> TagsModule::ResolveBuiltinDialogTextFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        if (context.allowSideEffects && binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogTextNoActive), 2800.0);
        }
        return std::string();
    }

    const std::string rawValue = Unquote(Trim(param));
    if (rawValue.empty()) {
        if (context.allowSideEffects && binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogTextEmptyParam), 2800.0);
        }
        return std::string();
    }

    const std::optional<int> index = ParseInteger(rawValue);
    if (!index.has_value() || *index < 0) {
        if (context.allowSideEffects && binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogTextInvalidIndex), 2800.0);
        }
        return std::string();
    }

    std::string error;
    const std::optional<DialogTextItems> items = ReadActiveDialogTextItems(sampApi, error);
    if (!items.has_value()) {
        if (context.allowSideEffects && binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogTextReadFailed), 2800.0);
        }
        return std::string();
    }

    if (*index >= static_cast<int>(items->flat.size())) {
        if (context.allowSideEffects && binderModule_) {
            NotifyDialogError(
                UiSettings::Instance().Format(
                    UiText::ToastDialogTextOutOfRange,
                    std::to_string(*index).c_str(),
                    std::to_string(items->flat.empty() ? 0 : static_cast<int>(items->flat.size() - 1)).c_str()),
                2800.0);
        }
        return std::string();
    }

    return items->flat[static_cast<std::size_t>(*index)].text;
}

std::optional<std::string> TagsModule::ResolveBuiltinSaveDialogFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastSaveDialogNoActive), 2800.0);
        }
        return std::string();
    }

    const std::optional<std::filesystem::path> helperDataPath = helper_paths::ResolveHelperDataDirectory();
    if (!helperDataPath.has_value()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastSaveDialogDocumentsUnavailable), 2800.0);
        }
        return std::string();
    }

    const std::filesystem::path targetDirectory = GetHelperSavedDialogsRoot(*helperDataPath);
    std::error_code directoryError;
    std::filesystem::create_directories(targetDirectory, directoryError);
    if (directoryError) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastSaveDialogCreateDirFailed), 2800.0);
        }
        return std::string();
    }

    const std::string caption = NormalizeDialogCaptionVisibleText(sampApi->get_dialog_caption());
    std::string desiredName = Unquote(Trim(param));
    if (desiredName.empty()) {
        desiredName = caption;
    }
    if (desiredName.empty()) {
        const int dialogId = sampApi->SAMP_DIALOG_ID();
        desiredName = dialogId >= 0 ? "dialog_" + std::to_string(dialogId) : std::string("dialog");
    }

    std::wstring stem = Utf8ToWide(desiredName);
    if (stem.empty()) {
        stem = L"dialog";
    }

    const std::filesystem::path targetPath = MakeUniqueTextFilePath(targetDirectory, std::move(stem));
    std::ofstream stream(targetPath, std::ios::binary);
    if (!stream.is_open()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastSaveDialogWriteFailed), 2800.0);
        }
        return std::string();
    }

    const int dialogId = sampApi->SAMP_DIALOG_ID();
    const int dialogStyle = sampApi->GetCurrentDialogStyle();
    const int selectedIndex = sampApi->GetCurrentDialogListItem();
    const SampApi::DialogSelectionText selection = sampApi->getDialogSelectedItemText();
    const std::string editboxText = sampApi->sampGetDialogEditboxText();
    const std::string dialogText = sampApi->sampGetDialogText();

    stream << "Caption: " << caption << "\r\n";
    stream << "Dialog ID: " << dialogId << "\r\n";
    stream << "Style: " << DialogStyleName(dialogStyle) << " (" << dialogStyle << ")\r\n";
    if (selectedIndex >= 0) {
        stream << "Selected Item Index: " << selectedIndex << "\r\n";
    }
    if (selection.found) {
        stream << "Selected Item Text: " << selection.text << "\r\n";
    }
    if (!editboxText.empty()) {
        stream << "Editbox Text: " << editboxText << "\r\n";
    }
    stream << "\r\n----- Dialog Text -----\r\n";
    stream << dialogText;
    stream.flush();

    if (!stream.good()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastSaveDialogWriteFailed), 2800.0);
        }
        return std::string();
    }

    if (binderModule_) {
        std::string savedPath = WideToMultiByte(targetPath.native(), CP_UTF8);
        if (savedPath.empty()) {
            savedPath = WideToMultiByte(targetPath.filename().native(), CP_UTF8);
        }
        NotifySuccess(
            UiSettings::Instance().Format(UiText::ToastSaveDialogSuccess, savedPath.c_str()),
            2800.0);
    }

    return std::string();
}

std::optional<std::string> TagsModule::ResolveBinderActionFunctionTag(
    std::string_view action,
    std::string_view param,
    const EvaluationContext& context) const {
    if (!binderModule_) {
        return action == "ended" ? std::string("0") : std::string();
    }
    if (!context.allowSideEffects && action != "ended") {
        return std::string();
    }

    const BinderModule::TagActionResult result =
        binderModule_->ExecuteTagAction(action, param, context.runningBindRuntimeId);
    if (action == "ended") {
        return result.value.empty() ? std::string("0") : result.value;
    }
    return result.value;
}

std::optional<std::string> TagsModule::ResolveSimpleTag(std::string_view name, const EvaluationContext& context) const {
    const std::string normalized = ToLower(name);

    for (const auto& [customName, customValue] : customVariables_) {
        if (normalized == ToLower(customName)) {
            return customValue;
        }
    }

    if (const TagEntry* entry = tagRegistry_.Find(TagKind::Simple, normalized);
        entry && entry->simpleResolver) {
        return entry->simpleResolver(*this, context);
    }

    return std::nullopt;
}

std::optional<std::string> TagsModule::ResolveFunctionTag(
    std::string_view name,
    std::string_view param,
    const EvaluationContext& context,
    int depth) const {
    const std::string normalized = ToLower(name);
    if (const TagEntry* entry = tagRegistry_.Find(TagKind::Function, normalized);
        entry && entry->functionResolver) {
        if (normalized == "ifandor" || normalized == "dialogresponse") {
            return entry->functionResolver(*this, param, context, depth);
        }
        return entry->functionResolver(*this, ExpandTextRecursive(param, context, depth + 1), context, depth);
    }

    return std::nullopt;
}

std::string TagsModule::ExpandSimpleTags(std::string_view text, const EvaluationContext& context) const {
    std::string output;
    output.reserve(text.size());

    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t start = text.find('{', pos);
        if (start == std::string_view::npos) {
            output.append(text.substr(pos));
            break;
        }

        output.append(text.substr(pos, start - pos));
        const std::size_t end = text.find('}', start + 1);
        if (end == std::string_view::npos) {
            output.append(text.substr(start));
            break;
        }

        const std::string_view name = text.substr(start + 1, end - start - 1);
        bool validName = !name.empty();
        for (const unsigned char ch : name) {
            if (std::isalnum(ch) == 0 && ch != '_') {
                validName = false;
                break;
            }
        }

        if (!validName) {
            output.append(text.substr(start, end - start + 1));
            pos = end + 1;
            continue;
        }

        if (const std::optional<std::string> value = ResolveSimpleTag(name, context); value.has_value()) {
            output += *value;
        } else {
            output.append(text.substr(start, end - start + 1));
        }
        pos = end + 1;
    }

    return output;
}

std::string TagsModule::ExpandFunctionTags(std::string_view text, const EvaluationContext& context, int depth) const {
    std::string output;
    output.reserve(text.size());

    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t start = text.find('[', pos);
        if (start == std::string_view::npos) {
            output.append(text.substr(pos));
            break;
        }

        output.append(text.substr(pos, start - pos));

        std::size_t nameEnd = start + 1;
        while (nameEnd < text.size()) {
            const unsigned char ch = static_cast<unsigned char>(text[nameEnd]);
            if (std::isalnum(ch) == 0 && ch != '_') {
                break;
            }
            ++nameEnd;
        }

        if (nameEnd == start + 1) {
            output.push_back(text[start]);
            pos = start + 1;
            continue;
        }

        std::size_t openParen = nameEnd;
        while (openParen < text.size() && std::isspace(static_cast<unsigned char>(text[openParen])) != 0) {
            ++openParen;
        }
        if (openParen < text.size() && text[openParen] == ']') {
            const std::string_view name = text.substr(start + 1, nameEnd - start - 1);
            if (const std::optional<std::string> value = ResolveFunctionTag(name, {}, context, depth);
                value.has_value()) {
                output += *value;
            } else {
                output.append(text.substr(start, openParen - start + 1));
            }
            pos = openParen + 1;
            continue;
        }
        if (openParen >= text.size() || text[openParen] != '(') {
            output.append(text.substr(start, openParen - start));
            pos = openParen;
            continue;
        }

        int parenDepth = 1;
        std::size_t cursor = openParen + 1;
        for (; cursor < text.size(); ++cursor) {
            if (text[cursor] == '(') {
                ++parenDepth;
            } else if (text[cursor] == ')') {
                --parenDepth;
                if (parenDepth == 0) {
                    break;
                }
            }
        }

        if (cursor >= text.size() || cursor + 1 >= text.size() || text[cursor + 1] != ']') {
            output.append(text.substr(start));
            break;
        }

        const std::string_view name = text.substr(start + 1, nameEnd - start - 1);
        const std::string_view rawParam = text.substr(openParen + 1, cursor - openParen - 1);
        if (const std::optional<std::string> value = ResolveFunctionTag(
                name,
                rawParam,
                context,
                depth);
            value.has_value()) {
            output += *value;
        } else {
            output.append(text.substr(start, cursor - start + 2));
        }
        pos = cursor + 2;
    }

    return output;
}

std::string TagsModule::ExpandTextRecursive(std::string_view text, const EvaluationContext& context, int depth) const {
    if (depth > kRecursionLimit) {
        return std::string(text);
    }

    const std::string withFunctions = ExpandFunctionTags(text, context, depth);
    return ExpandSimpleTags(withFunctions, context);
}

std::string TagsModule::ExpandText(std::string_view text) const {
    return ExpandText(text, ResolveActiveContext());
}

std::string TagsModule::ExpandText(std::string_view text, const EvaluationContext& context) const {
    EvaluationContext effective = context;
    if (!effective.sampApi) {
        effective.sampApi = sampApi_;
    }
    return ExpandTextRecursive(text, effective, 0);
}

std::string TagsModule::ExpandHudText(std::string_view text) const {
    EvaluationContext context = ResolveActiveContext("hud", {});
    context.allowSideEffects = false;
    return ExpandText(text, context);
}

std::string TagsModule::ExpandOutgoingText(
    std::string_view text,
    std::string_view activationSource,
    std::string_view activationText) const {
    return ExpandText(text, ResolveActiveContext(activationSource, activationText));
}

void TagsModule::OpenKeyEmulatePicker() {
    keyPickerSearchQuery_.clear();
    keyPickerHoverTriggered_ = true;
    ImGui::OpenPopup("##tags_keyemulate_picker");
}

void TagsModule::OpenDialogItemPicker() {
    dialogItemPickerSearchQuery_.clear();
    dialogItemPickerOpenPending_ = true;
}

void TagsModule::OpenDialogTextPicker() {
    dialogTextPickerSearchQuery_.clear();
    dialogTextPickerOpenPending_ = true;
}

void TagsModule::DrawKeyEmulatePickerPopup() {
    UiSettings& ui = UiSettings::Instance();

    ImGui::SetNextWindowSize(ScaleUi(560.0f, 520.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("##tags_keyemulate_picker")) {
        if (!ImGui::IsPopupOpen("##tags_keyemulate_picker")) {
            keyPickerHoverTriggered_ = false;
        }
        return;
    }

    ImGui::TextUnformatted(ui.Text(UiText::MiscVariablesKeyPickerTitle));
    ImGui::TextWrapped("%s", ui.Text(UiText::MiscVariablesKeyPickerIntro));
    ImGui::Separator();

    InputTextWithHintString(
        "##tags_keyemulate_search",
        ui.Text(UiText::MiscVariablesKeyPickerSearchHint),
        keyPickerSearchQuery_,
        ImGuiInputTextFlags_AutoSelectAll,
        96);
    ImGui::Spacing();

    const std::string filter = ToLower(keyPickerSearchQuery_);
    DrawSearchableTokenList(
        "##tags_keyemulate_picker_list",
        GetVirtualKeyPickerEntries(),
        filter,
        UiText::MiscVariablesKeyPickerEmpty,
        keyPickerSearchQuery_,
        [](const TagsModule::VirtualKeyPickerEntry& entry) -> const std::string& { return entry.search; },
        [](const TagsModule::VirtualKeyPickerEntry& entry) -> const std::string& { return entry.label; },
        [](const TagsModule::VirtualKeyPickerEntry& entry) { return MakeKeyEmulateTokenImpl(entry.code); });

    ImGui::Spacing();
    ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesKeyPickerCopyHint));

    if (!ImGui::IsPopupOpen("##tags_keyemulate_picker")) {
        keyPickerHoverTriggered_ = false;
    }
    ImGui::EndPopup();
}

void TagsModule::DrawDialogItemPickerPopup() {
    if (dialogItemPickerOpenPending_) {
        ImGui::OpenPopup("##tags_dialogitem_picker");
        dialogItemPickerOpenPending_ = false;
    }

    UiSettings& ui = UiSettings::Instance();
    ImGui::SetNextWindowSize(ScaleUi(620.0f, 540.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("##tags_dialogitem_picker")) {
        return;
    }

    ImGui::TextUnformatted(ui.Text(UiText::MiscVariablesDialogItemPickerTitle));
    ImGui::TextWrapped("%s", ui.Text(UiText::MiscVariablesDialogItemPickerIntro));
    ImGui::Separator();

    InputTextWithHintString(
        "##tags_dialogitem_search",
        ui.Text(UiText::MiscVariablesDialogItemPickerSearchHint),
        dialogItemPickerSearchQuery_,
        ImGuiInputTextFlags_AutoSelectAll,
        128);
    ImGui::Spacing();

    std::string error;
    const std::optional<DialogListItems> items = ReadActiveDialogListItems(sampApi_, error);
    if (!items.has_value()) {
        const UiText errorText = error == "not_list" ? UiText::MiscVariablesDialogItemPickerNotList : UiText::MiscVariablesDialogItemPickerNoDialog;
        ImGui::TextDisabled("%s", ui.Text(errorText));
        ImGui::EndPopup();
        return;
    }

    const std::string caption = sampApi_ ? NormalizeDialogCaptionVisibleText(sampApi_->get_dialog_caption()) : std::string();
    if (!caption.empty()) {
        ImGui::TextDisabled("%s", ui.Format(UiText::MiscVariablesDialogItemPickerCaptionLabel, caption.c_str()).c_str());
    }
    if (!items->headerText.empty()) {
        ImGui::TextWrapped("%s", ui.Format(UiText::MiscVariablesDialogItemPickerHeaderLabel, items->headerText.c_str()).c_str());
    }
    ImGui::Spacing();

    const std::string filter = ToLowerUtf8(dialogItemPickerSearchQuery_);
    DrawSearchableTokenList(
        "##tags_dialogitem_picker_list",
        items->items,
        filter,
        UiText::MiscVariablesDialogItemPickerEmpty,
        dialogItemPickerSearchQuery_,
        [](const DialogListItemInfo& item) {
            return ToLowerUtf8(std::to_string(item.index1) + " " + item.text + " " + item.rawText);
        },
        [](const DialogListItemInfo& item) {
            const std::string visibleText = item.text.empty() ? item.rawText : item.text;
            return std::to_string(item.index1) + " - " + visibleText;
        },
        [](const DialogListItemInfo& item) {
            return "[dialogitem(" + std::to_string(item.index1) + ")]";
        });

    ImGui::Spacing();
    ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesDialogItemPickerCopyHint));
    ImGui::EndPopup();
}

void TagsModule::DrawDialogTextPickerPopup() {
    if (dialogTextPickerOpenPending_) {
        ImGui::OpenPopup("##tags_dialogtext_picker");
        dialogTextPickerOpenPending_ = false;
    }

    UiSettings& ui = UiSettings::Instance();
    ImGui::SetNextWindowSize(ScaleUi(620.0f, 540.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("##tags_dialogtext_picker")) {
        return;
    }

    ImGui::TextUnformatted(ui.Text(UiText::MiscVariablesDialogTextPickerTitle));
    ImGui::TextWrapped("%s", ui.Text(UiText::MiscVariablesDialogTextPickerIntro));
    ImGui::Separator();

    InputTextWithHintString(
        "##tags_dialogtext_search",
        ui.Text(UiText::MiscVariablesDialogTextPickerSearchHint),
        dialogTextPickerSearchQuery_,
        ImGuiInputTextFlags_AutoSelectAll,
        128);
    ImGui::Spacing();

    std::string error;
    const std::optional<DialogTextItems> items = ReadActiveDialogTextItems(sampApi_, error);
    if (!items.has_value()) {
        ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesDialogTextPickerNoDialog));
        ImGui::EndPopup();
        return;
    }

    const std::string caption = sampApi_ ? NormalizeDialogCaptionVisibleText(sampApi_->get_dialog_caption()) : std::string();
    if (!caption.empty()) {
        ImGui::TextDisabled("%s", ui.Format(UiText::MiscVariablesDialogTextPickerCaptionLabel, caption.c_str()).c_str());
    }
    ImGui::TextDisabled(
        "%s",
        ui.Format(UiText::MiscVariablesDialogTextPickerCountLabel, std::to_string(items->flat.size()).c_str()).c_str());
    ImGui::Spacing();

    const std::string filter = ToLowerUtf8(dialogTextPickerSearchQuery_);
    DrawSearchableTokenList(
        "##tags_dialogtext_picker_list",
        items->flat,
        filter,
        UiText::MiscVariablesDialogTextPickerEmpty,
        dialogTextPickerSearchQuery_,
        [](const DialogTextToken& token) {
            return ToLowerUtf8(std::to_string(token.index) + " " + token.text);
        },
        [](const DialogTextToken& token) {
            return std::to_string(token.index) + " - " + token.text;
        },
        [](const DialogTextToken& token) {
            return "[dialogtext(" + std::to_string(token.index) + ")]";
        });

    ImGui::Spacing();
    ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesDialogTextPickerCopyHint));
    ImGui::EndPopup();
}

void TagsModule::DrawMiscHomePage() {
    UiSettings& ui = UiSettings::Instance();

    ImGui::SeparatorText(ui.Text(UiText::TabMisc));
    ImGui::TextWrapped("%s", ui.Text(UiText::MiscHomeIntro));
    ImGui::Spacing();

    if (DrawNavigationCardButton(
            "##misc_variables_card",
            ui.Text(UiText::MiscVariablesTitle),
            ui.Text(UiText::MiscVariablesEntryDesc),
            ui.Text(UiText::MiscOpenSectionAction),
            ImVec4(0.97f, 0.83f, 0.46f, 1.0f),
            136.0f)) {
        currentPage_ = MiscPage::Variables;
    }
}

void TagsModule::DrawVariablesPage() {
    UiSettings& ui = UiSettings::Instance();

    if (ImGui::Button(ui.Text(UiText::EditorBack), ScaleUi(120.0f, 0.0f))) {
        currentPage_ = MiscPage::Home;
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ui.Text(UiText::TabMisc));
    ImGui::Spacing();

    ImGui::SeparatorText(ui.Text(UiText::MiscVariablesTitle));
    ImGui::TextWrapped("%s", ui.Text(UiText::MiscVariablesIntro));
    ImGui::Spacing();

    if (ImGui::BeginChild("##tags_overview_card", ImVec2(0.0f, ScaleUi(120.0f)), ImGuiChildFlags_FrameStyle)) {
        ImGui::TextColored(ImVec4(0.97f, 0.83f, 0.46f, 1.0f), "%s", ui.Text(UiText::MiscVariablesCardTitle));
        ImGui::Spacing();
        ImGui::TextWrapped("%s", ui.Text(UiText::MiscVariablesCardDesc));
        ImGui::Spacing();
        if (ImGui::BeginTable("##tags_overview_stats", 5, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawStatBlock(
                ui.Text(UiText::MiscVariablesBuiltinsLabel),
                std::to_string(tagRegistry_.Entries().size()),
                ImVec4(0.97f, 0.83f, 0.46f, 1.0f));
            ImGui::TableSetColumnIndex(1);
            DrawStatBlock(
                ui.Text(UiText::MiscVariablesSimpleLabel),
                std::to_string(tagRegistry_.Count(TagKind::Simple)),
                ImVec4(0.55f, 0.86f, 0.98f, 1.0f));
            ImGui::TableSetColumnIndex(2);
            DrawStatBlock(
                ui.Text(UiText::MiscVariablesFunctionLabel),
                std::to_string(tagRegistry_.Count(TagKind::Function)),
                ImVec4(0.75f, 0.92f, 0.62f, 1.0f));
            ImGui::TableSetColumnIndex(3);
            DrawStatBlock(
                ui.Text(UiText::MiscVariablesCustomLabel),
                std::to_string(customVariables_.size()),
                ImVec4(0.88f, 0.70f, 0.96f, 1.0f));
            ImGui::TableSetColumnIndex(4);
            DrawStatBlock(
                ui.Text(UiText::MiscVariablesConfigLabel),
                std::string(kTagsSectionName),
                ImVec4(0.92f, 0.92f, 0.92f, 1.0f));
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::BeginTable(
            "##tags_main_layout",
            2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings,
            ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("catalog", ImGuiTableColumnFlags_WidthStretch, 0.48f);
        ImGui::TableSetupColumn("inspector", ImGuiTableColumnFlags_WidthStretch, 0.52f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::BeginChild("##tags_catalog_card", ImVec2(0.0f, 0.0f), ImGuiChildFlags_FrameStyle)) {
            ImGui::SeparatorText(ui.Text(UiText::MiscVariablesCatalogTitle));
            InputTextWithHintString(
                "##tags_search_query",
                ui.Text(UiText::MiscVariablesSearchHint),
                searchQuery_,
                ImGuiInputTextFlags_AutoSelectAll,
                128);
            ImGui::Spacing();

            std::vector<int> visibleIndices;
            const std::string query = ToLower(searchQuery_);
            const auto& entries = tagRegistry_.Entries();
            for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
                const TagEntry& tag = entries[static_cast<std::size_t>(i)];
                const std::string haystack = ToLower(std::string(tag.token) + " " + ui.Text(tag.descriptionText));
                if (query.empty() || haystack.find(query) != std::string::npos) {
                    visibleIndices.push_back(i);
                }
            }

            if (ImGui::BeginChild("##tags_catalog_list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
                if (visibleIndices.empty()) {
                    ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesCatalogEmpty));
                } else if (ImGui::BeginTable(
                               "##tags_catalog_table",
                               2,
                               ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoSavedSettings,
                               ImVec2(0.0f, 0.0f))) {
                    ImGui::TableSetupColumn("kind", ImGuiTableColumnFlags_WidthFixed, ScaleUi(148.0f));
                    ImGui::TableSetupColumn("token", ImGuiTableColumnFlags_WidthStretch);

                    for (const int index : visibleIndices) {
                        const TagEntry& tag = entries[static_cast<std::size_t>(index)];
                        const bool selected = selectedTagIndex_ == index;

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("%s", TagKindLabel(tag.kind, ui));

                        ImGui::TableSetColumnIndex(1);
                        if (ImGui::Selectable(tag.token.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                            selectedTagIndex_ = index;
                        }
                        if (ImGui::IsItemHovered() || ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip("%s", ui.Text(tag.descriptionText));
                        }
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        if (ImGui::BeginChild("##tags_inspector_card", ImVec2(0.0f, 0.0f), ImGuiChildFlags_FrameStyle)) {
            ImGui::SeparatorText(ui.Text(UiText::MiscVariablesInspectorTitle));
            const TagEntry* selectedTag = tagRegistry_.FindByIndex(selectedTagIndex_);
            if (!selectedTag) {
                ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesInspectorEmpty));
            } else {
                ImGui::TextColored(ImVec4(0.55f, 0.86f, 0.98f, 1.0f), "%s", selectedTag->token.c_str());
                if (selectedTag->kind == TagKind::Function && std::string_view(selectedTag->name) == "keyemulate") {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(" + ")) {
                        OpenKeyEmulatePicker();
                    }

                    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
                    if (hovered && !keyPickerHoverTriggered_ && !ImGui::IsPopupOpen("##tags_keyemulate_picker")) {
                        OpenKeyEmulatePicker();
                    } else if (!hovered && !ImGui::IsPopupOpen("##tags_keyemulate_picker")) {
                        keyPickerHoverTriggered_ = false;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", ui.Text(UiText::MiscVariablesKeyPickerOpenHint));
                    }
                }
                if (selectedTag->kind == TagKind::Function && std::string_view(selectedTag->name) == "dialogitem") {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(" +##dialogitem_picker")) {
                        OpenDialogItemPicker();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", ui.Text(UiText::MiscVariablesDialogItemPickerOpenHint));
                    }
                }
                if (selectedTag->kind == TagKind::Function && std::string_view(selectedTag->name) == "dialogtext") {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(" +##dialogtext_picker")) {
                        OpenDialogTextPicker();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", ui.Text(UiText::MiscVariablesDialogTextPickerOpenHint));
                    }
                }
                ImGui::TextDisabled("%s", TagKindLabel(selectedTag->kind, ui));
                ImGui::Separator();

                ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesDescriptionLabel));
                ImGui::TextWrapped("%s", ui.Text(selectedTag->descriptionText));
                ImGui::Spacing();

                ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesExampleLabel));
                ImGui::TextWrapped("%s", selectedTag->example.c_str());
                ImGui::Spacing();

                if (ImGui::Button(ui.Text(UiText::MiscVariablesCopyToken), ScaleUi(170.0f, 0.0f))) {
                    ImGui::SetClipboardText(selectedTag->token.c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button(ui.Text(UiText::MiscVariablesCopyExample), ScaleUi(170.0f, 0.0f))) {
                    ImGui::SetClipboardText(selectedTag->example.c_str());
                }

                if (selectedTag->kind == TagKind::Function && std::string_view(selectedTag->name) == "paramcmd") {
                    ImGui::Spacing();
                    ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesParamcmdNote));
                }
                if (selectedTag->kind == TagKind::Function && std::string_view(selectedTag->name) == "keyemulate") {
                    ImGui::Spacing();
                    ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesKeyEmulateNote));
                }
            }

            DrawKeyEmulatePickerPopup();
            DrawDialogItemPickerPopup();
            DrawDialogTextPickerPopup();
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
}

bool TagsModule::IsMiscHomePage() const {
    return currentPage_ == MiscPage::Home;
}

void TagsModule::DrawMiscTab() {
    if (currentPage_ == MiscPage::Variables) {
        DrawVariablesPage();
        return;
    }

    DrawMiscHomePage();
}
