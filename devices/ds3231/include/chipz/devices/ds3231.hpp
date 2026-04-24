// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_DEVICES_DS3231_HPP
#define CHIPZ_DEVICES_DS3231_HPP

#include <chipz/core/chip.hpp>
#include <chipz/interfaces/i2c_interface.hpp>
#include <array>
#include <cstdint>
#include <ctime>
#include <string>

namespace chipz {
namespace devices {

/**
 * @brief Driver for DS3231 Real-Time Clock (RTC) chip
 *
 * Scheduling:
 *   PreInit     — initial status read in flight (started in initialize());
 *                 suspends on WaitCondition::comm.
 *   ClearingOSF — OSF bit set; run() issues the clear write; suspends on
 *                 WaitCondition::comm.
 *   Idle        — dispatches pending application requests first
 *                 (time/alarm write, alarm read), then periodic reads:
 *                 time every TIME_READ_PERIOD_MS, status every
 *                 kStatusReadInterval time-read cycles (~1 s).
 */
class DS3231 : public Chip<interfaces::I2CInterface> {
public:
    struct Temperature {
        int8_t  integer;
        uint8_t fraction; // 0.25°C resolution (0, 25, 50, 75)
    };

    enum class TimeParameter { Second, Minute, Hour, Day, Month, Year };
    enum class AdjustDirection { Up, Down };

    explicit DS3231(interfaces::I2CInterface& comm)
        : Chip<interfaces::I2CInterface>(comm)
        , status_(Status::Uninitialized)
        , state_(State::PreInit)
        , current_time_{}
        , time_update_request_(false)
        , alarm1_update_request_(false)
        , alarm2_update_request_(false)
        , alarm1_read_request_(false)
        , alarm2_read_request_(false)
        , time_read_paused_(false)
        , status_read_countdown_(kStatusReadInterval)
        , alarm1_seconds_(0)
        , alarm1_minutes_(0)
        , alarm1_hours_(0)
        , alarm1_day_date_(0)
        , alarm2_minutes_(0)
        , alarm2_hours_(0)
        , alarm2_day_date_(0)
        , control_(0)
        , control_status_(0)
        , aging_offset_(0)
        , temp_(0)
    {}

    bool initialize() override {
        if (!get<interfaces::I2CInterface>().isReady()) {
            status_ = Status::Error;
            return false;
        }

        setConnection<interfaces::I2CInterface>(
            get<interfaces::I2CInterface>().registerConnection(I2C_ADDRESS));

        state_                 = State::PreInit;
        time_update_request_   = false;
        alarm1_update_request_ = false;
        alarm2_update_request_ = false;
        alarm1_read_request_   = false;
        alarm2_read_request_   = false;
        time_read_paused_      = false;
        status_read_countdown_ = kStatusReadInterval;

        if (!this->receive<interfaces::I2CInterface>(
                get<interfaces::I2CInterface>().getRxBuffer(),
                STATUS_REGISTER_LENGTH)) {
            status_ = Status::Error;
            return false;
        }

        status_ = Status::Ready;
        return true;
    }

    bool reset() override {
        status_ = Status::Uninitialized;
        return initialize();
    }

    bool isReady() const override {
        return status_ == Status::Ready &&
               get<interfaces::I2CInterface>().isReady() &&
               state_ == State::Idle;
    }

    Status getStatus() const override { return status_; }

    std::string getDeviceId() const override { return "DS3231 RTC"; }

    bool main() override { return true; }

    WaitCondition run() override {
        if (status_ != Status::Ready) return WaitCondition::demand();

        switch (state_) {
            case State::PreInit:
                // Status read started in initialize(); just wait for comm.
                return WaitCondition::comm(get<interfaces::I2CInterface>());

            case State::ClearingOSF:
                get<interfaces::I2CInterface>().getTxBuffer()[0] = control_;
                get<interfaces::I2CInterface>().getTxBuffer()[1] =
                    control_status_ & ~STATUS_REG_OSF_BIT;
                if (!this->transmit<interfaces::I2CInterface>(
                        get<interfaces::I2CInterface>().getTxBuffer(), 2)) {
                    return WaitCondition::immediate();  // bus busy — retry
                }
                state_ = State::WritingOSF;
                return WaitCondition::comm(get<interfaces::I2CInterface>());

            case State::Idle:
                return dispatchIdle();

            default:
                // A comm-awaited state re-entered without a comm event.
                return WaitCondition::comm(get<interfaces::I2CInterface>());
        }
    }

