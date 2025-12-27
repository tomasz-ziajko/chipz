#ifndef CHIPZ_DEVICES_DS3231_HPP
#define CHIPZ_DEVICES_DS3231_HPP

#include "../peripheral.hpp"
#include "../interfaces/i2c_interface.hpp"
#include <cstdint>
#include <ctime>
#include <array>
#include <functional>

namespace chipz {
namespace devices {

/**
 * @brief Driver for DS3231 Real-Time Clock (RTC) chip
 *
 * The DS3231 is a highly accurate I2C real-time clock with
 * integrated temperature-compensated crystal oscillator (TCXO)
 * and crystal.
 *
 * This implementation mirrors the state machine design from the C version,
 * providing full compatibility with embedded systems requirements.
 *
 * @tparam CommInterface Communication interface type (typically I2C)
 */
template<typename CommInterface>
class DS3231 : public Peripheral {
public:
    struct Temperature {
        int8_t integer;
        uint8_t fraction; // 0.25°C resolution (0, 25, 50, 75)
    };

    enum class TimeParameter {
        Second,
        Minute,
        Hour,
        Day,
        Month,
        Year
    };

    enum class AdjustDirection {
        Up,
        Down
    };

    /**
     * @brief Construct DS3231 driver with communication interface
     * @param comm Reference to communication interface (I2C)
     * @param get_tick Function to get current system tick in milliseconds
     */
    DS3231(CommInterface& comm, std::function<uint32_t()> get_tick = nullptr)
        : comm_(comm)
        , status_(Status::Uninitialized)
        , state_(State::PreInit)
        , current_time_{}
        , tick_timer_(0)
        , last_tick_(0)
        , get_tick_(get_tick)
        , time_update_request_(false)
        , time_update_waiting_for_interrupt_(false)
        , alarm1_update_request_(false)
        , alarm1_update_waiting_for_interrupt_(false)
        , alarm2_update_request_(false)
        , alarm2_update_waiting_for_interrupt_(false)
        , alarm1_read_request_(false)
        , alarm1_read_in_progress_(false)
        , alarm2_read_request_(false)
        , alarm2_read_in_progress_(false)
        , time_read_in_progress_(false)
        , time_read_paused_(false)
        , status_read_in_progress_(false)
        , osf_clear_pending_(false)
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
    {
        // Set up I2C completion callback
        comm_.setTransferCompleteCallback(
            [this](bool success) { this->onTransferComplete(success); }
        );
    }

