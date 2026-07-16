#include "notepad_txt_operations.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "debug_log.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <utility>

namespace notepadtxt {
namespace {

namespace fs = std::filesystem;

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

bool EqualsOrdinalInsensitive(std::wstring_view lhs, std::wstring_view rhs) {
    return CompareStringOrdinal(
               lhs.data(),
               static_cast<int>(lhs.size()),
               rhs.data(),
               static_cast<int>(rhs.size()),
               TRUE)
        == CSTR_EQUAL;
}

bool IsExcludedTopLevelPath(const fs::path& relative) {
    const auto first = relative.begin();
    if (first == relative.end()) {
        return false;
    }
    const std::wstring name = first->wstring();
    return EqualsOrdinalInsensitive(name, L"images") || EqualsOrdinalInsensitive(name, L"export");
}

bool IsReservedWindowsName(std::wstring_view component) {
    const std::size_t dot = component.find(L'.');
    const std::wstring_view stem = component.substr(0, dot);
    if (EqualsOrdinalInsensitive(stem, L"CON")
        || EqualsOrdinalInsensitive(stem, L"PRN")
        || EqualsOrdinalInsensitive(stem, L"AUX")
        || EqualsOrdinalInsensitive(stem, L"NUL")
        || EqualsOrdinalInsensitive(stem, L"CLOCK$")) {
        return true;
    }
    if (stem.size() == 4
        && (EqualsOrdinalInsensitive(stem.substr(0, 3), L"COM")
            || EqualsOrdinalInsensitive(stem.substr(0, 3), L"LPT"))
        && stem[3] >= L'1'
        && stem[3] <= L'9') {
        return true;
    }
    return false;
}

bool IsSafeWindowsComponent(std::wstring_view component) {
    if (component.empty() || component == L"." || component == L".."
        || component.back() == L' ' || component.back() == L'.'
        || IsReservedWindowsName(component)) {
        return false;
    }
    for (const wchar_t ch : component) {
        if (ch < 32 || ch == L'<' || ch == L'>' || ch == L':' || ch == L'"'
            || ch == L'/' || ch == L'\\' || ch == L'|' || ch == L'?' || ch == L'*') {
            return false;
        }
    }
    return true;
}

bool IsSafeRelativeTxtPath(std::string_view relativeUtf8, fs::path& relative) {
    const std::wstring wide = Utf8ToWide(relativeUtf8);
    if (wide.empty() || relativeUtf8.empty()) {
        return false;
    }
    const fs::path rawRelative(wide);
    for (const fs::path& component : rawRelative) {
        if (!IsSafeWindowsComponent(component.wstring())) {
            return false;
        }
    }
    relative = rawRelative.lexically_normal();
    if (relative.empty() || relative.is_absolute() || relative.has_root_path()
        || !EqualsOrdinalInsensitive(relative.extension().wstring(), L".txt")
        || IsExcludedTopLevelPath(relative)) {
        return false;
    }
    return true;
}

bool HasReparsePoint(const fs::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

bool ResolveSafeTxtPath(
    const fs::path& root,
    std::string_view relativeUtf8,
    fs::path& relative,
    fs::path& absolute,
    unsigned long& error) {
    if (!IsSafeRelativeTxtPath(relativeUtf8, relative)) {
        error = ERROR_INVALID_NAME;
        return false;
    }
    absolute = (root / relative).lexically_normal();
    fs::path current = root;
    auto component = relative.begin();
    const auto end = relative.end();
    for (; component != end; ++component) {
        current /= *component;
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD attributeError = GetLastError();
            if (attributeError == ERROR_FILE_NOT_FOUND || attributeError == ERROR_PATH_NOT_FOUND) {
                break;
            }
            error = attributeError;
            return false;
        }
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            error = ERROR_REPARSE_TAG_INVALID;
            return false;
        }
    }
    error = ERROR_SUCCESS;
    return true;
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

bool ReadAll(HANDLE file, std::uint64_t size, std::string& bytes, unsigned long& error) {
    if (size > kMaximumFileBytes || size > static_cast<std::uint64_t>(SIZE_MAX)) {
        error = ERROR_FILE_TOO_LARGE;
        return false;
    }
    bytes.assign(static_cast<std::size_t>(size), '\0');
    LARGE_INTEGER start{};
    if (!SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) {
        error = GetLastError();
        return false;
    }
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD read = 0;
        const DWORD remaining = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, MAXDWORD));
        if (!ReadFile(file, bytes.data() + offset, remaining, &read, nullptr)) {
            error = GetLastError();
            return false;
        }
        if (read == 0) {
            error = ERROR_HANDLE_EOF;
            return false;
        }
        offset += read;
    }
    return true;
}

