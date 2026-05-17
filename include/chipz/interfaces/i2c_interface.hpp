// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_INTERFACES_I2C_INTERFACE_HPP
#define CHIPZ_INTERFACES_I2C_INTERFACE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "../core/communication_interface.hpp"

namespace chipz {
namespace interfaces {

/**
 * @brief I2C Communication Interface implementation
 *
 * N is the transfer buffer size in bytes — set it to the kMaxTransfer of the
 * largest device on the bus. Buffers live in the object with no heap allocation.
 *
 * ReadFn / WriteFn are the types of the I2C read/write callables. Typically
 * captureless lambdas or function pointers; use partial CTAD to deduce them:
 *   I2CInterface<N>(read_lambda, write_lambda)
 */
template <size_t N, typename ReadFn, typename WriteFn>
class I2CInterface : public CommunicationInterface {
    public:
    I2CInterface(ReadFn read_fn, WriteFn write_fn) :
        CommunicationInterface(),
        device_address_(0),
        i2c_read_(std::move(read_fn)),
        i2c_write_(std::move(write_fn)),
        current_mem_address_(0)
    {
    }

    void setDeviceAddress(uint8_t address)
    {
        device_address_ = address;
    }

    ConnectionId registerConnection(uint8_t device_address)
    {
        ConnectionId id = nextId();
        connections_.push_back(device_address);
        return id;
    }

    void selectConnection(ConnectionId id) override
    {
        device_address_ = connections_[id];
    }

    uint8_t* getTxBuffer() override
    {
        return tx_buffer_.data();
    }
    uint8_t* getRxBuffer() override
    {
        return rx_buffer_.data();
    }

    bool transmit(const uint8_t* data, size_t length) override
    {
        if (transfer_in_progress_) {
            return false;
        }

        if (data != tx_buffer_.data()) {
            for (size_t i = 0; i < length; ++i) {
                tx_buffer_[i] = data[i];
            }
        }

        transfer_in_progress_ = true;

        int result =
            i2c_write_(device_address_, current_mem_address_, tx_buffer_.data(), static_cast<uint16_t>(length));

        if (result != 0) {
            notifyError();
            return false;
        }

        return true;
    }

    bool receive(uint8_t* /*buffer*/, size_t length) override
    {
        if (transfer_in_progress_) {
            return false;
        }

        transfer_in_progress_ = true;

        int result = i2c_read_(device_address_, current_mem_address_, rx_buffer_.data(), static_cast<uint16_t>(length));

        if (result != 0) {
            notifyError();
            return false;
        }

        return true;
    }

    void setMemoryAddress(uint8_t mem_address)
    {
        current_mem_address_ = mem_address;
    }

    uint8_t getMemoryAddress() const
    {
        return current_mem_address_;
    }

    uint8_t getDeviceAddress() const
    {
        return device_address_;
    }

    private:
    uint8_t                device_address_;
    ReadFn                 i2c_read_;
    WriteFn                i2c_write_;
    uint8_t                current_mem_address_;
    std::vector<uint8_t>   connections_;
    std::array<uint8_t, N> tx_buffer_{};
    std::array<uint8_t, N> rx_buffer_{};
};

}  // namespace interfaces
}  // namespace chipz

#endif  // CHIPZ_INTERFACES_I2C_INTERFACE_HPP
