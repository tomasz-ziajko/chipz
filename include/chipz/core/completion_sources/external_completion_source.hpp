// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_COMPLETION_SOURCES_EXTERNAL_COMPLETION_SOURCE_HPP
#define CHIPZ_COMPLETION_SOURCES_EXTERNAL_COMPLETION_SOURCE_HPP

#include <functional>

namespace chipz {
namespace CompletionSource {

/**
 * @brief Completion source driven by an external event
 *
 * Fires when an external signal arrives — typically the transfer-complete
 * interrupt of an underlying bus (e.g. an I2C GPIO expander whose write
 * transaction must finish before the parallel bus is considered done).
 *
 * At construction, supply a registration function that accepts a one-shot
 * callback and arranges to call it when the event fires:
 *
 * @code
 *   ParallelInterface<6, CompletionSource::External> bus(
 *       writeFn, readFn,
 *       CompletionSource::External([&i2c](std::function<void()> cb) {
 *           i2c.onNextTransferComplete(cb);
 *       }));
 * @endcode
 *
 * The duration_us hint passed to arm() is ignored — external sources fire
 * on their own schedule. cancel() is a no-op: once an external event is in
 * flight it cannot be recalled.
 */
class External {
public:
    using RegisterFn = std::function<void(std::function<void()>)>;

    explicit External(RegisterFn register_fn)
        : register_fn_(std::move(register_fn)) {}

    void arm(uint32_t /*duration_us*/, std::function<void()> on_complete) {
        register_fn_(std::move(on_complete));
    }

    void cancel() {}  // no-op: external events cannot be recalled

private:
    RegisterFn register_fn_;
};

} // namespace CompletionSource
} // namespace chipz

#endif // CHIPZ_COMPLETION_SOURCES_EXTERNAL_COMPLETION_SOURCE_HPP
