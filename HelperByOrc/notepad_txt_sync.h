#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace notepadtxt {

constexpr std::uint64_t kMaximumFileBytes = 4ULL * 1024ULL * 1024ULL;

enum class TextEncoding {
    Utf8,
    Utf16Le,
    Utf16Be,
    Cp1251,
};

enum class NewlineStyle {
    Lf,
    CrLf,
    Cr,
};

enum class FileStatus {
    Ready,
    TooLarge,
    ReadError,
    DecodeError,
};

struct TextFormat {
    TextEncoding encoding = TextEncoding::Utf8;
    NewlineStyle newline = NewlineStyle::CrLf;
    bool bom = false;
};

struct FileVersion {
    std::uint64_t size = 0;
    std::uint64_t lastWriteTime = 0;
    std::string identity{};
    std::string contentHash{};
};

struct FileSnapshot {
    std::string relativePath{};
    std::string text{};
    TextFormat format{};
    FileVersion version{};
    FileStatus status = FileStatus::Ready;
    unsigned long error = 0;
    bool bodyReused = false;
};

struct ScanMetrics {
    std::uint64_t generation = 0;
    std::size_t entries = 0;
    std::size_t txtFiles = 0;
    std::size_t bodiesRead = 0;
    std::size_t bodiesReused = 0;
    std::uint64_t bytesRead = 0;
    double durationMs = 0.0;
    bool watcherAvailable = false;
    bool complete = true;
    unsigned long error = 0;
};

struct ScanResult {
    std::uint64_t generation = 0;
    std::vector<FileSnapshot> files{};
    ScanMetrics metrics{};
};

bool DecodeTextBytes(std::string_view bytes, std::string& utf8Text, TextFormat& format);
bool EncodeTextBytes(std::string_view utf8Text, const TextFormat& format, std::string& bytes);

const char* TextEncodingName(TextEncoding value);
TextEncoding ParseTextEncoding(std::string_view value);
const char* NewlineStyleName(NewlineStyle value);
NewlineStyle ParseNewlineStyle(std::string_view value);

class SyncService {
public:
    SyncService();
    ~SyncService();

    SyncService(const SyncService&) = delete;
    SyncService& operator=(const SyncService&) = delete;
    SyncService(SyncService&&) noexcept;
    SyncService& operator=(SyncService&&) noexcept;

    void Start(
        std::filesystem::path root,
        std::uint64_t generation,
        std::vector<FileSnapshot> cachedFiles,
        bool forceReadAll = false);
    void Stop();
    void RequestFullScan();
    std::optional<ScanResult> TakeLatestScan();
    bool Running() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace notepadtxt
