#include "notepad_storage.h"

#include "debug_log.h"
#include "notepad_builtin_instruction.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace notepadstorage {
namespace {

namespace fs = std::filesystem;

constexpr wchar_t kStateFileName[] = L"index.json";
constexpr std::uintmax_t kMaximumStateBytes = 32ULL * 1024ULL * 1024ULL;
constexpr std::chrono::milliseconds kWriterCoalesceWindow{ 200 };
constexpr std::chrono::seconds kFlushTimeout{ 5 };

std::optional<jsonutil::JsonObject> ReadState(
    const fs::path& path,
    std::string& persisted,
    unsigned long& error,
    std::string& message) {
    error = ERROR_SUCCESS;
    message.clear();

    std::error_code sizeError;
    const std::uintmax_t size = fs::file_size(path, sizeError);
    if (sizeError) {
        error = static_cast<unsigned long>(sizeError.value());
        message = "state file size is unavailable";
        return std::nullopt;
    }
    if (size > kMaximumStateBytes) {
        error = ERROR_FILE_TOO_LARGE;
        message = "state file exceeds the safety limit";
        return std::nullopt;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = ERROR_OPEN_FAILED;
        message = "state file could not be opened";
        return std::nullopt;
    }

    persisted.assign(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
    if (file.bad()) {
        error = ERROR_READ_FAULT;
        message = "state file could not be read";
        return std::nullopt;
    }

    std::string parseError;
    const std::optional<jsonutil::JsonValue> root = jsonutil::ParseJson(persisted, parseError);
    const jsonutil::JsonObject* object = root ? root->TryObject() : nullptr;
    if (!object) {
        error = ERROR_INVALID_DATA;
        message = parseError.empty() ? "state file is not a JSON object" : parseError;
        return std::nullopt;
    }
    return *object;
}

bool WriteTextAtomically(const fs::path& path, std::string_view text, unsigned long& error) {
    error = ERROR_SUCCESS;
    std::error_code directoryError;
    fs::create_directories(path.parent_path(), directoryError);
    if (directoryError) {
        error = static_cast<unsigned long>(directoryError.value());
        return false;
    }

    const fs::path temporary = path.parent_path()
        / (path.filename().wstring()
            + L".tmp."
            + std::to_wstring(GetCurrentProcessId())
            + L"."
            + std::to_wstring(GetTickCount64()));
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = ERROR_OPEN_FAILED;
            return false;
        }
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.flush();
        if (!file) {
            error = ERROR_WRITE_FAULT;
            file.close();
            std::error_code removeError;
            fs::remove(temporary, removeError);
            return false;
        }
    }

