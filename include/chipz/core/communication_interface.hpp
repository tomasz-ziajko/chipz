// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_COMMUNICATION_INTERFACE_HPP
#define CHIPZ_COMMUNICATION_INTERFACE_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chipz {

/**
 * @brief Abstract base class for communication interfaces
 *
 * Provides a unified interface for different communication protocols (I2C,
 * SPI, GPIO, etc.) Used by peripherals to communicate with hardware
 * devices.
 *
 * Interrupt contract:
 * When a hardware interrupt fires (transfer complete, bus error, etc.), the
 * concrete implementation calls one of the notify*() helpers. This sets the
 * pending interrupt state and invokes the pending callback (injected by
 * Core) to wake service(). Core then reads the pending type via
 * hasInterruptPending() / getPendingInterruptType() and routes to the
 * correct peripheral handler via onInterrupt().
 *
 * Buffer Management Strategy:
 * - Buffers are allocated on first request
 * - Buffers are kept allocated between requests (not deallocated)
 * - If a request needs more space, buffer is reallocated to larger size
 */
class CommunicationInterface {
    public:
    /**
     * @brief Types of hardware interrupts a communication interface can
     * generate
     *
     * Concrete implementations signal one of these types via the notify*()
     * helpers. New interface-specific types can be added here as the
     * library grows.
     */
    enum class InterruptType {
        TransferComplete,  ///< TX complete (SPI, I2C, UART...)
        RxComplete,        ///< RX complete, independent of TX (full-duplex UART)
        Error,             ///< Bus or protocol error (I2C NACK, SPI fault...)
        ArbitrationLost    ///< I2C arbitration lost
    };

    virtual ~CommunicationInterface() = default;

    // -------------------------------------------------------------------------
    // Connection management
    // -------------------------------------------------------------------------

    /**
     * @brief Opaque handle identifying a registered device on this bus
     *
     * Allocated by nextId() in the base class. Concrete interfaces store
     * per-connection configuration (device address, chip-select function,
     * etc.) in a vector indexed by this value.
     */
    using ConnectionId                               = uint8_t;
    static constexpr ConnectionId kInvalidConnection = 0xFF;

    /**
     * @brief Switch the interface to the configuration registered for @p id
     *
     * Called automatically by Peripheral<CommInterface>::transmit() and
     * receive() before every transfer — drivers never call this directly.
     *
     * @param id ConnectionId returned by a previous registerConnection()
     * call
     */
    virtual void selectConnection(ConnectionId id)
    {
        // TODO: handle invalid / unregistered id
        (void)id;
    }

    /**
     * @brief Transmit data through the communication interface
     * @param data Pointer to data to transmit
     * @param length Number of bytes to transmit
     * @return true if transmission started successfully, false otherwise
     */
    virtual bool transmit(const uint8_t* data, size_t length) = 0;

    /**
     * @brief Async transmit with a duration hint for completion sources
     *
     * The default implementation ignores duration_us and delegates to the
     * immediate transmit(). Interfaces that support deferred completion
     * (e.g. ParallelInterface<N>) override this to arm their
     * CompletionSources.
     *
     * @param data       Data to transmit
     * @param length     Number of bytes to transmit
     * @param duration_us Duration hint forwarded to TimerCompletionSource;
     *                    ignored by other sources and by the default
     * implementation
     */
    virtual bool transmit(const uint8_t* data, size_t length, uint32_t duration_us)
    {
        (void)duration_us;
        return transmit(data, length);
    }

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
    virtual bool isReady() const
    {
        return !transfer_in_progress_;
    }

    // -------------------------------------------------------------------------
    // Interrupt contract — used by Core for routing
    // -------------------------------------------------------------------------

    /**
     * @brief Check if a hardware interrupt is pending service
     * @return true if an interrupt has fired and not yet been cleared
     */
    bool hasInterruptPending() const
    {
        return interrupt_pending_;
    }

    /**
     * @brief Get the type of the pending interrupt
     * @return Interrupt type (valid only when hasInterruptPending() is
     * true)
     */
    InterruptType getPendingInterruptType() const
    {
        return interrupt_type_;
    }

    /**
     * @brief Get the success status of the pending interrupt
     * @return true if the operation succeeded (meaningful for
     * TransferComplete)
     */
    bool getInterruptSuccess() const
    {
        return interrupt_success_;
    }

    /**
     * @brief Clear the pending interrupt flag after Core has routed it
     */
    void clearInterrupt()
    {
        interrupt_pending_ = false;
    }

