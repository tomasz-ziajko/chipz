// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_DEVICES_TJA1145_HPP
#define CHIPZ_DEVICES_TJA1145_HPP

#include <chipz/core/chip.hpp>
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
 * Scheduling:
 *   !request_power_down_: resets config/event flags, drives Standby→NormalRequested→Normal.
 *     Normal reached → WaitCondition::demand(). Wake via Core::wake() after requestPowerDown(true).
 *   request_power_down_: drives Normal through config sequence then →SleepRequested→Sleep.
 *     Sleep reached → WaitCondition::demand(). Wake via Core::wake() after requestPowerDown(false).
 *   SPI disabled at any point → WaitCondition::delayMs(kPollPeriodMs).
 *   spi_op_ tracks the in-flight read type for onTransferComplete() decoding (replaces _requested_ bools).
 *
 * Note: requestStateChangeToNormalMode() / requestStateChangeToSleepMode() issue SPI directly
 * and bypass run() scheduling — caller is responsible for ensuring Core::service() drives follow-up.
 */
class TJA1145 : public Chip<CommunicationInterface> {
    using SPI          = CommunicationInterface;
    using ConnectionId = CommunicationInterface::ConnectionId;
    using Status       = ChipBase::Status;

    public:
    static constexpr size_t kMaxTransfer = 2;

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
    TJA1145(SPI& comm, ConnectionId connection_id, const Config& config,
            std::function<uint8_t()> get_spi_transmission_disabled = nullptr) :
        Chip<SPI>(comm),
        connection_id_(connection_id),
        status_(Status::Uninitialized),
        state_(State::Standby),
        config_(config),
        request_power_down_(false),
        power_down_permission_(false),
        can_data_rate_set_(false),
        can_extended_format_set_(false),
        can_control_updated_(false),
        event_enabled_(false),
        system_event_checked_(false),
        clear_system_event_request_(false),
        transceiver_event_checked_(false),
        clear_transceiver_event_request_(false),
        wake_up_pin_event_checked_(false),
        clear_wake_up_pin_event_request_(false),
        spi_op_(SpiOp::None),
        get_spi_transmission_disabled_(get_spi_transmission_disabled)
    {
    }

    // Chip interface implementation
    bool initialize() override
    {
        if (!this->template get<SPI>().isReady()) {
            status_ = Status::Error;
            return false;
        }

        this->template setConnection<SPI>(connection_id_);

        // Reset state
        state_                           = State::Standby;
        request_power_down_              = false;
        power_down_permission_           = false;
        can_data_rate_set_               = false;
        can_extended_format_set_         = false;
        can_control_updated_             = false;
        event_enabled_                   = false;
        system_event_checked_            = false;
        clear_system_event_request_      = false;
        transceiver_event_checked_       = false;
        clear_transceiver_event_request_ = false;
        wake_up_pin_event_checked_       = false;
        clear_wake_up_pin_event_request_ = false;
        spi_op_                          = SpiOp::None;

        status_ = Status::Ready;
        return true;
    }

    bool reset() override
    {
        status_ = Status::Uninitialized;
        return initialize();
    }

    bool isReady() const override
    {
        return status_ == Status::Ready && this->template get<SPI>().isReady() &&
               (state_ == State::Normal || state_ == State::Sleep);
    }

    Status getStatus() const override
    {
        return status_;
    }

    std::string getDeviceId() const override
    {
        return "TJA1145 CAN Transceiver";
    }

    bool main() override
    {
        return true;
    }

