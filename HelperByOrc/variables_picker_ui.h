#pragma once

#include "ui_settings.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace variables_picker {

enum class EntryKind {
    Simple,
    Function,
    Custom,
    Parameter,
};

enum class Category {
    All,
    Player,
    Target,
    Vehicle,
    World,
    Time,
    SampDialog,
    Arizona,
    Binder,
    Text,
    Actions,
    Custom,
    Parameters,
};

enum class Mode {
    Manage,
    Insert,
};

enum class RequestType {
    None,
    Copy,
    Insert,
    OpenKeyEmulatePicker,
    OpenDialogItemPicker,
    OpenArizonaDialogItemPicker,
    OpenDialogTextPicker,
    OpenArizonaDialogTextPicker,
    OpenBindSelectorBuilder,
    OpenWaitIfBuilder,
    SaveCustom,
    DeleteCustom,
};

struct Entry {
    EntryKind kind = EntryKind::Simple;
    Category category = Category::Text;
    std::string id{};
    std::string name{};
    std::string token{};
    std::string example{};
    UiText descriptionText = UiText::Count;
    std::string description{};
    std::string value{};
    bool action = false;
};

struct State {
    std::string search{};
    Category activeCategory = Category::All;
    std::string selectedId{};
    bool customDraftOpen = false;
    bool customCreateMode = true;
    std::string customOriginalName{};
    std::string customName{};
    std::string customValue{};
    std::string customError{};
    bool customDeleteConfirmOpen = false;

    std::vector<int> visibleCache{};
    std::string visibleSearchCache{};
    Category visibleCategoryCache = Category::All;
    UiLanguage visibleLanguageCache = UiLanguage::Russian;
    std::size_t visibleEntriesHash = 0;
    std::array<int, 13> categoryCountsCache{};
    std::size_t categoryCountsEntriesHash = 0;
    std::vector<std::string> searchBlobCache{};
    std::size_t searchBlobEntriesHash = 0;
    UiLanguage searchBlobLanguageCache = UiLanguage::Russian;
};

using InspectorExtraDrawCallback = void (*)(void* context, const Entry& entry);

struct Options {
    Mode mode = Mode::Manage;
    const char* id = "variables_picker";
    bool allowInsert = false;
    bool allowCustomEdit = false;
    bool closeOnInsert = false;
    ImVec2 size = ImVec2(0.0f, 0.0f);
    bool allowCopyInInsert = false;
    InspectorExtraDrawCallback drawInspectorExtra = nullptr;
    void* inspectorExtraContext = nullptr;
};

struct Request {
    RequestType type = RequestType::None;
    bool closePopupAfterAction = false;
    std::string text{};
    std::string name{};
    std::string value{};
};

std::string MakeEntryId(EntryKind kind, std::string_view token);
const char* CategoryLabel(Category category, UiSettings& ui);
Request Draw(State& state, const std::vector<Entry>& entries, const Options& options);

} // namespace variables_picker
