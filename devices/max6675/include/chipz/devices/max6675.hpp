#ifndef CHIPZ_DEVICES_MAX6675_HPP
#define CHIPZ_DEVICES_MAX6675_HPP

#include <chipz/peripheral.hpp>
#include <chipz/interfaces/spi_interface.hpp>
#include <cstdint>
#include <functional>

namespace chipz {
namespace devices {

/**
 * @brief Driver for MAX6675 Cold-Junction-Compensated K-Thermocouple-to-Digital Converter
 *
 * The MAX6675 performs cold-junction compensation and digitizes the
 * signal from a K-type thermocouple. The data is output in a 12-bit
 * resolution, SPI-compatible, read-only format.
 *
 * This implementation mirrors the design from the C version,
 * providing full compatibility with embedded systems requirements.
 *
 * Temperature resolution: 0.25°C (12-bit)
 * Update rate: Approximately every 220ms (internal MAX6675 conversion)
 * Reading period: 1000ms (configurable)
 *
 * @tparam CommInterface Communication interface type (typically SPI)
 */
class MAX6675 : public Peripheral<interfaces::SPIInterface> {
public:
    /**
     * @brief Construct MAX6675 driver with communication interface
     * @param comm Reference to communication interface (SPI)
     * @param get_tick Function to get current system tick in milliseconds
     */
    MAX6675(interfaces::SPIInterface& comm, std::function<uint32_t()> get_tick = nullptr)
        : Peripheral<interfaces::SPIInterface>(comm)
        , status_(Status::Uninitialized)
        , temperature_(0)
        , tick_timer_(0)
        , last_tick_(0)
        , connection_open_(false)
        , transfer_in_progress_(false)
        , get_tick_(get_tick)
    {
    }

    // Peripheral interface implementation
    bool initialize() override {
        if (!comm_.isReady()) {
            status_ = Status::Error;
            return false;
        }

        // Reset state
        temperature_ = 0;
        tick_timer_ = 0;
        connection_open_ = false;
        transfer_in_progress_ = false;

        if (get_tick_) {
            last_tick_ = get_tick_();
        }

        status_ = Status::Ready;
        return true;
    }

    bool reset() override {
        status_ = Status::Uninitialized;
        return initialize();
    }

    bool isReady() const override {
        return status_ == Status::Ready && comm_.isReady();
    }

    Status getStatus() const override {
        return status_;
    }

    std::string getDeviceId() const override {
        return "MAX6675 Thermocouple Converter";
    }

    bool main() override {
        if (status_ != Status::Ready) {
            return false;
        }

        // Skip if transfer is in progress
        if (transfer_in_progress_) {
            return true;
        }

        // Calculate elapsed time since last call
        if (get_tick_) {
            uint32_t current_tick = get_tick_();
            uint32_t elapsed_ms = current_tick - last_tick_;
            last_tick_ = current_tick;

            // Increment tick timer
            tick_timer_ += elapsed_ms;
        }

        // Read temperature at specified period
        if (tick_timer_ >= READ_PERIOD_MS) {
            tick_timer_ = 0;
            transfer_in_progress_ = true;

            // Start SPI transfer (read 2 bytes)
            if (!this->receive(comm_.getRxBuffer(), SPI_TRANSFER_LENGTH)) {
                transfer_in_progress_ = false;
                status_ = Status::Error;
                return false;
            }
        }

        return true;
    }

    // MAX6675-specific interface

    /**
     * @brief Get last measured temperature in Celsius
     * @return Temperature in degrees Celsius
     */
    uint32_t getTemperature() const {
        return temperature_;
    }

    /**
     * @brief Get last measured temperature in Celsius (float)
     * @return Temperature in degrees Celsius with 0.25°C resolution
     */
    float getTemperatureCelsius() const {
        return static_cast<float>(temperature_) * RESOLUTION;
    }

    /**
     * @brief Get last measured temperature in Fahrenheit
     * @return Temperature in degrees Fahrenheit
     */
    float getTemperatureFahrenheit() const {
        return (getTemperatureCelsius() * 9.0f / 5.0f) + 32.0f;
    }

    /**
     * @brief Check if thermocouple is connected
     * @return true if thermocouple is connected, false if open/disconnected
     */
    bool isThermocoupleConnected() const {
        return !connection_open_;
    }

private:
    Status status_;

    // Temperature data
    uint32_t temperature_;      // Temperature in raw units (divide by 4 for °C, or multiply by 0.25)
    uint32_t tick_timer_;       // Tick counter for timing
    uint32_t last_tick_;        // Last tick value
    bool connection_open_;      // True if thermocouple is disconnected
    bool transfer_in_progress_; // True if SPI transfer is active

    std::function<uint32_t()> get_tick_;

    // MAX6675 specifications
    static constexpr float RESOLUTION = 0.25f;        // °C per bit
    static constexpr float MIN_TEMP = 0.0f;           // °C
    static constexpr float MAX_TEMP = 1024.0f;        // °C
    static constexpr uint16_t SPI_TRANSFER_LENGTH = 2; // 2 bytes to read
    static constexpr uint32_t READ_PERIOD_MS = 1000;  // Read every 1000ms

    /**
     * @brief SPI transfer completion callback
     * @param success True if transfer succeeded, false on error
     */
    void onTransferComplete(bool success) override {
        transfer_in_progress_ = false;

        if (!success) {
            status_ = Status::Error;
            return;
        }

        // Deserialize SPI data
        deserializeSpiData();
    }

    /**
     * @brief Deserialize temperature data from SPI receive buffer
     *
     * MAX6675 output format (16 bits, MSB first):
     * D15-D5: 12-bit temperature data (bits 14:3)
     * D2: Thermocouple input open (1 = open, 0 = connected)
     * D1: Device ID (always 0)
     * D0: Three-state (always 0)
     *
     * Temperature calculation:
     * - Extract bits 14:3 (12-bit value)
     * - Each LSB = 0.25°C
     * - Temperature (°C) = 12-bit value * 0.25
     */
    void deserializeSpiData() {
        const uint8_t* rx_buffer = comm_.getRxBuffer();

        // Extract 12-bit temperature value
        // rx_buffer[0] contains D15-D8 (bits 7:0 = D15:D8)
        // rx_buffer[1] contains D7-D0 (bits 7:0 = D7:D0)
        //
        // Temperature is in bits D14:D3
        // rx_buffer[0] >> 3 gives us bits D14:D11 in positions 4:1
        // rx_buffer[1] << 5 gives us bits D10:D8 in positions 12:10
        // We need bits 14:3, so shift right by 3 positions total

        temperature_ = (static_cast<uint32_t>(rx_buffer[0]) >> 3);
        temperature_ |= (static_cast<uint32_t>(rx_buffer[1]) << 5);
        temperature_ = temperature_ / 4; // Divide by 4 to get integer representation (each count = 0.25°C)

        // Check thermocouple connection status (bit 2 of first byte)
        connection_open_ = ((rx_buffer[0] & 0x04) == 0x04);
    }
};

} // namespace devices
} // namespace chipz

#endif // CHIPZ_DEVICES_MAX6675_HPP
