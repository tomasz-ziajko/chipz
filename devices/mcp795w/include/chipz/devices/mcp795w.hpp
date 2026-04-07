#ifndef CHIPZ_DEVICES_MCP795W_HPP
#define CHIPZ_DEVICES_MCP795W_HPP

#include <chipz/peripheral.hpp>
#include <chipz/interfaces/spi_interface.hpp>
#include <cstdint>
#include <ctime>
#include <functional>
#include <string>

namespace chipz {
namespace devices {

/**
 * @brief Driver for MCP795W SPI Real-Time Clock/Calendar with SRAM
 *
 * The MCP795W is a low-power real-time clock/calendar with battery backup
 * and 64 bytes of SRAM. Communicates via SPI interface.
 *
 * This implementation mirrors the design from the C version,
 * maintaining the exact sequence logic required for correct operation.
 * Uses std::tm for compatibility with DS3231 interface.
 *
 * @tparam CommInterface Communication interface type (typically SPI)
 */
class MCP795W : public Peripheral<interfaces::SPIInterface> {
public:
    /**
     * @brief Construct MCP795W driver with communication interface
     * @param comm Reference to communication interface (SPI)
     * @param get_spi_transmission_disabled Function to check if SPI transmission is disabled
     */
    MCP795W(interfaces::SPIInterface& comm,
            std::function<uint8_t()> get_spi_transmission_disabled = nullptr)
        : Peripheral<interfaces::SPIInterface>(comm)
        , status_(Status::Uninitialized)
        , current_time_{}
        , alarm_time_{}
        , timer_(0)
        , clock_started_(false)
        , date_reset_request_(false)
        , date_reset_requested_(false)
        , set_alarm_request_(false)
        , set_alarm_requested_(false)
        , time_keep_requested_(false)
        , disable_request_(false)
        , shutdown_allowed_(false)
        , get_spi_transmission_disabled_(get_spi_transmission_disabled)
    {
    }

    // Peripheral interface implementation
    bool initialize() override {
        if (!comm_.isReady()) {
            status_ = Status::Error;
            return false;
        }

        // Reset state
        shutdown_allowed_ = false;
        disable_request_ = false;
        clock_started_ = false;
        timer_ = 0;
        date_reset_request_ = false;
        set_alarm_request_ = false;
        date_reset_requested_ = false;
        set_alarm_requested_ = false;
        time_keep_requested_ = false;

        // Initialize default time (10:10)
        current_time_ = {};
        current_time_.tm_hour = 10;
        current_time_.tm_min = 10;

        status_ = Status::Ready;
        return true;
    }

    bool reset() override {
        status_ = Status::Uninitialized;
        return initialize();
    }

    bool isReady() const override {
        return status_ == Status::Ready && comm_.isReady() && clock_started_;
    }

    Status getStatus() const override {
        return status_;
    }

    std::string getDeviceId() const override {
        return "MCP795W RTC";
    }

    bool main() override {
        if (status_ != Status::Ready) {
            return false;
        }

        timer_ += 10;

        if (timer_ < 100) {
            return true;
        }

        if (get_spi_transmission_disabled_ && get_spi_transmission_disabled_()) {
            return true;
        }

        timer_ -= 100;

        if (!shutdown_allowed_) {
            shutdown_allowed_ = false;

            if (set_alarm_request_) {
                set_alarm_requested_ = true;
                set_alarm_request_ = false;
                buildAlarm();
                return true;
            }

            if (date_reset_request_) {
                date_reset_requested_ = true;
                date_reset_request_ = false;
                updateTimekeep();
                return true;
            }
        }

        if (disable_request_ &&
            !set_alarm_request_ &&
            !date_reset_request_ &&
            !date_reset_requested_ &&
            !set_alarm_requested_)
        {
            shutdown_allowed_ = true;
        }
        else {
            shutdown_allowed_ = false;
        }

        time_keep_requested_ = true;
        requestCurrentTimeTransmission();

        return true;
    }

