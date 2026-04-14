// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_CORE_HPP
#define CHIPZ_CORE_HPP

#include "chip.hpp"
#include "communication_interface.hpp"
#include "timer_interface.hpp"
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <vector>

namespace chipz {

/**
 * @brief Central lifecycle and scheduling manager for chipz peripherals
 *
 * Core owns an internal scheduler that drives peripheral execution via
 * one-shot hardware timer interrupts. Peripherals are explicitly registered
 * via add() and scheduled at the tick level — no periodic polling.
 *
 * Execution model:
 *   - Both the timer ISR and comm interface ISRs only set an atomic pending
 *     flag (minimal ISR). No driver code runs in interrupt context.
 *   - service() is called from the main loop (or an RTOS task). It runs in
 *     two passes:
 *       Pass 1 — comm interrupts: all pending comm interface interrupts are
 *                routed to the active peripheral on each bus and cleared.
 *                This always runs before any main() call.
 *       Pass 2 — scheduler: all due peripherals are run in priority order.
 *                Peripherals that become overdue during pass 2 are also
 *                serviced in the same cycle (each runs at most once).
 *   - After both passes the timer is reprogrammed for the next deadline.
 *
 * IRQ dispatch:
 *   - Core is parameterised on the platform IRQn enum type and its numeric
 *     range [kIRQnFirst, kIRQnLast]. These come from the port's irq.hpp.
 *   - Non-comm IRQs (EXTI, timers, DMA, CAN, …) are routed via onIRQ(),
 *     stored in isr_table_, and delivered to chips in service() pass 1.
 *   - Comm IRQs (I2C, SPI, …) reach Core through notify*() calls made by
 *     the port's HAL weak-callback overrides in chipz_isrs.cpp. The static
 *     pending flag (registered via CommunicationInterface::registerCorePending)
 *     ensures service() runs after every comm completion without needing a
 *     per-interface std::function callback.
 *
 * Template parameters:
 *   @tparam IRQnType   Platform IRQn enum (e.g. chipz::port::stm32h5xx::IRQn)
 *   @tparam kIRQnFirst Lowest IRQn value in the enum (typically -15 for ARM)
 *   @tparam kIRQnLast  Highest IRQn value in the enum
 */
template<typename IRQnType, int16_t kIRQnFirst, int16_t kIRQnLast>
class Core {
public:
    static constexpr size_t kRange     = static_cast<size_t>(kIRQnLast - kIRQnFirst + 1);
    static constexpr size_t kMaskWords = (kRange + 31u) / 32u;

    /**
     * @brief Construct Core with a platform timer
     *
     * Registers the pending_ flag with CommunicationInterface so that all
     * notify*() calls wake service() without any per-interface callback.
     *
     * @param timer Platform-specific one-shot timer implementation
     */
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
     * @brief Route a hardware interrupt to the registered chip
     *
     * Call from the platform IRQ handler (defined in the port's chipz_isrs.cpp)
     * for any non-communication interrupt source (EXTI, timers, DMA, CAN, …).
     *
     * Safe to call from interrupt context — only sets atomic flags.
     * Delivery to the chip happens in service() pass 1.
     *
     * @param irqn The IRQn that fired
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
     * @brief Wake service() from an ISR without routing to any chip
     *
     * Use this when an ISR needs to trigger a service() cycle but does not
     * correspond to a registered IRQ source (e.g. a timer used solely as a
     * scheduler tick).
     */
    void wakeFromISR() {
        pending_.store(true, std::memory_order_release);
    }

    /**
     * @brief Register a peripheral with Core
     *
     * Injects all runtime callbacks (defer, claim-bus), registers the
     * peripheral's communication interface for interrupt routing, and
     * populates the IRQ dispatch table from requiredIRQs().
     * Must be called before initialize().
     *
     * @param peripheral Peripheral to register
     */
    void add(ChipBase& peripheral) {
        // Inject defer callbacks
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

        // Register all comm interfaces and inject per-interface claim-bus callbacks
        for (auto* comm : peripheral.getCommInterfaces()) {
            bool already_registered = false;
            for (auto& entry : comm_entries_) {
                if (entry.comm == comm) {
                    already_registered = true;
                    break;
                }
            }

            if (!already_registered) {
                // No setPendingCallback needed: CommunicationInterface::notify*()
                // wakes Core directly via the static pending flag registered in
                // Core's constructor.
                comm_entries_.push_back({comm, nullptr});
            }

            // Inject claim-bus: records this peripheral as active on this bus
            peripheral.setClaimBusCallback(comm, [this, comm, &peripheral]() {
                for (auto& entry : comm_entries_) {
                    if (entry.comm == comm) {
                        entry.active_peripheral = &peripheral;
                        return;
                    }
                }
            });
        }

        // Populate IRQ dispatch table
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
            timer_.getCurrentTick(),
            peripheral.getDefaultPriority(),
            false
        });
    }

