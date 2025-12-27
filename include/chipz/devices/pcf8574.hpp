#ifndef CHIPZ_DEVICES_PCF8574_HPP
#define CHIPZ_DEVICES_PCF8574_HPP

/**
 * @file pcf8574.hpp
 * @brief PCF8574 I2C GPIO Expander Driver
 *
 * The PCF8574 is an 8-bit I/O expander for the I2C bus. It provides general-purpose
 * remote I/O expansion via the two-wire bidirectional I2C bus (serial clock - SCL,
 * serial data - SDA).
 *
 * @section features Key Features
 * - 8-bit I/O expander (P0-P7)
 * - I2C-bus interface (100 kHz, 400 kHz)
 * - Quasi-bidirectional I/Os with interrupt capability
 * - Simple write/read interface (no internal registers)
 * - Low power consumption
 * - Common use: LCD display control (HD44780), LED control, keypad scanning
 *
 * @section usage Usage
 *
 * @code{.cpp}
 * // Create I2C interface
 * chipz::interfaces::I2CInterface i2c(0x27,  // PCF8574 address
 *     [](uint8_t addr, uint8_t reg, uint8_t* data, uint16_t size) -> int {
 *         return HAL_I2C_Master_Transmit_IT(&hi2c3, addr << 1, data, size);
 *     },
 *     [](uint8_t addr, uint8_t reg, const uint8_t* data, uint16_t size) -> int {
 *         return HAL_I2C_Master_Receive_IT(&hi2c3, addr << 1,
 *                                          const_cast<uint8_t*>(data), size);
 *     }
 * );
 *
 * // Create PCF8574 device
 * chipz::devices::PCF8574<chipz::interfaces::I2CInterface> gpio(i2c);
 * gpio.initialize();
 *
 * // Write to outputs
 * gpio.write(0xFF);  // All pins high
 * gpio.write(0x00);  // All pins low
 *
 * // Read inputs (must set pins high first to enable pull-ups)
 * gpio.write(0xFF);
 * uint8_t inputs = gpio.read();
 *
 * // Individual pin control
 * gpio.setPin(3, true);   // Set P3 high
 * gpio.clearPin(5);       // Set P5 low
 * gpio.togglePin(7);      // Toggle P7
 * @endcode
 *
 * @section hd44780_example HD44780 LCD Control Example
 *
 * @code{.cpp}
 * // Pin mapping for HD44780:
 * // P7-P4: D7-D4 (data lines)
 * // P3: BL (backlight)
 * // P2: E (enable)
 * // P1: RW (read/write)
 * // P0: RS (register select)
 *
 * void sendNibbleToLCD(uint8_t nibble, bool rs, bool e) {
 *     uint8_t data = 0;
 *     data |= (nibble & 0x0F) << 4;  // D4-D7
 *     data |= rs ? 0x01 : 0x00;      // RS
 *     data |= e ? 0x04 : 0x00;       // E
 *     data |= 0x08;                  // Backlight on
 *     gpio.write(data);
 * }
 * @endcode
 *
 * @section hardware Hardware Considerations
 *
 * **Pull-ups**: PCF8574 outputs are quasi-bidirectional with weak pull-ups.
 * For reading inputs, write 1 to the pin first to enable the pull-up.
 *
 * **I2C Address**: PCF8574 has a fixed base address (0x20 or 0x27 depending on variant)
 * plus 3 address pins (A0-A2) for up to 8 devices on the same bus.
 *
 * **Interrupt Output**: PCF8574 has an INT pin that goes low when input changes.
 * This driver does not currently support interrupt-driven input reading.
 *
 * @author Chipz Library
 * @date 2025-12-27
 */

#include "../peripheral.hpp"
#include "../communication_interface.hpp"
#include <cstdint>
#include <functional>

namespace chipz {
namespace devices {

/**
 * @brief PCF8574 I2C GPIO Expander Device Driver
 *
 * Template-based driver for the PCF8574 8-bit I/O expander.
 * Uses dependency injection for communication interface abstraction.
 *
 * @tparam CommInterface Communication interface type (must support I2C operations)
 *
 * @note PCF8574 has no internal registers. Read/write operations directly
 *       access the I/O pins. Writing 1 enables weak pull-up for input reading.
 */
template<typename CommInterface>
class PCF8574 : public Peripheral {
public:
    /**
     * @brief Pin numbers for individual pin access
     */
    enum class Pin : uint8_t {
        P0 = 0,
        P1 = 1,
        P2 = 2,
        P3 = 3,
        P4 = 4,
        P5 = 5,
        P6 = 6,
        P7 = 7
    };

