#include "notepad_txt_sync.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "debug_log.h"
#include "notepad_builtin_instruction.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace notepadtxt {
namespace {

namespace fs = std::filesystem;

constexpr unsigned int kCp1251 = 1251;
constexpr auto kWatcherDebounce = std::chrono::milliseconds(300);
constexpr DWORD kWatcherFilter = FILE_NOTIFY_CHANGE_FILE_NAME
    | FILE_NOTIFY_CHANGE_DIR_NAME
    | FILE_NOTIFY_CHANGE_SIZE
    | FILE_NOTIFY_CHANGE_LAST_WRITE
    | FILE_NOTIFY_CHANGE_CREATION;

std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            result.data(),
            required)
        != required) {
        return {};
    }
    return result;
}

std::string WideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            result.data(),
            required,
            nullptr,
            nullptr)
        != required) {
        return {};
    }
    return result;
}

std::wstring DecodeMultiByte(std::string_view text, unsigned int codePage, DWORD flags) {
    if (text.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(
        codePage,
        flags,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            codePage,
            flags,
            text.data(),
            static_cast<int>(text.size()),
            result.data(),
            required)
        != required) {
        return {};
    }
    return result;
}

bool EncodeMultiByte(
    std::wstring_view text,
    unsigned int codePage,
    DWORD flags,
    std::string& result,
    bool rejectBestFit) {
    result.clear();
    if (text.empty()) {
        return true;
    }
    BOOL usedDefault = FALSE;
    BOOL* usedDefaultPtr = rejectBestFit ? &usedDefault : nullptr;
    const int required = WideCharToMultiByte(
        codePage,
        flags,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        usedDefaultPtr);
    if (required <= 0 || usedDefault) {
        return false;
    }
    result.resize(static_cast<std::size_t>(required));
    usedDefault = FALSE;
    const int written = WideCharToMultiByte(
        codePage,
        flags,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        required,
        nullptr,
        usedDefaultPtr);
    return written == required && !usedDefault;
}

std::wstring LowerOrdinal(std::wstring value) {
    if (!value.empty()) {
        CharLowerBuffW(value.data(), static_cast<DWORD>(value.size()));
    }
    return value;
}

bool EqualsOrdinalInsensitive(std::wstring_view lhs, std::wstring_view rhs) {
    return CompareStringOrdinal(
               lhs.data(),
               static_cast<int>(lhs.size()),
               rhs.data(),
               static_cast<int>(rhs.size()),
               TRUE)
        == CSTR_EQUAL;
}

bool IsTxtExtension(const fs::path& path) {
    return EqualsOrdinalInsensitive(path.extension().wstring(), L".txt");
}

bool IsExcludedTopLevelPath(const fs::path& relative) {
    const auto first = relative.begin();
    if (first == relative.end()) {
        return false;
    }
    const std::wstring name = first->wstring();
    return EqualsOrdinalInsensitive(name, L"images") || EqualsOrdinalInsensitive(name, L"export");
}

bool HasReparsePoint(const fs::directory_entry& entry) {
    const DWORD attributes = GetFileAttributesW(entry.path().c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

std::wstring RelativeKey(const fs::path& relative) {
    return LowerOrdinal(relative.lexically_normal().generic_wstring());
}

std::wstring RelativeUtf8Key(std::string_view relativeUtf8) {
    const std::wstring wide = Utf8ToWide(relativeUtf8);
    if (wide.empty() && !relativeUtf8.empty()) {
        return {};
    }
    return RelativeKey(fs::path(wide));
}

std::string RelativePathToUtf8(const fs::path& relative) {
    return WideToUtf8(relative.lexically_normal().generic_wstring());
}

std::uint64_t FileTimeToUint64(const FILETIME& value) {
    ULARGE_INTEGER result{};
    result.LowPart = value.dwLowDateTime;
    result.HighPart = value.dwHighDateTime;
    return result.QuadPart;
}

std::string FileIdentity(const BY_HANDLE_FILE_INFORMATION& info) {
    char buffer[40]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%08lX:%08lX%08lX",
        info.dwVolumeSerialNumber,
        info.nFileIndexHigh,
        info.nFileIndexLow);
    return buffer;
}

std::string HashBytes(std::string_view bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    char buffer[24]{};
    std::snprintf(buffer, sizeof(buffer), "%016llX", static_cast<unsigned long long>(hash));
    return buffer;
}

NewlineStyle DetectNewlineStyle(std::string_view text) {
    std::size_t crlf = 0;
    std::size_t lf = 0;
    std::size_t cr = 0;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\r') {
            if (index + 1 < text.size() && text[index + 1] == '\n') {
                ++crlf;
                ++index;
            } else {
                ++cr;
            }
        } else if (text[index] == '\n') {
            ++lf;
        }
    }
    if (crlf >= lf && crlf >= cr && crlf != 0) {
        return NewlineStyle::CrLf;
    }
    if (cr > lf && cr != 0) {
        return NewlineStyle::Cr;
    }
    return NewlineStyle::Lf;
}

