# Testing Guide for chipz

This document provides detailed guidance on testing chipz drivers and creating mock hardware interfaces.

## Overview

chipz uses **Google Test** and **Google Mock** to enable thorough testing of embedded drivers without requiring physical hardware. This is essential for:

- Continuous integration and automated testing
- Development without hardware access
- Testing error conditions that are hard to reproduce with real hardware
- Fast iteration during development

## Test Organization

### Test Files

Each driver should have a corresponding test file:
- `test_peripheral.cpp` - Tests for the base `Peripheral` class
- `test_<device>.cpp` - Tests for specific device drivers (e.g., `test_ds3231.cpp`)

### Mock Files

Mock hardware interfaces are defined in header files:
- `mock_i2c.hpp` - Mock I2C bus interface
- `mock_spi.hpp` - Mock SPI bus interface
- `mock_gpio.hpp` - Mock GPIO interface

## Writing Tests for Drivers

### Basic Test Structure

```cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <chipz/devices/your_device.hpp>
#include "mock_i2c.hpp"  // Or appropriate mock interface

using namespace chipz;
using namespace chipz::devices;
using namespace chipz::testing;
using ::testing::Return;
using ::testing::_;

class YourDeviceTest : public ::testing::Test {
protected:
    YourDevice device;
    MockI2C mockI2C;  // Or MockSPI, MockGPIO, etc.

    void SetUp() override {
        // Common setup for all tests
    }

    void TearDown() override {
        // Cleanup after tests
    }
};

TEST_F(YourDeviceTest, TestName) {
    // Arrange: Set up expectations
    EXPECT_CALL(mockI2C, someMethod(_))
        .WillOnce(Return(true));

    // Act: Execute the code under test
    bool result = device.someOperation();

    // Assert: Verify the results
    EXPECT_TRUE(result);
}
```

### Essential Tests for Every Driver

1. **Device ID Test**
   ```cpp
   TEST_F(YourDeviceTest, DeviceIdIsCorrect) {
       EXPECT_EQ(device.getDeviceId(), "Expected Device Name");
   }
   ```

2. **Initial State Test**
   ```cpp
   TEST_F(YourDeviceTest, InitialStatusIsUninitialized) {
       EXPECT_EQ(device.getStatus(), Peripheral::Status::Uninitialized);
       EXPECT_FALSE(device.isReady());
   }
   ```

3. **Initialization Test**
   ```cpp
   TEST_F(YourDeviceTest, InitializeSetsStatusToReady) {
       // Mock hardware initialization
       EXPECT_CALL(mockI2C, begin())
           .WillOnce(Return(true));
       EXPECT_CALL(mockI2C, isDeviceConnected(DEVICE_ADDRESS))
           .WillOnce(Return(true));

       EXPECT_TRUE(device.initialize());
       EXPECT_EQ(device.getStatus(), Peripheral::Status::Ready);
       EXPECT_TRUE(device.isReady());
   }
   ```

4. **Reset Test**
   ```cpp
   TEST_F(YourDeviceTest, ResetRestoresDevice) {
       device.initialize();
       EXPECT_TRUE(device.reset());
       EXPECT_TRUE(device.isReady());
   }
   ```

## Mock Interface Patterns

### I2C Mocking (for DS3231, etc.)

```cpp
// Simulate successful I2C communication
EXPECT_CALL(mockI2C, isDeviceConnected(0x68))
    .WillOnce(Return(true));

// Mock reading a single register
EXPECT_CALL(mockI2C, readRegister(0x68, 0x00, _))
    .WillOnce(DoAll(
        SetArgReferee<2>(0x42),  // Set the output value
        Return(true)
    ));

// Mock reading multiple bytes
EXPECT_CALL(mockI2C, readRegisterMulti(0x68, 0x00, _, 7))
    .WillOnce([](uint8_t addr, uint8_t reg, uint8_t* buffer, size_t len) {
        buffer[0] = 0x30;  // Fill buffer with test data
        buffer[1] = 0x45;
        // ... etc
        return true;
    });

// Mock writing a register
EXPECT_CALL(mockI2C, writeRegister(0x68, 0x0E, 0x05))
    .WillOnce(Return(true));
```

### SPI Mocking (for MAX6675, etc.)

```cpp
// Mock SPI transaction
EXPECT_CALL(mockSPI, chipSelect(true));
EXPECT_CALL(mockSPI, transfer16(0x0000))
    .WillOnce(Return(0x0320));  // Simulated response
EXPECT_CALL(mockSPI, chipSelect(false));

// Mock configuration
EXPECT_CALL(mockSPI, setClockSpeed(4000000));
EXPECT_CALL(mockSPI, setDataMode(0));
EXPECT_CALL(mockSPI, setBitOrder(true));  // MSB first
```

### GPIO Mocking (for HD44780, etc.)

```cpp
// Mock pin configuration
EXPECT_CALL(mockGPIO, pinMode(4, MockGPIO::PinMode::Output));
EXPECT_CALL(mockGPIO, pinMode(5, MockGPIO::PinMode::Output));

// Mock digital writes
EXPECT_CALL(mockGPIO, digitalWrite(4, MockGPIO::PinState::High));
EXPECT_CALL(mockGPIO, digitalWrite(5, MockGPIO::PinState::Low));

// Mock delays
EXPECT_CALL(mockGPIO, delayMicroseconds(1))
    .Times(AtLeast(1));

// Mock reading a pin
EXPECT_CALL(mockGPIO, digitalRead(7))
    .WillOnce(Return(MockGPIO::PinState::Low));
```

