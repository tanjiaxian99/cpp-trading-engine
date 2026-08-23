#include "perf/latency_histogram.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

void LatencyHistogram::Record(std::uint64_t nanos) {
    total_count_++;
    max_ = std::max(max_, nanos);
    counts_[BucketIndex(nanos)]++;
}

std::uint64_t LatencyHistogram::Percentile(double p) const {
    if (total_count_ == 0) {
        return 0;
    }

    const auto target = std::max<std::uint64_t>(
        1, static_cast<std::uint64_t>(std::ceil(p * static_cast<double>(total_count_))));

    std::uint64_t cumulative = 0;
    for (std::size_t i = 0; i < kNumBuckets; ++i) {
        cumulative += counts_[i];
        if (cumulative >= target) {
            return BucketUpperBound(i);
        }
    }
    return max_;
}

std::size_t LatencyHistogram::BucketIndex(std::uint64_t nanos) {
    if (nanos < kSubBucketCount) {
        return nanos;
    }

    const int msb = 63 - std::countl_zero(nanos);
    const int octave = msb - kSubBucketBits + 1;
    if (octave > kNumOctaves) {
        return kNumBuckets - 1;
    }

    // 1st octave has sub-buckets of width 1, subsequent octaves double the widths
    const int shift = octave - 1;
    const std::uint64_t sub_offset = (nanos >> shift) - kSubBucketCount;
    return kSubBucketCount + (octave - 1) * kSubBucketCount + sub_offset;
}

std::uint64_t LatencyHistogram::BucketUpperBound(std::size_t index) {
    if (index < kSubBucketCount) {
        return index;
    }

    const std::size_t offset_from_base = index - kSubBucketCount;
    const int octave = static_cast<int>(offset_from_base / kSubBucketCount) + 1;
    const std::uint64_t sub_offset = offset_from_base % kSubBucketCount;
    const int shift = octave - 1;

    // kSubBucketCount << shift gets to the start of the octave,
    // adding sub_offset << shift moves it down the octave
    const std::uint64_t lower = (kSubBucketCount + sub_offset) << shift;
    return lower + (1ULL << shift) - 1;
}
