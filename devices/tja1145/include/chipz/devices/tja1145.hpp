// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_DEVICES_TJA1145_HPP
#define CHIPZ_DEVICES_TJA1145_HPP

#include <chipz/chip.hpp>
#include <chipz/interfaces/spi_interface.hpp>
#include <cstdint>
#include <functional>

namespace chipz {
namespace devices {

/**
 * @brief Driver for TJA1145 CAN High-Speed Transceiver with Partial Networking
 *
 * The TJA1145 is a CAN transceiver with selective wake-up functionality
 * and partial networking support for low-power applications.
 *
 * This implementation mirrors the design from the C version,
 * maintaining the exact sequence logic required for correct operation.
 *
 * @tparam CommInterface Communication interface type (typically SPI)
 */
class TJA1145 : public Chip<interfaces::SPIInterface> {
public:
    enum class State {
        Off,
        NormalRequested,
        Normal,
        SleepRequested,
        Sleep,
        Standby
    };

    struct Config {
        bool enableWakeupOnCan;
        bool wakeupOnExtendedId;
    };

    /**
     * @brief Construct TJA1145 driver with communication interface
     * @param comm Reference to communication interface (SPI)
     * @param config Configuration parameters
     * @param get_spi_transmission_disabled Function to check if SPI transmission is disabled
     */
    TJA1145(interfaces::SPIInterface& comm,
            const Config& config,
            std::function<uint8_t()> get_spi_transmission_disabled = nullptr)
        : Chip<interfaces::SPIInterface>(comm)
        , status_(Status::Uninitialized)
        , state_(State::Standby)
        , config_(config)
        , request_power_down_(false)
        , power_down_permission_(false)
        , state_check_requested_(false)
        , can_data_rate_set_(false)
        , can_extended_format_set_(false)
        , can_control_updated_(false)
        , event_enabled_(false)
        , system_event_checked_(false)
        , system_event_check_requested_(false)
        , clear_system_event_request_(false)
        , transceiver_event_checked_(false)
        , transceiver_event_check_requested_(false)
        , clear_transceiver_event_request_(false)
        , wake_up_pin_event_checked_(false)
        , wakeup_pin_event_check_requested_(false)
        , clear_wake_up_pin_event_request_(false)
        , get_spi_transmission_disabled_(get_spi_transmission_disabled)
    {
    }

    // Chip interface implementation
    bool initialize() override {
        if (!comm_.isReady()) {
            status_ = Status::Error;
            return false;
        }

        // Reset state
        state_ = State::Standby;
        request_power_down_ = false;
        power_down_permission_ = false;
        state_check_requested_ = false;
        can_data_rate_set_ = false;
        can_extended_format_set_ = false;
        can_control_updated_ = false;
        event_enabled_ = false;
        system_event_checked_ = false;
        system_event_check_requested_ = false;
        clear_system_event_request_ = false;
        transceiver_event_checked_ = false;
        transceiver_event_check_requested_ = false;
        clear_transceiver_event_request_ = false;
        wake_up_pin_event_checked_ = false;
        wakeup_pin_event_check_requested_ = false;
        clear_wake_up_pin_event_request_ = false;

        status_ = Status::Ready;
        return true;
    }

    bool reset() override {
        status_ = Status::Uninitialized;
        return initialize();
    }

    bool isReady() const override {
        return status_ == Status::Ready && comm_.isReady() &&
               (state_ == State::Normal || state_ == State::Sleep);
    }

    Status getStatus() const override {
        return status_;
    }

    std::string getDeviceId() const override {
        return "TJA1145 CAN Transceiver";
    }

    bool main() override {
        if (status_ != Status::Ready) {
            return false;
        }

        if (!request_power_down_) {
            // Normal operation mode requested
            can_data_rate_set_ = false;
            can_control_updated_ = false;
            can_extended_format_set_ = false;
            event_enabled_ = false;

            system_event_checked_ = false;
            system_event_check_requested_ = false;
            clear_system_event_request_ = false;
            transceiver_event_checked_ = false;
            transceiver_event_check_requested_ = false;
            clear_transceiver_event_request_ = false;
            wake_up_pin_event_checked_ = false;
            wakeup_pin_event_check_requested_ = false;
            clear_wake_up_pin_event_request_ = false;

            power_down_permission_ = false;

            if (get_spi_transmission_disabled_ && get_spi_transmission_disabled_()) {
                return true;
            }

            if (State::Normal == state_) {
                return true;
            }

            if (State::NormalRequested == state_) {
                checkState();
                return true;
            }

            requestNormalMode();
        }
        else {
            // Power down requested
            if (!config_.enableWakeupOnCan) {
                power_down_permission_ = true;
            }

            if (get_spi_transmission_disabled_ && get_spi_transmission_disabled_()) {
                return true;
            }

            if (State::Sleep == state_) {
                power_down_permission_ = true;
                return true;
            }

            if (State::Standby == state_ || State::Off == state_) {
                requestNormalMode();
                transceiver_event_checked_ = false;
                system_event_checked_ = false;
                event_enabled_ = false;
            }
            else if (State::NormalRequested == state_) {
                checkState();
            }
            else if (State::Normal == state_) {
                // Execute configuration sequence (MUST maintain this order!)
                if (clear_system_event_request_) {
                    clear_system_event_request_ = false;
                    clearSystemEventFlag();
                }
                else if (clear_transceiver_event_request_) {
                    clear_transceiver_event_request_ = false;
                    clearTransceiverEventFlag();
                }
                else if (!can_control_updated_) {
                    can_control_updated_ = true;
                    setCanControl();
                }
                else if (!can_data_rate_set_) {
                    can_data_rate_set_ = true;
                    setDataRate();
                }
                else if (!event_enabled_) {
                    event_enabled_ = true;
                    setEventEnable();
                }
                else if (!can_extended_format_set_) {
                    can_extended_format_set_ = true;
                    setCanExtendedDataFormat();
                }
                else if (!system_event_checked_) {
                    system_event_checked_ = true;
                    requestSystemEventCheck();
                }
                else if (!transceiver_event_checked_) {
                    transceiver_event_checked_ = true;
                    requestTransceiverEventCheck();
                }
                else {
                    requestSleepMode();
                }
            }
            else if (State::SleepRequested == state_) {
                checkState();
            }
        }

        return true;
    }

