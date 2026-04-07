#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <chipz/devices/ds3231.hpp>
#include <chipz/interfaces/i2c_interface.hpp>
#include "mock_i2c.hpp"

using namespace chipz;
using namespace chipz::devices;
using namespace chipz::interfaces;
using namespace chipz::testing;
using ::testing::Return;
using ::testing::_;

class DS3231Test : public ::testing::Test {
protected:
    // Null I2C lambdas — no real hardware; transfers succeed immediately
    I2CInterface i2c{
        0x68,
        [](uint8_t, uint8_t, uint8_t*, uint16_t) { return 0; },
        [](uint8_t, uint8_t, const uint8_t*, uint16_t) { return 0; }
    };
    DS3231 rtc{i2c};
    MockI2C mockI2C;

    void SetUp() override {}
};

TEST_F(DS3231Test, DeviceIdIsCorrect) {
    EXPECT_EQ(rtc.getDeviceId(), "DS3231 RTC");
}

TEST_F(DS3231Test, InitialStatusIsUninitialized) {
    EXPECT_EQ(rtc.getStatus(), PeripheralBase::Status::Uninitialized);
    EXPECT_FALSE(rtc.isReady());
}

TEST_F(DS3231Test, InitializeSetsStatusToReady) {
    EXPECT_TRUE(rtc.initialize());
    EXPECT_EQ(rtc.getStatus(), PeripheralBase::Status::Ready);
    // isReady() requires state == Idle which is only reached after Core routes
    // the initial interrupt — not testable here without a Core instance
}

TEST_F(DS3231Test, SetTimeDoesNotCrash) {
    rtc.initialize();
    std::tm t{};
    t.tm_hour = 14;
    t.tm_min  = 45;
    t.tm_sec  = 30;
    EXPECT_NO_THROW(rtc.setTime(t));
}

TEST_F(DS3231Test, GetTimeReturnsNonNullPointer) {
    rtc.initialize();
    EXPECT_NE(rtc.getTime(), nullptr);
}

TEST_F(DS3231Test, GetTemperatureReturnsTrue) {
    rtc.initialize();
    DS3231::Temperature temp;
    EXPECT_TRUE(rtc.getTemperature(temp));
}

TEST_F(DS3231Test, SetAlarm1DoesNotCrash) {
    rtc.initialize();
    EXPECT_NO_THROW(rtc.setAlarm1(0, 30, 8, 1));
}

TEST_F(DS3231Test, SetAlarm2DoesNotCrash) {
    rtc.initialize();
    EXPECT_NO_THROW(rtc.setAlarm2(30, 8, 1));
}
