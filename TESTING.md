# Testing Guide for chipz

## Running the Tests

**One-liner (configure, build, and run):**
```bash
mkdir -p build && cd build && cmake -DCHIPZ_BUILD_TESTS=ON .. && cmake --build . && ctest --output-on-failure
```

**Configure and build:**
```bash
mkdir -p build && cd build
cmake -DCHIPZ_BUILD_TESTS=ON ..
cmake --build .
```

**Run all tests:**
```bash
ctest --output-on-failure
```

**Run the test binary directly (verbose):**
```bash
./tests/chipz_tests
```

**Run a specific test suite:**
```bash
./tests/chipz_tests --gtest_filter=I2CInterfaceTest.*
./tests/chipz_tests --gtest_filter=ChipTest.*
```

**Run tests matching a pattern:**
```bash
./tests/chipz_tests --gtest_filter=*Transmit*
```

**Clean:**
```bash
cmake --build . --target clean
```

---

## Test Coverage

| Component | Test file | Status |
|---|---|---|
| `CommunicationInterface` | `tests/core/test_communication_interface.cpp` | covered |
| `Chip` / `ChipBase` | `tests/core/test_chip.cpp` | covered |
| `I2CInterface` | `tests/interfaces/test_i2c_interface.cpp` | covered |
| `SPIInterface` | `tests/interfaces/test_spi_interface.cpp` | covered |
| `UARTInterface` | `tests/interfaces/test_uart_interface.cpp` | covered |
| `ParallelInterface` | — | not tested yet |
| `DS3231` | — | not tested yet |
| `MAX6675` | — | not tested yet |
| `PCF8574` | — | not tested yet |
| `HD44780` | — | not tested yet |
| `TJA1145` | — | not tested yet |
| `MCP795W` | — | not tested yet |

---

## Test Framework

chipz uses **Google Test** (fetched automatically via CMake `FetchContent` at configure time — no manual install required). Google Mock is also available via `chipz_test_support`.

---

## Writing Tests

### Testing communication interfaces

Interfaces (`I2CInterface`, `SPIInterface`, `UARTInterface`) accept HAL functions as `std::function` in their constructors. Tests inject lambdas instead of real HAL calls:

```cpp
#include <gtest/gtest.h>
#include <chipz/interfaces/i2c_interface.hpp>

using namespace chipz::interfaces;

TEST(I2CInterfaceTest, TransmitCallsWriteWithCorrectAddress) {
    uint8_t captured_addr = 0;

    I2CInterface i2c(
        [](uint8_t, uint8_t, uint8_t*, uint16_t) -> int { return 0; },
        [&](uint8_t dev, uint8_t mem, const uint8_t* data, uint16_t size) -> int {
            captured_addr = dev;
            return 0;
        }
    );

    i2c.setDeviceAddress(0x48);
    uint8_t payload[] = {0xFF};
    i2c.transmit(payload, 1);

    EXPECT_EQ(captured_addr, 0x48);
}
```

### Testing device drivers

Device drivers inherit from `Chip<CommInterfaces...>`. Test them with real interface instances backed by lambda stubs:

```cpp
#include <gtest/gtest.h>
#include <chipz/interfaces/i2c_interface.hpp>
#include <chipz/devices/ds3231.hpp>

class DS3231Test : public ::testing::Test {
protected:
    chipz::interfaces::I2CInterface i2c{
        [](uint8_t, uint8_t, uint8_t* data, uint16_t size) -> int {
            // Fill data with simulated register values
            return 0;
        },
        [](uint8_t, uint8_t, const uint8_t*, uint16_t) -> int { return 0; }
    };
    DS3231 rtc{i2c};
};

TEST_F(DS3231Test, InitializeSetsStatusToReady) {
    EXPECT_TRUE(rtc.initialize());
    EXPECT_EQ(rtc.getStatus(), chipz::ChipBase::Status::Ready);
}
```

### Adding a test for a device

1. Create `devices/<device>/test_<device>.cpp`
2. Add a `if(CHIPZ_BUILD_TESTS)` block to `devices/<device>/CMakeLists.txt`:

```cmake
if(CHIPZ_BUILD_TESTS)
    add_executable(chipz_test_<device> test_<device>.cpp)
    target_link_libraries(chipz_test_<device> PRIVATE
        chipz::<device>
        chipz_test_support
        GTest::gtest_main
        GTest::gmock
    )
    gtest_discover_tests(chipz_test_<device>)
endif()
```

---

## Best Practices

- **One behavior per test** — keep each `TEST` focused on a single outcome
- **Descriptive names** — `TransmitReturnsFalseWhenBusy` not `Test2`
- **Test error paths** — verify failure returns and interrupt signalling on HAL errors
- **Inject HAL at the boundary** — pass lambda stubs to interface constructors; do not mock internal chipz classes
- **Scope chip instances** — `ChipBase` uses a static registry; let local variables go out of scope to auto-unregister between tests

---

## Continuous Integration

```yaml
name: Tests
on: [push, pull_request]
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build and test
        run: |
          mkdir build && cd build
          cmake -DCHIPZ_BUILD_TESTS=ON ..
          cmake --build .
          ctest --output-on-failure
```

---

## References

- [Google Test documentation](https://google.github.io/googletest/)
- [Google Mock for Dummies](https://google.github.io/googletest/gmock_for_dummies.html)