bool ReadSnapshot(const fs::path& absolute, std::string relativePath, FileSnapshot& snapshot, unsigned long& error) {
    snapshot = {};
    snapshot.relativePath = std::move(relativePath);
    HANDLE file = CreateFileW(
        absolute.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        snapshot.status = FileStatus::ReadError;
        snapshot.error = error;
        return false;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file, &info)) {
        error = GetLastError();
        snapshot.status = FileStatus::ReadError;
        snapshot.error = error;
        CloseHandle(file);
        return false;
    }
    snapshot.version.size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    snapshot.version.lastWriteTime = FileTimeToUint64(info.ftLastWriteTime);
    snapshot.version.identity = FileIdentity(info);
    std::string bytes;
    if (!ReadAll(file, snapshot.version.size, bytes, error)) {
        snapshot.status = error == ERROR_FILE_TOO_LARGE ? FileStatus::TooLarge : FileStatus::ReadError;
        snapshot.error = error;
        CloseHandle(file);
        return false;
    }
    CloseHandle(file);
    snapshot.version.contentHash = HashBytes(bytes);
    if (!DecodeTextBytes(bytes, snapshot.text, snapshot.format)) {
        error = ERROR_NO_UNICODE_TRANSLATION;
        snapshot.status = FileStatus::DecodeError;
        snapshot.error = error;
        return false;
    }
    snapshot.status = FileStatus::Ready;
    error = ERROR_SUCCESS;
    return true;
}

bool SameExpectedVersion(const FileVersion& expected, const FileVersion& current) {
    if (!expected.identity.empty() && expected.identity != current.identity) {
        return false;
    }
    if (expected.size != current.size || expected.lastWriteTime != current.lastWriteTime) {
        return false;
    }
    return expected.contentHash.empty() || expected.contentHash == current.contentHash;
}

bool EnsureTargetDirectory(const fs::path& root, const fs::path& target, unsigned long& error) {
    std::error_code createError;
    fs::create_directories(target.parent_path(), createError);
    if (createError) {
        error = static_cast<unsigned long>(createError.value());
        return false;
    }
    fs::path relative = fs::relative(target.parent_path(), root, createError);
    if (createError) {
        error = static_cast<unsigned long>(createError.value());
        return false;
    }
    fs::path current = root;
    for (const fs::path& component : relative) {
        current /= component;
        if (HasReparsePoint(current)) {
            error = ERROR_REPARSE_TAG_INVALID;
            return false;
        }
    }
    return true;
}

bool WriteAll(HANDLE file, std::string_view bytes, unsigned long& error) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        const DWORD remaining = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, MAXDWORD));
        if (!WriteFile(file, bytes.data() + offset, remaining, &written, nullptr)) {
            error = GetLastError();
            return false;
        }
        if (written == 0) {
            error = ERROR_WRITE_FAULT;
            return false;
        }
        offset += written;
    }
    return true;
}

fs::path TemporarySibling(const fs::path& target, std::uint64_t requestId, std::size_t index, const wchar_t* suffix) {
    return target.parent_path()
        / (target.filename().wstring() + L".hbo-" + std::to_wstring(GetCurrentProcessId()) + L"-"
            + std::to_wstring(requestId) + L"-" + std::to_wstring(index) + suffix);
}

