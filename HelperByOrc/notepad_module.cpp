#include "notepad_module.h"

#include "app_config.h"
#include "debug_log.h"
#include "icon_picker_ui.h"
#include "json_utils.h"
#include "markup_renderer.h"
#include "native_file_dialog.h"
#include "notepad_txt_operations.h"
#include "notepad_txt_sync.h"
#include "tags_module.h"
#include "ui_icons.h"
#include "ui_settings.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <charconv>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kNotepadSectionName = "notepad";
constexpr int kNotepadSchemaVersion = 2;
constexpr wchar_t kNotepadAssetsFolder[] = L"notepad";
constexpr wchar_t kNotepadImagesFolder[] = L"images";
constexpr wchar_t kNotepadExportFolder[] = L"export";
constexpr char kPayloadNote[] = "HBO_NOTEPAD_NOTE";
constexpr char kPayloadFolder[] = "HBO_NOTEPAD_FOLDER";
constexpr char kModalPopupId[] = "###notepad_modal";
constexpr char kOrderTypeFolder[] = "folder";
constexpr char kOrderTypeNote[] = "note";

enum class ItemType {
    Folder,
    Note,
};

enum class DropPlacement {
    Before,
    After,
    Inside,
    End,
};

enum class PendingModal {
    None,
    CreateFolder,
    CreateNote,
    RenameFolder,
    RenameNote,
    DeleteFolder,
    DeleteNote,
};

struct FolderEntry {
    std::string path{};
    std::uint64_t createdAt = 0;
    std::uint64_t updatedAt = 0;
};

struct TxtSourceState {
    std::string relativePath{};
    notepadtxt::TextFormat format{};
    notepadtxt::FileVersion version{};
    notepadtxt::FileStatus status = notepadtxt::FileStatus::Ready;
    unsigned long error = 0;
    bool missing = false;
    bool conflict = false;
    bool writePendingRecovery = false;
    bool pendingExternalMissing = false;
    std::string pendingExternalText{};
    notepadtxt::TextFormat pendingFormat{};
    notepadtxt::FileVersion pendingVersion{};
    bool operationPending = false;
};

struct NoteEntry {
    std::string id{};
    std::string title{};
    std::string folderPath{};
    std::string text{};
    bool favorite = false;
    std::uint64_t createdAt = 0;
    std::uint64_t updatedAt = 0;
    std::optional<TxtSourceState> txtSource{};
};

struct OrderItem {
    ItemType type = ItemType::Note;
    std::string value{};
};

struct RowItem {
    ItemType type = ItemType::Note;
    std::string value{};
    std::string label{};
};

enum class PendingTxtActionKind {
    Save,
    RenameNote,
    MoveNote,
    RenameFolder,
    MoveFolder,
    DeleteNote,
    DeleteFolder,
};

struct PendingTxtAction {
    PendingTxtActionKind kind = PendingTxtActionKind::Save;
    std::string subject{};
    std::string value{};
    std::string targetDirectory{};
    std::optional<OrderItem> anchor{};
    DropPlacement placement = DropPlacement::End;
    std::vector<std::string> noteIds{};
};

struct RenderedNoteCache {
    std::string noteId{};
    std::uint64_t updatedAt = 0;
    bool applyTags = false;
    const TagsModule* tagsModule = nullptr;
    std::string source{};
    std::string rendered{};
    bool valid = false;
};

struct RenderedEditCache {
    bool applyTags = false;
    const TagsModule* tagsModule = nullptr;
    std::string source{};
    std::string rendered{};
    bool valid = false;
};

struct CopyLinesCache {
    std::string source{};
    std::vector<std::string> lines{};
    bool valid = false;
};

struct ImGuiStringUserData {
    std::string* value = nullptr;
    int* cursor = nullptr;
};

std::uint64_t UnixTimeNow() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

float ScaleUi(float value) {
    return UiSettings::Instance().Scale(value);
}

ImVec2 ScaleUi(float x, float y) {
    return UiSettings::Instance().Scale(ImVec2(x, y));
}

double NotepadPerfNowMs() {
    static const double s_invFrequencyMs = [] {
        LARGE_INTEGER frequency{};
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
            return 0.0;
        }
        return 1000.0 / static_cast<double>(frequency.QuadPart);
    }();

    if (s_invFrequencyMs <= 0.0) {
        return static_cast<double>(GetTickCount64());
    }

    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) * s_invFrequencyMs;
}

std::string EllipsizeSingleLine(std::string text, ImFont* font, float fontSize, float maxWidth) {
    if (text.empty() || maxWidth <= 0.0f || !font) {
        return maxWidth > 0.0f ? text : std::string();
    }

    const auto textWidth = [font, fontSize](std::string_view value) {
        return font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, value.data(), value.data() + value.size()).x;
    };
    if (textWidth(text) <= maxWidth) {
        return text;
    }

    constexpr std::string_view kEllipsis = "...";
    if (textWidth(kEllipsis) >= maxWidth) {
        return std::string(kEllipsis);
    }

    std::vector<std::size_t> boundaries;
    boundaries.reserve(text.size() + 1);
    boundaries.push_back(0);
    for (std::size_t pos = 0; pos < text.size();) {
        ++pos;
        while (pos < text.size() && (static_cast<unsigned char>(text[pos]) & 0xC0u) == 0x80u) {
            ++pos;
        }
        boundaries.push_back(pos);
    }

    const float prefixWidthLimit = maxWidth - textWidth(kEllipsis);
    std::size_t low = 0;
    std::size_t high = boundaries.size() - 1;
    while (low < high) {
        const std::size_t middle = low + (high - low + 1) / 2;
        const std::string_view prefix(text.data(), boundaries[middle]);
        if (textWidth(prefix) <= prefixWidthLimit) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }

    text.resize(boundaries[low]);
    text += kEllipsis;
    return text;
}

float ButtonItemWidth(std::string_view label) {
    return ImGui::CalcTextSize(label.data(), label.data() + label.size()).x
        + ImGui::GetStyle().FramePadding.x * 2.0f;
}

float CheckboxItemWidth(std::string_view label) {
    return ImGui::GetFrameHeight()
        + ImGui::GetStyle().ItemInnerSpacing.x
        + ImGui::CalcTextSize(label.data(), label.data() + label.size()).x;
}

bool ContinueToolbar(float nextItemWidth) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float nextX = ImGui::GetItemRectMax().x + style.ItemSpacing.x;
    const float contentMaxX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    if (nextX + nextItemWidth > contentMaxX) {
        return false;
    }
    ImGui::SameLine();
    return true;
}

void DrawTextTooltip(std::string_view text) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ScaleUi(520.0f));
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

std::string EscapeImGuiId(std::string_view id) {
    std::string escaped;
    escaped.reserve(id.size());
    for (const char ch : id) {
        if (ch == '%') {
            escaped += "%25";
        } else if (ch == '#') {
            escaped += "%23";
        } else {
            escaped.push_back(ch);
        }
    }
    return escaped;
}

bool DrawEllipsizedText(
    std::string_view text,
    float maxWidth,
    const ImVec4* color = nullptr,
    std::string_view tooltip = {}) {
    const std::string visible = EllipsizeSingleLine(
        std::string(text),
        ImGui::GetFont(),
        ImGui::GetFontSize(),
        std::max(0.0f, maxWidth));
    const bool clipped = visible != text;
    if (color) {
        ImGui::PushStyleColor(ImGuiCol_Text, *color);
    }
    ImGui::TextUnformatted(visible.data(), visible.data() + visible.size());
    if (color) {
        ImGui::PopStyleColor();
    }
    if (clipped && ImGui::IsItemHovered()) {
        DrawTextTooltip(tooltip.empty() ? text : tooltip);
    }
    return clipped;
}

struct SelectableTextResult {
    bool pressed = false;
    bool hovered = false;
    bool clipped = false;
    ImVec2 rowMin{};
    ImVec2 rowMax{};
};

SelectableTextResult DrawSelectableText(
    std::string_view id,
    std::string_view text,
    bool selected,
    ImGuiSelectableFlags flags,
    const ImVec2& size,
    float trailingWidth = 0.0f) {
    SelectableTextResult result;
    const std::string stableId = EscapeImGuiId(id);
    ImGui::PushID(stableId.c_str());
    result.pressed = ImGui::Selectable("##item", selected, flags, size);
    result.hovered = ImGui::IsItemHovered();
    result.rowMin = ImGui::GetItemRectMin();
    result.rowMax = ImGui::GetItemRectMax();
    ImGui::PopID();

    const ImGuiStyle& style = ImGui::GetStyle();
    const float textWidth = std::max(
        0.0f,
        result.rowMax.x - result.rowMin.x - style.FramePadding.x * 2.0f - trailingWidth);
    const std::string visible = EllipsizeSingleLine(
        std::string(text),
        ImGui::GetFont(),
        ImGui::GetFontSize(),
        textWidth);
    result.clipped = visible != text;

    const ImVec2 textSize = ImGui::CalcTextSize(visible.data(), visible.data() + visible.size());
    const ImVec2 textPos(
        result.rowMin.x + style.FramePadding.x,
        result.rowMin.y + std::max(0.0f, (result.rowMax.y - result.rowMin.y - textSize.y) * 0.5f));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(result.rowMin, result.rowMax, true);
    drawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), visible.data(), visible.data() + visible.size());
    drawList->PopClipRect();
    return result;
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

std::wstring MultiByteToWide(std::string_view text, UINT codePage, DWORD flags = 0) {
    if (text.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(
        codePage,
        flags,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            codePage,
            flags,
            text.data(),
            static_cast<int>(text.size()),
            result.data(),
            required)
        <= 0) {
        return {};
    }
    return result;
}

std::wstring Utf8ToWide(std::string_view text) {
    std::wstring wide = MultiByteToWide(text, CP_UTF8, MB_ERR_INVALID_CHARS);
    if (wide.empty() && !text.empty()) {
        wide = MultiByteToWide(text, CP_ACP);
    }
    return wide;
}

std::string WideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            0,
            text.data(),
            static_cast<int>(text.size()),
            result.data(),
            required,
            nullptr,
            nullptr)
        <= 0) {
        return {};
    }
    return result;
}

std::string NormalizeImportedText(std::string text) {
    if (text.size() >= 3
        && static_cast<unsigned char>(text[0]) == 0xEF
        && static_cast<unsigned char>(text[1]) == 0xBB
        && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    if (text.empty()) {
        return text;
    }
    if (!MultiByteToWide(text, CP_UTF8, MB_ERR_INVALID_CHARS).empty()) {
        return text;
    }
    const std::wstring wide = MultiByteToWide(text, CP_ACP);
    return wide.empty() ? text : WideToUtf8(wide);
}

std::string PathToUtf8(const fs::path& path) {
    return WideToUtf8(path.wstring());
}

std::string LowerUtf8(std::string_view text) {
    std::wstring wide = Utf8ToWide(text);
    if (wide.empty()) {
        std::string result(text);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return result;
    }
    CharLowerBuffW(wide.data(), static_cast<DWORD>(wide.size()));
    return WideToUtf8(wide);
}

std::string UpperUtf8(std::string_view text) {
    std::wstring wide = Utf8ToWide(text);
    if (wide.empty()) {
        std::string result(text);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        return result;
    }
    CharUpperBuffW(wide.data(), static_cast<DWORD>(wide.size()));
    return WideToUtf8(wide);
}

bool ContainsNoCase(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    const std::string h = LowerUtf8(haystack);
    const std::string n = LowerUtf8(needle);
    return h.find(n) != std::string::npos;
}

int ImGuiStringResizeCallback(ImGuiInputTextCallbackData* data) {
    auto* userData = static_cast<ImGuiStringUserData*>(data->UserData);
    if (!userData || !userData->value) {
        return 0;
    }
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        userData->value->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = userData->value->data();
    } else if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit && userData->cursor) {
        *userData->cursor = data->CursorPos;
    }
    return 0;
}

bool InputTextString(
    const char* label,
    std::string& value,
    ImGuiInputTextFlags flags = 0,
    std::size_t minBuffer = 128,
    int* cursor = nullptr) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }
    ImGuiStringUserData userData{ &value, cursor };
    return ImGui::InputText(
        label,
        value.data(),
        value.capacity() + 1,
        flags | ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackEdit,
        ImGuiStringResizeCallback,
        &userData);
}

bool InputTextWithHintString(
    const char* label,
    const char* hint,
    std::string& value,
    ImGuiInputTextFlags flags = 0,
    std::size_t minBuffer = 128,
    int* cursor = nullptr) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }
    ImGuiStringUserData userData{ &value, cursor };
    return ImGui::InputTextWithHint(
        label,
        hint,
        value.data(),
        value.capacity() + 1,
        flags | ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackEdit,
        ImGuiStringResizeCallback,
        &userData);
}

bool InputTextMultilineString(
    const char* label,
    std::string& value,
    const ImVec2& size,
    ImGuiInputTextFlags flags = 0,
    int* cursor = nullptr) {
    if (value.capacity() < 4096) {
        value.reserve(4096);
    }
    ImGuiStringUserData userData{ &value, cursor };
    return ImGui::InputTextMultiline(
        label,
        value.data(),
        value.capacity() + 1,
        size,
        flags | ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackEdit,
        ImGuiStringResizeCallback,
        &userData);
}

std::vector<std::string> SplitPath(std::string_view path) {
    std::vector<std::string> parts;
    std::size_t begin = 0;
    while (begin < path.size()) {
        std::size_t end = path.find('/', begin);
        if (end == std::string_view::npos) {
            end = path.size();
        }
        std::string part = TrimAscii(path.substr(begin, end - begin));
        if (!part.empty()) {
            parts.push_back(std::move(part));
        }
        begin = end + 1;
    }
    return parts;
}

std::string JoinPathParts(const std::vector<std::string>& parts, std::size_t count) {
    std::string result;
    for (std::size_t i = 0; i < count && i < parts.size(); ++i) {
        if (!result.empty()) {
            result += "/";
        }
        result += parts[i];
    }
    return result;
}

std::string NormalizeFolderPath(std::string_view path) {
    const std::vector<std::string> parts = SplitPath(path);
    return JoinPathParts(parts, parts.size());
}

std::string ParentPath(std::string_view path) {
    const std::string normalized = NormalizeFolderPath(path);
    const std::size_t pos = normalized.find_last_of('/');
    return pos == std::string::npos ? std::string() : normalized.substr(0, pos);
}

std::string BaseName(std::string_view path) {
    const std::string normalized = NormalizeFolderPath(path);
    const std::size_t pos = normalized.find_last_of('/');
    return pos == std::string::npos ? normalized : normalized.substr(pos + 1);
}

std::string JoinFolderPath(std::string_view parent, std::string_view name) {
    const std::string cleanParent = NormalizeFolderPath(parent);
    const std::string cleanName = TrimAscii(name);
    return cleanParent.empty() ? cleanName : cleanParent + "/" + cleanName;
}

bool PathStartsWith(std::string_view path, std::string_view prefix) {
    if (prefix.empty()) {
        return true;
    }
    if (path == prefix) {
        return true;
    }
    return path.size() > prefix.size()
        && path.substr(0, prefix.size()) == prefix
        && path[prefix.size()] == '/';
}

std::string ReplacePathPrefix(std::string_view path, std::string_view oldPrefix, std::string_view newPrefix) {
    if (!PathStartsWith(path, oldPrefix)) {
        return std::string(path);
    }
    if (path.size() == oldPrefix.size()) {
        return NormalizeFolderPath(newPrefix);
    }
    const std::string suffix(path.substr(oldPrefix.size() + 1));
    return newPrefix.empty() ? suffix : std::string(newPrefix) + "/" + suffix;
}

bool IsValidFolderName(std::string_view name) {
    const std::string value = TrimAscii(name);
    if (value.empty()) {
        return false;
    }
    return value.find('/') == std::string::npos && value.find('\\') == std::string::npos;
}

std::string SanitizeFileStem(std::string_view value, std::string_view fallback) {
    std::wstring wide = Utf8ToWide(value);
    std::wstring result;
    for (wchar_t ch : wide) {
        if (ch == L'<' || ch == L'>' || ch == L':' || ch == L'"' || ch == L'/' || ch == L'\\'
            || ch == L'|' || ch == L'?' || ch == L'*' || ch < 32) {
            result.push_back(L'_');
        } else {
            result.push_back(ch);
        }
    }
    std::string utf8 = TrimAscii(WideToUtf8(result));
    while (!utf8.empty() && (utf8.back() == '.' || utf8.back() == ' ')) {
        utf8.pop_back();
    }
    if (utf8.empty() || utf8 == "." || utf8 == "..") {
        utf8 = std::string(fallback);
    }
    const std::size_t dot = utf8.find('.');
    const std::string stemUpper = UpperUtf8(utf8.substr(0, dot));
    const bool reserved = stemUpper == "CON" || stemUpper == "PRN" || stemUpper == "AUX"
        || stemUpper == "NUL" || stemUpper == "CLOCK$"
        || (stemUpper.size() == 4
            && (stemUpper.starts_with("COM") || stemUpper.starts_with("LPT"))
            && stemUpper[3] >= '1' && stemUpper[3] <= '9');
    if (reserved) {
        utf8 += "_";
    }
    return utf8;
}