    // MCP795W-specific interface (similar to DS3231)

    /**
     * @brief Set the current date and time
     * @param time Standard C time structure
     */
    void setTime(const std::tm& time) {
        date_reset_request_ = true;
        current_time_ = time;
    }

    /**
     * @brief Get the current cached time
     * @return Pointer to current time structure
     */
    const std::tm* getTime() const {
        return &current_time_;
    }

    /**
     * @brief Get current time by copying to provided structure
     * @param time Reference to time structure to fill
     */
    void getTime(std::tm& time) const {
        time = current_time_;
    }

    /**
     * @brief Set alarm time
     * @param time Alarm time structure
     */
    void setAlarm(const std::tm& time) {
        set_alarm_request_ = true;
        alarm_time_ = time;
    }

    /**
     * @brief Request alarm to be set
     */
    void requestAlarmSet() {
        set_alarm_request_ = true;
    }

    /**
     * @brief Check if clock is ready (started)
     * @return true if clock has been initialized and started
     */
    bool getReady() const {
        return clock_started_;
    }

    /**
     * @brief Check if shutdown is allowed
     * @return true if safe to shut down
     */
    bool getShutdownReady() const {
        return shutdown_allowed_;
    }

    /**
     * @brief Request stop/disable
     * @param request true to request stop, false to continue
     */
    void requestStop(bool request) {
        disable_request_ = request;
    }

    /**
     * @brief Get formatted time string
     * @return Formatted time string (YYYY/MM/DD HH:MM:SS)
     */
    std::string getTimeString() const {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%04d/%02d/%02d %02d:%02d:%02d",
                current_time_.tm_year + 1900,
                current_time_.tm_mon + 1,
                current_time_.tm_mday,
                current_time_.tm_hour,
                current_time_.tm_min,
                current_time_.tm_sec);
        return std::string(buffer);
    }

private:
    Status status_;

    std::tm current_time_;
    std::tm alarm_time_;

    uint16_t timer_;
    bool clock_started_;
    bool date_reset_request_;
    bool date_reset_requested_;
    bool set_alarm_request_;
    bool set_alarm_requested_;
    bool time_keep_requested_;
    bool disable_request_;
    bool shutdown_allowed_;

    std::function<uint8_t()> get_spi_transmission_disabled_;

    // MCP795W SPI Opcodes
    static constexpr uint8_t OPCODE_EEREAD = 0x03;
    static constexpr uint8_t OPCODE_EEWRITE = 0x02;
    static constexpr uint8_t OPCODE_EEWRDI = 0x04;
    static constexpr uint8_t OPCODE_EEWREN = 0x06;
    static constexpr uint8_t OPCODE_SRREAD = 0x05;
    static constexpr uint8_t OPCODE_SRWRITE = 0x01;
    static constexpr uint8_t OPCODE_READ = 0x13;
    static constexpr uint8_t OPCODE_WRITE = 0x12;
    static constexpr uint8_t OPCODE_UNLOCK = 0x14;
    static constexpr uint8_t OPCODE_IDWRITE = 0x32;
    static constexpr uint8_t OPCODE_IDREAD = 0x33;
    static constexpr uint8_t OPCODE_CLRWDT = 0x44;
    static constexpr uint8_t OPCODE_CLRRAM = 0x54;

    // MCP795W Register addresses
    static constexpr uint8_t TIME_AND_DATE_START_ADDRESS = 0;
    static constexpr uint8_t TIMEKEEP_SEND_TRANSMISSION_LENGTH = 11;
    static constexpr uint8_t ALARM0_START_ADDRESS = 0x0C;
    static constexpr uint8_t ALARM0_SEND_TRANSMISSION_LENGTH = 8;

    // Alarm masks
    static constexpr uint8_t SECOND_ALARM_MASK = 0x00;
    static constexpr uint8_t HOUR_ALARM_MASK = 0x02;
    static constexpr uint8_t GLOBAL_ALARM_MASK = 0x07;

