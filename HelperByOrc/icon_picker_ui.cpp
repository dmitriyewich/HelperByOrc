#include "icon_picker_ui.h"

#include "app_config.h"
#include "icon_registry.h"
#include "json_utils.h"
#include "ui_icons.h"
#include "ui_settings.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace icon_picker {
namespace {

constexpr char kIconsConfigSection[] = "icons";
constexpr std::size_t kMaxRecentIcons = 12;

struct ImGuiStringUserData {
    std::string* value = nullptr;
};

int InputTextResizeCallback(ImGuiInputTextCallbackData* data) {
    auto* userData = static_cast<ImGuiStringUserData*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        IM_ASSERT(userData && userData->value);
        userData->value->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = userData->value->data();
    }
    return 0;
}

bool InputTextWithHintString(const char* label, const char* hint, std::string& value, std::size_t minBuffer = 256) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }

    ImGuiStringUserData userData{ &value };
    return ImGui::InputTextWithHint(
        label,
        hint,
        value.data(),
        value.capacity() + 1,
        ImGuiInputTextFlags_CallbackResize,
        InputTextResizeCallback,
        &userData);
}

std::string ToLowerAscii(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    bool lastWasSpace = false;
    for (const unsigned char ch : value) {
        char next = static_cast<char>(ch);
        if (ch < 0x80) {
            if (std::isalnum(ch) != 0) {
                next = static_cast<char>(std::tolower(ch));
                lastWasSpace = false;
            } else if (ch == '_' || ch == '-' || std::isspace(ch) != 0) {
                next = ' ';
                if (lastWasSpace) {
                    continue;
                }
                lastWasSpace = true;
            } else {
                next = ' ';
                if (lastWasSpace) {
                    continue;
                }
                lastWasSpace = true;
            }
        } else {
            lastWasSpace = false;
        }
        out.push_back(next);
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

std::size_t RecentHash(const std::vector<std::string>& recent) {
    std::uint32_t hash = 2166136261u;
    for (const std::string& item : recent) {
        for (const unsigned char ch : item) {
            hash ^= ch;
            hash *= 16777619u;
        }
        hash ^= 0xff;
        hash *= 16777619u;
    }
    return hash;
}

std::vector<std::string> LoadRecentIcons() {
    std::vector<std::string> recent;
    const jsonutil::JsonObject section = AppConfig::Instance().ReadSectionObject(kIconsConfigSection);
    const jsonutil::JsonArray* array = jsonutil::JsonArrayOrNull(&section, "recent");
    if (!array) {
        return recent;
    }

    for (const jsonutil::JsonValue& value : *array) {
        const std::string* text = value.TryString();
        if (!text) {
            continue;
        }
        const std::string canonical = icon_registry::NormalizeIconId(*text);
        if (!canonical.empty() && icon_registry::Find(canonical)
            && std::find(recent.begin(), recent.end(), canonical) == recent.end()) {
            recent.push_back(canonical);
            if (recent.size() >= kMaxRecentIcons) {
                break;
            }
        }
    }
    return recent;
}

void SaveRecentIcons(std::vector<std::string> recent) {
    if (recent.size() > kMaxRecentIcons) {
        recent.resize(kMaxRecentIcons);
    }

    AppConfig::Instance().QueueMutation([recent = std::move(recent)](jsonutil::JsonObject& root) {
        jsonutil::JsonObject section;
        if (const auto it = root.find(kIconsConfigSection); it != root.end()) {
            if (const jsonutil::JsonObject* existing = it->second.TryObject()) {
                section = *existing;
            }
        }

        jsonutil::JsonArray array;
        for (const std::string& iconId : recent) {
            array.emplace_back(iconId);
        }
        section["schema_version"] = 1;
        section["recent"] = jsonutil::JsonValue(std::move(array));
        root[kIconsConfigSection] = jsonutil::JsonValue(std::move(section));
    });
}

void EnsureRecentLoaded(State& state) {
    if (state.recentLoaded) {
        return;
    }
    state.recent = LoadRecentIcons();
    state.recentLoaded = true;
}

const char* CategoryLabel(std::string_view category, UiSettings& ui) {
    if (category == "all") return ui.Text(UiText::IconPickerAllCategories);
    if (category == "recent") return ui.Text(UiText::IconPickerRecent);
    if (category == "brands") return ui.Text(UiText::IconPickerCategoryBrands);
    if (category == "transport") return ui.Text(UiText::IconPickerCategoryTransport);
    if (category == "game") return ui.Text(UiText::IconPickerCategoryGame);
    if (category == "documents") return ui.Text(UiText::IconPickerCategoryDocuments);
    if (category == "communication") return ui.Text(UiText::IconPickerCategoryCommunication);
    if (category == "status") return ui.Text(UiText::IconPickerCategoryStatus);
    if (category == "money") return ui.Text(UiText::IconPickerCategoryMoney);
    if (category == "settings") return ui.Text(UiText::IconPickerCategorySettings);
    if (category == "people") return ui.Text(UiText::IconPickerCategoryPeople);
    return ui.Text(UiText::IconPickerCategoryGeneral);
}

const std::vector<std::string>& Categories() {
    static const std::vector<std::string> categories = {
        "all",
        "recent",
        "general",
        "brands",
        "transport",
        "game",
        "documents",
        "communication",
        "status",
        "money",
        "settings",
        "people",
    };
    return categories;
}

bool MatchesQuery(const icon_registry::IconEntry& entry, const std::string& query) {
    return icon_registry::MatchesSearch(entry, query);
}

bool InRecent(const State& state, std::string_view canonical) {
    return std::find(state.recent.begin(), state.recent.end(), canonical) != state.recent.end();
}

void RebuildFilter(State& state) {
    state.filtered.clear();
    const std::string query = ToLowerAscii(state.search);
    const bool recentOnly = state.category == "recent";
    const bool allCategories = state.category == "all";

    const icon_registry::IconList icons = icon_registry::AllIcons();
    state.filtered.reserve(icons.size);
    for (std::size_t i = 0; i < icons.size; ++i) {
        const icon_registry::IconEntry& entry = icons.data[i];
        if (!allCategories && !recentOnly && state.category != icon_registry::CategoryId(entry.category)) {
            continue;
        }
        const std::string canonical = icon_registry::CanonicalId(entry);
        if (recentOnly && !InRecent(state, canonical)) {
            continue;
        }
        if (!MatchesQuery(entry, query)) {
            continue;
        }
        state.filtered.push_back(&entry);
    }

    if (recentOnly) {
        std::stable_sort(state.filtered.begin(), state.filtered.end(), [&](const void* lhs, const void* rhs) {
            const auto* left = static_cast<const icon_registry::IconEntry*>(lhs);
            const auto* right = static_cast<const icon_registry::IconEntry*>(rhs);
            const std::string leftId = icon_registry::CanonicalId(*left);
            const std::string rightId = icon_registry::CanonicalId(*right);
            const auto leftIt = std::find(state.recent.begin(), state.recent.end(), leftId);
            const auto rightIt = std::find(state.recent.begin(), state.recent.end(), rightId);
            return leftIt < rightIt;
        });
    }

    state.lastSearch = state.search;
    state.lastCategory = state.category;
    state.lastRecentHash = RecentHash(state.recent);
}

void EnsureFilter(State& state) {
    const std::size_t recentHash = RecentHash(state.recent);
    if (state.search != state.lastSearch || state.category != state.lastCategory || recentHash != state.lastRecentHash) {
        RebuildFilter(state);
    }
}

const char* StyleLabel(icon_registry::IconStyle style, UiSettings& ui) {
    return style == icon_registry::IconStyle::Brands
        ? ui.Text(UiText::IconPickerStyleBrand)
        : ui.Text(UiText::IconPickerStyleSolid);
}

} // namespace

void OpenPopup(const char* popupId) {
    ImGui::OpenPopup(popupId ? popupId : "icon_picker");
}

void TouchRecent(State& state, std::string_view iconId) {
    const std::string canonical = icon_registry::NormalizeIconId(iconId);
    if (canonical.empty() || !icon_registry::Find(canonical)) {
        return;
    }

    state.recent.erase(std::remove(state.recent.begin(), state.recent.end(), canonical), state.recent.end());
    state.recent.insert(state.recent.begin(), canonical);
    if (state.recent.size() > kMaxRecentIcons) {
        state.recent.resize(kMaxRecentIcons);
    }
    SaveRecentIcons(state.recent);
}

std::string MarkupToken(std::string_view iconId) {
    const std::string canonical = icon_registry::NormalizeIconId(iconId);
    return "#icon(" + canonical + ")";
}

bool DrawPopup(State& state, const Options& options, std::string& outIconId) {
    EnsureRecentLoaded(state);
    UiSettings& ui = UiSettings::Instance();
    bool selected = false;

    const char* popupId = options.popupId ? options.popupId : "icon_picker";
    if (!ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return false;
    }

    const ImVec2 size = ui.Scale(options.size);
    ImGui::SetNextItemWidth(size.x * 0.64f);
    const std::string hint = std::string(ui_icons::Search) + " " + ui.Text(UiText::IconPickerSearchHint);
    InputTextWithHintString("##icon_picker_search", hint.c_str(), state.search);
    ImGui::SameLine();

    ImGui::SetNextItemWidth(size.x * 0.30f);
    if (ImGui::BeginCombo("##icon_picker_category", CategoryLabel(state.category, ui))) {
        for (const std::string& category : Categories()) {
            const bool isSelected = state.category == category;
            if (ImGui::Selectable(CategoryLabel(category, ui), isSelected)) {
                state.category = category;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    EnsureFilter(state);

    if (ImGui::BeginChild("##icon_picker_list", ImVec2(size.x, size.y), ImGuiChildFlags_Borders)) {
        if (state.filtered.empty()) {
            ImGui::TextUnformatted(ui.Text(UiText::IconPickerNoMatches));
        } else {
            const float rowHeight = std::ceil(ui.Scale(34.0f));
            const float glyphX = ui.Scale(10.0f);
            const float idX = ui.Scale(46.0f);
            const float metaX = std::max(ui.Scale(260.0f), size.x * 0.58f);

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(state.filtered.size()), rowHeight);
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const auto* entry = static_cast<const icon_registry::IconEntry*>(state.filtered[static_cast<std::size_t>(row)]);
                    if (!entry) {
                        continue;
                    }

                    const std::string canonical = icon_registry::CanonicalId(*entry);
                    ImGui::PushID(canonical.c_str());
                    const bool rowSelected = state.selectedIconId == canonical;
                    if (ImGui::Selectable("##icon_row", rowSelected, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0.0f, rowHeight))) {
                        state.selectedIconId = canonical;
                        outIconId = canonical;
                        TouchRecent(state, canonical);
                        selected = true;
                        ImGui::CloseCurrentPopup();
                    }

                    const ImVec2 min = ImGui::GetItemRectMin();
                    const ImVec2 max = ImGui::GetItemRectMax();
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
                    const ImU32 mutedColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                    const float textY = min.y + std::floor((rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
                    drawList->AddText(ImVec2(min.x + glyphX, textY), textColor, entry->glyph);
                    drawList->AddText(ImVec2(min.x + idX, textY), textColor, canonical.c_str());

                    char meta[80]{};
                    std::snprintf(meta, sizeof(meta), "%s  U+%04X", StyleLabel(entry->style, ui), static_cast<unsigned>(entry->codepoint));
                    drawList->AddText(ImVec2(min.x + metaX, textY), mutedColor, meta);

                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                        ImGui::SetTooltip(
                            "%s\n%s\n%s\nU+%04X",
                            canonical.c_str(),
                            StyleLabel(entry->style, ui),
                            icon_registry::CategoryId(entry->category),
                            static_cast<unsigned>(entry->codepoint));
                    }
                    ImGui::PopID();
                }
            }
        }
    }
    ImGui::EndChild();

    if (ImGui::Button(ui.Text(UiText::Cancel))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
    return selected;
}

} // namespace icon_picker