    void setTime(const std::tm& time) {
        current_time_        = time;
        time_update_request_ = true;
        time_read_paused_    = false;
    }

    const std::tm* getTime() const { return &current_time_; }

    void adjustTime(TimeParameter param, AdjustDirection dir) {
        switch (param) {
            case TimeParameter::Hour:
                if (dir == AdjustDirection::Up) {
                    current_time_.tm_hour++;
                    if (current_time_.tm_hour > 23) current_time_.tm_hour = 0;
                } else {
                    if (current_time_.tm_hour == 0) current_time_.tm_hour = 23;
                    else                            current_time_.tm_hour--;
                }
                break;

            case TimeParameter::Minute:
                if (dir == AdjustDirection::Up) {
                    current_time_.tm_min++;
                    if (current_time_.tm_min > 59) current_time_.tm_min = 0;
                } else {
                    if (current_time_.tm_min == 0) current_time_.tm_min = 59;
                    else                           current_time_.tm_min--;
                }
                break;

            case TimeParameter::Second:
                if (dir == AdjustDirection::Up) {
                    current_time_.tm_sec++;
                    if (current_time_.tm_sec > 59) current_time_.tm_sec = 0;
                } else {
                    if (current_time_.tm_sec == 0) current_time_.tm_sec = 59;
                    else                           current_time_.tm_sec--;
                }
                break;

            case TimeParameter::Day:
                if (dir == AdjustDirection::Up) {
                    current_time_.tm_mday++;
                    if (current_time_.tm_mday > 31) current_time_.tm_mday = 1;
                } else {
                    if (current_time_.tm_mday == 1) current_time_.tm_mday = 31;
                    else                            current_time_.tm_mday--;
                }
                break;

            case TimeParameter::Month:
                if (dir == AdjustDirection::Up) {
                    current_time_.tm_mon++;
                    if (current_time_.tm_mon > 11) current_time_.tm_mon = 0;
                } else {
                    if (current_time_.tm_mon == 0) current_time_.tm_mon = 11;
                    else                           current_time_.tm_mon--;
                }
                break;

            case TimeParameter::Year:
                if (dir == AdjustDirection::Up) {
                    current_time_.tm_year++;
                    if (current_time_.tm_year > 199) current_time_.tm_year = 100;
                } else {
                    if (current_time_.tm_year == 100) current_time_.tm_year = 199;
                    else                              current_time_.tm_year--;
                }
                break;
        }
    }

    void setAlarm1(uint8_t seconds, uint8_t minutes, uint8_t hours, uint8_t day_date) {
        alarm1_seconds_        = seconds;
        alarm1_minutes_        = minutes;
        alarm1_hours_          = hours;
        alarm1_day_date_       = day_date;
        alarm1_update_request_ = true;
    }

    void setAlarm2(uint8_t minutes, uint8_t hours, uint8_t day_date) {
        alarm2_minutes_        = minutes;
        alarm2_hours_          = hours;
        alarm2_day_date_       = day_date;
        alarm2_update_request_ = true;
    }

    void readAlarm1()     { alarm1_read_request_ = true;  }
    void readAlarm2()     { alarm2_read_request_ = true;  }
    void pauseTimeRead()  { time_read_paused_    = true;  }
    void resumeTimeRead() { time_read_paused_    = false; }

    bool getTemperature(Temperature& temp) const {
        temp.integer  = static_cast<int8_t>(temp_ >> 8);
        temp.fraction = static_cast<uint8_t>(((temp_ & 0xFF) >> 6) * 25);
        return true;
    }

private:
    enum class State {
        PreInit,       // init status read in flight
        ClearingOSF,   // OSF set; need to issue clear write
        WritingOSF,    // OSF clear write in flight
        Idle,          // nothing in flight; next run() dispatches
        ReadingTime,
        ReadingStatus,
        WritingTime,
        WritingAlarm1,
        WritingAlarm2,
        ReadingAlarm1,
        ReadingAlarm2,
    };

    Status  status_;
    State   state_;
    std::tm current_time_;

