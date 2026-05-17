// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

/**
 * @file demo.cpp
 * @brief chipz application entry points for the NUCLEO-H533RE example
 *
 * Wires up an HD44780 LCD (20×4) via a PCF8574 I2C GPIO expander on I2C1.
 *
 * Hardware connections:
 *   I2C1 → PCF8574 (address 0x27) → HD44780 (4-bit mode)
 *   PCF8574 pin mapping: P0=RS P1=RW P2=E P3=BL P4=D4 P5=D5 P6=D6 P7=D7
 *
 * Entry points called from main.c:
 *   chipz_app_init() — registers peripherals, initialises Core
 *   demo_app_run()   — called each iteration of the main loop; runs service()
 */

#include <chipz/core/core.hpp>
#include <chipz/core/timer_interface.hpp>
#include <chipz/devices/hd44780.hpp>
#include <chipz/devices/pcf8574.hpp>

#include "irq.hpp"
#include "spin.hpp"
#include "stm32h5xx_hal.h"
#include "tim6_timer.hpp"

using chipz::port::stm32h5xx::IRQn;
using chipz::port::stm32h5xx::kIRQnFirst;
using chipz::port::stm32h5xx::kIRQnLast;

// ---------------------------------------------------------------------------
// HAL handles — defined by CubeMX; extern declared here
// ---------------------------------------------------------------------------

extern "C" {
__attribute__((weak)) extern I2C_HandleTypeDef  hi2c1;
__attribute__((weak)) extern TIM_HandleTypeDef  htim6;
}

// ---------------------------------------------------------------------------
// chipz objects
// ---------------------------------------------------------------------------

// PCF8574 on I2C1: uses HAL_I2C_Master_Transmit_IT (no register address).
// Device address 0x27 is fixed in the lambda (A0=A1=A2=0 on PCB).
static auto s_i2c1_pcf_write = [](const uint8_t* data, uint16_t len) -> int {
    return HAL_I2C_Master_Transmit_IT(&hi2c1, 0x27u << 1, const_cast<uint8_t*>(data), len);
};

using PCF8574IfaceType = chipz::devices::PCF8574Interface<1, decltype(s_i2c1_pcf_write)>;
PCF8574IfaceType g_pcf8574_iface{s_i2c1_pcf_write};

// Route I2C1 ISR callbacks to g_pcf8574_iface
chipz::CommunicationInterface* g_i2c1_iface = &g_pcf8574_iface;

chipz::port::stm32h5xx::TIM6Timer        g_tim6_timer{htim6};
chipz::Core<IRQn, kIRQnFirst, kIRQnLast> g_core{g_tim6_timer, chipz::port::stm32h5xx::spinUs};

chipz::devices::HD44780 g_hd44780{g_pcf8574_iface, {chipz::devices::HD44780::DisplaySize::Size20x4, false, false}};

// ---------------------------------------------------------------------------
// TIM6 bridge — called from HAL_TIM_PeriodElapsedCallback in chipz_isrs.cpp
// ---------------------------------------------------------------------------

extern "C" void chipz_tim6_elapsed()
{
    g_tim6_timer.onElapsed();
}

// ---------------------------------------------------------------------------
// Public API — called from main.c via extern "C" declarations
// ---------------------------------------------------------------------------

extern "C" void chipz_app_init()
{
    chipz::port::stm32h5xx::initDwt();
    g_core.add(g_hd44780);
    g_core.initialize();
}

extern "C" void demo_app_run()
{
    static bool     initialized     = false;
    static bool     hw_visible      = false;
    static uint8_t  demo_line       = 0;   // 0=line2, 1=line3, 2=line4
    static bool     demo_need_clear = false;
    static uint16_t demo_clear_pos  = 0;
    static uint32_t hw_deadline     = 0;
    static uint32_t demo_deadline   = 0;

    static const char hello[]  = "Hello World! ";
    static const char clear1[] = "             ";  // 13 spaces — matches hello length
    static const char chipz[]  = "CHIPZ demo code!";
    static const char clear2[] = "                ";  // 16 spaces — matches chipz length

    if (!initialized && g_hd44780.isReady()) {
        uint32_t now  = HAL_GetTick();
        hw_deadline   = now + 3000;
        demo_deadline = now + 2000;
        g_hd44780.writeBufferAtPosition(chipz, 20, 16);
        g_core.wake(g_hd44780);
        initialized = true;
    }

    if (initialized && g_hd44780.isReady()) {
        uint32_t now = HAL_GetTick();

        if (demo_need_clear) {
            g_hd44780.writeBufferAtPosition(clear2, demo_clear_pos, 16);
            g_core.wake(g_hd44780);
            demo_need_clear = false;
        }
        else if (static_cast<int32_t>(now - hw_deadline) >= 0) {
            hw_deadline += 3000;
            hw_visible = !hw_visible;
            g_hd44780.writeBufferAtPosition(hw_visible ? hello : clear1, 0, 13);
            g_core.wake(g_hd44780);
        }
        else if (static_cast<int32_t>(now - demo_deadline) >= 0) {
            demo_deadline += 2000;
            demo_clear_pos = 20u + static_cast<uint16_t>(demo_line) * 20u;
            demo_line      = (demo_line + 1u) % 3u;
            g_hd44780.writeBufferAtPosition(chipz, 20u + static_cast<uint16_t>(demo_line) * 20u, 16);
            g_core.wake(g_hd44780);
            demo_need_clear = true;
        }
    }

    g_core.service();
}
