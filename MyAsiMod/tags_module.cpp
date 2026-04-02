#include "tags_module.h"

#include "app_config.h"
#include "binder_module.h"
#include "hotkey_utils.h"
#include "json_utils.h"
#include "samp_api.h"

#include <game_sa/CSprite.h>
#include <extensions/ScriptCommands.h>
#include <game_sa/eScriptCommands.h>
#include <game_sa/CPools.h>
#include <game_sa/common.h>
#include <RenderWare.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <ShlObj.h>

namespace {

constexpr std::string_view kTagsSectionName = "tags";
constexpr std::string_view kCustomVarsKey = "custom_vars";
constexpr int kRecursionLimit = 10;
constexpr int kKeyEmulateStartDelayMs = 20;
constexpr int kKeyEmulateTapMs = 35;
constexpr float kClosestScreenTargetZOffset = 0.9f;
constexpr unsigned int kAnsiCodePage = CP_ACP;
constexpr std::uintptr_t kTakeScreenshotAddress = 0x5D0820;
constexpr wchar_t kHelperScreensRelativePath[] = L"GTA San Andreas User Files\\HelperByOrc\\screens";
thread_local std::vector<TagsModule::OwnedEvaluationContext> g_activeContextStack;

enum class ScreenCaptureError {
    None,
    DocumentsUnavailable,
    InvalidFolder,
    CaptureFailed,
};

struct ScreenCaptureResult {
    ScreenCaptureError error = ScreenCaptureError::None;
    std::string detail{};
    std::filesystem::path savedPath{};

    bool Ok() const {
        return error == ScreenCaptureError::None;
    }
};

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

std::string ToLowerAscii(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const unsigned char ch : value) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

struct VirtualKeyPickerEntry {
    UINT code = 0;
    std::string label{};
    std::string search{};
};

std::string MakeKeyEmulateToken(UINT keyCode) {
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

std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }

    const int wideLength = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (wideLength <= 0) {
        return {};
    }

    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            wide.data(),
            wideLength)
        <= 0) {
        return {};
    }

    return wide;
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

std::optional<std::filesystem::path> GetDocumentsFolder() {
    PWSTR rawPath = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &rawPath);
    if (FAILED(hr) || !rawPath) {
        if (rawPath) {
            CoTaskMemFree(rawPath);
        }
        return std::nullopt;
    }

    std::filesystem::path path(rawPath);
    CoTaskMemFree(rawPath);
    return path;
}

