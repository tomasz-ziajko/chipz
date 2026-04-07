#ifndef CHIPZ_TESTS_MOCK_SPI_HPP
#define CHIPZ_TESTS_MOCK_SPI_HPP

#include <gmock/gmock.h>
#include <cstdint>
#include <vector>

namespace chipz {
namespace testing {

/**
 * @brief Mock SPI interface for testing SPI-based peripherals
 *
 * This mock can be used to test devices like MAX6675 without actual hardware.
 * It allows you to verify that the driver makes correct SPI calls and
 * to simulate device responses.
 */
class MockSPI {
public:
    virtual ~MockSPI() = default;

    // Mock methods for SPI operations
    MOCK_METHOD(bool, begin, (), ());
    MOCK_METHOD(bool, end, (), ());

    MOCK_METHOD(void, setClockSpeed, (uint32_t speedHz), ());
    MOCK_METHOD(void, setDataMode, (uint8_t mode), ());  // Mode 0-3
    MOCK_METHOD(void, setBitOrder, (bool msbFirst), ());

    MOCK_METHOD(uint8_t, transfer, (uint8_t data), ());
    MOCK_METHOD(void, transfer, (uint8_t* buffer, size_t length), ());

    MOCK_METHOD(void, chipSelect, (bool select), ());  // true = select (low), false = deselect (high)

    MOCK_METHOD(uint16_t, transfer16, (uint16_t data), ());
};

} // namespace testing
} // namespace chipz

#endif // CHIPZ_TESTS_MOCK_SPI_HPP