    /**
     * @brief Initialize all registered peripherals and arm the timer
     * @return true if all peripherals initialized successfully
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

    /**
     * @brief Override the scheduling priority of a registered peripheral
     * @param peripheral Target peripheral (must already be registered via add())
     * @param priority   New priority (0 = highest, 255 = lowest)
     */
    void setPriority(ChipBase& peripheral, uint8_t priority) {
        for (auto& entry : entries_) {
            if (entry.peripheral == &peripheral) {
                entry.priority = priority;
                return;
            }
        }
    }

    /**
     * @brief Service due peripherals — call from main loop or RTOS task
     *
     * Returns immediately if no interrupt (timer or comm) has fired since
     * the last call. Otherwise:
     *
     *   Pass 1a — routes all pending non-comm IRQs to registered chips.
     *   Pass 1b — routes all pending comm interrupts to the active peripheral
     *             on each bus (always before any main() call in the same cycle).
     *   Pass 2  — runs all due peripherals in priority order, including any
     *             that became overdue while earlier ones were executing.
     *
     * Reprograms the timer before returning.
     */
    void service() {
        if (!pending_.exchange(false, std::memory_order_acquire)) {
            return;
        }

        // Pass 1a: route pending non-comm IRQ events
        for (size_t w = 0; w < kMaskWords; ++w) {
            uint32_t fired = isr_pending_[w].exchange(0, std::memory_order_acquire);
            while (fired) {
                const int bit = std::countr_zero(fired);
                fired &= fired - 1;
                const size_t idx = w * 32u + static_cast<size_t>(bit);
                if (idx < kRange) {
                    if (auto* p = isr_table_[idx]) {
                        p->onIRQ(static_cast<int16_t>(
                            static_cast<int>(idx) + static_cast<int>(kIRQnFirst)
                        ));
                    }
                }
            }
        }

        // Pass 1b: route all pending comm interrupts
        for (auto& comm_entry : comm_entries_) {
            if (comm_entry.comm->hasInterruptPending()) {
                if (comm_entry.active_peripheral) {
                    comm_entry.active_peripheral->onInterrupt(
                        *comm_entry.comm,
                        comm_entry.comm->getPendingInterruptType(),
                        comm_entry.comm->getInterruptSuccess()
                    );
                }
                comm_entry.comm->clearInterrupt();
            }
        }

        // Pass 2: run due peripherals in priority order
        for (auto& entry : entries_) {
            entry.ran_this_cycle = false;
        }

        while (true) {
            uint64_t now = timer_.getCurrentTick();

            ScheduleEntry* best = nullptr;
            for (auto& entry : entries_) {
                if (!entry.ran_this_cycle && entry.next_deadline_ticks <= now) {
                    if (!best || entry.priority < best->priority) {
                        best = &entry;
                    }
                }
            }

            if (!best) break;

            best->ran_this_cycle = true;
            best->peripheral->main();

            // If main() didn't defer, advance deadline past current tick
            uint64_t after = timer_.getCurrentTick();
            if (best->next_deadline_ticks <= after) {
                best->next_deadline_ticks = after + 1;
            }
        }

        scheduleNextInterrupt();
    }

private:
    struct ScheduleEntry {
        ChipBase* peripheral;
        uint64_t  next_deadline_ticks;
        uint8_t   priority;
        bool      ran_this_cycle;
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

    TimerInterface&                                    timer_;
    std::atomic<bool>                                  pending_;
    std::array<std::atomic<uint32_t>, kMaskWords>      isr_pending_{};
    uint32_t                                           tick_frequency_hz_;
    uint64_t                                           ticks_per_ms_;
    std::vector<ScheduleEntry>                         entries_;
    std::vector<CommEntry>                             comm_entries_;
    std::array<ChipBase*, kRange>                      isr_table_{};

    void deferEntry(ChipBase& peripheral, uint64_t ticks) {
        uint64_t deadline = timer_.getCurrentTick() + ticks;
        for (auto& entry : entries_) {
            if (entry.peripheral == &peripheral) {
                entry.next_deadline_ticks = deadline;
                return;
            }
        }
    }

    void scheduleNextInterrupt() {
        if (entries_.empty()) return;

        uint64_t now     = timer_.getCurrentTick();
        uint64_t nearest = UINT64_MAX;

        for (const auto& entry : entries_) {
            if (entry.next_deadline_ticks < nearest) {
                nearest = entry.next_deadline_ticks;
            }
        }

        if (nearest <= now) {
            pending_.store(true, std::memory_order_release);
        } else {
            timer_.schedule(nearest - now);
        }
    }
};

} // namespace chipz

#endif // CHIPZ_CORE_HPP
