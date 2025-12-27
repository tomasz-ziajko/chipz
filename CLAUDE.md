# Chipz - Modern C++ Embedded Device Driver Library

## Overview

**Chipz** is a header-only C++17 library providing type-safe, platform-independent device drivers for embedded systems. The library follows modern C++ best practices while maintaining zero-cost abstractions suitable for resource-constrained microcontrollers.

**Key Features:**
- Header-only core library (zero compilation overhead)
- Platform-independent through function pointer injection
- RTOS-compatible with async/interrupt-driven operations
- Fully templated device drivers for type safety
- Comprehensive test suite with hardware mocks
- STM32 HAL integration examples

## Architecture

### Core Components

```
chipz/
├── Peripheral              # Base class for all devices (non-templated)
├── CommunicationInterface  # Base class for protocol implementations
├── interfaces/             # Protocol-specific implementations
│   ├── I2CInterface       # I2C async communication
│   └── SPIInterface       # SPI async communication
├── devices/                # Device driver implementations
│   ├── DS3231            # Real-Time Clock (I2C)
│   ├── HD44780           # LCD Controller (GPIO)
│   ├── MAX6675           # Thermocouple Converter (SPI)
│   ├── MCP795W           # SRAM with RTC (SPI)
│   └── TJA1145           # CAN Transceiver (SPI)
└── platform/               # Optional platform-specific utilities
    └── freertos/          # FreeRTOS integration helpers
        └── cpp_allocator  # C++ new/delete using FreeRTOS heap
```

### Design Philosophy

#### 1. **Header-Only Core**
All device drivers and interfaces are implemented in headers using templates. This eliminates compilation dependencies and allows full compiler optimization.

#### 2. **Platform Independence**
Hardware abstraction through `std::function` callbacks:

```cpp
// I2C Interface - inject platform HAL functions
I2CInterface(uint8_t device_address,
             std::function<int(...)> read_func,
             std::function<int(...)> write_func);

// Device Template - works with any I2C implementation
template<typename CommInterface>
class DS3231 : public Peripheral { ... };
```

#### 3. **Async/Interrupt-Driven**
All communication interfaces support non-blocking operations suitable for RTOS environments:

```cpp
// Start async transfer
bool transmit(const uint8_t* data, size_t length);

// Called from interrupt handler
void handleInterrupt(bool success);

// Callback when transfer completes
void setTransferCompleteCallback(std::function<void(bool)> callback);
```

#### 4. **Zero-Cost Abstraction**
- Templates eliminate runtime polymorphism overhead
- Lazy buffer allocation (grows as needed, never shrinks)
- No dynamic allocation in critical paths
- Minimal memory footprint

## Supported Devices

| Device | Interface | Type | Features |
|--------|-----------|------|----------|
| **DS3231** | I2C | Real-Time Clock | Temperature sensor, alarms, 32.768kHz output |
| **HD44780** | GPIO | LCD Controller | 16x2/20x4 character displays, state machine init |
| **MAX6675** | SPI | Thermocouple | K-type thermocouple, cold junction compensation |
| **MCP795W** | SPI | RTC + SRAM | Battery-backed time, 64 bytes SRAM |
| **TJA1145** | SPI | CAN Transceiver | System Basis Chip, wake/sleep, diagnostics |

## Real-World Usage

**Production Status**: ✅ Successfully deployed in fireplace controller project

### Verified Working Configuration
- **Platform**: STM32H533xx with FreeRTOS
- **Devices in use**: DS3231 (I2C), MAX6675 (SPI)
- **Communication**: Async interrupt-driven I2C and SPI
- **Integration**: Mixed C/C++ codebase with legacy drivers
- **Status**: Both devices operational and stable

### Lessons Learned from Integration

#### Critical Requirements for FreeRTOS + C++

1. **Task Stack Size**: Minimum 8KB (2048 words)
   - Default 512 bytes is insufficient for C++ code
   - Lambdas, templates, and virtual functions require more stack
   - Stack overflow shows as PC = 0xA5A5A5A5 (FreeRTOS fill pattern)

2. **Heap Size**: Minimum 32KB
   - Default 8KB is too small for C++ dynamic allocation
   - Must use heap_4 or heap_5 (NOT heap_1)
   - C++ allocator redirects new/delete to FreeRTOS heap

3. **Nullptr Guards**: Essential in interrupt handlers
   - SPI/I2C interrupts may fire during initialization
   - Always check pointers before calling methods:
     ```cpp
     void spi_interrupt(void) {
         if (interface_ptr) {
             interface_ptr->handleInterrupt(true);
         }
     }
     ```

4. **Build Order**: STM32CubeMX before chipz library
   - Platform library needs FreeRTOS headers
   - CMake subdirectory order matters

