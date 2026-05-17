// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_DEVICES_PCF8574_HPP
#define CHIPZ_DEVICES_PCF8574_HPP

#include <chipz/core/communication_interface.hpp>
#include <algorithm>
#include <array>
#include <cstdint>

namespace chipz {
namespace devices {

/**
 * @brief PCF8574 I2C GPIO expander as a CommunicationInterface
 *
 * Wraps a single-byte I2C Master_Transmit write as a chipz CommunicationInterface
 * so that HD44780<PCF8574Interface<...>> can use it directly as its parallel bus.
 *
 * PCF8574 has no register addresses — each transaction is just device_addr + data_byte.
 * Use HAL_I2C_Master_Transmit_IT (not Mem_Write_IT) in the write callable.
 *
 * Standard HD44780 pin mapping:
 *   P0=RS  P1=RW  P2=E  P3=BL  P4=D4  P5=D5  P6=D6  P7=D7
 *
 * WriteFn signature: (const uint8_t* data, uint16_t len) -> int
 *   Returns 0 (HAL_OK) on success, non-zero on error.
 *
 * ISR routing: assign g_i2cN_iface = &your_instance in app.cpp so that
 * HAL_I2C_MasterTxCpltCallback routes to notifyTransferComplete().
 *
 * @tparam N        Buffer size in bytes (1 for single-byte PCF8574 writes)
 * @tparam WriteFn  Callable type for the I2C write function
 */
template <size_t N, typename WriteFn>
class PCF8574Interface : public CommunicationInterface {
    static_assert(N >= 1, "PCF8574Interface buffer size must be at least 1");

    public:
    static constexpr size_t kBufferSize = N;

    explicit PCF8574Interface(WriteFn write_fn) : write_fn_(write_fn) {}

    uint8_t* getTxBuffer() override
    {
        return tx_buf_.data();
    }

    uint8_t* getRxBuffer() override
    {
        return rx_buf_.data();
    }

    bool transmit(const uint8_t* data, size_t length) override
    {
        if (transfer_in_progress_) {
            return false;
        }
        size_t n = std::min(length, N);
        std::copy(data, data + n, tx_buf_.data());
        transfer_in_progress_ = true;
        if (write_fn_(tx_buf_.data(), static_cast<uint16_t>(n)) != 0) {
            transfer_in_progress_ = false;
            return false;
        }
        return true;
    }

    bool receive(uint8_t*, size_t) override
    {
        return false;
    }

    private:
    WriteFn                write_fn_;
    std::array<uint8_t, N> tx_buf_{};
    std::array<uint8_t, N> rx_buf_{};
};

}  // namespace devices
}  // namespace chipz

#endif  // CHIPZ_DEVICES_PCF8574_HPP
