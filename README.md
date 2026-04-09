# chipz

A C++20 header-only library of interrupt-driven embedded peripheral drivers. chipz provides a unified scheduling and communication framework so drivers run without polling — all execution is triggered by hardware interrupts and dispatched from a main-loop service call.

## Overview

chipz separates three concerns:

- **`Core`** — scheduler and ISR router. Registered peripherals are called only when due or when a hardware interrupt fires on their bus.
- **`Peripheral<T>`** — base class for device drivers. Drivers implement `main()` and call `defer_ms()` / `defer_us()` to control their own scheduling rate.
- **Communication interfaces** (`I2CInterface`, `SPIInterface`) — thin wrappers around HAL IT functions. One instance per physical bus; multiple peripherals may share the same instance.

Execution model:
1. Hardware ISRs set atomic flags only — no driver code runs in interrupt context.
2. `Core::service()` (called from the main loop) processes pending flags in two passes: comm interrupts first, then scheduled `main()` calls in priority order.

## Supported Devices

| Device    | Bus  | Description                              |
|-----------|------|------------------------------------------|
| DS3231    | I2C  | High-accuracy RTC with temperature sensor |
| MAX6675   | SPI  | K-type thermocouple-to-digital converter  |
| MCP795W   | SPI  | RTC with SRAM and battery switchover      |
| TJA1145   | SPI  | Automotive CAN transceiver                |
| HD44780   | GPIO/SPI | Character LCD controller             |

## Requirements

- C++20 compiler (GCC 10+, Clang 12+)
- CMake 3.23+
- A platform timer implementation (see `TimerInterface`)
- For STM32: STM32 HAL (CubeMX-generated init code)

## Integrating chipz

### 1. Add the library

```cmake
add_subdirectory(path/to/chipz)
```

### 2. Register devices and generate ISR handlers

Use the `chipz_add_<device>()` functions to link each device and record its ISR requirements. Then call `chipz_generate_isrs()` once to emit a single `chipz_isr_handlers.cpp` containing all peripheral vector handlers and HAL weak-callback overrides.

```cmake
chipz_add_ds3231(TARGET my_app
    I2C_INSTANCE I2C1
    I2C_HANDLE   hi2c1
    I2C_IFACE    g_i2c1)

chipz_add_max6675(TARGET my_app
    SPI_INSTANCE SPI2
    SPI_HANDLE   hspi2
    SPI_IFACE    g_spi2)

chipz_generate_isrs(TARGET      my_app
                    CORE        g_core
                    HAL_INCLUDE "stm32h5xx_hal.h")
```

Optional parameters:
- `chipz_add_ds3231`: `ALARM_EXTI <pin>` — EXTI line for the INT# alarm pin
- `chipz_add_tja1145`: `WAKE_EXTI <pin>`, `CAN_INSTANCE <CAN1>`, `CAN_HANDLE <hcan1>`
- `chipz_add_mcp795w`: `ALARM_EXTI <pin>`
- `chipz_add_hd44780`: `SPI_INSTANCE`, `SPI_HANDLE`, `SPI_IFACE` (only if using an SPI GPIO expander)

### 3. Add port files and application source

```cmake
target_sources(my_app PRIVATE
    path/to/chipz/port/stm32h5xx/config.cpp      # I2C/SPI interface objects
    path/to/chipz/port/stm32h5xx/chipz_isrs.cpp  # exceptions, SysTick, EXTI
    app.cpp
)
```

### 4. Wire up in application code

```cpp
// app.cpp
#include <chipz/core.hpp>
#include <chipz/devices/ds3231.hpp>
#include <chipz/devices/max6675.hpp>
#include <chipz/interfaces/i2c_interface.hpp>
#include <chipz/interfaces/spi_interface.hpp>
#include "stm32h5xx_hal.h"

extern chipz::interfaces::I2CInterface g_i2c1;
extern chipz::interfaces::SPIInterface g_spi2;

// Implement chipz::TimerInterface with your platform timer
class SysTickTimer final : public chipz::TimerInterface { /* ... */ };

SysTickTimer g_systick_timer;
chipz::Core  g_core{g_systick_timer};

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

## STM32H5xx Port

`port/stm32h5xx/` contains ready-to-compile files for the STM32H5xx family. They do not need modification.

| File             | Provides                                                         |
|------------------|------------------------------------------------------------------|
| `config.cpp`     | `g_i2c1`–`g_i2c4`, `g_spi1`–`g_spi3` interface objects wrapping HAL IT functions |
| `chipz_isrs.cpp` | Cortex-M exception handlers, SysTick, EXTI0–EXTI15 handlers     |

HAL handles that are not initialized in your project (e.g. `hi2c3` when only I2C1 is used) are declared `__attribute__((weak))` in `config.cpp`, so they resolve to null at link time without error.

The comm peripheral ISR handlers (e.g. `I2C1_EV_IRQHandler`, `HAL_I2C_MemRxCpltCallback`) are generated separately by `chipz_generate_isrs()` into `chipz_isr_handlers.cpp`.

## Architecture

### Core classes

| Class | Role |
|-------|------|
| `chipz::Core` | Scheduler and ISR router. Registered via `add()`, driven via `service()`. |
| `chipz::PeripheralBase` | Abstract base — `initialize()`, `main()`, `onInterrupt()`, `onISR()`, priority |
| `chipz::Peripheral<T>` | Template middle layer — binds a driver to its comm interface, provides `transmit()` / `receive()` |
| `chipz::TimerInterface` | Abstract one-shot timer — implement for your platform |
| `chipz::CommunicationInterface` | Abstract comm contract — implemented by `I2CInterface` / `SPIInterface` |

### Writing a driver

```cpp
#include <chipz/peripheral.hpp>
#include <chipz/interfaces/spi_interface.hpp>

