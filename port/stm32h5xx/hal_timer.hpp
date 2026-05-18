// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_PORT_STM32H5XX_HAL_TIMER_HPP
#define CHIPZ_PORT_STM32H5XX_HAL_TIMER_HPP

#include <chipz/core/timer_interface.hpp>
#include "stm32h5xx_hal.h"

namespace chipz::port::stm32h5xx {

/**
 * @brief One-shot ms-resolution timer backed by any STM32 basic/GP TIM peripheral.
 *
 * The chosen TIM must be configured by CubeMX with:
 *   - Prescaler set so that the counter ticks at 1 kHz (1 ms / tick)
 *   - One-pulse mode enabled
 *   - Update interrupt enabled
 *
 * schedule(N) programs the timer to fire exactly once after N milliseconds, then
 * stops automatically. schedule() stops any in-progress count before
 * reprogramming, so it is safe to call while a previous count is running
 * (used by Core when a shorter deadline preempts a longer one).
 *
 * getCurrentTick() returns HAL_GetTick() (SysTick ms counter) so that Core's
 * absolute deadline arithmetic remains consistent with the HAL timebase.
 * SysTick is left exclusively for HAL — this class does not touch it.
 *
 * Wire the ISR in your application:
 *   extern "C" void chipz_tim6_elapsed() { g_timer.onElapsed(); }
 */
class HALTimer final : public chipz::TimerInterface {
    public:
    explicit HALTimer(TIM_HandleTypeDef& htim) : htim_(htim) {}

    void schedule(uint64_t ticks_from_now) override
    {
        HAL_TIM_Base_Stop_IT(&htim_);

        if (ticks_from_now == 0u) {
            // Already due — notify immediately rather than arming the hardware.
            if (on_elapsed_) {
                on_elapsed_();
            }
            return;
        }

        // ARR is 16-bit. Clamp; delays > 65535 ms are unusual for chipz drivers.
        const uint32_t ms  = ticks_from_now > 65535u ? 65535u : static_cast<uint32_t>(ticks_from_now);
        // In upcounting one-pulse mode the update event fires after (ARR + 1) counts.
        const uint32_t arr = ms - 1u;

        __HAL_TIM_SET_AUTORELOAD(&htim_, arr);
        __HAL_TIM_SET_COUNTER(&htim_, 0u);
        __HAL_TIM_CLEAR_FLAG(&htim_, TIM_FLAG_UPDATE);
        HAL_TIM_Base_Start_IT(&htim_);
    }

    void cancel() override
    {
        HAL_TIM_Base_Stop_IT(&htim_);
    }

    uint64_t getCurrentTick() const override
    {
        return static_cast<uint64_t>(HAL_GetTick());
    }

    uint32_t getTickFrequencyHz() const override
    {
        return 1000u;
    }

    /// Called from HAL_TIM_PeriodElapsedCallback (via chipz_tim6_elapsed) when the timer fires.
    void onElapsed()
    {
        if (on_elapsed_) {
            on_elapsed_();
        }
    }

    private:
    TIM_HandleTypeDef& htim_;
};

}  // namespace chipz::port::stm32h5xx

#endif  // CHIPZ_PORT_STM32H5XX_HAL_TIMER_HPP
