# chipz

A C++20 header-only library of interrupt-driven embedded peripheral drivers. chipz provides a unified scheduling and communication framework so drivers run without polling — all execution is triggered by hardware interrupts dispatched from a main-loop service call.

## Overview

chipz separates three concerns:

- **`Core`** — scheduler and ISR router. Registered peripherals are resumed only when a hardware interrupt fires on their bus, a deadline elapses, or another explicit condition is satisfied. One hardware timer drives all deadline-based scheduling.
- **`Chip<CommInterfaces...>`** — base class for device drivers. Drivers implement `run()` as a C++20 coroutine that `co_yield`s a `WaitCondition` to tell Core exactly when to resume next — no spurious calls.
- **Communication interfaces** (`I2CInterface`, `SPIInterface`, `UARTInterface`, `ParallelInterface`) — thin wrappers around HAL IT functions. One instance per physical bus; multiple chips may share the same bus.

Execution model:
1. Hardware ISRs set atomic flags only — no driver code runs in interrupt context.
2. `Core::add()` calls `run()` once per driver to obtain a `DriverTask` coroutine handle, stored for the lifetime of the driver.
3. `Core::service()` (called from the main loop) processes pending flags in three passes:
   - **Pass 1a** — non-comm IRQs routed via `onIRQ()`; IRQ-waiting drivers marked runnable.
   - **Pass 1b** — comm interrupts routed via `onInterrupt()`; comm-waiting drivers marked runnable.
   - **Pass 2** — runnable drivers resumed in priority order; the `WaitCondition` the coroutine `co_yield`s sets its next wait.

## Scheduling — WaitCondition

Drivers are C++20 coroutines. `run()` is called once; the returned `DriverTask` handle is stored by Core and resumed whenever the driver's wait condition is satisfied. The driver `co_yield`s a `WaitCondition` to declare what it is waiting for:

| Condition | Meaning |
|-----------|---------|
| `WaitCondition::immediate()` | Resume next `service()` cycle |
| `WaitCondition::delayMs(N)` | Resume after N milliseconds |
| `WaitCondition::delayUs(N)` | Resume after N microseconds |
| `WaitCondition::comm(iface)` | Resume when any interrupt fires on `iface` (TransferComplete, Error, ArbitrationLost) |
| `WaitCondition::irq(irqn)` | Resume when hardware IRQ `irqn` fires |
| `WaitCondition::demand()` | Resume only when `Core::wake(chip)` is called |

Legacy drivers that override `main()` instead of `run()` continue to work — the default `run()` wraps `main()` in an infinite coroutine loop and yields `immediate()` each iteration.

## Supported Devices

| Device   | Bus      | Description                               |
|----------|----------|-------------------------------------------|
| DS3231   | I2C      | High-accuracy RTC with temperature sensor |
| MAX6675  | SPI      | K-type thermocouple-to-digital converter  |
| MCP795W  | SPI      | RTC with SRAM and battery switchover      |
| TJA1145  | SPI      | Automotive CAN transceiver                |
| PCF8574  | I2C      | 8-bit I2C GPIO expander                   |
| HD44780  | Parallel | Character LCD controller (4-bit mode)     |

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
#include "port/stm32h5xx/spin.hpp"
#include "port/stm32h5xx/hal_timer.hpp"
#include "stm32h5xx_hal.h"

using chipz::port::stm32h5xx::IRQn;
using chipz::port::stm32h5xx::kIRQnFirst;
using chipz::port::stm32h5xx::kIRQnLast;

extern chipz::interfaces::I2CInterface g_i2c1;
extern chipz::interfaces::SPIInterface g_spi2;
extern TIM_HandleTypeDef htim6;

chipz::port::stm32h5xx::HALTimer         g_tim6_timer{htim6};
chipz::Core<IRQn, kIRQnFirst, kIRQnLast> g_core{g_tim6_timer, chipz::port::stm32h5xx::spinUs};

chipz::devices::DS3231  g_ds3231 {g_i2c1};
chipz::devices::MAX6675 g_max6675{g_spi2};