### Memory Footprint (Actual Production Build)
```
STM32H533xx with FreeRTOS + Chipz (DS3231 + MAX6675):
  RAM:   54 KB / 272 KB (19.6%)
    - FreeRTOS heap: 32 KB
    - Task stack: 8 KB
    - BSS/Data: ~14 KB

  FLASH: 97 KB / 512 KB (18.5%)
    - Application code
    - Chipz library (header-only, templated)
    - STM32 HAL drivers
    - FreeRTOS kernel
```

## Platform Integration

### Communication Interface Pattern

All devices use templated communication interfaces:

```cpp
// Create platform-specific interface
I2CInterface i2c(0x68,  // Device address
    // Read function (inject STM32 HAL)
    [](uint8_t addr, uint8_t reg, uint8_t* data, uint16_t size) {
        return HAL_I2C_Mem_Read_IT(&hi2c1, addr << 1, reg,
                                   I2C_MEMADD_SIZE_8BIT, data, size);
    },
    // Write function
    [](uint8_t addr, uint8_t reg, const uint8_t* data, uint16_t size) {
        return HAL_I2C_Mem_Write_IT(&hi2c1, addr << 1, reg,
                                    I2C_MEMADD_SIZE_8BIT,
                                    const_cast<uint8_t*>(data), size);
    }
);

// Create device using interface
DS3231<I2CInterface> rtc(i2c, []() { return HAL_GetTick(); });

// Connect interrupt to interface
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c == &hi2c1) {
        i2c.handleInterrupt(true);
    }
}
```

### RTOS Integration

#### Tick Provider
All devices accept a tick provider function:

```cpp
// FreeRTOS
DS3231<I2CInterface> rtc(i2c, []() -> uint32_t {
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
});

// STM32 HAL (no RTOS)
DS3231<I2CInterface> rtc(i2c, []() -> uint32_t {
    return HAL_GetTick();
});
```

#### Main Loop Integration
Each device has a `main()` method for periodic processing:

```cpp
// FreeRTOS task
void DeviceTask(void *argument) {
    for(;;) {
        rtc.main();      // Process RTC state machine
        display.main();  // Process LCD state machine
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Bare metal main loop
while (1) {
    rtc.main();
    display.main();
    HAL_Delay(10);
}
```

## Platform-Specific Components

### FreeRTOS C++ Allocator

**Status**: ✅ **Tested and verified in production** (fireplace controller project)

**Problem:** By default, C++ `new`/`delete` operators use newlib malloc/free, which allocates from a small system heap (typically 512 bytes). This causes heap exhaustion and corruption in embedded systems using FreeRTOS.

**Solution:** Override global `new`/`delete` operators to use FreeRTOS heap:

```cpp
#include "chipz/platform/freertos/cpp_allocator.hpp"

// In your CMakeLists.txt, add:
target_sources(your_app PRIVATE
    ${CHIPZ_SOURCE_DIR}/src/platform/freertos/cpp_allocator.cpp
)
```

This automatically redirects all C++ allocations to `pvPortMalloc`/`vPortFree`.

**Configuration:**
Increase FreeRTOS heap size for C++ objects:

```c
// FreeRTOSConfig.h
#define configTOTAL_HEAP_SIZE  ((size_t)32768)  // 32KB recommended
```

## Directory Structure

```
libs/
├── include/chipz/              # Public API headers (header-only)
│   ├── chipz.hpp              # Main include (imports all modules)
│   ├── peripheral.hpp         # Base class
│   ├── communication_interface.hpp
│   ├── interfaces/
│   │   ├── i2c_interface.hpp
│   │   └── spi_interface.hpp
│   ├── devices/
│   │   ├── ds3231.hpp
│   │   ├── hd44780.hpp
│   │   ├── max6675.hpp
│   │   ├── mcp795w.hpp
│   │   └── tja1145.hpp
│   └── platform/              # Optional platform utilities
│       └── freertos/
│           └── cpp_allocator.hpp
├── src/                        # Platform implementations (need compilation)
│   └── platform/
│       └── freertos/
│           └── cpp_allocator.cpp
├── examples/                   # Usage demonstrations
│   ├── basic_usage.cpp
│   ├── ds3231_stm32.cpp       # STM32 HAL integration
│   ├── max6675_stm32.cpp
│   └── hd44780_stm32.cpp
├── tests/                      # Google Test suite
│   ├── mock_i2c.hpp
│   ├── mock_spi.hpp
│   ├── test_ds3231.cpp
│   └── test_max6675.cpp
├── CMakeLists.txt             # Build configuration
├── CLAUDE.md                  # This file (AI assistant guide)
└── README.md                  # User documentation
```

## Build System

### CMake Configuration

