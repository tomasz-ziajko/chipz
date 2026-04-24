// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_CORE_HPP
#define CHIPZ_CORE_HPP

#include "chip.hpp"
#include "communication_interface.hpp"
#include "timer_interface.hpp"
#include "wait_condition.hpp"
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <vector>

namespace chipz {

/**
 * @brief Central lifecycle and scheduling manager for chipz peripherals
 *
 * Execution model:
 *   - ISRs only set atomic flags — no driver code ever runs in interrupt context.
 *   - service() is called from the main loop. It runs in three passes:
 *       Pass 1a — non-comm IRQs: routed to onIRQ(); drivers waiting on that IRQ
 *                 are marked Immediate.
 *       Pass 1b — comm interrupts: routed to onInterrupt(); drivers waiting on
 *                 that interface are marked Immediate.
 *       Pass 2  — scheduler: all Immediate / overdue-Deadline drivers run in
 *                 priority order. Each driver's run() return value sets its next
 *                 wait condition — no spurious calls.
 *
 * Scheduling:
 *   run() returns a WaitCondition. Core suspends the driver until the condition
 *   is satisfied. One hardware timer drives all deadline-based waits.
 *
 *   Legacy drivers that override main() instead of run() continue to work
 *   unchanged — the default run() delegates to main() and returns Immediate.
 *   defer_ms_ / defer_us_ callbacks remain available as a shim during migration.
 *
 * @tparam IRQnType   Platform IRQn enum (e.g. chipz::port::stm32h5xx::IRQn)
 * @tparam kIRQnFirst Lowest IRQn value (typically -15 for ARM Cortex-M)
 * @tparam kIRQnLast  Highest IRQn value
 */
template<typename IRQnType, int16_t kIRQnFirst, int16_t kIRQnLast>
class Core {
public:
    static constexpr size_t kRange     = static_cast<size_t>(kIRQnLast - kIRQnFirst + 1);
    static constexpr size_t kMaskWords = (kRange + 31u) / 32u;

    explicit Core(TimerInterface& timer)
        : timer_(timer)
        , pending_(false)
        , tick_frequency_hz_(timer.getTickFrequencyHz())
        , ticks_per_ms_(static_cast<uint64_t>(tick_frequency_hz_) / 1000u)
    {
        CommunicationInterface::registerCorePending(&pending_);
        timer_.setElapsedCallback([this]() {
            pending_.store(true, std::memory_order_release);
        });
    }

    /**
     * @brief Route a non-comm hardware IRQ from the port ISR.
     * Safe to call from interrupt context — only sets atomic flags.
     */
    void onIRQ(IRQnType irqn) {
        const size_t idx = toIdx(irqn);
        if (idx < kRange) {
            isr_pending_[idx / 32u].fetch_or(
                1u << (idx % 32u),
                std::memory_order_release
            );
            pending_.store(true, std::memory_order_release);
        }
    }

    /**
     * @brief Wake service() from an ISR without routing to any chip.
     */
    void wakeFromISR() {
        pending_.store(true, std::memory_order_release);
    }

    /**
     * @brief Wake a Demand-suspended driver from non-ISR context.
     * Safe to call from the main loop or another driver's run().
     */
    void wake(ChipBase& peripheral) {
        for (auto& entry : entries_) {
            if (entry.peripheral == &peripheral) {
                entry.wait = WaitCondition::immediate();
                pending_.store(true, std::memory_order_release);
                return;
            }
        }
    }