    if (!MoveFileExW(
            temporary.c_str(),
            path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = GetLastError();
        std::error_code removeError;
        fs::remove(temporary, removeError);
        return false;
    }
    return true;
}

bool FilesEqual(const fs::path& left, const fs::path& right, std::error_code& error) {
    error.clear();
    const std::uintmax_t leftSize = fs::file_size(left, error);
    if (error) {
        return false;
    }
    const std::uintmax_t rightSize = fs::file_size(right, error);
    if (error || leftSize != rightSize) {
        return false;
    }

    std::ifstream lhs(left, std::ios::binary);
    std::ifstream rhs(right, std::ios::binary);
    if (!lhs || !rhs) {
        error = std::make_error_code(std::errc::io_error);
        return false;
    }

    constexpr std::size_t kBufferBytes = 64 * 1024;
    std::vector<char> leftBuffer(kBufferBytes);
    std::vector<char> rightBuffer(kBufferBytes);
    while (lhs && rhs) {
        lhs.read(leftBuffer.data(), static_cast<std::streamsize>(leftBuffer.size()));
        rhs.read(rightBuffer.data(), static_cast<std::streamsize>(rightBuffer.size()));
        const std::streamsize leftRead = lhs.gcount();
        const std::streamsize rightRead = rhs.gcount();
        if (leftRead != rightRead
            || !std::equal(
                leftBuffer.data(),
                leftBuffer.data() + static_cast<std::size_t>(leftRead),
                rightBuffer.data())) {
            return false;
        }
    }
    if (lhs.bad() || rhs.bad()) {
        error = std::make_error_code(std::errc::io_error);
        return false;
    }
    return true;
}

bool IsReparsePoint(const fs::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

bool CopyLegacyTree(
    const fs::path& source,
    const fs::path& target,
    unsigned long& error,
    std::string& message) {
    error = ERROR_SUCCESS;
    message.clear();

    std::error_code existsError;
    if (!fs::exists(source, existsError)) {
        if (existsError) {
            error = static_cast<unsigned long>(existsError.value());
            message = "legacy directory is unavailable";
            return false;
        }
        return true;
    }
    if (!fs::is_directory(source, existsError) || existsError || IsReparsePoint(source)) {
        error = existsError
            ? static_cast<unsigned long>(existsError.value())
            : ERROR_REPARSE_TAG_INVALID;
        message = "legacy directory is not a regular directory";
        return false;
    }

    std::vector<fs::path> createdFiles;
    std::error_code iteratorError;
    fs::recursive_directory_iterator iterator(
        source,
        fs::directory_options::skip_permission_denied,
        iteratorError);
    const fs::recursive_directory_iterator end;
    while (iterator != end && !iteratorError) {
        const fs::directory_entry& entry = *iterator;
        if (IsReparsePoint(entry.path())) {
            if (entry.is_directory(iteratorError)) {
                iterator.disable_recursion_pending();
            }
            iterator.increment(iteratorError);
            continue;
        }

        std::error_code relativeError;
        const fs::path relative = fs::relative(entry.path(), source, relativeError);
        if (relativeError || relative.empty()) {
            error = static_cast<unsigned long>(
                relativeError ? relativeError.value() : ERROR_INVALID_NAME);
            message = "legacy relative path could not be resolved";
            break;
        }
        const fs::path destination = target / relative;
        std::error_code operationError;
        if (entry.is_directory(operationError)) {
            fs::create_directories(destination, operationError);
        } else if (entry.is_regular_file(operationError)) {
            if (!relative.has_parent_path()
                && notepadbuiltin::IsInstructionSource(relative.wstring())) {
                iterator.increment(iteratorError);
                continue;
            }
            fs::create_directories(destination.parent_path(), operationError);
            if (!operationError) {
                const bool destinationExists = fs::exists(destination, operationError);
                if (!operationError && destinationExists) {
                    if (!FilesEqual(entry.path(), destination, operationError)) {
                        if (!operationError) {
                            operationError = std::make_error_code(std::errc::file_exists);
                        }
                    }
                } else if (!operationError) {
                    fs::copy_file(entry.path(), destination, fs::copy_options::none, operationError);
                    if (!operationError) {
                        createdFiles.push_back(destination);
                    }
                }
            }
        }
        if (operationError) {
            error = static_cast<unsigned long>(operationError.value());
            message = "legacy files conflict with global notepad";
            break;
        }
        iterator.increment(iteratorError);
    }
    if (iteratorError && error == ERROR_SUCCESS) {
        error = static_cast<unsigned long>(iteratorError.value());
        message = "legacy directory enumeration failed";
    }
    if (error == ERROR_SUCCESS) {
        return true;
    }

    for (auto it = createdFiles.rbegin(); it != createdFiles.rend(); ++it) {
        std::error_code removeError;
        fs::remove(*it, removeError);
    }
    return false;
}

} // namespace

struct Store::Impl {
    fs::path root{};
    fs::path statePath{};
    bool global = false;
    std::string persisted{};

    std::mutex mutex{};
    std::condition_variable cv{};
    std::condition_variable idleCv{};
    std::thread writer{};
    bool stop = false;
    bool busy = false;
    bool flushRequested = false;
    bool hasPending = false;
    bool writeFailed = false;
    unsigned long lastWriteError = ERROR_SUCCESS;
    std::uint64_t revision = 0;
    jsonutil::JsonValue pending{};