    /**
     * @brief Construct PCF8574 device
     *
     * @param comm Reference to communication interface (I2CInterface)
     * @param get_tick Optional function to get system tick in milliseconds
     * @param transfer_complete_callback Optional callback when I2C transfer completes
     *
     * @note The PCF8574 I2C address must be configured in the CommInterface.
     *       Common addresses: 0x20-0x27 (PCF8574) or 0x38-0x3F (PCF8574A)
     * @note The transfer_complete_callback is useful when PCF8574 is used to
     *       control another device (e.g., HD44780 LCD via GPIO pins)
     */
    PCF8574(CommInterface& comm,
            std::function<uint32_t()> get_tick = nullptr,
            std::function<void()> transfer_complete_callback = nullptr)
        : Peripheral()
        , comm_(comm)
        , get_tick_(get_tick)
        , transfer_complete_callback_(transfer_complete_callback)
        , status_(Status::Uninitialized)
        , output_state_(0xFF)  // All pins high (safe default)
        , input_state_(0xFF)
        , write_request_(false)
        , read_request_(false)
        , transfer_in_progress_(false)
    {
        // Register callback for transfer completion
        comm_.setTransferCompleteCallback(
            [this](bool success) { this->onTransferComplete(success); }
        );
    }

    /**
     * @brief Initialize the PCF8574
     *
     * Sets all pins high (enables pull-ups) and marks device as ready.
     * PCF8574 has no initialization sequence - it's always ready after power-on.
     *
     * @return true if initialization successful, false otherwise
     */
    bool initialize() override {
        if (!comm_.isReady()) {
            return false;
        }

        // Set all pins high (default safe state, enables pull-ups)
        output_state_ = 0xFF;
        write_request_ = false;
        read_request_ = false;
        transfer_in_progress_ = false;
        status_ = Status::Ready;

        return true;
    }

    /**
     * @brief Reset the PCF8574 to default state
     *
     * Sets all pins high (default state).
     *
     * @return true if reset successful
     */
    bool reset() override {
        output_state_ = 0xFF;
        status_ = Status::Ready;
        return write(output_state_);
    }

    /**
     * @brief Check if device is ready for operations
     *
     * @return true if device is initialized and not busy
     */
    bool isReady() const override {
        return (status_ == Status::Ready) && !transfer_in_progress_;
    }

    /**
     * @brief Get current device status
     *
     * @return Current status (Uninitialized, Ready, Busy, Error)
     */
    Status getStatus() const override {
        if (transfer_in_progress_) {
            return Status::Busy;
        }
        return status_;
    }

    /**
     * @brief Get device identification string
     *
     * @return Device ID string "PCF8574"
     */
    std::string getDeviceId() const override {
        return "PCF8574";
    }

    /**
     * @brief Main periodic function - processes queued write/read requests
     *
     * PCF8574 uses a request/process model to avoid dropping writes:
     * - write() queues the request (always succeeds)
     * - main() processes the request when I2C is not busy
     *
     * This prevents write loss when multiple rapid calls occur (e.g., HD44780 init)
     *
     * @return true always
     */
    bool main() override {
        // Process pending write request
        if (write_request_ && !transfer_in_progress_ && status_ == Status::Ready) {
            write_request_ = false;
            transfer_in_progress_ = true;

            uint8_t* tx_buffer = comm_.getTxBuffer();
            tx_buffer[0] = output_state_;

            bool success = comm_.transmit(tx_buffer, 1);
            if (!success) {
                transfer_in_progress_ = false;
                status_ = Status::Error;
            }
        }

        // Process pending read request
        if (read_request_ && !transfer_in_progress_ && status_ == Status::Ready) {
            read_request_ = false;
            transfer_in_progress_ = true;

            uint8_t* rx_buffer = comm_.getRxBuffer();
            bool success = comm_.receive(rx_buffer, 1);
            if (!success) {
                transfer_in_progress_ = false;
                status_ = Status::Error;
            }
        }

        return true;
    }

