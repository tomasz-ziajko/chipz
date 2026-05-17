// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

/**
 * @file app.cpp
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
 *   chipz_app_run()  — called each iteration of the main loop; runs service()
 */

#include <chipz/core/core.hpp>
#include <chipz/core/timer_interface.hpp>
#include <chipz/devices/hd44780.hpp>
#include <chipz/devices/pcf8574.hpp>

#include "irq.hpp"
#include "stm32h5xx_hal.h"

using chipz::port::stm32h5xx::IRQn;
using chipz::port::stm32h5xx::kIRQnFirst;
using chipz::port::stm32h5xx::kIRQnLast;

// ---------------------------------------------------------------------------
// SysTickTimer — concrete TimerInterface backed by HAL_GetTick() (1 ms tick)
// ---------------------------------------------------------------------------

class SysTickTimer final : public chipz::TimerInterface {
    public:
    void schedule(uint64_t ticks_from_now) override
    {
        deadline_ = static_cast<uint64_t>(HAL_GetTick()) + ticks_from_now;
        armed_    = true;
    }

    void cancel() override
    {
        armed_ = false;
    }

    uint64_t getCurrentTick() const override
    {
        return static_cast<uint64_t>(HAL_GetTick());
    }

    uint32_t getTickFrequencyHz() const override
    {
        return 1000u;
    }

    void onSysTick()
    {
        if (armed_ && static_cast<uint64_t>(HAL_GetTick()) >= deadline_) {
            armed_ = false;
            if (on_elapsed_) {
                on_elapsed_();
            }
        }
    }

    private:
    uint64_t deadline_ = 0;
    bool     armed_    = false;
};

// ---------------------------------------------------------------------------
// HAL handle — defined by CubeMX; extern declared here
// ---------------------------------------------------------------------------

extern "C" {
__attribute__((weak)) extern I2C_HandleTypeDef hi2c1;
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

SysTickTimer                             g_systick_timer;
chipz::Core<IRQn, kIRQnFirst, kIRQnLast> g_core{g_systick_timer};

chipz::devices::HD44780 g_hd44780{g_pcf8574_iface, {chipz::devices::HD44780::DisplaySize::Size20x4, false, false}};

// ---------------------------------------------------------------------------
// SysTick bridge — called from SysTick_Handler in chipz_isrs.cpp
// ---------------------------------------------------------------------------

extern "C" void chipz_systick_tick()
{
    g_systick_timer.onSysTick();
}

// ---------------------------------------------------------------------------
// Public API — called from main.c via extern "C" declarations
// ---------------------------------------------------------------------------

extern "C" void chipz_app_init()
{
    g_core.add(g_hd44780);
    g_core.initialize();
}

extern "C" void chipz_app_run()
{
    static bool hello_sent = false;
    if (!hello_sent && g_hd44780.isReady()) {
        static const char msg[] = "Hello World!";
        g_hd44780.writeBufferAtPosition(msg, 0, sizeof(msg) - 1);
        g_core.wake(g_hd44780);
        hello_sent = true;
    }

    g_core.service();
}
