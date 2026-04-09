#include "text_encoding.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <string_view>

namespace textencoding {

namespace {

constexpr unsigned int kCp1251 = 1251;

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

} // namespace textencoding
