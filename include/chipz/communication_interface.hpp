#ifndef CHIPZ_COMMUNICATION_INTERFACE_HPP
#define CHIPZ_COMMUNICATION_INTERFACE_HPP

#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>

namespace chipz {

/**
 * @brief Abstract base class for communication interfaces
 *
 * Provides a unified interface for different communication protocols (I2C, SPI, GPIO, etc.)
 * Used by peripherals to communicate with hardware devices.
 * Supports asynchronous operations with callback notifications.
 *
 * Buffer Management Strategy:
 * - Buffers are allocated on first request
 * - Buffers are kept allocated between requests (not deallocated)
 * - If a request needs more space, buffer is reallocated to larger size
 * - This minimizes allocations while maintaining flexibility
 */
class CommunicationInterface {
public:
    /**
     * @brief Callback function type for transfer completion
     * Called when a transfer (transmit or receive) completes
     * @param success True if transfer was successful, false on error
     */
    using TransferCompleteCallback = std::function<void(bool success)>;

    virtual ~CommunicationInterface() = default;

    /**
     * @brief Transmit data through the communication interface
     * @param data Pointer to data to transmit
     * @param length Number of bytes to transmit
     * @return true if transmission started successfully, false otherwise
     */
    virtual bool transmit(const uint8_t* data, size_t length) = 0;

    /**
     * @brief Receive data through the communication interface
     * @param buffer Pointer to buffer where received data will be stored
     * @param length Number of bytes to receive
     * @return true if reception started successfully, false otherwise
     */
    virtual bool receive(uint8_t* buffer, size_t length) = 0;

    /**
     * @brief Check if the interface is ready for a new operation
     * @return true if ready (no transfer in progress), false if busy
     */
    virtual bool isReady() const {
        return !transfer_in_progress_;
    }

    /**
     * @brief Set callback function for transfer completion notification
     * @param callback Function to call when transfer completes
     */
    void setTransferCompleteCallback(TransferCompleteCallback callback) {
        transfer_complete_callback_ = callback;
    }

    /**
     * @brief Get pointer to transmit buffer
     * @return Pointer to internal transmit buffer
     *
     * Note: Ensures minimum buffer size to prevent returning invalid pointers
     */
    uint8_t* getTxBuffer() {
        // Ensure buffer has minimum size allocated (prevents nullptr from empty vector)
        if (tx_buffer_.empty()) {
            tx_buffer_.resize(32);  // Reasonable default size
        }
        return tx_buffer_.data();
    }

    /**
     * @brief Get pointer to receive buffer
     * @return Pointer to internal receive buffer
     *
     * Note: Ensures minimum buffer size to prevent returning invalid pointers
     */
    uint8_t* getRxBuffer() {
        // Ensure buffer has minimum size allocated (prevents nullptr from empty vector)
        if (rx_buffer_.empty()) {
            rx_buffer_.resize(32);  // Reasonable default size
        }
        return rx_buffer_.data();
    }

    /**
     * @brief Get size of internal buffers
     * @return Buffer size in bytes (returns TX buffer size, RX is same)
     */
    size_t getBufferSize() const {
        return tx_buffer_.size();
    }

protected:
    CommunicationInterface() : transfer_in_progress_(false) {}

    // Prevent copying
    CommunicationInterface(const CommunicationInterface&) = delete;
    CommunicationInterface& operator=(const CommunicationInterface&) = delete;

    // Allow moving
    CommunicationInterface(CommunicationInterface&&) = default;
    CommunicationInterface& operator=(CommunicationInterface&&) = default;

    /**
     * @brief Transmit buffer (grows as needed, never shrinks)
     */
    std::vector<uint8_t> tx_buffer_;

    /**
     * @brief Receive buffer (grows as needed, never shrinks)
     */
    std::vector<uint8_t> rx_buffer_;

    /**
     * @brief Flag indicating if a transfer is currently in progress
     */
    bool transfer_in_progress_;

    /**
     * @brief Callback function for transfer completion
     * Should be called by derived classes when a transfer completes
     */
    TransferCompleteCallback transfer_complete_callback_;

    /**
     * @brief Notify that transfer has completed
     * Helper method for derived classes to invoke the callback
     * @param success True if transfer succeeded, false on error
     */
    void notifyTransferComplete(bool success) {
        if (transfer_complete_callback_) {
            transfer_complete_callback_(success);
        }
    }

    /**
     * @brief Ensure buffer has at least the requested size
     * Allocates on first call, reallocates only if more space needed
     * @param buffer Reference to buffer vector
     * @param required_size Minimum required size in bytes
     */
    void ensureBufferSize(std::vector<uint8_t>& buffer, size_t required_size) {
        if (buffer.size() < required_size) {
            buffer.resize(required_size);
        }
    }
};

} // namespace chipz

#endif // CHIPZ_COMMUNICATION_INTERFACE_HPP