bool MovePathSafely(
    const fs::path& source,
    const fs::path& target,
    std::uint64_t requestId,
    std::size_t index,
    unsigned long& error) {
    const std::wstring sourcePath = source.lexically_normal().wstring();
    const std::wstring targetPath = target.lexically_normal().wstring();
    if (!EqualsOrdinalInsensitive(sourcePath, targetPath) || sourcePath == targetPath) {
        if (sourcePath == targetPath || MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
            error = ERROR_SUCCESS;
            return true;
        }
        error = GetLastError();
        return false;
    }

    const fs::path temporary = TemporarySibling(source, requestId, index, L".case");
    if (!MoveFileExW(source.c_str(), temporary.c_str(), MOVEFILE_WRITE_THROUGH)) {
        error = GetLastError();
        return false;
    }
    if (MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
        error = ERROR_SUCCESS;
        return true;
    }
    error = GetLastError();
    MoveFileExW(temporary.c_str(), source.c_str(), MOVEFILE_WRITE_THROUGH);
    return false;
}

struct WriteRequest {
    std::string token{};
    std::string relativePath{};
    std::string text{};
    TextFormat format{};
    FileVersion expectedVersion{};
    bool allowCreate = false;
};

struct Request {
    std::uint64_t id = 0;
    std::uint64_t generation = 0;
    OperationKind kind = OperationKind::Write;
    std::optional<WriteRequest> write{};
    std::vector<MoveItem> moves{};
    std::vector<DeleteItem> deletes{};
};

OperationResult ProcessWrite(const fs::path& root, const Request& request) {
    OperationResult result{ request.id, request.generation, OperationKind::Write, false, {} };
    const WriteRequest& write = *request.write;
    OperationItemResult item;
    item.token = write.token;
    item.sourceRelativePath = write.relativePath;
    item.targetRelativePath = write.relativePath;
    fs::path relative;
    fs::path target;
    if (!ResolveSafeTxtPath(root, write.relativePath, relative, target, item.error)) {
        result.items.push_back(std::move(item));
        return result;
    }

    const DWORD attributes = GetFileAttributesW(target.c_str());
    const bool exists = attributes != INVALID_FILE_ATTRIBUTES;
    const DWORD attributeError = exists ? ERROR_SUCCESS : GetLastError();
    if (exists) {
        FileSnapshot current;
        if (!ReadSnapshot(target, write.relativePath, current, item.error)) {
            result.items.push_back(std::move(item));
            return result;
        }
        if (!SameExpectedVersion(write.expectedVersion, current.version)) {
            item.conflict = true;
            item.error = ERROR_REVISION_MISMATCH;
            item.snapshot = std::move(current);
            result.items.push_back(std::move(item));
            return result;
        }
        item.snapshot = std::move(current);
    } else if (!write.allowCreate || (attributeError != ERROR_FILE_NOT_FOUND && attributeError != ERROR_PATH_NOT_FOUND)) {
        item.error = attributeError;
        result.items.push_back(std::move(item));
        return result;
    }

    std::string bytes;
    if (!EncodeTextBytes(write.text, write.format, bytes)) {
        item.error = ERROR_NO_UNICODE_TRANSLATION;
        result.items.push_back(std::move(item));
        return result;
    }
    if (bytes.size() > kMaximumFileBytes) {
        item.error = ERROR_FILE_TOO_LARGE;
        result.items.push_back(std::move(item));
        return result;
    }
    if (!EnsureTargetDirectory(root, target, item.error)) {
        result.items.push_back(std::move(item));
        return result;
    }
    const fs::path temporary = TemporarySibling(target, request.id, 0, L".tmp");
    HANDLE file = CreateFileW(
        temporary.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        item.error = GetLastError();
        result.items.push_back(std::move(item));
        return result;
    }
    bool writeSucceeded = WriteAll(file, bytes, item.error) && FlushFileBuffers(file);
    if (!writeSucceeded && item.error == ERROR_SUCCESS) {
        item.error = GetLastError();
    }
    CloseHandle(file);
    if (!writeSucceeded) {
        DeleteFileW(temporary.c_str());
        result.items.push_back(std::move(item));
        return result;
    }
    const DWORD moveFlags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH;
    if (!MoveFileExW(temporary.c_str(), target.c_str(), moveFlags)) {
        item.error = GetLastError();
        DeleteFileW(temporary.c_str());
        result.items.push_back(std::move(item));
        return result;
    }
    if (!ReadSnapshot(target, write.relativePath, item.snapshot, item.error)) {
        result.items.push_back(std::move(item));
        return result;
    }
    item.success = true;
    result.success = true;
    result.items.push_back(std::move(item));
    return result;
}

