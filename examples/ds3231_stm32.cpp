/**
 * @file ds3231_stm32.cpp
 * @brief DS3231 RTC example for STM32 with I2C
 */

#include <chipz/chipz.hpp>
#include "stm32f4xx_hal.h" // Replace with your STM32 family

// STM32 HAL I2C handle (configured in main.c or STM32CubeMX)
extern I2C_HandleTypeDef hi2c1;

// System tick (milliseconds)
volatile uint32_t system_tick_ms = 0;

// I2C interface wrapper
chipz::I2CInterface i2c(
    // transmit
    [](const uint8_t* data, size_t length) -> bool {
        return HAL_I2C_Master_Transmit_IT(&hi2c1, 0x68 << 1,
                                          const_cast<uint8_t*>(data), length) == HAL_OK;
    },
    // receive
    [](uint8_t* buffer, size_t length) -> bool {
        return HAL_I2C_Master_Receive_IT(&hi2c1, 0x68 << 1,
                                         buffer, length) == HAL_OK;
    },
    // transmit with memory address
    [](uint8_t mem_addr, const uint8_t* data, size_t length) -> bool {
        return HAL_I2C_Mem_Write_IT(&hi2c1, 0x68 << 1, mem_addr, I2C_MEMADD_SIZE_8BIT,
                                    const_cast<uint8_t*>(data), length) == HAL_OK;
    },
    // receive with memory address
    [](uint8_t mem_addr, uint8_t* buffer, size_t length) -> bool {
        return HAL_I2C_Mem_Read_IT(&hi2c1, 0x68 << 1, mem_addr, I2C_MEMADD_SIZE_8BIT,
                                   buffer, length) == HAL_OK;
    }
);

// DS3231 RTC instance
chipz::devices::DS3231<chipz::I2CInterface> rtc(
    i2c,
    []() -> uint32_t { return system_tick_ms; }
);

void setup() {
    // Initialize DS3231
    if (!rtc.initialize()) {
        // Error: handle initialization failure
        while(1);
    }

    // Set initial time (optional)
    std::tm time = {};
    time.tm_year = 125;  // 2025 - 1900
    time.tm_mon = 0;     // January
    time.tm_mday = 1;
    time.tm_hour = 12;
    time.tm_min = 0;
    time.tm_sec = 0;
    rtc.setTime(time);
}

void loop() {
    // Call main() to run the RTC state machine
    rtc.main();

    // Check if ready and get time
    if (rtc.isReady()) {
        const std::tm* current_time = rtc.getTime();
        // Use current_time...
    }
}

// STM32 HAL I2C complete callbacks
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c == &hi2c1) {
        i2c.onTransferComplete(true);
    }
}

void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c == &hi2c1) {
        i2c.onTransferComplete(true);
    }
}

void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c == &hi2c1) {
        i2c.onTransferComplete(true);
    }
}

void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c == &hi2c1) {
        i2c.onTransferComplete(true);
    }
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c == &hi2c1) {
        i2c.onTransferComplete(false);
    }
}

// SysTick callback (increment system tick)
void HAL_SYSTICK_Callback(void) {
    system_tick_ms++;
}

int main() {
    HAL_Init();
    // Configure clocks, I2C, etc. (usually done by STM32CubeMX generated code)

    setup();

    while (1) {
        loop();
        HAL_Delay(10); // 10ms loop
    }
}
