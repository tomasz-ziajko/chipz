// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_CHIP_HPP
#define CHIPZ_CHIP_HPP

#include "communication_interface.hpp"
#include "concepts.hpp"
#include "isr_source.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <tuple>
#include <vector>

namespace chipz {

namespace detail {

// --- type_index_v: index of T in pack Ts... ---
template<typename T, typename... Ts>
struct type_index;

template<typename T, typename... Rest>
struct type_index<T, T, Rest...> : std::integral_constant<size_t, 0> {};

template<typename T, typename First, typename... Rest>
struct type_index<T, First, Rest...>
    : std::integral_constant<size_t, 1 + type_index<T, Rest...>::value> {};

template<typename T, typename... Ts>
constexpr size_t type_index_v = type_index<T, Ts...>::value;

// --- all_unique_v: true when all types in pack are distinct ---
template<typename T, typename... Ts>
constexpr bool contains_v = (std::is_same_v<T, Ts> || ...);

template<typename... Ts>
struct all_unique : std::true_type {};

template<typename T, typename... Rest>
struct all_unique<T, Rest...>
    : std::bool_constant<!contains_v<T, Rest...> && all_unique<Rest...>::value> {};

template<typename... Ts>
constexpr bool all_unique_v = all_unique<Ts...>::value;

} // namespace detail

/**
 * @brief Non-template base class for all externally-connected chips
 *
 * Defines the common virtual interface and owns the static registry.
 * Being non-template allows heterogeneous collections of chips
 * and enables the static management methods (initializeAll, runAllMain, etc.).
 *
 * AUTOMATIC REGISTRATION:
 * Each ChipBase instance automatically registers itself upon construction
 * and unregisters upon destruction.
 *
 * COMM INTERRUPT ROUTING:
 * Core calls getCommInterfaces() at add() time to register all of the chip's
 * communication interfaces. When an interrupt fires on any of them, Core
 * looks up which chip is active on that bus and calls onInterrupt() on it,
 * passing a reference to the specific interface that fired.
 *
 * SCHEDULING:
 * Core injects defer and claim-bus callbacks at add() time. Drivers call
 * defer_ms_ / defer_us_ from main() to skip the next N ms/µs. The
 * claim-bus callback is injected per interface via setClaimBusCallback()
 * and is invoked by Chip<CommInterfaces...>::transmit() / receive() wrappers
 * before each transfer.
 */
class ChipBase {
public:
    enum class Status {
        Uninitialized,
        Ready,
        Busy,
        Error,
        Disconnected
    };

    virtual ~ChipBase() {
        unregisterInstance(this);
    }

    virtual bool initialize() = 0;
    virtual bool reset() = 0;
    virtual bool isReady() const = 0;
    virtual Status getStatus() const = 0;
    virtual std::string getDeviceId() const = 0;
    virtual bool main() = 0;

    /**
     * @brief Get default scheduling priority for this chip
     *
     * Lower value = higher priority (0 = highest, 255 = lowest).
     * Can be overridden per driver class or at runtime via Core::setPriority().
     *
     * @return Default priority (128 = mid-range)
     */
    virtual uint8_t getDefaultPriority() const { return 128; }

    /**
     * @brief Get all communication interfaces used by this chip
     *
     * Called by Core::add() to register each interface for interrupt routing.
     * Returns an empty span for chips with no communication interfaces.
     *
     * @return Span over the chip's communication interfaces
     */
    virtual std::span<CommunicationInterface*> getCommInterfaces() { return {}; }

    /**
     * @brief Handle a routed communication interrupt
     *
     * Called by Core when a hardware interrupt fires on one of this chip's
     * communication interfaces. The @p which parameter identifies the
     * interface that fired. Chip<CommInterfaces...> overrides this and
     * dispatches to onTransferComplete / onError / onArbitrationLost.
     *
     * @param which   The interface whose interrupt fired
     * @param type    Type of interrupt that fired
     * @param success True for TransferComplete with no error
     */
    virtual void onInterrupt(CommunicationInterface& which,
                             CommunicationInterface::InterruptType type,
                             bool success) {
        (void)which;
        (void)type;
        (void)success;
    }