OperationResult ProcessMove(const fs::path& root, const Request& request) {
    OperationResult result{ request.id, request.generation, OperationKind::Move, false, {} };
    struct PreparedMove {
        MoveItem input{};
        fs::path source{};
        fs::path target{};
    };
    std::vector<PreparedMove> prepared;
    prepared.reserve(request.moves.size());
    std::set<std::wstring> targetKeys;
    for (const MoveItem& move : request.moves) {
        OperationItemResult item;
        item.token = move.token;
        item.sourceRelativePath = move.sourceRelativePath;
        item.targetRelativePath = move.targetRelativePath;
        fs::path sourceRelative;
        fs::path targetRelative;
        fs::path source;
        fs::path target;
        if (!ResolveSafeTxtPath(root, move.sourceRelativePath, sourceRelative, source, item.error)
            || !ResolveSafeTxtPath(root, move.targetRelativePath, targetRelative, target, item.error)) {
            result.items.push_back(std::move(item));
            return result;
        }
        FileSnapshot current;
        if (!ReadSnapshot(source, move.sourceRelativePath, current, item.error)
            || !SameExpectedVersion(move.expectedVersion, current.version)) {
            item.conflict = item.error == ERROR_SUCCESS;
            if (item.conflict) {
                item.error = ERROR_REVISION_MISMATCH;
                item.snapshot = std::move(current);
            }
            result.items.push_back(std::move(item));
            return result;
        }
        std::wstring targetKey = target.lexically_normal().wstring();
        std::transform(targetKey.begin(), targetKey.end(), targetKey.begin(), towlower);
        if (!targetKeys.insert(targetKey).second) {
            item.error = ERROR_ALREADY_EXISTS;
            result.items.push_back(std::move(item));
            return result;
        }
        const DWORD targetAttributes = GetFileAttributesW(target.c_str());
        if (targetAttributes != INVALID_FILE_ATTRIBUTES
            && !EqualsOrdinalInsensitive(source.wstring(), target.wstring())) {
            item.error = ERROR_ALREADY_EXISTS;
            result.items.push_back(std::move(item));
            return result;
        }
        if (!EnsureTargetDirectory(root, target, item.error)) {
            result.items.push_back(std::move(item));
            return result;
        }
        prepared.push_back({ move, source, target });
    }

    std::size_t moved = 0;
    for (; moved < prepared.size(); ++moved) {
        unsigned long error = ERROR_SUCCESS;
        if (!MovePathSafely(
                prepared[moved].source,
                prepared[moved].target,
                request.id,
                moved,
                error)) {
            for (std::size_t rollback = moved; rollback > 0; --rollback) {
                unsigned long rollbackError = ERROR_SUCCESS;
                MovePathSafely(
                    prepared[rollback - 1].target,
                    prepared[rollback - 1].source,
                    request.id,
                    prepared.size() + rollback,
                    rollbackError);
            }
            OperationItemResult item;
            item.token = prepared[moved].input.token;
            item.sourceRelativePath = prepared[moved].input.sourceRelativePath;
            item.targetRelativePath = prepared[moved].input.targetRelativePath;
            item.error = error;
            result.items.push_back(std::move(item));
            return result;
        }
    }

    result.success = true;
    for (const PreparedMove& move : prepared) {
        OperationItemResult item;
        item.token = move.input.token;
        item.sourceRelativePath = move.input.sourceRelativePath;
        item.targetRelativePath = move.input.targetRelativePath;
        if (!ReadSnapshot(move.target, move.input.targetRelativePath, item.snapshot, item.error)) {
            result.success = false;
        } else {
            item.success = true;
        }
        result.items.push_back(std::move(item));
    }
    return result;
}