std::string NormalizeNewlines(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\r') {
            if (index + 1 < text.size() && text[index + 1] == '\n') {
                ++index;
            }
            result.push_back('\n');
        } else {
            result.push_back(text[index]);
        }
    }
    return result;
}

std::string ApplyNewlineStyle(std::string_view text, NewlineStyle style) {
    const std::string normalized = NormalizeNewlines(text);
    if (style == NewlineStyle::Lf) {
        return normalized;
    }
    const std::string_view newline = style == NewlineStyle::CrLf ? "\r\n" : "\r";
    std::string result;
    result.reserve(normalized.size() + normalized.size() / 20);
    for (const char ch : normalized) {
        if (ch == '\n') {
            result.append(newline);
        } else {
            result.push_back(ch);
        }
    }
    return result;
}

bool DecodeUtf16(std::string_view bytes, bool bigEndian, std::string& utf8) {
    if ((bytes.size() % 2) != 0) {
        return false;
    }
    std::wstring wide(bytes.size() / 2, L'\0');
    for (std::size_t index = 0; index < wide.size(); ++index) {
        const unsigned char first = static_cast<unsigned char>(bytes[index * 2]);
        const unsigned char second = static_cast<unsigned char>(bytes[index * 2 + 1]);
        const std::uint16_t value = bigEndian
            ? static_cast<std::uint16_t>((first << 8) | second)
            : static_cast<std::uint16_t>(first | (second << 8));
        wide[index] = static_cast<wchar_t>(value);
    }
    utf8 = WideToUtf8(wide);
    return !utf8.empty() || wide.empty();
}

bool ReadHandleBytes(HANDLE file, std::uint64_t size, std::string& bytes, unsigned long& error) {
    bytes.clear();
    if (size > kMaximumFileBytes || size > static_cast<std::uint64_t>(SIZE_MAX)) {
        error = ERROR_FILE_TOO_LARGE;
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    LARGE_INTEGER offset{};
    if (!SetFilePointerEx(file, offset, nullptr, FILE_BEGIN)) {
        error = GetLastError();
        return false;
    }
    std::size_t total = 0;
    while (total < bytes.size()) {
        DWORD read = 0;
        const DWORD remaining = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - total, MAXDWORD));
        if (!ReadFile(file, bytes.data() + total, remaining, &read, nullptr)) {
            error = GetLastError();
            return false;
        }
        if (read == 0) {
            error = ERROR_HANDLE_EOF;
            return false;
        }
        total += read;
    }
    error = ERROR_SUCCESS;
    return true;
}

bool SameVersion(const FileVersion& lhs, const FileVersion& rhs) {
    return lhs.size == rhs.size
        && lhs.lastWriteTime == rhs.lastWriteTime
        && lhs.identity == rhs.identity;
}

struct CacheIndex {
    std::unordered_map<std::wstring, const FileSnapshot*> byPath{};
    std::unordered_map<std::string, const FileSnapshot*> byIdentity{};
    std::unordered_set<std::string> ambiguousIdentities{};
};

CacheIndex BuildCacheIndex(const std::vector<FileSnapshot>& files) {
    CacheIndex result;
    result.byPath.reserve(files.size());
    result.byIdentity.reserve(files.size());
    for (const FileSnapshot& file : files) {
        const std::wstring pathKey = RelativeUtf8Key(file.relativePath);
        if (!pathKey.empty()) {
            result.byPath[pathKey] = &file;
        }
        if (file.version.identity.empty()) {
            continue;
        }
        const auto [_, inserted] = result.byIdentity.emplace(file.version.identity, &file);
        if (!inserted) {
            result.ambiguousIdentities.insert(file.version.identity);
        }
    }
    for (const std::string& identity : result.ambiguousIdentities) {
        result.byIdentity.erase(identity);
    }
    return result;
}

