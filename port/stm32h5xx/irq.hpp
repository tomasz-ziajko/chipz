// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_PORT_STM32H5XX_IRQ_HPP
#define CHIPZ_PORT_STM32H5XX_IRQ_HPP

#include <cstdint>

namespace chipz {
namespace port {
namespace stm32h5xx {

/**
 * @brief STM32H533xx interrupt vector numbers
 *
 * Mirrors IRQn_Type from the CMSIS device header (stm32h533xx.h) so that
 * chipz port code can reference interrupts by name without pulling in the
 * full CMSIS/HAL headers. Values are identical to IRQn_Type — safe to
 * static_cast between the two.
 *
 * The underlying type is int16_t to accommodate the negative ARM
 * Cortex-M33 exception numbers.
 */
enum class IRQn : int16_t {

    // -----------------------------------------------------------------------
    // ARM Cortex-M33 core exceptions
    // -----------------------------------------------------------------------

    Reset            = -15,
    NonMaskableInt   = -14,
    HardFault        = -13,
    MemoryManagement = -12,
    BusFault         = -11,
    UsageFault       = -10,
    SecureFault      =  -9,
    SVCall           =  -5,
    DebugMonitor     =  -4,
    PendSV           =  -2,
    SysTick          =  -1,

    // -----------------------------------------------------------------------
    // STM32H533xx peripheral interrupts
    // -----------------------------------------------------------------------

    WWDG                = 0,
    PVD_AVD             = 1,
    RTC                 = 2,
    RTC_S               = 3,
    TAMP                = 4,
    RAMCFG              = 5,
    FLASH               = 6,
    FLASH_S             = 7,
    GTZC                = 8,
    RCC                 = 9,
    RCC_S               = 10,

    EXTI0               = 11,
    EXTI1               = 12,
    EXTI2               = 13,
    EXTI3               = 14,
    EXTI4               = 15,
    EXTI5               = 16,
    EXTI6               = 17,
    EXTI7               = 18,
    EXTI8               = 19,
    EXTI9               = 20,
    EXTI10              = 21,
    EXTI11              = 22,
    EXTI12              = 23,
    EXTI13              = 24,
    EXTI14              = 25,
    EXTI15              = 26,

    GPDMA1_Channel0     = 27,
    GPDMA1_Channel1     = 28,
    GPDMA1_Channel2     = 29,
    GPDMA1_Channel3     = 30,
    GPDMA1_Channel4     = 31,
    GPDMA1_Channel5     = 32,
    GPDMA1_Channel6     = 33,
    GPDMA1_Channel7     = 34,

    IWDG                = 35,
    SAES                = 36,
    ADC1                = 37,
    DAC1                = 38,

    FDCAN1_IT0          = 39,
    FDCAN1_IT1          = 40,

    TIM1_BRK            = 41,
    TIM1_UP             = 42,
    TIM1_TRG_COM        = 43,
    TIM1_CC             = 44,
    TIM2                = 45,
    TIM3                = 46,
    TIM4                = 47,
    TIM5                = 48,
    TIM6                = 49,
    TIM7                = 50,

    I2C1_EV             = 51,
    I2C1_ER             = 52,
    I2C2_EV             = 53,
    I2C2_ER             = 54,

    SPI1                = 55,
    SPI2                = 56,
    SPI3                = 57,

    USART1              = 58,
    USART2              = 59,
    USART3              = 60,
    UART4               = 61,
    UART5               = 62,
    LPUART1             = 63,

    LPTIM1              = 64,

    TIM8_BRK            = 65,
    TIM8_UP             = 66,
    TIM8_TRG_COM        = 67,
    TIM8_CC             = 68,

    ADC2                = 69,
    LPTIM2              = 70,
    TIM15               = 71,

    USB_DRD_FS          = 74,
    CRS                 = 75,
    UCPD1               = 76,
    FMC                 = 77,
    OCTOSPI1            = 78,
    SDMMC1              = 79,

    I2C3_EV             = 80,
    I2C3_ER             = 81,

    SPI4                = 82,

    USART6              = 85,

    GPDMA2_Channel0     = 90,
    GPDMA2_Channel1     = 91,
    GPDMA2_Channel2     = 92,
    GPDMA2_Channel3     = 93,
    GPDMA2_Channel4     = 94,
    GPDMA2_Channel5     = 95,
    GPDMA2_Channel6     = 96,
    GPDMA2_Channel7     = 97,

    FPU                 = 103,
    ICACHE              = 104,
    DCACHE1             = 105,
    DCMI_PSSI           = 108,

    FDCAN2_IT0          = 109,
    FDCAN2_IT1          = 110,

    DTS                 = 113,
    RNG                 = 114,
    OTFDEC1             = 115,
    AES                 = 116,
    HASH                = 117,
    PKA                 = 118,
    CEC                 = 119,
    TIM12               = 120,

    I3C1_EV             = 123,
    I3C1_ER             = 124,

    I3C2_EV             = 131,
    I3C2_ER             = 132,
};

} // namespace stm32h5xx
} // namespace port
} // namespace chipz

#endif // CHIPZ_PORT_STM32H5XX_IRQ_HPP
