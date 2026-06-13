#include "icon_registry.h"

#include "icon_registry_data.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <iterator>
#include <string>

namespace icon_registry {
namespace {

struct ParsedId {
    bool hasStyle = false;
    IconStyle style = IconStyle::Solid;
    std::string id{};
};

std::string Trim(std::string_view value) {
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

std::string NormalizeBareId(std::string_view value) {
    const std::string trimmed = Trim(value);
    std::string out;
    out.reserve(trimmed.size());

    bool lastWasDash = false;
    for (const unsigned char ch : trimmed) {
        char next = '\0';
        if (std::isalnum(ch) != 0) {
            next = static_cast<char>(std::tolower(ch));
        } else if (ch == '_' || ch == '-' || std::isspace(ch) != 0) {
            next = '-';
        } else {
            next = '-';
        }

        if (next == '-') {
            if (!out.empty() && !lastWasDash) {
                out.push_back('-');
                lastWasDash = true;
            }
            continue;
        }

        out.push_back(next);
        lastWasDash = false;
    }

    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    return out;
}

ParsedId ParseId(std::string_view value) {
    ParsedId parsed;
    const std::string trimmed = Trim(value);
    const std::size_t colon = trimmed.find(':');
    if (colon == std::string::npos) {
        parsed.id = NormalizeBareId(trimmed);
        return parsed;
    }

    const std::string style = NormalizeBareId(std::string_view(trimmed).substr(0, colon));
    parsed.id = NormalizeBareId(std::string_view(trimmed).substr(colon + 1));
    if (style == "brand" || style == "brands") {
        parsed.hasStyle = true;
        parsed.style = IconStyle::Brands;
    } else if (style == "solid") {
        parsed.hasStyle = true;
        parsed.style = IconStyle::Solid;
    }
    return parsed;
}

struct IconRange {
    const IconEntry* begin = nullptr;
    const IconEntry* end = nullptr;
};

IconRange RangeForStyle(IconStyle style) {
    const IconEntry* begin = generated::kIcons;
    const IconEntry* end = generated::kIcons + std::size(generated::kIcons);
    const IconEntry* brands = begin + generated::kSolidIconCount;
    if (style == IconStyle::Solid) {
        return IconRange{ begin, brands };
    }
    return IconRange{ brands, end };
}

std::string_view LegacyAlias(std::string_view id) {
    if (id == "file") return "book";
    if (id == "gun") return "bolt";
    if (id == "note") return "book";
    if (id == "save") return "floppy-disk";
    if (id == "user") return "tags";
    if (id == "weapon") return "bolt";
    if (id == "wrench") return "sliders";
    return {};
}

const IconEntry* FindBare(IconStyle style, std::string_view id) {
    const std::string normalized = NormalizeBareId(id);
    if (normalized.empty()) {
        return nullptr;
    }

    const IconRange range = RangeForStyle(style);
    const IconEntry* it = std::lower_bound(range.begin, range.end, normalized, [](const IconEntry& entry, const std::string& value) {
        return std::string_view(entry.id) < std::string_view(value);
    });
    if (it != range.end && normalized == it->id) {
        return it;
    }

    const std::string_view alias = style == IconStyle::Solid ? LegacyAlias(normalized) : std::string_view{};
    if (alias.empty()) {
        return nullptr;
    }
    it = std::lower_bound(range.begin, range.end, alias, [](const IconEntry& entry, std::string_view value) {
        return std::string_view(entry.id) < value;
    });
    return it != range.end && alias == it->id ? it : nullptr;
}

bool Contains(std::string_view haystack, std::string_view needle) {
    return !needle.empty() && haystack.find(needle) != std::string_view::npos;
}

bool ContainsNormalizedId(std::string_view id, std::string_view query) {
    if (Contains(id, query)) {
        return true;
    }

    std::string spaced;
    spaced.reserve(id.size());
    for (const char ch : id) {
        spaced.push_back(ch == '-' || ch == '_' ? ' ' : ch);
    }
    return Contains(spaced, query);
}

bool AnyAliasMatches(std::string_view query, std::initializer_list<std::string_view> aliases) {
    for (std::string_view alias : aliases) {
        if (Contains(alias, query)) {
            return true;
        }
    }
    return false;
}

bool IdAliasMatches(std::string_view id, std::string_view query) {
    if ((id == "car" || id == "car-side" || id == "car-rear" || id == "cars" || id == "truck" || id == "taxi"
            || id == "steering-wheel")
        && AnyAliasMatches(query, { "машина", "авто", "транспорт", "vehicle" })) {
        return true;
    }
    if ((id == "bolt" || id == "gun" || id == "swords" || id == "knife")
        && AnyAliasMatches(query, { "weapon", "оружие", "пистолет" })) {
        return true;
    }
    if ((id == "folder" || id == "folder-open" || id == "folder-plus" || id == "folder-tree")
        && AnyAliasMatches(query, { "папка" })) {
        return true;
    }
    if (id == "keyboard" && AnyAliasMatches(query, { "бинд", "клавиша", "кнопка" })) {
        return true;
    }
    if ((id == "message" || id == "messages" || id == "comment" || id == "comments")
        && AnyAliasMatches(query, { "чат", "сообщение" })) {
        return true;
    }
    if ((id == "heart" || id == "heart-pulse") && AnyAliasMatches(query, { "здоровье", "хп", "hp" })) {
        return true;
    }
    if ((id == "shield" || id == "shield-halved") && AnyAliasMatches(query, { "броня", "armor" })) {
        return true;
    }
    if ((id == "money-bill" || id == "money-bill-wave" || id == "sack-dollar")
        && AnyAliasMatches(query, { "деньги", "банк" })) {
        return true;
    }
    if ((id == "house" || id == "home") && AnyAliasMatches(query, { "дом" })) {
        return true;
    }
    if ((id == "gear" || id == "gears" || id == "sliders") && AnyAliasMatches(query, { "настройки" })) {
        return true;
    }
    if ((id == "id-card" || id == "address-card") && AnyAliasMatches(query, { "паспорт" })) {
        return true;
    }
    if ((id == "building-columns" || id == "landmark") && AnyAliasMatches(query, { "банк" })) {
        return true;
    }
    if ((id == "people-roof" || id == "users" || id == "user-group")
        && AnyAliasMatches(query, { "семья", "фракция" })) {
        return true;
    }
    if ((id == "warehouse" || id == "boxes-stacked" || id == "box") && AnyAliasMatches(query, { "склад" })) {
        return true;
    }
    if ((id == "map" || id == "map-location" || id == "map-location-dot" || id == "location-dot")
        && AnyAliasMatches(query, { "arizona", "аризона" })) {
        return true;
    }
    return false;
}

bool TokenAliasMatches(std::string_view id, std::string_view query) {
    const std::string normalized = NormalizeBareId(query);
    if (normalized.empty()) {
        return false;
    }

    std::size_t start = 0;
    while (start <= id.size()) {
        const std::size_t end = id.find('-', start);
        const std::string_view token = id.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if ((token == "car" || token == "truck" || token == "taxi") && normalized == "vehicle") return true;
        if ((token == "gun" || token == "swords" || token == "knife") && normalized == "weapon") return true;
        if ((token == "message" || token == "comment") && normalized == "chat") return true;
        if (token == "heart" && (normalized == "health" || normalized == "hp")) return true;
        if (token == "shield" && normalized == "armor") return true;
        if ((token == "money" || token == "dollar") && normalized == "bank") return true;
        if ((token == "gear" || token == "sliders") && normalized == "setting") return true;
        if ((token == "user" || token == "users") && (normalized == "family" || normalized == "faction")) return true;
        if ((token == "warehouse" || token == "box") && normalized == "storage") return true;
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}

} // namespace

IconList AllIcons() {
    return IconList{ generated::kIcons, std::size(generated::kIcons) };
}

const IconEntry* Find(std::string_view id) {
    const ParsedId parsed = ParseId(id);
    if (parsed.id.empty()) {
        return nullptr;
    }
    if (parsed.hasStyle) {
        return FindBare(parsed.style, parsed.id);
    }

    if (const IconEntry* solid = FindBare(IconStyle::Solid, parsed.id)) {
        return solid;
    }
    return FindBare(IconStyle::Brands, parsed.id);
}

const IconEntry* Find(IconStyle style, std::string_view id) {
    return FindBare(style, id);
}

std::string ResolveGlyph(std::string_view id, std::string_view fallback) {
    if (const IconEntry* entry = Find(id)) {
        return entry->glyph;
    }
    return std::string(fallback);
}

std::string NormalizeIconId(std::string_view id) {
    if (const IconEntry* entry = Find(id)) {
        return CanonicalId(*entry);
    }

    const ParsedId parsed = ParseId(id);
    if (parsed.id.empty()) {
        return {};
    }
    if (parsed.hasStyle && parsed.style == IconStyle::Brands) {
        return "brand:" + parsed.id;
    }
    return parsed.id;
}

std::string CanonicalId(const IconEntry& entry) {
    if (entry.style == IconStyle::Brands) {
        return "brand:" + std::string(entry.id);
    }
    return entry.id;
}

const ImWchar* SolidRanges() {
    return generated::kSolidRanges;
}

const ImWchar* BrandsRanges() {
    return generated::kBrandsRanges;
}

const char* StyleId(IconStyle style) {
    return style == IconStyle::Brands ? "brand" : "solid";
}

const char* StylePrefix(IconStyle style) {
    return style == IconStyle::Brands ? "brand:" : "solid:";
}

const char* CategoryId(IconCategory category) {
    switch (category) {
    case IconCategory::Brands: return "brands";
    case IconCategory::Transport: return "transport";
    case IconCategory::Game: return "game";
    case IconCategory::Documents: return "documents";
    case IconCategory::Communication: return "communication";
    case IconCategory::Status: return "status";
    case IconCategory::Money: return "money";
    case IconCategory::Settings: return "settings";
    case IconCategory::People: return "people";
    case IconCategory::General:
    default: return "general";
    }
}

bool MatchesSearch(const IconEntry& entry, std::string_view normalizedQuery) {
    if (normalizedQuery.empty()) {
        return true;
    }

    const std::string_view id(entry.id);
    if (ContainsNormalizedId(id, normalizedQuery)
        || Contains(StyleId(entry.style), normalizedQuery)
        || Contains(CategoryId(entry.category), normalizedQuery)
        || IdAliasMatches(id, normalizedQuery)
        || TokenAliasMatches(id, normalizedQuery)) {
        return true;
    }

    return entry.style == IconStyle::Brands && Contains("brand brands logo", normalizedQuery);
}

} // namespace icon_registry
