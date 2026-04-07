#ifndef CHIPZ_CONCEPTS_HPP
#define CHIPZ_CONCEPTS_HPP

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace chipz::concepts {

template<typename T>
concept CommunicationInterface = requires(T t,
                                          const uint8_t* data,
                                          uint8_t* buf,
                                          size_t len,
                                          std::function<void(bool)> cb) {
    { t.transmit(data, len) } -> std::same_as<bool>;
    { t.receive(buf, len) }   -> std::same_as<bool>;
    { t.isReady() }           -> std::same_as<bool>;
    { t.getRxBuffer() }       -> std::same_as<uint8_t*>;
    { t.getTxBuffer() }       -> std::same_as<uint8_t*>;
    { t.setTransferCompleteCallback(cb) };
};

} // namespace chipz::concepts

#endif // CHIPZ_CONCEPTS_HPP