    /**
     * @brief Declare which non-communication ISR sources this chip needs
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
     * @return View over the ISR sources required by this chip
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
     * @brief Inject a claim-bus callback for a specific communication interface
     *
     * Called by Core::add() once per registered interface. The Chip<> template
     * override stores the callback indexed by interface, and invokes it before
     * each transmit() / receive() to tell Core which chip is active on that bus.
     *
     * @param comm Interface to associate the callback with
     * @param fn   Callback to invoke before each transfer on @p comm
     */
    virtual void setClaimBusCallback(CommunicationInterface* comm,
                                     std::function<void()> fn) {
        (void)comm;
        (void)fn;
    }

    // -------------------------------------------------------------------------
    // Static registry utilities
    // -------------------------------------------------------------------------

    static bool initializeAll() {
        bool all_success = true;
        for (auto* chip : getRegistry()) {
            if (!chip->initialize()) {
                all_success = false;
            }
        }
        return all_success;
    }

    static bool resetAll() {
        bool all_success = true;
        for (auto* chip : getRegistry()) {
            if (!chip->reset()) {
                all_success = false;
            }
        }
        return all_success;
    }

    static bool runAllMain() {
        bool all_success = true;
        for (auto* chip : getRegistry()) {
            if (!chip->main()) {
                all_success = false;
            }
        }
        return all_success;
    }

