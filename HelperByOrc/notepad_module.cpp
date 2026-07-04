#include "notepad_module.h"

#include "app_config.h"
#include "debug_log.h"
#include "icon_picker_ui.h"
#include "json_utils.h"
#include "markup_renderer.h"
#include "tags_module.h"
#include "ui_icons.h"
#include "ui_settings.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <commdlg.h>
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
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kNotepadSectionName = "notepad";
constexpr int kNotepadSchemaVersion = 1;
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

struct NoteEntry {
    std::string id{};
    std::string title{};
    std::string folderPath{};
    std::string text{};
    bool favorite = false;
    std::uint64_t createdAt = 0;
    std::uint64_t updatedAt = 0;
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
    if (utf8.empty() || utf8 == "." || utf8 == "..") {
        utf8 = std::string(fallback);
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
    MarkupRenderer renderer;
    icon_picker::State iconPickerState{};
    RenderedNoteCache renderedNoteCache{};
    RenderedEditCache renderedEditCache{};
    RenderStats lastRenderStats{};
    std::uint64_t idCounter = 0;

    void OnProcessAttach(HMODULE moduleHandle) {
        module = moduleHandle;
    }

    void SetTagsModule(TagsModule* modulePtr) {
        tagsModule = modulePtr;
    }

    void Shutdown() {
        SaveEditBufferIfNeeded();
        ReleaseDeviceResources();
        configLoaded = false;
        folders.clear();
        notes.clear();
        order.clear();
        iconPickerState = {};
        renderedNoteCache = {};
        renderedEditCache = {};
        lastRenderStats = {};
    }

    void ReloadConfig() {
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
        lastRenderStats = {};
    }

    void FlushPendingEdits() {
        SaveEditBufferIfNeeded();
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

    void EnsureLoaded() {
        if (configLoaded) {
            return;
        }
        LoadConfig();
        configLoaded = true;
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
        debuglog::WriteInfo("[notepad] config loaded folders=%zu notes=%zu", folders.size(), notes.size());
    }

    void NormalizeModel() {
        for (NoteEntry& note : notes) {
            note.folderPath = NormalizeFolderPath(note.folderPath);
            if (!note.folderPath.empty() && !FindFolder(note.folderPath)) {
                note.folderPath.clear();
            }
            if (note.title.empty()) {
                note.title = UiSettings::Instance().Text(UiText::NotepadUntitled);
            }
        }

        std::sort(folders.begin(), folders.end(), [](const FolderEntry& lhs, const FolderEntry& rhs) {
            return LowerUtf8(lhs.path) < LowerUtf8(rhs.path);
        });

        std::set<std::string> validFolderPaths;
        validFolderPaths.insert("");
        for (const FolderEntry& folder : folders) {
            validFolderPaths.insert(folder.path);
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
            std::set<std::string> seenFolders;
            std::set<std::string> seenNotes;
            std::vector<OrderItem> normalized;
            for (const OrderItem& item : items) {
                if (item.type == ItemType::Folder) {
                    const FolderEntry* folder = FindFolder(item.value);
                    if (folder && ParentPath(folder->path) == directory && seenFolders.insert(folder->path).second) {
                        normalized.push_back({ ItemType::Folder, folder->path });
                    }
                } else if (item.type == ItemType::Note) {
                    const NoteEntry* note = FindNote(item.value);
                    if (note && note->folderPath == directory && seenNotes.insert(note->id).second) {
                        normalized.push_back({ ItemType::Note, note->id });
                    }
                }
            }

            for (const FolderEntry& folder : folders) {
                if (ParentPath(folder.path) == directory && seenFolders.insert(folder.path).second) {
                    normalized.push_back({ ItemType::Folder, folder.path });
                }
            }
            for (const NoteEntry& note : notes) {
                if (note.folderPath == directory && seenNotes.insert(note.id).second) {
                    normalized.push_back({ ItemType::Note, note.id });
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

    void SelectNote(std::string_view id) {
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
        QueueSave();
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
        SelectNote(note.id);
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
        copy.createdAt = UnixTimeNow();
        copy.updatedAt = copy.createdAt;
        notes.push_back(copy);
        order[copy.folderPath].push_back({ ItemType::Note, copy.id });
        SelectNote(copy.id);
        QueueSave();
    }

    void RenameNote(std::string_view id, std::string_view title) {
        NoteEntry* note = FindNote(id);
        if (!note) {
            return;
        }
        const std::string cleanTitle = TrimAscii(title);
        note->title = cleanTitle.empty() ? UiSettings::Instance().Text(UiText::NotepadUntitled) : cleanTitle;
        note->updatedAt = UnixTimeNow();
        QueueSave();
    }

    void RenameFolder(std::string_view path, std::string_view newName) {
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
        NormalizeModel();
        QueueSave();
    }

    void DeleteNote(std::string_view id) {
        SaveEditBufferIfNeeded();
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
        QueueSave();
        debuglog::WriteInfo("[notepad] note deleted id=%s", removedId.c_str());
    }

    void DeleteFolder(std::string_view path) {
        SaveEditBufferIfNeeded();
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
        NormalizeModel();
        QueueSave();
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

    bool ApplyDrop(const OrderItem& dragged, std::string targetDirectory, std::optional<OrderItem> anchor, DropPlacement placement) {
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
            SelectNote(note->id);
            QueueSave();
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
        NormalizeModel();
        QueueSave();
        return true;
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
        renderedNoteCache.rendered = tagsModule->ExpandText(note.text);
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
        renderedEditCache.rendered = tagsModule->ExpandText(editBuffer);
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
            ImGui::TextDisabled("%s", statusMessage.c_str());
        }
        ImGui::Spacing();

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const bool narrow = avail.x < ScaleUi(900.0f);
        if (narrow) {
            const float navHeight = std::clamp(avail.y * 0.34f, ScaleUi(210.0f), ScaleUi(300.0f));
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

    void DrawNoteSelectableRow(const NoteEntry& note, const std::string& label, bool selected) {
        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_None, ImVec2(0.0f, ScaleUi(24.0f)))) {
            SelectNote(note.id);
            if (!search.empty()) {
                search.clear();
            }
        }
        DrawFavoriteMarker(note);
        DrawNoteContextMenu(note);
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

        ImGui::SetNextItemWidth(-1.0f);
        const std::string searchHint = std::string(ui_icons::Search) + " " + ui.Text(UiText::NotepadSearchHint);
        InputTextWithHintString("##notepad_search", searchHint.c_str(), search, 0, 128);
        if (search.empty()) {
            DrawBreadcrumbs();
        }

        if (ImGui::BeginChild("notepad_nav_list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
            if (search.empty()) {
                const bool hasFavorites = !FavoriteNotes().empty();
                DrawFavorites();
                if (hasFavorites) {
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
        ImGui::TextUnformatted(label.c_str());
        if (ImGui::IsItemHovered() && !currentFolder.empty()) {
            const std::string full = std::string(ui.Text(UiText::NotepadRootName)) + " / " + currentFolder;
            ImGui::SetTooltip("%s", full.c_str());
        }
        ImGui::Spacing();
    }

    void DrawFavorites() {
        UiSettings& ui = UiSettings::Instance();
        const auto favorites = FavoriteNotes();
        if (favorites.empty()) {
            return;
        }
        const bool selectedFavoriteVisible = std::any_of(favorites.begin(), favorites.end(), [&](const NoteEntry* note) {
            return note && note->id == selectedNoteId;
        });
        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth
            | (selectedFavoriteVisible ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        const std::string header = std::string(ui_icons::Star) + " " + ui.Text(UiText::NotepadFavorites)
            + " (" + std::to_string(favorites.size()) + ")";
        if (!ImGui::TreeNodeEx("##notepad_favorites", flags, "%s", header.c_str())) {
            return;
        }
        for (const NoteEntry* note : favorites) {
            if (!note) {
                continue;
            }
            const std::string label = std::string(ui_icons::Book) + " " + note->title + "##fav_" + note->id;
            DrawNoteSelectableRow(*note, label, selectedNoteId == note->id);
        }
        ImGui::TreePop();
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
        std::string label = icon + " " + row.label;
        label += "##notepad_row_" + row.value;

        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, ScaleUi(24.0f)))) {
            if (isFolder) {
                SelectFolder(row.value);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    OpenFolder(row.value);
                }
            } else {
                SelectNote(row.value);
            }
        }
        const ImVec2 rowMin = ImGui::GetItemRectMin();
        const ImVec2 rowMax = ImGui::GetItemRectMax();
        if (!isFolder) {
            if (const NoteEntry* note = FindNote(row.value)) {
                DrawFavoriteMarker(*note);
            }
        }
        DrawDragSource({ row.type, row.value }, row.label);
        DrawRowDropTarget({ row.type, row.value }, rowMin, rowMax);
        if (isFolder) {
            if (ImGui::BeginPopupContextItem(("notepad_folder_popup_" + row.value).c_str())) {
                DrawFolderContextActions(row.value);
                ImGui::EndPopup();
            }
        } else if (const NoteEntry* note = FindNote(row.value)) {
            DrawNoteContextMenu(*note);
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
            label += "##notepad_search_" + note->id;
            DrawNoteSelectableRow(*note, label, selectedNoteId == note->id);
        }
    }

    void DrawNoteContextMenu(const NoteEntry& note) {
        if (!ImGui::BeginPopupContextItem(("notepad_note_popup_" + note.id).c_str())) {
            return;
        }
        UiSettings& ui = UiSettings::Instance();
        if (ImGui::MenuItem(ui.Text(UiText::Edit))) {
            SelectNote(note.id);
            editing = true;
            editBuffer = note.text;
            editDirty = false;
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

        ImGui::TextUnformatted(note->title.c_str());
        ImGui::SameLine();
        if (note->favorite) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.15f, 1.0f), "%s", ui_icons::Star);
            ImGui::SameLine();
        }
        if (ImGui::Button((std::string(ui_icons::Star) + " " + (note->favorite ? ui.Text(UiText::NotepadUnfavorite) : ui.Text(UiText::NotepadFavorite))).c_str())) {
            note->favorite = !note->favorite;
            note->updatedAt = UnixTimeNow();
            QueueSave();
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(ui_icons::Edit) + " " + ui.Text(UiText::Edit)).c_str())) {
            editing = true;
            editBuffer = note->text;
            editDirty = false;
            editCursor = static_cast<int>(editBuffer.size());
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(ui_icons::FileExport) + " " + ui.Text(UiText::NotepadExportTxt)).c_str())) {
            ExportNote(*note);
        }
        ImGui::SameLine();
        ImGui::Checkbox(ui.Text(UiText::NotepadApplyTags), &applyTags);

        ImGui::Separator();
        if (!editing) {
            DrawReadOnlyNote(*note, device);
        } else {
            DrawEditor(*note, device);
        }
    }

    void DrawReadOnlyNote(const NoteEntry& note, IDirect3DDevice9* device) {
        UiSettings& ui = UiSettings::Instance();
        if (ImGui::Button((std::string(ui_icons::Copy) + " " + ui.Text(UiText::NotepadCopyRaw)).c_str())) {
            ImGui::SetClipboardText(note.text.c_str());
            statusMessage = ui.Text(UiText::ToastClipboardCopied);
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(ui_icons::Copy) + " " + ui.Text(UiText::NotepadCopyRendered)).c_str())) {
            const std::string rendered = RenderedText(note);
            ImGui::SetClipboardText(MarkupRenderer::StripMarkup(rendered).c_str());
            statusMessage = ui.Text(UiText::ToastClipboardCopied);
        }
        ImGui::SameLine();
        if (ImGui::Button(copyLineMode ? ui.Text(UiText::NotepadPreviewMode) : ui.Text(UiText::NotepadCopyLine))) {
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
        std::stringstream stream{ text };
        std::string line;
        int index = 0;
        while (std::getline(stream, line)) {
            std::string plain = MarkupRenderer::StripMarkupLine(line);
            if (plain.empty()) {
                plain = " ";
            }
            ImGui::PushID(index++);
            if (ImGui::Selectable(plain.c_str(), false)) {
                ImGui::SetClipboardText(plain == " " ? "" : plain.c_str());
                statusMessage = UiSettings::Instance().Text(UiText::ToastClipboardCopied);
            }
            ImGui::PopID();
        }
        lastRenderStats.copyLinesMs += NotepadPerfNowMs() - beginMs;
    }