FileSnapshot ScanFile(
    const fs::path& path,
    const fs::path& relative,
    const CacheIndex& cache,
    const std::unordered_set<std::wstring>& dirtyPaths,
    bool forceReadAll,
    ScanMetrics& metrics) {
    FileSnapshot snapshot;
    snapshot.relativePath = RelativePathToUtf8(relative);

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        snapshot.status = FileStatus::ReadError;
        snapshot.error = GetLastError();
        return snapshot;
    }

    BY_HANDLE_FILE_INFORMATION before{};
    if (!GetFileInformationByHandle(file, &before)) {
        snapshot.status = FileStatus::ReadError;
        snapshot.error = GetLastError();
        CloseHandle(file);
        return snapshot;
    }
    snapshot.version.size = (static_cast<std::uint64_t>(before.nFileSizeHigh) << 32) | before.nFileSizeLow;
    snapshot.version.lastWriteTime = FileTimeToUint64(before.ftLastWriteTime);
    snapshot.version.identity = FileIdentity(before);
    if (snapshot.version.size > kMaximumFileBytes) {
        snapshot.status = FileStatus::TooLarge;
        snapshot.error = ERROR_FILE_TOO_LARGE;
        CloseHandle(file);
        return snapshot;
    }

    const std::wstring pathKey = RelativeKey(relative);
    const bool pathDirty = forceReadAll || dirtyPaths.contains(pathKey);
    const FileSnapshot* cached = nullptr;
    if (const auto pathIt = cache.byPath.find(pathKey); pathIt != cache.byPath.end()) {
        cached = pathIt->second;
    } else if (const auto identityIt = cache.byIdentity.find(snapshot.version.identity); identityIt != cache.byIdentity.end()) {
        cached = identityIt->second;
    }
    if (!pathDirty && cached && cached->status == FileStatus::Ready && SameVersion(cached->version, snapshot.version)) {
        snapshot.format = cached->format;
        snapshot.version.contentHash = cached->version.contentHash;
        snapshot.bodyReused = true;
        ++metrics.bodiesReused;
        CloseHandle(file);
        return snapshot;
    }

    std::string bytes;
    if (!ReadHandleBytes(file, snapshot.version.size, bytes, snapshot.error)) {
        snapshot.status = FileStatus::ReadError;
        CloseHandle(file);
        return snapshot;
    }
    BY_HANDLE_FILE_INFORMATION after{};
    if (!GetFileInformationByHandle(file, &after)
        || before.nFileSizeHigh != after.nFileSizeHigh
        || before.nFileSizeLow != after.nFileSizeLow
        || FileTimeToUint64(before.ftLastWriteTime) != FileTimeToUint64(after.ftLastWriteTime)) {
        snapshot.status = FileStatus::ReadError;
        snapshot.error = ERROR_RETRY;
        CloseHandle(file);
        return snapshot;
    }
    CloseHandle(file);

    snapshot.version.contentHash = HashBytes(bytes);
    if (!DecodeTextBytes(bytes, snapshot.text, snapshot.format)) {
        snapshot.status = FileStatus::DecodeError;
        snapshot.error = ERROR_NO_UNICODE_TRANSLATION;
        return snapshot;
    }
    ++metrics.bodiesRead;
    metrics.bytesRead += bytes.size();
    return snapshot;
}

