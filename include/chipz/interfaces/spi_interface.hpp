#ifndef CHIPZ_INTERFACES_SPI_INTERFACE_HPP
#define CHIPZ_INTERFACES_SPI_INTERFACE_HPP

#include "../communication_interface.hpp"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <functional>

namespace chipz {
namespace interfaces {

/**
 * @brief SPI Communication Interface implementation
 *
 * Wraps low-level SPI transfer operations (typically C HAL functions)
 * and provides the CommunicationInterface abstraction.
 *
 * This class is designed to wrap hardware abstraction layers like STM32 HAL,
 * Arduino SPI, or custom SPI implementations.
 */
class SPIInterface : public CommunicationInterface {
public:
    /**
     * @brief Function pointer type for SPI transfer operation
     * SPI is full-duplex: transmits and receives simultaneously
     * @param tx_buffer Pointer to data to transmit
     * @param rx_buffer Pointer to buffer for received data
     * @param size Number of bytes to transfer
     * @return 0 on success, error code otherwise
     */
    using SPITransferFunction = std::function<int(uint8_t* tx_buffer,
                                                   uint8_t* rx_buffer,
                                                   uint16_t size)>;

    /**
     * @brief Function pointer type for chip select control
     * @param select true to assert CS (select chip), false to deassert
     */
    using ChipSelectFunction = std::function<void(bool select)>;

    /**
     * @brief Construct SPI interface with low-level SPI functions
     * @param transfer_func Function to perform SPI transfer
     * @param cs_func Function to control chip select (optional)
     *
     * Note: Buffers are not pre-allocated. They are allocated on first use
     * and grow as needed to minimize heap allocations.
     */
    SPIInterface(SPITransferFunction transfer_func,
                 ChipSelectFunction cs_func = nullptr)
        : CommunicationInterface()  // Initialize base class
        , spi_transfer_(transfer_func)
        , chip_select_(cs_func)
    {
        // Buffers in base class start empty and grow on demand
    }

    /**
     * @brief Transmit data via SPI
     * Note: SPI is full-duplex, so this also receives data into rx_buffer
     * @param data Pointer to data to transmit
     * @param length Number of bytes to transmit
     * @return true if transmission started successfully, false otherwise
     */
    bool transmit(const uint8_t* data, size_t length) override {
        if (transfer_in_progress_ || !spi_transfer_) {
            return false;
        }

        // Ensure buffers are large enough (allocates on first use, grows if needed)
        ensureBufferSize(tx_buffer_, length);
        ensureBufferSize(rx_buffer_, length);

        // Copy data to internal buffer
        for (size_t i = 0; i < length; ++i) {
            tx_buffer_[i] = data[i];
        }

        transfer_in_progress_ = true;

        // Assert chip select (if provided)
        if (chip_select_) {
            chip_select_(true);
        }

        // Perform SPI transfer (full-duplex)
        int result = spi_transfer_(tx_buffer_.data(), rx_buffer_.data(),
                                   static_cast<uint16_t>(length));

        // Deassert chip select (if provided)
        if (chip_select_) {
            chip_select_(false);
        }

        transfer_in_progress_ = false;

        // Notify completion
        notifyTransferComplete(result == 0);

        return (result == 0);
    }

    /**
     * @brief Receive data via SPI
     * Note: SPI requires transmitting to receive. This sends zeros (dummy bytes)
     * @param buffer Pointer to buffer where received data will be stored
     * @param length Number of bytes to receive
     * @return true if reception started successfully, false otherwise
     */
    bool receive(uint8_t* buffer, size_t length) override {
        if (transfer_in_progress_ || !spi_transfer_) {
            return false;
        }

        // Ensure buffers are large enough (allocates on first use, grows if needed)
        ensureBufferSize(tx_buffer_, length);
        ensureBufferSize(rx_buffer_, length);

        // Fill TX buffer with dummy bytes (zeros) for receive operation
        for (size_t i = 0; i < length; ++i) {
            tx_buffer_[i] = 0x00;
        }

        transfer_in_progress_ = true;

        // Assert chip select (if provided)
        if (chip_select_) {
            chip_select_(true);
        }

        // Perform SPI transfer (send dummy bytes, receive data)
        int result = spi_transfer_(tx_buffer_.data(), rx_buffer_.data(),
                                   static_cast<uint16_t>(length));

        // Deassert chip select (if provided)
        if (chip_select_) {
            chip_select_(false);
        }

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
     * @brief Perform a full-duplex SPI transfer (transmit and receive simultaneously)
     * @param tx_data Pointer to data to transmit
     * @param rx_data Pointer to buffer for received data
     * @param length Number of bytes to transfer
     * @return true if transfer successful, false otherwise
     */
    bool transfer(const uint8_t* tx_data, uint8_t* rx_data, size_t length) {
        if (transfer_in_progress_ || !spi_transfer_) {
            return false;
        }

        // Ensure buffers are large enough (allocates on first use, grows if needed)
        ensureBufferSize(tx_buffer_, length);
        ensureBufferSize(rx_buffer_, length);

        // Copy TX data to internal buffer
        for (size_t i = 0; i < length; ++i) {
            tx_buffer_[i] = tx_data[i];
        }

        transfer_in_progress_ = true;

        // Assert chip select (if provided)
        if (chip_select_) {
            chip_select_(true);
        }

        // Perform SPI transfer
        int result = spi_transfer_(tx_buffer_.data(), rx_buffer_.data(),
                                   static_cast<uint16_t>(length));

        // Deassert chip select (if provided)
        if (chip_select_) {
            chip_select_(false);
        }

        // Copy RX data from internal buffer
        if (result == 0) {
            for (size_t i = 0; i < length; ++i) {
                rx_data[i] = rx_buffer_[i];
            }
        }

        transfer_in_progress_ = false;

        // Notify completion
        notifyTransferComplete(result == 0);

        return (result == 0);
    }

    /**
     * @brief Manually control chip select
     * @param select true to assert CS (select), false to deassert
     */
    void setChipSelect(bool select) {
        if (chip_select_) {
            chip_select_(select);
        }
    }

private:
    SPITransferFunction spi_transfer_;
    ChipSelectFunction chip_select_;
    // Note: tx_buffer_, rx_buffer_, and transfer_in_progress_ are in base class
};

} // namespace interfaces
} // namespace chipz

#endif // CHIPZ_INTERFACES_SPI_INTERFACE_HPP
