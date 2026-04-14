// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_INTERFACES_CAN_INTERFACE_HPP
#define CHIPZ_INTERFACES_CAN_INTERFACE_HPP

#include "../communication_interface.hpp"
#include <cstdint>
#include <cstddef>
#include <functional>

namespace chipz {
namespace interfaces {

/**
 * @brief CAN / CAN-FD frame with inline data storage
 *
 * Self-contained frame object that carries all information required for
 * transmission or produced by reception: CAN ID, protocol flags, DLC, and
 * the payload bytes.
 *
 * @tparam MaxPayload Maximum data bytes this frame can hold.
 *         Use 8 for classic CAN 2.0A/B, 64 for CAN-FD. Any value is accepted
 *         but DLC encoding above 8 bytes follows the CAN-FD non-linear table.
 *
 * Typical usage — static frame, reused each cycle:
 * @code
 *   static chipz::interfaces::CANFrame<8> tx_frame =
 *       chipz::interfaces::CANFrame<8>::classic(0x1A0, false, nullptr, 0);
 *
 *   // In the periodic handler:
 *   tx_frame.data()[0] = sensor_value;
 *   tx_frame.setLength(1);
 *   can.transmit(tx_frame);
 * @endcode
 *
 * Typical usage — dynamic frame, dispatched once:
 * @code
 *   auto frame = chipz::interfaces::CANFrame<64>::fd(0x18FF1234, true, true, buf, 32);
 *   can.transmit(frame);
 * @endcode
 *
 * Received frame — copy into a pre-allocated object:
 * @code
 *   void MyDevice::onRxComplete(CommunicationInterface& comm) {
 *       can.copyReceivedFrame(rx_storage_);   // copy into static member
 *       process(rx_storage_);
 *   }
 * @endcode
 */
template<size_t MaxPayload = 64>
class CANFrame {
public:
    CANFrame() = default;

    // -------------------------------------------------------------------------
    // Named constructors — explicit about frame type at creation time
    // -------------------------------------------------------------------------

    /**
     * @brief Create a classic CAN 2.0 frame (up to 8 data bytes)
     * @param id          11-bit (extended_id=false) or 29-bit (extended_id=true) CAN ID
     * @param extended_id true for 29-bit extended identifier
     * @param data        Payload bytes; may be nullptr if length is 0
     * @param length      Number of payload bytes (clamped to min(8, MaxPayload))
     */
    static CANFrame classic(uint32_t id, bool extended_id,
                            const uint8_t* data, uint8_t length) {
        CANFrame f;
        f.id_          = id;
        f.extended_id_ = extended_id;
        f.fd_format_   = false;
        f.brs_         = false;
        uint8_t clamped = length < 8u ? length : 8u;
        if (clamped > MaxPayload) clamped = static_cast<uint8_t>(MaxPayload);
        f.applyData(data, clamped);
        return f;
    }

    /**
     * @brief Create a CAN-FD frame (up to 64 data bytes)
     * @param id          CAN ID
     * @param extended_id true for 29-bit extended identifier
     * @param brs         true to enable bit rate switching
     * @param data        Payload bytes; may be nullptr if length is 0
     * @param length      Number of payload bytes (clamped to MaxPayload)
     */
    static CANFrame fd(uint32_t id, bool extended_id, bool brs,
                       const uint8_t* data, uint8_t length) {
        CANFrame f;
        f.id_          = id;
        f.extended_id_ = extended_id;
        f.fd_format_   = true;
        f.brs_         = brs;
        uint8_t clamped = length < MaxPayload ? length : static_cast<uint8_t>(MaxPayload);
        f.applyData(data, clamped);
        return f;
    }

    // -------------------------------------------------------------------------
    // Mutating setters — for reusing a static frame across cycles
    // -------------------------------------------------------------------------

    /**
     * @brief Set the CAN identifier
     * @param id          CAN ID value
     * @param extended_id true for 29-bit extended ID, false for 11-bit standard
     */
    void setId(uint32_t id, bool extended_id = false) {
        id_          = id;
        extended_id_ = extended_id;
    }

    /**
     * @brief Switch between classic CAN and CAN-FD mode
     * @param fd_format true = CAN-FD frame, false = classic CAN frame
     * @param brs       true = enable bit rate switching (ignored for classic CAN)
     */
    void setFdMode(bool fd_format, bool brs = false) {
        fd_format_ = fd_format;
        brs_       = brs;
    }