OperationResult ProcessDelete(const fs::path& root, const Request& request) {
    OperationResult result{ request.id, request.generation, OperationKind::Delete, false, {} };
    struct PreparedDelete {
        DeleteItem input{};
        fs::path source{};
        fs::path temporary{};
    };
    std::vector<PreparedDelete> prepared;
    prepared.reserve(request.deletes.size());
    for (std::size_t index = 0; index < request.deletes.size(); ++index) {
        const DeleteItem& remove = request.deletes[index];
        OperationItemResult item;
        item.token = remove.token;
        item.sourceRelativePath = remove.relativePath;
        fs::path relative;
        fs::path source;
        if (!ResolveSafeTxtPath(root, remove.relativePath, relative, source, item.error)) {
            result.items.push_back(std::move(item));
            return result;
        }
        FileSnapshot current;
        if (!ReadSnapshot(source, remove.relativePath, current, item.error)
            || !SameExpectedVersion(remove.expectedVersion, current.version)) {
            item.conflict = item.error == ERROR_SUCCESS;
            if (item.conflict) {
                item.error = ERROR_REVISION_MISMATCH;
                item.snapshot = std::move(current);
            }
            result.items.push_back(std::move(item));
            return result;
        }
        prepared.push_back({ remove, source, TemporarySibling(source, request.id, index, L".delete") });
    }

    std::size_t renamed = 0;
    for (; renamed < prepared.size(); ++renamed) {
        if (!MoveFileExW(prepared[renamed].source.c_str(), prepared[renamed].temporary.c_str(), MOVEFILE_WRITE_THROUGH)) {
            const DWORD error = GetLastError();
            for (std::size_t rollback = renamed; rollback > 0; --rollback) {
                MoveFileExW(
                    prepared[rollback - 1].temporary.c_str(),
                    prepared[rollback - 1].source.c_str(),
                    MOVEFILE_WRITE_THROUGH);
            }
            OperationItemResult item;
            item.token = prepared[renamed].input.token;
            item.sourceRelativePath = prepared[renamed].input.relativePath;
            item.error = error;
            result.items.push_back(std::move(item));
            return result;
        }
    }

    result.success = true;
    for (const PreparedDelete& remove : prepared) {
        OperationItemResult item;
        item.token = remove.input.token;
        item.sourceRelativePath = remove.input.relativePath;
        if (!DeleteFileW(remove.temporary.c_str())) {
            item.error = GetLastError();
            MoveFileExW(remove.temporary.c_str(), remove.source.c_str(), MOVEFILE_WRITE_THROUGH);
            result.success = false;
        } else {
            item.success = true;
        }
        result.items.push_back(std::move(item));
    }
    return result;
}

OperationResult ProcessRequest(const fs::path& root, const Request& request) {
    switch (request.kind) {
    case OperationKind::Move:
        return ProcessMove(root, request);
    case OperationKind::Delete:
        return ProcessDelete(root, request);
    case OperationKind::Write:
    default:
        return ProcessWrite(root, request);
    }
}

} // namespace

struct OperationService::Impl {
    mutable std::mutex mutex{};
    std::condition_variable wake{};
    fs::path root{};
    std::uint64_t generation = 0;
    std::uint64_t nextRequestId = 0;
    std::deque<Request> requests{};
    std::vector<OperationResult> results{};
    std::thread worker{};
    bool stopping = false;
    bool running = false;
    bool inFlight = false;

    ~Impl() {
        Stop();
    }

