// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

/**
 * @file chipz_isrs.cpp
 * @brief Cortex-M exception handlers and GPIO EXTI handlers for STM32H5xx
 *
 * This file provides:
 *   - Cortex-M system exception handlers (NMI, faults, SVC, PendSV, SysTick)
 *   - GPIO EXTI line handlers (EXTI0-15), routing fired lines to g_core.onISR()
 *
 * Peripheral communication IRQ handlers (I2C, SPI, etc.) and their HAL weak
 * callback overrides are generated automatically by chipz_generate_isrs() into
 * chipz_isr_handlers.cpp — they are not defined here.
 *
 * SysTick
 * -------
 * SysTick_Handler calls HAL_IncTick() (required by HAL timeout machinery) and
 * chipz_systick_tick() (defined in app.cpp). Both will be removed when the HAL
 * dependency is eliminated.
 *
 * EXTI note (STM32H5xx vs STM32F4/F7)
 * ------------------------------------
 * Unlike STM32F4/F7, the H5xx family assigns a dedicated vector to every
 * EXTI line 0-15. chipz::ISRSource still groups lines 5-9 and 10-15 (at
 * most one peripheral may register per group at the scheduler level).
 */

#include <chipz/core.hpp>
#include <chipz/isr_source.hpp>
#include "stm32h5xx_hal.h"

extern chipz::Core g_core;
extern "C" void chipz_systick_tick();

extern "C" {

// ---------------------------------------------------------------------------
// Cortex-M exception handlers
// ---------------------------------------------------------------------------

void NMI_Handler()        { while (1) {} }
void HardFault_Handler()  { while (1) {} }
void MemManage_Handler()  { while (1) {} }
void BusFault_Handler()   { while (1) {} }
void UsageFault_Handler() { while (1) {} }
void SVC_Handler()        {}
void DebugMon_Handler()   {}
void PendSV_Handler()     {}

void SysTick_Handler() {
    HAL_IncTick();
    chipz_systick_tick();
}

// ---------------------------------------------------------------------------
// GPIO EXTI line handlers
// ---------------------------------------------------------------------------

void EXTI0_IRQHandler()  { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);  g_core.onISR(chipz::ISRSource::EXTI0);     }
void EXTI1_IRQHandler()  { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_1);  g_core.onISR(chipz::ISRSource::EXTI1);     }
void EXTI2_IRQHandler()  { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_2);  g_core.onISR(chipz::ISRSource::EXTI2);     }
void EXTI3_IRQHandler()  { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_3);  g_core.onISR(chipz::ISRSource::EXTI3);     }
void EXTI4_IRQHandler()  { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_4);  g_core.onISR(chipz::ISRSource::EXTI4);     }
void EXTI5_IRQHandler()  { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_5);  g_core.onISR(chipz::ISRSource::EXTI5_9);   }
void EXTI6_IRQHandler()  { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_6);  g_core.onISR(chipz::ISRSource::EXTI5_9);   }
void EXTI7_IRQHandler()  { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_7);  g_core.onISR(chipz::ISRSource::EXTI5_9);   }
void EXTI8_IRQHandler()  { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_8);  g_core.onISR(chipz::ISRSource::EXTI5_9);   }
void EXTI9_IRQHandler()  { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_9);  g_core.onISR(chipz::ISRSource::EXTI5_9);   }
void EXTI10_IRQHandler() { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_10); g_core.onISR(chipz::ISRSource::EXTI10_15); }
void EXTI11_IRQHandler() { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_11); g_core.onISR(chipz::ISRSource::EXTI10_15); }
void EXTI12_IRQHandler() { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_12); g_core.onISR(chipz::ISRSource::EXTI10_15); }
void EXTI13_IRQHandler() { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_13); g_core.onISR(chipz::ISRSource::EXTI10_15); }
void EXTI14_IRQHandler() { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_14); g_core.onISR(chipz::ISRSource::EXTI10_15); }
void EXTI15_IRQHandler() { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_15); g_core.onISR(chipz::ISRSource::EXTI10_15); }

} // extern "C"