ScanResult ScanDirectory(
    const fs::path& root,
    std::uint64_t generation,
    const std::vector<FileSnapshot>& cachedFiles,
    const std::unordered_set<std::wstring>& dirtyPaths,
    bool forceReadAll,
    bool watcherAvailable,
    const std::atomic_bool& cancelRequested) {
    const auto begin = std::chrono::steady_clock::now();
    ScanResult result;
    result.generation = generation;
    result.metrics.generation = generation;
    result.metrics.watcherAvailable = watcherAvailable;
    const CacheIndex cache = BuildCacheIndex(cachedFiles);

    std::error_code iteratorError;
    fs::recursive_directory_iterator iterator(
        root,
        fs::directory_options::skip_permission_denied,
        iteratorError);
    if (iteratorError) {
        result.metrics.complete = false;
        result.metrics.error = static_cast<unsigned long>(iteratorError.value());
    }
    const fs::recursive_directory_iterator end;
    while (iterator != end) {
        if (cancelRequested.load(std::memory_order_relaxed)) {
            result.metrics.complete = false;
            result.metrics.error = ERROR_CANCELLED;
            break;
        }
        if (iteratorError) {
            result.metrics.complete = false;
            result.metrics.error = static_cast<unsigned long>(iteratorError.value());
            debuglog::WriteError(
                "[notepad][txt] scan iterator failed root=%ls error=%d",
                root.c_str(),
                iteratorError.value());
            iteratorError.clear();
            iterator.increment(iteratorError);
            continue;
        }
        const fs::directory_entry entry = *iterator;
        ++result.metrics.entries;
        std::error_code relativeError;
        const fs::path relative = fs::relative(entry.path(), root, relativeError);
        if (relativeError) {
            iterator.increment(iteratorError);
            continue;
        }
        if (IsExcludedTopLevelPath(relative)) {
            std::error_code directoryError;
            if (entry.is_directory(directoryError) && !directoryError) {
                iterator.disable_recursion_pending();
            }
            iterator.increment(iteratorError);
            continue;
        }
        if (!relative.has_parent_path()
            && notepadbuiltin::IsInstructionSource(relative.wstring())) {
            iterator.increment(iteratorError);
            continue;
        }
        if (HasReparsePoint(entry)) {
            std::error_code directoryError;
            if (entry.is_directory(directoryError) && !directoryError) {
                iterator.disable_recursion_pending();
            }
            iterator.increment(iteratorError);
            continue;
        }
        std::error_code typeError;
        if (entry.is_regular_file(typeError) && !typeError && IsTxtExtension(entry.path())) {
            ++result.metrics.txtFiles;
            result.files.push_back(ScanFile(
                entry.path(),
                relative,
                cache,
                dirtyPaths,
                forceReadAll,
                result.metrics));
        }
        iterator.increment(iteratorError);
    }

    std::sort(result.files.begin(), result.files.end(), [](const FileSnapshot& lhs, const FileSnapshot& rhs) {
        return RelativeUtf8Key(lhs.relativePath) < RelativeUtf8Key(rhs.relativePath);
    });
    result.metrics.durationMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin)
                                      .count();
    return result;
}

} // namespace

bool DecodeTextBytes(std::string_view bytes, std::string& utf8Text, TextFormat& format) {
    utf8Text.clear();
    format = {};
    std::string decoded;
    if (bytes.size() >= 3
        && static_cast<unsigned char>(bytes[0]) == 0xEF
        && static_cast<unsigned char>(bytes[1]) == 0xBB
        && static_cast<unsigned char>(bytes[2]) == 0xBF) {
        const std::string_view payload = bytes.substr(3);
        const std::wstring wide = DecodeMultiByte(payload, CP_UTF8, MB_ERR_INVALID_CHARS);
        if (wide.empty() && !payload.empty()) {
            return false;
        }
        decoded.assign(payload);
        format.encoding = TextEncoding::Utf8;
        format.bom = true;
    } else if (bytes.size() >= 2
        && static_cast<unsigned char>(bytes[0]) == 0xFF
        && static_cast<unsigned char>(bytes[1]) == 0xFE) {
        if (!DecodeUtf16(bytes.substr(2), false, decoded)) {
            return false;
        }
        format.encoding = TextEncoding::Utf16Le;
        format.bom = true;
    } else if (bytes.size() >= 2
        && static_cast<unsigned char>(bytes[0]) == 0xFE
        && static_cast<unsigned char>(bytes[1]) == 0xFF) {
        if (!DecodeUtf16(bytes.substr(2), true, decoded)) {
            return false;
        }
        format.encoding = TextEncoding::Utf16Be;
        format.bom = true;
    } else {
        const std::wstring utf8Wide = DecodeMultiByte(bytes, CP_UTF8, MB_ERR_INVALID_CHARS);
        if (!utf8Wide.empty() || bytes.empty()) {
            decoded.assign(bytes);
            format.encoding = TextEncoding::Utf8;
        } else {
            const std::wstring cp1251Wide = DecodeMultiByte(bytes, kCp1251, 0);
            if (cp1251Wide.empty() && !bytes.empty()) {
                return false;
            }
            decoded = WideToUtf8(cp1251Wide);
            if (decoded.empty() && !cp1251Wide.empty()) {
                return false;
            }
            format.encoding = TextEncoding::Cp1251;
        }
    }
    format.newline = DetectNewlineStyle(decoded);
    utf8Text = NormalizeNewlines(decoded);
    return true;
}

