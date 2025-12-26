#ifndef CHIPZ_TESTS_MOCK_GPIO_HPP
#define CHIPZ_TESTS_MOCK_GPIO_HPP

#include <gmock/gmock.h>
#include <cstdint>

namespace chipz {
namespace testing {

/**
 * @brief Mock GPIO interface for testing parallel peripherals
 *
 * This mock can be used to test devices like HD44780 that use parallel
 * GPIO communication. It allows you to verify pin manipulations and
 * timing requirements.
 */
class MockGPIO {
public:
    enum class PinMode {
        Input,
        Output,
        InputPullup,
        InputPulldown
    };

    enum class PinState {
        Low = 0,
        High = 1
    };

    virtual ~MockGPIO() = default;

    // Mock methods for GPIO operations
    MOCK_METHOD(void, pinMode, (uint8_t pin, PinMode mode), ());
    MOCK_METHOD(void, digitalWrite, (uint8_t pin, PinState state), ());
    MOCK_METHOD(PinState, digitalRead, (uint8_t pin), ());

    MOCK_METHOD(void, delayMicroseconds, (uint32_t us), ());
    MOCK_METHOD(void, delayMilliseconds, (uint32_t ms), ());
};

} // namespace testing
} // namespace chipz

#endif // CHIPZ_TESTS_MOCK_GPIO_HPP
