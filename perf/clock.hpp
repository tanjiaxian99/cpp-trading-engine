#pragma once

#include <cstdint>

namespace perf {

[[nodiscard]] inline std::uint64_t ReadCounter() {
    std::uint64_t ticks;
    // Retrieve the value of cntvct_el0 register which increments monotonically at a fixed rate
    asm volatile("mrs %0, cntvct_el0" : "=r"(ticks));
    return ticks;
}

[[nodiscard]] inline std::uint64_t CounterFrequency() {
    std::uint64_t freq;
    // Retrieve the value of cntfrq_el0 register which gives the number of ticks cntvct_el0
    // increments by per second
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    return freq;
}

[[nodiscard]] inline std::uint64_t TicksToNanos(std::uint64_t ticks) {
    static const std::uint64_t kFreq = CounterFrequency();
    return ticks * 1'000'000'000ULL / kFreq;
}

}  // namespace perf