## Testing Error Conditions

Always test failure scenarios:

```cpp
TEST_F(YourDeviceTest, HandlesI2CFailureGracefully) {
    // Simulate I2C communication failure
    EXPECT_CALL(mockI2C, readRegister(_, _, _))
        .WillOnce(Return(false));

    uint8_t value;
    EXPECT_FALSE(device.readData(value));
    EXPECT_EQ(device.getStatus(), Peripheral::Status::Error);
}

TEST_F(YourDeviceTest, HandlesDeviceNotConnected) {
    EXPECT_CALL(mockI2C, isDeviceConnected(DEVICE_ADDRESS))
        .WillOnce(Return(false));

    EXPECT_FALSE(device.initialize());
    EXPECT_EQ(device.getStatus(), Peripheral::Status::Disconnected);
}
```

## Testing Data Conversion

For devices that convert raw data (like temperature sensors):

```cpp
TEST_F(MAX6675Test, ConvertsRawDataToTemperature) {
    // MAX6675: 12-bit temperature value in bits 14-3
    // Temperature = value * 0.25°C
    // For 25.00°C: 25.00 / 0.25 = 100 = 0x64
    // Shifted left 3 bits: 0x320
    uint16_t rawValue = 0x0320;

    EXPECT_CALL(mockSPI, chipSelect(true));
    EXPECT_CALL(mockSPI, transfer16(_))
        .WillOnce(Return(rawValue));
    EXPECT_CALL(mockSPI, chipSelect(false));

    MAX6675::Reading reading;
    EXPECT_TRUE(thermocouple.readTemperature(reading));
    EXPECT_FLOAT_EQ(reading.temperature, 25.0f);
    EXPECT_FALSE(reading.thermocoupleOpen);
}
```

## Using Google Mock Matchers

Google Mock provides powerful matchers for flexible expectations:

```cpp
using ::testing::_;           // Matches any value
using ::testing::Eq;          // Matches equal value
using ::testing::Gt;          // Greater than
using ::testing::Lt;          // Less than
using ::testing::AllOf;       // All conditions must match
using ::testing::AnyOf;       // Any condition can match

// Example: Accept any pin number
EXPECT_CALL(mockGPIO, digitalWrite(_, MockGPIO::PinState::High));

// Example: Specific value match
EXPECT_CALL(mockI2C, writeRegister(0x68, Eq(0x0E), _));

// Example: Range matching
EXPECT_CALL(mockSPI, setClockSpeed(AllOf(Gt(1000000), Lt(5000000))));
```

## Running Specific Tests

```bash
# Run all tests for a specific device
./tests/chipz_tests --gtest_filter=DS3231Test.*

# Run tests matching a pattern
./tests/chipz_tests --gtest_filter=*Temperature*

# Run a specific test
./tests/chipz_tests --gtest_filter=DS3231Test.ReadTemperatureReturnsExpectedValue

# Run tests with verbose output
./tests/chipz_tests --gtest_filter=DS3231Test.* --gtest_print_time=1

# Repeat tests (useful for detecting flaky tests)
./tests/chipz_tests --gtest_repeat=10
```

## Disabled Tests

Use `DISABLED_` prefix for tests that are templates or not yet ready:

```cpp
TEST_F(YourDeviceTest, DISABLED_TemplateForFutureTest) {
    // This test won't run but serves as documentation
}
```

These tests can be run explicitly:
```bash
./tests/chipz_tests --gtest_also_run_disabled_tests
```

## Best Practices

1. **Test one thing per test** - Each test should verify a single behavior
2. **Use descriptive test names** - `ReadTemperatureWhenThermocoupleDisconnected` not `Test1`
3. **Arrange-Act-Assert** - Structure tests clearly with setup, execution, and verification
4. **Mock at the hardware boundary** - Mock I2C/SPI/GPIO, not higher-level driver methods
5. **Test both success and failure** - Verify error handling is correct
6. **Don't test implementation details** - Test behavior, not internal state
7. **Keep tests independent** - Each test should run successfully in isolation
8. **Document complex mocks** - Add comments explaining non-obvious mock data

## Example: Complete Driver Test

See `tests/test_ds3231.cpp`, `tests/test_hd44780.cpp`, and `tests/test_max6675.cpp` for complete examples of testing different device types.

## Adding New Mock Interfaces

If you need a new hardware interface (UART, CAN, etc.):

1. Create `mock_<interface>.hpp` in the `tests/` directory
2. Define the interface as a virtual class
3. Use `MOCK_METHOD` for each operation
4. Document the interface in this guide
5. Create example tests showing usage

Example:
```cpp
class MockUART {
public:
    virtual ~MockUART() = default;

    MOCK_METHOD(bool, begin, (uint32_t baudRate), ());
    MOCK_METHOD(size_t, write, (const uint8_t* data, size_t length), ());
    MOCK_METHOD(size_t, read, (uint8_t* buffer, size_t length), ());
    MOCK_METHOD(size_t, available, (), ());
};
```

## Continuous Integration

Tests should run on every commit. Example GitHub Actions workflow:

```yaml
name: Tests
on: [push, pull_request]
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Build and Test
        run: |
          mkdir build && cd build
          cmake -DCHIPZ_BUILD_TESTS=ON ..
          make
          ctest --output-on-failure
```

## References

- [Google Test Documentation](https://google.github.io/googletest/)
- [Google Mock for Dummies](https://google.github.io/googletest/gmock_for_dummies.html)
- [Google Mock Cheat Sheet](https://google.github.io/googletest/gmock_cheat_sheet.html)
