#include "tags_module_impl.h"

#include "debug_log.h"
#include "text_encoding.h"
#include "user_files_path.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>

namespace {
constexpr std::uintmax_t kMaxDictionaryFileBytes = 1024 * 1024;
constexpr std::size_t kMaxDictionaryPairs = 4096;
constexpr std::size_t kMaxDictionaryWordBytes = 128;
constexpr wchar_t kDictionaryRelativePath[] = L"vars\\transliteration.txt";
constexpr std::string_view kDictionaryTemplate =
    "# HelperByOrc: Latin|Кириллица\n"
    "# Одна пара слов на строку. Пустые строки и строки с # игнорируются.\n"
    "# One word pair per line. Empty lines and lines starting with # are ignored.\n"
    "# Collins|Коллинс\n";

bool IsAsciiWhitespace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

std::string_view TrimAscii(std::string_view value) {
    while (!value.empty() && IsAsciiWhitespace(value.front())) {
        value.remove_prefix(1);
    }
    while (!value.empty() && IsAsciiWhitespace(value.back())) {
        value.remove_suffix(1);
    }
    return value;
}

bool IsAsciiLetter(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

char ToLowerAscii(char ch) {
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
}

bool NormalizeLatinWord(std::string_view word, std::string& normalized) {
    if (word.empty() || word.size() > kMaxDictionaryWordBytes) {
        return false;
    }

    normalized.clear();
    normalized.reserve(word.size());
    for (const char ch : word) {
        if (!IsAsciiLetter(ch)) {
            return false;
        }
        normalized.push_back(ToLowerAscii(ch));
    }
    return true;
}

void AppendUtf8CodePoint(std::string& output, std::uint16_t codePoint) {
    output.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
    output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
}

bool NormalizeRussianWord(std::string_view word, std::string& normalized) {
    if (word.empty() || word.size() > kMaxDictionaryWordBytes) {
        return false;
    }

    normalized.clear();
    normalized.reserve(word.size());
    std::size_t position = 0;
    while (position < word.size()) {
        if (position + 1 >= word.size()) {
            return false;
        }

        const unsigned char lead = static_cast<unsigned char>(word[position]);
        const unsigned char continuation = static_cast<unsigned char>(word[position + 1]);
        if ((lead != 0xD0 && lead != 0xD1) || (continuation & 0xC0) != 0x80) {
            return false;
        }

        std::uint16_t codePoint = static_cast<std::uint16_t>(((lead & 0x1F) << 6) | (continuation & 0x3F));
        if (codePoint == 0x0401) {
            codePoint = 0x0451;
        } else if (codePoint >= 0x0410 && codePoint <= 0x042F) {
            codePoint = static_cast<std::uint16_t>(codePoint + 0x20);
        } else if (codePoint != 0x0451 && (codePoint < 0x0430 || codePoint > 0x044F)) {
            return false;
        }

        AppendUtf8CodePoint(normalized, codePoint);
        position += 2;
    }
    return true;
}

std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }

    const int length = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (length <= 0) {
        return {};
    }

    std::string output(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(value.size()),
            output.data(),
            length,
            nullptr,
            nullptr)
        <= 0) {
        return {};
    }
    return output;
}
} // namespace