    void StartWriter() {
        if (writer.joinable()) {
            return;
        }
        stop = false;
        writer = std::thread([this] { WriterLoop(); });
    }

    void WriterLoop() {
        for (;;) {
            jsonutil::JsonValue state;
            std::uint64_t writeRevision = 0;
            {
                std::unique_lock lock(mutex);
                cv.wait(lock, [&] { return stop || hasPending; });
                if (stop && !hasPending) {
                    break;
                }

                std::uint64_t observedRevision = revision;
                while (!stop && !flushRequested) {
                    const bool changed = cv.wait_for(
                        lock,
                        kWriterCoalesceWindow,
                        [&] { return stop || flushRequested || revision != observedRevision; });
                    if (!changed) {
                        break;
                    }
                    observedRevision = revision;
                }

                state = std::move(pending);
                writeRevision = revision;
                hasPending = false;
                busy = true;
                flushRequested = false;
            }

            std::string output;
            jsonutil::WriteJson(state, output, 0);
            bool saved = true;
            bool skipped = output == persisted;
            unsigned long error = ERROR_SUCCESS;
            if (output.size() > kMaximumStateBytes) {
                saved = false;
                skipped = false;
                error = ERROR_FILE_TOO_LARGE;
            } else if (!skipped) {
                saved = WriteTextAtomically(statePath, output, error);
                if (saved) {
                    persisted = output;
                }
            }

            if (saved) {
                debuglog::WriteInfo(
                    skipped
                        ? "[notepad][store] snapshot skipped unchanged rev=%llu bytes=%zu"
                        : "[notepad][store] snapshot saved rev=%llu bytes=%zu",
                    static_cast<unsigned long long>(writeRevision),
                    output.size());
            } else {
                debuglog::WriteError(
                    "[notepad][store] snapshot write failed rev=%llu error=%lu",
                    static_cast<unsigned long long>(writeRevision),
                    error);
            }

            {
                std::lock_guard lock(mutex);
                busy = false;
                writeFailed = !saved;
                if (!saved) {
                    lastWriteError = error;
                }
                if (!hasPending) {
                    idleCv.notify_all();
                }
            }
        }
    }
};

Store::Store() : impl_(std::make_unique<Impl>()) {
}

Store::~Store() {
    Stop();
}

OpenResult Store::Open(
    std::filesystem::path globalRoot,
    std::filesystem::path legacyRoot,
    const jsonutil::JsonObject& legacyState) {
    Stop();
    impl_->root = std::move(globalRoot);
    impl_->statePath = impl_->root / kStateFileName;
    impl_->global = false;
    impl_->persisted.clear();
    impl_->stop = false;
    impl_->busy = false;
    impl_->flushRequested = false;
    impl_->hasPending = false;
    impl_->writeFailed = false;
    impl_->lastWriteError = ERROR_SUCCESS;
    impl_->revision = 0;
    impl_->pending = {};

    OpenResult result;
    std::error_code existsError;
    const bool rootExists = fs::exists(impl_->root, existsError);
    if (existsError || (rootExists && IsReparsePoint(impl_->root))) {
        result.error = existsError
            ? static_cast<unsigned long>(existsError.value())
            : ERROR_REPARSE_TAG_INVALID;
        result.message = "global notepad root is unavailable or redirected";
        result.state = legacyState;
        return result;
    }
    existsError.clear();
    const bool stateExists = fs::exists(impl_->statePath, existsError);
    if (existsError || (stateExists && IsReparsePoint(impl_->statePath))) {
        result.error = static_cast<unsigned long>(existsError.value());
        if (result.error == ERROR_SUCCESS) {
            result.error = ERROR_REPARSE_TAG_INVALID;
        }
        result.message = "global state path is unavailable or redirected";
        result.state = legacyState;
        return result;
    }

    if (stateExists) {
        std::optional<jsonutil::JsonObject> state = ReadState(
            impl_->statePath,
            impl_->persisted,
            result.error,
            result.message);
        if (!state) {
            result.state = legacyState;
            debuglog::WriteError(
                "[notepad][store] global state rejected path=%ls error=%lu reason=%s; using legacy profile data",
                impl_->statePath.c_str(),
                result.error,
                result.message.c_str());
            return result;
        }
        result.state = std::move(*state);
        result.global = true;
        impl_->global = true;
        impl_->StartWriter();
        debuglog::WriteInfo("[notepad][store] global state loaded path=%ls", impl_->statePath.c_str());
        return result;
    }

    std::error_code directoryError;
    fs::create_directories(impl_->root, directoryError);
    if (directoryError) {
        result.error = static_cast<unsigned long>(directoryError.value());
        result.message = "global notepad directory could not be created";
        result.state = legacyState;
        return result;
    }

    if (!CopyLegacyTree(legacyRoot, impl_->root, result.error, result.message)) {
        result.state = legacyState;
        debuglog::WriteError(
            "[notepad][store] legacy migration blocked source=%ls target=%ls error=%lu reason=%s",
            legacyRoot.c_str(),
            impl_->root.c_str(),
            result.error,
            result.message.c_str());
        return result;
    }

    jsonutil::WriteJson(jsonutil::JsonValue(legacyState), impl_->persisted, 0);
    if (impl_->persisted.size() > kMaximumStateBytes) {
        result.error = ERROR_FILE_TOO_LARGE;
        result.message = "legacy notepad state exceeds the safety limit";
        result.state = legacyState;
        debuglog::WriteError(
            "[notepad][store] migration state rejected path=%ls bytes=%zu limit=%llu",
            impl_->statePath.c_str(),
            impl_->persisted.size(),
            static_cast<unsigned long long>(kMaximumStateBytes));
        return result;
    }
    if (!WriteTextAtomically(impl_->statePath, impl_->persisted, result.error)) {
        result.message = "global state could not be initialized";
        result.state = legacyState;
        debuglog::WriteError(
            "[notepad][store] migration state write failed path=%ls error=%lu",
            impl_->statePath.c_str(),
            result.error);
        return result;
    }

    result.state = legacyState;
    result.global = true;
    result.migrated = true;
    impl_->global = true;
    impl_->StartWriter();
    debuglog::WriteInfo(
        "[notepad][store] legacy data migrated source=%ls target=%ls",
        legacyRoot.c_str(),
        impl_->root.c_str());
    return result;
}

void Store::QueueSave(jsonutil::JsonValue state) {
    if (!impl_->global) {
        return;
    }
    {
        std::lock_guard lock(impl_->mutex);
        impl_->pending = std::move(state);
        impl_->hasPending = true;
        impl_->writeFailed = false;
        ++impl_->revision;
    }
    impl_->cv.notify_one();
}

bool Store::Flush() {
    if (!impl_->writer.joinable()) {
        return true;
    }
    std::unique_lock lock(impl_->mutex);
    impl_->flushRequested = true;
    impl_->cv.notify_one();
    const bool completed = impl_->idleCv.wait_for(
        lock,
        kFlushTimeout,
        [&] { return !impl_->hasPending && !impl_->busy; });
    return completed && !impl_->writeFailed;
}

void Store::Stop() {
    if (!impl_->writer.joinable()) {
        impl_->global = false;
        return;
    }
    if (!Flush()) {
        debuglog::WriteError("[notepad][store] flush timed out during stop");
    }
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stop = true;
    }
    impl_->cv.notify_one();
    impl_->writer.join();
    impl_->global = false;
}

bool Store::IsGlobal() const {
    return impl_->global;
}

unsigned long Store::TakeLastError() {
    std::lock_guard lock(impl_->mutex);
    return std::exchange(impl_->lastWriteError, ERROR_SUCCESS);
}

const std::filesystem::path& Store::RootDirectory() const {
    return impl_->root;
}

const std::filesystem::path& Store::StatePath() const {
    return impl_->statePath;
}

} // namespace notepadstorage
