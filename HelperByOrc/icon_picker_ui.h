#pragma once

#include <imgui.h>

#include <string>
#include <string_view>
#include <vector>

namespace icon_picker {

struct State {
    std::string search{};
    std::string category = "all";
    std::string selectedIconId{};
    std::vector<std::string> recent{};
    bool recentLoaded = false;
    std::string lastSearch{};
    std::string lastCategory{};
    std::size_t lastRecentHash = 0;
    std::vector<const void*> filtered{};
};

struct Options {
    const char* popupId = "icon_picker";
    ImVec2 size = ImVec2(560.0f, 460.0f);
};

void OpenPopup(const char* popupId);
bool DrawPopup(State& state, const Options& options, std::string& outIconId);
void TouchRecent(State& state, std::string_view iconId);
std::string MarkupToken(std::string_view iconId);

} // namespace icon_picker