    /**
     * @brief Replace the entire payload and update DLC
     * @param src    Source bytes (may be nullptr when length is 0)
     * @param length Number of bytes to copy (clamped to MaxPayload)
     */
    void setData(const uint8_t* src, uint8_t length) {
        uint8_t clamped = length < MaxPayload ? length : static_cast<uint8_t>(MaxPayload);
        applyData(src, clamped);
    }

    /**
     * @brief Update DLC / data_length without copying new data
     *
     * Use after writing directly into data() to commit the new length.
     * @param length Number of valid bytes in data() (clamped to MaxPayload)
     */
    void setLength(uint8_t length) {
        uint8_t clamped = length < MaxPayload ? length : static_cast<uint8_t>(MaxPayload);
        data_length_    = clamped;
        dlc_            = lengthToDlc(clamped);
    }

    // -------------------------------------------------------------------------
    // Accessors
    // -------------------------------------------------------------------------

    uint32_t       id()           const { return id_; }
    bool           isExtendedId() const { return extended_id_; }
    bool           isFdFormat()   const { return fd_format_; }
    bool           isBrs()        const { return brs_; }
    uint8_t        dlc()          const { return dlc_; }
    uint8_t        dataLength()   const { return data_length_; }

    /** @brief Direct access to payload buffer for in-place writes */
    uint8_t*       data()       { return data_; }
    const uint8_t* data() const { return data_; }

    // -------------------------------------------------------------------------
    // DLC helpers — public so port lambdas and drivers can reuse them
    // -------------------------------------------------------------------------

    /**
     * @brief Decode DLC to actual byte count
     *
     * Classic CAN: DLC 0–8 maps 1:1.
     * CAN-FD: DLC 9→12, 10→16, 11→20, 12→24, 13→32, 14→48, 15→64.
     */
    static constexpr uint8_t dlcToLength(uint8_t d) {
        if (d <= 8) return d;
        switch (d) {
            case  9: return 12;
            case 10: return 16;
            case 11: return 20;
            case 12: return 24;
            case 13: return 32;
            case 14: return 48;
            case 15: return 64;
            default: return 64;
        }
    }

    /**
     * @brief Encode byte count to DLC (rounds up to next valid CAN-FD size)
     */
    static constexpr uint8_t lengthToDlc(uint8_t len) {
        if (len <=  8) return len;
        if (len <= 12) return 9;
        if (len <= 16) return 10;
        if (len <= 20) return 11;
        if (len <= 24) return 12;
        if (len <= 32) return 13;
        if (len <= 48) return 14;
        return 15;
    }

private:
    uint32_t id_          = 0;
    bool     extended_id_ = false;
    bool     fd_format_   = false;
    bool     brs_         = false;
    uint8_t  dlc_         = 0;
    uint8_t  data_length_ = 0;
    uint8_t  data_[MaxPayload]{};

    void applyData(const uint8_t* src, uint8_t length) {
        data_length_ = length;
        dlc_         = lengthToDlc(length);
        if (src) {
            for (uint8_t i = 0; i < length; ++i) data_[i] = src[i];
        }
    }
};

/**
 * @brief CAN / CAN-FD Communication Interface implementation
 *
 * Supports both classic CAN (8-byte payload) and CAN-FD (up to 64-byte payload).
 * The frame's isFdFormat() flag selects the wire format at transmit time; the
 * interface always allocates for the CAN-FD maximum internally.
 *
 * @tparam TxFifoDepth Hardware TX FIFO depth in frames. transmit() returns false when
 *                     this many frames are already pending in the hardware FIFO so the
 *                     caller never over-submits. Set this to match the value configured
 *                     in your HAL / CubeMX (FDCAN TX FIFO/Queue size).
 *
 * TX FIFO model
 * -------------
 * Unlike I2C / SPI / UART which allow one in-flight transfer at a time, CAN hardware
 * maintains its own TX FIFO that can hold TxFifoDepth frames simultaneously. Each
 * successful transmit() increments an internal counter; each notifyTxComplete() call
 * (one per frame acknowledged by the bus) decrements it. isReady() / isTxFifoFull()
 * expose the current occupancy.
 *
 * RX model
 * --------
 * RX is FIFO-driven by the hardware. When the HAL RX callback fires, chipz_isrs.cpp
 * calls notifyRxComplete(), which invokes the RxFunction to drain one frame from the
 * hardware FIFO into an internal CANFrame. The peripheral retrieves it via
 * getReceivedFrame() (zero-copy reference) or copyReceivedFrame() (deep copy into a
 * caller-owned frame) inside onRxComplete().
 */
template<size_t TxFifoDepth = 3>
class CANInterface : public CommunicationInterface {
public:
    using Frame = CANFrame<64>;

