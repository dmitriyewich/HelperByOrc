#include "text_encoding.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <string>
#include <string_view>

namespace textencoding {

namespace {

constexpr unsigned int kCp1251 = 1251;

// Russian GTA GXT uses font-table byte indices, including ASCII look-alikes.
constexpr std::array<unsigned char, 256> MakeGxtToCp1251Table() {
    std::array<unsigned char, 256> table{};
    for (std::size_t index = 0; index < table.size(); ++index) {
        table[index] = static_cast<unsigned char>(index);
    }

    table[0x3F] = 0xF4;
    table[0x41] = 0xC0;
    table[0x43] = 0xD1;
    table[0x45] = 0xA8;
    table[0x48] = 0xCD;
    table[0x4B] = 0xCA;
    table[0x4D] = 0xCC;
    table[0x4F] = 0xCE;
    table[0x50] = 0xD0;
    table[0x58] = 0xD5;
    table[0x59] = 0xD3;
    table[0x61] = 0xE0;
    table[0x63] = 0xF1;
    table[0x65] = 0xE5;
    table[0x6B] = 0xEA;
    table[0x6F] = 0xEE;
    table[0x70] = 0xF0;
    table[0x78] = 0xF5;
    table[0x79] = 0xF3;
    table[0x80] = 0xC1;
    table[0x81] = 0xD4;
    table[0x82] = 0xC3;
    table[0x83] = 0xC4;
    table[0x84] = 0xC6;
    table[0x85] = 0xC8;
    table[0x86] = 0xC9;
    table[0x87] = 0xCB;
    table[0x88] = 0xC7;
    table[0x89] = 0xD6;
    table[0x8A] = 0xD9;
    table[0x8B] = 0xC2;
    table[0x8C] = 0xCF;
    table[0x8D] = 0xD7;
    table[0x8E] = 0xD8;
    table[0x8F] = 0xD2;
    table[0x90] = 0xFA;
    table[0x91] = 0xDB;
    table[0x92] = 0xDC;
    table[0x93] = 0xDD;
    table[0x94] = 0xDE;
    table[0x95] = 0xDF;
    table[0x96] = 0xCC;
    table[0x97] = 0xE1;
    table[0x98] = 0xF4;
    table[0x99] = 0xE3;
    table[0x9A] = 0xE4;
    table[0x9B] = 0xE6;
    table[0x9C] = 0xE8;
    table[0x9D] = 0xE9;
    table[0x9E] = 0xEB;
    table[0x9F] = 0xE7;
    table[0xA0] = 0xF6;
    table[0xA1] = 0xF9;
    table[0xA2] = 0xE2;
    table[0xA3] = 0xEF;
    table[0xA4] = 0xF7;
    table[0xA5] = 0xF8;
    table[0xA6] = 0xF2;
    table[0xA7] = 0xDA;
    table[0xA8] = 0xFB;
    table[0xA9] = 0xFC;
    table[0xAA] = 0xFD;
    table[0xAB] = 0xFE;
    table[0xAC] = 0xFF;
    table[0xAD] = 0xCD;
    table[0xAE] = 0xED;
    table[0xAF] = 0xEC;
    return table;
}

constexpr auto kGxtToCp1251 = MakeGxtToCp1251Table();

std::wstring DecodeMultiByte(std::string_view text, unsigned int codePage, DWORD flags) {
    if (text.empty()) {
        return {};
    }

    const int wideLength = MultiByteToWideChar(codePage, flags, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (wideLength <= 0) {
        return {};
    }

    std::wstring wideBuffer(static_cast<std::size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(codePage, flags, text.data(), static_cast<int>(text.size()), wideBuffer.data(), wideLength) <= 0) {
        return {};
    }

    return wideBuffer;
}

std::string EncodeMultiByte(std::wstring_view text, unsigned int codePage, DWORD flags) {
    if (text.empty()) {
        return {};
    }

    const int ansiLength = WideCharToMultiByte(
        codePage, flags, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (ansiLength <= 0) {
        return {};
    }

    std::string ansiBuffer(static_cast<std::size_t>(ansiLength), '\0');
    if (WideCharToMultiByte(
            codePage,
            flags,
            text.data(),
            static_cast<int>(text.size()),
            ansiBuffer.data(),
            ansiLength,
            nullptr,
            nullptr)
        <= 0) {
        return {};
    }

    return ansiBuffer;
}

} // namespace

EncodingGuess Detect(std::string_view text) {
    if (text.empty()) {
        return EncodingGuess::Empty;
    }

    if (IsAscii(text)) {
        return EncodingGuess::Ascii;
    }

    if (IsUtf8(text)) {
        return EncodingGuess::Utf8;
    }

    return EncodingGuess::Cp1251;
}

bool IsUtf8(std::string_view text) {
    if (text.empty()) {
        return true;
    }

    const auto wide = DecodeMultiByte(text, CP_UTF8, MB_ERR_INVALID_CHARS);
    return !wide.empty();
}

bool IsAscii(std::string_view text) {
    for (const unsigned char ch : text) {
        if (ch >= 0x80) {
            return false;
        }
    }
    return true;
}

std::string AnsiToUtf8(std::string_view text, unsigned int codePage) {
    if (text.empty()) {
        return {};
    }

    const auto wide = DecodeMultiByte(text, codePage, 0);
    if (wide.empty()) {
        return std::string(text);
    }

    const auto utf8 = EncodeMultiByte(wide, CP_UTF8, 0);
    return utf8.empty() ? std::string(text) : utf8;
}

std::string Utf8ToAnsi(std::string_view text, unsigned int codePage) {
    if (text.empty()) {
        return {};
    }

    const auto wide = DecodeMultiByte(text, CP_UTF8, MB_ERR_INVALID_CHARS);
    if (wide.empty()) {
        return std::string(text);
    }

    const auto ansi = EncodeMultiByte(wide, codePage, 0);
    return ansi.empty() ? std::string(text) : ansi;
}

std::string GameToUtf8(std::string_view text) {
    const auto detected = Detect(text);
    if (detected == EncodingGuess::Empty) {
        return {};
    }

    if (detected == EncodingGuess::Ascii || detected == EncodingGuess::Utf8) {
        return std::string(text);
    }

    return AnsiToUtf8(text, kCp1251);
}

std::string Utf8ToGame(std::string_view text) {
    const auto detected = Detect(text);
    if (detected == EncodingGuess::Empty) {
        return {};
    }

    if (detected == EncodingGuess::Cp1251) {
        return std::string(text);
    }

    return Utf8ToAnsi(text, kCp1251);
}

std::string GxtToUtf8(std::string_view text) {
    if (text.empty()) {
        return {};
    }

    bool hasRussianGxtGlyph = false;
    for (const unsigned char ch : text) {
        if (ch >= 0x80 && kGxtToCp1251[ch] != ch) {
            hasRussianGxtGlyph = true;
            break;
        }
    }

    if (!hasRussianGxtGlyph) {
        return GameToUtf8(text);
    }

    std::string cp1251(text);
    for (char& ch : cp1251) {
        ch = static_cast<char>(kGxtToCp1251[static_cast<unsigned char>(ch)]);
    }
    return AnsiToUtf8(cp1251, kCp1251);
}

} // namespace textencoding