**Core Library (Header-Only):**
```cmake
# Always available - no compilation needed
find_package(chipz REQUIRED)
target_link_libraries(your_app PRIVATE chipz::chipz)
```

**Platform Support (Optional):**
```cmake
# Option 1: Enable platform support in chipz
set(CHIPZ_ENABLE_FREERTOS ON CACHE BOOL "Enable FreeRTOS support")
find_package(chipz REQUIRED)
target_link_libraries(your_app PRIVATE
    chipz::chipz
    chipz::platform::freertos  # Compiled platform library
)

# Option 2: Manually include platform sources
target_sources(your_app PRIVATE
    ${CHIPZ_SOURCE_DIR}/src/platform/freertos/cpp_allocator.cpp
)
```

### Build Options

```bash
# Build library only (header-only)
cmake ..

# Build with examples
cmake -DCHIPZ_BUILD_EXAMPLES=ON ..

# Build with tests
cmake -DCHIPZ_BUILD_TESTS=ON ..

# Build with FreeRTOS platform support
cmake -DCHIPZ_ENABLE_FREERTOS=ON ..
```

## Testing

The library includes comprehensive unit tests using Google Test:

```bash
# Build and run tests
cmake -DCHIPZ_BUILD_TESTS=ON ..
cmake --build .
ctest --output-on-failure
```

**Mock Interfaces:**
- `MockI2C` - Simulates I2C bus operations
- `MockSPI` - Simulates SPI transfers
- `MockGPIO` - Simulates GPIO pin control

**Test Coverage:**
- Device initialization sequences
- State machine transitions
- Error handling
- Data parsing and formatting
- Interrupt-driven operations

## Common Patterns

### 1. Device Initialization

```cpp
// Create communication interface
SPIInterface spi(
    [](uint8_t* tx, uint8_t* rx, uint16_t size) {
        return HAL_SPI_TransmitReceive_IT(&hspi1, tx, rx, size);
    },
    [](bool select) {
        HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, select ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }
);

// Create device
MAX6675<SPIInterface> thermocouple(spi, []() { return HAL_GetTick(); });

// Initialize
if (thermocouple.initialize()) {
    // Device ready
}
```

### 2. Reading Device Data

```cpp
// In main loop or RTOS task
thermocouple.main();  // Process state machine

if (thermocouple.isReady()) {
    float temperature = thermocouple.getTemperature();
    printf("Temperature: %.2f°C\n", temperature);
}
```

### 3. Interrupt Handling

```cpp
// Global interface pointers (for interrupt access)
SPIInterface* thermocouple_spi = nullptr;

void setup() {
    thermocouple_spi = new SPIInterface(...);
    // ... device setup
}

// HAL interrupt callback
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi == &hspi1) {
        thermocouple_spi->handleInterrupt(true);
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi) {
    if (hspi == &hspi1) {
        thermocouple_spi->handleInterrupt(false);
    }
}
```

## Extending the Library

### Adding a New Device Driver

1. Create device header: `include/chipz/devices/your_device.hpp`
2. Inherit from `Peripheral` base class
3. Template on communication interface type
4. Implement required virtual methods:
   - `initialize()` - Device initialization
   - `reset()` - Software reset
   - `isReady()` - Check if device is operational
   - `getStatus()` - Return current status
   - `getDeviceId()` - Return device identifier
   - `main()` - Periodic state machine processing

5. Add device-specific methods
6. Create tests in `tests/test_your_device.cpp`
7. Add example in `examples/your_device_stm32.cpp`

### Adding Platform Support

1. Create platform directory: `src/platform/your_rtos/`
2. Implement platform-specific code (e.g., allocators, timers)
3. Add header: `include/chipz/platform/your_rtos/`
4. Update CMakeLists.txt with platform option
5. Add documentation and examples

## Requirements

- **C++17** compiler (GCC 7+, Clang 5+, MSVC 2017+)
- **CMake 3.14+** for building
- **Google Test** (optional, for tests)
- **RTOS** (optional, FreeRTOS recommended)

## License

See LICENSE file in repository root.

## Version

Current version: **0.1.0**

**Status**: ✅ Production-tested with STM32H5 + FreeRTOS

**Verified devices**: DS3231 (I2C), MAX6675 (SPI)

Last updated: 2025-12-27

---

**Note for AI Assistants:**

This library is designed for embedded systems with limited resources. When modifying or extending the library:

1. **Maintain header-only pattern** for core drivers
2. **Use templates** for zero-cost abstraction
3. **Avoid dynamic allocation** in critical paths
4. **Follow async/interrupt-driven** patterns
5. **Keep platform code separate** in `platform/` directories
6. **Write tests** for all new functionality
7. **Provide STM32 examples** for common use cases
8. **Document memory requirements** for each feature

The library prioritizes **correctness, testability, and minimal overhead** over convenience features.
