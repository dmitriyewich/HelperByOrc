#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace module_signature_scanner {

struct PatternByte {
    std::uint8_t value = 0;
    bool wildcard = false;
};

std::optional<std::vector<PatternByte>> ParsePattern(std::string_view patternText);

bool IsReadableProtect(DWORD protect);
bool GetLoadedModuleImageRange(HMODULE module, std::uintptr_t& imageBase, std::uintptr_t& imageEnd);

std::uintptr_t FindPattern(HMODULE module, const PatternByte* pattern, std::size_t patternSize);
std::uintptr_t FindPattern(HMODULE module, const std::vector<PatternByte>& pattern);
std::uintptr_t FindPattern(HMODULE module, std::string_view patternText);

} // namespace module_signature_scanner
