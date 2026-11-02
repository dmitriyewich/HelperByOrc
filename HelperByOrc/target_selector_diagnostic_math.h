#pragma once

#include <cstddef>
#include <cstdint>

namespace targetdiag {

inline constexpr std::uint64_t kSettleUs = 2000000;
inline constexpr std::uint64_t kMeasureUs = 12000000;
inline constexpr std::uint64_t kPassUs = kSettleUs + kMeasureUs;
inline constexpr unsigned int kPassCount = 4;

struct Phase {
    unsigned int pass = 0;
    bool measuring = false;
    bool complete = false;
};

constexpr Phase LocatePhase(std::uint64_t elapsedUs) noexcept {
    if (elapsedUs >= kPassUs * kPassCount) {
        return { kPassCount - 1, false, true };
    }
    return {
        static_cast<unsigned int>(elapsedUs / kPassUs),
        elapsedUs % kPassUs >= kSettleUs,
        false,
    };
}

constexpr unsigned int AlgorithmForPass(unsigned int pass, bool startWithRaycast = false) noexcept {
    return (pass & 1u) ^ (startWithRaycast ? 1u : 0u);
}

// Nearest-rank quantile of a sorted array. Empty arrays have no quantile.
constexpr std::size_t QuantileIndex(std::size_t count, unsigned int percentile) noexcept {
    if (count == 0) {
        return 0;
    }
    if (percentile > 100) {
        percentile = 100;
    }
    const std::size_t rank = (count / 100) * percentile
        + ((count % 100) * percentile + 99) / 100;
    return rank > 0 ? rank - 1 : 0;
}

} // namespace targetdiag
