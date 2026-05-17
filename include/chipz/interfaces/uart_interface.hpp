// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_INTERFACES_UART_INTERFACE_HPP
#define CHIPZ_INTERFACES_UART_INTERFACE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "../core/communication_interface.hpp"

namespace chipz {
namespace interfaces {

/**
 * @brief UART Communication Interface implementation
 *
 * Wraps low-level async UART TX/RX operations (HAL IT or DMA mode).
 * N is the buffer size in bytes for both TX and RX paths.
 *
 * UART is full-duplex: TX and RX are fully independent. Both can be in-flight
 * simultaneously. The base class tracks TX state via transfer_in_progress_;
 * this class adds rx_in_progress_ for the independent RX path.
 *
 * Interrupt contract:
 *   HAL TX-complete callback → call notifyTransferComplete(true) on this instance
 *   HAL RX-complete callback → call notifyRxComplete() on this instance
 *   HAL error callback       → call notifyError() on this instance
 *
 * After notifyRxComplete() fires, the driver reads received data from getRxBuffer().
 */
template <size_t N>
class UARTInterface : public CommunicationInterface {
    public:
    using UARTTransmitFunction = std::function<int(const uint8_t* data, uint16_t size)>;
    using UARTReceiveFunction  = std::function<int(uint8_t* buffer, uint16_t size)>;

    UARTInterface(UARTTransmitFunction tx_func, UARTReceiveFunction rx_func) :
        CommunicationInterface(), uart_tx_(std::move(tx_func)), uart_rx_(std::move(rx_func)), rx_in_progress_(false)
    {
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
        if (transfer_in_progress_ || !uart_tx_) {
            return false;
        }

        if (data != tx_buffer_.data()) {
            for (size_t i = 0; i < length; ++i) {
                tx_buffer_[i] = data[i];
            }
        }

        transfer_in_progress_ = true;

        int result = uart_tx_(tx_buffer_.data(), static_cast<uint16_t>(length));
        if (result != 0) {
            transfer_in_progress_ = false;
            notifyError();
            return false;
        }

        return true;
    }

    bool receive(uint8_t* /*buffer*/, size_t length) override
    {
        if (rx_in_progress_ || !uart_rx_) {
            return false;
        }

        rx_in_progress_ = true;

        int result = uart_rx_(rx_buffer_.data(), static_cast<uint16_t>(length));
        if (result != 0) {
            rx_in_progress_ = false;
            notifyError();
            return false;
        }

        return true;
    }

    bool isReady() const override
    {
        return !transfer_in_progress_ && !rx_in_progress_;
    }

    bool isTxReady() const
    {
        return !transfer_in_progress_;
    }

    bool isRxReady() const
    {
        return !rx_in_progress_;
    }

    // -------------------------------------------------------------------------
    // ISR notification
    // -------------------------------------------------------------------------

    void notifyRxComplete() override
    {
        rx_in_progress_ = false;
        CommunicationInterface::notifyRxComplete();
    }

    private:
    UARTTransmitFunction   uart_tx_;
    UARTReceiveFunction    uart_rx_;
    bool                   rx_in_progress_;
    std::array<uint8_t, N> tx_buffer_{};
    std::array<uint8_t, N> rx_buffer_{};
};

}  // namespace interfaces
}  // namespace chipz

#endif  // CHIPZ_INTERFACES_UART_INTERFACE_HPP