std::wstring BuildDialogFilter(std::initializer_list<std::pair<UiText, const wchar_t*>> entries) {
    std::wstring filter;
    for (const auto& [labelId, pattern] : entries) {
        filter += Utf8ToWide(UiSettings::Instance().Text(labelId));
        filter.push_back(L'\0');
        filter += pattern;
        filter.push_back(L'\0');
    }
    filter.push_back(L'\0');
    return filter;
}

jsonutil::JsonArray SerializeOrderItems(const std::vector<OrderItem>& items) {
    jsonutil::JsonArray array;
    for (const OrderItem& item : items) {
        jsonutil::JsonObject object;
        object["type"] = item.type == ItemType::Folder ? kOrderTypeFolder : kOrderTypeNote;
        object["value"] = item.value;
        array.emplace_back(std::move(object));
    }
    return array;
}

std::vector<OrderItem> DeserializeOrderItems(const jsonutil::JsonArray* array) {
    std::vector<OrderItem> items;
    if (!array) {
        return items;
    }
    for (const jsonutil::JsonValue& value : *array) {
        const jsonutil::JsonObject* object = value.TryObject();
        if (!object) {
            continue;
        }
        const std::string type = jsonutil::JsonStringOr(object, "type", "");
        const std::string itemValue = jsonutil::JsonStringOr(object, "value", "");
        if (itemValue.empty()) {
            continue;
        }
        if (type == kOrderTypeFolder) {
            items.push_back({ ItemType::Folder, NormalizeFolderPath(itemValue) });
        } else if (type == kOrderTypeNote) {
            items.push_back({ ItemType::Note, itemValue });
        }
    }
    return items;
}

std::uint64_t JsonUint64StringOr(
    const jsonutil::JsonObject* object,
    const char* key,
    std::uint64_t fallback = 0) {
    const std::string text = jsonutil::JsonStringOr(object, key, "");
    if (!text.empty()) {
        std::uint64_t value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error == std::errc{} && end == text.data() + text.size()) {
            return value;
        }
    }
    return jsonutil::JsonNumberOr<std::uint64_t>(object, key, fallback);
}

jsonutil::JsonObject SerializeTxtVersion(const notepadtxt::FileVersion& version) {
    jsonutil::JsonObject object;
    object["size"] = static_cast<double>(version.size);
    object["last_write_time"] = std::to_string(version.lastWriteTime);
    object["identity"] = version.identity;
    object["content_hash"] = version.contentHash;
    return object;
}

notepadtxt::FileVersion DeserializeTxtVersion(const jsonutil::JsonObject* object) {
    notepadtxt::FileVersion version;
    version.size = JsonUint64StringOr(object, "size");
    version.lastWriteTime = JsonUint64StringOr(object, "last_write_time");
    version.identity = jsonutil::JsonStringOr(object, "identity", "");
    version.contentHash = jsonutil::JsonStringOr(object, "content_hash", "");
    return version;
}

const char* TxtFileStatusName(notepadtxt::FileStatus status) {
    switch (status) {
    case notepadtxt::FileStatus::TooLarge:
        return "too_large";
    case notepadtxt::FileStatus::ReadError:
        return "read_error";
    case notepadtxt::FileStatus::DecodeError:
        return "decode_error";
    case notepadtxt::FileStatus::Ready:
    default:
        return "ready";
    }
}

notepadtxt::FileStatus ParseTxtFileStatus(std::string_view value) {
    if (value == "too_large") {
        return notepadtxt::FileStatus::TooLarge;
    }
    if (value == "read_error") {
        return notepadtxt::FileStatus::ReadError;
    }
    if (value == "decode_error") {
        return notepadtxt::FileStatus::DecodeError;
    }
    return notepadtxt::FileStatus::Ready;
}

jsonutil::JsonObject SerializeTxtSource(const TxtSourceState& source) {
    jsonutil::JsonObject object;
    object["kind"] = "txt";
    object["relative_path"] = source.relativePath;
    object["encoding"] = notepadtxt::TextEncodingName(source.format.encoding);
    object["newline"] = notepadtxt::NewlineStyleName(source.format.newline);
    object["bom"] = source.format.bom;
    object["version"] = jsonutil::JsonValue(SerializeTxtVersion(source.version));
    object["status"] = TxtFileStatusName(source.status);
    object["error"] = static_cast<double>(source.error);
    object["missing"] = source.missing;
    object["conflict"] = source.conflict;
    object["write_pending"] = source.writePendingRecovery;
    if (source.conflict) {
        object["pending_missing"] = source.pendingExternalMissing;
        object["pending_external_text"] = source.pendingExternalText;
        object["pending_encoding"] = notepadtxt::TextEncodingName(source.pendingFormat.encoding);
        object["pending_newline"] = notepadtxt::NewlineStyleName(source.pendingFormat.newline);
        object["pending_bom"] = source.pendingFormat.bom;
        object["pending_version"] = jsonutil::JsonValue(SerializeTxtVersion(source.pendingVersion));
    }
    return object;
}

std::optional<TxtSourceState> DeserializeTxtSource(const jsonutil::JsonObject* object) {
    if (!object || jsonutil::JsonStringOr(object, "kind", "") != "txt") {
        return std::nullopt;
    }
    TxtSourceState source;
    source.relativePath = jsonutil::JsonStringOr(object, "relative_path", "");
    if (source.relativePath.empty()) {
        return std::nullopt;
    }
    source.format.encoding = notepadtxt::ParseTextEncoding(jsonutil::JsonStringOr(object, "encoding", "utf8"));
    source.format.newline = notepadtxt::ParseNewlineStyle(jsonutil::JsonStringOr(object, "newline", "crlf"));
    source.format.bom = jsonutil::JsonBoolOr(object, "bom", false);
    source.version = DeserializeTxtVersion(jsonutil::JsonObjectOrNull(object, "version"));
    source.status = ParseTxtFileStatus(jsonutil::JsonStringOr(object, "status", "ready"));
    source.error = jsonutil::JsonNumberOr<unsigned long>(object, "error", 0);
    source.missing = jsonutil::JsonBoolOr(object, "missing", false);
    source.conflict = jsonutil::JsonBoolOr(object, "conflict", false);
    source.writePendingRecovery = jsonutil::JsonBoolOr(object, "write_pending", false);
    if (source.conflict) {
        source.pendingExternalMissing = jsonutil::JsonBoolOr(object, "pending_missing", false);
        source.pendingExternalText = jsonutil::JsonStringOr(object, "pending_external_text", "");
        source.pendingFormat.encoding = notepadtxt::ParseTextEncoding(jsonutil::JsonStringOr(object, "pending_encoding", "utf8"));
        source.pendingFormat.newline = notepadtxt::ParseNewlineStyle(jsonutil::JsonStringOr(object, "pending_newline", "crlf"));
        source.pendingFormat.bom = jsonutil::JsonBoolOr(object, "pending_bom", false);
        source.pendingVersion = DeserializeTxtVersion(jsonutil::JsonObjectOrNull(object, "pending_version"));
    }
    return source;
}

std::string TxtSourcePathKey(std::string_view relativePath) {
    std::string normalized(relativePath);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    return LowerUtf8(normalized);
}

std::string TxtSourceFolderPath(std::string_view relativePath) {
    const fs::path path(Utf8ToWide(relativePath));
    return NormalizeFolderPath(PathToUtf8(path.parent_path()));
}

std::string TxtSourceTitle(std::string_view relativePath) {
    const fs::path path(Utf8ToWide(relativePath));
    return PathToUtf8(path.stem());
}

bool SameTxtVersion(const notepadtxt::FileVersion& lhs, const notepadtxt::FileVersion& rhs) {
    return lhs.size == rhs.size
        && lhs.lastWriteTime == rhs.lastWriteTime
        && lhs.identity == rhs.identity
        && lhs.contentHash == rhs.contentHash;
}

bool SameTxtFormat(const notepadtxt::TextFormat& lhs, const notepadtxt::TextFormat& rhs) {
    return lhs.encoding == rhs.encoding && lhs.newline == rhs.newline && lhs.bom == rhs.bom;
}

} // namespace

struct NotepadModule::Impl {
    HMODULE module = nullptr;
    TagsModule* tagsModule = nullptr;
    bool configLoaded = false;
    std::vector<FolderEntry> folders;
    std::vector<NoteEntry> notes;
    std::map<std::string, std::vector<OrderItem>> order;
    std::string currentFolder;
    std::string selectedNoteId;
    std::string selectedFolderPath;
    ItemType selectedType = ItemType::Note;
    std::string search;
    bool editing = false;
    bool editDirty = false;
    bool applyTags = true;
    bool copyLineMode = false;
    std::string editBuffer;
    int editCursor = -1;
    PendingModal pendingModal = PendingModal::None;
    bool modalOpenRequested = false;
    std::string modalBuffer;
    std::string modalTarget;
    std::string statusMessage;
    std::string txtOperationPendingStatusMessage;
    MarkupRenderer renderer;
    icon_picker::State iconPickerState{};
    RenderedNoteCache renderedNoteCache{};
    RenderedEditCache renderedEditCache{};
    CopyLinesCache copyLinesCache{};
    RenderStats lastRenderStats{};
    std::uint64_t idCounter = 0;
    notepadtxt::SyncService txtSync{};
    notepadtxt::OperationService txtOperations{};
    std::uint64_t txtSyncGeneration = 0;
    std::unordered_map<std::uint64_t, PendingTxtAction> pendingTxtActions{};
    bool txtForceRescanOnRestart = false;

    void OnProcessAttach(HMODULE moduleHandle) {
        module = moduleHandle;
    }

    void SetTagsModule(TagsModule* modulePtr) {
        tagsModule = modulePtr;
    }

    void Shutdown() {
        SaveEditBufferIfNeeded();
        if (!txtOperations.Flush()) {
            debuglog::WriteError("[notepad][txt] operation flush timed out during shutdown");
        }
        txtOperations.Stop();
        ApplyPendingTxtOperations(false);
        txtSync.Stop();
        ReleaseDeviceResources();
        configLoaded = false;
        folders.clear();
        notes.clear();
        order.clear();
        iconPickerState = {};
        renderedNoteCache = {};
        renderedEditCache = {};
        copyLinesCache = {};
        lastRenderStats = {};
    }

    void ReloadConfig() {
        txtOperations.Stop();
        txtSync.Stop();
        ++txtSyncGeneration;
        pendingTxtActions.clear();
        txtForceRescanOnRestart = false;
        ReleaseDeviceResources();
        configLoaded = false;
        folders.clear();
        notes.clear();
        order.clear();
        currentFolder.clear();
        selectedNoteId.clear();
        selectedFolderPath.clear();
        editing = false;
        editDirty = false;
        editBuffer.clear();
        iconPickerState = {};
        renderedNoteCache = {};
        renderedEditCache = {};
        copyLinesCache = {};
        lastRenderStats = {};
    }

    void FlushPendingEdits() {
        SaveEditBufferIfNeeded();
        const bool flushed = txtOperations.Flush();
        if (!flushed) {
            debuglog::WriteError("[notepad][txt] operation flush timed out before profile change");
            txtOperations.Stop();
            ApplyPendingTxtOperations(false);
            txtOperations.Start(NotepadDirectory(), txtSyncGeneration);
            StartTxtScanner(txtForceRescanOnRestart);
            txtForceRescanOnRestart = false;
            return;
        }
        ApplyPendingTxtOperations();
    }

    void ReleaseDeviceResources() {
        renderer.ReleaseDeviceResources();
    }

    fs::path ProfileDirectory() const {
        return AppConfig::Instance().ActiveProfileDirectory();
    }

    fs::path NotepadDirectory() const {
        return ProfileDirectory() / kNotepadAssetsFolder;
    }

    fs::path ImagesDirectory() const {
        return NotepadDirectory() / kNotepadImagesFolder;
    }

    fs::path ExportDirectory() const {
        return NotepadDirectory() / kNotepadExportFolder;
    }

    fs::path ImagesDirectoryPath() {
        EnsureLoaded();
        EnsureAssetDirectories();
        return ImagesDirectory();
    }

    void EnsureAssetDirectories() const {
        std::error_code error;
        fs::create_directories(ImagesDirectory(), error);
        if (error) {
            debuglog::WriteError("[notepad] failed to create images directory: %ls error=%d", ImagesDirectory().c_str(), error.value());
        }
        error.clear();
        fs::create_directories(ExportDirectory(), error);
        if (error) {
            debuglog::WriteError("[notepad] failed to create export directory: %ls error=%d", ExportDirectory().c_str(), error.value());
        }
    }

    FolderEntry* FindFolder(std::string_view path) {
        const std::string normalized = NormalizeFolderPath(path);
        const auto it = std::find_if(folders.begin(), folders.end(), [&](const FolderEntry& folder) {
            return folder.path == normalized;
        });
        return it == folders.end() ? nullptr : &(*it);
    }

    const FolderEntry* FindFolder(std::string_view path) const {
        const std::string normalized = NormalizeFolderPath(path);
        const auto it = std::find_if(folders.begin(), folders.end(), [&](const FolderEntry& folder) {
            return folder.path == normalized;
        });
        return it == folders.end() ? nullptr : &(*it);
    }

    NoteEntry* FindNote(std::string_view id) {
        const auto it = std::find_if(notes.begin(), notes.end(), [&](const NoteEntry& note) {
            return note.id == id;
        });
        return it == notes.end() ? nullptr : &(*it);
    }

    const NoteEntry* FindNote(std::string_view id) const {
        const auto it = std::find_if(notes.begin(), notes.end(), [&](const NoteEntry& note) {
            return note.id == id;
        });
        return it == notes.end() ? nullptr : &(*it);
    }

    bool TryGetNote(std::string_view id, NotepadModule::NoteContent& out) {
        EnsureLoaded();
        const NoteEntry* note = FindNote(id);
        if (!note) {
            return false;
        }
        out.id = note->id;
        out.title = note->title;
        out.folderPath = note->folderPath;
        out.text = editing && editDirty && selectedNoteId == note->id ? editBuffer : note->text;
        return true;
    }

