#pragma once

#include <cstdint>
#include "esp_timer.h"

namespace Iotaru {

/// Monotonic 64-bit millisecond uptime (~292 million years range, no 49.7-day rollover).
inline int64_t monotonicMs() {
    return esp_timer_get_time() / 1000LL;
}

/// Monotonic 64-bit microsecond uptime.
inline int64_t monotonicUs() {
    return esp_timer_get_time();
}

/// Wrap-safe 32-bit unsigned elapsed duration (valid for intervals < 49.7 days).
inline uint32_t elapsedMs32(uint32_t nowMs, uint32_t prevMs) {
    return static_cast<uint32_t>(nowMs - prevMs);
}

/// Explicit deadline timer without magic-zero sentinel collision.
struct OneShotDeadline {
    bool    active     = false;
    int64_t deadlineMs = 0;

    void arm(int64_t delayMs) {
        deadlineMs = monotonicMs() + delayMs;
        active = true;
    }

    void cancel() {
        active = false;
        deadlineMs = 0;
    }

    bool expired() const {
        return active && (monotonicMs() >= deadlineMs);
    }
};

} // namespace Iotaru

