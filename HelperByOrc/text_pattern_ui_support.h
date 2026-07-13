#pragma once

#include "ui_settings.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace text_pattern_ui {

struct ReferenceItem {
    UiText category;
    const char* expression;
    UiText description;
    bool requiresRawColorCodes = false;
};

std::span<const ReferenceItem> ReferenceItems();
bool ContainsBroadWildcard(std::string_view pattern);
std::size_t Utf8CharacterOffset(std::string_view value, std::size_t byteOffset);
std::string FormatCompilePosition(std::string_view pattern, std::size_t byteOffset);
void AppendWarning(std::string& warning, std::string_view message);

} // namespace text_pattern_ui