    void Start(fs::path newRoot, std::uint64_t newGeneration) {
        Stop();
        {
            std::lock_guard lock(mutex);
            root = std::move(newRoot);
            generation = newGeneration;
            requests.clear();
            results.clear();
            stopping = false;
            running = true;
            inFlight = false;
        }
        worker = std::thread([this] { WorkerLoop(); });
    }

    void Stop() {
        {
            std::lock_guard lock(mutex);
            if (!running && !worker.joinable()) {
                return;
            }
            stopping = true;
        }
        wake.notify_all();
        if (worker.joinable()) {
            worker.join();
        }
        std::lock_guard lock(mutex);
        running = false;
        stopping = false;
        inFlight = false;
        requests.clear();
    }

    std::uint64_t Queue(Request request) {
        std::lock_guard lock(mutex);
        if (!running || stopping) {
            return 0;
        }
        request.id = ++nextRequestId;
        request.generation = generation;
        const std::uint64_t id = request.id;
        requests.push_back(std::move(request));
        wake.notify_all();
        return id;
    }

    bool Flush(unsigned long timeoutMs) {
        std::unique_lock lock(mutex);
        return wake.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] {
            return requests.empty() && !inFlight;
        });
    }

    std::vector<OperationResult> TakeResults() {
        std::lock_guard lock(mutex);
        std::vector<OperationResult> ready = std::move(results);
        results.clear();
        return ready;
    }

    bool Busy() const {
        std::lock_guard lock(mutex);
        return inFlight || !requests.empty();
    }

    void WorkerLoop() {
        for (;;) {
            Request request;
            fs::path operationRoot;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stopping || !requests.empty(); });
                if (stopping && requests.empty()) {
                    return;
                }
                request = std::move(requests.front());
                requests.pop_front();
                operationRoot = root;
                inFlight = true;
            }
            OperationResult result = ProcessRequest(operationRoot, request);
            debuglog::WriteInfo(
                "[notepad][txt] operation id=%llu generation=%llu kind=%d success=%d items=%zu",
                static_cast<unsigned long long>(result.requestId),
                static_cast<unsigned long long>(result.generation),
                static_cast<int>(result.kind),
                result.success ? 1 : 0,
                result.items.size());
            {
                std::lock_guard lock(mutex);
                results.push_back(std::move(result));
                inFlight = false;
            }
            wake.notify_all();
        }
    }
};

OperationService::OperationService() : impl_(std::make_unique<Impl>()) {
}

OperationService::~OperationService() = default;

OperationService::OperationService(OperationService&&) noexcept = default;

OperationService& OperationService::operator=(OperationService&&) noexcept = default;

void OperationService::Start(std::filesystem::path root, std::uint64_t generation) {
    impl_->Start(std::move(root), generation);
}

void OperationService::Stop() {
    impl_->Stop();
}

std::uint64_t OperationService::QueueWrite(
    std::string token,
    std::string relativePath,
    std::string text,
    TextFormat format,
    FileVersion expectedVersion,
    bool allowCreate) {
    Request request;
    request.kind = OperationKind::Write;
    request.write = WriteRequest{
        std::move(token),
        std::move(relativePath),
        std::move(text),
        format,
        std::move(expectedVersion),
        allowCreate,
    };
    return impl_->Queue(std::move(request));
}

std::uint64_t OperationService::QueueMove(std::vector<MoveItem> items) {
    Request request;
    request.kind = OperationKind::Move;
    request.moves = std::move(items);
    return impl_->Queue(std::move(request));
}

std::uint64_t OperationService::QueueDelete(std::vector<DeleteItem> items) {
    Request request;
    request.kind = OperationKind::Delete;
    request.deletes = std::move(items);
    return impl_->Queue(std::move(request));
}

bool OperationService::Flush(unsigned long timeoutMs) {
    return impl_->Flush(timeoutMs);
}

std::vector<OperationResult> OperationService::TakeResults() {
    return impl_->TakeResults();
}

bool OperationService::Busy() const {
    return impl_->Busy();
}

} // namespace notepadtxt
