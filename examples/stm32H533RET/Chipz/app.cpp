// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

/**
 * @file app.cpp
 * @brief chipz application entry points for the NUCLEO-H533RE example
 *
 * Wires up the MAX6675 thermocouple driver over SPI2.
 *
 * Entry points called from main.c:
 *   chipz_app_init() — registers peripherals, initialises Core
 *   chipz_app_run()  — called each iteration of the main loop; runs service()
 *
 * Timer
 * -----
 * SysTickTimer wraps HAL_GetTick() (1 ms resolution, 1000 Hz) as the chipz
 * TimerInterface. The SysTick ISR calls chipz_systick_tick() each millisecond
 * to check whether the scheduler deadline has elapsed and fire on_elapsed_().
 */

#include <chipz/core/core.hpp>
#include <chipz/core/timer_interface.hpp>
#include <chipz/devices/max6675.hpp>
#include <chipz/interfaces/spi_interface.hpp>
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
    void schedule(uint64_t ticks_from_now) override {
        deadline_ = static_cast<uint64_t>(HAL_GetTick()) + ticks_from_now;
        armed_    = true;
    }

    void cancel() override { armed_ = false; }

    uint64_t getCurrentTick() const override {
        return static_cast<uint64_t>(HAL_GetTick());
    }

    uint32_t getTickFrequencyHz() const override { return 1000u; }

    // Called from SysTick_Handler (via chipz_systick_tick()) after HAL_IncTick().
    void onSysTick() {
        if (armed_ && static_cast<uint64_t>(HAL_GetTick()) >= deadline_) {
            armed_ = false;
            if (on_elapsed_) on_elapsed_();
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
    __attribute__((weak)) extern SPI_HandleTypeDef hspi2;
}

// ---------------------------------------------------------------------------
// chipz objects
// ---------------------------------------------------------------------------

using SPI2Type = chipz::interfaces::SPIInterface<chipz::devices::MAX6675<>::kMaxTransfer>;

SPI2Type g_spi2{
    [](uint8_t* tx, uint8_t* rx, uint16_t len) -> int {
        return HAL_SPI_TransmitReceive_IT(&hspi2, tx, rx, len);
    }
};

// Override the weak pointer in chipz_isrs.cpp — ISR callbacks route through this
chipz::CommunicationInterface* g_spi2_iface = &g_spi2;

SysTickTimer g_systick_timer;
chipz::Core<IRQn, kIRQnFirst, kIRQnLast> g_core{g_systick_timer};

chipz::devices::MAX6675<> g_max6675{g_spi2};

// ---------------------------------------------------------------------------
// SysTick bridge — called from SysTick_Handler in chipz_isrs.cpp
// ---------------------------------------------------------------------------

extern "C" void chipz_systick_tick() {
    g_systick_timer.onSysTick();
}

// ---------------------------------------------------------------------------
// Public API — called from main.c via extern "C" declarations
// ---------------------------------------------------------------------------

extern "C" void chipz_app_init() {
    g_core.add(g_max6675);
    g_core.initialize();
}

extern "C" void chipz_app_run() {
    g_core.service();
}
