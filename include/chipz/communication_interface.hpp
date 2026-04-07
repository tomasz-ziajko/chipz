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
 *
 * Interrupt contract:
 * When a hardware interrupt fires (transfer complete, bus error, etc.), the concrete
 * implementation calls one of the notify*() helpers. This sets the pending interrupt
 * state and invokes the pending callback (injected by Core) to wake service().
 * Core then reads the pending type via hasInterruptPending() / getPendingInterruptType()
 * and routes to the correct peripheral handler via onInterrupt().
 *
 * Buffer Management Strategy:
 * - Buffers are allocated on first request
 * - Buffers are kept allocated between requests (not deallocated)
 * - If a request needs more space, buffer is reallocated to larger size
 */
class CommunicationInterface {
public:
    /**
     * @brief Types of hardware interrupts a communication interface can generate
     *
     * Concrete implementations signal one of these types via the notify*() helpers.
     * New interface-specific types can be added here as the library grows.
     */
    enum class InterruptType {
        TransferComplete,  ///< Successful data transfer (SPI, I2C, UART...)
        Error,             ///< Bus or protocol error (I2C NACK, SPI fault...)
        ArbitrationLost    ///< I2C arbitration lost
    };

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

    // -------------------------------------------------------------------------
    // Interrupt contract — used by Core for routing
    // -------------------------------------------------------------------------

    /**
     * @brief Check if a hardware interrupt is pending service
     * @return true if an interrupt has fired and not yet been cleared
     */
    bool hasInterruptPending() const {
        return interrupt_pending_;
    }

    /**
     * @brief Get the type of the pending interrupt
     * @return Interrupt type (valid only when hasInterruptPending() is true)
     */
    InterruptType getPendingInterruptType() const {
        return interrupt_type_;
    }

    /**
     * @brief Get the success status of the pending interrupt
     * @return true if the operation succeeded (meaningful for TransferComplete)
     */
    bool getInterruptSuccess() const {
        return interrupt_success_;
    }

    /**
     * @brief Clear the pending interrupt flag after Core has routed it
     */
    void clearInterrupt() {
        interrupt_pending_ = false;
    }

    /**
     * @brief Inject Core's pending callback
     *
     * Called by Core::add(). When an interrupt fires, this callback is invoked
     * to set Core's pending_ flag and wake service(). Must be minimal — no
     * driver logic, just a flag write.
     *
     * @param cb Callback to invoke when an interrupt fires
     */
    void setPendingCallback(std::function<void()> cb) {
        on_pending_callback_ = std::move(cb);
    }

    // -------------------------------------------------------------------------
    // Buffer access
    // -------------------------------------------------------------------------

    /**
     * @brief Get pointer to transmit buffer
     * @return Pointer to internal transmit buffer
     */
    uint8_t* getTxBuffer() {
        return tx_buffer_.data();
    }

    /**
     * @brief Get pointer to receive buffer
     * @return Pointer to internal receive buffer
     */
    uint8_t* getRxBuffer() {
        return rx_buffer_.data();
    }

    /**
     * @brief Get size of internal buffers
     * @return Buffer size in bytes
     */
    size_t getBufferSize() const {
        return tx_buffer_.size();
    }

protected:
    CommunicationInterface()
        : transfer_in_progress_(false)
        , interrupt_pending_(false)
        , interrupt_type_(InterruptType::TransferComplete)
        , interrupt_success_(false)
    {}

    CommunicationInterface(const CommunicationInterface&) = delete;
    CommunicationInterface& operator=(const CommunicationInterface&) = delete;
    CommunicationInterface(CommunicationInterface&&) = default;
    CommunicationInterface& operator=(CommunicationInterface&&) = default;

    std::vector<uint8_t> tx_buffer_;
    std::vector<uint8_t> rx_buffer_;
    bool transfer_in_progress_;

    // -------------------------------------------------------------------------
    // Interrupt state — set by notify*() helpers, read by Core
    // -------------------------------------------------------------------------

    bool interrupt_pending_;
    InterruptType interrupt_type_;
    bool interrupt_success_;
    std::function<void()> on_pending_callback_;

    /**
     * @brief Signal a successful or failed transfer completion
     *
     * Call from the ISR (or synchronous completion) after a transmit/receive.
     * Sets interrupt state and wakes Core via the pending callback.
     *
     * Note: In real async hardware implementations ensure proper memory ordering
     * between the ISR write and the main-loop read of interrupt_pending_.
     *
     * @param success true if transfer succeeded, false on error
     */
    void notifyTransferComplete(bool success) {
        interrupt_type_    = InterruptType::TransferComplete;
        interrupt_success_ = success;
        interrupt_pending_ = true;
        if (on_pending_callback_) {
            on_pending_callback_();
        }
    }

    /**
     * @brief Signal a bus or protocol error interrupt
     */
    void notifyError() {
        interrupt_type_    = InterruptType::Error;
        interrupt_success_ = false;
        interrupt_pending_ = true;
        if (on_pending_callback_) {
            on_pending_callback_();
        }
    }

    /**
     * @brief Signal an I2C arbitration-lost interrupt
     */
    void notifyArbitrationLost() {
        interrupt_type_    = InterruptType::ArbitrationLost;
        interrupt_success_ = false;
        interrupt_pending_ = true;
        if (on_pending_callback_) {
            on_pending_callback_();
        }
    }

    /**
     * @brief Ensure buffer has at least the requested size
     */
    void ensureBufferSize(std::vector<uint8_t>& buffer, size_t required_size) {
        if (buffer.size() < required_size) {
            buffer.resize(required_size);
        }
    }
};

} // namespace chipz

#endif // CHIPZ_COMMUNICATION_INTERFACE_HPP
