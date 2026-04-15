// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

/**
 * @file config.cpp
 * @brief Chipz communication interface instances for STM32H5xx
 *
 * Defines one chipz interface object per physical bus available on the
 * STM32H5xx family. Each object wraps the matching STM32 HAL IT functions
 * so that transfers are interrupt-driven and non-blocking.
 *
 * Usage
 * -----
 * In your project, extern the interfaces you use:
 *
 *   extern chipz::interfaces::I2CInterface g_i2c1;
 *   extern chipz::interfaces::SPIInterface g_spi1;
 *
 * Pass them to your peripheral constructors:
 *
 *   chipz::devices::DS3231  g_ds3231 { g_i2c1 };
 *   chipz::devices::MAX6675 g_max6675{ g_spi1 };
 *
 * HAL handles
 * -----------
 * The handles below (hi2c1, hspi1 …) must be defined and initialized by your
 * project's HAL initialization code (typically CubeMX-generated). This file
 * only extern-declares them — it does not own or initialize them.
 *
 * I2C addressing
 * --------------
 * chipz stores 7-bit device addresses. The lambdas below shift them left by
 * one bit as required by the STM32 HAL (which expects an 8-bit address with
 * the R/W bit as bit 0). Drivers set their address via
 * I2CInterface::setDeviceAddress() with the 7-bit value.
 *
 * Chip select (SPI)
 * -----------------
 * The SPI interfaces defined here do not manage chip select — CS is a GPIO
 * concern that belongs to the individual driver or the application. Each SPI
 * peripheral driver is responsible for asserting and deasserting its own CS
 * pin around calls to transmit() / receive().
 *
 * Instances available on STM32H533RE
 * ------------------------------------
 * Verify against your device's datasheet / reference manual (RM0481) before
 * enabling instances that may not be present on your specific package.
 */

#include <chipz/interfaces/i2c_interface.hpp>
#include <chipz/interfaces/spi_interface.hpp>
#include <chipz/interfaces/uart_interface.hpp>
#include <chipz/network/can/can_interface.hpp>

#include "stm32h5xx_hal.h"

// ---------------------------------------------------------------------------
// HAL handles — defined by CubeMX / your HAL init; extern-declared here
// ---------------------------------------------------------------------------

extern "C" {
#ifdef HAL_I2C_MODULE_ENABLED
__attribute__((weak)) extern I2C_HandleTypeDef hi2c1;
__attribute__((weak)) extern I2C_HandleTypeDef hi2c2;
__attribute__((weak)) extern I2C_HandleTypeDef hi2c3;
__attribute__((weak)) extern I2C_HandleTypeDef hi2c4;
#endif

#ifdef HAL_SPI_MODULE_ENABLED
__attribute__((weak)) extern SPI_HandleTypeDef hspi1;
__attribute__((weak)) extern SPI_HandleTypeDef hspi2;
__attribute__((weak)) extern SPI_HandleTypeDef hspi3;
__attribute__((weak)) extern SPI_HandleTypeDef hspi4;
#endif

#ifdef HAL_UART_MODULE_ENABLED
__attribute__((weak)) extern UART_HandleTypeDef huart1;
__attribute__((weak)) extern UART_HandleTypeDef huart2;
__attribute__((weak)) extern UART_HandleTypeDef huart3;
__attribute__((weak)) extern UART_HandleTypeDef huart4;
#endif

#ifdef HAL_FDCAN_MODULE_ENABLED
__attribute__((weak)) extern FDCAN_HandleTypeDef hfdcan1;
__attribute__((weak)) extern FDCAN_HandleTypeDef hfdcan2;
#endif

}

// ---------------------------------------------------------------------------
// I2C bus interfaces
// ---------------------------------------------------------------------------

#ifdef HAL_I2C_MODULE_ENABLED

