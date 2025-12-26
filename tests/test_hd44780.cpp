#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <chipz/devices/hd44780.hpp>
#include "mock_gpio.hpp"

using namespace chipz;
using namespace chipz::devices;
using namespace chipz::testing;
using ::testing::Return;
using ::testing::_;
using ::testing::AtLeast;

class HD44780Test : public ::testing::Test {
protected:
    HD44780::Config config{
        HD44780::InterfaceMode::Bit4,
        HD44780::DisplaySize::Size16x2,
        true,  // cursor visible
        false  // cursor blink
    };
    HD44780 lcd{config};
    MockGPIO mockGPIO;

    void SetUp() override {
        // Common setup
    }
};

TEST_F(HD44780Test, DeviceIdIsCorrect) {
    EXPECT_EQ(lcd.getDeviceId(), "HD44780 LCD");
}

TEST_F(HD44780Test, InitialStatusIsUninitialized) {
    EXPECT_EQ(lcd.getStatus(), Peripheral::Status::Uninitialized);
    EXPECT_FALSE(lcd.isReady());
}

TEST_F(HD44780Test, InitializeSetsStatusToReady) {
    EXPECT_TRUE(lcd.initialize());
    EXPECT_EQ(lcd.getStatus(), Peripheral::Status::Ready);
    EXPECT_TRUE(lcd.isReady());
}

TEST_F(HD44780Test, ConfigurationStoresCorrectly) {
    HD44780::Config config8bit{
        HD44780::InterfaceMode::Bit8,
        HD44780::DisplaySize::Size20x4,
        false,
        true
    };
    HD44780 lcd8bit(config8bit);

    // The config is stored, though we can't directly test it without getters
    // When implementing, you might want to add configuration accessors
    EXPECT_NO_THROW(lcd8bit.initialize());
}

TEST_F(HD44780Test, ClearReturnsExpectedValue) {
    lcd.initialize();

    // Example of what the test would look like with actual implementation:
    // EXPECT_CALL(mockGPIO, digitalWrite(_, _))
    //     .Times(AtLeast(1));

    // Current mockup returns false
    EXPECT_FALSE(lcd.clear());
}

TEST_F(HD44780Test, SetCursorReturnsExpectedValue) {
    lcd.initialize();

    // Example of what the test would look like with actual implementation:
    // EXPECT_CALL(mockGPIO, digitalWrite(_, _))
    //     .Times(AtLeast(1));

    // Current mockup returns false
    EXPECT_FALSE(lcd.setCursor(0, 0));
    EXPECT_FALSE(lcd.setCursor(1, 15));
}

TEST_F(HD44780Test, PrintReturnsExpectedValue) {
    lcd.initialize();

    // Example of what the test would look like with actual implementation:
    // std::string text = "Hello";
    // EXPECT_CALL(mockGPIO, digitalWrite(_, _))
    //     .Times(AtLeast(text.length()));

    // Current mockup returns false
    EXPECT_FALSE(lcd.print("Hello, World!"));
}

TEST_F(HD44780Test, PrintCharReturnsExpectedValue) {
    lcd.initialize();

    // Current mockup returns false
    EXPECT_FALSE(lcd.printChar('A'));
}

TEST_F(HD44780Test, CreateCustomCharReturnsExpectedValue) {
    lcd.initialize();

    uint8_t heart[8] = {
        0b00000,
        0b01010,
        0b11111,
        0b11111,
        0b01110,
        0b00100,
        0b00000,
        0b00000
    };

    // Current mockup returns false
    EXPECT_FALSE(lcd.createCustomChar(0, heart));
}

// Example test showing GPIO mocking pattern for parallel interface
TEST_F(HD44780Test, DISABLED_ExampleWithGPIOMocking) {
    // This demonstrates how you would mock GPIO operations
    // When implementing the driver, use this as a template

    // Example: Mocking the initialization sequence
    // EXPECT_CALL(mockGPIO, pinMode(_, MockGPIO::PinMode::Output))
    //     .Times(AtLeast(6)); // RS, E, D4-D7

    // EXPECT_CALL(mockGPIO, delayMilliseconds(_))
    //     .Times(AtLeast(1)); // Initial delay

    // Example: Mocking writing a character
    // When writing 'A' (0x41) in 4-bit mode, you'd expect:
    // EXPECT_CALL(mockGPIO, digitalWrite(_, MockGPIO::PinState::High))
    //     .Times(AtLeast(1)); // RS = 1 for data

    // EXPECT_CALL(mockGPIO, digitalWrite(_, MockGPIO::PinState::Low))
    //     .Times(AtLeast(1)); // Enable pulse

    // (You would need to inject mockGPIO into the driver)
    // lcd.initialize(&mockGPIO);
    // lcd.printChar('A');
}
