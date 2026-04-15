// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_INTERFACES_PARALLEL_INTERFACE_HPP
#define CHIPZ_INTERFACES_PARALLEL_INTERFACE_HPP

#include "../core/communication_interface.hpp"
#include "../core/concepts.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <tuple>

namespace chipz {
namespace interfaces {

/**
 * @brief N-line parallel / GPIO communication interface
 *
 * Models an N-bit parallel bus (or a single GPIO line when N == 1) as a
 * CommunicationInterface. The underlying transport is fully abstracted behind
 * two callbacks injected at construction:
 *
 *   WriteFn — called with the N-bit bus value on transmit
 *   ReadFn  — called to sample the N-bit bus value on receive
 *
 * Either callback may be null, signalling that the direction is not supported.
 * transmit() returns false if WriteFn is null; receive() returns false if
 * ReadFn is null.
 *
 * ASYNC COMPLETION
 * By default transmit is synchronous: notifyTransferComplete fires before
 * the call returns. For cases that require settling time or depend on an
 * underlying async transfer (e.g. an I2C GPIO expander), pass one or more
 * CompletionSource objects as additional template arguments and constructor
 * arguments, then use the timed overload:
 *
 *   transmit(data, len, duration_us)
 *
 * All armed sources form a barrier — notifyTransferComplete fires only once
 * every source has signalled. receive() is always synchronous.
 *
 * BUS WIDTH
 * N must be in [1, 32]. Data is packed little-endian into ceil(N/8) bytes.
 * The GPIOInterface alias sets N == 1.
 *
 * EXAMPLES
 * @code
 *   // GPIO bit-bang with timer settling (immediate if no timer needed):
 *   ParallelInterface<6> bus(writeFn, readFn);
 *
 *   // GPIO with hardware timer for E-pulse timing:
 *   TimerCompletionSource timerSrc(myTimer);
 *   ParallelInterface<6, TimerCompletionSource> bus(writeFn, readFn,
 *                                                   std::move(timerSrc));
 *
 *   // I2C expander — wait for both I2C completion and a minimum timer delay:
 *   TimerCompletionSource  timerSrc(myTimer);
 *   ExternalCompletionSource extSrc([&i2c](auto cb){ i2c.onNextComplete(cb); });
 *   ParallelInterface<6, TimerCompletionSource, ExternalCompletionSource>
 *       bus(writeFn, readFn, std::move(timerSrc), std::move(extSrc));
 * @endcode
 *
 * @tparam N       Number of parallel lines (1–32)
 * @tparam Sources Zero or more CompletionSource types (owned by value)
 */
template<size_t N, typename... Sources>
class ParallelInterface : public CommunicationInterface {
    static_assert(N >= 1 && N <= 32, "ParallelInterface: N must be between 1 and 32");
    static_assert((chipz::concepts::CompletionSource<Sources> && ...),
                  "ParallelInterface: each Source must satisfy chipz::concepts::CompletionSource "
                  "(e.g. CompletionSource::Timer, CompletionSource::External)");

public:
    using WriteFn = std::function<void(uint32_t)>;
    using ReadFn  = std::function<uint32_t()>;

    static constexpr uint32_t kBusMask   = (N < 32u) ? ((uint32_t(1) << N) - 1u) : 0xFFFFFFFFu;
    static constexpr size_t   kByteCount = (N + 7u) / 8u;

    /**
     * @brief Construct with optional completion sources
     *
     * Sources are owned by value.  When no sources are provided, both
     * transmit() overloads complete synchronously before returning.
     */
    explicit ParallelInterface(WriteFn write_fn, ReadFn read_fn, Sources... sources)
        : write_fn_(std::move(write_fn))
        , read_fn_(std::move(read_fn))
        , sources_(std::move(sources)...)
    {}

    // -------------------------------------------------------------------------
    // CommunicationInterface — transmit
    // -------------------------------------------------------------------------

    /**
     * @brief Immediate transmit — notifyTransferComplete fires before returning
     */
    bool transmit(const uint8_t* data, size_t length) override {
        if (!write_fn_ || transfer_in_progress_) return false;
        transfer_in_progress_ = true;
        write_fn_(bus_value_from(data, length));
        notifyTransferComplete(true);
        return true;
    }

    /**
     * @brief Async transmit — completion deferred to armed sources
     *
     * Arms all construction-time CompletionSources with duration_us. If no
     * sources were provided, behaves identically to the immediate overload.
     * notifyTransferComplete fires once every source has signalled.
     *
     * @param duration_us Forwarded to timer sources (ignored by external sources
     *                    and when no sources are configured).
     */
    bool transmit(const uint8_t* data, size_t length, uint32_t duration_us) override {
        if (!write_fn_ || transfer_in_progress_) return false;
        transfer_in_progress_ = true;
        write_fn_(bus_value_from(data, length));
        if constexpr (sizeof...(Sources) == 0) {
            notifyTransferComplete(true);
        } else {
            arm_sources(duration_us);
        }
        return true;
    }

    // -------------------------------------------------------------------------
    // CommunicationInterface — receive (always synchronous)
    // -------------------------------------------------------------------------

    /**
     * @brief Synchronous receive — samples the bus and returns immediately
     */
    bool receive(uint8_t* buffer, size_t length) override {
        if (!read_fn_ || transfer_in_progress_) return false;
        transfer_in_progress_ = true;
        bus_value_to(buffer, length, read_fn_());
        notifyTransferComplete(true);
        return true;
    }

private:
    WriteFn                  write_fn_;
    ReadFn                   read_fn_;
    std::tuple<Sources...>   sources_;
    std::atomic<int>         pending_count_{0};

    // -------------------------------------------------------------------------
    // Buffer ↔ bus-value conversion (little-endian, masked to N bits)
    // -------------------------------------------------------------------------

    static uint32_t bus_value_from(const uint8_t* data, size_t length) {
        uint32_t value = 0;
        const size_t bytes = std::min(length, kByteCount);
        for (size_t i = 0; i < bytes; ++i) {
            value |= static_cast<uint32_t>(data[i]) << (8u * i);
        }
        return value & kBusMask;
    }

    static void bus_value_to(uint8_t* buffer, size_t length, uint32_t value) {
        value &= kBusMask;
        const size_t bytes = std::min(length, kByteCount);
        for (size_t i = 0; i < bytes; ++i) {
            buffer[i] = static_cast<uint8_t>((value >> (8u * i)) & 0xFFu);
        }
    }

    // -------------------------------------------------------------------------
    // Completion barrier
    // -------------------------------------------------------------------------

    void arm_sources(uint32_t duration_us) {
        // Set count before arming any source — a timer with duration_us == 0
        // fires synchronously inside arm(), and must see the full count.
        pending_count_.store(static_cast<int>(sizeof...(Sources)),
                             std::memory_order_release);

        auto on_complete = [this]() {
            if (pending_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                notifyTransferComplete(true);
            }
        };

        std::apply([&](auto&... src) {
            (src.arm(duration_us, on_complete), ...);
        }, sources_);
    }
};

/**
 * @brief Single-line GPIO interface — degenerate case of ParallelInterface<1>
 *
 * Data byte 0 carries the pin state: 0 = low, 1 = high.
 * Add Sources... template arguments for async settling if needed.
 */
using GPIOInterface = ParallelInterface<1>;

} // namespace interfaces
} // namespace chipz

#endif // CHIPZ_INTERFACES_PARALLEL_INTERFACE_HPP