class MyDevice : public chipz::Peripheral<chipz::interfaces::SPIInterface> {
public:
    explicit MyDevice(chipz::interfaces::SPIInterface& spi)
        : chipz::Peripheral<chipz::interfaces::SPIInterface>(spi) {}

    bool initialize() override { /* ... */ return true; }
    bool reset()      override { return true; }
    bool isReady() const override { return status_ == Status::Ready; }
    chipz::PeripheralBase::Status getStatus() const override { return status_; }
    std::string getDeviceId() const override { return "MyDevice"; }

    bool main() override {
        this->transmit(tx_buf_, sizeof(tx_buf_));
        defer_ms_(100);  // run again in 100 ms
        return true;
    }

protected:
    void onTransferComplete(bool success) override { /* handle result */ }
};
```

## Project Structure

```
chipz/
├── include/chipz/
│   ├── chipz.hpp                  # Umbrella include
│   ├── core.hpp                   # Scheduler and ISR router
│   ├── peripheral.hpp             # PeripheralBase and Peripheral<T>
│   ├── communication_interface.hpp
│   ├── concepts.hpp
│   ├── isr_source.hpp
│   ├── timer_interface.hpp
│   └── interfaces/
│       ├── i2c_interface.hpp
│       └── spi_interface.hpp
├── devices/
│   ├── ds3231/include/chipz/devices/ds3231.hpp
│   ├── hd44780/include/chipz/devices/hd44780.hpp
│   ├── max6675/include/chipz/devices/max6675.hpp
│   ├── mcp795w/include/chipz/devices/mcp795w.hpp
│   └── tja1145/include/chipz/devices/tja1145.hpp
├── port/
│   └── stm32h5xx/
│       ├── config.cpp             # I2C/SPI interface object definitions
│       ├── chipz_isrs.cpp         # Exception handlers, SysTick, EXTI
│       └── irq.hpp
├── examples/
│   └── stm32H533RET/              # NUCLEO-H533RE: DS3231 (I2C1) + MAX6675 (SPI2)
├── cmake/
│   ├── ChipzISR.cmake             # chipz_add_*() and chipz_generate_isrs()
│   └── chipzConfig.cmake.in
└── CMakeLists.txt
```

## CMake Targets

| Target          | Contents                            |
|-----------------|-------------------------------------|
| `chipz::core`   | Core infrastructure (headers only)  |
| `chipz::ds3231` | DS3231 driver                       |
| `chipz::max6675`| MAX6675 driver                      |
| `chipz::mcp795w`| MCP795W driver                      |
| `chipz::tja1145`| TJA1145 driver                      |
| `chipz::hd44780`| HD44780 driver                      |
| `chipz::chipz`  | Umbrella — links all of the above   |

Prefer `chipz_add_<device>()` over `target_link_libraries(... chipz::<device>)` directly — the add functions also record the ISR metadata needed by `chipz_generate_isrs()`.

## Known Issues / TODO

- **Shared bus contention**: When two peripherals sharing a comm interface are both scheduled in the same `Core::service()` cycle, the second peripheral's `transmit()` returns false (bus busy). Drivers handle this by returning early from `main()` and retrying next cycle. A future improvement could add bus-level queuing inside `Core`.

## License

Copyright (c) 2026 Tomasz Ziajko

This software is dual-licensed:

- **GPLv3** (see [LICENSE](LICENSE)) for open-source use
- **Commercial license** for proprietary or commercial use — contact the author

SPDX-License-Identifier: `GPL-3.0-only`
