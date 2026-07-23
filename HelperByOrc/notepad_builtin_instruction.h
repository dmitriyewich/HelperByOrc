#pragma once

#include <string_view>

namespace notepadbuiltin {

std::string_view DefaultId();
std::string_view Title();
std::string_view InstructionText();
bool IsInstructionSource(std::string_view relativePath);
bool IsInstructionSource(std::wstring_view relativePath);

} // namespace notepadbuiltin
