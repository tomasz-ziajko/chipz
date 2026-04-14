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
#include <chipz/interfaces/can_interface.hpp>

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
// Both classic CAN (fd_format=false, dlc 0–8) and CAN-FD (fd_format=true,
// dlc 0–15) frames are supported from the same interface instance — the Frame
// struct fields drive the HAL header at transmit time. MaxPayload=64 covers
// the largest CAN-FD payload; classic frames simply use fewer bytes.
//
// The helpers below are defined once and shared by all instances to keep each
// instantiation as concise as the UART / SPI / I2C entries above.
// ---------------------------------------------------------------------------

#ifdef HAL_FDCAN_MODULE_ENABLED

using CANFrame     = chipz::interfaces::CANFrame<64>;
using CANInterface = chipz::interfaces::CANInterface<>;

static int fdcan_tx(FDCAN_HandleTypeDef* h, const CANFrame& frame) {
    FDCAN_TxHeaderTypeDef hdr{};
    hdr.Identifier          = frame.id();
    hdr.IdType              = frame.isExtendedId() ? FDCAN_EXTENDED_ID   : FDCAN_STANDARD_ID;
    hdr.TxFrameType         = FDCAN_DATA_FRAME;
    hdr.DataLength          = static_cast<uint32_t>(frame.dlc()) << 16U;
    hdr.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    hdr.BitRateSwitch       = frame.isBrs()      ? FDCAN_BRS_ON        : FDCAN_BRS_OFF;
    hdr.FDFormat            = frame.isFdFormat() ? FDCAN_FD_CAN        : FDCAN_CLASSIC_CAN;
    hdr.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;
    hdr.MessageMarker       = 0;
    return HAL_FDCAN_AddMessageToTxFifoQ(h, &hdr, frame.data());
}

static int fdcan_rx(FDCAN_HandleTypeDef* h, uint32_t fifo, CANFrame& frame) {
    FDCAN_RxHeaderTypeDef hdr{};
    uint8_t buf[64]{};
    int result = HAL_FDCAN_GetRxMessage(h, fifo, &hdr, buf);
    if (result == HAL_OK) {
        uint8_t dlc    = static_cast<uint8_t>((hdr.DataLength >> 16U) & 0x0FU);
        uint8_t length = CANFrame::dlcToLength(dlc);
        frame.setId(hdr.Identifier, hdr.IdType == FDCAN_EXTENDED_ID);
        frame.setFdMode(hdr.FDFormat == FDCAN_FD_CAN, hdr.BitRateSwitch == FDCAN_BRS_ON);
        frame.setData(buf, length);
    }
    return result;
}

CANInterface g_can1{
    [](const CANFrame& f) -> int { return fdcan_tx(&hfdcan1, f); },
    [](CANFrame& f)       -> int { return fdcan_rx(&hfdcan1, FDCAN_RX_FIFO0, f); }
};

CANInterface g_can2{
    [](const CANFrame& f) -> int { return fdcan_tx(&hfdcan2, f); },
    [](CANFrame& f)       -> int { return fdcan_rx(&hfdcan2, FDCAN_RX_FIFO0, f); }
};

#endif // HAL_FDCAN_MODULE_ENABLED
