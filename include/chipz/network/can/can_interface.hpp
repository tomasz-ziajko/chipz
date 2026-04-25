// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_NETWORK_CAN_INTERFACE_HPP
#define CHIPZ_NETWORK_CAN_INTERFACE_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <tuple>

namespace chipz {
namespace network {

// -------------------------------------------------------------------------
// DLC helpers — free functions, used by CANMessage and port ISR code
// -------------------------------------------------------------------------

constexpr uint8_t dlcToLength(uint8_t d)
{
    if (d <= 8) {
        return d;
    }
    switch (d) {
        case 9:
            return 12;
        case 10:
            return 16;
        case 11:
            return 20;
        case 12:
            return 24;
        case 13:
            return 32;
        case 14:
            return 48;
        case 15:
            return 64;
        default:
            return 64;
    }
}

constexpr uint8_t lengthToDlc(uint8_t len)
{
    if (len <= 8) {
        return len;
    }
    if (len <= 12) {
        return 9;
    }
    if (len <= 16) {
        return 10;
    }
    if (len <= 20) {
        return 11;
    }
    if (len <= 24) {
        return 12;
    }
    if (len <= 32) {
        return 13;
    }
    if (len <= 48) {
        return 14;
    }
    return 15;
}

// -------------------------------------------------------------------------
// CANMessage — typed CAN / CAN-FD message with compile-time ID and length
// -------------------------------------------------------------------------

/**
 * @brief Typed CAN / CAN-FD message with compile-time ID and payload length
 *
 * Each distinct message on the bus is a distinct C++ type. CANInterface holds
 * one storage slot per registered type — last-value semantics, no heap
 * allocation. The callback fires each time a matching frame is received.
 *
 * @tparam ID          CAN ID (11-bit standard or 29-bit extended)
 * @tparam Length      Payload length in bytes (1–8 classic CAN, 1–64 CAN-FD)
 * @tparam ExtendedId  true for 29-bit extended identifier (default: false)
 * @tparam FdFormat    true for CAN-FD frame (default: false)
 * @tparam Brs         true to enable bit rate switching, CAN-FD only (default: false)
 *
 * RX — register a callback on the interface's message slot:
 * @code
 *   using EngineSpeed = chipz::network::CANMessage<0x100, 2>;
 *   g_can1.message<EngineSpeed>().setCallback([](const EngineSpeed& m) {
 *       uint16_t rpm = (m.data()[0] << 8) | m.data()[1];
 *   });
 * @endcode
 *
 * TX — fill data and call transmit():
 * @code
 *   using ThrottleCmd = chipz::network::CANMessage<0x200, 1>;
 *   static ThrottleCmd tx_msg;
 *   tx_msg.data()[0] = setpoint;
 *   g_can1.transmit(tx_msg);
 * @endcode
 */
template <uint32_t ID, uint8_t Length, bool ExtendedId = false, bool FdFormat = false, bool Brs = false>
class CANMessage {
    public:
    static constexpr uint32_t kId         = ID;
    static constexpr uint8_t  kLength     = Length;
    static constexpr bool     kExtendedId = ExtendedId;
    static constexpr bool     kFdFormat   = FdFormat;
    static constexpr bool     kBrs        = Brs;
    static constexpr uint8_t  kDlc        = lengthToDlc(Length);

    using Callback = std::function<void(const CANMessage&)>;

    void setCallback(Callback cb)
    {
        callback_ = std::move(cb);
    }

    uint8_t* data()
    {
        return data_;
    }
    const uint8_t* data() const
    {
        return data_;
    }

    /**
     * @brief Called by CANInterface on ID match
     *
     * Copies up to Length bytes from src then fires the callback.
     * Excess bytes (length > Length) are silently ignored.
     */
    void onReceived(const uint8_t* src, uint8_t length)
    {
        uint8_t n = length < Length ? length : Length;
        for (uint8_t i = 0; i < n; ++i) {
            data_[i] = src[i];
        }
        if (callback_) {
            callback_(*this);
        }
    }

    private:
    uint8_t  data_[Length]{};
    Callback callback_;
};

// -------------------------------------------------------------------------
// CANInterfaceBase — non-template base for ISR notification
// -------------------------------------------------------------------------

/**
 * @brief Non-template base for CANInterface
 *
 * Exposes only the ISR notification surface so that port files
 * (chipz_isrs.cpp) can extern g_can1 / g_can2 as CANInterfaceBase* without
 * knowing the concrete message type list. The frame read from the hardware
 * FIFO happens in the ISR before calling notifyRxComplete().
 */
class CANInterfaceBase {
    public:
    virtual ~CANInterfaceBase() = default;

    /**
     * @brief Decrement TX FIFO occupancy counter
     *
     * Call from HAL_FDCAN_TxBufferCompleteCallback once per acknowledged frame.
     */
    virtual void notifyTxComplete() = 0;

    /**
     * @brief Dispatch a received frame to the matching registered message slot
     *
     * Call from HAL_FDCAN_RxFifo0Callback / HAL_FDCAN_RxFifo1Callback after
     * reading the raw frame from the hardware FIFO via HAL_FDCAN_GetRxMessage.
     *
     * @param id     CAN ID (standard or extended)
     * @param data   Payload bytes read from the hardware FIFO
     * @param length Payload byte count (use dlcToLength() to convert HAL DLC)
     */
    virtual void notifyRxComplete(uint32_t id, const uint8_t* data, uint8_t length) = 0;

