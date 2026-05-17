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
#include <chipz/devices/ds3231.hpp>
#include <chipz/devices/max6675.hpp>
#include <chipz/devices/mcp795w.hpp>
#include <chipz/interfaces/i2c_interface.hpp>
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

    // Called from SysTick_Handler (via chipz_systick_tick()) after HAL_IncTick().
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
__attribute__((weak)) extern SPI_HandleTypeDef hspi2;
__attribute__((weak)) extern I2C_HandleTypeDef hi2c2;
}

// ---------------------------------------------------------------------------
// chipz objects
// ---------------------------------------------------------------------------

constexpr size_t kSpi2BufferSize =
    std::max(chipz::devices::MAX6675::kMaxTransfer, chipz::devices::MCP795W::kMaxTransfer);
constexpr size_t kI2c2BufferSize = chipz::devices::DS3231::kMaxTransfer;

static auto s_spi2_transfer = [](uint8_t* tx, uint8_t* rx, uint16_t len) -> int {
    return HAL_SPI_TransmitReceive_IT(&hspi2, tx, rx, len);
};
static auto s_i2c2_read = [](uint8_t dev, uint8_t mem, uint8_t* data, uint16_t len) -> int {
    return HAL_I2C_Mem_Read_IT(&hi2c2, dev, mem, I2C_MEMADD_SIZE_8BIT, data, len);
};
static auto s_i2c2_write = [](uint8_t dev, uint8_t mem, const uint8_t* data, uint16_t len) -> int {
    return HAL_I2C_Mem_Write_IT(&hi2c2, dev, mem, I2C_MEMADD_SIZE_8BIT, const_cast<uint8_t*>(data), len);
};

using SPI2Type = chipz::interfaces::SPIInterface<kSpi2BufferSize, decltype(s_spi2_transfer)>;
using I2C2Type = chipz::interfaces::I2CInterface<kI2c2BufferSize, decltype(s_i2c2_read), decltype(s_i2c2_write)>;

SPI2Type g_spi2{s_spi2_transfer};
I2C2Type g_i2c2{s_i2c2_read, s_i2c2_write};

// Override the weak pointers in chipz_isrs.cpp — ISR callbacks route through these
chipz::CommunicationInterface* g_spi2_iface = &g_spi2;
chipz::CommunicationInterface* g_i2c2_iface = &g_i2c2;

SysTickTimer                             g_systick_timer;
chipz::Core<IRQn, kIRQnFirst, kIRQnLast> g_core{g_systick_timer};

auto                    g_max6675_conn = g_spi2.registerConnection([](bool) {});
chipz::devices::MAX6675 g_max6675{g_spi2, g_max6675_conn};

auto                    g_mcp795w_conn = g_spi2.registerConnection([](bool) {});
chipz::devices::MCP795W g_mcp795w{g_spi2, g_mcp795w_conn};

auto                   g_ds3231_conn = g_i2c2.registerConnection(chipz::devices::DS3231::I2C_ADDRESS);
chipz::devices::DS3231 g_ds3231{g_i2c2, g_ds3231_conn};

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
    g_core.add(g_max6675);
    g_core.add(g_mcp795w);
    g_core.add(g_ds3231);
    g_core.initialize();
}

extern "C" void chipz_app_run()
{
    // TEST CRASH — remove before production use
    // Triggers a HardFault (DACCVIOL) ~3 s after boot to exercise the
    // fault capture and fault_monitor.py analysis pipeline.
    static uint32_t tick = 0;
    if (++tick == 3000u) {
        volatile uint32_t* null_ptr = nullptr;
        *null_ptr                   = 0xDEAD'BEEFu;
    }

    g_core.service();
}
