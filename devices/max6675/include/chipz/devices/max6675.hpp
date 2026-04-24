// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_DEVICES_MAX6675_HPP
#define CHIPZ_DEVICES_MAX6675_HPP

#include <chipz/core/chip.hpp>
#include <chipz/interfaces/spi_interface.hpp>
#include <cstdint>
#include <string>

namespace chipz {
namespace devices {

/**
 * @brief Driver for MAX6675 Cold-Junction-Compensated K-Thermocouple-to-Digital Converter
 *
 * Temperature resolution: 0.25°C (12-bit)
 * Update rate: ~220ms (internal MAX6675 conversion)
 * Reading period: kReadPeriodMs (1000ms)
 *
 * Scheduling (coroutine):
 *   Loop: receive (retry immediate if bus busy) → co_yield comm → deserialize → co_yield delayMs.
 */
class MAX6675 : public Chip<interfaces::SPIInterface> {
public:
    explicit MAX6675(interfaces::SPIInterface& comm)
        : Chip<interfaces::SPIInterface>(comm)
        , status_(Status::Uninitialized)
        , temperature_(0)
        , connection_open_(false)
        , last_transfer_ok_(false)
    {}

    bool initialize() override {
        if (!get<interfaces::SPIInterface>().isReady()) {
            status_ = Status::Error;
            return false;
        }
        temperature_     = 0;
        connection_open_ = false;
        last_transfer_ok_ = false;
        status_          = Status::Ready;
        return true;
    }

    bool reset() override {
        status_ = Status::Uninitialized;
        return initialize();
    }

    bool isReady() const override {
        return status_ == Status::Ready && get<interfaces::SPIInterface>().isReady();
    }

    Status getStatus() const override { return status_; }

    std::string getDeviceId() const override { return "MAX6675"; }

    bool main() override { return true; }

    DriverTask run() override {
        while (true) {
            if (status_ != Status::Ready) {
                co_yield WaitCondition::demand();
                continue;
            }
            if (!receive<interfaces::SPIInterface>(
                    get<interfaces::SPIInterface>().getRxBuffer(), kTransferLength)) {
                co_yield WaitCondition::immediate();
                continue;
            }
            co_yield WaitCondition::comm(get<interfaces::SPIInterface>());
            if (last_transfer_ok_) deserialize();
            co_yield WaitCondition::delayMs(kReadPeriodMs);
        }
    }

    uint32_t getTemperature()       const { return temperature_; }
    float    getTemperatureCelsius()    const { return static_cast<float>(temperature_) * kResolution; }
    float    getTemperatureFahrenheit() const { return getTemperatureCelsius() * 9.0f / 5.0f + 32.0f; }
    bool     isThermocoupleConnected()  const { return !connection_open_; }

private:
    Status   status_;
    uint32_t temperature_;
    bool     connection_open_;
    bool     last_transfer_ok_;

    static constexpr float    kResolution   = 0.25f;
    static constexpr uint16_t kTransferLength = 2;
    static constexpr uint32_t kReadPeriodMs  = 1000;

    void onTransferComplete(CommunicationInterface& /*which*/, bool success) override {
        last_transfer_ok_ = success;
    }

    void deserialize() {
        const uint8_t* rx = get<interfaces::SPIInterface>().getRxBuffer();

        // Bits 14:3 hold the 12-bit temperature (MSB first, each LSB = 0.25°C)
        uint32_t raw = (static_cast<uint32_t>(rx[0]) << 8) | rx[1];
        temperature_    = (raw >> 3) & 0x0FFFu;

        connection_open_ = (rx[1] & 0x04) != 0;
    }
};

} // namespace devices
} // namespace chipz

#endif // CHIPZ_DEVICES_MAX6675_HPP