    /**
     * @brief Register a peripheral and inject all runtime callbacks.
     * Must be called before initialize().
     */
    void add(ChipBase& peripheral) {
        peripheral.setDeferCallbacks(
            [this, &peripheral](uint32_t ms) {
                uint64_t ticks = std::max(
                    uint64_t(1),
                    static_cast<uint64_t>(ms) * ticks_per_ms_
                );
                deferEntry(peripheral, ticks);
            },
            [this, &peripheral](uint32_t us) {
                uint64_t ticks = std::max(
                    uint64_t(1),
                    static_cast<uint64_t>(us) * tick_frequency_hz_ / 1000000u
                );
                deferEntry(peripheral, ticks);
            }
        );

        for (auto* comm : peripheral.getCommInterfaces()) {
            bool already_registered = false;
            for (auto& entry : comm_entries_) {
                if (entry.comm == comm) {
                    already_registered = true;
                    break;
                }
            }

            if (!already_registered) {
                comm_entries_.push_back({comm, nullptr});
            }

            peripheral.setClaimBusCallback(comm, [this, comm, &peripheral]() {
                for (auto& entry : comm_entries_) {
                    if (entry.comm == comm) {
                        entry.active_peripheral = &peripheral;
                        return;
                    }
                }
            });
        }

        for (int16_t irqn_raw : peripheral.requiredIRQs()) {
            const auto idx = static_cast<size_t>(
                static_cast<int>(irqn_raw) - static_cast<int>(kIRQnFirst)
            );
            if (idx < kRange) {
                isr_table_[idx] = &peripheral;
            }
        }

        entries_.push_back({
            &peripheral,
            WaitCondition::immediate(),
            peripheral.getDefaultPriority(),
            false
        });
    }

    /**
     * @brief Initialize all registered peripherals and arm the timer.
     */
    bool initialize() {
        bool all_ok = true;
        for (auto& entry : entries_) {
            if (!entry.peripheral->initialize()) {
                all_ok = false;
            }
        }
        scheduleNextInterrupt();
        return all_ok;
    }

    void setPriority(ChipBase& peripheral, uint8_t priority) {
        for (auto& entry : entries_) {
            if (entry.peripheral == &peripheral) {
                entry.priority = priority;
                return;
            }
        }
    }

    /**
     * @brief Service due peripherals — call from main loop or RTOS task.
     *
     * Returns immediately if no interrupt or timer has fired since the last call.
     *
     * Pass 1a — routes pending non-comm IRQs; wakes IRQ-waiting drivers.
     * Pass 1b — routes pending comm interrupts; wakes comm-waiting drivers.
     * Pass 2  — runs all Immediate / overdue-Deadline drivers in priority order.
     *           Each driver's run() return value sets its next WaitCondition.
     */
    void service() {
        if (!pending_.exchange(false, std::memory_order_acquire)) {
            return;
        }

        // Pass 1a: route non-comm IRQs, wake IRQ-waiting drivers
        for (size_t w = 0; w < kMaskWords; ++w) {
            uint32_t fired = isr_pending_[w].exchange(0, std::memory_order_acquire);
            while (fired) {
                const int    bit      = std::countr_zero(fired);
                fired &= fired - 1;
                const size_t idx      = w * 32u + static_cast<size_t>(bit);
                if (idx < kRange) {
                    const int16_t irqn_val = static_cast<int16_t>(
                        static_cast<int>(idx) + static_cast<int>(kIRQnFirst)
                    );
                    for (auto& entry : entries_) {
                        if (entry.wait.type() == WaitCondition::Type::IRQ &&
                            entry.wait.irqn() == irqn_val) {
                            entry.wait = WaitCondition::immediate();
                        }
                    }
                    if (auto* p = isr_table_[idx]) {
                        p->onIRQ(irqn_val);
                    }
                }
            }
        }

        // Pass 1b: route comm interrupts, wake comm-waiting drivers
        for (auto& ce : comm_entries_) {
            if (ce.comm->hasInterruptPending()) {
                for (auto& entry : entries_) {
                    if (entry.wait.type() == WaitCondition::Type::Comm &&
                        entry.wait.commInterface() == ce.comm) {
                        entry.wait = WaitCondition::immediate();
                    }
                }
                if (ce.active_peripheral) {
                    ce.active_peripheral->onInterrupt(
                        *ce.comm,
                        ce.comm->getPendingInterruptType(),
                        ce.comm->getInterruptSuccess()
                    );
                }
                ce.comm->clearInterrupt();
            }
        }

        // Pass 2: run due drivers in priority order
        for (auto& entry : entries_) {
            entry.ran_this_cycle = false;
        }

        while (true) {
            const uint64_t now = timer_.getCurrentTick();

            ScheduleEntry* best = nullptr;
            for (auto& entry : entries_) {
                if (!entry.ran_this_cycle && isRunnable(entry, now)) {
                    if (!best || entry.priority < best->priority) {
                        best = &entry;
                    }
                }
            }

            if (!best) break;

            best->ran_this_cycle = true;
            WaitCondition cond = best->peripheral->run();

            // defer_ms_ / defer_us_ may have set a Deadline during run().
            // The explicit return value takes precedence unless it's Immediate,
            // in which case the defer deadline is respected.
            if (cond.type() == WaitCondition::Type::Immediate &&
                best->wait.type() == WaitCondition::Type::Deadline) {
                // defer was called — keep the deadline it set
            } else {
                best->wait = resolveWaitCondition(cond);
            }
        }

        scheduleNextInterrupt();
    }

private:
    struct ScheduleEntry {
        ChipBase*     peripheral;
        WaitCondition wait;
        uint8_t       priority;
        bool          ran_this_cycle;
    };