    /**
     * @brief SPI transfer completion callback
     * @param success True if transfer succeeded, false on error
     */
    void onTransferComplete(bool success) override {
        if (!success) {
            status_ = Status::Error;
            return;
        }

        if (!clock_started_) {
            clock_started_ = true;
        }

        if (date_reset_requested_) {
            date_reset_requested_ = false;
            return;
        }
        else if (set_alarm_requested_) {
            set_alarm_requested_ = false;
            return;
        }
        else if (time_keep_requested_) {
            time_keep_requested_ = false;
            decodeTimekeep();
        }
    }

    /**
     * @brief Update timekeep registers (write current time to RTC)
     */
    void updateTimekeep() {
        uint8_t* tx_buffer = comm_.getTxBuffer();

        tx_buffer[0] = OPCODE_WRITE;
        tx_buffer[1] = TIME_AND_DATE_START_ADDRESS;

        // Hundreds of seconds register
        tx_buffer[2] = 0;

        // Seconds register (BCD encoding)
        tx_buffer[3] = current_time_.tm_sec / 10;
        tx_buffer[3] = ((tx_buffer[3] << 4) & 0x70);
        tx_buffer[3] |= ((current_time_.tm_sec % 10) & 0x0F);
        // ST bit in seconds register (start oscillator)
        tx_buffer[3] |= 0x80;

        // Minutes register (BCD encoding)
        tx_buffer[4] = current_time_.tm_min / 10;
        tx_buffer[4] = ((tx_buffer[4] << 4) & 0x70);
        tx_buffer[4] |= ((current_time_.tm_min % 10) & 0x0F);

        // Hours register (BCD encoding)
        tx_buffer[5] = current_time_.tm_hour / 10;
        tx_buffer[5] = ((tx_buffer[5] << 4) & 0x30);
        tx_buffer[5] |= ((current_time_.tm_hour % 10) & 0x0F);
        // Hours register 24h format
        tx_buffer[5] &= ~0x40;

        // VBATT enable
        tx_buffer[6] = 0x08;

        // Day of month register (BCD encoding)
        tx_buffer[7] = current_time_.tm_mday / 10;
        tx_buffer[7] = ((tx_buffer[7] << 4) & 0x30);
        tx_buffer[7] |= ((current_time_.tm_mday % 10) & 0x0F);

        // Month register (BCD encoding, tm_mon is 0-11, RTC uses 1-12)
        uint8_t month = current_time_.tm_mon + 1;
        tx_buffer[8] = month / 10;
        tx_buffer[8] = ((tx_buffer[8] << 4) & 0x10);
        tx_buffer[8] |= ((month % 10) & 0x0F);

        // Year register (BCD encoding, tm_year is years since 1900)
        uint32_t year_temp = current_time_.tm_year + 1900;
        year_temp = year_temp - 2000;
        tx_buffer[9] = year_temp / 10;
        tx_buffer[9] = ((tx_buffer[9] << 4) & 0xF0);
        tx_buffer[9] |= ((year_temp % 10) & 0x0F);

        // Control register - Enable ALARM 0
        tx_buffer[10] = 0x10;

        this->transmit(tx_buffer, TIMEKEEP_SEND_TRANSMISSION_LENGTH);
    }

    /**
     * @brief Request current time transmission (read from RTC)
     */
    void requestCurrentTimeTransmission() {
        uint8_t* tx_buffer = comm_.getTxBuffer();

        tx_buffer[0] = OPCODE_READ;
        tx_buffer[1] = TIME_AND_DATE_START_ADDRESS;

        this->transmit(tx_buffer, TIMEKEEP_SEND_TRANSMISSION_LENGTH);
    }

