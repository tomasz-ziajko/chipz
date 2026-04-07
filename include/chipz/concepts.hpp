#ifndef CHIPZ_CONCEPTS_HPP
#define CHIPZ_CONCEPTS_HPP

#include "communication_interface.hpp"
#include <concepts>
#include <cstddef>
#include <cstdint>

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

} // namespace chipz::concepts

#endif // CHIPZ_CONCEPTS_HPP