    void DrawEditor(NoteEntry& note, IDirect3DDevice9* device) {
        UiSettings& ui = UiSettings::Instance();
        const bool savePressed = ImGui::Button((std::string(ui_icons::SaveDisk) + " " + ui.Text(UiText::Save)).c_str());
        if (savePressed) {
            editDirty = true;
            SaveEditBufferIfNeeded();
            editing = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::Cancel))) {
            editing = false;
            editDirty = false;
            editBuffer = note.text;
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(ui_icons::Image) + " " + ui.Text(UiText::NotepadInsertImage)).c_str())) {
            InsertImageFromDialog();
        }
        ImGui::SameLine();
        const std::string iconPickerPopup = std::string(ui.Text(UiText::IconPickerTitle)) + "##notepad_icon_picker";
        if (ImGui::Button((std::string(ui_icons::Star) + " " + ui.Text(UiText::HudMarkupIcon)).c_str())) {
            icon_picker::OpenPopup(iconPickerPopup.c_str());
        }
        std::string selectedIconId;
        if (icon_picker::DrawPopup(iconPickerState, icon_picker::Options{ iconPickerPopup.c_str(), ImVec2(560.0f, 460.0f) }, selectedIconId)) {
            InsertTextAtCursor(icon_picker::MarkupToken(selectedIconId) + " ");
        }
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::NotepadMarkupHelp))) {
            ImGui::OpenPopup("notepad_markup_help");
        }
        DrawMarkupHelpPopup();

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const bool vertical = avail.x < ScaleUi(760.0f);
        if (vertical) {
            ImGui::TextDisabled("%s", ui.Text(UiText::NotepadEditor));
            if (InputTextMultilineString("##notepad_edit", editBuffer, ImVec2(-1.0f, std::max(ScaleUi(220.0f), avail.y * 0.48f)), 0, &editCursor)) {
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
        ImGui::TextDisabled("%s", ui.Text(UiText::NotepadMarkupHelpHint));
        ImGui::EndPopup();
    }

    void DrawPreviewText(const std::string& text, IDirect3DDevice9* device) {
        const double beginMs = NotepadPerfNowMs();
        renderer.DrawText(text, device, ImagesDirectory());
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

        ImGui::SetNextWindowSize(ScaleUi(420.0f, 0.0f), ImGuiCond_Appearing);
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
            ImGui::SetNextItemWidth(ScaleUi(360.0f));
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
        ImGui::SameLine();
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
        wchar_t fileName[MAX_PATH]{};
        const std::wstring title = Utf8ToWide(UiSettings::Instance().Text(titleId));
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = nullptr;
        ofn.lpstrFile = fileName;
        ofn.nMaxFile = static_cast<DWORD>(std::size(fileName));
        ofn.lpstrFilter = filter.c_str();
        ofn.lpstrTitle = title.c_str();
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
        if (!GetOpenFileNameW(&ofn)) {
            return std::nullopt;
        }
        return fs::path(fileName);
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

    fs::path MakeUniquePath(const fs::path& directory, const fs::path& desiredName) const {
        fs::path candidate = directory / desiredName.filename();
        const fs::path stem = candidate.stem();
        const fs::path ext = candidate.extension();
        int suffix = 1;
        while (fs::exists(candidate)) {
            candidate = directory / (stem.wstring() + L"_" + std::to_wstring(suffix++) + ext.wstring());
        }
        return candidate;
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
        const fs::path target = MakeUniquePath(ImagesDirectory(), desiredName);
        std::error_code copyError;
        fs::copy_file(*source, target, fs::copy_options::none, copyError);
        if (copyError) {
            statusMessage = UiSettings::Instance().Text(UiText::NotepadImageInsertFailed);
            debuglog::WriteError("[notepad] image copy failed source=%ls target=%ls error=%d", source->c_str(), target.c_str(), copyError.value());
            return;
        }
        const std::string relative = PathToUtf8(target.filename());
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
        fs::path target = MakeUniquePath(ExportDirectory(), fs::path(Utf8ToWide(safeTitle)).replace_extension(L".txt"));
        std::ofstream file(target, std::ios::binary | std::ios::trunc);
        if (!file) {
            statusMessage = UiSettings::Instance().Text(UiText::NotepadExportFailed);
            return;
        }
        file.write(note.text.data(), static_cast<std::streamsize>(note.text.size()));
        if (!file) {
            statusMessage = UiSettings::Instance().Text(UiText::NotepadExportFailed);
            return;
        }
        statusMessage = UiSettings::Instance().Format(UiText::NotepadExportSuccessFormat, PathToUtf8(target.filename()).c_str());
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
