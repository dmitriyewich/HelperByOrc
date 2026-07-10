#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

class LogoTextureLoader final {
public:
    static constexpr std::uint32_t kWidth = 768;
    static constexpr std::uint32_t kHeight = 512;
    static constexpr std::size_t kRowBytes = static_cast<std::size_t>(kWidth) * 4;
    static constexpr std::size_t kDecodedBytes = kRowBytes * kHeight;

    bool DecodeEmbeddedPng(HMODULE module, int resourceId);
    bool IsReady() const noexcept;
    const std::uint8_t* Pixels() const noexcept;
    std::size_t PixelBytes() const noexcept;
    void Reset() noexcept;

private:
    std::vector<std::uint8_t> pixels_{};
    std::atomic_bool ready_{ false };
};