    /**
     * @brief Decode timekeep data from RX buffer
     */
    void decodeTimekeep() {
        const uint8_t* rx_buffer = comm_.getRxBuffer();

        // Skip hundreds of seconds register (index 2)

        // Seconds register (BCD decoding)
        current_time_.tm_sec = (((rx_buffer[3] & 0x70) >> 4) * 10);
        current_time_.tm_sec += (rx_buffer[3] & 0x0F);

        // Minutes register (BCD decoding)
        current_time_.tm_min = (((rx_buffer[4] & 0x70) >> 4) * 10);
        current_time_.tm_min += (rx_buffer[4] & 0x0F);

        // Hours register (BCD decoding)
        current_time_.tm_hour = (((rx_buffer[5] & 0x30) >> 4) * 10);
        current_time_.tm_hour += (rx_buffer[5] & 0x0F);

        // Day of month register (BCD decoding)
        current_time_.tm_mday = (((rx_buffer[7] & 0x30) >> 4) * 10);
        current_time_.tm_mday += (rx_buffer[7] & 0x0F);

        // Month register (BCD decoding, RTC uses 1-12, tm_mon is 0-11)
        uint8_t month = (((rx_buffer[8] & 0x10) >> 4) * 10);
        month += (rx_buffer[8] & 0x0F);
        current_time_.tm_mon = month - 1;

        // Year register (BCD decoding, assume 2000-2099)
        uint16_t year = (((rx_buffer[9] & 0xF0) >> 4) * 10);
        year += (rx_buffer[9] & 0x0F);
        year += 2000;
        current_time_.tm_year = year - 1900;

        // Not provided by MCP795W
        current_time_.tm_isdst = -1;
    }

    /**
     * @brief Build alarm configuration and send to RTC
     */
    void buildAlarm() {
        uint8_t* tx_buffer = comm_.getTxBuffer();

        tx_buffer[0] = OPCODE_WRITE;
        tx_buffer[1] = ALARM0_START_ADDRESS;

        // Seconds register (BCD encoding)
        tx_buffer[2] = alarm_time_.tm_sec / 10;
        tx_buffer[2] = ((tx_buffer[2] << 4) & 0x70);
        tx_buffer[2] |= ((alarm_time_.tm_sec % 10) & 0x0F);

        // Minutes register (BCD encoding)
        tx_buffer[3] = alarm_time_.tm_min / 10;
        tx_buffer[3] = ((tx_buffer[3] << 4) & 0x70);
        tx_buffer[3] |= ((alarm_time_.tm_min % 10) & 0x0F);

        // Hours register (BCD encoding)
        tx_buffer[4] = alarm_time_.tm_hour / 10;
        tx_buffer[4] = ((tx_buffer[4] << 4) & 0x30);
        tx_buffer[4] |= ((alarm_time_.tm_hour % 10) & 0x0F);
        // Hours register 24h format
        tx_buffer[4] |= 0x70;

        tx_buffer[5] = 0;

        // Clear interrupt flag
        tx_buffer[5] &= ~0x08;

        // Set alarm mask
        tx_buffer[5] |= (SECOND_ALARM_MASK << 4);

        // Associate alarm pin to IRQ output
        tx_buffer[5] &= ~0x08;

        // Day of month register (BCD encoding)
        tx_buffer[6] = alarm_time_.tm_mday / 10;
        tx_buffer[6] = ((tx_buffer[6] << 4) & 0x30);
        tx_buffer[6] |= ((alarm_time_.tm_mday % 10) & 0x0F);

        // Month register (BCD encoding, tm_mon is 0-11, RTC uses 1-12)
        uint8_t month = alarm_time_.tm_mon + 1;
        tx_buffer[7] = month / 10;
        tx_buffer[7] = ((tx_buffer[7] << 4) & 0x10);
        tx_buffer[7] |= ((month % 10) & 0x0F);

        this->transmit(tx_buffer, ALARM0_SEND_TRANSMISSION_LENGTH);
    }
};

} // namespace devices
} // namespace chipz

#endif // CHIPZ_DEVICES_MCP795W_HPP
