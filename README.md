# chipz

A C++20 header-only library of interrupt-driven embedded peripheral drivers. chipz provides a unified scheduling and communication framework so drivers run without polling — all execution is triggered by hardware interrupts dispatched from a main-loop service call.

## Overview

chipz separates three concerns:

- **`Core`** — scheduler and ISR router. Registered peripherals are called only when a hardware interrupt fires on their bus, a deadline elapses, or another explicit condition is satisfied. One hardware timer drives all deadline-based scheduling.
- **`Chip<CommInterfaces...>`** — base class for device drivers. Drivers implement `run()` and return a `WaitCondition` that tells Core exactly when to call them next — no spurious calls.
- **Communication interfaces** (`I2CInterface`, `SPIInterface`, `UARTInterface`, `ParallelInterface`) — thin wrappers around HAL IT functions. One instance per physical bus; multiple chips may share the same bus.

Execution model:
1. Hardware ISRs set atomic flags only — no driver code runs in interrupt context.
2. `Core::service()` (called from the main loop) processes pending flags in three passes:
   - **Pass 1a** — non-comm IRQs routed via `onIRQ()`; IRQ-waiting drivers marked runnable.
   - **Pass 1b** — comm interrupts routed via `onInterrupt()`; comm-waiting drivers marked runnable.
   - **Pass 2** — runnable drivers called in priority order; each driver's `run()` return value sets its next `WaitCondition`.

## Scheduling — WaitCondition

`ChipBase::run()` returns a `WaitCondition` that suspends the driver until a specific event:

| Condition | Meaning |
|-----------|---------|
| `WaitCondition::immediate()` | Call `run()` next `service()` cycle |
| `WaitCondition::delayMs(N)` | Call `run()` after N milliseconds |
| `WaitCondition::delayUs(N)` | Call `run()` after N microseconds |
| `WaitCondition::comm(iface)` | Call `run()` when any interrupt fires on `iface` (TransferComplete, Error, ArbitrationLost) |
| `WaitCondition::irq(irqn)` | Call `run()` when hardware IRQ `irqn` fires |
| `WaitCondition::demand()` | Call `run()` only when `Core::wake(chip)` is called |

Legacy drivers that override `main()` instead of `run()` continue to work — the default `run()` delegates to `main()` and returns `immediate()`.

## Supported Devices

| Device   | Bus      | Description                               |
|----------|----------|-------------------------------------------|
| DS3231   | I2C      | High-accuracy RTC with temperature sensor |
| MAX6675  | SPI      | K-type thermocouple-to-digital converter  |
| MCP795W  | SPI      | RTC with SRAM and battery switchover      |
| TJA1145  | SPI      | Automotive CAN transceiver                |
| HD44780  | GPIO/SPI | Character LCD controller                  |

## Requirements

- C++20 compiler (GCC 10+, Clang 12+)
- CMake 3.14+
- A platform `TimerInterface` implementation
- For STM32: STM32 HAL (CubeMX-generated init code)

## Integrating chipz

### 1. Add the library

```cmake
add_subdirectory(path/to/chipz)
target_link_libraries(my_app PRIVATE chipz::chipz)
```

### 2. Add port files

```cmake
target_sources(my_app PRIVATE
    path/to/chipz/port/stm32h5xx/config.cpp       # I2C/SPI/UART/CAN interface objects
    path/to/chipz/port/stm32h5xx/chipz_isrs.cpp   # All ISR handlers and HAL callbacks
    path/to/chipz/port/stm32h5xx/fault_handlers.cpp
    app.cpp
)
```

### 3. Wire up in application code

```cpp
#include <chipz/core/core.hpp>
#include <chipz/devices/ds3231.hpp>
#include <chipz/devices/max6675.hpp>
#include <chipz/interfaces/i2c_interface.hpp>
#include <chipz/interfaces/spi_interface.hpp>
#include "port/stm32h5xx/irq.hpp"
#include "stm32h5xx_hal.h"

using chipz::port::stm32h5xx::IRQn;
using chipz::port::stm32h5xx::kIRQnFirst;
using chipz::port::stm32h5xx::kIRQnLast;

extern chipz::interfaces::I2CInterface g_i2c1;
extern chipz::interfaces::SPIInterface g_spi2;

class SysTickTimer final : public chipz::TimerInterface { /* ... */ };

SysTickTimer g_systick_timer;
chipz::Core<IRQn, kIRQnFirst, kIRQnLast> g_core{g_systick_timer};

chipz::devices::DS3231  g_ds3231 {g_i2c1, []() -> uint32_t { return HAL_GetTick(); }};
chipz::devices::MAX6675 g_max6675{g_spi2, []() -> uint32_t { return HAL_GetTick(); }};

extern "C" void chipz_app_init() {
    g_core.add(g_ds3231);
    g_core.add(g_max6675);
    g_core.initialize();
}

extern "C" void chipz_app_run() {
    g_core.service();  // call from main loop
}
```

## Writing a Driver

### Event-driven (new style)