    /**
     * @brief Function type for async CAN/CAN-FD transmit
     *
     * Maps the chipz Frame to the platform HAL call (e.g. fills
     * FDCAN_TxHeaderTypeDef and calls HAL_FDCAN_AddMessageToTxFifoQ).
     * The HAL copies data to the hardware FIFO synchronously, so the Frame
     * reference does not need to remain valid after the function returns.
     *
     * @return 0 on success, non-zero HAL error code otherwise
     */
    using TxFunction = std::function<int(const Frame& frame)>;

    /**
     * @brief Function type that drains one frame from the HAL RX FIFO
     *
     * Called from notifyRxComplete() in ISR context. Must retrieve the pending
     * message from the hardware FIFO (e.g. HAL_FDCAN_GetRxMessage) and populate
     * the Frame (id, extended_id, fd_format, brs, dlc/length, data).
     *
     * @return 0 on success, non-zero HAL error code otherwise
     */
    using RxFunction = std::function<int(Frame& frame)>;

    /**
     * @brief Construct a CAN interface
     * @param tx_func Function to start a CAN transmit (adds frame to HW FIFO)
     * @param rx_func Function to drain one frame from the HW RX FIFO
     */
    CANInterface(TxFunction tx_func, RxFunction rx_func)
        : CommunicationInterface()
        , can_tx_(std::move(tx_func))
        , can_rx_(std::move(rx_func))
    {}

    // -------------------------------------------------------------------------
    // TX default header — used by transmit(data, len) for fixed-id periodic data
    // -------------------------------------------------------------------------

    /**
     * @brief Set the default CAN ID used by transmit(data, len)
     * @param id          CAN ID (0–0x7FF standard, 0–0x1FFFFFFF extended)
     * @param extended_id true for 29-bit extended identifier
     */
    void setDefaultId(uint32_t id, bool extended_id = false) {
        tx_defaults_.setId(id, extended_id);
    }

    /**
     * @brief Set the default CAN-FD flags used by transmit(data, len)
     * @param fd_format true for CAN-FD frame, false for classic CAN
     * @param brs       true to enable bit rate switching (CAN-FD only)
     */
    void setDefaultFdMode(bool fd_format, bool brs = false) {
        tx_defaults_.setFdMode(fd_format, brs);
    }

    // -------------------------------------------------------------------------
    // Primary TX API
    // -------------------------------------------------------------------------

    /**
     * @brief Transmit a fully-formed CANFrame
     *
     * The frame carries its own ID, format flags, DLC, and payload — no separate
     * header setup required. Returns false immediately if the hardware TX FIFO is
     * full (tx_pending_ == TxFifoDepth); the caller should retry or queue.
     *
     * Completion is signalled per-frame via notifyTxComplete() from the HAL TX
     * callback → InterruptType::TransferComplete → onTransferComplete().
     *
     * @return true if accepted into the hardware FIFO, false if FIFO full
     */
    bool transmit(const Frame& frame) {
        if (isTxFifoFull() || !can_tx_) {
            return false;
        }

        ++tx_pending_;
        int result = can_tx_(frame);
        if (result != 0) {
            --tx_pending_;
            notifyError();
            return false;
        }

        return true;
    }

    /**
     * @brief Transmit a fully-formed CANFrame (alias for transmit(frame))
     *
     * Provided for API symmetry with other interfaces that expose transmitFrame().
     */
    bool transmitFrame(const Frame& frame) {
        return transmit(frame);
    }

