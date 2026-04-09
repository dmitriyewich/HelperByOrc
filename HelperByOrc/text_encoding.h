#pragma once

#include <string>
#include <string_view>

namespace textencoding {

enum class EncodingGuess {
    Empty,
    Ascii,
    Utf8,
    Cp1251,
};

EncodingGuess Detect(std::string_view text);
bool IsUtf8(std::string_view text);
bool IsAscii(std::string_view text);

std::string AnsiToUtf8(std::string_view text, unsigned int codePage);
std::string Utf8ToAnsi(std::string_view text, unsigned int codePage);

std::string GameToUtf8(std::string_view text);
std::string Utf8ToGame(std::string_view text);

} // namespace textencoding
