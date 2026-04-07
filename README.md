# chipz

A modern C++17 header-only library for embedded chip drivers, designed to provide a clean and consistent interface for interacting with various peripheral devices.

## Overview

chipz provides a unified framework for working with embedded chips and peripherals. Similar to how Boost extends the C++ standard library, chipz aims to enhance embedded development with well-designed, reusable driver implementations.

## Features

- **Header-only library** - Easy integration, no compilation required
- **Unified interface** - All drivers inherit from a common `Peripheral` base class
- **Type-safe** - Leverages modern C++17 features
- **Zero dependencies** - Pure C++ implementation
- **CMake support** - Easy integration into existing projects

## Supported Devices

Currently includes mockup drivers for:

- **DS3231** - High-precision I2C Real-Time Clock with temperature sensor
- **HD44780** - Character LCD controller (4-bit and 8-bit modes)
- **MAX6675** - K-type thermocouple-to-digital converter (SPI)

## Requirements

- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.14+ (for building examples and tests)
- Git (for fetching Google Test when building tests)

## Quick Start

### Using CMake (Recommended)

1. Add chipz to your project:

```cmake
add_subdirectory(path/to/chipz)
target_link_libraries(your_target PRIVATE chipz::chipz)
```

2. Include in your code:

```cpp
#include <chipz/chipz.hpp>

int main() {
    chipz::devices::DS3231 rtc;

    if (rtc.initialize()) {
        // Use the device
    }

    return 0;
}
```

### Manual Integration

Simply add the `include` directory to your compiler's include path:

```bash
g++ -std=c++17 -I/path/to/chipz/include your_code.cpp
```

## Building the Library

chipz is a header-only library, so there's nothing to build for the library itself. However, you can build the examples and tests.

### Build Examples

```bash
# Clone the repository
git clone https://github.com/yourusername/chipz.git
cd chipz

# Create build directory
mkdir build && cd build

# Configure with CMake (examples enabled by default)
cmake ..

# Build examples
make

# Run an example
./examples/basic_usage
./examples/ds3231_example
./examples/hd44780_example
./examples/max6675_example
```

### Build and Run Tests

Tests use Google Test and Google Mock, which are automatically downloaded during CMake configuration.

```bash
# From the chipz directory
mkdir build && cd build

# Configure with tests enabled
cmake -DCHIPZ_BUILD_TESTS=ON ..

# Build tests
make

# Run all tests
ctest

# Or run the test executable directly for detailed output
./tests/chipz_tests

# Run specific tests
./tests/chipz_tests --gtest_filter=DS3231Test.*
./tests/chipz_tests --gtest_filter=*Temperature*
```

### Build Options

Configure the build with CMake options:

```bash
# Build everything (examples and tests)
cmake -DCHIPZ_BUILD_EXAMPLES=ON -DCHIPZ_BUILD_TESTS=ON ..

# Build only tests (no examples)
cmake -DCHIPZ_BUILD_EXAMPLES=OFF -DCHIPZ_BUILD_TESTS=ON ..

# Build only the library (no examples or tests)
cmake -DCHIPZ_BUILD_EXAMPLES=OFF -DCHIPZ_BUILD_TESTS=OFF ..
```

### Installation

Install chipz system-wide (or to a custom prefix):

```bash
# From the build directory
cmake --install . --prefix /usr/local

# Or with sudo for system-wide installation
sudo cmake --install .
```

After installation, use in your CMake projects:

```cmake
find_package(chipz REQUIRED)
target_link_libraries(your_target PRIVATE chipz::chipz)
```

## Architecture

### Base Class: `Peripheral`

All device drivers inherit from the `chipz::Peripheral` base class, which provides:

- `initialize()` - Initialize the device
- `reset()` - Reset device to default state
- `isReady()` - Check device readiness
- `getStatus()` - Get current device status
- `getDeviceId()` - Get device identifier string

### Device-Specific Interfaces

Each driver extends the base interface with device-specific functionality:

```cpp
// DS3231 RTC example
chipz::devices::DS3231 rtc;
rtc.initialize();

chipz::devices::DS3231::DateTime dt;
rtc.getDateTime(dt);

chipz::devices::DS3231::Temperature temp;
rtc.getTemperature(temp);
```

## Testing

chipz uses **Google Test** and **Google Mock** for comprehensive testing, with a focus on mocking hardware interfaces for embedded development.

### Testing Philosophy

Since chipz is designed for embedded systems, thorough testing without hardware is crucial. The test suite includes:

- **Mock hardware interfaces** (I2C, SPI, GPIO) for testing drivers without physical devices
- **Unit tests** for each driver verifying correct behavior
- **Example tests** showing how to mock hardware communication patterns

### Mock Interfaces

The test suite provides three mock hardware interfaces:

#### MockI2C
For testing I2C devices like DS3231:
```cpp
#include "mock_i2c.hpp"

MockI2C mockI2C;
EXPECT_CALL(mockI2C, isDeviceConnected(0x68))
    .WillOnce(Return(true));
EXPECT_CALL(mockI2C, readRegisterMulti(0x68, 0x00, _, 7))
    .WillOnce(Return(true));
```

