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
 * Reading period: READ_PERIOD_MS (1000ms)
 *
 * Scheduling:
 *   StartRead — kicks off a 2-byte SPI receive, suspends on WaitCondition::comm.
 *   WaitDelay — entered from onTransferComplete; suspends on WaitCondition::delayMs.
 */
class MAX6675 : public Chip<interfaces::SPIInterface> {
public:
    explicit MAX6675(interfaces::SPIInterface& comm)
        : Chip<interfaces::SPIInterface>(comm)
        , status_(Status::Uninitialized)
        , temperature_(0)
        , connection_open_(false)
        , state_(State::StartRead)
    {}

    bool initialize() override {
        if (!get<interfaces::SPIInterface>().isReady()) {
            status_ = Status::Error;
            return false;
        }
        temperature_    = 0;
        connection_open_ = false;
        state_          = State::StartRead;
        status_         = Status::Ready;
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

    bool main() override { return true; }  // replaced by run()

    WaitCondition run() override {
        if (status_ != Status::Ready) return WaitCondition::demand();

        switch (state_) {
            case State::StartRead:
                if (!receive<interfaces::SPIInterface>(
                        get<interfaces::SPIInterface>().getRxBuffer(),
                        kTransferLength)) {
                    return WaitCondition::immediate();  // bus busy — retry next cycle
                }
                return WaitCondition::comm(get<interfaces::SPIInterface>());

            case State::WaitDelay:
                state_ = State::StartRead;
                return WaitCondition::delayMs(kReadPeriodMs);
        }

        return WaitCondition::immediate();
    }

    uint32_t getTemperature()       const { return temperature_; }
    float    getTemperatureCelsius()    const { return static_cast<float>(temperature_) * kResolution; }
    float    getTemperatureFahrenheit() const { return getTemperatureCelsius() * 9.0f / 5.0f + 32.0f; }
    bool     isThermocoupleConnected()  const { return !connection_open_; }

private:
    enum class State { StartRead, WaitDelay };

    Status   status_;
    uint32_t temperature_;
    bool     connection_open_;
    State    state_;

    static constexpr float    kResolution   = 0.25f;
    static constexpr uint16_t kTransferLength = 2;
    static constexpr uint32_t kReadPeriodMs  = 1000;

    void onTransferComplete(CommunicationInterface& /*which*/, bool success) override {
        if (success) deserialize();
        state_ = State::WaitDelay;  // retry after delay regardless of success
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