    std::vector<NotepadModule::NoteSummary> NoteSummaries() {
        EnsureLoaded();
        std::vector<NotepadModule::NoteSummary> result;
        result.reserve(notes.size());
        for (const NoteEntry& note : notes) {
            result.push_back(NotepadModule::NoteSummary{
                note.id,
                note.title,
                note.folderPath,
            });
        }
        std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.folderPath != rhs.folderPath) {
                return LowerUtf8(lhs.folderPath) < LowerUtf8(rhs.folderPath);
            }
            return LowerUtf8(lhs.title) < LowerUtf8(rhs.title);
        });
        return result;
    }

    bool FolderExists(std::string_view path) const {
        return path.empty() || FindFolder(path) != nullptr;
    }

    bool FolderNameExists(std::string_view parent, std::string_view name, std::string_view ignorePath = {}) const {
        const std::string target = JoinFolderPath(parent, name);
        for (const FolderEntry& folder : folders) {
            if (folder.path == target && folder.path != ignorePath) {
                return true;
            }
        }
        return false;
    }

    std::string GenerateNoteId() {
        const std::uint64_t now = static_cast<std::uint64_t>(GetTickCount64());
        for (;;) {
            char buffer[64]{};
            std::snprintf(buffer, sizeof(buffer), "note_%llx_%llx",
                static_cast<unsigned long long>(now),
                static_cast<unsigned long long>(++idCounter));
            if (!FindNote(buffer)) {
                return buffer;
            }
        }
    }

    std::string EnsureTxtFolderHierarchy(std::string_view requestedPath) {
        const std::vector<std::string> parts = SplitPath(requestedPath);
        std::string parent;
        for (const std::string& part : parts) {
            const std::string requested = JoinFolderPath(parent, part);
            const std::string requestedKey = LowerUtf8(requested);
            const auto existing = std::find_if(folders.begin(), folders.end(), [&](const FolderEntry& folder) {
                return LowerUtf8(folder.path) == requestedKey;
            });
            if (existing != folders.end()) {
                parent = existing->path;
                continue;
            }
            FolderEntry folder;
            folder.path = requested;
            folder.createdAt = UnixTimeNow();
            folder.updatedAt = folder.createdAt;
            folders.push_back(folder);
            order.try_emplace(folder.path);
            order[parent].push_back({ ItemType::Folder, folder.path });
            parent = folder.path;
        }
        return parent;
    }

    void MoveSourceNoteOrder(std::string_view noteId, std::string_view oldFolder, std::string_view newFolder) {
        if (oldFolder == newFolder) {
            return;
        }
        std::size_t oldIndex = order[std::string(oldFolder)].size();
        auto& oldItems = order[std::string(oldFolder)];
        const auto oldIt = std::find_if(oldItems.begin(), oldItems.end(), [&](const OrderItem& item) {
            return item.type == ItemType::Note && item.value == noteId;
        });
        if (oldIt != oldItems.end()) {
            oldIndex = static_cast<std::size_t>(std::distance(oldItems.begin(), oldIt));
            oldItems.erase(oldIt);
        }
        auto& newItems = order[std::string(newFolder)];
        newItems.insert(
            newItems.begin() + static_cast<std::ptrdiff_t>(std::min(oldIndex, newItems.size())),
            { ItemType::Note, std::string(noteId) });
    }

    std::vector<notepadtxt::FileSnapshot> TxtScannerCache() const {
        std::vector<notepadtxt::FileSnapshot> cachedFiles;
        for (const NoteEntry& note : notes) {
            if (!note.txtSource) {
                continue;
            }
            notepadtxt::FileSnapshot snapshot;
            snapshot.relativePath = note.txtSource->relativePath;
            snapshot.format = note.txtSource->format;
            snapshot.version = note.txtSource->version;
            snapshot.status = note.txtSource->status;
            snapshot.error = note.txtSource->error;
            cachedFiles.push_back(std::move(snapshot));
        }
        return cachedFiles;
    }

    void StartTxtScanner(bool forceReadAll = false) {
        txtSync.Start(NotepadDirectory(), txtSyncGeneration, TxtScannerCache(), forceReadAll);
    }

    void StartTxtSync() {
        const std::uint64_t generation = ++txtSyncGeneration;
        txtOperations.Start(NotepadDirectory(), generation);
        StartTxtScanner(true);
        debuglog::WriteInfo(
            "[notepad][txt] sync started generation=%llu root=%ls",
            static_cast<unsigned long long>(generation),
            NotepadDirectory().c_str());
    }

    void ApplyPendingTxtScan() {
        std::optional<notepadtxt::ScanResult> pending = txtSync.TakeLatestScan();
        if (!pending || pending->generation != txtSyncGeneration) {
            return;
        }
        const double beginMs = NotepadPerfNowMs();
        std::unordered_map<std::string, std::vector<std::size_t>> notesByPath;
        std::unordered_map<std::string, std::vector<std::size_t>> notesByIdentity;
        std::unordered_map<std::string, std::vector<std::string>> scanPathsByIdentity;
        notesByPath.reserve(notes.size());
        notesByIdentity.reserve(notes.size());
        scanPathsByIdentity.reserve(pending->files.size());
        for (std::size_t index = 0; index < notes.size(); ++index) {
            const NoteEntry& note = notes[index];
            if (!note.txtSource) {
                continue;
            }
            notesByPath[TxtSourcePathKey(note.txtSource->relativePath)].push_back(index);
            if (!note.txtSource->version.identity.empty()) {
                notesByIdentity[note.txtSource->version.identity].push_back(index);
            }
        }
        for (const notepadtxt::FileSnapshot& snapshot : pending->files) {
            if (!snapshot.version.identity.empty()) {
                scanPathsByIdentity[snapshot.version.identity].push_back(TxtSourcePathKey(snapshot.relativePath));
            }
        }

        std::set<std::string> matchedIds;
        std::size_t created = 0;
        std::size_t updated = 0;
        std::size_t missing = 0;
        std::size_t conflicts = 0;
        bool modelChanged = false;
        for (notepadtxt::FileSnapshot& snapshot : pending->files) {
            std::optional<std::size_t> noteIndex;
            const auto pathIt = notesByPath.find(TxtSourcePathKey(snapshot.relativePath));
            if (pathIt != notesByPath.end() && pathIt->second.size() == 1) {
                const std::size_t candidateIndex = pathIt->second.front();
                const TxtSourceState& candidateSource = *notes[candidateIndex].txtSource;
                const bool identityMovedElsewhere = !candidateSource.version.identity.empty()
                    && candidateSource.version.identity != snapshot.version.identity
                    && scanPathsByIdentity[candidateSource.version.identity].size() == 1
                    && scanPathsByIdentity[candidateSource.version.identity].front()
                        != TxtSourcePathKey(snapshot.relativePath);
                if (!identityMovedElsewhere) {
                    noteIndex = candidateIndex;
                }
            }
            if (!noteIndex && !snapshot.version.identity.empty()) {
                const auto identityIt = notesByIdentity.find(snapshot.version.identity);
                if (identityIt != notesByIdentity.end() && identityIt->second.size() == 1) {
                    noteIndex = identityIt->second.front();
                }
            }
            if (noteIndex && matchedIds.contains(notes[*noteIndex].id)) {
                noteIndex.reset();
            }

            if (!noteIndex) {
                NoteEntry note;
                note.id = GenerateNoteId();
                note.title = TxtSourceTitle(snapshot.relativePath);
                if (note.title.empty()) {
                    note.title = UiSettings::Instance().Text(UiText::NotepadUntitled);
                }
                note.folderPath = EnsureTxtFolderHierarchy(TxtSourceFolderPath(snapshot.relativePath));
                note.text = std::move(snapshot.text);
                note.createdAt = UnixTimeNow();
                note.updatedAt = note.createdAt;
                TxtSourceState source;
                source.relativePath = snapshot.relativePath;
                source.format = snapshot.format;
                source.version = snapshot.version;
                source.status = snapshot.status;
                source.error = snapshot.error;
                note.txtSource = std::move(source);
                notes.push_back(std::move(note));
                order[notes.back().folderPath].push_back({ ItemType::Note, notes.back().id });
                matchedIds.insert(notes.back().id);
                ++created;
                modelChanged = true;
                continue;
            }

            NoteEntry& note = notes[*noteIndex];
            if (!note.txtSource || !matchedIds.insert(note.id).second) {
                continue;
            }
            TxtSourceState& source = *note.txtSource;
            const std::string newFolder = EnsureTxtFolderHierarchy(TxtSourceFolderPath(snapshot.relativePath));
            const std::string newTitle = TxtSourceTitle(snapshot.relativePath);
            if (source.relativePath != snapshot.relativePath) {
                source.relativePath = snapshot.relativePath;
                modelChanged = true;
            }
            if (note.folderPath != newFolder) {
                MoveSourceNoteOrder(note.id, note.folderPath, newFolder);
                note.folderPath = newFolder;
                modelChanged = true;
            }
            if (!newTitle.empty() && note.title != newTitle) {
                note.title = newTitle;
                modelChanged = true;
            }
            if (source.missing || source.status != snapshot.status || source.error != snapshot.error) {
                source.missing = false;
                source.status = snapshot.status;
                source.error = snapshot.error;
                modelChanged = true;
            }
            if (snapshot.status != notepadtxt::FileStatus::Ready) {
                if (!SameTxtVersion(source.version, snapshot.version)) {
                    source.version = snapshot.version;
                    modelChanged = true;
                }
                modelChanged = true;
                debuglog::WriteError(
                    "[notepad][txt] source unavailable id=%s path=%s status=%s error=%lu",
                    note.id.c_str(),
                    snapshot.relativePath.c_str(),
                    TxtFileStatusName(snapshot.status),
                    snapshot.error);
                continue;
            }

            if (source.conflict) {
                if (!snapshot.bodyReused) {
                    source.pendingExternalMissing = false;
                    source.pendingExternalText = std::move(snapshot.text);
                    source.pendingFormat = snapshot.format;
                    source.pendingVersion = snapshot.version;
                    modelChanged = true;
                }
                continue;
            }

            const bool externalContentChanged = !snapshot.bodyReused
                && (source.version.contentHash != snapshot.version.contentHash || source.writePendingRecovery);
            if (externalContentChanged && editing && editDirty && selectedNoteId == note.id) {
                source.conflict = true;
                source.writePendingRecovery = false;
                source.pendingExternalMissing = false;
                source.pendingExternalText = std::move(snapshot.text);
                source.pendingFormat = snapshot.format;
                source.pendingVersion = snapshot.version;
                ++conflicts;
                modelChanged = true;
                debuglog::WriteError(
                    "[notepad][txt] edit conflict id=%s path=%s",
                    note.id.c_str(),
                    snapshot.relativePath.c_str());
                continue;
            }

            if (externalContentChanged) {
                note.text = std::move(snapshot.text);
                note.updatedAt = UnixTimeNow();
                renderedNoteCache = {};
                ++updated;
                modelChanged = true;
            }
            if (!SameTxtFormat(source.format, snapshot.format) || !SameTxtVersion(source.version, snapshot.version)
                || source.writePendingRecovery || source.conflict || !source.pendingExternalText.empty()) {
                source.format = snapshot.format;
                source.version = snapshot.version;
                source.conflict = false;
                source.writePendingRecovery = false;
                source.pendingExternalMissing = false;
                source.pendingExternalText.clear();
                source.pendingFormat = {};
                source.pendingVersion = {};
                modelChanged = true;
            }
        }

        if (pending->metrics.complete) {
            for (NoteEntry& note : notes) {
                if (!note.txtSource || matchedIds.contains(note.id)) {
                    continue;
                }
                if (!note.txtSource->missing) {
                    if ((editing && editDirty && selectedNoteId == note.id) || note.txtSource->writePendingRecovery) {
                        note.txtSource->conflict = true;
                        note.txtSource->writePendingRecovery = false;
                        note.txtSource->pendingExternalMissing = true;
                        note.txtSource->pendingExternalText = note.text;
                        note.txtSource->pendingFormat = note.txtSource->format;
                        note.txtSource->pendingVersion = {};
                        ++conflicts;
                    }
                    note.txtSource->missing = true;
                    note.txtSource->status = notepadtxt::FileStatus::ReadError;
                    note.txtSource->error = ERROR_FILE_NOT_FOUND;
                    ++missing;
                    modelChanged = true;
                    debuglog::WriteError(
                        "[notepad][txt] source missing id=%s path=%s",
                        note.id.c_str(),
                        note.txtSource->relativePath.c_str());
                }
            }
        }

        if (modelChanged) {
            QueueSave();
        }
        const double applyMs = NotepadPerfNowMs() - beginMs;
        debuglog::WriteInfo(
            "[notepad][txt] scan applied generation=%llu created=%zu updated=%zu missing=%zu conflicts=%zu apply=%.2fms complete=%d",
            static_cast<unsigned long long>(pending->generation),
            created,
            updated,
            missing,
            conflicts,
            applyMs,
            pending->metrics.complete ? 1 : 0);
    }

    void ApplyTxtSnapshot(NoteEntry& note, const notepadtxt::FileSnapshot& snapshot) {
        if (!note.txtSource) {
            return;
        }
        TxtSourceState& source = *note.txtSource;
        source.relativePath = snapshot.relativePath;
        source.format = snapshot.format;
        source.version = snapshot.version;
        source.status = snapshot.status;
        source.error = snapshot.error;
        source.missing = false;
        source.conflict = false;
        source.writePendingRecovery = false;
        source.pendingExternalMissing = false;
        source.pendingExternalText.clear();
        source.pendingFormat = {};
        source.pendingVersion = {};
        source.operationPending = false;
        note.text = snapshot.text;
        note.updatedAt = UnixTimeNow();
        renderedNoteCache = {};
    }

    void RewriteTxtSourceFolderPrefix(std::string_view oldPath, std::string_view newPath) {
        for (NoteEntry& note : notes) {
            if (!note.txtSource) {
                continue;
            }
            const std::string sourceFolder = TxtSourceFolderPath(note.txtSource->relativePath);
            if (!PathStartsWith(sourceFolder, oldPath)) {
                continue;
            }
            const fs::path sourcePath(Utf8ToWide(note.txtSource->relativePath));
            const std::string targetFolder = ReplacePathPrefix(sourceFolder, oldPath, newPath);
            note.txtSource->relativePath = TxtRelativePath(targetFolder, PathToUtf8(sourcePath.filename()));
        }
    }

    void ShowTxtOperationPendingStatus() {
        txtOperationPendingStatusMessage = UiSettings::Instance().Text(UiText::NotepadTxtOperationPending);
        statusMessage = txtOperationPendingStatusMessage;
    }

    void ApplyPendingTxtOperations(bool restartScanner = true) {
        std::vector<notepadtxt::OperationResult> results = txtOperations.TakeResults();
        if (results.empty()) {
            return;
        }

        txtSync.Stop();
        bool modelChanged = false;
        for (const notepadtxt::OperationResult& result : results) {
            const auto pendingIt = pendingTxtActions.find(result.requestId);
            if (pendingIt == pendingTxtActions.end()) {
                continue;
            }
            const PendingTxtAction action = pendingIt->second;
            pendingTxtActions.erase(pendingIt);
            for (const std::string& noteId : action.noteIds) {
                if (NoteEntry* note = FindNote(noteId); note && note->txtSource) {
                    note->txtSource->operationPending = false;
                }
            }
            if (result.generation != txtSyncGeneration) {
                continue;
            }

            if (!result.success) {
                txtForceRescanOnRestart = true;
                unsigned long operationError = ERROR_GEN_FAILURE;
                for (const notepadtxt::OperationItemResult& item : result.items) {
                    if (item.error != ERROR_SUCCESS) {
                        operationError = item.error;
                    }
                    NoteEntry* note = FindNote(item.token);
                    if (!note || !note->txtSource) {
                        continue;
                    }
                    TxtSourceState& source = *note->txtSource;
                    source.error = item.error;
                    if ((item.conflict || action.kind == PendingTxtActionKind::Save)
                        && item.snapshot.status == notepadtxt::FileStatus::Ready
                        && !item.snapshot.relativePath.empty()) {
                        source.conflict = true;
                        source.writePendingRecovery = false;
                        source.pendingExternalMissing = false;
                        source.status = notepadtxt::FileStatus::Ready;
                        source.pendingExternalText = item.snapshot.text;
                        source.pendingFormat = item.snapshot.format;
                        source.pendingVersion = item.snapshot.version;
                    } else if (item.error == ERROR_FILE_NOT_FOUND || item.error == ERROR_PATH_NOT_FOUND) {
                        source.status = notepadtxt::FileStatus::ReadError;
                        source.missing = true;
                        if (action.kind == PendingTxtActionKind::Save) {
                            source.conflict = true;
                            source.writePendingRecovery = false;
                            source.pendingExternalMissing = true;
                            source.pendingExternalText = action.value;
                            source.pendingFormat = source.format;
                            source.pendingVersion = {};
                        }
                    }
                    modelChanged = true;
                    debuglog::WriteError(
                        "[notepad][txt] operation item failed id=%llu token=%s source=%s target=%s error=%lu conflict=%d",
                        static_cast<unsigned long long>(result.requestId),
                        item.token.c_str(),
                        item.sourceRelativePath.c_str(),
                        item.targetRelativePath.c_str(),
                        item.error,
                        item.conflict ? 1 : 0);
                }
                statusMessage = UiSettings::Instance().Format(UiText::NotepadTxtOperationFailedFormat, operationError);
                debuglog::WriteError(
                    "[notepad][txt] operation apply failed id=%llu kind=%d error=%lu",
                    static_cast<unsigned long long>(result.requestId),
                    static_cast<int>(result.kind),
                    operationError);
                continue;
            }

            for (const notepadtxt::OperationItemResult& item : result.items) {
                if (NoteEntry* note = FindNote(item.token); note && note->txtSource && item.success) {
                    ApplyTxtSnapshot(*note, item.snapshot);
                    modelChanged = true;
                }
            }

            switch (action.kind) {
            case PendingTxtActionKind::Save:
                break;
            case PendingTxtActionKind::RenameNote:
                RenameNoteModel(action.subject, action.value, false);
                break;
            case PendingTxtActionKind::MoveNote:
                ApplyDropModel(
                    { ItemType::Note, action.subject },
                    action.targetDirectory,
                    action.anchor,
                    action.placement,
                    false);
                break;
            case PendingTxtActionKind::RenameFolder: {
                const std::string newPath = JoinFolderPath(ParentPath(action.subject), action.value);
                RewriteTxtSourceFolderPrefix(action.subject, newPath);
                RenameFolderModel(action.subject, action.value, false);
                break;
            }
            case PendingTxtActionKind::MoveFolder: {
                const std::string newPath = JoinFolderPath(action.targetDirectory, BaseName(action.subject));
                RewriteTxtSourceFolderPrefix(action.subject, newPath);
                ApplyDropModel(
                    { ItemType::Folder, action.subject },
                    action.targetDirectory,
                    action.anchor,
                    action.placement,
                    false);
                break;
            }
            case PendingTxtActionKind::DeleteNote:
                DeleteNoteModel(action.subject, false);
                break;
            case PendingTxtActionKind::DeleteFolder:
                DeleteFolderModel(action.subject, false);
                break;
            }
        }

        if (modelChanged) {
            QueueSave();
        }
        const bool operationsFinished = pendingTxtActions.empty() && !txtOperations.Busy();
        if (operationsFinished) {
            if (!txtOperationPendingStatusMessage.empty()
                && statusMessage == txtOperationPendingStatusMessage) {
                statusMessage.clear();
            }
            txtOperationPendingStatusMessage.clear();
        }
        if (restartScanner && operationsFinished) {
            StartTxtScanner(txtForceRescanOnRestart);
            txtForceRescanOnRestart = false;
        }
    }

    void EnsureLoaded() {
        if (!configLoaded) {
            LoadConfig();
            configLoaded = true;
        }
        ApplyPendingTxtOperations();
        if (!TxtOperationPending()) {
            ApplyPendingTxtScan();
        }
    }

    void LoadConfig() {
        folders.clear();
        notes.clear();
        order.clear();
        currentFolder.clear();
        selectedNoteId.clear();
        selectedFolderPath.clear();
        editing = false;
        editDirty = false;
        editBuffer.clear();
        renderedNoteCache = {};
        renderedEditCache = {};
        EnsureAssetDirectories();

        const jsonutil::JsonObject section = AppConfig::Instance().ReadSectionObject(kNotepadSectionName);
        if (const jsonutil::JsonArray* folderArray = jsonutil::JsonArrayOrNull(&section, "folders")) {
            for (const jsonutil::JsonValue& value : *folderArray) {
                const jsonutil::JsonObject* object = value.TryObject();
                if (!object) {
                    continue;
                }
                FolderEntry folder;
                folder.path = NormalizeFolderPath(jsonutil::JsonStringOr(object, "path", ""));
                folder.createdAt = jsonutil::JsonNumberOr<std::uint64_t>(object, "created_at", UnixTimeNow());
                folder.updatedAt = jsonutil::JsonNumberOr<std::uint64_t>(object, "updated_at", folder.createdAt);
                if (!folder.path.empty() && !FindFolder(folder.path)) {
                    folders.push_back(std::move(folder));
                }
            }
        }

        if (const jsonutil::JsonArray* noteArray = jsonutil::JsonArrayOrNull(&section, "notes")) {
            for (const jsonutil::JsonValue& value : *noteArray) {
                const jsonutil::JsonObject* object = value.TryObject();
                if (!object) {
                    continue;
                }
                NoteEntry note;
                note.id = jsonutil::JsonStringOr(object, "id", "");
                note.title = jsonutil::JsonStringOr(object, "title", UiSettings::Instance().Text(UiText::NotepadUntitled));
                note.folderPath = NormalizeFolderPath(jsonutil::JsonStringOr(object, "folder_path", ""));
                note.text = jsonutil::JsonStringOr(object, "text", "");
                note.favorite = jsonutil::JsonBoolOr(object, "favorite", false);
                note.createdAt = jsonutil::JsonNumberOr<std::uint64_t>(object, "created_at", UnixTimeNow());
                note.updatedAt = jsonutil::JsonNumberOr<std::uint64_t>(object, "updated_at", note.createdAt);
                note.txtSource = DeserializeTxtSource(jsonutil::JsonObjectOrNull(object, "source"));
                if (note.id.empty()) {
                    note.id = GenerateNoteId();
                }
                if (!FindNote(note.id)) {
                    notes.push_back(std::move(note));
                }
            }
        }

        if (const jsonutil::JsonObject* orderObject = jsonutil::JsonObjectOrNull(&section, "order")) {
            for (const auto& [key, value] : *orderObject) {
                order[NormalizeFolderPath(key)] = DeserializeOrderItems(value.TryArray());
            }
        }

        currentFolder = NormalizeFolderPath(jsonutil::JsonStringOr(&section, "last_open_folder_path", ""));
        selectedNoteId = jsonutil::JsonStringOr(&section, "selected_note_id", "");
        if (!FolderExists(currentFolder)) {
            currentFolder.clear();
        }
        if (!FindNote(selectedNoteId)) {
            selectedNoteId.clear();
        }
        NormalizeModel();
        StartTxtSync();
        debuglog::WriteInfo("[notepad] config loaded folders=%zu notes=%zu", folders.size(), notes.size());
    }

    void NormalizeModel() {
        for (FolderEntry& folder : folders) {
            folder.path = NormalizeFolderPath(folder.path);
        }
        std::sort(folders.begin(), folders.end(), [](const FolderEntry& lhs, const FolderEntry& rhs) {
            return LowerUtf8(lhs.path) < LowerUtf8(rhs.path);
        });

        std::set<std::string> validFolderPaths;
        validFolderPaths.insert("");
        std::unordered_set<std::string> validFolderLookup;
        validFolderLookup.reserve(folders.size() + 1);
        validFolderLookup.insert("");
        std::unordered_map<std::string, std::string> folderParents;
        folderParents.reserve(folders.size());
        std::unordered_map<std::string, std::vector<std::string>> childFolders;
        childFolders.reserve(folders.size() + 1);
        for (const FolderEntry& folder : folders) {
            validFolderPaths.insert(folder.path);
            validFolderLookup.insert(folder.path);
            const std::string parent = ParentPath(folder.path);
            folderParents.emplace(folder.path, parent);
            childFolders[parent].push_back(folder.path);
        }

        std::unordered_map<std::string, const NoteEntry*> notesById;
        notesById.reserve(notes.size());
        std::unordered_map<std::string, std::vector<std::string>> childNotes;
        childNotes.reserve(validFolderPaths.size());
        for (NoteEntry& note : notes) {
            note.folderPath = NormalizeFolderPath(note.folderPath);
            if (!validFolderLookup.contains(note.folderPath)) {
                note.folderPath.clear();
            }
            if (note.title.empty()) {
                note.title = UiSettings::Instance().Text(UiText::NotepadUntitled);
            }
            notesById.emplace(note.id, &note);
            childNotes[note.folderPath].push_back(note.id);
        }

        for (auto it = order.begin(); it != order.end();) {
            if (!validFolderPaths.contains(it->first)) {
                it = order.erase(it);
            } else {
                ++it;
            }
        }
        for (const std::string& path : validFolderPaths) {
            order.try_emplace(path);
        }

        for (auto& [directory, items] : order) {
            static const std::vector<std::string> kEmptyChildren;
            const auto folderChildrenIt = childFolders.find(directory);
            const auto noteChildrenIt = childNotes.find(directory);
            const std::vector<std::string>& folderChildren = folderChildrenIt == childFolders.end()
                ? kEmptyChildren
                : folderChildrenIt->second;
            const std::vector<std::string>& noteChildren = noteChildrenIt == childNotes.end()
                ? kEmptyChildren
                : noteChildrenIt->second;
            std::unordered_set<std::string> seenFolders;
            std::unordered_set<std::string> seenNotes;
            seenFolders.reserve(folderChildren.size());
            seenNotes.reserve(noteChildren.size());
            std::vector<OrderItem> normalized;
            normalized.reserve(items.size() + folderChildren.size() + noteChildren.size());
            for (const OrderItem& item : items) {
                if (item.type == ItemType::Folder) {
                    const auto parentIt = folderParents.find(item.value);
                    if (parentIt != folderParents.end()
                        && parentIt->second == directory
                        && seenFolders.insert(item.value).second) {
                        normalized.push_back({ ItemType::Folder, item.value });
                    }
                } else if (item.type == ItemType::Note) {
                    const auto noteIt = notesById.find(item.value);
                    if (noteIt != notesById.end()
                        && noteIt->second->folderPath == directory
                        && seenNotes.insert(item.value).second) {
                        normalized.push_back({ ItemType::Note, item.value });
                    }
                }
            }

            for (const std::string& folderPath : folderChildren) {
                if (seenFolders.insert(folderPath).second) {
                    normalized.push_back({ ItemType::Folder, folderPath });
                }
            }
            for (const std::string& noteId : noteChildren) {
                if (seenNotes.insert(noteId).second) {
                    normalized.push_back({ ItemType::Note, noteId });
                }
            }
            items = std::move(normalized);
        }
    }

    jsonutil::JsonValue SerializeConfig() const {
        jsonutil::JsonObject root;
        root["schema_version"] = kNotepadSchemaVersion;

        jsonutil::JsonArray folderArray;
        for (const FolderEntry& folder : folders) {
            jsonutil::JsonObject object;
            object["path"] = folder.path;
            object["created_at"] = static_cast<double>(folder.createdAt);
            object["updated_at"] = static_cast<double>(folder.updatedAt);
            folderArray.emplace_back(std::move(object));
        }
        root["folders"] = std::move(folderArray);

        jsonutil::JsonArray noteArray;
        for (const NoteEntry& note : notes) {
            jsonutil::JsonObject object;
            object["id"] = note.id;
            object["title"] = note.title;
            object["folder_path"] = note.folderPath;
            object["text"] = note.text;
            object["favorite"] = note.favorite;
            object["created_at"] = static_cast<double>(note.createdAt);
            object["updated_at"] = static_cast<double>(note.updatedAt);
            if (note.txtSource) {
                object["source"] = jsonutil::JsonValue(SerializeTxtSource(*note.txtSource));
            }
            noteArray.emplace_back(std::move(object));
        }
        root["notes"] = std::move(noteArray);

        jsonutil::JsonObject orderObject;
        for (const auto& [path, items] : order) {
            orderObject[path] = SerializeOrderItems(items);
        }
        root["order"] = std::move(orderObject);
        root["last_open_folder_path"] = currentFolder;
        root["selected_note_id"] = selectedNoteId;
        return jsonutil::JsonValue(std::move(root));
    }

    void QueueSave() {
        NormalizeModel();
        AppConfig::Instance().QueueSectionReplace(std::string(kNotepadSectionName), SerializeConfig());
    }

    void SelectNote(std::string_view id, bool queueSave = true) {
        SaveEditBufferIfNeeded();
        NoteEntry* note = FindNote(id);
        if (!note) {
            return;
        }
        selectedNoteId = note->id;
        selectedFolderPath.clear();
        selectedType = ItemType::Note;
        editing = false;
        editDirty = false;
        editBuffer = note->text;
        editCursor = static_cast<int>(editBuffer.size());
        currentFolder = note->folderPath;
        if (queueSave) {
            QueueSave();
        }
    }

    void SelectFolder(std::string_view path) {
        SaveEditBufferIfNeeded();
        const std::string normalized = NormalizeFolderPath(path);
        if (!normalized.empty() && !FindFolder(normalized)) {
            return;
        }
        selectedFolderPath = normalized;
        selectedNoteId.clear();
        selectedType = ItemType::Folder;
    }

    void OpenFolder(std::string_view path) {
        SaveEditBufferIfNeeded();
        const std::string normalized = NormalizeFolderPath(path);
        if (!normalized.empty() && !FindFolder(normalized)) {
            return;
        }
        currentFolder = normalized;
        selectedFolderPath = normalized;
        selectedNoteId.clear();
        selectedType = ItemType::Folder;
        QueueSave();
    }

    bool TxtOperationPending() const {
        return txtOperations.Busy() || !pendingTxtActions.empty();
    }

    bool PrepareForTxtStructureChange() {
        if (editing && editDirty) {
            SaveEditBufferIfNeeded();
        }
        if (TxtOperationPending()) {
            ShowTxtOperationPendingStatus();
            return false;
        }
        return !editDirty;
    }

    bool HasLiveTxtSourceInTree(std::string_view path) const {
        return std::any_of(notes.begin(), notes.end(), [&](const NoteEntry& note) {
            return note.txtSource && !note.txtSource->missing && PathStartsWith(note.folderPath, path);
        });
    }

    std::string TxtRelativePath(std::string_view folderPath, std::string_view fileName) const {
        fs::path path;
        if (!folderPath.empty()) {
            path = fs::path(Utf8ToWide(folderPath));
        }
        path /= fs::path(Utf8ToWide(fileName));
        return PathToUtf8(path.lexically_normal());
    }

    bool TrackTxtRequest(
        std::uint64_t requestId,
        PendingTxtAction action,
        const std::vector<std::string>& noteIds) {
        if (requestId == 0) {
            statusMessage = UiSettings::Instance().Format(UiText::NotepadTxtOperationFailedFormat, ERROR_NOT_READY);
            return false;
        }
        const bool firstPendingRequest = pendingTxtActions.empty();
        action.noteIds = noteIds;
        pendingTxtActions.emplace(requestId, std::move(action));
        if (firstPendingRequest) {
            txtSync.Stop();
        }
        for (const std::string& noteId : noteIds) {
            if (NoteEntry* note = FindNote(noteId); note && note->txtSource) {
                note->txtSource->operationPending = true;
            }
        }
        ShowTxtOperationPendingStatus();
        return true;
    }

    bool QueueTxtWrite(NoteEntry& note, std::string text) {
        if (!note.txtSource || note.txtSource->operationPending) {
            ShowTxtOperationPendingStatus();
            return false;
        }
        TxtSourceState& source = *note.txtSource;
        const std::string previousText = note.text;
        const std::uint64_t requestId = txtOperations.QueueWrite(
            note.id,
            source.relativePath,
            text,
            source.format,
            source.version,
            source.missing);
        PendingTxtAction action;
        action.kind = PendingTxtActionKind::Save;
        action.subject = note.id;
        action.value = previousText;
        if (!TrackTxtRequest(requestId, std::move(action), { note.id })) {
            return false;
        }
        source.writePendingRecovery = true;
        note.text = std::move(text);
        note.updatedAt = UnixTimeNow();
        QueueSave();
        return true;
    }

    bool QueueTxtNoteMove(
        NoteEntry& note,
        std::string targetRelativePath,
        PendingTxtAction action) {
        if (!note.txtSource || note.txtSource->operationPending) {
            ShowTxtOperationPendingStatus();
            return false;
        }
        notepadtxt::MoveItem item;
        item.token = note.id;
        item.sourceRelativePath = note.txtSource->relativePath;
        item.targetRelativePath = std::move(targetRelativePath);
        item.expectedVersion = note.txtSource->version;
        const std::uint64_t requestId = txtOperations.QueueMove({ std::move(item) });
        return TrackTxtRequest(requestId, std::move(action), { note.id });
    }

    bool QueueTxtFolderMove(
        std::string_view oldPath,
        std::string_view newPath,
        PendingTxtAction action) {
        std::vector<notepadtxt::MoveItem> items;
        std::vector<std::string> noteIds;
        for (const NoteEntry& note : notes) {
            if (!note.txtSource || note.txtSource->missing || !PathStartsWith(note.folderPath, oldPath)) {
                continue;
            }
            const std::string targetFolder = ReplacePathPrefix(note.folderPath, oldPath, newPath);
            const fs::path sourcePath(Utf8ToWide(note.txtSource->relativePath));
            notepadtxt::MoveItem item;
            item.token = note.id;
            item.sourceRelativePath = note.txtSource->relativePath;
            item.targetRelativePath = TxtRelativePath(targetFolder, PathToUtf8(sourcePath.filename()));
            item.expectedVersion = note.txtSource->version;
            items.push_back(std::move(item));
            noteIds.push_back(note.id);
        }
        if (items.empty()) {
            return false;
        }
        const std::uint64_t requestId = txtOperations.QueueMove(std::move(items));
        return TrackTxtRequest(requestId, std::move(action), noteIds);
    }

    bool QueueTxtDelete(
        std::vector<notepadtxt::DeleteItem> items,
        std::vector<std::string> noteIds,
        PendingTxtAction action) {
        if (items.empty()) {
            return false;
        }
        const std::uint64_t requestId = txtOperations.QueueDelete(std::move(items));
        return TrackTxtRequest(requestId, std::move(action), noteIds);
    }

    void SaveEditBufferIfNeeded() {
        if (!editing || !editDirty) {
            return;
        }
        NoteEntry* note = FindNote(selectedNoteId);
        if (!note) {
            editing = false;
            editDirty = false;
            return;
        }
        if (note->txtSource) {
            if (note->txtSource->conflict) {
                note->text = editBuffer;
                note->updatedAt = UnixTimeNow();
                editDirty = false;
                QueueSave();
                statusMessage = UiSettings::Instance().Text(UiText::NotepadTxtConflict);
                return;
            }
            if (!QueueTxtWrite(*note, editBuffer)) {
                return;
            }
            editDirty = false;
            debuglog::WriteInfo("[notepad][txt] note write queued id=%s len=%zu", note->id.c_str(), note->text.size());
            return;
        }
        note->text = editBuffer;
        note->updatedAt = UnixTimeNow();
        editDirty = false;
        QueueSave();
        debuglog::WriteInfo("[notepad] note saved id=%s len=%zu", note->id.c_str(), note->text.size());
    }

    void CreateFolder(std::string_view parent, std::string_view name) {
        const std::string cleanName = TrimAscii(name);
        const std::string cleanParent = NormalizeFolderPath(parent);
        if (!IsValidFolderName(cleanName)) {
            statusMessage = UiSettings::Instance().Text(UiText::NotepadInvalidName);
            return;
        }
        if (FolderNameExists(cleanParent, cleanName)) {
            statusMessage = UiSettings::Instance().Text(UiText::NotepadFolderExists);
            return;
        }
        FolderEntry folder;
        folder.path = JoinFolderPath(cleanParent, cleanName);
        folder.createdAt = UnixTimeNow();
        folder.updatedAt = folder.createdAt;
        folders.push_back(folder);
        order.try_emplace(folder.path);
        order[cleanParent].push_back({ ItemType::Folder, folder.path });
        SelectFolder(folder.path);
        QueueSave();
        debuglog::WriteInfo("[notepad] folder created path=%s", folder.path.c_str());
    }

    void CreateNote(std::string_view folderPath, std::string_view title) {
        NoteEntry note;
        note.id = GenerateNoteId();
        note.title = TrimAscii(title);
        if (note.title.empty()) {
            note.title = UiSettings::Instance().Text(UiText::NotepadUntitled);
        }
        note.folderPath = NormalizeFolderPath(folderPath);
        note.createdAt = UnixTimeNow();
        note.updatedAt = note.createdAt;
        notes.push_back(note);
        order[note.folderPath].push_back({ ItemType::Note, note.id });
        SelectNote(note.id, false);
        editing = true;
        editBuffer = note.text;
        editDirty = false;
        QueueSave();
        debuglog::WriteInfo("[notepad] note created id=%s folder=%s", note.id.c_str(), note.folderPath.c_str());
    }

    void DuplicateNote(std::string_view id) {
        const NoteEntry* original = FindNote(id);
        if (!original) {
            return;
        }
        NoteEntry copy = *original;
        copy.id = GenerateNoteId();
        copy.title += UiSettings::Instance().Text(UiText::NotepadCopySuffix);
        copy.favorite = false;
        copy.txtSource.reset();
        copy.createdAt = UnixTimeNow();
        copy.updatedAt = copy.createdAt;
        notes.push_back(copy);
        order[copy.folderPath].push_back({ ItemType::Note, copy.id });
        SelectNote(copy.id, false);
        QueueSave();
    }

    void RenameNoteModel(std::string_view id, std::string_view title, bool queueSave = true) {
        NoteEntry* note = FindNote(id);
        if (!note) {
            return;
        }
        const std::string cleanTitle = TrimAscii(title);
        note->title = cleanTitle.empty() ? UiSettings::Instance().Text(UiText::NotepadUntitled) : cleanTitle;
        note->updatedAt = UnixTimeNow();
        if (queueSave) {
            QueueSave();
        }
    }

    void RenameFolderModel(std::string_view path, std::string_view newName, bool queueSave = true) {
        const std::string oldPath = NormalizeFolderPath(path);
        FolderEntry* folder = FindFolder(oldPath);
        const std::string cleanName = TrimAscii(newName);
        if (!folder || !IsValidFolderName(cleanName)) {
            statusMessage = UiSettings::Instance().Text(UiText::NotepadInvalidName);
            return;
        }
        const std::string parent = ParentPath(oldPath);
        const std::string newPath = JoinFolderPath(parent, cleanName);
        if (newPath != oldPath && FolderNameExists(parent, cleanName, oldPath)) {
            statusMessage = UiSettings::Instance().Text(UiText::NotepadFolderExists);
            return;
        }

        for (FolderEntry& item : folders) {
            if (PathStartsWith(item.path, oldPath)) {
                item.path = ReplacePathPrefix(item.path, oldPath, newPath);
                item.updatedAt = UnixTimeNow();
            }
        }
        for (NoteEntry& note : notes) {
            if (PathStartsWith(note.folderPath, oldPath)) {
                note.folderPath = ReplacePathPrefix(note.folderPath, oldPath, newPath);
                note.updatedAt = UnixTimeNow();
            }
        }
        RewriteOrderFolderPrefix(oldPath, newPath);
        if (currentFolder == oldPath || PathStartsWith(currentFolder, oldPath)) {
            currentFolder = ReplacePathPrefix(currentFolder, oldPath, newPath);
        }
        if (selectedFolderPath == oldPath || PathStartsWith(selectedFolderPath, oldPath)) {
            selectedFolderPath = ReplacePathPrefix(selectedFolderPath, oldPath, newPath);
        }
        if (queueSave) {
            QueueSave();
        }
    }

    void DeleteNoteModel(std::string_view id, bool queueSave = true) {
        const auto it = std::find_if(notes.begin(), notes.end(), [&](const NoteEntry& note) {
            return note.id == id;
        });
        if (it == notes.end()) {
            return;
        }
        const std::string removedId = it->id;
        notes.erase(it);
        RemoveOrderItem({ ItemType::Note, removedId });
        if (selectedNoteId == removedId) {
            selectedNoteId.clear();
            editing = false;
            editBuffer.clear();
        }
        if (queueSave) {
            QueueSave();
        }
        debuglog::WriteInfo("[notepad] note deleted id=%s", removedId.c_str());
    }

    void DeleteFolderModel(std::string_view path, bool queueSave = true) {
        const std::string removedPath = NormalizeFolderPath(path);
        if (removedPath.empty() || !FindFolder(removedPath)) {
            return;
        }
        folders.erase(std::remove_if(folders.begin(), folders.end(), [&](const FolderEntry& folder) {
            return PathStartsWith(folder.path, removedPath);
        }), folders.end());
        notes.erase(std::remove_if(notes.begin(), notes.end(), [&](const NoteEntry& note) {
            return PathStartsWith(note.folderPath, removedPath);
        }), notes.end());
        for (auto it = order.begin(); it != order.end();) {
            if (PathStartsWith(it->first, removedPath)) {
                it = order.erase(it);
            } else {
                ++it;
            }
        }
        RemoveOrderItem({ ItemType::Folder, removedPath });
        if (PathStartsWith(currentFolder, removedPath)) {
            currentFolder = ParentPath(removedPath);
        }
        if (PathStartsWith(selectedFolderPath, removedPath)) {
            selectedFolderPath.clear();
        }
        if (!selectedNoteId.empty() && !FindNote(selectedNoteId)) {
            selectedNoteId.clear();
            editing = false;
            editBuffer.clear();
        }
        if (queueSave) {
            QueueSave();
        }
        debuglog::WriteInfo("[notepad] folder deleted path=%s", removedPath.c_str());
    }

    void RemoveOrderItem(const OrderItem& item) {
        for (auto& [_, items] : order) {
            items.erase(std::remove_if(items.begin(), items.end(), [&](const OrderItem& existing) {
                return existing.type == item.type && existing.value == item.value;
            }), items.end());
        }
    }

    void InsertOrderItem(std::string_view directory, OrderItem item, const std::optional<OrderItem>& anchor, DropPlacement placement) {
        std::vector<OrderItem>& items = order[NormalizeFolderPath(directory)];
        std::size_t index = items.size();
        if (anchor.has_value() && placement != DropPlacement::End) {
            const auto it = std::find_if(items.begin(), items.end(), [&](const OrderItem& existing) {
                return existing.type == anchor->type && existing.value == anchor->value;
            });
            if (it != items.end()) {
                index = static_cast<std::size_t>(std::distance(items.begin(), it));
                if (placement == DropPlacement::After) {
                    ++index;
                }
            }
        }
        items.insert(items.begin() + static_cast<std::ptrdiff_t>(std::min(index, items.size())), std::move(item));
    }

    void RewriteOrderFolderPrefix(std::string_view oldPath, std::string_view newPath) {
        std::map<std::string, std::vector<OrderItem>> rewritten;
        for (auto& [key, items] : order) {
            const std::string newKey = PathStartsWith(key, oldPath) ? ReplacePathPrefix(key, oldPath, newPath) : key;
            for (OrderItem& item : items) {
                if (item.type == ItemType::Folder && PathStartsWith(item.value, oldPath)) {
                    item.value = ReplacePathPrefix(item.value, oldPath, newPath);
                }
            }
            auto& target = rewritten[newKey];
            target.insert(target.end(), items.begin(), items.end());
        }
        order = std::move(rewritten);
    }

    bool ApplyDropModel(
        const OrderItem& dragged,
        std::string targetDirectory,
        std::optional<OrderItem> anchor,
        DropPlacement placement,
        bool queueSave = true) {
        targetDirectory = NormalizeFolderPath(targetDirectory);
        if (anchor.has_value() && anchor->type == dragged.type && anchor->value == dragged.value && placement != DropPlacement::Inside) {
            return false;
        }
        if (dragged.type == ItemType::Note) {
            NoteEntry* note = FindNote(dragged.value);
            if (!note || !FolderExists(targetDirectory)) {
                return false;
            }
            RemoveOrderItem(dragged);
            note->folderPath = targetDirectory;
            note->updatedAt = UnixTimeNow();
            InsertOrderItem(targetDirectory, dragged, anchor, placement);
            SelectNote(note->id, false);
            if (queueSave) {
                QueueSave();
            }
            return true;
        }

        FolderEntry* folder = FindFolder(dragged.value);
        if (!folder || !FolderExists(targetDirectory)) {
            return false;
        }
        const std::string oldPath = folder->path;
        if (targetDirectory == oldPath || PathStartsWith(targetDirectory, oldPath)) {
            return false;
        }
        const std::string name = BaseName(oldPath);
        std::string newPath = JoinFolderPath(targetDirectory, name);
        if (newPath != oldPath && FolderNameExists(targetDirectory, name, oldPath)) {
            return false;
        }

        RemoveOrderItem({ ItemType::Folder, oldPath });
        if (newPath != oldPath) {
            for (FolderEntry& item : folders) {
                if (PathStartsWith(item.path, oldPath)) {
                    item.path = ReplacePathPrefix(item.path, oldPath, newPath);
                    item.updatedAt = UnixTimeNow();
                }
            }
            for (NoteEntry& note : notes) {
                if (PathStartsWith(note.folderPath, oldPath)) {
                    note.folderPath = ReplacePathPrefix(note.folderPath, oldPath, newPath);
                    note.updatedAt = UnixTimeNow();
                }
            }
            RewriteOrderFolderPrefix(oldPath, newPath);
            if (PathStartsWith(currentFolder, oldPath)) {
                currentFolder = ReplacePathPrefix(currentFolder, oldPath, newPath);
            }
            if (PathStartsWith(selectedFolderPath, oldPath)) {
                selectedFolderPath = ReplacePathPrefix(selectedFolderPath, oldPath, newPath);
            }
        }
        InsertOrderItem(targetDirectory, { ItemType::Folder, newPath }, anchor, placement);
        SelectFolder(newPath);
        if (queueSave) {
            QueueSave();
        }
        return true;
    }

    void RenameNote(std::string_view id, std::string_view title) {
        NoteEntry* note = FindNote(id);
        if (!note) {
            return;
        }
        if (!note->txtSource) {
            RenameNoteModel(id, title);
            return;
        }
        if (!PrepareForTxtStructureChange()) {
            return;
        }
        const std::string cleanTitle = SanitizeFileStem(
            TrimAscii(title),
            UiSettings::Instance().Text(UiText::NotepadUntitled));
        const std::string targetRelativePath = TxtRelativePath(note->folderPath, cleanTitle + ".txt");
        if (targetRelativePath == note->txtSource->relativePath) {
            note->txtSource->relativePath = targetRelativePath;
            RenameNoteModel(id, cleanTitle);
            return;
        }
        if (note->txtSource->missing) {
            note->txtSource->relativePath = targetRelativePath;
            RenameNoteModel(id, cleanTitle);
            return;
        }
        PendingTxtAction action;
        action.kind = PendingTxtActionKind::RenameNote;
        action.subject = note->id;
        action.value = cleanTitle;
        QueueTxtNoteMove(*note, targetRelativePath, std::move(action));
    }

    void RenameFolder(std::string_view path, std::string_view newName) {
        const std::string oldPath = NormalizeFolderPath(path);
        const std::string cleanName = TrimAscii(newName);
        if (!FindFolder(oldPath) || !IsValidFolderName(cleanName)) {
            statusMessage = UiSettings::Instance().Text(UiText::NotepadInvalidName);
            return;
        }
        const bool hasLiveTxtSources = HasLiveTxtSourceInTree(oldPath);
        if (hasLiveTxtSources && SanitizeFileStem(cleanName, "") != cleanName) {
            statusMessage = UiSettings::Instance().Text(UiText::NotepadInvalidName);
            return;
        }
        const std::string newPath = JoinFolderPath(ParentPath(oldPath), cleanName);
        if (newPath != oldPath && FolderNameExists(ParentPath(oldPath), cleanName, oldPath)) {
            statusMessage = UiSettings::Instance().Text(UiText::NotepadFolderExists);
            return;
        }
        if (newPath == oldPath) {
            return;
        }
        if (!PrepareForTxtStructureChange()) {
            return;
        }
        PendingTxtAction action;
        action.kind = PendingTxtActionKind::RenameFolder;
        action.subject = oldPath;
        action.value = cleanName;
        if (hasLiveTxtSources) {
            if (QueueTxtFolderMove(oldPath, newPath, std::move(action))) {
                editing = false;
                editDirty = false;
            }
            return;
        }
        RenameFolderModel(oldPath, cleanName);
        RewriteTxtSourceFolderPrefix(oldPath, newPath);
        QueueSave();
    }

    void DeleteNote(std::string_view id) {
        NoteEntry* note = FindNote(id);
        if (!note) {
            return;
        }
        if (!note->txtSource || note->txtSource->missing) {
            DeleteNoteModel(id);
            return;
        }
        if (!PrepareForTxtStructureChange()) {
            return;
        }
        notepadtxt::DeleteItem item;
        item.token = note->id;
        item.relativePath = note->txtSource->relativePath;
        item.expectedVersion = note->txtSource->version;
        PendingTxtAction action;
        action.kind = PendingTxtActionKind::DeleteNote;
        action.subject = note->id;
        if (QueueTxtDelete({ std::move(item) }, { note->id }, std::move(action))) {
            editing = false;
            editDirty = false;
        }
    }

    void DeleteFolder(std::string_view path) {
        const std::string removedPath = NormalizeFolderPath(path);
        if (removedPath.empty() || !FindFolder(removedPath)) {
            return;
        }
        if (!PrepareForTxtStructureChange()) {
            return;
        }
        std::vector<notepadtxt::DeleteItem> items;
        std::vector<std::string> noteIds;
        for (const NoteEntry& note : notes) {
            if (!note.txtSource || note.txtSource->missing || !PathStartsWith(note.folderPath, removedPath)) {
                continue;
            }
            notepadtxt::DeleteItem item;
            item.token = note.id;
            item.relativePath = note.txtSource->relativePath;
            item.expectedVersion = note.txtSource->version;
            items.push_back(std::move(item));
            noteIds.push_back(note.id);
        }
        if (items.empty()) {
            DeleteFolderModel(removedPath);
            return;
        }
        PendingTxtAction action;
        action.kind = PendingTxtActionKind::DeleteFolder;
        action.subject = removedPath;
        if (QueueTxtDelete(std::move(items), std::move(noteIds), std::move(action))) {
            editing = false;
            editDirty = false;
        }
    }

    bool ApplyDrop(
        const OrderItem& dragged,
        std::string targetDirectory,
        std::optional<OrderItem> anchor,
        DropPlacement placement) {
        targetDirectory = NormalizeFolderPath(targetDirectory);
        if (dragged.type == ItemType::Note) {
            NoteEntry* note = FindNote(dragged.value);
            if (!note || !FolderExists(targetDirectory) || !note->txtSource) {
                return ApplyDropModel(dragged, std::move(targetDirectory), std::move(anchor), placement);
            }
            if (!PrepareForTxtStructureChange()) {
                return false;
            }
            if (note->folderPath == targetDirectory) {
                return ApplyDropModel(dragged, std::move(targetDirectory), std::move(anchor), placement);
            }
            if (note->txtSource->missing) {
                const bool applied = ApplyDropModel(dragged, targetDirectory, anchor, placement);
                if (applied) {
                    const fs::path sourcePath(Utf8ToWide(note->txtSource->relativePath));
                    note->txtSource->relativePath = TxtRelativePath(targetDirectory, PathToUtf8(sourcePath.filename()));
                    QueueSave();
                }
                return applied;
            }
            const fs::path sourcePath(Utf8ToWide(note->txtSource->relativePath));
            PendingTxtAction action;
            action.kind = PendingTxtActionKind::MoveNote;
            action.subject = note->id;
            action.targetDirectory = targetDirectory;
            action.anchor = anchor;
            action.placement = placement;
            const bool queued = QueueTxtNoteMove(
                *note,
                TxtRelativePath(targetDirectory, PathToUtf8(sourcePath.filename())),
                std::move(action));
            if (queued) {
                editing = false;
                editDirty = false;
            }
            return queued;
        }

        FolderEntry* folder = FindFolder(dragged.value);
        if (!folder || !FolderExists(targetDirectory)) {
            return false;
        }
        const std::string oldPath = folder->path;
        if (targetDirectory == oldPath || PathStartsWith(targetDirectory, oldPath)) {
            return false;
        }
        const std::string newPath = JoinFolderPath(targetDirectory, BaseName(oldPath));
        if (newPath != oldPath && FolderNameExists(targetDirectory, BaseName(oldPath), oldPath)) {
            return false;
        }
        if (newPath == oldPath) {
            return ApplyDropModel(dragged, targetDirectory, anchor, placement);
        }
        if (!PrepareForTxtStructureChange()) {
            return false;
        }
        PendingTxtAction action;
        action.kind = PendingTxtActionKind::MoveFolder;
        action.subject = oldPath;
        action.targetDirectory = targetDirectory;
        action.anchor = anchor;
        action.placement = placement;
        if (HasLiveTxtSourceInTree(oldPath)) {
            const bool queued = QueueTxtFolderMove(oldPath, newPath, std::move(action));
            if (queued) {
                editing = false;
                editDirty = false;
            }
            return queued;
        }
        const bool applied = ApplyDropModel(dragged, targetDirectory, anchor, placement);
        if (applied) {
            RewriteTxtSourceFolderPrefix(oldPath, newPath);
            QueueSave();
        }
        return applied;
    }

    int CountDescendantFolders(std::string_view path) const {
        int count = 0;
        for (const FolderEntry& folder : folders) {
            if (folder.path != path && PathStartsWith(folder.path, path)) {
                ++count;
            }
        }
        return count;
    }

    int CountNotesInFolderTree(std::string_view path) const {
        int count = 0;
        for (const NoteEntry& note : notes) {
            if (PathStartsWith(note.folderPath, path)) {
                ++count;
            }
        }
        return count;
    }

    std::vector<RowItem> BuildRows(std::string_view directory) const {
        const std::string normalized = NormalizeFolderPath(directory);
        std::vector<RowItem> rows;
        const auto orderIt = order.find(normalized);
        if (orderIt == order.end()) {
            return rows;
        }
        for (const OrderItem& item : orderIt->second) {
            if (item.type == ItemType::Folder) {
                if (const FolderEntry* folder = FindFolder(item.value)) {
                    rows.push_back({ ItemType::Folder, folder->path, BaseName(folder->path) });
                }
            } else if (const NoteEntry* note = FindNote(item.value)) {
                rows.push_back({ ItemType::Note, note->id, note->title });
            }
        }
        return rows;
    }

    std::vector<const NoteEntry*> FavoriteNotes() const {
        std::vector<const NoteEntry*> result;
        for (const NoteEntry& note : notes) {
            if (note.favorite) {
                result.push_back(&note);
            }
        }
        return result;
    }

    std::vector<const NoteEntry*> SearchNotes(std::string_view query) const {
        std::vector<const NoteEntry*> result;
        for (const NoteEntry& note : notes) {
            if (ContainsNoCase(note.title, query) || ContainsNoCase(note.text, query) || ContainsNoCase(note.folderPath, query)) {
                result.push_back(&note);
            }
        }
        return result;
    }

    const std::string& RenderedText(const NoteEntry& note) {
        if (!applyTags || !tagsModule) {
            ++lastRenderStats.previewCacheHits;
            lastRenderStats.renderedBytes = note.text.size();
            return note.text;
        }
        if (renderedNoteCache.valid
            && renderedNoteCache.noteId == note.id
            && renderedNoteCache.updatedAt == note.updatedAt
            && renderedNoteCache.applyTags == applyTags
            && renderedNoteCache.tagsModule == tagsModule
            && renderedNoteCache.source == note.text) {
            ++lastRenderStats.previewCacheHits;
            lastRenderStats.renderedBytes = renderedNoteCache.rendered.size();
            return renderedNoteCache.rendered;
        }

        const double beginMs = NotepadPerfNowMs();
        renderedNoteCache.noteId = note.id;
        renderedNoteCache.updatedAt = note.updatedAt;
        renderedNoteCache.applyTags = applyTags;
        renderedNoteCache.tagsModule = tagsModule;
        renderedNoteCache.source = note.text;
        renderedNoteCache.rendered = tagsModule->ExpandText(note.text, TagsModule::EvaluationContext{
                                                                         nullptr,
                                                                         "notepad",
                                                                         {},
                                                                         {},
                                                                         true,
                                                                     });
        renderedNoteCache.valid = true;
        lastRenderStats.tagsMs += NotepadPerfNowMs() - beginMs;
        lastRenderStats.renderedBytes = renderedNoteCache.rendered.size();
        ++lastRenderStats.previewCacheMisses;
        return renderedNoteCache.rendered;
    }

    const std::string& RenderedEditText() {
        if (!applyTags || !tagsModule) {
            ++lastRenderStats.previewCacheHits;
            lastRenderStats.renderedBytes = editBuffer.size();
            return editBuffer;
        }
        if (renderedEditCache.valid
            && renderedEditCache.applyTags == applyTags
            && renderedEditCache.tagsModule == tagsModule
            && renderedEditCache.source == editBuffer) {
            ++lastRenderStats.previewCacheHits;
            lastRenderStats.renderedBytes = renderedEditCache.rendered.size();
            return renderedEditCache.rendered;
        }

        const double beginMs = NotepadPerfNowMs();
        renderedEditCache.applyTags = applyTags;
        renderedEditCache.tagsModule = tagsModule;
        renderedEditCache.source = editBuffer;
        renderedEditCache.rendered = tagsModule->ExpandText(editBuffer, TagsModule::EvaluationContext{
                                                                       nullptr,
                                                                       "notepad",
                                                                       {},
                                                                       {},
                                                                       true,
                                                                   });
        renderedEditCache.valid = true;
        lastRenderStats.tagsMs += NotepadPerfNowMs() - beginMs;
        lastRenderStats.renderedBytes = renderedEditCache.rendered.size();
        ++lastRenderStats.previewCacheMisses;
        return renderedEditCache.rendered;
    }

    void DrawMainTab(IDirect3DDevice9* device) {
        const double totalBeginMs = NotepadPerfNowMs();
        lastRenderStats = {};
        double stageBeginMs = totalBeginMs;
        EnsureLoaded();
        lastRenderStats.loadMs = NotepadPerfNowMs() - stageBeginMs;
        lastRenderStats.folders = static_cast<int>(folders.size());
        lastRenderStats.notes = static_cast<int>(notes.size());
        lastRenderStats.editing = editing;
        lastRenderStats.copyLineMode = copyLineMode;
        lastRenderStats.applyTags = applyTags;

        stageBeginMs = NotepadPerfNowMs();
        HandleKeyboardShortcuts();
        lastRenderStats.shortcutsMs = NotepadPerfNowMs() - stageBeginMs;

        UiSettings& ui = UiSettings::Instance();
        ImGui::SeparatorText(ui.Text(UiText::TabNotepad));
        if (!statusMessage.empty()) {
            const ImVec4 disabledColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            DrawEllipsizedText(statusMessage, ImGui::GetContentRegionAvail().x, &disabledColor);
        }
        ImGui::Spacing();

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const bool narrow = avail.x < ScaleUi(900.0f);
        if (narrow) {
            const float preferredNavHeight = std::clamp(avail.y * 0.34f, ScaleUi(120.0f), ScaleUi(300.0f));
            const float maxNavHeight = std::max(ScaleUi(90.0f), avail.y - ScaleUi(190.0f));
            const float navHeight = std::min(preferredNavHeight, maxNavHeight);
            if (ImGui::BeginChild("notepad_left_top", ImVec2(0.0f, navHeight), ImGuiChildFlags_None)) {
                stageBeginMs = NotepadPerfNowMs();
                DrawLeftPanel();
                lastRenderStats.leftPanelMs += NotepadPerfNowMs() - stageBeginMs;
            }
            ImGui::EndChild();
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            if (ImGui::BeginChild("notepad_right_bottom", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
                stageBeginMs = NotepadPerfNowMs();
                DrawRightPanel(device);
                lastRenderStats.rightPanelMs += NotepadPerfNowMs() - stageBeginMs;
            }
            ImGui::EndChild();
        } else {
            const float leftWidth = std::clamp(avail.x * 0.30f, ScaleUi(260.0f), ScaleUi(340.0f));
            if (ImGui::BeginChild("notepad_left", ImVec2(leftWidth, 0.0f), ImGuiChildFlags_None)) {
                stageBeginMs = NotepadPerfNowMs();
                DrawLeftPanel();
                lastRenderStats.leftPanelMs += NotepadPerfNowMs() - stageBeginMs;
            }
            ImGui::EndChild();
            ImGui::SameLine(0.0f, ScaleUi(10.0f));
            if (ImGui::BeginChild("notepad_right", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
                stageBeginMs = NotepadPerfNowMs();
                DrawRightPanel(device);
                lastRenderStats.rightPanelMs += NotepadPerfNowMs() - stageBeginMs;
            }
            ImGui::EndChild();
        }

        stageBeginMs = NotepadPerfNowMs();
        DrawModals();
        lastRenderStats.modalsMs = NotepadPerfNowMs() - stageBeginMs;
        lastRenderStats.totalMs = NotepadPerfNowMs() - totalBeginMs;
    }

    void HandleKeyboardShortcuts() {
        const ImGuiIO& io = ImGui::GetIO();
        if (editing && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            SaveEditBufferIfNeeded();
            editing = false;
        }
        if (ImGui::IsAnyItemActive()) {
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            if (!selectedNoteId.empty()) {
                OpenDeleteNoteModal(selectedNoteId);
            } else if (!selectedFolderPath.empty()) {
                OpenDeleteFolderModal(selectedFolderPath);
            }
        } else if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
            if (!selectedNoteId.empty()) {
                OpenRenameNoteModal(selectedNoteId);
            } else if (!selectedFolderPath.empty()) {
                OpenRenameFolderModal(selectedFolderPath);
            }
        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            if (editing) {
                editing = false;
                editDirty = false;
                if (const NoteEntry* note = FindNote(selectedNoteId)) {
                    editBuffer = note->text;
                }
            }
        } else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            if (!selectedFolderPath.empty()) {
                OpenFolder(selectedFolderPath);
            }
        }
    }

    bool DrawToolbarButton(const char* icon, const char* id, const char* tooltip) {
        const std::string label = std::string(icon) + "##" + id;
        const bool pressed = ImGui::Button(label.c_str(), ScaleUi(30.0f, 28.0f));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip);
        }
        return pressed;
    }

    std::string CompactBreadcrumbLabel() const {
        UiSettings& ui = UiSettings::Instance();
        if (currentFolder.empty()) {
            return ui.Text(UiText::NotepadRootName);
        }
        const std::vector<std::string> parts = SplitPath(currentFolder);
        if (parts.size() <= 2) {
            return std::string(ui.Text(UiText::NotepadRootName)) + " / " + currentFolder;
        }
        return std::string(ui.Text(UiText::NotepadRootName)) + " / ... / " + parts.back();
    }

    void DrawFavoriteMarker(const NoteEntry& note) const {
        if (!note.favorite) {
            return;
        }
        const ImVec2 rowMin = ImGui::GetItemRectMin();
        const ImVec2 rowMax = ImGui::GetItemRectMax();
        const ImVec2 textSize = ImGui::CalcTextSize(ui_icons::Star);
        const ImVec2 pos(rowMax.x - textSize.x - ScaleUi(6.0f), rowMin.y + (rowMax.y - rowMin.y - textSize.y) * 0.5f);
        ImGui::GetWindowDrawList()->AddText(pos, ImGui::GetColorU32(ImVec4(1.0f, 0.82f, 0.18f, 1.0f)), ui_icons::Star);
    }

    void DrawNoteSelectableRow(
        const NoteEntry& note,
        std::string_view id,
        std::string_view label,
        bool selected) {
        const float trailingWidth = note.favorite
            ? ImGui::CalcTextSize(ui_icons::Star).x + ScaleUi(12.0f)
            : 0.0f;
        const SelectableTextResult item = DrawSelectableText(
            id,
            label,
            selected,
            ImGuiSelectableFlags_None,
            ImVec2(0.0f, ScaleUi(24.0f)),
            trailingWidth);
        if (item.pressed) {
            SelectNote(note.id);
            if (!search.empty()) {
                search.clear();
            }
        }
        DrawFavoriteMarker(note);
        DrawNoteContextMenu(note);
        if (item.hovered && item.clipped && !ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            DrawTextTooltip(label);
        }
    }

    void DrawLeftPanel() {
        UiSettings& ui = UiSettings::Instance();
        if (DrawToolbarButton(ui_icons::Plus, "notepad_new_note", ui.Text(UiText::NotepadNewNote))) {
            modalBuffer = ui.Text(UiText::NotepadUntitled);
            modalTarget = currentFolder;
            pendingModal = PendingModal::CreateNote;
            modalOpenRequested = true;
        }
        ImGui::SameLine();
        if (DrawToolbarButton(ui_icons::Folder, "notepad_new_folder", ui.Text(UiText::NotepadNewFolder))) {
            modalBuffer = ui.Text(UiText::NotepadDefaultFolder);
            modalTarget = currentFolder;
            pendingModal = PendingModal::CreateFolder;
            modalOpenRequested = true;
        }
        ImGui::SameLine();
        if (DrawToolbarButton(ui_icons::FileImport, "notepad_import_txt", ui.Text(UiText::NotepadImportTxt))) {
            ImportTxtAsNote();
        }
        ImGui::SameLine();
        if (DrawToolbarButton(ui_icons::RotateLeft, "notepad_refresh_txt", ui.Text(UiText::NotepadTxtRefresh))) {
            if (TxtOperationPending()) {
                ShowTxtOperationPendingStatus();
            } else {
                txtSync.RequestFullScan();
                statusMessage = ui.Text(UiText::NotepadTxtRefresh);
            }
        }

        ImGui::SetNextItemWidth(-1.0f);
        const std::string searchHint = std::string(ui_icons::Search) + " " + ui.Text(UiText::NotepadSearchHint);
        InputTextWithHintString("##notepad_search", searchHint.c_str(), search, 0, 128);
        if (search.empty()) {
            DrawBreadcrumbs();
        }

        if (ImGui::BeginChild("notepad_nav_list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
            if (search.empty()) {
                if (DrawFavorites()) {
                    ImGui::Separator();
                }
                DrawCurrentFolderRows();
            } else {
                DrawSearchResults();
            }
        }
        ImGui::EndChild();
    }

    void DrawBreadcrumbs() {
        UiSettings& ui = UiSettings::Instance();
        ImGui::Spacing();
        if (ImGui::Button((std::string(ui_icons::ChevronLeft) + "##notepad_back_root").c_str(), ScaleUi(26.0f, 0.0f))) {
            OpenFolder("");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", ui.Text(UiText::NotepadRootName));
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(ui_icons::AngleUp) + "##notepad_up").c_str(), ScaleUi(26.0f, 0.0f))) {
            OpenFolder(ParentPath(currentFolder));
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", ui.Text(UiText::EditorBack));
        }
        ImGui::SameLine();
        const std::string label = CompactBreadcrumbLabel();
        const std::string full = currentFolder.empty()
            ? label
            : std::string(ui.Text(UiText::NotepadRootName)) + " / " + currentFolder;
        const bool clipped = DrawEllipsizedText(label, ImGui::GetContentRegionAvail().x, nullptr, full);
        if (!clipped && ImGui::IsItemHovered() && !currentFolder.empty() && label != full) {
            ImGui::SetTooltip("%s", full.c_str());
        }
        ImGui::Spacing();
    }

    bool DrawFavorites() {
        UiSettings& ui = UiSettings::Instance();
        const auto favorites = FavoriteNotes();
        if (favorites.empty()) {
            return false;
        }
        const bool selectedFavoriteVisible = std::any_of(favorites.begin(), favorites.end(), [&](const NoteEntry* note) {
            return note && note->id == selectedNoteId;
        });
        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth
            | (selectedFavoriteVisible ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        const std::string header = std::string(ui_icons::Star) + " " + ui.Text(UiText::NotepadFavorites)
            + " (" + std::to_string(favorites.size()) + ")";
        if (!ImGui::TreeNodeEx("##notepad_favorites", flags, "%s", header.c_str())) {
            return true;
        }
        for (const NoteEntry* note : favorites) {
            if (!note) {
                continue;
            }
            const std::string label = std::string(ui_icons::Book) + " " + note->title;
            DrawNoteSelectableRow(*note, "fav_" + note->id, label, selectedNoteId == note->id);
        }
        ImGui::TreePop();
        return true;
    }

    void DrawCurrentFolderRows() {
        UiSettings& ui = UiSettings::Instance();
        const std::vector<RowItem> rows = BuildRows(currentFolder);
        if (rows.empty()) {
            ImGui::TextDisabled("%s", ui.Text(UiText::NotepadEmptyFolder));
        }
        for (const RowItem& row : rows) {
            DrawRow(row);
        }
        DrawEndDropTarget();
    }

    void DrawRow(const RowItem& row) {
        const bool isFolder = row.type == ItemType::Folder;
        const bool selected = isFolder ? selectedFolderPath == row.value : selectedNoteId == row.value;
        const std::string icon = isFolder ? ui_icons::Folder : ui_icons::Book;
        const std::string label = icon + " " + row.label;
        const NoteEntry* note = isFolder ? nullptr : FindNote(row.value);
        const float trailingWidth = note && note->favorite
            ? ImGui::CalcTextSize(ui_icons::Star).x + ScaleUi(12.0f)
            : 0.0f;
        const SelectableTextResult item = DrawSelectableText(
            "notepad_row_" + row.value,
            label,
            selected,
            ImGuiSelectableFlags_AllowDoubleClick,
            ImVec2(0.0f, ScaleUi(24.0f)),
            trailingWidth);
        if (item.pressed) {
            if (isFolder) {
                SelectFolder(row.value);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    OpenFolder(row.value);
                }
            } else {
                SelectNote(row.value);
            }
        }
        if (note) {
            DrawFavoriteMarker(*note);
        }
        DrawDragSource({ row.type, row.value }, row.label);
        DrawRowDropTarget({ row.type, row.value }, item.rowMin, item.rowMax);
        if (isFolder) {
            const std::string popupId = "notepad_folder_popup_" + EscapeImGuiId(row.value);
            if (ImGui::BeginPopupContextItem(popupId.c_str())) {
                DrawFolderContextActions(row.value);
                ImGui::EndPopup();
            }
        } else if (note) {
            DrawNoteContextMenu(*note);
        }
        if (item.hovered && item.clipped && !ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            DrawTextTooltip(label);
        }
    }

    void DrawDragSource(const OrderItem& item, const std::string& label) {
        if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            return;
        }
        const char* payloadType = item.type == ItemType::Folder ? kPayloadFolder : kPayloadNote;
        ImGui::SetDragDropPayload(payloadType, item.value.c_str(), item.value.size() + 1);
        ImGui::TextUnformatted(label.c_str());
        ImGui::EndDragDropSource();
    }

    void DrawRowDropTarget(const OrderItem& anchor, const ImVec2& rowMin, const ImVec2& rowMax) {
        if (!ImGui::BeginDragDropTarget()) {
            return;
        }
        const float y = ImGui::GetIO().MousePos.y;
        const float height = std::max(1.0f, rowMax.y - rowMin.y);
        DropPlacement placement = DropPlacement::Before;
        std::string targetDirectory = currentFolder;
        if (y > rowMin.y + height * 0.66f) {
            placement = DropPlacement::After;
        } else if (anchor.type == ItemType::Folder && y > rowMin.y + height * 0.33f) {
            placement = DropPlacement::Inside;
            targetDirectory = anchor.value;
        }

        auto acceptPayload = [&](const char* type, ItemType itemType) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(type)) {
                const std::string value(static_cast<const char*>(payload->Data));
                if (payload->IsDelivery()) {
                    std::optional<OrderItem> targetAnchor = placement == DropPlacement::Inside ? std::nullopt : std::optional<OrderItem>(anchor);
                    ApplyDrop({ itemType, value }, targetDirectory, targetAnchor, placement);
                }
            }
        };
        acceptPayload(kPayloadNote, ItemType::Note);
        acceptPayload(kPayloadFolder, ItemType::Folder);
        ImGui::EndDragDropTarget();
    }

    void DrawEndDropTarget() {
        ImGui::Dummy(ImVec2(-1.0f, ScaleUi(30.0f)));
        if (!ImGui::BeginDragDropTarget()) {
            return;
        }
        auto acceptPayload = [&](const char* type, ItemType itemType) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(type)) {
                const std::string value(static_cast<const char*>(payload->Data));
                if (payload->IsDelivery()) {
                    ApplyDrop({ itemType, value }, currentFolder, std::nullopt, DropPlacement::End);
                }
            }
        };
        acceptPayload(kPayloadNote, ItemType::Note);
        acceptPayload(kPayloadFolder, ItemType::Folder);
        ImGui::EndDragDropTarget();
    }

    void DrawSearchResults() {
        UiSettings& ui = UiSettings::Instance();
        const auto results = SearchNotes(search);
        if (results.empty()) {
            ImGui::TextDisabled("%s", ui.Text(UiText::NotepadEmptySearch));
            return;
        }
        ImGui::TextDisabled("%s %zu", ui.Text(UiText::NotepadSearchResults), results.size());
        for (const NoteEntry* note : results) {
            std::string label = std::string(ui_icons::Book) + " " + note->title;
            if (!note->folderPath.empty()) {
                label += "  [" + note->folderPath + "]";
            }
            DrawNoteSelectableRow(*note, "notepad_search_" + note->id, label, selectedNoteId == note->id);
        }
    }

    bool IsNoteEditDisabled(const NoteEntry& note) const {
        const bool sourceUnavailable = note.txtSource
            && !note.txtSource->missing
            && note.txtSource->status != notepadtxt::FileStatus::Ready;
        return TxtOperationPending()
            || sourceUnavailable
            || (note.txtSource && note.txtSource->conflict);
    }

    void BeginEditingNote(const NoteEntry& note) {
        if (selectedNoteId != note.id) {
            SelectNote(note.id);
        }
        editing = true;
        editBuffer = note.text;
        editDirty = false;
        editCursor = static_cast<int>(editBuffer.size());
    }

    void DrawNoteContextMenu(const NoteEntry& note) {
        const std::string popupId = "notepad_note_popup_" + EscapeImGuiId(note.id);
        if (!ImGui::BeginPopupContextItem(popupId.c_str())) {
            return;
        }
        UiSettings& ui = UiSettings::Instance();
        if (ImGui::MenuItem(ui.Text(UiText::Edit), nullptr, false, !IsNoteEditDisabled(note))) {
            BeginEditingNote(note);
        }
        if (ImGui::MenuItem(ui.Text(UiText::FolderRename))) {
            OpenRenameNoteModal(note.id);
        }
        if (ImGui::MenuItem(ui.Text(UiText::ActionDuplicate))) {
            DuplicateNote(note.id);
        }
        if (ImGui::MenuItem(note.favorite ? ui.Text(UiText::NotepadUnfavorite) : ui.Text(UiText::NotepadFavorite))) {
            if (NoteEntry* target = FindNote(note.id)) {
                target->favorite = !target->favorite;
                target->updatedAt = UnixTimeNow();
                QueueSave();
            }
        }
        if (ImGui::MenuItem(ui.Text(UiText::NotepadExportTxt))) {
            ExportNote(note);
        }
        if (ImGui::MenuItem(ui.Text(UiText::Delete))) {
            OpenDeleteNoteModal(note.id);
        }
        ImGui::EndPopup();
    }

    void DrawFolderContextActions(const std::string& path) {
        UiSettings& ui = UiSettings::Instance();
        if (ImGui::MenuItem(ui.Text(UiText::NotepadOpenFolder))) {
            OpenFolder(path);
        }
        if (ImGui::MenuItem(ui.Text(UiText::FolderRename))) {
            OpenRenameFolderModal(path);
        }
        if (ImGui::MenuItem(ui.Text(UiText::Delete))) {
            OpenDeleteFolderModal(path);
        }
    }

    void DrawRightPanel(IDirect3DDevice9* device) {
        UiSettings& ui = UiSettings::Instance();
        NoteEntry* note = selectedNoteId.empty() ? nullptr : FindNote(selectedNoteId);
        if (!note) {
            ImGui::TextColored(ImVec4(0.65f, 0.75f, 0.90f, 1.0f), "%s", ui.Text(UiText::NotepadNoSelection));
            return;
        }

        const float favoriteMarkerWidth = note->favorite
            ? ImGui::CalcTextSize(ui_icons::Star).x + ImGui::GetStyle().ItemSpacing.x
            : 0.0f;
        DrawEllipsizedText(
            note->title,
            std::max(1.0f, ImGui::GetContentRegionAvail().x - favoriteMarkerWidth));
        if (note->favorite) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.15f, 1.0f), "%s", ui_icons::Star);
        }

        const std::string favoriteLabel = std::string(ui_icons::Star) + " "
            + (note->favorite ? ui.Text(UiText::NotepadUnfavorite) : ui.Text(UiText::NotepadFavorite));
        const std::string editLabel = std::string(ui_icons::Edit) + " " + ui.Text(UiText::Edit);
        const std::string exportLabel = std::string(ui_icons::FileExport) + " " + ui.Text(UiText::NotepadExportTxt);
        const std::string applyTagsLabel = ui.Text(UiText::NotepadApplyTags);
        if (ImGui::Button(favoriteLabel.c_str())) {
            note->favorite = !note->favorite;
            note->updatedAt = UnixTimeNow();
            QueueSave();
        }
        ContinueToolbar(ButtonItemWidth(editLabel));
        ImGui::BeginDisabled(IsNoteEditDisabled(*note));
        if (ImGui::Button(editLabel.c_str())) {
            BeginEditingNote(*note);
        }
        ImGui::EndDisabled();
        ContinueToolbar(ButtonItemWidth(exportLabel));
        if (ImGui::Button(exportLabel.c_str())) {
            ExportNote(*note);
        }
        ContinueToolbar(CheckboxItemWidth(applyTagsLabel));
        ImGui::Checkbox(applyTagsLabel.c_str(), &applyTags);

        if (note->txtSource) {
            TxtSourceState& source = *note->txtSource;
            const std::string sourceDescription = std::string(notepadtxt::TextEncodingName(source.format.encoding))
                + ", " + notepadtxt::NewlineStyleName(source.format.newline)
                + (source.format.bom ? ", BOM, " : ", ")
                + source.relativePath;
            const std::string sourceText = ui.Format(UiText::NotepadTxtSourceFormat, sourceDescription.c_str());
            const ImVec4 disabledColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            DrawEllipsizedText(sourceText, ImGui::GetContentRegionAvail().x, &disabledColor);
            if (source.operationPending) {
                ImGui::TextColored(ImVec4(0.90f, 0.75f, 0.25f, 1.0f), "%s", ui.Text(UiText::NotepadTxtOperationPending));
            } else if (source.missing) {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f), "%s", ui.Text(UiText::NotepadTxtMissing));
            } else if (source.status != notepadtxt::FileStatus::Ready) {
                ImGui::TextColored(
                    ImVec4(0.95f, 0.45f, 0.35f, 1.0f),
                    "%s",
                    ui.Format(UiText::NotepadTxtUnavailableFormat, source.error).c_str());
            }
            if (source.conflict) {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.30f, 1.0f), "%s", ui.Text(UiText::NotepadTxtConflict));
                const UiText externalChoice = source.pendingExternalMissing
                    ? UiText::NotepadTxtAcceptDelete
                    : UiText::NotepadTxtUseFile;
                if (ImGui::Button(ui.Text(externalChoice))) {
                    const bool pendingMissing = source.pendingExternalMissing;
                    note->text = source.pendingExternalText;
                    note->updatedAt = UnixTimeNow();
                    if (!pendingMissing) {
                        source.format = source.pendingFormat;
                        source.version = source.pendingVersion;
                        source.status = notepadtxt::FileStatus::Ready;
                        source.error = ERROR_SUCCESS;
                        source.missing = false;
                    }
                    source.conflict = false;
                    source.writePendingRecovery = false;
                    source.pendingExternalMissing = false;
                    source.pendingExternalText.clear();
                    source.pendingFormat = {};
                    source.pendingVersion = {};
                    editing = false;
                    editDirty = false;
                    editBuffer = note->text;
                    renderedNoteCache = {};
                    QueueSave();
                    txtSync.RequestFullScan();
                    statusMessage.clear();
                }
                ContinueToolbar(ButtonItemWidth(ui.Text(UiText::NotepadTxtOverwriteFile)));
                if (ImGui::Button(ui.Text(UiText::NotepadTxtOverwriteFile))) {
                    const TxtSourceState previousSource = source;
                    if (!source.pendingExternalMissing) {
                        source.format = source.pendingFormat;
                        source.version = source.pendingVersion;
                        source.status = notepadtxt::FileStatus::Ready;
                        source.error = ERROR_SUCCESS;
                        source.missing = false;
                    }
                    source.conflict = false;
                    source.writePendingRecovery = false;
                    source.pendingExternalMissing = false;
                    source.pendingExternalText.clear();
                    source.pendingFormat = {};
                    source.pendingVersion = {};
                    editing = false;
                    editDirty = false;
                    editBuffer = note->text;
                    if (!QueueTxtWrite(*note, note->text)) {
                        source = previousSource;
                    }
                }
            }
        }

        ImGui::Separator();
        if (!editing) {
            DrawReadOnlyNote(*note, device);
        } else {
            DrawEditor(*note, device);
        }
    }

    void DrawReadOnlyNote(const NoteEntry& note, IDirect3DDevice9* device) {
        UiSettings& ui = UiSettings::Instance();
        const std::string copyRawLabel = std::string(ui_icons::Copy) + " " + ui.Text(UiText::NotepadCopyRaw);
        const std::string copyRenderedLabel = std::string(ui_icons::Copy) + " " + ui.Text(UiText::NotepadCopyRendered);
        const std::string modeLabel = copyLineMode
            ? ui.Text(UiText::NotepadPreviewMode)
            : ui.Text(UiText::NotepadCopyLine);
        if (ImGui::Button(copyRawLabel.c_str())) {
            ImGui::SetClipboardText(note.text.c_str());
            statusMessage = ui.Text(UiText::ToastClipboardCopied);
        }
        ContinueToolbar(ButtonItemWidth(copyRenderedLabel));
        if (ImGui::Button(copyRenderedLabel.c_str())) {
            const std::string rendered = RenderedText(note);
            ImGui::SetClipboardText(MarkupRenderer::StripMarkup(rendered).c_str());
            statusMessage = ui.Text(UiText::ToastClipboardCopied);
        }
        ContinueToolbar(ButtonItemWidth(modeLabel));
        if (ImGui::Button(modeLabel.c_str())) {
            copyLineMode = !copyLineMode;
        }
        ImGui::Spacing();
        if (ImGui::BeginChild("notepad_preview_read", ImVec2(0.0f, 0.0f), false)) {
            const double previewBeginMs = NotepadPerfNowMs();
            if (copyLineMode) {
                DrawCopyLines(RenderedText(note));
            } else {
                DrawPreviewText(RenderedText(note), device);
            }
            lastRenderStats.readPreviewMs += NotepadPerfNowMs() - previewBeginMs;
        }
        ImGui::EndChild();
    }

    void DrawCopyLines(const std::string& text) {
        const double beginMs = NotepadPerfNowMs();
        if (!copyLinesCache.valid || copyLinesCache.source != text) {
            copyLinesCache.source = text;
            copyLinesCache.lines.clear();
            std::stringstream stream{ text };
            std::string line;
            while (std::getline(stream, line)) {
                std::string plain = MarkupRenderer::StripMarkupLine(line);
                if (plain.empty()) {
                    plain = " ";
                }
                copyLinesCache.lines.push_back(std::move(plain));
            }
            copyLinesCache.valid = true;
        }

        lastRenderStats.copyLinesTotal = static_cast<int>(copyLinesCache.lines.size());
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(copyLinesCache.lines.size()));
        while (clipper.Step()) {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                const std::string& plain = copyLinesCache.lines[static_cast<std::size_t>(index)];
                const SelectableTextResult item = DrawSelectableText(
                    "copy_line_" + std::to_string(index),
                    plain,
                    false,
                    ImGuiSelectableFlags_None,
                    ImVec2(0.0f, 0.0f));
                ++lastRenderStats.copyLinesVisible;
                if (item.pressed) {
                    ImGui::SetClipboardText(plain == " " ? "" : plain.c_str());
                    statusMessage = UiSettings::Instance().Text(UiText::ToastClipboardCopied);
                }
                if (item.hovered && item.clipped) {
                    DrawTextTooltip(plain);
                }
            }
        }
        lastRenderStats.copyLinesMs += NotepadPerfNowMs() - beginMs;
    }

    void DrawEditor(NoteEntry& note, IDirect3DDevice9* device) {
        UiSettings& ui = UiSettings::Instance();
        const std::string saveLabel = std::string(ui_icons::SaveDisk) + " " + ui.Text(UiText::Save);
        const std::string cancelLabel = ui.Text(UiText::Cancel);
        const std::string imageLabel = std::string(ui_icons::Image) + " " + ui.Text(UiText::NotepadInsertImage);
        const std::string iconLabel = std::string(ui_icons::Star) + " " + ui.Text(UiText::HudMarkupIcon);
        const std::string helpLabel = ui.Text(UiText::NotepadMarkupHelp);
        const bool savePressed = ImGui::Button(saveLabel.c_str());
        if (savePressed) {
            editDirty = true;
            SaveEditBufferIfNeeded();
            editing = false;
        }
        ContinueToolbar(ButtonItemWidth(cancelLabel));
        if (ImGui::Button(cancelLabel.c_str())) {
            editing = false;
            editDirty = false;
            editBuffer = note.text;
        }
        ContinueToolbar(ButtonItemWidth(imageLabel));
        if (ImGui::Button(imageLabel.c_str())) {
            InsertImageFromDialog();
        }
        ContinueToolbar(ButtonItemWidth(iconLabel));
        const std::string iconPickerPopup = std::string(ui.Text(UiText::IconPickerTitle)) + "##notepad_icon_picker";
        if (ImGui::Button(iconLabel.c_str())) {
            icon_picker::OpenPopup(iconPickerPopup.c_str());
        }
        ContinueToolbar(ButtonItemWidth(helpLabel));
        if (ImGui::Button(helpLabel.c_str())) {
            ImGui::OpenPopup("notepad_markup_help");
        }

        std::string selectedIconId;
        const float uiScale = std::max(0.01f, ScaleUi(1.0f));
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        const ImVec2 iconPickerSize(
            std::min(560.0f, std::max(280.0f, (displaySize.x - ScaleUi(80.0f)) / uiScale)),
            std::min(460.0f, std::max(180.0f, (displaySize.y - ScaleUi(150.0f)) / uiScale)));
        if (icon_picker::DrawPopup(
                iconPickerState,
                icon_picker::Options{ iconPickerPopup.c_str(), iconPickerSize },
                selectedIconId)) {
            InsertTextAtCursor(icon_picker::MarkupToken(selectedIconId) + " ");
        }
        DrawMarkupHelpPopup();

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const bool vertical = avail.x < ScaleUi(760.0f);
        if (vertical) {
            ImGui::TextDisabled("%s", ui.Text(UiText::NotepadEditor));
            const float fixedRowsHeight = ImGui::GetTextLineHeightWithSpacing() * 2.0f
                + ImGui::GetStyle().ItemSpacing.y * 2.0f;
            const float maxEditorHeight = std::max(
                ScaleUi(80.0f),
                avail.y - ScaleUi(120.0f) - fixedRowsHeight);
            const float editorHeight = std::max(
                ScaleUi(80.0f),
                std::min(std::max(ScaleUi(140.0f), avail.y * 0.48f), maxEditorHeight));
            if (InputTextMultilineString("##notepad_edit", editBuffer, ImVec2(-1.0f, editorHeight), 0, &editCursor)) {
                editDirty = true;
            }
            ImGui::TextDisabled("%s", ui.Text(UiText::NotepadLivePreview));
            if (ImGui::BeginChild("notepad_preview_edit_vertical", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
                const double previewBeginMs = NotepadPerfNowMs();
                DrawPreviewText(RenderedEditText(), device);
                lastRenderStats.editPreviewMs += NotepadPerfNowMs() - previewBeginMs;
            }
            ImGui::EndChild();
        } else {
            const float editorWidth = std::max(ScaleUi(300.0f), avail.x * 0.50f);
            if (ImGui::BeginChild("notepad_edit_column", ImVec2(editorWidth, 0.0f), false)) {
                ImGui::TextDisabled("%s", ui.Text(UiText::NotepadEditor));
                if (InputTextMultilineString("##notepad_edit", editBuffer, ImVec2(-1.0f, -1.0f), 0, &editCursor)) {
                    editDirty = true;
                }
            }
            ImGui::EndChild();
            ImGui::SameLine();
            if (ImGui::BeginChild("notepad_preview_edit", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
                ImGui::TextDisabled("%s", ui.Text(UiText::NotepadLivePreview));
                const double previewBeginMs = NotepadPerfNowMs();
                DrawPreviewText(RenderedEditText(), device);
                lastRenderStats.editPreviewMs += NotepadPerfNowMs() - previewBeginMs;
            }
            ImGui::EndChild();
        }
    }

    void DrawMarkupHelpPopup() {
        UiSettings& ui = UiSettings::Instance();
        if (!ImGui::BeginPopup("notepad_markup_help")) {
            return;
        }
        ImGui::TextUnformatted(ui.Text(UiText::NotepadMarkupHelpTitle));
        ImGui::Separator();
        ImGui::TextUnformatted("{FF0000} text");
        ImGui::TextUnformatted("#center text");
        ImGui::TextUnformatted("#color00ff00 #bg202020 text");
        ImGui::TextUnformatted("#font18 #icon(star) #icon(car) text");
        ImGui::TextUnformatted("text #font18 big #font normal");
        ImGui::TextUnformatted("#shadow #outline HUD text #reset plain");
        ImGui::TextUnformatted("#img(example.png, size(320,180))");
        ImGui::TextUnformatted("#bullet text");
        ImGui::TextUnformatted("#hr");
        const ImVec4 disabledColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        ImGui::PushStyleColor(ImGuiCol_Text, disabledColor);
        ImGui::TextWrapped("%s", ui.Text(UiText::NotepadMarkupHelpHint));
        ImGui::PopStyleColor();
        ImGui::EndPopup();
    }

    void DrawPreviewText(const std::string& text, IDirect3DDevice9* device) {
        const double beginMs = NotepadPerfNowMs();
        renderer.DrawText(text, device, ImagesDirectory());
        const MarkupRenderer::DrawStats stats = renderer.LastDrawStats();
        lastRenderStats.previewLines += stats.totalLines;
        lastRenderStats.previewDrawnLines += stats.drawnLines;
        lastRenderStats.previewSkippedLines += stats.skippedLines;
        lastRenderStats.previewCachedLines += stats.cachedLines;
        lastRenderStats.drawPreviewMs += NotepadPerfNowMs() - beginMs;
    }

    void OpenRenameNoteModal(std::string_view id) {
        if (const NoteEntry* note = FindNote(id)) {
            modalTarget = note->id;
            modalBuffer = note->title;
            pendingModal = PendingModal::RenameNote;
            modalOpenRequested = true;
        }
    }

    void OpenRenameFolderModal(std::string_view path) {
        const std::string normalized = NormalizeFolderPath(path);
        if (FindFolder(normalized)) {
            modalTarget = normalized;
            modalBuffer = BaseName(normalized);
            pendingModal = PendingModal::RenameFolder;
            modalOpenRequested = true;
        }
    }

    void OpenDeleteNoteModal(std::string_view id) {
        if (FindNote(id)) {
            modalTarget = std::string(id);
            pendingModal = PendingModal::DeleteNote;
            modalOpenRequested = true;
        }
    }

    void OpenDeleteFolderModal(std::string_view path) {
        const std::string normalized = NormalizeFolderPath(path);
        if (FindFolder(normalized)) {
            modalTarget = normalized;
            pendingModal = PendingModal::DeleteFolder;
            modalOpenRequested = true;
        }
    }

    void DrawModals() {
        if (pendingModal == PendingModal::None) {
            return;
        }
        UiSettings& ui = UiSettings::Instance();
        const char* title = ui.Text(UiText::NotepadModalTitle);
        if (pendingModal == PendingModal::DeleteNote) {
            title = ui.Text(UiText::NotepadDeleteNoteTitle);
        } else if (pendingModal == PendingModal::DeleteFolder) {
            title = ui.Text(UiText::NotepadDeleteFolderTitle);
        } else if (pendingModal == PendingModal::RenameFolder || pendingModal == PendingModal::RenameNote) {
            title = ui.Text(UiText::NotepadRenameTitle);
        } else if (pendingModal == PendingModal::CreateFolder) {
            title = ui.Text(UiText::NotepadCreateFolderTitle);
        } else if (pendingModal == PendingModal::CreateNote) {
            title = ui.Text(UiText::NotepadCreateNoteTitle);
        }

        const float modalWidth = std::max(
            1.0f,
            std::min(ScaleUi(420.0f), ImGui::GetIO().DisplaySize.x - ScaleUi(32.0f)));
        ImGui::SetNextWindowSize(ImVec2(modalWidth, 0.0f), ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(ImVec2(1.0f, 0.0f), ImVec2(modalWidth, FLT_MAX));
        const std::string modalTitle = std::string(title) + kModalPopupId;
        if (modalOpenRequested) {
            ImGui::OpenPopup(modalTitle.c_str());
            modalOpenRequested = false;
        }
        if (!ImGui::BeginPopupModal(modalTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        ImGui::TextUnformatted(title);
        ImGui::Separator();

        const bool deleteModal = pendingModal == PendingModal::DeleteNote || pendingModal == PendingModal::DeleteFolder;
        bool submitModal = false;
        if (deleteModal) {
            if (pendingModal == PendingModal::DeleteNote) {
                const NoteEntry* note = FindNote(modalTarget);
                const std::string question = ui.Format(
                    UiText::NotepadDeleteNoteQuestionFormat,
                    note ? note->title.c_str() : modalTarget.c_str());
                ImGui::TextWrapped("%s", question.c_str());
            } else {
                const std::string question = ui.Format(
                    UiText::NotepadDeleteFolderQuestionFormat,
                    BaseName(modalTarget).c_str(),
                    CountDescendantFolders(modalTarget),
                    CountNotesInFolderTree(modalTarget));
                ImGui::TextWrapped("%s", question.c_str());
            }
        } else {
            ImGui::SetNextItemWidth(-1.0f);
            const char* hint = pendingModal == PendingModal::CreateNote || pendingModal == PendingModal::RenameNote
                ? ui.Text(UiText::NotepadNoteTitle)
                : ui.Text(UiText::NotepadFolderName);
            submitModal = InputTextString(
                ("##notepad_modal_input_" + std::string(hint)).c_str(),
                modalBuffer,
                ImGuiInputTextFlags_EnterReturnsTrue,
                128);
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            pendingModal = PendingModal::None;
            modalOpenRequested = false;
            modalTarget.clear();
            modalBuffer.clear();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        const char* primary = deleteModal ? ui.Text(UiText::NotepadConfirmDelete) : ui.Text(UiText::Save);
        if (submitModal || ImGui::Button(primary)) {
            ApplyModalAction();
            ImGui::CloseCurrentPopup();
            pendingModal = PendingModal::None;
            modalOpenRequested = false;
        }
        ContinueToolbar(ButtonItemWidth(ui.Text(UiText::Cancel)));
        if (ImGui::Button(ui.Text(UiText::Cancel))) {
            pendingModal = PendingModal::None;
            modalOpenRequested = false;
            modalTarget.clear();
            modalBuffer.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void ApplyModalAction() {
        switch (pendingModal) {
        case PendingModal::CreateFolder:
            CreateFolder(modalTarget, modalBuffer);
            break;
        case PendingModal::CreateNote:
            CreateNote(modalTarget, modalBuffer);
            break;
        case PendingModal::RenameFolder:
            RenameFolder(modalTarget, modalBuffer);
            break;
        case PendingModal::RenameNote:
            RenameNote(modalTarget, modalBuffer);
            break;
        case PendingModal::DeleteFolder:
            DeleteFolder(modalTarget);
            break;
        case PendingModal::DeleteNote:
            DeleteNote(modalTarget);
            break;
        case PendingModal::None:
            break;
        }
        modalTarget.clear();
        modalBuffer.clear();
    }

    std::optional<fs::path> OpenFileDialog(UiText titleId, const std::wstring& filter) const {
        const std::wstring title = Utf8ToWide(UiSettings::Instance().Text(titleId));
        return native_file_dialog::OpenFile(title, filter);
    }

    void ImportTxtAsNote() {
        const auto source = OpenFileDialog(UiText::NotepadImportTxt, BuildDialogFilter({
            { UiText::NotepadTxtFilesFilter, L"*.txt" },
            { UiText::NotepadAllFilesFilter, L"*.*" },
        }));
        if (!source.has_value()) {
            return;
        }
        std::ifstream file(*source, std::ios::binary);
        if (!file) {
            statusMessage = UiSettings::Instance().Text(UiText::NotepadImportFailed);
            return;
        }
        const std::string content = NormalizeImportedText(
            std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()));
        const std::string title = PathToUtf8(source->stem());
        CreateNote(currentFolder, title.empty() ? UiSettings::Instance().Text(UiText::NotepadUntitled) : title);
        if (NoteEntry* note = FindNote(selectedNoteId)) {
            note->text = content;
            note->updatedAt = UnixTimeNow();
            editBuffer = content;
            editDirty = false;
            editing = false;
            QueueSave();
        }
    }

    std::optional<fs::path> MakeUniquePath(const fs::path& directory, const fs::path& desiredName) const {
        fs::path candidate = directory / desiredName.filename();
        const fs::path stem = candidate.stem();
        const fs::path ext = candidate.extension();
        int suffix = 1;
        for (;;) {
            std::error_code existsError;
            const bool candidateExists = fs::exists(candidate, existsError);
            if (existsError) {
                debuglog::WriteError("[notepad] unique image path stat failed path=%ls error=%d", candidate.c_str(), existsError.value());
                return std::nullopt;
            }
            if (!candidateExists) {
                return candidate;
            }
            candidate = directory / (stem.wstring() + L"_" + std::to_wstring(suffix++) + ext.wstring());
        }
    }

    void InsertImageFromDialog() {
        const auto source = OpenFileDialog(
            UiText::NotepadInsertImage,
            BuildDialogFilter({
                { UiText::NotepadImageFilesFilter, L"*.png;*.jpg;*.jpeg;*.bmp;*.gif" },
                { UiText::NotepadAllFilesFilter, L"*.*" },
            }));
        if (!source.has_value()) {
            return;
        }
        EnsureAssetDirectories();
        const std::string sanitized = SanitizeFileStem(PathToUtf8(source->stem()), "image");
        const fs::path desiredName = fs::path(Utf8ToWide(sanitized)).replace_extension(source->extension());
        const std::optional<fs::path> target = MakeUniquePath(ImagesDirectory(), desiredName);
        if (!target) {
            statusMessage = UiSettings::Instance().Text(UiText::NotepadImageInsertFailed);
            return;
        }
        std::error_code copyError;
        fs::copy_file(*source, *target, fs::copy_options::none, copyError);
        if (copyError) {
            statusMessage = UiSettings::Instance().Text(UiText::NotepadImageInsertFailed);
            debuglog::WriteError("[notepad] image copy failed source=%ls target=%ls error=%d", source->c_str(), target->c_str(), copyError.value());
            return;
        }
        const std::string relative = PathToUtf8(target->filename());
        InsertTextAtCursor("#img(" + relative + ")\n");
        statusMessage = UiSettings::Instance().Text(UiText::NotepadImageCopied);
    }

    void InsertTextAtCursor(std::string text) {
        const int safeCursor = std::clamp(editCursor, 0, static_cast<int>(editBuffer.size()));
        editBuffer.insert(static_cast<std::size_t>(safeCursor), text);
        editCursor = safeCursor + static_cast<int>(text.size());
        editDirty = true;
    }

    void ExportNote(const NoteEntry& note) {
        EnsureAssetDirectories();
        const std::string safeTitle = SanitizeFileStem(note.title, "note");
        const std::optional<fs::path> target = MakeUniquePath(
            ExportDirectory(),
            fs::path(Utf8ToWide(safeTitle)).replace_extension(L".txt"));
        if (!target) {
            statusMessage = UiSettings::Instance().Text(UiText::NotepadExportFailed);
            return;
        }
        std::ofstream file(*target, std::ios::binary | std::ios::trunc);
        if (!file) {
            statusMessage = UiSettings::Instance().Text(UiText::NotepadExportFailed);
            return;
        }
        file.write(note.text.data(), static_cast<std::streamsize>(note.text.size()));
        if (!file) {
            statusMessage = UiSettings::Instance().Text(UiText::NotepadExportFailed);
            return;
        }
        statusMessage = UiSettings::Instance().Format(UiText::NotepadExportSuccessFormat, PathToUtf8(target->filename()).c_str());
    }
};

NotepadModule::NotepadModule() : impl_(std::make_unique<Impl>()) {
}

NotepadModule::~NotepadModule() = default;

NotepadModule::NotepadModule(NotepadModule&&) noexcept = default;

NotepadModule& NotepadModule::operator=(NotepadModule&&) noexcept = default;

void NotepadModule::OnProcessAttach(HMODULE module) {
    impl_->OnProcessAttach(module);
}

void NotepadModule::Shutdown() {
    impl_->Shutdown();
}

void NotepadModule::ReloadConfig() {
    impl_->ReloadConfig();
}

void NotepadModule::FlushPendingEdits() {
    impl_->FlushPendingEdits();
}

void NotepadModule::ReleaseDeviceResources() {
    impl_->ReleaseDeviceResources();
}

void NotepadModule::SetTagsModule(TagsModule* tagsModule) {
    impl_->SetTagsModule(tagsModule);
}

void NotepadModule::DrawMainTab(IDirect3DDevice9* device) {
    impl_->DrawMainTab(device);
}

NotepadModule::RenderStats NotepadModule::LastRenderStats() const {
    return impl_->lastRenderStats;
}

bool NotepadModule::TryGetNote(std::string_view id, NoteContent& out) {
    return impl_->TryGetNote(id, out);
}

std::vector<NotepadModule::NoteSummary> NotepadModule::NoteSummaries() {
    return impl_->NoteSummaries();
}

std::filesystem::path NotepadModule::ImagesDirectoryPath() {
    return impl_->ImagesDirectoryPath();
}
