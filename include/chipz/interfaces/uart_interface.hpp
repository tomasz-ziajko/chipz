// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_INTERFACES_UART_INTERFACE_HPP
#define CHIPZ_INTERFACES_UART_INTERFACE_HPP

#include "../core/communication_interface.hpp"
#include <cstdint>
#include <cstddef>
#include <functional>

namespace chipz {
namespace interfaces {

/**
 * @brief UART Communication Interface implementation
 *
 * Wraps low-level async UART TX/RX operations (typically HAL IT or DMA mode)
 * and provides the CommunicationInterface abstraction.
 *
 * UART is full-duplex: TX and RX are fully independent. Both can be in-flight
 * simultaneously. The base class tracks TX state via transfer_in_progress_;
 * this class adds rx_in_progress_ for the independent RX path.
 *
 * Interrupt contract:
 *   - HAL TX-complete callback  → call notifyTxComplete() on this instance
 *   - HAL RX-complete callback  → call notifyRxComplete() on this instance
 *   - HAL error callback        → call notifyError() on this instance
 *
 * Typical receive flow:
 *   1. Peripheral calls receive(comm.getRxBuffer(), length) to arm the HAL.
 *   2. HAL fills rx_buffer_ and fires the ISR.
 *   3. notifyRxComplete() clears rx_in_progress_, signals Core.
 *   4. Core routes InterruptType::RxComplete → peripheral.onInterrupt().
 *   5. Peripheral reads comm.getRxBuffer(), processes the frame, re-arms.
 *
 * Note: UART is point-to-point; there is no registerConnection() or
 * selectConnection() — one UARTInterface represents one UART peripheral.
 */
class UARTInterface : public CommunicationInterface {
public:
    /**
     * @brief Function type for async UART transmit
     * @param data Pointer to data to transmit
     * @param size Number of bytes to transmit
     * @return 0 on success, non-zero error code otherwise
     */
    using UARTTransmitFunction = std::function<int(const uint8_t* data, uint16_t size)>;

    /**
     * @brief Function type for async UART receive (arms the HAL receiver)
     * @param buffer Pointer to receive buffer (must remain valid until ISR fires)
     * @param size Number of bytes to receive
     * @return 0 on success, non-zero error code otherwise
     */
    using UARTReceiveFunction = std::function<int(uint8_t* buffer, uint16_t size)>;

    /**
     * @brief Construct a UART interface
     * @param tx_func Function to start an async UART transmit
     * @param rx_func Function to arm the UART receiver
     */
    UARTInterface(UARTTransmitFunction tx_func, UARTReceiveFunction rx_func)
        : CommunicationInterface()
        , uart_tx_(std::move(tx_func))
        , uart_rx_(std::move(rx_func))
        , rx_in_progress_(false)
        , rx_armed_buffer_(nullptr)
        , rx_armed_length_(0)
    {}

    /**
     * @brief Start an async UART transmit
     *
     * Data is copied into the internal TX buffer before the HAL call so the
     * caller may reuse its buffer immediately. Completion is signalled via
     * notifyTxComplete() from the HAL callback, which wakes Core and delivers
     * InterruptType::TransferComplete to the active peripheral.
     *
     * @param data   Data to transmit
     * @param length Number of bytes to transmit
     * @return true if the transmit was started, false if TX is busy or no
     *         transmit function was provided
     */
    bool transmit(const uint8_t* data, size_t length) override {
        if (transfer_in_progress_ || !uart_tx_) {
            return false;
        }

        ensureBufferSize(tx_buffer_, length);
        for (size_t i = 0; i < length; ++i) {
            tx_buffer_[i] = data[i];
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

    /**
     * @brief Arm the UART receiver for an async receive
     *
     * The HAL writes directly into the internal RX buffer. When @p buffer
     * differs from getRxBuffer() the data is copied in notifyRxComplete();
     * passing getRxBuffer() as @p buffer avoids the copy entirely.
     *
     * Completion is signalled via notifyRxComplete() from the HAL callback,
     * which wakes Core and delivers InterruptType::RxComplete to the active
     * peripheral. TX is unaffected — a simultaneous transmit() is allowed.
     *
     * @param buffer Destination for received data (or getRxBuffer() to read
     *               in-place after the interrupt)
     * @param length Number of bytes to receive
     * @return true if the receiver was armed, false if RX is already in
     *         progress or no receive function was provided
     */
    bool receive(uint8_t* buffer, size_t length) override {
        if (rx_in_progress_ || !uart_rx_) {
            return false;
        }

        ensureBufferSize(rx_buffer_, length);
        rx_armed_buffer_ = buffer;
        rx_armed_length_ = length;
        rx_in_progress_  = true;

        int result = uart_rx_(rx_buffer_.data(), static_cast<uint16_t>(length));
        if (result != 0) {
            rx_in_progress_  = false;
            rx_armed_buffer_ = nullptr;
            notifyError();
            return false;
        }

        return true;
    }

    /**
     * @brief Check if both TX and RX paths are idle
     * @return true only when neither a transmit nor a receive is in progress
     */
    bool isReady() const override {
        return !transfer_in_progress_ && !rx_in_progress_;
    }

    /**
     * @brief Check if the TX path is idle and ready for a new transmit
     */
    bool isTxReady() const { return !transfer_in_progress_; }

    /**
     * @brief Check if the RX path is idle and ready to be armed
     */
    bool isRxReady() const { return !rx_in_progress_; }

    // -------------------------------------------------------------------------
    // ISR notification — call these from HAL UART callbacks
    // -------------------------------------------------------------------------

    /**
     * @brief Signal TX complete from the HAL UART TX-complete callback
     *
     * Clears transfer_in_progress_ and delivers InterruptType::TransferComplete
     * to Core. Safe to call from ISR context.
     */
    void notifyTxComplete() {
        notifyTransferComplete(true);
    }

    /**
     * @brief Signal RX complete from the HAL UART RX-complete callback
     *
     * Clears rx_in_progress_, copies data to the caller-provided buffer if it
     * differs from the internal rx_buffer_, and delivers
     * InterruptType::RxComplete to Core. Safe to call from ISR context.
     */
    void notifyRxComplete() {
        rx_in_progress_ = false;

        // Copy to caller-provided buffer when it differs from the internal one
        if (rx_armed_buffer_ && rx_armed_buffer_ != rx_buffer_.data()) {
            for (size_t i = 0; i < rx_armed_length_; ++i) {
                rx_armed_buffer_[i] = rx_buffer_[i];
            }
        }
        rx_armed_buffer_ = nullptr;

        // Delegate interrupt signalling to base (sets RxComplete, wakes Core)
        CommunicationInterface::notifyRxComplete();
    }

private:
    UARTTransmitFunction uart_tx_;
    UARTReceiveFunction  uart_rx_;

    bool     rx_in_progress_;
    uint8_t* rx_armed_buffer_;  ///< Caller-provided buffer registered in receive()
    size_t   rx_armed_length_;  ///< Byte count registered in receive()

    // Note: tx_buffer_, rx_buffer_, and transfer_in_progress_ are in base class
};

} // namespace interfaces
} // namespace chipz

#endif // CHIPZ_INTERFACES_UART_INTERFACE_HPP