    /**
     * @brief Write byte to PCF8574 outputs
     *
     * Queues a write request. The actual I2C transfer happens in main().
     * This ensures no writes are dropped when called rapidly (e.g., HD44780 init).
     *
     * @param data 8-bit value to write to P0-P7 pins
     * @return true always (write is queued, not executed immediately)
     *
     * @note PCF8574 has no internal registers. This directly sets pin states.
     * @note To read inputs, write 1 to those pins first (enables pull-up).
     * @note Call main() periodically to process the queued write request.
     */
    bool write(uint8_t data) {
        output_state_ = data;
        write_request_ = true;
        return true;
    }

    /**
     * @brief Read byte from PCF8574 inputs
     *
     * Queues a read request. The actual I2C transfer happens in main().
     * Result available after transfer completion via getInputState().
     *
     * @return true always (read is queued, not executed immediately)
     *
     * @note Before reading, write 1 to pins you want to read (enables pull-up).
     * @note Call main() periodically to process the queued read request.
     * @note Use getInputState() to get result after transfer completes.
     */
    bool read() {
        read_request_ = true;
        return true;
    }

    /**
     * @brief Set specific pin high
     *
     * @param pin Pin number (0-7) or Pin enum
     * @return true if write operation started successfully
     */
    bool setPin(uint8_t pin) {
        if (pin > 7) return false;
        return write(output_state_ | (1 << pin));
    }

    bool setPin(Pin pin, bool value) {
        return value ? setPin(static_cast<uint8_t>(pin))
                     : clearPin(static_cast<uint8_t>(pin));
    }

    /**
     * @brief Set specific pin low
     *
     * @param pin Pin number (0-7) or Pin enum
     * @return true if write operation started successfully
     */
    bool clearPin(uint8_t pin) {
        if (pin > 7) return false;
        return write(output_state_ & ~(1 << pin));
    }

    bool clearPin(Pin pin) {
        return clearPin(static_cast<uint8_t>(pin));
    }

    /**
     * @brief Toggle specific pin
     *
     * @param pin Pin number (0-7) or Pin enum
     * @return true if write operation started successfully
     */
    bool togglePin(uint8_t pin) {
        if (pin > 7) return false;
        return write(output_state_ ^ (1 << pin));
    }

    bool togglePin(Pin pin) {
        return togglePin(static_cast<uint8_t>(pin));
    }

    /**
     * @brief Get current output state
     *
     * Returns the last value written to the device.
     * This is NOT a read from the device - use read() for that.
     *
     * @return Last written output state (0x00-0xFF)
     */
    uint8_t getOutputState() const {
        return output_state_;
    }

    /**
     * @brief Get last read input state
     *
     * Returns the result of the last successful read() operation.
     * To get current state, call read() and wait for completion.
     *
     * @return Last read input state (0x00-0xFF)
     */
    uint8_t getInputState() const {
        return input_state_;
    }

    /**
     * @brief Get state of specific pin from last read
     *
     * @param pin Pin number (0-7) or Pin enum
     * @return true if pin was high in last read
     */
    bool getPinState(uint8_t pin) const {
        if (pin > 7) return false;
        return (input_state_ & (1 << pin)) != 0;
    }

    bool getPinState(Pin pin) const {
        return getPinState(static_cast<uint8_t>(pin));
    }

private:
    /**
     * @brief Handle transfer completion callback
     *
     * Called by communication interface when I2C transfer completes.
     * Updates input state if this was a read operation.
     * Calls external callback if registered (e.g., to notify HD44780).
     *
     * @param success true if transfer succeeded, false on error
     */
    void onTransferComplete(bool success) {
        transfer_in_progress_ = false;

        if (!success) {
            status_ = Status::Error;
            return;
        }

        // If this was a read, update input state
        // (We can't distinguish read from write here, so we always update)
        input_state_ = comm_.getRxBuffer()[0];

        status_ = Status::Ready;

        // Notify external component if callback is registered
        if (transfer_complete_callback_) {
            transfer_complete_callback_();
        }
    }

    CommInterface& comm_;                          ///< Communication interface reference
    std::function<uint32_t()> get_tick_;          ///< System tick function (unused)
    std::function<void()> transfer_complete_callback_;  ///< Transfer complete callback
    Status status_;                                ///< Current device status
    uint8_t output_state_;                        ///< Last written output state
    uint8_t input_state_;                         ///< Last read input state
    bool write_request_;                          ///< Write request pending (queued)
    bool read_request_;                           ///< Read request pending (queued)
    bool transfer_in_progress_;                   ///< I2C transfer in progress flag
};

} // namespace devices
} // namespace chipz

#endif // CHIPZ_DEVICES_PCF8574_HPP