void TagsModule::Impl::LoadTransliterationDictionary() {
    transliterationDictionaryOpenFailed_ = false;

    const std::optional<std::filesystem::path> helperDataPath = helper_paths::ResolveHelperDataDirectory();
    if (!helperDataPath) {
        transliterationDictionaryStatus_.state = TransliterationDictionaryState::Error;
        debuglog::WriteError("[tags][transliteration] dictionary path unavailable");
        return;
    }

    transliterationDictionaryPath_ = *helperDataPath / kDictionaryRelativePath;
    transliterationDictionaryPathUtf8_ = WideToUtf8(transliterationDictionaryPath_.wstring());

    std::error_code error;
    const bool exists = std::filesystem::exists(transliterationDictionaryPath_, error);
    if (error) {
        transliterationDictionaryStatus_.state = TransliterationDictionaryState::Error;
        debuglog::WriteError(
            "[tags][transliteration] dictionary stat failed error=%d path=\"%s\"",
            error.value(),
            transliterationDictionaryPathUtf8_.c_str());
        return;
    }
    if (!exists) {
        transliterationDictionary_ = {};
        transliterationDictionaryStatus_ = {};
        transliterationDictionaryStatus_.state = TransliterationDictionaryState::Missing;
        debuglog::WriteInfo(
            "[tags][transliteration] dictionary missing path=\"%s\"",
            transliterationDictionaryPathUtf8_.c_str());
        return;
    }

    const std::uintmax_t fileSize = std::filesystem::file_size(transliterationDictionaryPath_, error);
    if (error || fileSize > kMaxDictionaryFileBytes) {
        transliterationDictionaryStatus_.state = TransliterationDictionaryState::Error;
        debuglog::WriteError(
            "[tags][transliteration] dictionary size rejected bytes=%llu error=%d path=\"%s\"",
            static_cast<unsigned long long>(error ? 0 : fileSize),
            error.value(),
            transliterationDictionaryPathUtf8_.c_str());
        return;
    }

    std::ifstream file(transliterationDictionaryPath_, std::ios::binary);
    if (!file) {
        transliterationDictionaryStatus_.state = TransliterationDictionaryState::Error;
        debuglog::WriteError(
            "[tags][transliteration] dictionary open failed path=\"%s\"",
            transliterationDictionaryPathUtf8_.c_str());
        return;
    }

    std::string content(static_cast<std::size_t>(fileSize), '\0');
    if (fileSize != 0) {
        file.read(content.data(), static_cast<std::streamsize>(content.size()));
        if (file.gcount() != static_cast<std::streamsize>(content.size())) {
            transliterationDictionaryStatus_.state = TransliterationDictionaryState::Error;
            debuglog::WriteError(
                "[tags][transliteration] dictionary read failed path=\"%s\"",
                transliterationDictionaryPathUtf8_.c_str());
            return;
        }
    }
    if (file.peek() != std::char_traits<char>::eof()) {
        transliterationDictionaryStatus_.state = TransliterationDictionaryState::Error;
        debuglog::WriteError(
            "[tags][transliteration] dictionary changed while reading path=\"%s\"",
            transliterationDictionaryPathUtf8_.c_str());
        return;
    }

    if (content.size() >= 3
        && static_cast<unsigned char>(content[0]) == 0xEF
        && static_cast<unsigned char>(content[1]) == 0xBB
        && static_cast<unsigned char>(content[2]) == 0xBF) {
        content.erase(0, 3);
    }
    if (!content.empty() && !textencoding::IsUtf8(content)) {
        transliterationDictionaryStatus_.state = TransliterationDictionaryState::Error;
        debuglog::WriteError(
            "[tags][transliteration] dictionary invalid UTF-8 path=\"%s\"",
            transliterationDictionaryPathUtf8_.c_str());
        return;
    }

    TransliterationDictionarySnapshot snapshot;
    TransliterationDictionaryStatus status;
    const std::size_t estimatedPairs = std::min<std::size_t>(
        static_cast<std::size_t>(std::count(content.begin(), content.end(), '\n')) + 1,
        kMaxDictionaryPairs);
    snapshot.latinToCyrillic.reserve(estimatedPairs);
    snapshot.cyrillicToLatin.reserve(estimatedPairs);
    std::unordered_set<std::string, TransparentStringHash, TransparentStringEqual> knownPairs;
    knownPairs.reserve(estimatedPairs);

    std::string normalizedLatin;
    std::string normalizedCyrillic;
    std::size_t lineBegin = 0;
    while (lineBegin <= content.size()) {
        const std::size_t newline = content.find('\n', lineBegin);
        const std::size_t lineEnd = newline == std::string::npos ? content.size() : newline;
        std::string_view line(content.data() + lineBegin, lineEnd - lineBegin);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        line = TrimAscii(line);

        if (!line.empty() && line.front() != '#') {
            const std::size_t separator = line.find('|');
            if (separator == std::string_view::npos || line.find('|', separator + 1) != std::string_view::npos) {
                ++status.invalidLines;
            } else {
                const std::string_view latin = TrimAscii(line.substr(0, separator));
                const std::string_view cyrillic = TrimAscii(line.substr(separator + 1));
                if (!NormalizeLatinWord(latin, normalizedLatin)
                    || !NormalizeRussianWord(cyrillic, normalizedCyrillic)) {
                    ++status.invalidLines;
                } else {
                    std::string pairKey;
                    pairKey.reserve(normalizedLatin.size() + normalizedCyrillic.size() + 1);
                    pairKey.append(normalizedLatin);
                    pairKey.push_back('\0');
                    pairKey.append(normalizedCyrillic);

                    if (knownPairs.contains(pairKey)) {
                        ++status.duplicateLines;
                    } else if (snapshot.latinToCyrillic.contains(normalizedLatin)
                        || snapshot.cyrillicToLatin.contains(normalizedCyrillic)) {
                        ++status.conflictLines;
                    } else if (snapshot.latinToCyrillic.size() >= kMaxDictionaryPairs) {
                        ++status.limitLines;
                    } else {
                        knownPairs.emplace(std::move(pairKey));
                        snapshot.latinToCyrillic.emplace(normalizedLatin, std::string(cyrillic));
                        snapshot.cyrillicToLatin.emplace(normalizedCyrillic, std::string(latin));
                    }
                }
            }
        }

        if (newline == std::string::npos) {
            break;
        }
        lineBegin = newline + 1;
    }

    status.loadedPairs = snapshot.latinToCyrillic.size();
    const std::size_t skipped =
        status.invalidLines + status.duplicateLines + status.conflictLines + status.limitLines;
    status.state = skipped == 0
        ? TransliterationDictionaryState::Loaded
        : TransliterationDictionaryState::LoadedWithWarnings;

    transliterationDictionary_ = std::move(snapshot);
    transliterationDictionaryStatus_ = status;
    debuglog::WriteInfo(
        "[tags][transliteration] dictionary loaded pairs=%llu invalid=%llu duplicates=%llu conflicts=%llu limit=%llu path=\"%s\"",
        static_cast<unsigned long long>(status.loadedPairs),
        static_cast<unsigned long long>(status.invalidLines),
        static_cast<unsigned long long>(status.duplicateLines),
        static_cast<unsigned long long>(status.conflictLines),
        static_cast<unsigned long long>(status.limitLines),
        transliterationDictionaryPathUtf8_.c_str());
}

