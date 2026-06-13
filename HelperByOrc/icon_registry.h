#pragma once

#include <imgui.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace icon_registry {

enum class IconStyle {
    Solid,
    Brands,
};

enum class IconCategory {
    General,
    Brands,
    Transport,
    Game,
    Documents,
    Communication,
    Status,
    Money,
    Settings,
    People,
};

struct IconEntry {
    const char* id;
    IconStyle style;
    const char* glyph;
    ImWchar codepoint;
    IconCategory category;
};

struct IconList {
    const IconEntry* data = nullptr;
    std::size_t size = 0;
};

IconList AllIcons();
const IconEntry* Find(std::string_view id);
const IconEntry* Find(IconStyle style, std::string_view id);
std::string ResolveGlyph(std::string_view id, std::string_view fallback = {});
std::string NormalizeIconId(std::string_view id);
std::string CanonicalId(const IconEntry& entry);
const ImWchar* SolidRanges();
const ImWchar* BrandsRanges();
const char* StyleId(IconStyle style);
const char* StylePrefix(IconStyle style);
const char* CategoryId(IconCategory category);
bool MatchesSearch(const IconEntry& entry, std::string_view normalizedQuery);

} // namespace icon_registry