    bool    time_update_request_;
    bool    alarm1_update_request_;
    bool    alarm2_update_request_;
    bool    alarm1_read_request_;
    bool    alarm2_read_request_;
    bool    time_read_paused_;
    uint8_t status_read_countdown_;

    uint8_t alarm1_seconds_, alarm1_minutes_, alarm1_hours_, alarm1_day_date_;
    uint8_t alarm2_minutes_, alarm2_hours_, alarm2_day_date_;
    uint8_t control_, control_status_, aging_offset_;
    int16_t temp_;

    static constexpr uint8_t  I2C_ADDRESS           = 0x68;
    static constexpr uint8_t  TIME_REGISTER_LENGTH   = 7;
    static constexpr uint8_t  ALARM1_LENGTH          = 4;
    static constexpr uint8_t  ALARM2_LENGTH          = 3;
    static constexpr uint8_t  STATUS_REGISTER_LENGTH = 5;
    static constexpr uint8_t  STATUS_REG_OSF_BIT     = 0x80;
    static constexpr uint32_t kTimeReadPeriodMs      = 100;
    static constexpr uint8_t  kStatusReadInterval    = 10; // 10 × 100 ms ≈ 1 s

    // Dispatches a single operation from the Idle state.
    // Application requests take priority over periodic reads.
    // Countdown only decrements when no request is pending.
    WaitCondition dispatchIdle() {
        auto& i2c = get<interfaces::I2CInterface>();

        if (time_update_request_) {
            serializeCurrentTime();
            if (!this->transmit<interfaces::I2CInterface>(i2c.getTxBuffer(), TIME_REGISTER_LENGTH))
                return WaitCondition::immediate();
            time_update_request_ = false;
            state_ = State::WritingTime;
            return WaitCondition::comm(i2c);
        }

        if (alarm1_update_request_) {
            serializeAlarm1();
            if (!this->transmit<interfaces::I2CInterface>(i2c.getTxBuffer(), ALARM1_LENGTH))
                return WaitCondition::immediate();
            alarm1_update_request_ = false;
            state_ = State::WritingAlarm1;
            return WaitCondition::comm(i2c);
        }

        if (alarm2_update_request_) {
            serializeAlarm2();
            if (!this->transmit<interfaces::I2CInterface>(i2c.getTxBuffer(), ALARM2_LENGTH))
                return WaitCondition::immediate();
            alarm2_update_request_ = false;
            state_ = State::WritingAlarm2;
            return WaitCondition::comm(i2c);
        }

        if (alarm1_read_request_) {
            if (!this->receive<interfaces::I2CInterface>(i2c.getRxBuffer(), ALARM1_LENGTH))
                return WaitCondition::immediate();
            alarm1_read_request_ = false;
            state_ = State::ReadingAlarm1;
            return WaitCondition::comm(i2c);
        }

        if (alarm2_read_request_) {
            if (!this->receive<interfaces::I2CInterface>(i2c.getRxBuffer(), ALARM2_LENGTH))
                return WaitCondition::immediate();
            alarm2_read_request_ = false;
            state_ = State::ReadingAlarm2;
            return WaitCondition::comm(i2c);
        }

        // Periodic: status read every kStatusReadInterval time-read cycles.
        if (--status_read_countdown_ == 0) {
            status_read_countdown_ = kStatusReadInterval;
            if (!this->receive<interfaces::I2CInterface>(i2c.getRxBuffer(), STATUS_REGISTER_LENGTH))
                return WaitCondition::immediate();
            state_ = State::ReadingStatus;
            return WaitCondition::comm(i2c);
        }

        if (!time_read_paused_) {
            if (!this->receive<interfaces::I2CInterface>(i2c.getRxBuffer(), TIME_REGISTER_LENGTH))
                return WaitCondition::immediate();
            state_ = State::ReadingTime;
            return WaitCondition::comm(i2c);
        }

        return WaitCondition::delayMs(kTimeReadPeriodMs);
    }