bool EncodeTextBytes(std::string_view utf8Text, const TextFormat& format, std::string& bytes) {
    bytes.clear();
    const std::string withNewlines = ApplyNewlineStyle(utf8Text, format.newline);
    const std::wstring wide = DecodeMultiByte(withNewlines, CP_UTF8, MB_ERR_INVALID_CHARS);
    if (wide.empty() && !withNewlines.empty()) {
        return false;
    }
    if (format.encoding == TextEncoding::Utf8) {
        if (format.bom) {
            bytes.append("\xEF\xBB\xBF", 3);
        }
        bytes.append(withNewlines);
        return true;
    }
    if (format.encoding == TextEncoding::Cp1251) {
        return EncodeMultiByte(wide, kCp1251, WC_NO_BEST_FIT_CHARS, bytes, true);
    }

    bytes.reserve(wide.size() * 2 + 2);
    if (format.encoding == TextEncoding::Utf16Le) {
        bytes.append("\xFF\xFE", 2);
    } else {
        bytes.append("\xFE\xFF", 2);
    }
    for (const wchar_t ch : wide) {
        const auto value = static_cast<std::uint16_t>(ch);
        if (format.encoding == TextEncoding::Utf16Le) {
            bytes.push_back(static_cast<char>(value & 0xFF));
            bytes.push_back(static_cast<char>((value >> 8) & 0xFF));
        } else {
            bytes.push_back(static_cast<char>((value >> 8) & 0xFF));
            bytes.push_back(static_cast<char>(value & 0xFF));
        }
    }
    return true;
}

const char* TextEncodingName(TextEncoding value) {
    switch (value) {
    case TextEncoding::Utf16Le:
        return "utf16le";
    case TextEncoding::Utf16Be:
        return "utf16be";
    case TextEncoding::Cp1251:
        return "cp1251";
    case TextEncoding::Utf8:
    default:
        return "utf8";
    }
}

TextEncoding ParseTextEncoding(std::string_view value) {
    if (value == "utf16le") {
        return TextEncoding::Utf16Le;
    }
    if (value == "utf16be") {
        return TextEncoding::Utf16Be;
    }
    if (value == "cp1251") {
        return TextEncoding::Cp1251;
    }
    return TextEncoding::Utf8;
}

const char* NewlineStyleName(NewlineStyle value) {
    switch (value) {
    case NewlineStyle::Lf:
        return "lf";
    case NewlineStyle::Cr:
        return "cr";
    case NewlineStyle::CrLf:
    default:
        return "crlf";
    }
}

NewlineStyle ParseNewlineStyle(std::string_view value) {
    if (value == "lf") {
        return NewlineStyle::Lf;
    }
    if (value == "cr") {
        return NewlineStyle::Cr;
    }
    return NewlineStyle::CrLf;
}

struct SyncService::Impl {
    mutable std::mutex mutex{};
    std::condition_variable wake{};
    fs::path root{};
    std::uint64_t generation = 0;
    std::vector<FileSnapshot> cachedFiles{};
    std::optional<ScanResult> latestScan{};
    std::thread worker{};
    std::thread watcher{};
    HANDLE directoryHandle = INVALID_HANDLE_VALUE;
    bool stopping = false;
    bool running = false;
    bool scanRequested = false;
    bool directoryDirty = false;
    bool forceReadAll = false;
    bool watcherAvailable = false;
    std::atomic_bool cancelRequested{ false };
    std::chrono::steady_clock::time_point lastDirectoryChange{};
    std::unordered_set<std::wstring> dirtyPaths{};

    ~Impl() {
        Stop();
    }