    // Peripheral interface implementation
    bool initialize() override {
        if (!comm_.isReady()) {
            status_ = Status::Error;
            return false;
        }

        // Reset state machine
        state_ = State::PreInit;
        osf_clear_pending_ = false;
        time_read_in_progress_ = false;
        time_read_paused_ = false;
        status_read_in_progress_ = true;
        alarm1_update_request_ = false;
        alarm1_update_waiting_for_interrupt_ = false;
        alarm2_update_request_ = false;
        alarm2_update_waiting_for_interrupt_ = false;
        alarm1_read_request_ = false;
        alarm1_read_in_progress_ = false;
        alarm2_read_request_ = false;
        alarm2_read_in_progress_ = false;
        tick_timer_ = 0;

        if (get_tick_) {
            last_tick_ = get_tick_();
        }

        // Start in PREINIT state - query status register to check oscillator state
        setRegisterAddress(STATUS_START_ADDRESS);
        if (!comm_.receive(comm_.getRxBuffer(), STATUS_REGISTER_LENGTH)) {
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
        return status_ == Status::Ready && comm_.isReady() && state_ == State::Idle;
    }

    Status getStatus() const override {
        return status_;
    }

    std::string getDeviceId() const override {
        return "DS3231 RTC";
    }

    bool main() override {
        if (status_ != Status::Ready) {
            return false;
        }

        switch (state_) {
            case State::PreInit:
                // In PREINIT, oscillator state is unknown
                // Wait for initial status read to complete (handled in interrupt callback)
                // Don't start any operations until we know if OSF needs clearing
                break;

            case State::ClearingOSF:
                // Clear the OSF (Oscillator Stop Flag) in Control/Status register (0x0F, bit 7)
                // OSF must be explicitly cleared by writing 0 to bit 7
                comm_.getTxBuffer()[0] = control_;
                comm_.getTxBuffer()[1] = control_status_ & 0x7F; // Clear OSF bit (bit 7)
                setRegisterAddress(STATUS_START_ADDRESS);
                comm_.transmit(comm_.getTxBuffer(), 2);
                osf_clear_pending_ = true;
                // Stay in CLEARING_OSF state (transition to IDLE happens in callback)
                break;

            case State::Idle:
                // Ready for operations - check for pending requests
                if (time_update_request_) {
                    time_update_request_ = false;
                    time_update_waiting_for_interrupt_ = true;
                    serializeCurrentTime();
                    setRegisterAddress(TIME_REGISTER_START);
                    comm_.transmit(comm_.getTxBuffer(), TIME_REGISTER_LENGTH);
                    state_ = State::Running;
                    return true;
                }

                if (alarm1_update_request_) {
                    alarm1_update_request_ = false;
                    alarm1_update_waiting_for_interrupt_ = true;
                    serializeAlarm1();
                    setRegisterAddress(ALARM1_START_ADDRESS);
                    comm_.transmit(comm_.getTxBuffer(), ALARM1_LENGTH);
                    state_ = State::Running;
                    return true;
                }

                if (alarm2_update_request_) {
                    alarm2_update_request_ = false;
                    alarm2_update_waiting_for_interrupt_ = true;
                    serializeAlarm2();
                    setRegisterAddress(ALARM2_START_ADDRESS);
                    comm_.transmit(comm_.getTxBuffer(), ALARM2_LENGTH);
                    state_ = State::Running;
                    return true;
                }

                if (alarm1_read_request_) {
                    alarm1_read_request_ = false;
                    alarm1_read_in_progress_ = true;
                    setRegisterAddress(ALARM1_START_ADDRESS);
                    comm_.receive(comm_.getRxBuffer(), ALARM1_LENGTH);
                    state_ = State::Running;
                    return true;
                }

                if (alarm2_read_request_) {
                    alarm2_read_request_ = false;
                    alarm2_read_in_progress_ = true;
                    setRegisterAddress(ALARM2_START_ADDRESS);
                    comm_.receive(comm_.getRxBuffer(), ALARM2_LENGTH);
                    state_ = State::Running;
                    return true;
                }

                // Calculate elapsed time since last call
                if (get_tick_) {
                    uint32_t current_tick = get_tick_();
                    uint32_t elapsed_ms = current_tick - last_tick_;
                    last_tick_ = current_tick;

                    // Read current time at specified period (unless paused)
                    if (!time_read_paused_ && (tick_timer_ % TIME_READ_PERIOD_MS == 0)) {
                        time_read_in_progress_ = true;
                        setRegisterAddress(TIME_REGISTER_START);
                        comm_.receive(comm_.getRxBuffer(), TIME_REGISTER_LENGTH);
                        state_ = State::Running;
                    }

                    // Read status registers at specified period (offset by 50ms to avoid collision)
                    if ((tick_timer_ + 50) % STATUS_READ_PERIOD_MS == 0) {
                        status_read_in_progress_ = true;
                        setRegisterAddress(STATUS_START_ADDRESS);
                        comm_.receive(comm_.getRxBuffer(), STATUS_REGISTER_LENGTH);
                        state_ = State::Running;
                    }

                    // Increment tick timer
                    tick_timer_ += elapsed_ms;

                    // Wrap timer to prevent overflow
                    if (tick_timer_ >= TIMER_MAX_MS) {
                        tick_timer_ = 0;
                    }
                }
                break;

            case State::Running:
                // Active transmission in progress - wait for interrupt callback
                // Don't start any new operations
                break;
        }

        return true;
    }

    // DS3231-specific interface

    /**
     * @brief Set the current date and time on the DS3231
     * @param time Standard C time structure
     */
    void setTime(const std::tm& time) {
        current_time_ = time;
        time_update_request_ = true;
        time_read_paused_ = false; // Resume time reading
    }

    /**
     * @brief Get the current cached time
     * @param time Reference to time structure to fill
     * @return Pointer to current time structure
     */
    const std::tm* getTime() const {
        return &current_time_;
    }

    /**
     * @brief Adjust a specific time parameter up or down
     * @param param Time parameter to adjust
     * @param dir Direction to adjust (up or down)
     */
    void adjustTime(TimeParameter param, AdjustDirection dir) {
        switch (param) {
            case TimeParameter::Hour:
                if (dir == AdjustDirection::Up) {
                    current_time_.tm_hour++;
                    if (current_time_.tm_hour > 23) {
                        current_time_.tm_hour = 0;
                    }
                } else {
                    if (current_time_.tm_hour == 0) {
                        current_time_.tm_hour = 23;
                    } else {
                        current_time_.tm_hour--;
                    }
                }
                break;

            case TimeParameter::Minute:
                if (dir == AdjustDirection::Up) {
                    current_time_.tm_min++;
                    if (current_time_.tm_min > 59) {
                        current_time_.tm_min = 0;
                    }
                } else {
                    if (current_time_.tm_min == 0) {
                        current_time_.tm_min = 59;
                    } else {
                        current_time_.tm_min--;
                    }
                }
                break;

            case TimeParameter::Second:
                if (dir == AdjustDirection::Up) {
                    current_time_.tm_sec++;
                    if (current_time_.tm_sec > 59) {
                        current_time_.tm_sec = 0;
                    }
                } else {
                    if (current_time_.tm_sec == 0) {
                        current_time_.tm_sec = 59;
                    } else {
                        current_time_.tm_sec--;
                    }
                }
                break;

            case TimeParameter::Day:
                if (dir == AdjustDirection::Up) {
                    current_time_.tm_mday++;
                    if (current_time_.tm_mday > 31) {
                        current_time_.tm_mday = 1;
                    }
                } else {
                    if (current_time_.tm_mday == 1) {
                        current_time_.tm_mday = 31;
                    } else {
                        current_time_.tm_mday--;
                    }
                }
                break;

            case TimeParameter::Month:
                if (dir == AdjustDirection::Up) {
                    current_time_.tm_mon++;
                    if (current_time_.tm_mon > 11) { // tm_mon is 0-11
                        current_time_.tm_mon = 0;
                    }
                } else {
                    if (current_time_.tm_mon == 0) {
                        current_time_.tm_mon = 11;
                    } else {
                        current_time_.tm_mon--;
                    }
                }
                break;

            case TimeParameter::Year:
                if (dir == AdjustDirection::Up) {
                    current_time_.tm_year++;
                    if (current_time_.tm_year > 199) { // 199 = 2099 - 1900
                        current_time_.tm_year = 100; // 100 = 2000 - 1900
                    }
                } else {
                    if (current_time_.tm_year == 100) {
                        current_time_.tm_year = 199;
                    } else {
                        current_time_.tm_year--;
                    }
                }
                break;
        }
    }

    /**
     * @brief Set alarm 1
     * @param seconds Seconds value
     * @param minutes Minutes value
     * @param hours Hours value
     * @param day_date Day/date value
     */
    void setAlarm1(uint8_t seconds, uint8_t minutes, uint8_t hours, uint8_t day_date) {
        alarm1_seconds_ = seconds;
        alarm1_minutes_ = minutes;
        alarm1_hours_ = hours;
        alarm1_day_date_ = day_date;
        alarm1_update_request_ = true;
    }

    /**
     * @brief Set alarm 2
     * @param minutes Minutes value
     * @param hours Hours value
     * @param day_date Day/date value
     */
    void setAlarm2(uint8_t minutes, uint8_t hours, uint8_t day_date) {
        alarm2_minutes_ = minutes;
        alarm2_hours_ = hours;
        alarm2_day_date_ = day_date;
        alarm2_update_request_ = true;
    }

    /**
     * @brief Request to read alarm 1 from DS3231
     */
    void readAlarm1() {
        alarm1_read_request_ = true;
    }

    /**
     * @brief Request to read alarm 2 from DS3231
     */
    void readAlarm2() {
        alarm2_read_request_ = true;
    }

    /**
     * @brief Pause periodic time reading from DS3231
     */
    void pauseTimeRead() {
        time_read_paused_ = true;
    }

    /**
     * @brief Resume periodic time reading from DS3231
     */
    void resumeTimeRead() {
        time_read_paused_ = false;
    }

    /**
     * @brief Read the temperature from the DS3231
     * @param temp Reference to temperature structure to fill
     * @return true if temperature data is available
     */
    bool getTemperature(Temperature& temp) const {
        temp.integer = static_cast<int8_t>(temp_ >> 8);
        temp.fraction = ((temp_ & 0xFF) >> 6) * 25;
        return true;
    }

private:
    enum class State {
        PreInit,      // Oscillator state unknown, query status register
        ClearingOSF,  // Clear Oscillator Stop Flag for virgin DS3231
        Idle,         // Ready for operations, do nothing
        Running       // Active transmission in progress
    };

    CommInterface& comm_;
    Status status_;
    State state_;
    std::tm current_time_;

    // Timing
    uint32_t tick_timer_;
    uint32_t last_tick_;
    std::function<uint32_t()> get_tick_;

    // Request flags
    bool time_update_request_;
    bool time_update_waiting_for_interrupt_;
    bool alarm1_update_request_;
    bool alarm1_update_waiting_for_interrupt_;
    bool alarm2_update_request_;
    bool alarm2_update_waiting_for_interrupt_;
    bool alarm1_read_request_;
    bool alarm1_read_in_progress_;
    bool alarm2_read_request_;
    bool alarm2_read_in_progress_;
    bool time_read_in_progress_;
    bool time_read_paused_;
    bool status_read_in_progress_;
    bool osf_clear_pending_;

    // Alarm 1 registers (0x07-0x0A)
    uint8_t alarm1_seconds_;
    uint8_t alarm1_minutes_;
    uint8_t alarm1_hours_;
    uint8_t alarm1_day_date_;

    // Alarm 2 registers (0x0B-0x0D)
    uint8_t alarm2_minutes_;
    uint8_t alarm2_hours_;
    uint8_t alarm2_day_date_;

    // Control register (0x0E)
    uint8_t control_;

    // Control/Status register (0x0F)
    uint8_t control_status_;

    // Aging offset register (0x10)
    uint8_t aging_offset_;

    // Temperature registers (0x11-0x12)
    int16_t temp_;

    // DS3231 Register addresses and sizes
    static constexpr uint8_t I2C_ADDRESS = 0x68;
    static constexpr uint8_t TIME_REGISTER_START = 0x00;
    static constexpr uint8_t TIME_REGISTER_LENGTH = 7;
    static constexpr uint8_t ALARM1_START_ADDRESS = 0x07;
    static constexpr uint8_t ALARM1_LENGTH = 4;
    static constexpr uint8_t ALARM2_START_ADDRESS = 0x0B;
    static constexpr uint8_t ALARM2_LENGTH = 3;
    static constexpr uint8_t STATUS_START_ADDRESS = 0x0E;
    static constexpr uint8_t STATUS_REGISTER_LENGTH = 5;
    static constexpr uint8_t TEMP_REGISTER_START = 0x11;
    static constexpr uint8_t TEMP_REGISTER_LENGTH = 2;

    // Status register bits
    static constexpr uint8_t STATUS_REG_OSF_BIT = 0x80;

    // Timing constants
    static constexpr uint32_t TIME_READ_PERIOD_MS = 100;
    static constexpr uint32_t STATUS_READ_PERIOD_MS = 1000;
    static constexpr uint32_t TIMER_MAX_MS = 1000;

    /**
     * @brief I2C transfer completion callback (equivalent to ds3231_interrupt)
     * @param success True if transfer succeeded, false on error
     */
    void onTransferComplete(bool success) {
        if (!success) {
            status_ = Status::Error;
            state_ = State::Idle;
            return;
        }

        switch (state_) {
            case State::PreInit:
                // Initial status read completed - check OSF flag
                status_read_in_progress_ = false;
                deserializeStatus();

                // Check OSF bit in Control/Status register (0x0F)
                if (control_status_ & STATUS_REG_OSF_BIT) {
                    // OSF is set - need to clear it (virgin DS3231)
                    state_ = State::ClearingOSF;
                } else {
                    // OSF not set - oscillator is running, go to IDLE
                    state_ = State::Idle;
                }
                break;

            case State::ClearingOSF:
                // OSF clear write completed
                if (osf_clear_pending_) {
                    osf_clear_pending_ = false;
                    // Transition to IDLE - initialization complete
                    state_ = State::Idle;
                }
                break;

            case State::Idle:
                // Should not receive callbacks in IDLE state
                break;

            case State::Running:
                // Handle various interrupt sources and return to IDLE
                if (time_update_waiting_for_interrupt_) {
                    time_update_waiting_for_interrupt_ = false;
                    state_ = State::Idle;
                } else if (alarm1_update_waiting_for_interrupt_) {
                    alarm1_update_waiting_for_interrupt_ = false;
                    state_ = State::Idle;
                } else if (alarm2_update_waiting_for_interrupt_) {
                    alarm2_update_waiting_for_interrupt_ = false;
                    state_ = State::Idle;
                } else if (status_read_in_progress_) {
                    status_read_in_progress_ = false;
                    deserializeStatus();
                    state_ = State::Idle;
                } else if (time_read_in_progress_) {
                    time_read_in_progress_ = false;
                    deserializeCurrentTime();
                    state_ = State::Idle;
                } else if (alarm1_read_in_progress_) {
                    alarm1_read_in_progress_ = false;
                    deserializeAlarm1();
                    state_ = State::Idle;
                } else if (alarm2_read_in_progress_) {
                    alarm2_read_in_progress_ = false;
                    deserializeAlarm2();
                    state_ = State::Idle;
                }
                break;
        }
    }

    /**
     * @brief Serialize current time to TX buffer in BCD format
     */
    void serializeCurrentTime() {
        uint8_t* tx_buffer = comm_.getTxBuffer();

        // Seconds (bits 6:4 = tens, bits 3:0 = ones)
        tx_buffer[0] = ((current_time_.tm_sec / 10) << 4) & 0x70;
        tx_buffer[0] |= (current_time_.tm_sec % 10) & 0x0F;

        // Minutes (bits 6:4 = tens, bits 3:0 = ones)
        tx_buffer[1] = ((current_time_.tm_min / 10) << 4) & 0x70;
        tx_buffer[1] |= (current_time_.tm_min % 10) & 0x0F;

        // Hours - 24 hour mode (bits 5:4 = tens, bits 3:0 = ones)
        tx_buffer[2] = ((current_time_.tm_hour / 10) << 4) & 0x30;
        tx_buffer[2] |= (current_time_.tm_hour % 10) & 0x0F;

        // Day of week (1-7, tm_wday is 0-6)
        tx_buffer[3] = (current_time_.tm_wday + 1) & 0x07;

        // Day of month (bits 5:4 = tens, bits 3:0 = ones)
        tx_buffer[4] = ((current_time_.tm_mday / 10) << 4) & 0x30;
        tx_buffer[4] |= (current_time_.tm_mday % 10) & 0x0F;

        // Month (bit 4 = tens, bits 3:0 = ones, tm_mon is 0-11, DS3231 uses 1-12)
        uint8_t month = current_time_.tm_mon + 1;
        tx_buffer[5] = ((month / 10) << 4) & 0x10;
        tx_buffer[5] |= (month % 10) & 0x0F;

        // Year (bits 7:4 = tens, bits 3:0 = ones)
        uint8_t year = current_time_.tm_year % 100;
        tx_buffer[6] = ((year / 10) << 4) & 0xF0;
        tx_buffer[6] |= (year % 10) & 0x0F;
    }

    /**
     * @brief Deserialize current time from RX buffer (BCD format)
     */
    void deserializeCurrentTime() {
        const uint8_t* rx_buffer = comm_.getRxBuffer();

        // Seconds - BCD to decimal
        current_time_.tm_sec = ((rx_buffer[0] & 0x70) >> 4) * 10;
        current_time_.tm_sec += (rx_buffer[0] & 0x0F);

        // Minutes - BCD to decimal
        current_time_.tm_min = ((rx_buffer[1] & 0x70) >> 4) * 10;
        current_time_.tm_min += (rx_buffer[1] & 0x0F);

        // Hours - BCD to decimal (24-hour mode)
        current_time_.tm_hour = ((rx_buffer[2] & 0x30) >> 4) * 10;
        current_time_.tm_hour += (rx_buffer[2] & 0x0F);

        // Day of week (DS3231: 1-7, tm_wday: 0-6)
        current_time_.tm_wday = (rx_buffer[3] & 0x07) - 1;

        // Day of month - BCD to decimal
        current_time_.tm_mday = ((rx_buffer[4] & 0x30) >> 4) * 10;
        current_time_.tm_mday += (rx_buffer[4] & 0x0F);

        // Month - BCD to decimal (DS3231: 1-12, tm_mon: 0-11)
        current_time_.tm_mon = ((rx_buffer[5] & 0x10) >> 4) * 10;
        current_time_.tm_mon += (rx_buffer[5] & 0x0F);
        current_time_.tm_mon -= 1; // Convert to tm_mon range

        // Year - BCD to decimal
        current_time_.tm_year = ((rx_buffer[6] & 0xF0) >> 4) * 10;
        current_time_.tm_year += (rx_buffer[6] & 0x0F);
        current_time_.tm_year += 100; // Assume 2000-2099

        // Not provided by DS3231
        current_time_.tm_isdst = -1;
    }

    /**
     * @brief Serialize alarm 1 to TX buffer
     */
    void serializeAlarm1() {
        uint8_t* tx_buffer = comm_.getTxBuffer();
        tx_buffer[0] = alarm1_seconds_;
        tx_buffer[1] = alarm1_minutes_;
        tx_buffer[2] = alarm1_hours_;
        tx_buffer[3] = alarm1_day_date_;
    }

    /**
     * @brief Deserialize alarm 1 from RX buffer
     */
    void deserializeAlarm1() {
        const uint8_t* rx_buffer = comm_.getRxBuffer();
        alarm1_seconds_ = rx_buffer[0];
        alarm1_minutes_ = rx_buffer[1];
        alarm1_hours_ = rx_buffer[2];
        alarm1_day_date_ = rx_buffer[3];
    }

    /**
     * @brief Serialize alarm 2 to TX buffer
     */
    void serializeAlarm2() {
        uint8_t* tx_buffer = comm_.getTxBuffer();
        tx_buffer[0] = alarm2_minutes_;
        tx_buffer[1] = alarm2_hours_;
        tx_buffer[2] = alarm2_day_date_;
    }

    /**
     * @brief Deserialize alarm 2 from RX buffer
     */
    void deserializeAlarm2() {
        const uint8_t* rx_buffer = comm_.getRxBuffer();
        alarm2_minutes_ = rx_buffer[0];
        alarm2_hours_ = rx_buffer[1];
        alarm2_day_date_ = rx_buffer[2];
    }

    /**
     * @brief Deserialize status registers from RX buffer
     */
    void deserializeStatus() {
        const uint8_t* rx_buffer = comm_.getRxBuffer();

        // Control register (0x0E)
        control_ = rx_buffer[0];

        // Control/Status register (0x0F)
        control_status_ = rx_buffer[1];

        // Aging offset register (0x10)
        aging_offset_ = rx_buffer[2];

        // Temperature registers (0x11-0x12) - MSB first, then LSB
        temp_ = (rx_buffer[3] << 8) | rx_buffer[4];
    }

    /**
     * @brief Set I2C register address for next operation
     * @param address Register address to access
     */
    void setRegisterAddress(uint8_t address) {
        // Cast to I2CInterface to access setMemoryAddress()
        auto* i2c = static_cast<chipz::interfaces::I2CInterface*>(&comm_);
        i2c->setMemoryAddress(address);
    }
};

} // namespace devices
} // namespace chipz

#endif // CHIPZ_DEVICES_DS3231_HPP