    // TJA1145-specific interface

    /**
     * @brief Get current transceiver state
     * @return Current state
     */
    State getState() const {
        return state_;
    }

    /**
     * @brief Request power down mode
     * @param power_down_request true to request power down, false for normal operation
     */
    void requestPowerDown(bool power_down_request) {
        request_power_down_ = power_down_request;
    }

    /**
     * @brief Get power down permission status
     * @return true if power down is permitted
     */
    bool getPowerDownPermission() const {
        return power_down_permission_;
    }

    /**
     * @brief Request state change to normal mode
     */
    void requestStateChangeToNormalMode() {
        requestNormalMode();
    }

    /**
     * @brief Request state change to sleep mode
     */
    void requestStateChangeToSleepMode() {
        requestSleepMode();
    }

    /**
     * @brief Request state check
     */
    void requestStateCheck() {
        checkState();
    }

private:
    Status status_;
    State state_;
    Config config_;

    // State and request flags
    bool request_power_down_;
    bool power_down_permission_;
    bool state_check_requested_;

    // Configuration flags
    bool can_data_rate_set_;
    bool can_extended_format_set_;
    bool can_control_updated_;
    bool event_enabled_;

    // Event checking flags
    bool system_event_checked_;
    bool system_event_check_requested_;
    bool clear_system_event_request_;

    bool transceiver_event_checked_;
    bool transceiver_event_check_requested_;
    bool clear_transceiver_event_request_;

    bool wake_up_pin_event_checked_;
    bool wakeup_pin_event_check_requested_;
    bool clear_wake_up_pin_event_request_;

    std::function<uint8_t()> get_spi_transmission_disabled_;

    // TJA1145 Register addresses
    static constexpr uint8_t READ_ONLY_BIT = 0x01;
    static constexpr uint8_t REGISTER_MODE_CONTROL = 0x01;
    static constexpr uint8_t REGISTER_CAN_CONTROL = 0x20;
    static constexpr uint8_t REGISTER_TRANSCEIVER_EVENT_ENABLE = 0x23;
    static constexpr uint8_t REGISTER_DATA_RATE = 0x26;
    static constexpr uint8_t REGISTER_FRAME_CONTROL = 0x2F;
    static constexpr uint8_t REGISTER_SYSTEM_EVENT_STATUS = 0x61;
    static constexpr uint8_t REGISTER_TRANSCEIVER_EVENT_STATUS = 0x63;
    static constexpr uint8_t REGISTER_WAKE_PIN_EVENT_STATUS = 0x64;
    static constexpr uint8_t REGISTER_EVENT = 0x60;

    // TJA1145 Mode values
    static constexpr uint8_t MODE_NORMAL = 0x07;
    static constexpr uint8_t MODE_STANDBY = 0x04;
    static constexpr uint8_t MODE_SLEEP = 0x01;

    /**
     * @brief SPI transfer completion callback
     * @param success True if transfer succeeded, false on error
     */
    void onTransferComplete(bool success) override {
        if (!success) {
            status_ = Status::Error;
            return;
        }

        const uint8_t* rx_buffer = comm_.getRxBuffer();

        // Process callback based on what was requested
        if (system_event_check_requested_) {
            system_event_check_requested_ = false;
            if (rx_buffer[1] != 0) {
                clear_system_event_request_ = true;
            }
        }
        else if (transceiver_event_check_requested_) {
            transceiver_event_check_requested_ = false;
            if (rx_buffer[1] != 0) {
                clear_transceiver_event_request_ = true;
            }
        }
        else if (wakeup_pin_event_check_requested_) {
            wakeup_pin_event_check_requested_ = false;
            if (rx_buffer[1] != 0) {
                clear_wake_up_pin_event_request_ = true;
            }
        }
        else if (state_check_requested_) {
            if (MODE_NORMAL == (rx_buffer[1] & 0x07)) {
                state_ = State::Normal;
            }
            else if (MODE_SLEEP == (rx_buffer[1] & 0x07)) {
                state_ = State::Sleep;
            }
            else if (MODE_STANDBY == (rx_buffer[1] & 0x07)) {
                state_ = State::Standby;
            }
            state_check_requested_ = false;
        }
    }

