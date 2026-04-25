// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_COMMUNICATION_INTERFACE_HPP
#define CHIPZ_COMMUNICATION_INTERFACE_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace chipz {

/**
 * @brief Abstract base class for communication interfaces
 *
 * Non-template base that owns interrupt state and notify helpers.
 * Buffer storage lives in the concrete templated interface classes
 * (SPIInterface<N>, I2CInterface<N>, etc.) so the buffer size is a
 * compile-time constant and no heap allocation is required.
 *
 * Core and ChipBase hold CommunicationInterface* for type-erased routing.
 * Drivers access getTxBuffer()/getRxBuffer() through their typed reference
 * to the concrete interface — no virtual call needed on the hot path.
 */
class CommunicationInterface {
    public:
    enum class InterruptType {
        TransferComplete,
        RxComplete,
        Error,
        ArbitrationLost
    };

    virtual ~CommunicationInterface() = default;

    // -------------------------------------------------------------------------
    // Connection management
    // -------------------------------------------------------------------------

    using ConnectionId                               = uint8_t;
    static constexpr ConnectionId kInvalidConnection = 0xFF;

    virtual void selectConnection(ConnectionId id)
    {
        (void)id;
    }

    virtual bool transmit(const uint8_t* data, size_t length)          = 0;
    virtual bool transmit(const uint8_t* data, size_t length, uint32_t duration_us)
    {
        (void)duration_us;
        return transmit(data, length);
    }
    virtual bool receive(uint8_t* buffer, size_t length) = 0;

    virtual bool isReady() const
    {
        return !transfer_in_progress_;
    }

    // -------------------------------------------------------------------------
    // Interrupt contract — used by Core for routing
    // -------------------------------------------------------------------------

    bool hasInterruptPending() const
    {
        return interrupt_pending_;
    }

    InterruptType getPendingInterruptType() const
    {
        return interrupt_type_;
    }

    bool getInterruptSuccess() const
    {
        return interrupt_success_;
    }

    void clearInterrupt()
    {
        interrupt_pending_ = false;
    }

    static void registerCorePending(std::atomic<bool>* p)
    {
        s_core_pending_ = p;
    }

    // -------------------------------------------------------------------------
    // Interrupt notification — called from ISR context
    // -------------------------------------------------------------------------

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
     * @brief Signal an RX-complete interrupt (independent of TX).
     *
     * Virtual so that UARTInterface can override it to clear rx_in_progress_
     * before delegating here. ISRs call this through CommunicationInterface*
     * and get the correct derived behaviour via virtual dispatch.
     */
    virtual void notifyRxComplete()
    {
        interrupt_type_    = InterruptType::RxComplete;
        interrupt_success_ = true;
        interrupt_pending_ = true;
        if (s_core_pending_) {
            s_core_pending_->store(true, std::memory_order_release);
        }
    }

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

    ConnectionId nextId()
    {
        return next_id_++;
    }

    bool transfer_in_progress_;

    private:
    bool          interrupt_pending_;
    InterruptType interrupt_type_;
    bool          interrupt_success_;
    uint8_t       next_id_{0};

    inline static std::atomic<bool>* s_core_pending_ = nullptr;
};

}  // namespace chipz

#endif  // CHIPZ_COMMUNICATION_INTERFACE_HPP