    /**
     * @brief Signal a hardware error
     *
     * Call from HAL_FDCAN_ErrorCallback / HAL_FDCAN_ErrorStatusCallback.
     */
    virtual void notifyError() = 0;
};

// -------------------------------------------------------------------------
// CANInterface — variadic template, owns all registered RX message slots
// -------------------------------------------------------------------------

/**
 * @brief CAN / CAN-FD network interface
 *
 * Holds one storage slot per registered RX message type in a std::tuple.
 * On RX, dispatches by ID using short-circuit evaluation — the first
 * matching slot's callback fires and the search stops.
 * TX is a direct template call — no registration required for TX messages.
 *
 * No heap allocation in the RX path: the ISR (chipz_isrs.cpp) reads raw
 * bytes from the HAL FIFO and calls notifyRxComplete(id, data, length);
 * all frame storage is inline in the tuple slots.
 *
 * @tparam TxFifoDepth Hardware TX FIFO depth in frames — specify explicitly.
 *                     Use 1 if the hardware has a single TX mailbox.
 * @tparam RxMessages  CANMessage types this interface can receive.
 *                     May be empty if only TX is needed.
 *
 * @code
 *   using EngineSpeed = chipz::network::CANMessage<0x100, 2>;
 *   using ThrottlePos = chipz::network::CANMessage<0x101, 1>;
 *
 *   chipz::network::CANInterface<3, EngineSpeed, ThrottlePos> g_can1{ tx_func };
 *
 *   g_can1.message<EngineSpeed>().setCallback([](const EngineSpeed& m) { ... });
 * @endcode
 */
template <size_t TxFifoDepth, typename... RxMessages>
class CANInterface : public CANInterfaceBase {
    public:
    /**
     * @brief HAL transmit function
     *
     * Receives the full set of frame attributes as primitive types so the
     * function signature is independent of any CANMessage template parameters.
     *
     * @return 0 on success, non-zero HAL error code otherwise
     */
    using TxFunction = std::function<int(uint32_t id, bool extended_id, bool fd_format, bool brs, const uint8_t* data,
                                         uint8_t length)>;

    explicit CANInterface(TxFunction tx_func) : tx_func_(std::move(tx_func))
    {
    }

    CANInterface(const CANInterface&)            = delete;
    CANInterface& operator=(const CANInterface&) = delete;

    // -------------------------------------------------------------------------
    // TX — works with any CANMessage type, no prior registration required
    // -------------------------------------------------------------------------

    /**
     * @brief Transmit a typed CAN message
     *
     * Frame attributes (ID, flags, length) are extracted from the message
     * type's compile-time constants. Returns false if the TX FIFO is full.
     */
    template <uint32_t ID, uint8_t Length, bool Ext, bool Fd, bool B>
    bool transmit(const CANMessage<ID, Length, Ext, Fd, B>& msg)
    {
        if (isTxFifoFull() || !tx_func_) {
            return false;
        }
        ++tx_pending_;
        if (tx_func_(ID, Ext, Fd, B, msg.data(), Length) != 0) {
            --tx_pending_;
            return false;
        }
        return true;
    }

    // -------------------------------------------------------------------------
    // RX message access
    // -------------------------------------------------------------------------

    /**
     * @brief Access a registered RX message slot to set its callback or read data
     *
     * @tparam Msg Must be one of the RxMessages types passed to CANInterface
     */
    template <typename Msg>
    Msg& message()
    {
        return std::get<Msg>(messages_);
    }

    template <typename Msg>
    const Msg& message() const
    {
        return std::get<Msg>(messages_);
    }

    // -------------------------------------------------------------------------
    // TX FIFO state
    // -------------------------------------------------------------------------

    bool isTxFifoFull() const
    {
        return tx_pending_ >= TxFifoDepth;
    }
    bool isTxFifoEmpty() const
    {
        return tx_pending_ == 0;
    }
    size_t txFifoFreeSlots() const
    {
        return tx_pending_ < TxFifoDepth ? TxFifoDepth - tx_pending_ : 0u;
    }

    // -------------------------------------------------------------------------
    // CANInterfaceBase overrides — called from ISR context
    // -------------------------------------------------------------------------

    void notifyTxComplete() override
    {
        if (tx_pending_ > 0) {
            --tx_pending_;
        }
    }

    void notifyRxComplete(uint32_t id, const uint8_t* data, uint8_t length) override
    {
        // Short-circuit fold: stops at the first matching message type
        (dispatchSingle<RxMessages>(id, data, length) || ...);
    }

    void notifyError() override
    {
    }

    private:
    std::tuple<RxMessages...> messages_;
    TxFunction                tx_func_;
    size_t                    tx_pending_{0};

    template <typename Msg>
    bool dispatchSingle(uint32_t id, const uint8_t* data, uint8_t length)
    {
        if (Msg::kId == id) {
            std::get<Msg>(messages_).onReceived(data, length);
            return true;
        }
        return false;
    }
};

}  // namespace network
}  // namespace chipz

#endif  // CHIPZ_NETWORK_CAN_INTERFACE_HPP
