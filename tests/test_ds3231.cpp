#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <chipz/devices/ds3231.hpp>
#include "mock_i2c.hpp"

using namespace chipz;
using namespace chipz::devices;
using namespace chipz::testing;
using ::testing::Return;
using ::testing::_;
using ::testing::SetArgReferee;

class DS3231Test : public ::testing::Test {
protected:
    DS3231 rtc;
    MockI2C mockI2C;

    void SetUp() override {
        // Common setup can go here
    }
};

TEST_F(DS3231Test, DeviceIdIsCorrect) {
    EXPECT_EQ(rtc.getDeviceId(), "DS3231 RTC");
}

TEST_F(DS3231Test, InitialStatusIsUninitialized) {
    EXPECT_EQ(rtc.getStatus(), Peripheral::Status::Uninitialized);
    EXPECT_FALSE(rtc.isReady());
}

TEST_F(DS3231Test, InitializeSetsStatusToReady) {
    // Note: This is a mockup driver, so initialize() doesn't actually use I2C yet
    // When you implement the actual driver, you would mock I2C calls here

    // Example of what the test would look like with actual I2C implementation:
    // EXPECT_CALL(mockI2C, isDeviceConnected(0x68))
    //     .WillOnce(Return(true));

    EXPECT_TRUE(rtc.initialize());
    EXPECT_EQ(rtc.getStatus(), Peripheral::Status::Ready);
    EXPECT_TRUE(rtc.isReady());
}

TEST_F(DS3231Test, SetDateTimeReturnsExpectedValue) {
    rtc.initialize();

    DS3231::DateTime dt{
        30,   // seconds
        45,   // minutes
        14,   // hours
        5,    // day of week
        26,   // day
        12,   // month
        2025  // year
    };

    // Example of what the test would look like with actual implementation:
    // EXPECT_CALL(mockI2C, writeRegisterMulti(0x68, 0x00, _, 7))
    //     .WillOnce(Return(true));

    // Current mockup returns false
    EXPECT_FALSE(rtc.setDateTime(dt));
}

TEST_F(DS3231Test, GetDateTimeReturnsExpectedValue) {
    rtc.initialize();

    DS3231::DateTime dt;

    // Example of what the test would look like with actual implementation:
    // EXPECT_CALL(mockI2C, readRegisterMulti(0x68, 0x00, _, 7))
    //     .WillOnce(DoAll(
    //         // Set buffer with BCD-encoded time data
    //         Return(true)
    //     ));

    // Current mockup returns false
    EXPECT_FALSE(rtc.getDateTime(dt));
}

TEST_F(DS3231Test, GetTemperatureReturnsExpectedValue) {
    rtc.initialize();

    DS3231::Temperature temp;

    // Example of what the test would look like with actual implementation:
    // EXPECT_CALL(mockI2C, readRegisterMulti(0x68, 0x11, _, 2))
    //     .WillOnce(DoAll(
    //         // Set buffer with temperature data (e.g., 25.75°C)
    //         Return(true)
    //     ));

    // Current mockup returns false
    EXPECT_FALSE(rtc.getTemperature(temp));
}

TEST_F(DS3231Test, EnableAlarmReturnsExpectedValue) {
    rtc.initialize();

    // Example of what the test would look like with actual implementation:
    // EXPECT_CALL(mockI2C, writeRegister(0x68, 0x0E, _))
    //     .WillOnce(Return(true));

    // Current mockup returns false
    EXPECT_FALSE(rtc.enableAlarm(1));
    EXPECT_FALSE(rtc.enableAlarm(2));
}

// This test demonstrates how you would test with actual I2C mocking
TEST_F(DS3231Test, DISABLED_ExampleWithI2CMocking) {
    // This is disabled because the driver isn't implemented yet
    // When you implement the driver, enable this test and use it as a template

    // Setup: Expect I2C device detection during initialization
    EXPECT_CALL(mockI2C, isDeviceConnected(0x68))
        .WillOnce(Return(true));

    // Initialize the device
    // (You would need to inject mockI2C into the driver)
    // EXPECT_TRUE(rtc.initialize(&mockI2C));

    // Setup: Expect reading time registers
    EXPECT_CALL(mockI2C, readRegisterMulti(0x68, 0x00, _, 7))
        .WillOnce([](uint8_t addr, uint8_t reg, uint8_t* buffer, size_t len) {
            // Simulate reading 2025-12-26 14:45:30 Thursday
            buffer[0] = 0x30; // seconds (BCD)
            buffer[1] = 0x45; // minutes (BCD)
            buffer[2] = 0x14; // hours (BCD)
            buffer[3] = 0x05; // day of week
            buffer[4] = 0x26; // day (BCD)
            buffer[5] = 0x12; // month (BCD)
            buffer[6] = 0x25; // year (BCD)
            return true;
        });

    DS3231::DateTime dt;
    // EXPECT_TRUE(rtc.getDateTime(dt));
    // EXPECT_EQ(dt.seconds, 30);
    // EXPECT_EQ(dt.minutes, 45);
    // EXPECT_EQ(dt.hours, 14);
}
