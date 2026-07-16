#pragma once

#include "notepad_txt_sync.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace notepadtxt {

enum class OperationKind {
    Write,
    Move,
    Delete,
};

struct MoveItem {
    std::string token{};
    std::string sourceRelativePath{};
    std::string targetRelativePath{};
    FileVersion expectedVersion{};
};

struct DeleteItem {
    std::string token{};
    std::string relativePath{};
    FileVersion expectedVersion{};
};

struct OperationItemResult {
    std::string token{};
    std::string sourceRelativePath{};
    std::string targetRelativePath{};
    bool success = false;
    bool conflict = false;
    unsigned long error = 0;
    FileSnapshot snapshot{};
};

struct OperationResult {
    std::uint64_t requestId = 0;
    std::uint64_t generation = 0;
    OperationKind kind = OperationKind::Write;
    bool success = false;
    std::vector<OperationItemResult> items{};
};

class OperationService {
public:
    OperationService();
    ~OperationService();

    OperationService(const OperationService&) = delete;
    OperationService& operator=(const OperationService&) = delete;
    OperationService(OperationService&&) noexcept;
    OperationService& operator=(OperationService&&) noexcept;

    void Start(std::filesystem::path root, std::uint64_t generation);
    void Stop();

    std::uint64_t QueueWrite(
        std::string token,
        std::string relativePath,
        std::string text,
        TextFormat format,
        FileVersion expectedVersion,
        bool allowCreate);
    std::uint64_t QueueMove(std::vector<MoveItem> items);
    std::uint64_t QueueDelete(std::vector<DeleteItem> items);

    bool Flush(unsigned long timeoutMs = 5000);
    std::vector<OperationResult> TakeResults();
    bool Busy() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace notepadtxt
