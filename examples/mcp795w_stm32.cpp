/**
 * @file mcp795w_stm32.cpp
 * @brief MCP795W RTC example for STM32 with SPI
 */

#include <chipz/chipz.hpp>
#include "stm32f4xx_hal.h" // Replace with your STM32 family

// STM32 HAL SPI handle (configured in main.c or STM32CubeMX)
extern SPI_HandleTypeDef hspi1;

// Chip select GPIO
#define RTC_CS_PIN GPIO_PIN_4
#define RTC_CS_PORT GPIOA

// SPI interface wrapper
chipz::SPIInterface spi(
    // transmit function
    [](const uint8_t* data, size_t length) -> bool {
        HAL_GPIO_WritePin(RTC_CS_PORT, RTC_CS_PIN, GPIO_PIN_RESET);
        bool result = HAL_SPI_Transmit_IT(&hspi1, const_cast<uint8_t*>(data), length) == HAL_OK;
        return result;
    },
    // receive function
    [](uint8_t* buffer, size_t length) -> bool {
        HAL_GPIO_WritePin(RTC_CS_PORT, RTC_CS_PIN, GPIO_PIN_RESET);
        bool result = HAL_SPI_Receive_IT(&hspi1, buffer, length) == HAL_OK;
        return result;
    },
    // chip select function
    [](bool select) {
        HAL_GPIO_WritePin(RTC_CS_PORT, RTC_CS_PIN,
                         select ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }
);

// MCP795W RTC instance
chipz::devices::MCP795W<chipz::SPIInterface> rtc(
    spi,
    nullptr // Optional: SPI transmission disabled check function
);

void setup() {
    // Initialize MCP795W
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

    // Set alarm (optional)
    std::tm alarm_time = {};
    alarm_time.tm_hour = 6;
    alarm_time.tm_min = 30;
    alarm_time.tm_sec = 0;
    rtc.setAlarm(alarm_time);
}

void loop() {
    // Call main() periodically (expects 10ms interval)
    rtc.main();

    // Check if ready and get time
    if (rtc.isReady()) {
        const std::tm* current_time = rtc.getTime();
        // Use current_time...

        // Get formatted time string
        // std::string time_str = rtc.getTimeString();

        // Check shutdown ready (for power management)
        // if (rtc.getShutdownReady()) {
        //     // Safe to shut down
        // }
    }
}

// STM32 HAL SPI complete callbacks
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi == &hspi1) {
        spi.onTransferComplete(true);
    }
}

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi == &hspi1) {
        spi.onTransferComplete(true);
    }
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi == &hspi1) {
        spi.onTransferComplete(true);
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi) {
    if (hspi == &hspi1) {
        spi.onTransferComplete(false);
    }
}

int main() {
    HAL_Init();
    // Configure clocks, SPI, GPIO, etc.

    setup();

    while (1) {
        loop();
        HAL_Delay(10); // 10ms loop (important for MCP795W timing)
    }
}
