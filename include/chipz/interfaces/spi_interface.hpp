// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_INTERFACES_SPI_INTERFACE_HPP
#define CHIPZ_INTERFACES_SPI_INTERFACE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "../core/communication_interface.hpp"

namespace chipz {
namespace interfaces {

/**
 * @brief SPI Communication Interface implementation
 *
 * Wraps low-level SPI transfer operations (typically C HAL functions)
 * and provides the CommunicationInterface abstraction.
 *
 * N is the transfer buffer size in bytes — set it to the kMaxTransfer of the
 * largest device on the bus (or std::max(Dev1::kMaxTransfer, Dev2::kMaxTransfer)
 * for shared buses). Buffers live in the object with no heap allocation.
 */
template <size_t N>
class SPIInterface : public CommunicationInterface {
    public:
    /**
     * @brief Function pointer type for SPI transfer operation
     * SPI is full-duplex: transmits and receives simultaneously
     */
    using SPITransferFunction = std::function<int(uint8_t* tx_buffer, uint8_t* rx_buffer, uint16_t size)>;

    /**
     * @brief Function pointer type for chip select control
     */
    using ChipSelectFunction = std::function<void(bool select)>;

    explicit SPIInterface(SPITransferFunction transfer_func) : CommunicationInterface(), spi_transfer_(transfer_func)
    {
    }

    /**
     * @brief Register a device on this SPI bus
     *
     * @param cs_func Function to assert/deassert this device's chip-select pin
     * @return ConnectionId to pass to Chip::setConnection()
     */
    ConnectionId registerConnection(ChipSelectFunction cs_func)
    {
        ConnectionId id = nextId();
        connections_.push_back(std::move(cs_func));
        return id;
    }

    void selectConnection(ConnectionId id) override
    {
        active_cs_ = connections_[id];
    }

    uint8_t* getTxBuffer() override
    {
        return tx_buffer_.data();
    }
    uint8_t* getRxBuffer() override
    {
        return rx_buffer_.data();
    }

    bool transmit(const uint8_t* data, size_t length) override
    {
        if (transfer_in_progress_ || !spi_transfer_) {
            return false;
        }

        if (data != tx_buffer_.data()) {
            for (size_t i = 0; i < length; ++i) {
                tx_buffer_[i] = data[i];
            }
        }

        transfer_in_progress_ = true;

        if (active_cs_) {
            active_cs_(true);
        }

        int result = spi_transfer_(tx_buffer_.data(), rx_buffer_.data(), static_cast<uint16_t>(length));

        if (result != 0) {
            if (active_cs_) {
                active_cs_(false);
            }
            notifyError();
            return false;
        }

        return true;
    }

    bool receive(uint8_t* /*buffer*/, size_t length) override
    {
        if (transfer_in_progress_ || !spi_transfer_) {
            return false;
        }

        for (size_t i = 0; i < length; ++i) {
            tx_buffer_[i] = 0x00;
        }

        transfer_in_progress_ = true;

        if (active_cs_) {
            active_cs_(true);
        }

        int result = spi_transfer_(tx_buffer_.data(), rx_buffer_.data(), static_cast<uint16_t>(length));

        if (result != 0) {
            if (active_cs_) {
                active_cs_(false);
            }
            notifyError();
            return false;
        }

        return true;
    }

    bool transfer(const uint8_t* tx_data, uint8_t* rx_data, size_t length)
    {
        if (transfer_in_progress_ || !spi_transfer_) {
            return false;
        }

        if (tx_data != tx_buffer_.data()) {
            for (size_t i = 0; i < length; ++i) {
                tx_buffer_[i] = tx_data[i];
            }
        }

        transfer_in_progress_ = true;

        if (active_cs_) {
            active_cs_(true);
        }

        int result = spi_transfer_(tx_buffer_.data(), rx_buffer_.data(), static_cast<uint16_t>(length));

        if (result != 0) {
            if (active_cs_) {
                active_cs_(false);
            }
            notifyError();
            return false;
        }

        if (rx_data && rx_data != rx_buffer_.data()) {
            for (size_t i = 0; i < length; ++i) {
                rx_data[i] = rx_buffer_[i];
            }
        }

        return true;
    }

    private:
    SPITransferFunction             spi_transfer_;
    ChipSelectFunction              active_cs_;
    std::vector<ChipSelectFunction> connections_;
    std::array<uint8_t, N>          tx_buffer_{};
    std::array<uint8_t, N>          rx_buffer_{};
};

}  // namespace interfaces
}  // namespace chipz

#endif  // CHIPZ_INTERFACES_SPI_INTERFACE_HPP