    DriverTask run() override
    {
        while (true) {
            if (status_ != Status::Ready) {
                co_yield WaitCondition::demand();
                continue;
            }

            if (!request_power_down_) {
                can_data_rate_set_               = false;
                can_control_updated_             = false;
                can_extended_format_set_         = false;
                event_enabled_                   = false;
                system_event_checked_            = false;
                clear_system_event_request_      = false;
                transceiver_event_checked_       = false;
                clear_transceiver_event_request_ = false;
                wake_up_pin_event_checked_       = false;
                clear_wake_up_pin_event_request_ = false;
                power_down_permission_           = false;

                if (get_spi_transmission_disabled_ && get_spi_transmission_disabled_()) {
                    co_yield WaitCondition::delayMs(kPollPeriodMs);
                    continue;
                }
                if (state_ == State::Normal) {
                    co_yield WaitCondition::demand();
                    continue;
                }
                if (state_ == State::NormalRequested) {
                    checkState();
                    co_yield WaitCondition::comm(this->template get<SPI>());
                    continue;
                }
                requestNormalMode();
                co_yield WaitCondition::comm(this->template get<SPI>());
                continue;
            }

            // Power-down sequence
            if (!config_.enableWakeupOnCan) {
                power_down_permission_ = true;
            }

            if (get_spi_transmission_disabled_ && get_spi_transmission_disabled_()) {
                co_yield WaitCondition::delayMs(kPollPeriodMs);
                continue;
            }
            if (state_ == State::Sleep) {
                power_down_permission_ = true;
                co_yield WaitCondition::demand();
                continue;
            }
            if (state_ == State::Standby || state_ == State::Off) {
                transceiver_event_checked_ = false;
                system_event_checked_      = false;
                event_enabled_             = false;
                requestNormalMode();
                co_yield WaitCondition::comm(this->template get<SPI>());
                continue;
            }
            if (state_ == State::NormalRequested) {
                checkState();
                co_yield WaitCondition::comm(this->template get<SPI>());
                continue;
            }
            if (state_ == State::Normal) {
                // Execute configuration sequence (MUST maintain this order!)
                if (clear_system_event_request_) {
                    clear_system_event_request_ = false;
                    clearSystemEventFlag();
                    co_yield WaitCondition::comm(this->template get<SPI>());
                    continue;
                }
                if (clear_transceiver_event_request_) {
                    clear_transceiver_event_request_ = false;
                    clearTransceiverEventFlag();
                    co_yield WaitCondition::comm(this->template get<SPI>());
                    continue;
                }
                if (!can_control_updated_) {
                    can_control_updated_ = true;
                    setCanControl();
                    co_yield WaitCondition::comm(this->template get<SPI>());
                    continue;
                }
                if (!can_data_rate_set_) {
                    can_data_rate_set_ = true;
                    setDataRate();
                    co_yield WaitCondition::comm(this->template get<SPI>());
                    continue;
                }
                if (!event_enabled_) {
                    event_enabled_ = true;
                    setEventEnable();
                    co_yield WaitCondition::comm(this->template get<SPI>());
                    continue;
                }
                if (!can_extended_format_set_) {
                    can_extended_format_set_ = true;
                    setCanExtendedDataFormat();
                    co_yield WaitCondition::comm(this->template get<SPI>());
                    continue;
                }
                if (!system_event_checked_) {
                    system_event_checked_ = true;
                    requestSystemEventCheck();
                    co_yield WaitCondition::comm(this->template get<SPI>());
                    continue;
                }
                if (!transceiver_event_checked_) {
                    transceiver_event_checked_ = true;
                    requestTransceiverEventCheck();
                    co_yield WaitCondition::comm(this->template get<SPI>());
                    continue;
                }
                requestSleepMode();
                co_yield WaitCondition::comm(this->template get<SPI>());
                continue;
            }
            if (state_ == State::SleepRequested) {
                checkState();
                co_yield WaitCondition::comm(this->template get<SPI>());
                continue;
            }

            co_yield WaitCondition::demand();
        }
    }

    // TJA1145-specific interface

    /**
     * @brief Get current transceiver state
     * @return Current state
     */
    State getState() const
    {
        return state_;
    }

    /**
     * @brief Request power down mode
     * @param power_down_request true to request power down, false for normal operation
     */
    void requestPowerDown(bool power_down_request)
    {
        request_power_down_ = power_down_request;
    }

    /**
     * @brief Get power down permission status
     * @return true if power down is permitted
     */
    bool getPowerDownPermission() const
    {
        return power_down_permission_;
    }

    /**
     * @brief Request state change to normal mode
     */
    void requestStateChangeToNormalMode()
    {
        requestNormalMode();
    }

    /**
     * @brief Request state change to sleep mode
     */
    void requestStateChangeToSleepMode()
    {
        requestSleepMode();
    }

    /**
     * @brief Request state check
     */
    void requestStateCheck()
    {
        checkState();
    }

    private:
    ConnectionId connection_id_;
    Status       status_;
    State        state_;
    Config       config_;

