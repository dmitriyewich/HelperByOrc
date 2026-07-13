#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace native_file_dialog {

std::optional<std::filesystem::path> OpenFile(const std::wstring& title, const std::wstring& filter);

} // namespace native_file_dialog
