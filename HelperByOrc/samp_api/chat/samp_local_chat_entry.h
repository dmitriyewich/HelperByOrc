#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace samp_local_chat {

constexpr std::size_t kNativeEntryTextBytes = 0x90;
constexpr std::size_t kChatEntryBaseOffset = 0x132;
constexpr std::size_t kChatEntrySize = 0xFC;
constexpr std::size_t kChatEntryCount = 100;
constexpr std::size_t kChatEntryTextOffset = 0x20;
constexpr std::size_t kChatEntryTypeOffset = 0xF0;
constexpr std::size_t kExtendedEntryTextStorageBytes =
    kChatEntryTypeOffset - kChatEntryTextOffset;
constexpr std::size_t kMaxEntryTextBytes =
    kExtendedEntryTextStorageBytes - 1;
constexpr std::size_t kLatestEntryTextOffset =
    kChatEntryBaseOffset
    + (kChatEntryCount - 1) * kChatEntrySize
    + kChatEntryTextOffset;
constexpr int kLocalMessageType = 8;

constexpr std::uint32_t RgbaToArgb(std::uint32_t color) {
    return (color >> 8) | (color << 24);
}

constexpr int HexDigitValue(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

constexpr bool TryParseColorTag(
    std::string_view text,
    std::size_t offset,
    std::size_t& consumed,
    std::uint32_t& color) {
    consumed = 0;
    color = 0;
    if (offset >= text.size() || text[offset] != '{') {
        return false;
    }

    for (const std::size_t hexLength : { std::size_t{ 6 }, std::size_t{ 8 } }) {
        const std::size_t close = offset + hexLength + 1;
        if (close >= text.size() || text[close] != '}') {
            continue;
        }

        std::uint32_t parsed = 0;
        for (std::size_t index = offset + 1; index < close; ++index) {
            const int digit = HexDigitValue(text[index]);
            if (digit < 0) {
                return false;
            }
            parsed = parsed * 16u + static_cast<std::uint32_t>(digit);
        }

        consumed = hexLength + 2;
        color = hexLength == 6 ? ((parsed << 8) | 0xFFu) : parsed;
        return true;
    }
    return false;
}

constexpr std::size_t SafeTruncationLength(std::string_view text) {
    if (text.size() <= kMaxEntryTextBytes) {
        return text.size();
    }

    for (std::size_t position = 0; position < kMaxEntryTextBytes;) {
        std::size_t consumed = 0;
        std::uint32_t color = 0;
        if (!TryParseColorTag(text, position, consumed, color)) {
            ++position;
            continue;
        }
        if (position + consumed > kMaxEntryTextBytes) {
            return position;
        }
        position += consumed;
    }
    return kMaxEntryTextBytes;
}

consteval bool LocalEntryContract() {
    constexpr std::string_view maxPlusOneText =
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA";
    static_assert(maxPlusOneText.size() == 208);

    constexpr std::string_view colorCrossingText =
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAA"
        "{FFFFFF}"
        "BBBBBBBBBB";
    static_assert(colorCrossingText.size() == 222);

    constexpr std::string_view colorAtLimitText =
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAA"
        "AAAAAAA"
        "{FFFFFF}"
        "B";
    static_assert(colorAtLimitText.size() == 208);

    std::size_t rgbaConsumed = 0;
    std::uint32_t rgbaColor = 0;
    const bool rgbaParsed = TryParseColorTag("{11223344}", 0, rgbaConsumed, rgbaColor);
    std::size_t invalidConsumed = 1;
    std::uint32_t invalidColor = 1;
    const bool invalidParsed = TryParseColorTag("{12345X}", 0, invalidConsumed, invalidColor);

    return SafeTruncationLength(maxPlusOneText) == kMaxEntryTextBytes
        && SafeTruncationLength(colorCrossingText) == 204
        && SafeTruncationLength(colorAtLimitText) == kMaxEntryTextBytes
        && rgbaParsed
        && rgbaConsumed == 10
        && rgbaColor == 0x11223344u
        && RgbaToArgb(0x6495EDFFu) == 0xFF6495EDu
        && kExtendedEntryTextStorageBytes == 0xD0
        && kMaxEntryTextBytes == 0xCF
        && kLatestEntryTextOffset == 0x62C6
        && !invalidParsed
        && invalidConsumed == 0
        && invalidColor == 0;
}

static_assert(LocalEntryContract());

} // namespace samp_local_chat