    struct CommEntry {
        CommunicationInterface* comm;
        ChipBase*               active_peripheral;
    };

    static constexpr size_t toIdx(IRQnType irqn) {
        return static_cast<size_t>(
            static_cast<int>(static_cast<int16_t>(irqn)) - static_cast<int>(kIRQnFirst)
        );
    }

    static bool isRunnable(const ScheduleEntry& e, uint64_t now) {
        switch (e.wait.type()) {
            case WaitCondition::Type::Immediate: return true;
            case WaitCondition::Type::Deadline:  return e.wait.ticks() <= now;
            default:                             return false;
        }
    }

    WaitCondition resolveWaitCondition(const WaitCondition& cond) const {
        const uint64_t now = timer_.getCurrentTick();
        switch (cond.type()) {
            case WaitCondition::Type::DelayMs:
                return WaitCondition::deadline(
                    now + static_cast<uint64_t>(cond.ms()) * ticks_per_ms_
                );
            case WaitCondition::Type::DelayUs:
                return WaitCondition::deadline(
                    now + static_cast<uint64_t>(cond.us()) *
                          static_cast<uint64_t>(tick_frequency_hz_) / 1000000u
                );
            default:
                return cond;
        }
    }

    void deferEntry(ChipBase& peripheral, uint64_t ticks) {
        const uint64_t deadline = timer_.getCurrentTick() + ticks;
        for (auto& entry : entries_) {
            if (entry.peripheral == &peripheral) {
                entry.wait = WaitCondition::deadline(deadline);
                return;
            }
        }
    }

    void scheduleNextInterrupt() {
        if (entries_.empty()) return;

        const uint64_t now     = timer_.getCurrentTick();
        uint64_t       nearest = UINT64_MAX;
        bool           has_immediate = false;

        for (const auto& entry : entries_) {
            switch (entry.wait.type()) {
                case WaitCondition::Type::Immediate:
                    has_immediate = true;
                    break;
                case WaitCondition::Type::Deadline:
                    if (entry.wait.ticks() < nearest)
                        nearest = entry.wait.ticks();
                    break;
                default:
                    break;
            }
        }

        if (has_immediate || nearest <= now) {
            pending_.store(true, std::memory_order_release);
        } else if (nearest < UINT64_MAX) {
            timer_.schedule(nearest - now);
        }
    }

    TimerInterface&                               timer_;
    std::atomic<bool>                             pending_;
    std::array<std::atomic<uint32_t>, kMaskWords> isr_pending_{};
    uint32_t                                      tick_frequency_hz_;
    uint64_t                                      ticks_per_ms_;
    std::vector<ScheduleEntry>                    entries_;
    std::vector<CommEntry>                        comm_entries_;
    std::array<ChipBase*, kRange>                 isr_table_{};
};

} // namespace chipz

#endif // CHIPZ_CORE_HPP
