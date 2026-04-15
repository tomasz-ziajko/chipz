// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_CONCEPTS_HPP
#define CHIPZ_CONCEPTS_HPP

#include "communication_interface.hpp"
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace chipz::concepts {

template<typename T>
concept CommunicationInterface = requires(T t, const uint8_t* data, uint8_t* buf, size_t len) {
    { t.transmit(data, len) }              -> std::same_as<bool>;
    { t.receive(buf, len) }                -> std::same_as<bool>;
    { t.isReady() }                        -> std::same_as<bool>;
    { t.getRxBuffer() }                    -> std::same_as<uint8_t*>;
    { t.getTxBuffer() }                    -> std::same_as<uint8_t*>;
    { t.hasInterruptPending() }            -> std::same_as<bool>;
    { t.getPendingInterruptType() }        -> std::same_as<chipz::CommunicationInterface::InterruptType>;
    { t.getInterruptSuccess() }            -> std::same_as<bool>;
    { t.clearInterrupt() };
};

/**
 * @brief Concept satisfied by any type usable as a CompletionSource
 *
 * Built-in implementations: CompletionSource::Timer, CompletionSource::External
 */
template<typename T>
concept CompletionSource = requires(T t, uint32_t d, std::function<void()> cb) {
    t.arm(d, std::move(cb));
    t.cancel();
};

} // namespace chipz::concepts

#endif // CHIPZ_CONCEPTS_HPP