    enum class SpiOp {
        None,
        CheckingState,
        CheckingSystemEvent,
        CheckingTransceiverEvent,
        CheckingWakeupPinEvent,
    };

    // State and request flags
    bool request_power_down_;
    bool power_down_permission_;

    // Configuration done-flags (reset on each !request_power_down_ run() entry)
    bool can_data_rate_set_;
    bool can_extended_format_set_;
    bool can_control_updated_;
    bool event_enabled_;

    // Event checking done-flags and clear requests
    bool system_event_checked_;
    bool clear_system_event_request_;
    bool transceiver_event_checked_;
    bool clear_transceiver_event_request_;
    bool wake_up_pin_event_checked_;
    bool clear_wake_up_pin_event_request_;

    SpiOp spi_op_;

    std::function<uint8_t()> get_spi_transmission_disabled_;

    static constexpr uint32_t kPollPeriodMs = 10;

    // TJA1145 Register addresses
    static constexpr uint8_t READ_ONLY_BIT                     = 0x01;
    static constexpr uint8_t REGISTER_MODE_CONTROL             = 0x01;
    static constexpr uint8_t REGISTER_CAN_CONTROL              = 0x20;
    static constexpr uint8_t REGISTER_TRANSCEIVER_EVENT_ENABLE = 0x23;
    static constexpr uint8_t REGISTER_DATA_RATE                = 0x26;
    static constexpr uint8_t REGISTER_FRAME_CONTROL            = 0x2F;
    static constexpr uint8_t REGISTER_SYSTEM_EVENT_STATUS      = 0x61;
    static constexpr uint8_t REGISTER_TRANSCEIVER_EVENT_STATUS = 0x63;
    static constexpr uint8_t REGISTER_WAKE_PIN_EVENT_STATUS    = 0x64;
    static constexpr uint8_t REGISTER_EVENT                    = 0x60;

    // TJA1145 Mode values
    static constexpr uint8_t MODE_NORMAL  = 0x07;
    static constexpr uint8_t MODE_STANDBY = 0x04;
    static constexpr uint8_t MODE_SLEEP   = 0x01;

    /**
     * @brief SPI transfer completion callback
     * @param success True if transfer succeeded, false on error
     */
    void onTransferComplete(CommunicationInterface& /*which*/, bool success) override
    {
        if (!success) {
            status_ = Status::Error;
            spi_op_ = SpiOp::None;
            return;
        }

        const uint8_t* rx_buffer = this->template get<SPI>().getRxBuffer();

        switch (spi_op_) {
            case SpiOp::CheckingState:
                if ((rx_buffer[1] & 0x07) == MODE_NORMAL) {
                    state_ = State::Normal;
                }
                else if ((rx_buffer[1] & 0x07) == MODE_SLEEP) {
                    state_ = State::Sleep;
                }
                else if ((rx_buffer[1] & 0x07) == MODE_STANDBY) {
                    state_ = State::Standby;
                }
                break;
            case SpiOp::CheckingSystemEvent:
                if (rx_buffer[1] != 0) {
                    clear_system_event_request_ = true;
                }
                break;
            case SpiOp::CheckingTransceiverEvent:
                if (rx_buffer[1] != 0) {
                    clear_transceiver_event_request_ = true;
                }
                break;
            case SpiOp::CheckingWakeupPinEvent:
                if (rx_buffer[1] != 0) {
                    clear_wake_up_pin_event_request_ = true;
                }
                break;
            case SpiOp::None:
                break;
        }
        spi_op_ = SpiOp::None;
    }

    /**
     * @brief Request state change to normal mode
     */
    void requestNormalMode()
    {
        state_             = State::NormalRequested;
        uint8_t* tx_buffer = this->template get<SPI>().getTxBuffer();
        tx_buffer[0]       = (REGISTER_MODE_CONTROL << 1) & (~READ_ONLY_BIT);
        tx_buffer[1]       = MODE_NORMAL;
        this->template transmit<SPI>(tx_buffer, 2);
    }

    /**
     * @brief Request state change to sleep mode
     */
    void requestSleepMode()
    {
        state_             = State::SleepRequested;
        uint8_t* tx_buffer = this->template get<SPI>().getTxBuffer();
        tx_buffer[0]       = (REGISTER_MODE_CONTROL << 1) & (~READ_ONLY_BIT);
        tx_buffer[1]       = MODE_SLEEP;
        this->template transmit<SPI>(tx_buffer, 2);
    }

