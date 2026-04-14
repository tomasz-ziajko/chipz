// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

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
     * @brief Construct a bus-level SPI interface
     * @param transfer_func Function to perform SPI transfer
     *
     * No chip-select is bound at construction — CS is device-specific and
     * registered per-device via registerConnection(). Drivers call
     * setConnection() with the returned ConnectionId so selectConnection()
     * asserts the correct CS pin before every transfer.
     */
    explicit SPIInterface(SPITransferFunction transfer_func)
        : CommunicationInterface()
        , spi_transfer_(transfer_func)
    {}

    /**
     * @brief Register a device on this SPI bus
     *
     * @param cs_func Function to assert/deassert this device's chip-select pin
     * @return ConnectionId to pass to Chip::setConnection()
     */
    ConnectionId registerConnection(ChipSelectFunction cs_func) {
        ConnectionId id = nextId();
        connections_.push_back(std::move(cs_func));
        return id;
    }

    void selectConnection(ConnectionId id) override {
        // TODO: handle invalid / out-of-range id
        active_cs_ = connections_[id];
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

        if (active_cs_) active_cs_(true);

        // Start async SPI transfer — completion notified via ISR callback.
        // CS is asserted here; deassert belongs in the completion path.
        // With hardware NSS (SPI_NSS_HARD_OUTPUT) active_cs_ is null and the
        // HAL manages the NSS pin automatically.
        int result = spi_transfer_(tx_buffer_.data(), rx_buffer_.data(),
                                   static_cast<uint16_t>(length));

        if (result != 0) {
            if (active_cs_) active_cs_(false);
            notifyError();
            return false;
        }

        return true;
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

        ensureBufferSize(tx_buffer_, length);
        ensureBufferSize(rx_buffer_, length);

        for (size_t i = 0; i < length; ++i) tx_buffer_[i] = 0x00;

        transfer_in_progress_ = true;

        if (active_cs_) active_cs_(true);

        int result = spi_transfer_(tx_buffer_.data(), rx_buffer_.data(),
                                   static_cast<uint16_t>(length));

        if (result != 0) {
            if (active_cs_) active_cs_(false);
            notifyError();
            return false;
        }

        // Copy into caller-provided buffer if different from rx_buffer_
        if (buffer != rx_buffer_.data()) {
            for (size_t i = 0; i < length; ++i) buffer[i] = rx_buffer_[i];
        }

        return true;
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

        if (active_cs_) active_cs_(true);

        int result = spi_transfer_(tx_buffer_.data(), rx_buffer_.data(),
                                   static_cast<uint16_t>(length));

        if (result != 0) {
            if (active_cs_) active_cs_(false);
            notifyError();
            return false;
        }

        if (rx_data != rx_buffer_.data()) {
            for (size_t i = 0; i < length; ++i) rx_data[i] = rx_buffer_[i];
        }

        return true;
    }

    /**
     * @brief Manually control chip select
     * @param select true to assert CS (select), false to deassert
     */
private:
    SPITransferFunction             spi_transfer_;
    ChipSelectFunction              active_cs_;
    std::vector<ChipSelectFunction> connections_;
    // Note: tx_buffer_, rx_buffer_, and transfer_in_progress_ are in base class
};

} // namespace interfaces
} // namespace chipz

#endif // CHIPZ_INTERFACES_SPI_INTERFACE_HPP
