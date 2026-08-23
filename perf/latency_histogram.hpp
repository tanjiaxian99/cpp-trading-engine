#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

class LatencyHistogram {
public:
    void Record(std::uint64_t nanos);

    [[nodiscard]] std::uint64_t Percentile(double p) const;
    [[nodiscard]] std::uint64_t Max() const {
        return max_;
    }
    [[nodiscard]] std::uint64_t Count() const {
        return total_count_;
    }

private:
    static constexpr int kSubBucketBits = 6;
    static constexpr std::uint64_t kSubBucketCount = 1ULL << kSubBucketBits;
    static constexpr int kNumOctaves = 28;  // The last octave covers 8.6s - 17.2s
    static constexpr std::size_t kNumBuckets =
        kSubBucketCount + static_cast<std::size_t>(kNumOctaves) * kSubBucketCount;

    [[nodiscard]] static std::size_t BucketIndex(std::uint64_t nanos);
    [[nodiscard]] static std::uint64_t BucketUpperBound(std::size_t index);

    std::array<std::uint64_t, kNumBuckets> counts_{};
    std::uint64_t total_count_ = 0;
    std::uint64_t max_ = 0;
};