    /**
     * @brief Request state check from device
     */
    void checkState()
    {
        spi_op_            = SpiOp::CheckingState;
        uint8_t* tx_buffer = this->template get<SPI>().getTxBuffer();
        tx_buffer[0]       = (REGISTER_MODE_CONTROL << 1) | (READ_ONLY_BIT);
        tx_buffer[1]       = 0;
        this->template transmit<SPI>(tx_buffer, 2);
    }

    /**
     * @brief Request system event check
     */
    void requestSystemEventCheck()
    {
        spi_op_            = SpiOp::CheckingSystemEvent;
        uint8_t* tx_buffer = this->template get<SPI>().getTxBuffer();
        tx_buffer[0]       = (REGISTER_SYSTEM_EVENT_STATUS << 1) | (READ_ONLY_BIT);
        tx_buffer[1]       = 0;
        this->template transmit<SPI>(tx_buffer, 2);
    }

    /**
     * @brief Request transceiver event check
     */
    void requestTransceiverEventCheck()
    {
        spi_op_            = SpiOp::CheckingTransceiverEvent;
        uint8_t* tx_buffer = this->template get<SPI>().getTxBuffer();
        tx_buffer[0]       = (REGISTER_TRANSCEIVER_EVENT_STATUS << 1) | (READ_ONLY_BIT);
        tx_buffer[1]       = 0;
        this->template transmit<SPI>(tx_buffer, 2);
    }

    /**
     * @brief Set CAN data rate
     */
    void setDataRate()
    {
        uint8_t* tx_buffer = this->template get<SPI>().getTxBuffer();
        tx_buffer[0]       = (REGISTER_DATA_RATE << 1) & (~READ_ONLY_BIT);
        tx_buffer[1]       = 0;
        this->template transmit<SPI>(tx_buffer, 2);
    }

    /**
     * @brief Set CAN control register
     */
    void setCanControl()
    {
        uint8_t* tx_buffer = this->template get<SPI>().getTxBuffer();
        tx_buffer[0]       = (REGISTER_CAN_CONTROL << 1) & (~READ_ONLY_BIT);
        tx_buffer[1]       = 0b00110001;
        this->template transmit<SPI>(tx_buffer, 2);
    }

    /**
     * @brief Set event enable register
     */
    void setEventEnable()
    {
        uint8_t* tx_buffer = this->template get<SPI>().getTxBuffer();
        if (config_.enableWakeupOnCan) {
            tx_buffer[0] = (REGISTER_TRANSCEIVER_EVENT_ENABLE << 1) & (~READ_ONLY_BIT);
        }
        else {
            tx_buffer[0] = (0x4C << 1) & (~READ_ONLY_BIT);
        }
        tx_buffer[1] = 0b00000001;
        this->template transmit<SPI>(tx_buffer, 2);
    }

    /**
     * @brief Set CAN extended data format
     */
    void setCanExtendedDataFormat()
    {
        uint8_t* tx_buffer = this->template get<SPI>().getTxBuffer();
        tx_buffer[0]       = (REGISTER_FRAME_CONTROL << 1) & (~READ_ONLY_BIT);
        tx_buffer[1]       = 0x80;
        this->template transmit<SPI>(tx_buffer, 2);
    }

    /**
     * @brief Clear system event flag
     */
    void clearSystemEventFlag()
    {
        uint8_t* tx_buffer = this->template get<SPI>().getTxBuffer();
        tx_buffer[0]       = (REGISTER_SYSTEM_EVENT_STATUS << 1) & (~READ_ONLY_BIT);
        tx_buffer[1]       = 0x16;
        this->template transmit<SPI>(tx_buffer, 2);
    }

    /**
     * @brief Clear transceiver event flag
     */
    void clearTransceiverEventFlag()
    {
        uint8_t* tx_buffer = this->template get<SPI>().getTxBuffer();
        tx_buffer[0]       = (REGISTER_TRANSCEIVER_EVENT_STATUS << 1) & (~READ_ONLY_BIT);
        tx_buffer[1]       = 0x33;
        this->template transmit<SPI>(tx_buffer, 2);
    }
};

}  // namespace devices
}  // namespace chipz

#endif  // CHIPZ_DEVICES_TJA1145_HPP
