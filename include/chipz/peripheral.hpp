// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_PERIPHERAL_HPP
#define CHIPZ_PERIPHERAL_HPP

#include "communication_interface.hpp"
#include "concepts.hpp"
#include "isr_source.hpp"
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace chipz {

/**
 * @brief Non-template base class for all peripheral devices
 *
 * Defines the common virtual interface and owns the static registry.
 * Being non-template allows heterogeneous collections of peripherals
 * and enables the static management methods (initializeAll, runAllMain, etc.).
 *
 * AUTOMATIC REGISTRATION:
 * Each PeripheralBase instance automatically registers itself upon construction
 * and unregisters upon destruction.
 *
 * COMM INTERRUPT ROUTING:
 * Core calls getCommInterface() at add() time to register the peripheral's
 * communication interface. When an interrupt fires, Core looks up which
 * peripheral is active on that bus and calls onInterrupt() on it.
 *
 * SCHEDULING:
 * Core injects defer and claim-bus callbacks at add() time. Drivers call
 * defer_ms_ / defer_us_ from main() to skip the next N ms/µs. The
 * claim_bus_fn_ is invoked by Peripheral<CommInterface>::transmit() /
 * receive() wrappers before each transfer.
 */
class PeripheralBase {
public:
    enum class Status {
        Uninitialized,
        Ready,
        Busy,
        Error,
        Disconnected
    };

    virtual ~PeripheralBase() {
        unregisterInstance(this);
    }

    virtual bool initialize() = 0;
    virtual bool reset() = 0;
    virtual bool isReady() const = 0;
    virtual Status getStatus() const = 0;
    virtual std::string getDeviceId() const = 0;
    virtual bool main() = 0;

    /**
     * @brief Get default scheduling priority for this peripheral
     *
     * Lower value = higher priority (0 = highest, 255 = lowest).
     * Can be overridden per driver class or at runtime via Core::setPriority().
     *
     * @return Default priority (128 = mid-range)
     */
    virtual uint8_t getDefaultPriority() const { return 128; }

    /**
     * @brief Get the communication interface used by this peripheral
     *
     * Called by Core::add() to register the interface for interrupt routing.
     * Returns nullptr for peripherals without a communication interface.
     *
     * @return Pointer to communication interface, or nullptr
     */
    virtual CommunicationInterface* getCommInterface() { return nullptr; }

    /**
     * @brief Handle a routed communication interrupt
     *
     * Called by Core when a hardware interrupt fires on this peripheral's
     * communication interface. Peripheral<CommInterface> overrides this and
     * dispatches to onTransferComplete / onError / onArbitrationLost.
     *
     * @param type    Type of interrupt that fired
     * @param success True for TransferComplete with no error
     */
    virtual void onInterrupt(CommunicationInterface::InterruptType type, bool success) {
        (void)type;
        (void)success;
    }

    /**
     * @brief Declare which non-communication ISR sources this peripheral needs
     *
     * Called by Core::add() to populate the ISR dispatch table. Return a
     * span over a static constexpr array — no heap allocation.
     *
     * Example (DS3231 with an alarm pin on EXTI3):
     * @code
     *   static constexpr ISRSource kISRs[] = { ISRSource::EXTI3 };
     *   std::span<const ISRSource> requiredISRs() const noexcept override {
     *       return kISRs;
     *   }
     * @endcode
     *
     * @return View over the ISR sources required by this peripheral
     */
    virtual std::span<const ISRSource> requiredISRs() const noexcept { return {}; }

    /**
     * @brief Handle a routed non-communication hardware interrupt
     *
     * Called by Core::service() pass 1 when an ISR source declared in
     * requiredISRs() fires. Runs in main-loop context — never in ISR context.
     *
     * @param source The ISRSource that fired
     */
    virtual void onISR(ISRSource source) noexcept { (void)source; }

    // -------------------------------------------------------------------------
    // Callback injection — called by Core::add()
    // -------------------------------------------------------------------------

    /**
     * @brief Inject defer callbacks from Core
     *
     * Drivers call defer_ms_(N) or defer_us_(N) from main() to ask the
     * scheduler not to call them again for N milliseconds / microseconds.
     * deferUs degrades to the nearest representable tick on coarse timers.
     */
    void setDeferCallbacks(std::function<void(uint32_t)> defer_ms,
                           std::function<void(uint32_t)> defer_us) {
        defer_ms_ = std::move(defer_ms);
        defer_us_ = std::move(defer_us);
    }

    /**
     * @brief Inject claim-bus callback from Core
     *
     * Called by Peripheral<CommInterface>::transmit() / receive() before
     * each transfer to inform Core which peripheral is active on the bus.
     * This lets Core route the next transfer-complete interrupt correctly.
     */
    void setClaimBusCallback(std::function<void()> fn) {
        claim_bus_fn_ = std::move(fn);
    }

    // -------------------------------------------------------------------------
    // Static registry utilities
    // -------------------------------------------------------------------------

    static bool initializeAll() {
        bool all_success = true;
        for (auto* peripheral : getRegistry()) {
            if (!peripheral->initialize()) {
                all_success = false;
            }
        }
        return all_success;
    }

    static bool resetAll() {
        bool all_success = true;
        for (auto* peripheral : getRegistry()) {
            if (!peripheral->reset()) {
                all_success = false;
            }
        }
        return all_success;
    }

    static bool runAllMain() {
        bool all_success = true;
        for (auto* peripheral : getRegistry()) {
            if (!peripheral->main()) {
                all_success = false;
            }
        }
        return all_success;
    }

    static bool allReady() {
        for (const auto* peripheral : getRegistry()) {
            if (!peripheral->isReady()) {
                return false;
            }
        }
        return true;
    }

    static size_t getCount() {
        return getRegistry().size();
    }

    static size_t getStatusCount(Status status) {
        size_t count = 0;
        for (const auto* peripheral : getRegistry()) {
            if (peripheral->getStatus() == status) {
                ++count;
            }
        }
        return count;
    }

protected:
    PeripheralBase() {
        registerInstance(this);
    }

    PeripheralBase(const PeripheralBase&) = delete;
    PeripheralBase& operator=(const PeripheralBase&) = delete;

    PeripheralBase(PeripheralBase&&) = default;
    PeripheralBase& operator=(PeripheralBase&&) = default;

    std::function<void(uint32_t)> defer_ms_;
    std::function<void(uint32_t)> defer_us_;
    std::function<void()>         claim_bus_fn_;

private:
    static std::vector<PeripheralBase*>& getRegistry() {
        static std::vector<PeripheralBase*> registry;
        return registry;
    }

    static void registerInstance(PeripheralBase* instance) {
        getRegistry().push_back(instance);
    }

    static void unregisterInstance(PeripheralBase* instance) {
        auto& registry = getRegistry();
        for (auto it = registry.begin(); it != registry.end(); ++it) {
            if (*it == instance) {
                registry.erase(it);
                break;
            }
        }
    }
};

/**
 * @brief Template middle layer binding a peripheral to its communication interface
 *
 * Inherits PeripheralBase and owns a reference to the communication interface.
 * Device drivers inherit from this class with their specific interface type.
 *
 * Provides:
 * - transmit() / receive() wrappers that claim the bus with Core before
 *   forwarding to the underlying comm interface
 * - Per-interrupt-type virtual handlers (onTransferComplete, onError,
 *   onArbitrationLost) that concrete drivers override as needed
 * - getCommInterface() and onInterrupt() implementations for Core routing
 *
 * @tparam CommInterface Communication interface type (must satisfy chipz::concepts::CommunicationInterface)
 */
template<chipz::concepts::CommunicationInterface CommInterface>
class Peripheral : public PeripheralBase {
public:
    CommunicationInterface* getCommInterface() override {
        return &comm_;
    }

    void onInterrupt(CommunicationInterface::InterruptType type, bool success) override {
        using IT = CommunicationInterface::InterruptType;
        switch (type) {
            case IT::TransferComplete: onTransferComplete(success); break;
            case IT::Error:            onError();                   break;
            case IT::ArbitrationLost:  onArbitrationLost();         break;
        }
    }

protected:
    CommInterface& comm_;
    CommunicationInterface::ConnectionId conn_id_{CommunicationInterface::kInvalidConnection};

    explicit Peripheral(CommInterface& comm) : comm_(comm) {}

    /**
     * @brief Transmit data — claims the bus with Core before forwarding
     *
     * Returns false immediately if the bus is busy (another peripheral's
     * transfer is in flight). In that case the driver should return from
     * main() and retry next cycle.
     *
     * @param data   Data to transmit
     * @param length Number of bytes
     * @return true if transmission started, false if bus busy or error
     */
    /**
     * @brief Register this peripheral's connection with its communication interface
     *
     * Call from initialize() with the ConnectionId returned by the interface's
     * registerConnection() method. From that point, selectConnection() is
     * called automatically before every transmit() and receive().
     */
    void setConnection(CommunicationInterface::ConnectionId id) {
        conn_id_ = id;
    }

    bool transmit(const uint8_t* data, size_t length) {
        if (!comm_.isReady()) {
            return false;
        }
        if (conn_id_ != CommunicationInterface::kInvalidConnection) {
            comm_.selectConnection(conn_id_);
        }
        if (claim_bus_fn_) {
            claim_bus_fn_();
        }
        return comm_.transmit(data, length);
    }

    /**
     * @brief Receive data — claims the bus with Core before forwarding
     *
     * Returns false immediately if the bus is busy.
     *
     * @param buffer Buffer to receive into
     * @param length Number of bytes
     * @return true if reception started, false if bus busy or error
     */
    bool receive(uint8_t* buffer, size_t length) {
        if (!comm_.isReady()) {
            return false;
        }
        if (conn_id_ != CommunicationInterface::kInvalidConnection) {
            comm_.selectConnection(conn_id_);
        }
        if (claim_bus_fn_) {
            claim_bus_fn_();
        }
        return comm_.receive(buffer, length);
    }

    // -------------------------------------------------------------------------
    // Per-interrupt-type virtuals — override in concrete drivers as needed
    // -------------------------------------------------------------------------

    /**
     * @brief Called by Core when a transfer-complete interrupt is routed here
     * @param success true if the transfer succeeded
     */
    virtual void onTransferComplete(bool success) { (void)success; }

    /**
     * @brief Called by Core when a bus/protocol error interrupt is routed here
     */
    virtual void onError() {}

    /**
     * @brief Called by Core when an I2C arbitration-lost interrupt is routed here
     */
    virtual void onArbitrationLost() {}
};

} // namespace chipz

#endif // CHIPZ_PERIPHERAL_HPP
