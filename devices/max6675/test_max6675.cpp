#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <chipz/devices/max6675.hpp>
#include <chipz/interfaces/spi_interface.hpp>
#include "mock_spi.hpp"

using namespace chipz;
using namespace chipz::devices;
using namespace chipz::interfaces;
using namespace chipz::testing;
using ::testing::Return;
using ::testing::_;

class MAX6675Test : public ::testing::Test {
protected:
    // Null SPI lambda — no real hardware; transfers succeed immediately
    SPIInterface spi{[](uint8_t*, uint8_t*, uint16_t) { return 0; }};
    MAX6675 thermocouple{spi};
    MockSPI mockSPI;

    void SetUp() override {}
};

TEST_F(MAX6675Test, DeviceIdIsCorrect) {
    EXPECT_EQ(thermocouple.getDeviceId(), "MAX6675 Thermocouple Converter");
}

TEST_F(MAX6675Test, InitialStatusIsUninitialized) {
    EXPECT_EQ(thermocouple.getStatus(), PeripheralBase::Status::Uninitialized);
    EXPECT_FALSE(thermocouple.isReady());
}

TEST_F(MAX6675Test, InitializeSetsStatusToReady) {
    EXPECT_TRUE(thermocouple.initialize());
    EXPECT_EQ(thermocouple.getStatus(), PeripheralBase::Status::Ready);
    EXPECT_TRUE(thermocouple.isReady());
}

TEST_F(MAX6675Test, GetTemperatureCelsiusDefaultsToZero) {
    thermocouple.initialize();
    EXPECT_FLOAT_EQ(thermocouple.getTemperatureCelsius(), 0.0f);
}

TEST_F(MAX6675Test, GetTemperatureFahrenheitDefaultsToFreezing) {
    thermocouple.initialize();
    // 0°C = 32°F
    EXPECT_FLOAT_EQ(thermocouple.getTemperatureFahrenheit(), 32.0f);
}

TEST_F(MAX6675Test, IsThermocoupleConnectedDefaultsToTrue) {
    thermocouple.initialize();
    // connection_open_ starts false, so isThermocoupleConnected() returns true
    EXPECT_TRUE(thermocouple.isThermocoupleConnected());
}
