// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_COMPLETION_SOURCES_TIMER_COMPLETION_SOURCE_HPP
#define CHIPZ_COMPLETION_SOURCES_TIMER_COMPLETION_SOURCE_HPP

#include "../timer_interface.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>

namespace chipz {
namespace CompletionSource {

/**
 * @brief Completion source backed by a one-shot hardware timer
 *
 * Uses a dedicated TimerInterface (separate from Core's scheduler timer).
 * When armed with duration_us > 0, schedules the timer and fires the callback
 * from the elapsed ISR. When armed with duration_us == 0, calls the callback
 * immediately (synchronously, no interrupt).
 *
 * Typical use: enforce minimum bus-settle or command-execution delays after
 * writing to a parallel / GPIO bus.
 *
 * @code
 *   ParallelInterface<6, CompletionSource::Timer> bus(writeFn, readFn,
 *                                                     CompletionSource::Timer(myTimer));
 * @endcode
 */
class Timer {
public:
    explicit Timer(TimerInterface& timer) : timer_(timer) {}

    void arm(uint32_t duration_us, std::function<void()> on_complete) {
        if (duration_us == 0) {
            on_complete();
            return;
        }
        timer_.setElapsedCallback(std::move(on_complete));
        uint64_t ticks = static_cast<uint64_t>(duration_us)
                         * static_cast<uint64_t>(timer_.getTickFrequencyHz())
                         / 1000000u;
        timer_.schedule(std::max(uint64_t(1), ticks));
    }

    void cancel() { timer_.cancel(); }

private:
    TimerInterface& timer_;
};

} // namespace CompletionSource
} // namespace chipz

#endif // CHIPZ_COMPLETION_SOURCES_TIMER_COMPLETION_SOURCE_HPP