bool TagsModule::Impl::OpenTransliterationDictionaryFile() {
    transliterationDictionaryOpenFailed_ = false;
    if (transliterationDictionaryPath_.empty()) {
        const std::optional<std::filesystem::path> helperDataPath = helper_paths::ResolveHelperDataDirectory();
        if (!helperDataPath) {
            transliterationDictionaryOpenFailed_ = true;
            debuglog::WriteError("[tags][transliteration] dictionary path unavailable for open");
            return false;
        }
        transliterationDictionaryPath_ = *helperDataPath / kDictionaryRelativePath;
        transliterationDictionaryPathUtf8_ = WideToUtf8(transliterationDictionaryPath_.wstring());
    }

    std::error_code error;
    const bool exists = std::filesystem::exists(transliterationDictionaryPath_, error);
    if (error) {
        transliterationDictionaryOpenFailed_ = true;
        debuglog::WriteError(
            "[tags][transliteration] dictionary stat before open failed error=%d path=\"%s\"",
            error.value(),
            transliterationDictionaryPathUtf8_.c_str());
        return false;
    }

    if (!exists) {
        std::filesystem::create_directories(transliterationDictionaryPath_.parent_path(), error);
        if (error) {
            transliterationDictionaryOpenFailed_ = true;
            debuglog::WriteError(
                "[tags][transliteration] dictionary directory create failed error=%d path=\"%s\"",
                error.value(),
                transliterationDictionaryPathUtf8_.c_str());
            return false;
        }

        std::ofstream file(transliterationDictionaryPath_, std::ios::binary | std::ios::trunc);
        if (!file) {
            transliterationDictionaryOpenFailed_ = true;
            debuglog::WriteError(
                "[tags][transliteration] dictionary template create failed path=\"%s\"",
                transliterationDictionaryPathUtf8_.c_str());
            return false;
        }
        file.write(kDictionaryTemplate.data(), static_cast<std::streamsize>(kDictionaryTemplate.size()));
        file.close();
        if (!file) {
            transliterationDictionaryOpenFailed_ = true;
            debuglog::WriteError(
                "[tags][transliteration] dictionary template write failed path=\"%s\"",
                transliterationDictionaryPathUtf8_.c_str());
            return false;
        }
        LoadTransliterationDictionary();
    }

    const HINSTANCE result = ShellExecuteW(
        nullptr,
        L"open",
        transliterationDictionaryPath_.c_str(),
        nullptr,
        nullptr,
        SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        transliterationDictionaryOpenFailed_ = true;
        debuglog::WriteError(
            "[tags][transliteration] dictionary shell open failed code=%lld path=\"%s\"",
            static_cast<long long>(reinterpret_cast<INT_PTR>(result)),
            transliterationDictionaryPathUtf8_.c_str());
        return false;
    }
    return true;
}

bool TagsModule::Impl::HasTransliterationDictionary() const {
    return !transliterationDictionary_.latinToCyrillic.empty();
}

const std::string* TagsModule::Impl::FindLatinDictionaryWord(std::string_view normalizedWord) const {
    const auto found = transliterationDictionary_.latinToCyrillic.find(normalizedWord);
    return found == transliterationDictionary_.latinToCyrillic.end() ? nullptr : &found->second;
}

const std::string* TagsModule::Impl::FindCyrillicDictionaryWord(std::string_view normalizedWord) const {
    const auto found = transliterationDictionary_.cyrillicToLatin.find(normalizedWord);
    return found == transliterationDictionary_.cyrillicToLatin.end() ? nullptr : &found->second;
}