    void Start(
        fs::path newRoot,
        std::uint64_t newGeneration,
        std::vector<FileSnapshot> seed,
        bool initialForceReadAll) {
        Stop();
        {
            std::lock_guard lock(mutex);
            root = std::move(newRoot);
            generation = newGeneration;
            cachedFiles = std::move(seed);
            latestScan.reset();
            stopping = false;
            running = true;
            scanRequested = true;
            directoryDirty = false;
            forceReadAll = initialForceReadAll;
            watcherAvailable = false;
            dirtyPaths.clear();
        }
        worker = std::thread([this] { WorkerLoop(); });
        watcher = std::thread([this] { WatcherLoop(); });
        wake.notify_all();
    }

    void Stop() {
        HANDLE handle = INVALID_HANDLE_VALUE;
        {
            std::lock_guard lock(mutex);
            if (!running && !worker.joinable() && !watcher.joinable()) {
                return;
            }
            stopping = true;
            cancelRequested.store(true, std::memory_order_relaxed);
            handle = directoryHandle;
        }
        wake.notify_all();
        if (handle != INVALID_HANDLE_VALUE) {
            CancelIoEx(handle, nullptr);
        }
        if (watcher.joinable()) {
            CancelSynchronousIo(watcher.native_handle());
        }
        if (watcher.joinable()) {
            watcher.join();
        }
        if (worker.joinable()) {
            worker.join();
        }
        std::lock_guard lock(mutex);
        if (directoryHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(directoryHandle);
            directoryHandle = INVALID_HANDLE_VALUE;
        }
        running = false;
        stopping = false;
        cancelRequested.store(false, std::memory_order_relaxed);
        watcherAvailable = false;
        latestScan.reset();
        dirtyPaths.clear();
    }

    void RequestFullScan() {
        {
            std::lock_guard lock(mutex);
            if (!running) {
                return;
            }
            scanRequested = true;
            forceReadAll = true;
        }
        wake.notify_all();
    }

    std::optional<ScanResult> TakeLatestScan() {
        std::lock_guard lock(mutex);
        if (!latestScan) {
            return std::nullopt;
        }
        std::optional<ScanResult> result = std::move(latestScan);
        latestScan.reset();
        return result;
    }

    bool Running() const {
        std::lock_guard lock(mutex);
        return running;
    }

    void NotifyDirectoryChange(std::unordered_set<std::wstring> paths, bool all) {
        {
            std::lock_guard lock(mutex);
            if (stopping) {
                return;
            }
            directoryDirty = true;
            forceReadAll = forceReadAll || all;
            lastDirectoryChange = std::chrono::steady_clock::now();
            dirtyPaths.insert(
                std::make_move_iterator(paths.begin()),
                std::make_move_iterator(paths.end()));
        }
        wake.notify_all();
    }

    void WorkerLoop() {
        for (;;) {
            fs::path scanRoot;
            std::uint64_t scanGeneration = 0;
            std::vector<FileSnapshot> scanCache;
            std::unordered_set<std::wstring> scanDirtyPaths;
            bool scanForceAll = false;
            bool scanWatcherAvailable = false;
            {
                std::unique_lock lock(mutex);
                for (;;) {
                    if (stopping) {
                        return;
                    }
                    if (scanRequested) {
                        break;
                    }
                    if (directoryDirty) {
                        const auto deadline = lastDirectoryChange + kWatcherDebounce;
                        if (std::chrono::steady_clock::now() >= deadline) {
                            break;
                        }
                        wake.wait_until(lock, deadline);
                        continue;
                    }
                    wake.wait(lock);
                }
                scanRoot = root;
                scanGeneration = generation;
                scanCache = cachedFiles;
                scanDirtyPaths = std::move(dirtyPaths);
                dirtyPaths.clear();
                scanForceAll = forceReadAll;
                scanWatcherAvailable = watcherAvailable;
                scanRequested = false;
                directoryDirty = false;
                forceReadAll = false;
            }

            ScanResult result = ScanDirectory(
                scanRoot,
                scanGeneration,
                scanCache,
                scanDirtyPaths,
                scanForceAll,
                scanWatcherAvailable,
                cancelRequested);
            debuglog::WriteInfo(
                "[notepad][txt][perf] generation=%llu entries=%zu txt=%zu read=%zu reused=%zu bytes=%llu scan=%.2fms watcher=%d complete=%d error=%lu",
                static_cast<unsigned long long>(result.generation),
                result.metrics.entries,
                result.metrics.txtFiles,
                result.metrics.bodiesRead,
                result.metrics.bodiesReused,
                static_cast<unsigned long long>(result.metrics.bytesRead),
                result.metrics.durationMs,
                result.metrics.watcherAvailable ? 1 : 0,
                result.metrics.complete ? 1 : 0,
                result.metrics.error);
            {
                std::lock_guard lock(mutex);
                if (stopping || generation != scanGeneration) {
                    continue;
                }
                cachedFiles.clear();
                cachedFiles.reserve(result.files.size());
                for (const FileSnapshot& file : result.files) {
                    FileSnapshot cached;
                    cached.relativePath = file.relativePath;
                    cached.format = file.format;
                    cached.version = file.version;
                    cached.status = file.status;
                    cached.error = file.error;
                    cachedFiles.push_back(std::move(cached));
                }
                latestScan = std::move(result);
            }
        }
    }