extern "C" void chipz_tim6_elapsed() { g_tim6_timer.onElapsed(); }

extern "C" void chipz_app_init() {
    chipz::port::stm32h5xx::initDwt();
    g_core.add(g_ds3231);
    g_core.add(g_max6675);
    g_core.initialize();
}

extern "C" void chipz_app_run() {
    g_core.service();  // call from main loop
}
```

## Writing a Driver

### Coroutine style (current)

`run()` is a C++20 coroutine. `co_yield` suspends the driver and declares what to wait for next. `onTransferComplete()` stores the result; the coroutine reads it after resuming.

```cpp
#include <chipz/core/chip.hpp>
#include <chipz/interfaces/i2c_interface.hpp>

class MyDevice : public chipz::Chip<chipz::interfaces::I2CInterface> {
public:
    explicit MyDevice(chipz::interfaces::I2CInterface& i2c)
        : chipz::Chip<chipz::interfaces::I2CInterface>(i2c)
        , last_transfer_ok_(false) {}

    bool initialize() override {
        setConnection<chipz::interfaces::I2CInterface>(
            get<chipz::interfaces::I2CInterface>().registerConnection(0x48));
        last_transfer_ok_ = false;
        return true;
    }
    bool reset()             override { return initialize(); }
    bool isReady()     const override { return true; }
    Status getStatus() const override { return Status::Ready; }
    std::string getDeviceId() const override { return "MyDevice"; }
    bool main()              override { return true; }