    /**
     * @brief Register the Core's pending flag for ISR wake-up
     *
     * Called once by Core's constructor. All CommunicationInterface
     * instances share a single static pointer to Core's pending_ flag. When
     * any notify*() method fires it sets the flag directly — no
     * std::function, no per-interface registration, no runtime overhead.
     *
     * Only one Core instance is supported (embedded systems always have one
     * scheduler). A second Core construction overwrites the pointer.
     *
     * @param p Pointer to Core's pending_ atomic flag
     */
    static void registerCorePending(std::atomic<bool>* p)
    {
        s_core_pending_ = p;
    }

    // -------------------------------------------------------------------------
    // Buffer access
    // -------------------------------------------------------------------------

    /**
     * @brief Get pointer to transmit buffer
     * @return Pointer to internal transmit buffer
     */
    uint8_t* getTxBuffer()
    {
        return tx_buffer_.data();
    }

    /**
     * @brief Get pointer to receive buffer
     * @return Pointer to internal receive buffer
     */
    uint8_t* getRxBuffer()
    {
        return rx_buffer_.data();
    }

    /**
     * @brief Get size of internal buffers
     * @return Buffer size in bytes
     */
    size_t getBufferSize() const
    {
        return tx_buffer_.size();
    }

    // -------------------------------------------------------------------------
    // Interrupt notification — called from ISR context (or HAL callbacks)
    // -------------------------------------------------------------------------

    /**
     * @brief Signal a successful or failed transfer completion
     *
     * Called from HAL transfer-complete callbacks defined in the port's
     * chipz_isrs.cpp (weak symbol overrides). Sets interrupt state and
     * wakes Core by writing the registered pending flag.
     *
     * Safe to call from ISR context — only atomic/bool writes, no
     * allocation.
     *
     * @param success true if transfer succeeded, false on error
     */
    void notifyTransferComplete(bool success)
    {
        transfer_in_progress_ = false;
        interrupt_type_       = InterruptType::TransferComplete;
        interrupt_success_    = success;
        interrupt_pending_    = true;
        if (s_core_pending_) {
            s_core_pending_->store(true, std::memory_order_release);
        }
    }

    /**
     * @brief Signal a bus or protocol error interrupt
     */
    void notifyError()
    {
        transfer_in_progress_ = false;
        interrupt_type_       = InterruptType::Error;
        interrupt_success_    = false;
        interrupt_pending_    = true;
        if (s_core_pending_) {
            s_core_pending_->store(true, std::memory_order_release);
        }
    }

    /**
     * @brief Signal an RX-complete interrupt (independent of TX)
     *
     * Called from HAL UART RX-complete callbacks. Does NOT clear
     * transfer_in_progress_ (that tracks TX). Concrete implementations that
     * track a separate rx_in_progress_ flag (e.g. UARTInterface) should
     * shadow this method to clear it before calling this base version.
     *
     * Safe to call from ISR context.
     */
    void notifyRxComplete()
    {
        interrupt_type_    = InterruptType::RxComplete;
        interrupt_success_ = true;
        interrupt_pending_ = true;
        if (s_core_pending_) {
            s_core_pending_->store(true, std::memory_order_release);
        }
    }

    /**
     * @brief Signal an I2C arbitration-lost interrupt
     */
    void notifyArbitrationLost()
    {
        transfer_in_progress_ = false;
        interrupt_type_       = InterruptType::ArbitrationLost;
        interrupt_success_    = false;
        interrupt_pending_    = true;
        if (s_core_pending_) {
            s_core_pending_->store(true, std::memory_order_release);
        }
    }

    protected:
    CommunicationInterface() :
        transfer_in_progress_(false),
        interrupt_pending_(false),
        interrupt_type_(InterruptType::TransferComplete),
        interrupt_success_(false)
    {
    }

    CommunicationInterface(const CommunicationInterface&)            = delete;
    CommunicationInterface& operator=(const CommunicationInterface&) = delete;
    CommunicationInterface(CommunicationInterface&&)                 = default;
    CommunicationInterface& operator=(CommunicationInterface&&)      = default;

    std::vector<uint8_t> tx_buffer_;
    std::vector<uint8_t> rx_buffer_;
    bool                 transfer_in_progress_;

    bool          interrupt_pending_;
    InterruptType interrupt_type_;
    bool          interrupt_success_;

    /**
     * @brief Ensure buffer has at least the requested size
     */
    void ensureBufferSize(std::vector<uint8_t>& buffer, size_t required_size)
    {
        if (buffer.size() < required_size) {
            buffer.resize(required_size);
        }
    }

    /**
     * @brief Allocate the next available ConnectionId
     *
     * Called by derived-class registerConnection() implementations to
     * obtain a unique ID before storing per-device configuration in their
     * own vector.
     */
    ConnectionId nextId()
    {
        return next_id_++;
    }

    private:
    uint8_t                          next_id_{0};
    inline static std::atomic<bool>* s_core_pending_ = nullptr;
};

}  // namespace chipz

#endif  // CHIPZ_COMMUNICATION_INTERFACE_HPP
