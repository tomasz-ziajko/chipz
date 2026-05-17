// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_PORT_STM32H5XX_SPIN_HPP
#define CHIPZ_PORT_STM32H5XX_SPIN_HPP

#include "stm32h5xx_hal.h"
#include <cstdint>

namespace chipz::port::stm32h5xx {

/// Enable DWT cycle counter. Call once before any spinUs() call.
inline void initDwt()
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

/// Busy-spin for exactly `us` microseconds using DWT->CYCCNT.
/// Requires initDwt() to have been called once at startup.
/// Safe for us < 1000 (sub-millisecond remainders); wraps correctly at 32-bit boundary.
inline void spinUs(uint32_t us)
{
    const uint32_t cycles = us * (SystemCoreClock / 1000000u);
    const uint32_t start  = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < cycles) {}
}

}  // namespace chipz::port::stm32h5xx

#endif  // CHIPZ_PORT_STM32H5XX_SPIN_HPP
