#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <chipz/devices/hd44780.hpp>
#include <chipz/interfaces/spi_interface.hpp>
#include "mock_gpio.hpp"

using namespace chipz;
using namespace chipz::devices;
using namespace chipz::interfaces;
using namespace chipz::testing;
using ::testing::Return;
using ::testing::_;
using ::testing::AtLeast;

// HD44780 uses update_pins_ callback for I/O and does not call comm_.transmit().
// SPIInterface is used here as a structural placeholder; a null transfer lambda
// ensures isReady() reports true without actual hardware.
using LCD = HD44780<SPIInterface>;

class HD44780Test : public ::testing::Test {
protected:
    SPIInterface spi{[](uint8_t*, uint8_t*, uint16_t) { return 0; }};
    LCD::Config config{
        LCD::InterfaceMode::Bit4,
        LCD::DisplaySize::Size16x2,
        true,  // cursor visible
        false  // cursor blink
    };
    LCD lcd{spi, config};
    MockGPIO mockGPIO;

    void SetUp() override {}
};

TEST_F(HD44780Test, DeviceIdIsCorrect) {
    EXPECT_EQ(lcd.getDeviceId(), "HD44780 LCD");
}

TEST_F(HD44780Test, InitialStatusIsUninitialized) {
    EXPECT_EQ(lcd.getStatus(), PeripheralBase::Status::Uninitialized);
    EXPECT_FALSE(lcd.isReady());
}

TEST_F(HD44780Test, InitializeSetsStatusToReady) {
    EXPECT_TRUE(lcd.initialize());
    EXPECT_EQ(lcd.getStatus(), PeripheralBase::Status::Ready);
}

TEST_F(HD44780Test, ConfigurationStoresCorrectly) {
    LCD::Config config8bit{
        LCD::InterfaceMode::Bit8,
        LCD::DisplaySize::Size20x4,
        false,
        true
    };
    LCD lcd8bit{spi, config8bit};
    EXPECT_NO_THROW(lcd8bit.initialize());
}

TEST_F(HD44780Test, WriteBufferReturnsFalseBeforeIdle) {
    // After initialize(), state_ is Ready but still in Initializing (not Idle yet)
    // writeBuffer should return false until state machine reaches Idle
    lcd.initialize();
    char buf[32] = {};
    EXPECT_FALSE(lcd.writeBuffer(buf));
}

TEST_F(HD44780Test, WriteBufferAtPositionReturnsFalseBeforeIdle) {
    lcd.initialize();
    char buf[4] = "Hi!";
    EXPECT_FALSE(lcd.writeBufferAtPosition(buf, 0, 3));
}
