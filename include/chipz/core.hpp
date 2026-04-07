#ifndef CHIPZ_CORE_HPP
#define CHIPZ_CORE_HPP

#include "peripheral.hpp"
#include "timer_interface.hpp"
#include <atomic>
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
 * Deferral:
 *   - Each registered peripheral receives defer_ms_ and defer_us_ callbacks.
 *   - Calling defer_ms_(N) or defer_us_(N) from within main() tells the
 *     scheduler not to call that peripheral again for N ms / µs.
 *   - deferUs degrades gracefully to the nearest representable tick on
 *     timers with coarser than 1 MHz resolution.
 *
 * Priority:
 *   - Lower number = higher priority (0 = highest, 255 = lowest).
 *   - Default is sourced from PeripheralBase::getDefaultPriority() (128).
 *   - Can be overridden at runtime via setPriority().
 *
 * Shared bus routing:
 *   - Multiple peripherals may share the same CommunicationInterface instance.
 *   - Before each transfer, the peripheral's transmit()/receive() wrapper calls
 *     claim_bus_fn_() which updates the active-peripheral record for that bus.
 *   - When a comm ISR fires, Core routes it to whichever peripheral last
 *     claimed the bus.
 */
class Core {
public:
    /**
     * @brief Construct Core with a platform timer
     * @param timer Platform-specific one-shot timer implementation
     */
    explicit Core(TimerInterface& timer)
        : timer_(timer)
        , pending_(false)
        , tick_frequency_hz_(timer.getTickFrequencyHz())
        , ticks_per_ms_(static_cast<uint64_t>(tick_frequency_hz_) / 1000u)
    {
        timer_.setElapsedCallback([this]() {
            pending_.store(true, std::memory_order_release);
        });
    }

    /**
     * @brief Register a peripheral with Core
     *
     * Injects all runtime callbacks (defer, claim-bus) and registers the
     * peripheral's communication interface for interrupt routing.
     * Must be called before initialize().
     *
     * @param peripheral Peripheral to register
     */
    void add(PeripheralBase& peripheral) {
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

        // Register comm interface and inject claim-bus callback
        CommunicationInterface* comm = peripheral.getCommInterface();
        if (comm) {
            // Register the comm interface if this is its first peripheral
            bool already_registered = false;
            for (auto& entry : comm_entries_) {
                if (entry.comm == comm) {
                    already_registered = true;
                    break;
                }
            }

            if (!already_registered) {
                comm->setPendingCallback([this]() {
                    pending_.store(true, std::memory_order_release);
                });
                comm_entries_.push_back({comm, nullptr});
            }

            // Inject claim-bus: records this peripheral as active on the bus
            peripheral.setClaimBusCallback([this, comm, &peripheral]() {
                for (auto& entry : comm_entries_) {
                    if (entry.comm == comm) {
                        entry.active_peripheral = &peripheral;
                        return;
                    }
                }
            });
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
    void setPriority(PeripheralBase& peripheral, uint8_t priority) {
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
     *   Pass 1 — routes all pending comm interrupts to the correct peripheral
     *            (always before any main() call in the same cycle).
     *   Pass 2 — runs all due peripherals in priority order, including any
     *            that became overdue while earlier ones were executing.
     *
     * Reprograms the timer before returning.
     */
    void service() {
        if (!pending_.exchange(false, std::memory_order_acquire)) {
            return;
        }

        // Pass 1: route all pending comm interrupts
        for (auto& comm_entry : comm_entries_) {
            if (comm_entry.comm->hasInterruptPending()) {
                if (comm_entry.active_peripheral) {
                    comm_entry.active_peripheral->onInterrupt(
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
        PeripheralBase* peripheral;
        uint64_t        next_deadline_ticks;
        uint8_t         priority;
        bool            ran_this_cycle;
    };

    struct CommEntry {
        CommunicationInterface* comm;
        PeripheralBase*         active_peripheral;
    };

    TimerInterface&            timer_;
    std::atomic<bool>          pending_;
    uint32_t                   tick_frequency_hz_;
    uint64_t                   ticks_per_ms_;
    std::vector<ScheduleEntry> entries_;
    std::vector<CommEntry>     comm_entries_;

    void deferEntry(PeripheralBase& peripheral, uint64_t ticks) {
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
