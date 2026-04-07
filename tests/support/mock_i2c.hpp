#ifndef CHIPZ_TESTS_MOCK_I2C_HPP
#define CHIPZ_TESTS_MOCK_I2C_HPP

#include <gmock/gmock.h>
#include <cstdint>
#include <vector>

namespace chipz {
namespace testing {

/**
 * @brief Mock I2C interface for testing I2C-based peripherals
 *
 * This mock can be used to test devices like DS3231 without actual hardware.
 * It allows you to verify that the driver makes correct I2C calls and
 * to simulate device responses.
 */
class MockI2C {
public:
    virtual ~MockI2C() = default;

    // Mock methods for I2C operations
    MOCK_METHOD(bool, begin, (), ());
    MOCK_METHOD(bool, end, (), ());

    MOCK_METHOD(bool, writeRegister, (uint8_t deviceAddress, uint8_t registerAddress, uint8_t value), ());
    MOCK_METHOD(bool, readRegister, (uint8_t deviceAddress, uint8_t registerAddress, uint8_t& value), ());

    MOCK_METHOD(bool, writeBytes, (uint8_t deviceAddress, const uint8_t* data, size_t length), ());
    MOCK_METHOD(bool, readBytes, (uint8_t deviceAddress, uint8_t* buffer, size_t length), ());

    MOCK_METHOD(bool, writeRegisterMulti, (uint8_t deviceAddress, uint8_t registerAddress, const uint8_t* data, size_t length), ());
    MOCK_METHOD(bool, readRegisterMulti, (uint8_t deviceAddress, uint8_t registerAddress, uint8_t* buffer, size_t length), ());

    MOCK_METHOD(bool, isDeviceConnected, (uint8_t deviceAddress), ());
};

} // namespace testing
} // namespace chipz

#endif // CHIPZ_TESTS_MOCK_I2C_HPP