chipz::interfaces::I2CInterface g_i2c1{
    [](uint8_t dev, uint8_t reg, uint8_t* buf, uint16_t len) -> int {
        return HAL_I2C_Mem_Read_IT(&hi2c1,
                                   static_cast<uint16_t>(dev) << 1,
                                   reg, I2C_MEMADD_SIZE_8BIT, buf, len);
    },
    [](uint8_t dev, uint8_t reg, const uint8_t* buf, uint16_t len) -> int {
        return HAL_I2C_Mem_Write_IT(&hi2c1,
                                    static_cast<uint16_t>(dev) << 1,
                                    reg, I2C_MEMADD_SIZE_8BIT,
                                    const_cast<uint8_t*>(buf), len);
    }
};

chipz::interfaces::I2CInterface g_i2c2{
    [](uint8_t dev, uint8_t reg, uint8_t* buf, uint16_t len) -> int {
        return HAL_I2C_Mem_Read_IT(&hi2c2,
                                   static_cast<uint16_t>(dev) << 1,
                                   reg, I2C_MEMADD_SIZE_8BIT, buf, len);
    },
    [](uint8_t dev, uint8_t reg, const uint8_t* buf, uint16_t len) -> int {
        return HAL_I2C_Mem_Write_IT(&hi2c2,
                                    static_cast<uint16_t>(dev) << 1,
                                    reg, I2C_MEMADD_SIZE_8BIT,
                                    const_cast<uint8_t*>(buf), len);
    }
};

chipz::interfaces::I2CInterface g_i2c3{
    [](uint8_t dev, uint8_t reg, uint8_t* buf, uint16_t len) -> int {
        return HAL_I2C_Mem_Read_IT(&hi2c3,
                                   static_cast<uint16_t>(dev) << 1,
                                   reg, I2C_MEMADD_SIZE_8BIT, buf, len);
    },
    [](uint8_t dev, uint8_t reg, const uint8_t* buf, uint16_t len) -> int {
        return HAL_I2C_Mem_Write_IT(&hi2c3,
                                    static_cast<uint16_t>(dev) << 1,
                                    reg, I2C_MEMADD_SIZE_8BIT,
                                    const_cast<uint8_t*>(buf), len);
    }
};

chipz::interfaces::I2CInterface g_i2c4{
    [](uint8_t dev, uint8_t reg, uint8_t* buf, uint16_t len) -> int {
        return HAL_I2C_Mem_Read_IT(&hi2c4,
                                   static_cast<uint16_t>(dev) << 1,
                                   reg, I2C_MEMADD_SIZE_8BIT, buf, len);
    },
    [](uint8_t dev, uint8_t reg, const uint8_t* buf, uint16_t len) -> int {
        return HAL_I2C_Mem_Write_IT(&hi2c4,
                                    static_cast<uint16_t>(dev) << 1,
                                    reg, I2C_MEMADD_SIZE_8BIT,
                                    const_cast<uint8_t*>(buf), len);
    }
};

#endif // HAL_I2C_MODULE_ENABLED

// ---------------------------------------------------------------------------
// SPI bus interfaces (no chip select — see file header)
// ---------------------------------------------------------------------------

#ifdef HAL_SPI_MODULE_ENABLED

chipz::interfaces::SPIInterface g_spi1{
    [](uint8_t* tx, uint8_t* rx, uint16_t len) -> int {
        return HAL_SPI_TransmitReceive_IT(&hspi1, tx, rx, len);
    }
};

chipz::interfaces::SPIInterface g_spi2{
    [](uint8_t* tx, uint8_t* rx, uint16_t len) -> int {
        return HAL_SPI_TransmitReceive_IT(&hspi2, tx, rx, len);
    }
};

chipz::interfaces::SPIInterface g_spi3{
    [](uint8_t* tx, uint8_t* rx, uint16_t len) -> int {
        return HAL_SPI_TransmitReceive_IT(&hspi3, tx, rx, len);
    }
};

chipz::interfaces::SPIInterface g_spi4{
    [](uint8_t* tx, uint8_t* rx, uint16_t len) -> int {
        return HAL_SPI_TransmitReceive_IT(&hspi4, tx, rx, len);
    }
};

#endif // HAL_SPI_MODULE_ENABLED

// ---------------------------------------------------------------------------
// UART interfaces
// ---------------------------------------------------------------------------

#ifdef HAL_UART_MODULE_ENABLED

