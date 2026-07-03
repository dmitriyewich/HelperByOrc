#include "module_signature_scanner.h"

#include <algorithm>
#include <cctype>
#include <limits>

namespace module_signature_scanner {
namespace {

constexpr DWORD kProtectTypeMask = 0xFF;

int HexValue(unsigned char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return -1;
}

bool TryParseHexByte(std::string_view token, std::uint8_t& value) {
    if (token.size() != 2) {
        return false;
    }

    const int high = HexValue(static_cast<unsigned char>(token[0]));
    const int low = HexValue(static_cast<unsigned char>(token[1]));
    if (high < 0 || low < 0) {
        return false;
    }

    value = static_cast<std::uint8_t>((high << 4) | low);
    return true;
}

bool IsWildcardToken(std::string_view token) {
    return token == "?" || token == "??";
}

bool IsReadableBaseProtect(DWORD protect) {
    switch (protect & kProtectTypeMask) {
    case PAGE_READONLY:
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    default:
        return false;
    }
}

std::uintptr_t ScanReadableRange(
    std::uintptr_t begin,
    std::size_t size,
    const PatternByte* pattern,
    std::size_t patternSize) {
    __try {
        const auto* const bytes = reinterpret_cast<const std::uint8_t*>(begin);
        const std::size_t limit = size - patternSize;

        for (std::size_t offset = 0; offset <= limit; ++offset) {
            bool found = true;
            for (std::size_t index = 0; index < patternSize; ++index) {
                const PatternByte& patternByte = pattern[index];
                if (!patternByte.wildcard && bytes[offset + index] != patternByte.value) {
                    found = false;
                    break;
                }
            }

            if (found) {
                return begin + offset;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }

    return 0;
}

} // namespace

std::optional<std::vector<PatternByte>> ParsePattern(std::string_view patternText) {
    std::vector<PatternByte> pattern;

    std::size_t offset = 0;
    while (offset < patternText.size()) {
        while (offset < patternText.size()
            && std::isspace(static_cast<unsigned char>(patternText[offset])) != 0) {
            ++offset;
        }
        if (offset >= patternText.size()) {
            break;
        }

        const std::size_t tokenBegin = offset;
        while (offset < patternText.size()
            && std::isspace(static_cast<unsigned char>(patternText[offset])) == 0) {
            ++offset;
        }

        const std::string_view token = patternText.substr(tokenBegin, offset - tokenBegin);
        if (IsWildcardToken(token)) {
            pattern.push_back(PatternByte{ 0, true });
            continue;
        }

        std::uint8_t value = 0;
        if (!TryParseHexByte(token, value)) {
            return std::nullopt;
        }

        pattern.push_back(PatternByte{ value, false });
    }

    if (pattern.empty()) {
        return std::nullopt;
    }

    return pattern;
}

bool IsReadableProtect(DWORD protect) {
    if ((protect & PAGE_GUARD) != 0 || (protect & PAGE_NOACCESS) != 0) {
        return false;
    }

    return IsReadableBaseProtect(protect);
}

bool GetLoadedModuleImageRange(HMODULE module, std::uintptr_t& imageBase, std::uintptr_t& imageEnd) {
    imageBase = 0;
    imageEnd = 0;

    if (module == nullptr) {
        return false;
    }

    const auto base = reinterpret_cast<std::uintptr_t>(module);

    __try {
        const auto* const dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE || dosHeader->e_lfanew <= 0) {
            return false;
        }

        const auto* const ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dosHeader->e_lfanew);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
            return false;
        }

        const auto sizeOfImage = static_cast<std::uintptr_t>(ntHeaders->OptionalHeader.SizeOfImage);
        if (sizeOfImage == 0 || base > std::numeric_limits<std::uintptr_t>::max() - sizeOfImage) {
            return false;
        }

        imageBase = base;
        imageEnd = base + sizeOfImage;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        imageBase = 0;
        imageEnd = 0;
        return false;
    }
}

std::uintptr_t FindPattern(HMODULE module, const PatternByte* pattern, std::size_t patternSize) {
    if (pattern == nullptr || patternSize == 0) {
        return 0;
    }

    std::uintptr_t imageBase = 0;
    std::uintptr_t imageEnd = 0;
    if (!GetLoadedModuleImageRange(module, imageBase, imageEnd)) {
        return 0;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    std::uintptr_t current = imageBase;

    while (current < imageEnd) {
        if (VirtualQuery(reinterpret_cast<const void*>(current), &mbi, sizeof(mbi)) == 0) {
            break;
        }

        const auto regionBase = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        if (regionBase == 0 || mbi.RegionSize == 0) {
            break;
        }

        const std::uintptr_t regionEnd =
            regionBase > std::numeric_limits<std::uintptr_t>::max() - mbi.RegionSize
            ? std::numeric_limits<std::uintptr_t>::max()
            : regionBase + mbi.RegionSize;
        if (regionEnd <= current) {
            break;
        }

        if (mbi.State == MEM_COMMIT && IsReadableProtect(mbi.Protect)) {
            const std::uintptr_t scanBegin = std::max(current, std::max(regionBase, imageBase));
            const std::uintptr_t scanEnd = std::min(regionEnd, imageEnd);
            if (scanEnd > scanBegin) {
                const auto scanSize = static_cast<std::size_t>(scanEnd - scanBegin);
                if (scanSize >= patternSize) {
                    const std::uintptr_t found = ScanReadableRange(scanBegin, scanSize, pattern, patternSize);
                    if (found != 0) {
                        return found;
                    }
                }
            }
        }

        current = regionEnd;
    }

    return 0;
}

std::uintptr_t FindPattern(HMODULE module, const std::vector<PatternByte>& pattern) {
    return FindPattern(module, pattern.data(), pattern.size());
}

std::uintptr_t FindPattern(HMODULE module, std::string_view patternText) {
    const std::optional<std::vector<PatternByte>> pattern = ParsePattern(patternText);
    if (!pattern.has_value()) {
        return 0;
    }

    return FindPattern(module, pattern->data(), pattern->size());
}

} // namespace module_signature_scanner
