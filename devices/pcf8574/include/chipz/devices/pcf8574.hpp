// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_DEVICES_PCF8574_HPP
#define CHIPZ_DEVICES_PCF8574_HPP

#include <chipz/core/chip.hpp>
#include <chipz/core/communication_interface.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace chipz {
namespace devices {

/**
 * @brief PCF8574 GPIO expander output as a CommunicationInterface
 *
 * Implements a single-byte parallel write bus backed by HAL_I2C_Master_Transmit_IT.
 * PCF8574 has no register addresses — each I2C transaction is device_addr + one data byte.
 *
 * Standard HD44780 pin mapping (PCF8574 output → LCD signal):
 *   P0=RS  P1=RW  P2=E  P3=BL  P4=D4  P5=D5  P6=D6  P7=D7
 *
 * WriteFn signature: (const uint8_t* data, uint16_t len) -> int
 *   Returns 0 (HAL_OK) on success, non-zero on error.
 *
 * ISR routing: point g_i2cN_iface at this object so that
 * HAL_I2C_MasterTxCpltCallback routes to notifyTransferComplete().
 *
 * @tparam N        Buffer size in bytes (1 for single-byte PCF8574 writes)
 * @tparam WriteFn  Callable type for the I2C write function
 */
template <size_t N, typename WriteFn>
class PCF8574GpioInterface : public CommunicationInterface {
    static_assert(N >= 1, "PCF8574GpioInterface buffer size must be at least 1");

    public:
    static constexpr size_t kBufferSize = N;

    explicit PCF8574GpioInterface(WriteFn write_fn) : write_fn_(write_fn) {}

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

/**
 * @brief PCF8574 I2C GPIO expander — Chip wrapper
 *
 * Models the PCF8574 as a first-class chipz Chip registered with Core.
 * It owns a PCF8574GpioInterface that HD44780 (or any other driver) can use
 * as its parallel bus by calling getParallelInterface().
 *
 * Usage:
 * @code
 *   auto write = [](const uint8_t* d, uint16_t n) { return HAL_I2C_Master_Transmit_IT(&hi2c1, addr, d, n); };
 *   PCF8574<1, decltype(write)> g_pcf8574{write};
 *   g_core.add(g_pcf8574);
 *   HD44780 g_hd44780{g_pcf8574.getParallelInterface(), ...};
 *   g_i2c1_iface = &g_pcf8574.getParallelInterface();  // ISR routing
 * @endcode
 *
 * @tparam N        I2C buffer size in bytes (1 for PCF8574)
 * @tparam WriteFn  Callable type for the underlying I2C write function
 */
template <size_t N, typename WriteFn>
class PCF8574 : public ChipBase {
    public:
    explicit PCF8574(WriteFn write_fn) :
        gpio_iface_(write_fn),
        comm_ifaces_{&gpio_iface_}
    {
    }

    PCF8574GpioInterface<N, WriteFn>& getParallelInterface()
    {
        return gpio_iface_;
    }

    // -------------------------------------------------------------------------
    // ChipBase interface
    // -------------------------------------------------------------------------

    bool initialize() override
    {
        return true;
    }

    bool reset() override
    {
        return true;
    }

    bool isReady() const override
    {
        return true;
    }

    ChipBase::Status getStatus() const override
    {
        return ChipBase::Status::Ready;
    }

    std::string getDeviceId() const override
    {
        return "PCF8574";
    }

    DriverTask run() override
    {
        while (true) {
            co_yield WaitCondition::demand();
        }
    }

    std::span<CommunicationInterface*> getCommInterfaces() override
    {
        return {comm_ifaces_, 1};
    }

    private:
    PCF8574GpioInterface<N, WriteFn> gpio_iface_;
    CommunicationInterface*          comm_ifaces_[1];
};

}  // namespace devices
}  // namespace chipz

#endif  // CHIPZ_DEVICES_PCF8574_HPP