    /**
     * @brief Transmit raw data using the defaults set via setDefaultId() / setDefaultFdMode()
     *
     * Builds a frame on the stack from the stored defaults and the supplied
     * payload. Use for fixed-id periodic transmissions where building a full
     * Frame object each call would be repetitive.
     *
     * @param data   Payload bytes (clamped to MaxPayload)
     * @param length Number of bytes to send
     * @return true if accepted into the hardware FIFO, false if FIFO full
     */
    bool transmit(const uint8_t* data, size_t length) override {
        if (isTxFifoFull() || !can_tx_) {
            return false;
        }

        Frame frame = tx_defaults_;
        frame.setData(data, static_cast<uint8_t>(length < 64u ? length : 64u));

        ++tx_pending_;
        int result = can_tx_(frame);
        if (result != 0) {
            --tx_pending_;
            notifyError();
            return false;
        }

        return true;
    }

    /**
     * @brief Not applicable for CAN — always returns false
     *
     * CAN RX is FIFO-driven; the hardware delivers frames asynchronously.
     * Use getReceivedFrame() / copyReceivedFrame() inside onRxComplete() instead.
     */
    bool receive(uint8_t* buffer, size_t length) override {
        (void)buffer;
        (void)length;
        return false;
    }

    // -------------------------------------------------------------------------
    // RX
    // -------------------------------------------------------------------------

    /**
     * @brief Zero-copy access to the most recently received frame
     *
     * Valid after onRxComplete() fires. The reference remains stable until the
     * next notifyRxComplete() call (i.e. the next received frame overwrites it).
     * If the peripheral needs to hold the data longer, use copyReceivedFrame().
     */
    const Frame& getReceivedFrame() const { return rx_frame_; }

    /**
     * @brief Deep-copy the most recently received frame into a caller-owned object
     *
     * Use when the peripheral needs the frame to outlive the next RX interrupt,
     * e.g. when enqueuing into a ring buffer or passing to a task.
     *
     * @param dest Destination frame (must have MaxPayload >= this interface's MaxPayload)
     */
    void copyReceivedFrame(Frame& dest) const { dest = rx_frame_; }

    // -------------------------------------------------------------------------
    // TX FIFO state
    // -------------------------------------------------------------------------

    /**
     * @brief true when the hardware TX FIFO has at least one free slot
     */
    bool isReady() const override { return !isTxFifoFull(); }

    /** @brief true when all TxFifoDepth slots are occupied */
    bool isTxFifoFull()  const { return tx_pending_ >= TxFifoDepth; }

    /** @brief true when no frames are pending in the hardware FIFO */
    bool isTxFifoEmpty() const { return tx_pending_ == 0; }

    /** @brief Number of free TX FIFO slots (0 when full) */
    size_t txFifoFreeSlots() const {
        return tx_pending_ < TxFifoDepth ? TxFifoDepth - tx_pending_ : 0u;
    }

    // -------------------------------------------------------------------------
    // ISR notification — call from HAL CAN/FDCAN callbacks
    // -------------------------------------------------------------------------

    /**
     * @brief Signal one TX frame complete from the HAL TX callback
     *
     * Decrements the pending counter and delivers InterruptType::TransferComplete
     * to Core. Called once per frame that was successfully placed on the bus.
     */
    void notifyTxComplete() {
        if (tx_pending_ > 0) --tx_pending_;
        notifyTransferComplete(true);
    }

    /**
     * @brief Drain one frame from the HAL RX FIFO and signal Core
     *
     * Calls the RxFunction to populate rx_frame_, then delivers
     * InterruptType::RxComplete to Core. Call from the HAL RX FIFO callback.
     */
    void notifyRxComplete() {
        if (can_rx_) {
            can_rx_(rx_frame_);
        }
        CommunicationInterface::notifyRxComplete();
    }

private:
    TxFunction can_tx_;
    RxFunction can_rx_;
    Frame      tx_defaults_{};   ///< Header defaults used by transmit(data, len)
    Frame      rx_frame_{};      ///< Populated by RxFunction on each received frame
    size_t     tx_pending_{0};   ///< Frames currently occupying the hardware TX FIFO

    // Note: transfer_in_progress_ from base class is unused for CAN — isReady() and
    // transmit() use tx_pending_ / TxFifoDepth instead. notifyTxComplete() calls
    // notifyTransferComplete() which resets it (benign).
};

} // namespace interfaces
} // namespace chipz

#endif // CHIPZ_INTERFACES_CAN_INTERFACE_HPP