    chipz::DriverTask run() override {
        auto& i2c = get<chipz::interfaces::I2CInterface>();
        while (true) {
            transmit<chipz::interfaces::I2CInterface>(cmd_, sizeof(cmd_));
            co_yield chipz::WaitCondition::comm(i2c);
            if (last_transfer_ok_)
                process(i2c.getRxBuffer());
            co_yield chipz::WaitCondition::delayMs(100);
        }
    }

protected:
    void onTransferComplete(chipz::CommunicationInterface&, bool success) override {
        last_transfer_ok_ = success;
    }

private:
    bool    last_transfer_ok_;
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
| `chipz_isrs.cpp`    | All ISR handlers, SysTick, EXTI0–15, TIM6, and HAL weak callback overrides for I2C/SPI/UART/FDCAN/TIM |
| `fault_handlers.cpp`| HardFault, BusFault, MemManage, UsageFault with diagnostic register dumps |
| `irq.hpp`           | `IRQn` enum mirroring CMSIS `IRQn_Type`; `kIRQnFirst`/`kIRQnLast` for `Core<>` template parameters |
| `hal_timer.hpp`     | `HALTimer : TimerInterface` — one-shot HAL TIM for coarse ms deadline scheduling |
| `spin.hpp`          | `initDwt()` / `spinUs(us)` — DWT CYCCNT busy-spin for sub-ms fine delays |

### Hybrid TIM6 + DWT scheduling

`Core` accepts an optional `SpinFn` (`void(*)(uint32_t us)`) as its second constructor argument. When a driver `co_yield`s `WaitCondition::delayUs(N)`, Core splits N into a coarse millisecond part (scheduled on TIM6) and a sub-millisecond remainder. The remainder is spun via `spinUs` immediately before `task.resume()`, giving precise timing without wasting CPU on a polling loop. Pass `chipz::port::stm32h5xx::spinUs` to enable this on STM32H5xx.

HAL handles not initialized in your project (e.g. `hi2c3` when only I2C1 is used) are declared `__attribute__((weak))` so they resolve to null at link time without error.

## Crash Analysis

`port/stm32h5xx/fault_handlers.cpp` installs handlers for HardFault, BusFault,
MemManage, and UsageFault. On entry each handler captures the full register
state — stacked frame, FPU registers, SCB fault-status registers, and a 32-word
stack snapshot — into a global `g_fault_info` struct stored in `.noinit` RAM.
The magic sentinel `0xDEADC0DE` is written **last**, so it doubles as a
completion flag. The handler then spins, leaving the CPU halted and the data
stable for the debugger to read.

`.noinit` RAM is not zeroed by the C startup library, so `g_fault_info` also
survives warm resets (watchdog, software reset). Add the section to your linker
script if it is not already present:

```
.noinit (NOLOAD) :
{
  KEEP(*(.noinit))
} >RAM
```

### Automated capture via OpenOCD

With OpenOCD running and the device connected, run from the example directory:

```sh
python ../../tools/fault_monitor.py --config fault_monitor.toml
```

The tool resolves `g_fault_info` from the ELF, connects to OpenOCD's TCL port
(4444), polls the magic word for up to `timeout` seconds, reads the struct when
it appears, and feeds it straight into the fault decoder. The magic word is
cleared afterward so a second run does not re-report the same crash.

`fault_monitor.toml` (committed alongside each example project):

```toml
elf         = "build/Debug/stm32H533RET.elf"
map         = "build/Debug/stm32H533RET.map"
src         = "../.."
host        = "localhost"
port        = 4444
timeout     = 5
tool_prefix = "starm-"
clear       = true
```

Any value can be overridden on the command line, e.g. `--timeout 15`.

### Manual capture via GDB

```
(gdb) dump binary memory dump.bin &g_fault_info ((char*)&g_fault_info + sizeof(g_fault_info))
```

Then decode offline:

```sh
python tools/fault_decoder.py --elf firmware.elf --map firmware.map \
                               --binary dump.bin --src .
```

`fault_decoder.py` also accepts `--gdb` (pasted `x/71xw` output) and `--hex`
(space-separated hex words).

### Real-time hook

For targets without a debugger attached, override `chipz_fault_report()` to
emit the dump immediately at fault time (UART, ITM/SWO, etc.):

```cpp
extern "C"
void chipz_fault_report(const chipz::port::stm32h5xx::FaultInfo& info) {
    // called from fault handler context — keep it simple
    char buf[32];
    snprintf(buf, sizeof(buf), "FAULT pc=%08lx\r\n", info.pc);
    HAL_UART_Transmit(&huart2, (uint8_t*)buf, strlen(buf), 50);
}
```

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
│   ├── pcf8574/include/chipz/devices/pcf8574.hpp
│   └── tja1145/include/chipz/devices/tja1145.hpp
├── port/
│   └── stm32h5xx/
│       ├── config.cpp
│       ├── chipz_isrs.cpp
│       ├── fault_handlers.cpp
│       ├── irq.hpp
│       ├── spin.hpp             # DWT CYCCNT busy-spin (initDwt / spinUs)
│       └── hal_timer.hpp       # HALTimer : TimerInterface
├── examples/
│   └── stm32H533RET/           # NUCLEO-H533RE: PCF8574 + HD44780 (I2C1, 20×4 LCD)
│       ├── Chipz/demo/demo.cpp # Application entry points
│       └── fault_monitor.toml  # OpenOCD crash capture config for this example
├── tools/
│   ├── fault_decoder.py        # Decodes a FaultInfo dump (binary / GDB / hex input)
│   └── fault_monitor.py        # Connects to OpenOCD, captures and decodes live crashes
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
| `chipz::pcf8574` | PCF8574 driver                     |
| `chipz::hd44780` | HD44780 driver                     |
| `chipz::chipz`   | Umbrella — links all of the above  |

## Known Issues / TODO

- **Test coverage**: ~25 tests total; TJA1145, MCP795W, PCF8574, and HD44780 have zero tests; no interrupt flow or multi-peripheral tests.
- **CAN scheduling**: `CANInterface` dispatches RX callbacks directly from ISR context. A future `Core`-integrated network scheduling mechanism will buffer frames and dispatch in `service()`.

## License

Copyright (c) 2026 Tomasz Ziajko

This software is dual-licensed:

- **GPLv3** (see [LICENSE](LICENSE)) for open-source use
- **Commercial license** for proprietary or commercial use — contact the author

SPDX-License-Identifier: `GPL-3.0-only`