    static bool allReady() {
        for (const auto* chip : getRegistry()) {
            if (!chip->isReady()) {
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
        for (const auto* chip : getRegistry()) {
            if (chip->getStatus() == status) {
                ++count;
            }
        }
        return count;
    }

protected:
    ChipBase() {
        registerInstance(this);
    }

    ChipBase(const ChipBase&) = delete;
    ChipBase& operator=(const ChipBase&) = delete;

    ChipBase(ChipBase&&) = default;
    ChipBase& operator=(ChipBase&&) = default;

    std::function<void(uint32_t)> defer_ms_;
    std::function<void(uint32_t)> defer_us_;

private:
    static std::vector<ChipBase*>& getRegistry() {
        static std::vector<ChipBase*> registry;
        return registry;
    }

    static void registerInstance(ChipBase* instance) {
        getRegistry().push_back(instance);
    }

    static void unregisterInstance(ChipBase* instance) {
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
 * @brief Template middle layer binding a chip to one or more communication interfaces
 *
 * Inherits ChipBase and owns references to all communication interfaces.
 * Device drivers inherit from this class, specifying every bus they use.
 *
 * Provides:
 * - get<T>()                  — access an interface by type
 * - transmit<T>() / receive<T>() — bus-selecting transfer wrappers that
 *                               claim the bus with Core before forwarding
 * - setConnection<T>()        — register the per-interface connection ID
 * - getCommInterfaces()       — returns all interfaces for Core registration
 * - Per-interrupt-type virtuals (onTransferComplete, onError,
 *   onArbitrationLost) with the firing interface passed as the first argument
 *
 * All interface types in the pack must be distinct.
 *
 * @tparam CommInterfaces Communication interface types
 *         (each must satisfy chipz::concepts::CommunicationInterface)
 */
template<chipz::concepts::CommunicationInterface... CommInterfaces>
class Chip : public ChipBase {
    static_assert(detail::all_unique_v<CommInterfaces...>,
                  "Chip: all communication interface types must be distinct");

public:
    /**
     * @brief Access a communication interface by type
     * @tparam T Interface type (must be one of CommInterfaces)
     */
    template<typename T>
    T& get() {
        return *std::get<detail::type_index_v<T, CommInterfaces...>>(comms_);
    }

    template<typename T>
    const T& get() const {
        return *std::get<detail::type_index_v<T, CommInterfaces...>>(comms_);
    }

    std::span<CommunicationInterface*> getCommInterfaces() override {
        return comm_ptrs_;
    }

    void onInterrupt(CommunicationInterface& which,
                     CommunicationInterface::InterruptType type,
                     bool success) override {
        using IT = CommunicationInterface::InterruptType;
        switch (type) {
            case IT::TransferComplete: onTransferComplete(which, success); break;
            case IT::Error:            onError(which);                     break;
            case IT::ArbitrationLost:  onArbitrationLost(which);           break;
        }
    }

    void setClaimBusCallback(CommunicationInterface* comm,
                             std::function<void()> fn) override {
        for (size_t i = 0; i < N; ++i) {
            if (comm_ptrs_[i] == comm) {
                claim_bus_fns_[i] = std::move(fn);
                return;
            }
        }
    }

protected:
    explicit Chip(CommInterfaces&... comms)
        : comms_{&comms...}
    {
        conn_ids_.fill(CommunicationInterface::kInvalidConnection);
        initCommPtrs(std::index_sequence_for<CommInterfaces...>{});
    }

    /**
     * @brief Register this chip's connection on a specific interface
     *
     * Call from initialize() with the ConnectionId returned by the interface's
     * registerConnection() method. From that point, selectConnection() is
     * called automatically before every transmit<T>() and receive<T>() on
     * that interface.
     *
     * @tparam T Interface type
     * @param  id ConnectionId returned by T::registerConnection()
     */
    template<typename T>
    void setConnection(CommunicationInterface::ConnectionId id) {
        conn_ids_[detail::type_index_v<T, CommInterfaces...>] = id;
    }

    /**
     * @brief Transmit data on the selected interface
     *
     * Claims the bus with Core before forwarding. Returns false immediately
     * if the bus is busy — the driver should retry next cycle.
     *
     * @tparam T     Interface type to transmit on
     * @param  data   Data to transmit
     * @param  length Number of bytes
     */
    template<typename T>
    bool transmit(const uint8_t* data, size_t length) {
        constexpr size_t idx = detail::type_index_v<T, CommInterfaces...>;
        auto& comm = get<T>();
        if (!comm.isReady()) return false;
        if (conn_ids_[idx] != CommunicationInterface::kInvalidConnection)
            comm.selectConnection(conn_ids_[idx]);
        if (claim_bus_fns_[idx]) claim_bus_fns_[idx]();
        return comm.transmit(data, length);
    }

    /**
     * @brief Async transmit with duration hint — arms CompletionSources on the interface
     *
     * Identical to transmit<T>(data, length) in terms of bus claiming, but
     * forwards duration_us to the interface so CompletionSources can be armed.
     * On interfaces without CompletionSources this is equivalent to the
     * immediate overload.
     *
     * @tparam T          Interface type to transmit on
     * @param  data        Data to transmit
     * @param  length      Number of bytes
     * @param  duration_us Duration hint for TimerCompletionSource (µs)
     */
    template<typename T>
    bool transmit(const uint8_t* data, size_t length, uint32_t duration_us) {
        constexpr size_t idx = detail::type_index_v<T, CommInterfaces...>;
        auto& comm = get<T>();
        if (!comm.isReady()) return false;
        if (conn_ids_[idx] != CommunicationInterface::kInvalidConnection)
            comm.selectConnection(conn_ids_[idx]);
        if (claim_bus_fns_[idx]) claim_bus_fns_[idx]();
        return comm.transmit(data, length, duration_us);
    }

    /**
     * @brief Receive data on the selected interface
     *
     * Claims the bus with Core before forwarding. Returns false if busy.
     *
     * @tparam T      Interface type to receive on
     * @param  buffer Buffer to receive into
     * @param  length Number of bytes
     */
    template<typename T>
    bool receive(uint8_t* buffer, size_t length) {
        constexpr size_t idx = detail::type_index_v<T, CommInterfaces...>;
        auto& comm = get<T>();
        if (!comm.isReady()) return false;
        if (conn_ids_[idx] != CommunicationInterface::kInvalidConnection)
            comm.selectConnection(conn_ids_[idx]);
        if (claim_bus_fns_[idx]) claim_bus_fns_[idx]();
        return comm.receive(buffer, length);
    }

    // -------------------------------------------------------------------------
    // Per-interrupt-type virtuals — override in concrete drivers as needed.
    // @p which identifies which interface fired.
    // -------------------------------------------------------------------------

    virtual void onTransferComplete(CommunicationInterface& which, bool success) {
        (void)which; (void)success;
    }

    virtual void onError(CommunicationInterface& which) { (void)which; }

    virtual void onArbitrationLost(CommunicationInterface& which) { (void)which; }

private:
    static constexpr size_t N = sizeof...(CommInterfaces);

    std::tuple<CommInterfaces*...>                               comms_;
    std::array<CommunicationInterface*, N>                       comm_ptrs_{};
    std::array<CommunicationInterface::ConnectionId, N>          conn_ids_{};
    std::array<std::function<void()>, N>                         claim_bus_fns_{};

    template<size_t... Is>
    void initCommPtrs(std::index_sequence<Is...>) {
        ((comm_ptrs_[Is] = std::get<Is>(comms_)), ...);
    }
};

} // namespace chipz

#endif // CHIPZ_CHIP_HPP