chipz::interfaces::UARTInterface g_uart1{
    [](const uint8_t* data, uint16_t len) -> int {
        return HAL_UART_Transmit_IT(&huart1, data, len);
    },
    [](uint8_t* buf, uint16_t len) -> int {
        return HAL_UART_Receive_IT(&huart1, buf, len);
    }
};

chipz::interfaces::UARTInterface g_uart2{
    [](const uint8_t* data, uint16_t len) -> int {
        return HAL_UART_Transmit_IT(&huart2, data, len);
    },
    [](uint8_t* buf, uint16_t len) -> int {
        return HAL_UART_Receive_IT(&huart2, buf, len);
    }
};

chipz::interfaces::UARTInterface g_uart3{
    [](const uint8_t* data, uint16_t len) -> int {
        return HAL_UART_Transmit_IT(&huart3, data, len);
    },
    [](uint8_t* buf, uint16_t len) -> int {
        return HAL_UART_Receive_IT(&huart3, buf, len);
    }
};

chipz::interfaces::UARTInterface g_uart4{
    [](const uint8_t* data, uint16_t len) -> int {
        return HAL_UART_Transmit_IT(&huart4, data, len);
    },
    [](uint8_t* buf, uint16_t len) -> int {
        return HAL_UART_Receive_IT(&huart4, buf, len);
    }
};

#endif // HAL_UART_MODULE_ENABLED

// ---------------------------------------------------------------------------
// FDCAN interfaces
//
// Define your application's RX message types below, then add them as
// template arguments to AppCANInterface1 / AppCANInterface2.
//
// Example:
//   using EngineSpeed = chipz::network::CANMessage<0x100, 2>;
//   using ThrottlePos = chipz::network::CANMessage<0x101, 1>;
//   using AppCANInterface1 = chipz::network::CANInterface<3, EngineSpeed, ThrottlePos>;
//
// TX messages do not need to be registered — call g_can1.transmit(msg)
// with any CANMessage type directly.
//
// TxFifoDepth=3 matches the STM32H5xx FDCAN TX FIFO depth. Adjust if your
// device or configuration differs.
// ---------------------------------------------------------------------------

#ifdef HAL_FDCAN_MODULE_ENABLED

static int fdcan_tx(FDCAN_HandleTypeDef* h, uint32_t id, bool extended_id,
                    bool fd_format, bool brs, const uint8_t* data, uint8_t length) {
    FDCAN_TxHeaderTypeDef hdr{};
    hdr.Identifier          = id;
    hdr.IdType              = extended_id ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    hdr.TxFrameType         = FDCAN_DATA_FRAME;
    hdr.DataLength          = static_cast<uint32_t>(chipz::network::lengthToDlc(length)) << 16U;
    hdr.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    hdr.BitRateSwitch       = brs       ? FDCAN_BRS_ON  : FDCAN_BRS_OFF;
    hdr.FDFormat            = fd_format ? FDCAN_FD_CAN  : FDCAN_CLASSIC_CAN;
    hdr.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;
    hdr.MessageMarker       = 0;
    return HAL_FDCAN_AddMessageToTxFifoQ(h, &hdr, const_cast<uint8_t*>(data));
}

// Add your RX message types to the template argument list below
using AppCANInterface1 = chipz::network::CANInterface<3>;
using AppCANInterface2 = chipz::network::CANInterface<3>;

static AppCANInterface1 s_can1{
    [](uint32_t id, bool ext, bool fd, bool brs, const uint8_t* data, uint8_t len) -> int {
        return fdcan_tx(&hfdcan1, id, ext, fd, brs, data, len);
    }
};

static AppCANInterface2 s_can2{
    [](uint32_t id, bool ext, bool fd, bool brs, const uint8_t* data, uint8_t len) -> int {
        return fdcan_tx(&hfdcan2, id, ext, fd, brs, data, len);
    }
};

chipz::network::CANInterfaceBase* g_can1 = &s_can1;
chipz::network::CANInterfaceBase* g_can2 = &s_can2;

#endif // HAL_FDCAN_MODULE_ENABLED