    void WatcherLoop() {
        const fs::path watchRoot = [this] {
            std::lock_guard lock(mutex);
            return root;
        }();
        HANDLE handle = CreateFileW(
            watchRoot.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            debuglog::WriteError(
                "[notepad][txt] watcher open failed root=%ls error=%lu",
                watchRoot.c_str(),
                error);
            return;
        }
        {
            std::lock_guard lock(mutex);
            if (stopping) {
                CloseHandle(handle);
                return;
            }
            directoryHandle = handle;
            watcherAvailable = true;
        }

        std::vector<std::byte> buffer(64 * 1024);
        for (;;) {
            DWORD bytesReturned = 0;
            const BOOL success = ReadDirectoryChangesW(
                handle,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                TRUE,
                kWatcherFilter,
                &bytesReturned,
                nullptr,
                nullptr);
            if (!success) {
                const DWORD error = GetLastError();
                bool isStopping = false;
                {
                    std::lock_guard lock(mutex);
                    isStopping = stopping;
                    watcherAvailable = false;
                }
                if (!isStopping && error != ERROR_OPERATION_ABORTED) {
                    debuglog::WriteError(
                        "[notepad][txt] watcher read failed root=%ls error=%lu",
                        watchRoot.c_str(),
                        error);
                    NotifyDirectoryChange({}, true);
                }
                break;
            }
            if (bytesReturned == 0) {
                NotifyDirectoryChange({}, true);
                continue;
            }

            std::unordered_set<std::wstring> paths;
            bool all = false;
            bool relevant = false;
            DWORD offset = 0;
            for (;;) {
                const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data() + offset);
                std::wstring relative(info->FileName, info->FileNameLength / sizeof(wchar_t));
                const fs::path relativePath(relative);
                if (!relative.empty() && !IsExcludedTopLevelPath(relativePath)) {
                    relevant = true;
                    if (info->Action == FILE_ACTION_MODIFIED) {
                        paths.insert(RelativeKey(relativePath));
                    }
                }
                if (info->NextEntryOffset == 0) {
                    break;
                }
                offset += info->NextEntryOffset;
                if (offset >= bytesReturned) {
                    all = true;
                    break;
                }
            }
            if (relevant || all) {
                NotifyDirectoryChange(std::move(paths), all);
            }
        }
    }
};

SyncService::SyncService() : impl_(std::make_unique<Impl>()) {
}

SyncService::~SyncService() = default;

SyncService::SyncService(SyncService&&) noexcept = default;

SyncService& SyncService::operator=(SyncService&&) noexcept = default;

void SyncService::Start(
    std::filesystem::path root,
    std::uint64_t generation,
    std::vector<FileSnapshot> cachedFiles,
    bool forceReadAll) {
    impl_->Start(std::move(root), generation, std::move(cachedFiles), forceReadAll);
}

void SyncService::Stop() {
    impl_->Stop();
}

void SyncService::RequestFullScan() {
    impl_->RequestFullScan();
}

std::optional<ScanResult> SyncService::TakeLatestScan() {
    return impl_->TakeLatestScan();
}

bool SyncService::Running() const {
    return impl_->Running();
}

} // namespace notepadtxt