    void onTransferComplete(CommunicationInterface& /*which*/, bool success) override {
        if (!success) {
            status_ = Status::Error;
            state_  = State::Idle;
            return;
        }

        switch (state_) {
            case State::PreInit:
                deserializeStatus();
                state_ = (control_status_ & STATUS_REG_OSF_BIT)
                       ? State::ClearingOSF
                       : State::Idle;
                break;

            case State::WritingOSF:
                state_ = State::Idle;
                break;

            case State::ReadingTime:
                deserializeCurrentTime();
                state_ = State::Idle;
                break;

            case State::ReadingStatus:
                deserializeStatus();
                state_ = State::Idle;
                break;

            case State::WritingTime:
            case State::WritingAlarm1:
            case State::WritingAlarm2:
                state_ = State::Idle;
                break;

            case State::ReadingAlarm1:
                deserializeAlarm1();
                state_ = State::Idle;
                break;

            case State::ReadingAlarm2:
                deserializeAlarm2();
                state_ = State::Idle;
                break;

            default:
                break;
        }
    }

    void serializeCurrentTime() {
        uint8_t* tx = get<interfaces::I2CInterface>().getTxBuffer();
        tx[0] = static_cast<uint8_t>(((current_time_.tm_sec  / 10) << 4) | (current_time_.tm_sec  % 10));
        tx[1] = static_cast<uint8_t>(((current_time_.tm_min  / 10) << 4) | (current_time_.tm_min  % 10));
        tx[2] = static_cast<uint8_t>(((current_time_.tm_hour / 10) << 4) | (current_time_.tm_hour % 10));
        tx[3] = static_cast<uint8_t>((current_time_.tm_wday + 1) & 0x07);
        tx[4] = static_cast<uint8_t>(((current_time_.tm_mday / 10) << 4) | (current_time_.tm_mday % 10));
        uint8_t month = static_cast<uint8_t>(current_time_.tm_mon + 1);
        tx[5] = static_cast<uint8_t>(((month / 10) << 4) | (month % 10));
        uint8_t year  = static_cast<uint8_t>(current_time_.tm_year % 100);
        tx[6] = static_cast<uint8_t>(((year  / 10) << 4) | (year  % 10));
    }

    void deserializeCurrentTime() {
        const uint8_t* rx = get<interfaces::I2CInterface>().getRxBuffer();
        current_time_.tm_sec  = ((rx[0] & 0x70) >> 4) * 10 + (rx[0] & 0x0F);
        current_time_.tm_min  = ((rx[1] & 0x70) >> 4) * 10 + (rx[1] & 0x0F);
        current_time_.tm_hour = ((rx[2] & 0x30) >> 4) * 10 + (rx[2] & 0x0F);
        current_time_.tm_wday = (rx[3] & 0x07) - 1;
        current_time_.tm_mday = ((rx[4] & 0x30) >> 4) * 10 + (rx[4] & 0x0F);
        current_time_.tm_mon  = ((rx[5] & 0x10) >> 4) * 10 + (rx[5] & 0x0F) - 1;
        current_time_.tm_year = ((rx[6] & 0xF0) >> 4) * 10 + (rx[6] & 0x0F) + 100;
        current_time_.tm_isdst = -1;
    }

    void serializeAlarm1() {
        uint8_t* tx = get<interfaces::I2CInterface>().getTxBuffer();
        tx[0] = alarm1_seconds_;
        tx[1] = alarm1_minutes_;
        tx[2] = alarm1_hours_;
        tx[3] = alarm1_day_date_;
    }

    void deserializeAlarm1() {
        const uint8_t* rx = get<interfaces::I2CInterface>().getRxBuffer();
        alarm1_seconds_  = rx[0];
        alarm1_minutes_  = rx[1];
        alarm1_hours_    = rx[2];
        alarm1_day_date_ = rx[3];
    }

    void serializeAlarm2() {
        uint8_t* tx = get<interfaces::I2CInterface>().getTxBuffer();
        tx[0] = alarm2_minutes_;
        tx[1] = alarm2_hours_;
        tx[2] = alarm2_day_date_;
    }

    void deserializeAlarm2() {
        const uint8_t* rx = get<interfaces::I2CInterface>().getRxBuffer();
        alarm2_minutes_  = rx[0];
        alarm2_hours_    = rx[1];
        alarm2_day_date_ = rx[2];
    }

    void deserializeStatus() {
        const uint8_t* rx = get<interfaces::I2CInterface>().getRxBuffer();
        control_        = rx[0];
        control_status_ = rx[1];
        aging_offset_   = rx[2];
        temp_           = static_cast<int16_t>((rx[3] << 8) | rx[4]);
    }
};

} // namespace devices
} // namespace chipz

#endif // CHIPZ_DEVICES_DS3231_HPP