#### MockSPI
For testing SPI devices like MAX6675:
```cpp
#include "mock_spi.hpp"

MockSPI mockSPI;
EXPECT_CALL(mockSPI, chipSelect(true));
EXPECT_CALL(mockSPI, transfer16(_))
    .WillOnce(Return(0x0320)); // Simulate 25°C
EXPECT_CALL(mockSPI, chipSelect(false));
```

#### MockGPIO
For testing parallel devices like HD44780:
```cpp
#include "mock_gpio.hpp"

MockGPIO mockGPIO;
EXPECT_CALL(mockGPIO, digitalWrite(_, MockGPIO::PinState::High))
    .Times(AtLeast(1));
```

### Writing Tests for New Drivers

When adding a new driver, create tests that:

1. Verify basic `Peripheral` interface compliance
2. Mock the hardware interface (I2C, SPI, GPIO, etc.)
3. Test normal operation with mocked successful responses
4. Test error conditions (disconnected devices, invalid data, etc.)
5. Verify timing requirements where applicable

Example test structure:
```cpp
TEST_F(MyDeviceTest, ReadDataWithMockedHardware) {
    // Setup: Define expected hardware behavior
    EXPECT_CALL(mockI2C, readRegister(DEVICE_ADDR, DATA_REG, _))
        .WillOnce(DoAll(SetArgReferee<2>(0x42), Return(true)));

    // Execute: Call the driver method
    uint8_t value;
    EXPECT_TRUE(device.readData(value));

    // Verify: Check the result
    EXPECT_EQ(value, 0x42);
}
```

See the `tests/` directory for complete examples of testing each driver type.

## Project Structure

```
chipz/
├── include/
│   └── chipz/
│       ├── chipz.hpp          # Main header (includes all)
│       ├── peripheral.hpp     # Base class
│       └── devices/
│           ├── ds3231.hpp     # DS3231 RTC driver
│           ├── hd44780.hpp    # HD44780 LCD driver
│           └── max6675.hpp    # MAX6675 thermocouple driver
├── examples/                  # Usage examples
│   ├── basic_usage.cpp
│   ├── ds3231_example.cpp
│   ├── hd44780_example.cpp
│   └── max6675_example.cpp
├── tests/                     # Google Test suite
│   ├── mock_i2c.hpp          # Mock I2C interface
│   ├── mock_spi.hpp          # Mock SPI interface
│   ├── mock_gpio.hpp         # Mock GPIO interface
│   ├── test_peripheral.cpp   # Base class tests
│   ├── test_ds3231.cpp       # DS3231 driver tests
│   ├── test_hd44780.cpp      # HD44780 driver tests
│   └── test_max6675.cpp      # MAX6675 driver tests
├── cmake/                     # CMake configuration files
├── CMakeLists.txt            # Main CMake file
├── LICENSE                   # BSD 3-Clause License
└── README.md                 # This file
```

## Design Philosophy

chipz follows these principles:

1. **Simplicity** - Clean, intuitive APIs
2. **Consistency** - Uniform interface across all drivers
3. **Modern C++** - Leverage C++17 features for safety and expressiveness
4. **Header-only** - Easy integration without build complications
5. **Extensibility** - Easy to add new device drivers

## Adding New Drivers

To add a new device driver:

1. Create a new header file in `include/chipz/devices/`
2. Inherit from `chipz::Peripheral`
3. Implement the required interface methods
4. Add device-specific functionality
5. Include in `chipz.hpp`

Example:

```cpp
#include "../peripheral.hpp"

namespace chipz {
namespace devices {

class MyDevice : public Peripheral {
public:
    bool initialize() override { /* ... */ }
    bool reset() override { /* ... */ }
    bool isReady() const override { /* ... */ }
    Status getStatus() const override { /* ... */ }
    std::string getDeviceId() const override { return "My Device"; }

    // Device-specific methods
    void mySpecificFunction() { /* ... */ }
};

} // namespace devices
} // namespace chipz
```

## License

This project is licensed under the BSD 3-Clause License - see the [LICENSE](LICENSE) file for details.

In summary, you are free to use, modify, and distribute this software, but you must:
- Include the copyright notice and license text in any redistribution
- Give appropriate credit to the original author

## Contributing

Contributions are welcome! Please follow the existing code style and architecture patterns.

## Known Issues / TODO

- **Shared bus contention**: When two peripherals sharing a comm interface (e.g. same SPI bus) are both scheduled in the same `Core::service()` cycle, the second peripheral's `this->transmit()` will return an error because the bus is already claimed. Drivers currently handle this by returning from `main()` early and retrying next cycle. A future improvement could add bus-level queuing inside Core so the second peripheral's transfer is automatically deferred without driver-level handling.

## Status

This library is in early development. The current drivers are mockups and do not contain actual hardware implementation yet.
