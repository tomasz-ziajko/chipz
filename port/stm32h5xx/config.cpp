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

#include "stm32h5xx_hal.h"

// ---------------------------------------------------------------------------
// HAL handles — defined by CubeMX / your HAL init; extern-declared here
// ---------------------------------------------------------------------------

extern "C" {
__attribute__((weak)) extern I2C_HandleTypeDef hi2c1;
__attribute__((weak)) extern I2C_HandleTypeDef hi2c2;
__attribute__((weak)) extern I2C_HandleTypeDef hi2c3;
__attribute__((weak)) extern I2C_HandleTypeDef hi2c4;

__attribute__((weak)) extern SPI_HandleTypeDef hspi1;
__attribute__((weak)) extern SPI_HandleTypeDef hspi2;
__attribute__((weak)) extern SPI_HandleTypeDef hspi3;
}

// ---------------------------------------------------------------------------
// I2C bus interfaces
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// SPI bus interfaces (no chip select — see file header)
// ---------------------------------------------------------------------------

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