    /**
     * @brief Request state change to normal mode
     */
    void requestNormalMode() {
        state_ = State::NormalRequested;
        uint8_t* tx_buffer = comm_.getTxBuffer();
        tx_buffer[0] = (REGISTER_MODE_CONTROL << 1) & (~READ_ONLY_BIT);
        tx_buffer[1] = MODE_NORMAL;
        this->transmit(tx_buffer, 2);
    }

    /**
     * @brief Request state change to sleep mode
     */
    void requestSleepMode() {
        state_ = State::SleepRequested;
        uint8_t* tx_buffer = comm_.getTxBuffer();
        tx_buffer[0] = (REGISTER_MODE_CONTROL << 1) & (~READ_ONLY_BIT);
        tx_buffer[1] = MODE_SLEEP;
        this->transmit(tx_buffer, 2);
    }

    /**
     * @brief Request state check from device
     */
    void checkState() {
        state_check_requested_ = true;
        uint8_t* tx_buffer = comm_.getTxBuffer();
        tx_buffer[0] = (REGISTER_MODE_CONTROL << 1) | (READ_ONLY_BIT);
        tx_buffer[1] = 0;
        this->transmit(tx_buffer, 2);
    }

    /**
     * @brief Request system event check
     */
    void requestSystemEventCheck() {
        system_event_check_requested_ = true;
        uint8_t* tx_buffer = comm_.getTxBuffer();
        tx_buffer[0] = (REGISTER_SYSTEM_EVENT_STATUS << 1) | (READ_ONLY_BIT);
        tx_buffer[1] = 0;
        this->transmit(tx_buffer, 2);
    }

    /**
     * @brief Request transceiver event check
     */
    void requestTransceiverEventCheck() {
        transceiver_event_check_requested_ = true;
        uint8_t* tx_buffer = comm_.getTxBuffer();
        tx_buffer[0] = (REGISTER_TRANSCEIVER_EVENT_STATUS << 1) | (READ_ONLY_BIT);
        tx_buffer[1] = 0;
        this->transmit(tx_buffer, 2);
    }

    /**
     * @brief Set CAN data rate
     */
    void setDataRate() {
        uint8_t* tx_buffer = comm_.getTxBuffer();
        tx_buffer[0] = (REGISTER_DATA_RATE << 1) & (~READ_ONLY_BIT);
        tx_buffer[1] = 0;
        this->transmit(tx_buffer, 2);
    }

    /**
     * @brief Set CAN control register
     */
    void setCanControl() {
        uint8_t* tx_buffer = comm_.getTxBuffer();
        tx_buffer[0] = (REGISTER_CAN_CONTROL << 1) & (~READ_ONLY_BIT);
        tx_buffer[1] = 0b00110001;
        this->transmit(tx_buffer, 2);
    }

    /**
     * @brief Set event enable register
     */
    void setEventEnable() {
        uint8_t* tx_buffer = comm_.getTxBuffer();
        if (config_.enableWakeupOnCan) {
            tx_buffer[0] = (REGISTER_TRANSCEIVER_EVENT_ENABLE << 1) & (~READ_ONLY_BIT);
        }
        else {
            tx_buffer[0] = (0x4C << 1) & (~READ_ONLY_BIT);
        }
        tx_buffer[1] = 0b00000001;
        this->transmit(tx_buffer, 2);
    }

    /**
     * @brief Set CAN extended data format
     */
    void setCanExtendedDataFormat() {
        uint8_t* tx_buffer = comm_.getTxBuffer();
        tx_buffer[0] = (REGISTER_FRAME_CONTROL << 1) & (~READ_ONLY_BIT);
        tx_buffer[1] = 0x80;
        this->transmit(tx_buffer, 2);
    }

    /**
     * @brief Clear system event flag
     */
    void clearSystemEventFlag() {
        uint8_t* tx_buffer = comm_.getTxBuffer();
        tx_buffer[0] = (REGISTER_SYSTEM_EVENT_STATUS << 1) & (~READ_ONLY_BIT);
        tx_buffer[1] = 0x16;
        this->transmit(tx_buffer, 2);
    }

    /**
     * @brief Clear transceiver event flag
     */
    void clearTransceiverEventFlag() {
        uint8_t* tx_buffer = comm_.getTxBuffer();
        tx_buffer[0] = (REGISTER_TRANSCEIVER_EVENT_STATUS << 1) & (~READ_ONLY_BIT);
        tx_buffer[1] = 0x33;
        this->transmit(tx_buffer, 2);
    }
};

} // namespace devices
} // namespace chipz

#endif // CHIPZ_DEVICES_TJA1145_HPP
