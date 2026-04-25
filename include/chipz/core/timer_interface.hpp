// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_TIMER_INTERFACE_HPP
#define CHIPZ_TIMER_INTERFACE_HPP

#include <cstdint>
#include <functional>

namespace chipz {

/**
 * @brief Abstract interface for a one-shot hardware timer
 *
 * Concrete implementations are platform-specific (e.g. STM32 TIM peripheral).
 * The timer is used by Core to schedule peripheral execution via hardware
 * interrupts rather than periodic polling.
 *
 * The ISR must call the elapsed callback set via setElapsedCallback().
 * The callback only sets a pending flag — no driver code runs in interrupt context.
 */
class TimerInterface {
    public:
    virtual ~TimerInterface() = default;

    /**
     * @brief Arm the timer to fire once after ticks_from_now ticks
     * @param ticks_from_now Number of ticks until the interrupt fires
     */
    virtual void schedule(uint64_t ticks_from_now) = 0;

    /**
     * @brief Disarm the timer, cancelling any pending interrupt
     */
    virtual void cancel() = 0;

    /**
     * @brief Get the current monotonic tick counter
     * @return Current tick value (never wraps within session for uint64_t)
     */
    virtual uint64_t getCurrentTick() const = 0;

    /**
     * @brief Get the timer tick frequency
     * @return Ticks per second (e.g. 1000000 for a 1MHz timer)
     */
    virtual uint32_t getTickFrequencyHz() const = 0;

    /**
     * @brief Set the callback invoked from the timer ISR when the timer elapses
     *
     * The callback must be minimal — it should only set an atomic flag.
     * Core injects [this]{ pending_ = true; } here.
     *
     * @param cb Callback to invoke on timer elapsed
     */
    void setElapsedCallback(std::function<void()> cb)
    {
        on_elapsed_ = std::move(cb);
    }

    protected:
    std::function<void()> on_elapsed_;
};

}  // namespace chipz

#endif  // CHIPZ_TIMER_INTERFACE_HPP