```cpp
#include <chipz/core/chip.hpp>
#include <chipz/interfaces/i2c_interface.hpp>

class MyDevice : public chipz::Chip<chipz::interfaces::I2CInterface> {
public:
    explicit MyDevice(chipz::interfaces::I2CInterface& i2c)
        : chipz::Chip<chipz::interfaces::I2CInterface>(i2c) {}

    bool initialize() override {
        setConnection<chipz::interfaces::I2CInterface>(
            get<chipz::interfaces::I2CInterface>().registerConnection(0x48));
        return true;
    }
    bool reset()             override { return initialize(); }
    bool isReady()     const override { return true; }
    Status getStatus() const override { return Status::Ready; }
    std::string getDeviceId() const override { return "MyDevice"; }
    bool main()              override { return true; }  // replaced by run()

    chipz::WaitCondition run() override {
        switch (state_) {
            case State::Idle:
                transmit<chipz::interfaces::I2CInterface>(cmd_, sizeof(cmd_));
                state_ = State::WaitingReply;
                return chipz::WaitCondition::comm(get<chipz::interfaces::I2CInterface>());

            case State::WaitingReply:
                process(get<chipz::interfaces::I2CInterface>().getRxBuffer());
                state_ = State::Idle;
                return chipz::WaitCondition::delayMs(100);
        }
        return chipz::WaitCondition::immediate();
    }

protected:
    void onTransferComplete(chipz::CommunicationInterface&, bool success) override {
        if (!success) state_ = State::Idle;
    }

private:
    enum class State { Idle, WaitingReply } state_ = State::Idle;
    uint8_t cmd_[2]{};
};
```

### Legacy style (main + defer)

Existing drivers using `main()` and `defer_ms_` work without modification:

```cpp
bool main() override {
    this->transmit<chipz::interfaces::SPIInterface>(tx_buf_, sizeof(tx_buf_));
    defer_ms_(100);
    return true;
}
```

## STM32H5xx Port

`port/stm32h5xx/` contains ready-to-compile files for the STM32H5xx family.

| File                | Provides |
|---------------------|----------|
| `config.cpp`        | `g_i2c1`–`g_i2c4`, `g_spi1`–`g_spi4`, `g_uart1`–`g_uart4`, `g_can1`/`g_can2` wrapping HAL IT functions |
| `chipz_isrs.cpp`    | All ISR handlers, SysTick, EXTI0–15, and HAL weak callback overrides for I2C/SPI/UART/FDCAN |
| `fault_handlers.cpp`| HardFault, BusFault, MemManage, UsageFault with diagnostic register dumps |
| `irq.hpp`           | `IRQn` enum mirroring CMSIS `IRQn_Type`; `kIRQnFirst`/`kIRQnLast` for `Core<>` template parameters |

HAL handles not initialized in your project (e.g. `hi2c3` when only I2C1 is used) are declared `__attribute__((weak))` so they resolve to null at link time without error.

## Project Structure

```
chipz/
├── include/chipz/
│   ├── chipz.hpp
│   └── core/
│       ├── chip.hpp                   # ChipBase and Chip<CommInterfaces...>
│       ├── core.hpp                   # Scheduler and ISR router
│       ├── communication_interface.hpp
│       ├── wait_condition.hpp         # WaitCondition — scheduling return type
│       ├── timer_interface.hpp
│       ├── concepts.hpp
│       └── completion_sources/
│           ├── timer_completion_source.hpp
│           └── external_completion_source.hpp
│   └── interfaces/
│       ├── i2c_interface.hpp
│       ├── spi_interface.hpp
│       ├── uart_interface.hpp
│       └── parallel_interface.hpp     # N-bit GPIO bus; GPIOInterface alias
│   └── network/
│       └── can/
│           └── can_interface.hpp      # CANInterface<TxFifoDepth, RxMessages...>
├── devices/
│   ├── ds3231/include/chipz/devices/ds3231.hpp
│   ├── hd44780/include/chipz/devices/hd44780.hpp
│   ├── max6675/include/chipz/devices/max6675.hpp
│   ├── mcp795w/include/chipz/devices/mcp795w.hpp
│   └── tja1145/include/chipz/devices/tja1145.hpp
├── port/
│   └── stm32h5xx/
│       ├── config.cpp
│       ├── chipz_isrs.cpp
│       ├── fault_handlers.cpp
│       └── irq.hpp
├── examples/
│   └── stm32H533RET/           # NUCLEO-H533RE: DS3231 (I2C1) + MAX6675 (SPI2)
├── tools/
│   └── fault_decoder.py        # Decodes fault register dumps from fault_handlers.cpp
└── CMakeLists.txt
```

## CMake Targets

| Target           | Contents                           |
|------------------|------------------------------------|
| `chipz::core`    | Core infrastructure (headers only) |
| `chipz::ds3231`  | DS3231 driver                      |
| `chipz::max6675` | MAX6675 driver                     |
| `chipz::mcp795w` | MCP795W driver                     |
| `chipz::tja1145` | TJA1145 driver                     |
| `chipz::hd44780` | HD44780 driver                     |
| `chipz::chipz`   | Umbrella — links all of the above  |

## Known Issues / TODO

- **Driver migration**: DS3231, MAX6675, HD44780 still use the legacy `main()` + `defer_ms_` pattern. Migration to `run()` + `WaitCondition` is pending per-driver.
- **Test coverage**: ~25 tests total; TJA1145 and MCP795W have zero tests; no interrupt flow or multi-peripheral tests.
- **CAN scheduling**: `CANInterface` dispatches RX callbacks directly from ISR context. A future `Core`-integrated network scheduling mechanism will buffer frames and dispatch in `service()`.

## License

Copyright (c) 2026 Tomasz Ziajko

This software is dual-licensed:

- **GPLv3** (see [LICENSE](LICENSE)) for open-source use
- **Commercial license** for proprietary or commercial use — contact the author

SPDX-License-Identifier: `GPL-3.0-only`