std::filesystem::path GetHelperScreensRoot(const std::filesystem::path& documentsPath) {
    return documentsPath / kHelperScreensRelativePath;
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
    const std::optional<std::filesystem::path> documentsPath = GetDocumentsFolder();
    if (!documentsPath.has_value()) {
        return ScreenCaptureResult{ ScreenCaptureError::DocumentsUnavailable };
    }

    std::filesystem::path targetDirectory = GetHelperScreensRoot(*documentsPath);
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

const std::vector<VirtualKeyPickerEntry>& GetVirtualKeyPickerEntries() {
    static const std::vector<VirtualKeyPickerEntry> entries = [] {
        std::vector<VirtualKeyPickerEntry> built;
        built.reserve(255);
        for (UINT keyCode = 1; keyCode <= 0xFF; ++keyCode) {
            const std::string name = hotkeys::KeyName(keyCode);
            const std::string label = std::to_string(keyCode) + " - " + name;
            built.push_back(VirtualKeyPickerEntry{
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

} // namespace

TagsModule::TagsModule() = default;

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

    tagRegistry_.RegisterFunction(
        "nick",
        "[nick(...)]",
        "[nick(15)]",
        UiText::TagsBuiltinNickFunctionDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context) {
            return module.ResolveBuiltinNickFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "paramcmd",
        "[paramcmd(...)]",
        "[paramcmd(1+)]",
        UiText::TagsBuiltinParamcmdDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context) {
            return module.ResolveBuiltinParamcmdFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "keyemulate",
        "[keyemulate(...)]",
        "[keyemulate(87)]",
        UiText::TagsBuiltinKeyEmulateDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context) {
            return module.ResolveBuiltinKeyEmulateFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "math",
        "[math(...)]",
        "[math(2+2)]",
        UiText::TagsBuiltinMathDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context) {
            return module.ResolveBuiltinMathFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "screen",
        "[screen(...)]",
        "[screen(Пример)]",
        UiText::TagsBuiltinScreenFunctionDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context) {
            return module.ResolveBuiltinScreenFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "wait",
        "[wait(...)]",
        "[wait(1000)]",
        UiText::TagsBuiltinWaitDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context) {
            return module.ResolveBuiltinWaitFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "binddisable",
        "[binddisable(...)]",
        "[binddisable({thisbind})]",
        UiText::TagsBuiltinBindDisableDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context) {
            return module.ResolveBinderActionFunctionTag("disable", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindenable",
        "[bindenable(...)]",
        "[bindenable(\"10\" \"folder\")]",
        UiText::TagsBuiltinBindEnableDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context) {
            return module.ResolveBinderActionFunctionTag("enable", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindstart",
        "[bindstart(...)]",
        "[bindstart(\"10\" \"folder\")]",
        UiText::TagsBuiltinBindStartDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context) {
            return module.ResolveBinderActionFunctionTag("start", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindstop",
        "[bindstop(...)]",
        "[bindstop({thisbind})]",
        UiText::TagsBuiltinBindStopDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context) {
            return module.ResolveBinderActionFunctionTag("stop", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindpause",
        "[bindpause(...)]",
        "[bindpause({thisbind})]",
        UiText::TagsBuiltinBindPauseDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context) {
            return module.ResolveBinderActionFunctionTag("pause", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindunpause",
        "[bindunpause(...)]",
        "[bindunpause({thisbind})]",
        UiText::TagsBuiltinBindUnpauseDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context) {
            return module.ResolveBinderActionFunctionTag("unpause", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindfastmenu",
        "[bindfastmenu(...)]",
        "[bindfastmenu(\"10\" \"folder\")]",
        UiText::TagsBuiltinBindFastMenuDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context) {
            return module.ResolveBinderActionFunctionTag("fastmenu", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindunfastmenu",
        "[bindunfastmenu(...)]",
        "[bindunfastmenu(\"10\" \"folder\")]",
        UiText::TagsBuiltinBindUnfastMenuDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context) {
            return module.ResolveBinderActionFunctionTag("unfastmenu", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindrandom",
        "[bindrandom(...)]",
        "[bindrandom(\"folder\")]",
        UiText::TagsBuiltinBindRandomDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context) {
            return module.ResolveBinderActionFunctionTag("random", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindended",
        "[bindended(...)]",
        "[bindended({thisbind})]",
        UiText::TagsBuiltinBindEndedDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context) {
            return module.ResolveBinderActionFunctionTag("ended", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindpopup",
        "[bindpopup(...)]",
        "[bindpopup(\"10\" \"folder\")]",
        UiText::TagsBuiltinBindPopupDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context) {
            return module.ResolveBinderActionFunctionTag("popup", param, context);
        });
}

void TagsModule::OnProcessAttach() {
    InitializeRegistry();
    LoadConfig();
    ResetTargetTracker();
    currentPage_ = MiscPage::Home;
    if (selectedTagIndex_ < 0 || selectedTagIndex_ >= static_cast<int>(tagRegistry_.Entries().size())) {
        selectedTagIndex_ = 0;
    }
}

void TagsModule::Shutdown() {
    for (ActiveVirtualKeyHold& hold : activeVirtualKeyHolds_) {
        ReleaseVirtualKeyHold(hold);
    }
    activeVirtualKeyHolds_.clear();
    pendingBindDelayOverrides_.clear();
    searchQuery_.clear();
    ResetTargetTracker();
    currentPage_ = MiscPage::Home;
    g_activeContextStack.clear();
}

void TagsModule::SetSampApi(SampApi* sampApi) {
    sampApi_ = sampApi;
}

void TagsModule::SetBinderModule(BinderModule* binderModule) {
    binderModule_ = binderModule;
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
    if (activeVirtualKeyHolds_.empty()) {
        return;
    }

    const std::uint64_t now = GetTickCount64();
    for (auto it = activeVirtualKeyHolds_.begin(); it != activeVirtualKeyHolds_.end();) {
        ActiveVirtualKeyHold& hold = *it;
        if (!hold.pressed && now >= hold.pressAtMs) {
            hold.pressed = SendVirtualKeyEvent(hold.keyCode, false);
            if (!hold.pressed) {
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

void TagsModule::LoadConfig() {
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
}

void TagsModule::SaveConfig() const {
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

void TagsModule::QueueVirtualKeyHold(unsigned int keyCode, int startDelayMs, int holdDurationMs) const {
    if (keyCode == 0 || keyCode > 0xFF) {
        return;
    }

    const std::uint64_t now = GetTickCount64();
    const std::uint64_t pressAtMs = now + static_cast<std::uint64_t>(std::max(startDelayMs, 0));
    const std::uint64_t releaseAtMs = pressAtMs + static_cast<std::uint64_t>(std::max(holdDurationMs, 1));

    for (auto it = activeVirtualKeyHolds_.begin(); it != activeVirtualKeyHolds_.end();) {
        if (it->keyCode == keyCode) {
            ReleaseVirtualKeyHold(*it);
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

std::string TagsModule::FormatCurrentTime(const char* format) {
    if (!format || *format == '\0') {
        return {};
    }

    std::time_t now = std::time(nullptr);
    std::tm localTime{};
    if (localtime_s(&localTime, &now) != 0) {
        return {};
    }

    char buffer[64]{};
    if (std::strftime(buffer, sizeof(buffer), format, &localTime) == 0) {
        return {};
    }
    return buffer;
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

std::optional<std::string> TagsModule::ResolveBuiltinScreenTag(const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    const ScreenCaptureResult result = CaptureGameScreenshot({});
    if (!result.Ok() && binderModule_) {
        binderModule_->ShowToast(DescribeScreenCaptureError(result.error, result.detail), true, 2800.0);
    }
    return std::string();
}

std::optional<std::string> TagsModule::ResolveBuiltinTPhotoTag(const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    if (!TakeGameCameraPhoto() && binderModule_) {
        binderModule_->ShowToast(UiSettings::Instance().Text(UiText::ToastTPhotoFailed), true, 2800.0);
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

std::optional<std::string> TagsModule::ResolveBuiltinNickFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }
    return ResolvePlayerNickById(*id, context);
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

std::optional<std::string> TagsModule::ResolveBuiltinScreenFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    std::string folder = Unquote(Trim(param));
    const ScreenCaptureResult result = CaptureGameScreenshot(folder);
    if (!result.Ok() && binderModule_) {
        binderModule_->ShowToast(DescribeScreenCaptureError(result.error, result.detail), true, 2800.0);
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
    const EvaluationContext& context) const {
    const std::string normalized = ToLower(name);
    if (const TagEntry* entry = tagRegistry_.Find(TagKind::Function, normalized);
        entry && entry->functionResolver) {
        return entry->functionResolver(*this, param, context);
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
                ExpandTextRecursive(rawParam, context, depth + 1),
                context);
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
    bool hasMatches = false;
    if (ImGui::BeginChild("##tags_keyemulate_picker_list", ScaleUi(0.0f, 360.0f), ImGuiChildFlags_Borders)) {
        for (const VirtualKeyPickerEntry& entry : GetVirtualKeyPickerEntries()) {
            if (!filter.empty() && entry.search.find(filter) == std::string::npos) {
                continue;
            }

            hasMatches = true;
            if (ImGui::Selectable(entry.label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                const std::string token = MakeKeyEmulateToken(entry.code);
                ImGui::SetClipboardText(token.c_str());
                keyPickerSearchQuery_.clear();
                ImGui::CloseCurrentPopup();
            }
        }

        if (!hasMatches) {
            ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesKeyPickerEmpty));
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesKeyPickerCopyHint));

    if (!ImGui::IsPopupOpen("##tags_keyemulate_picker")) {
        keyPickerHoverTriggered_ = false;
    }
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
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
}

void TagsModule::DrawMiscTab() {
    if (currentPage_ == MiscPage::Variables) {
        DrawVariablesPage();
        return;
    }

    DrawMiscHomePage();
}
