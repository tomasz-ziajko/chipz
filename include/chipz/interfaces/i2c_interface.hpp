#ifndef CHIPZ_INTERFACES_I2C_INTERFACE_HPP
#define CHIPZ_INTERFACES_I2C_INTERFACE_HPP

#include "../communication_interface.hpp"
#include <cstdint>
#include <cstddef>
#include <array>
#include <functional>

namespace chipz {
namespace interfaces {

/**
 * @brief I2C Communication Interface implementation
 *
 * Wraps low-level I2C read/write operations (typically C HAL functions)
 * and provides the CommunicationInterface abstraction.
 *
 * This class is designed to wrap hardware abstraction layers like STM32 HAL,
 * Arduino Wire, or custom I2C implementations.
 */
class I2CInterface : public CommunicationInterface {
public:
    /**
     * @brief Function pointer type for I2C memory read operation
     * @param device_address I2C device address (7-bit or 8-bit)
     * @param mem_address Memory/register address to read from
     * @param data Pointer to buffer for received data
     * @param size Number of bytes to read
     * @return 0 on success, error code otherwise
     */
    using I2CReadFunction = std::function<int(uint8_t device_address, uint8_t mem_address,
                                              uint8_t* data, uint16_t size)>;

    /**
     * @brief Function pointer type for I2C memory write operation
     * @param device_address I2C device address (7-bit or 8-bit)
     * @param mem_address Memory/register address to write to
     * @param data Pointer to data to transmit
     * @param size Number of bytes to write
     * @return 0 on success, error code otherwise
     */
    using I2CWriteFunction = std::function<int(uint8_t device_address, uint8_t mem_address,
                                               const uint8_t* data, uint16_t size)>;

    /**
     * @brief Construct I2C interface with low-level I2C functions and device address
     * @param device_address I2C device address (e.g., 0x68 for DS3231)
     * @param read_func Function to perform I2C read operation
     * @param write_func Function to perform I2C write operation
     *
     * Note: Buffers are not pre-allocated. They are allocated on first use
     * and grow as needed to minimize heap allocations.
     */
    I2CInterface(uint8_t device_address,
                 I2CReadFunction read_func,
                 I2CWriteFunction write_func)
        : CommunicationInterface()  // Initialize base class
        , device_address_(device_address)
        , i2c_read_(read_func)
        , i2c_write_(write_func)
        , current_mem_address_(0)
    {
        // Buffers in base class start empty and grow on demand
    }

    /**
     * @brief Transmit data to I2C device at specified memory address
     * @param data Pointer to data to transmit
     * @param length Number of bytes to transmit
     * @return true if transmission started successfully, false otherwise
     */
    bool transmit(const uint8_t* data, size_t length) override {
        if (transfer_in_progress_ || !i2c_write_) {
            return false;
        }

        // Ensure buffer is large enough (allocates on first use, grows if needed)
        ensureBufferSize(tx_buffer_, length);

        // Copy data to internal buffer
        for (size_t i = 0; i < length; ++i) {
            tx_buffer_[i] = data[i];
        }

        transfer_in_progress_ = true;

        // Perform I2C write operation
        int result = i2c_write_(device_address_, current_mem_address_,
                                tx_buffer_.data(), static_cast<uint16_t>(length));

        transfer_in_progress_ = false;

        // Notify completion
        notifyTransferComplete(result == 0);

        return (result == 0);
    }

    /**
     * @brief Receive data from I2C device at specified memory address
     * @param buffer Pointer to buffer where received data will be stored
     * @param length Number of bytes to receive
     * @return true if reception started successfully, false otherwise
     */
    bool receive(uint8_t* buffer, size_t length) override {
        if (transfer_in_progress_ || !i2c_read_) {
            return false;
        }

        // Ensure buffer is large enough (allocates on first use, grows if needed)
        ensureBufferSize(rx_buffer_, length);

        transfer_in_progress_ = true;

        // Perform I2C read operation
        int result = i2c_read_(device_address_, current_mem_address_,
                               rx_buffer_.data(), static_cast<uint16_t>(length));

        // Copy from internal buffer to user buffer
        if (result == 0) {
            for (size_t i = 0; i < length; ++i) {
                buffer[i] = rx_buffer_[i];
            }
        }

        transfer_in_progress_ = false;

        // Notify completion
        notifyTransferComplete(result == 0);

        return (result == 0);
    }

    /**
     * @brief Set the memory address for next I2C operation
     * @param mem_address Memory/register address
     */
    void setMemoryAddress(uint8_t mem_address) {
        current_mem_address_ = mem_address;
    }

    /**
     * @brief Get current memory address
     * @return Current memory address
     */
    uint8_t getMemoryAddress() const {
        return current_mem_address_;
    }

    /**
     * @brief Get I2C device address
     * @return Device address
     */
    uint8_t getDeviceAddress() const {
        return device_address_;
    }

private:
    uint8_t device_address_;
    I2CReadFunction i2c_read_;
    I2CWriteFunction i2c_write_;
    uint8_t current_mem_address_;
    // Note: tx_buffer_, rx_buffer_, and transfer_in_progress_ are in base class
};

} // namespace interfaces
} // namespace chipz

#endif // CHIPZ_INTERFACES_I2C_INTERFACE_HPP
