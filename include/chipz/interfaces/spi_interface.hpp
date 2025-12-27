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
 *
 * Supports async (interrupt-driven) SPI operations by default.
 * Call handleInterrupt() from your HAL SPI complete callbacks.
 */
class SPIInterface : public CommunicationInterface {
public:
    /**
     * @brief Function pointer type for SPI transfer operation (async)
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
     * @param transfer_func Function to perform async SPI transfer
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
        // Buffers start empty and will be allocated on first use
    }

    /**
     * @brief Transmit data via SPI (async)
     * Note: SPI is full-duplex, so this also receives data into rx_buffer
     * @param data Pointer to data to transmit
     * @param length Number of bytes to transmit
     * @return true if transmission started successfully, false otherwise
     *
     * Note: This starts an async operation. Call handleInterrupt() when complete.
     */
    bool transmit(const uint8_t* data, size_t length) override {
        if (transfer_in_progress_ || !spi_transfer_) {
            return false;
        }

        // Ensure RX buffer is allocated (SPI is full-duplex, always receives)
        if (rx_buffer_.size() < length) {
            rx_buffer_.resize(length);
        }

        transfer_in_progress_ = true;

        // Assert chip select (if provided)
        if (chip_select_) {
            chip_select_(true);
        }

        // Start async SPI transfer - pass data buffer directly to HAL
        int result = spi_transfer_(const_cast<uint8_t*>(data),
                                   rx_buffer_.data(),
                                   static_cast<uint16_t>(length));

        if (result != 0) {
            // Failed to start - reset state
            transfer_in_progress_ = false;
            if (chip_select_) {
                chip_select_(false);
            }
            return false;
        }

        // Transfer started successfully - completion will be signaled via handleInterrupt()
        return true;
    }

    /**
     * @brief Receive data via SPI (async)
     * Note: SPI requires transmitting to receive. This sends zeros (dummy bytes)
     * @param buffer Pointer to buffer where received data will be stored
     * @param length Number of bytes to receive
     * @return true if reception started successfully, false otherwise
     *
     * Note: This starts an async operation. Call handleInterrupt() when complete.
     */
    bool receive(uint8_t* buffer, size_t length) override {
        if (transfer_in_progress_ || !spi_transfer_) {
            return false;
        }

        // Ensure TX buffer is large enough and fill with zeros (dummy bytes for SPI receive)
        if (tx_buffer_.size() < length) {
            tx_buffer_.resize(length, 0x00);
        } else {
            // Buffer exists but might not be all zeros, fill the needed portion
            for (size_t i = 0; i < length; ++i) {
                tx_buffer_[i] = 0x00;
            }
        }

        transfer_in_progress_ = true;

        // Assert chip select (if provided)
        if (chip_select_) {
            chip_select_(true);
        }

        // Start async SPI transfer - send dummy bytes, receive into buffer
        int result = spi_transfer_(tx_buffer_.data(), buffer,
                                   static_cast<uint16_t>(length));

        if (result != 0) {
            // Failed to start - reset state
            transfer_in_progress_ = false;
            if (chip_select_) {
                chip_select_(false);
            }
            return false;
        }

        // Transfer started successfully - completion will be signaled via handleInterrupt()
        return true;
    }

    /**
     * @brief Handle SPI transfer completion (call from HAL callbacks)
     * @param success True if transfer succeeded, false on error
     *
     * Call this from HAL_SPI_TxRxCpltCallback, HAL_SPI_RxCpltCallback,
     * or HAL_SPI_ErrorCallback
     */
    void handleInterrupt(bool success) {
        transfer_in_progress_ = false;

        // Deassert chip select (if provided)
        if (chip_select_) {
            chip_select_(false);
        }

        // Notify registered callback (this will call device's onTransferComplete)
        // The HAL has already filled/sent the buffer that was passed to transmit/receive
        notifyTransferComplete(success);
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
