/**
 * @file max6675_stm32.cpp
 * @brief MAX6675 Thermocouple example for STM32 with SPI
 */

#include <chipz/chipz.hpp>
#include "stm32f4xx_hal.h" // Replace with your STM32 family

// STM32 HAL SPI handle (configured in main.c or STM32CubeMX)
extern SPI_HandleTypeDef hspi1;

// System tick (milliseconds)
volatile uint32_t system_tick_ms = 0;

// Chip select GPIO
#define MAX6675_CS_PIN GPIO_PIN_4
#define MAX6675_CS_PORT GPIOA

// SPI interface wrapper
chipz::SPIInterface spi(
    // transmit function
    [](const uint8_t* data, size_t length) -> bool {
        HAL_GPIO_WritePin(MAX6675_CS_PORT, MAX6675_CS_PIN, GPIO_PIN_RESET);
        bool result = HAL_SPI_Transmit_IT(&hspi1, const_cast<uint8_t*>(data), length) == HAL_OK;
        return result;
    },
    // receive function
    [](uint8_t* buffer, size_t length) -> bool {
        HAL_GPIO_WritePin(MAX6675_CS_PORT, MAX6675_CS_PIN, GPIO_PIN_RESET);
        bool result = HAL_SPI_Receive_IT(&hspi1, buffer, length) == HAL_OK;
        return result;
    },
    // chip select function
    [](bool select) {
        HAL_GPIO_WritePin(MAX6675_CS_PORT, MAX6675_CS_PIN,
                         select ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }
);

// MAX6675 thermocouple instance
chipz::devices::MAX6675<chipz::SPIInterface> thermocouple(
    spi,
    []() -> uint32_t { return system_tick_ms; }
);

void setup() {
    // Initialize MAX6675
    if (!thermocouple.initialize()) {
        // Error: handle initialization failure
        while(1);
    }
}

void loop() {
    // Call main() to run the thermocouple state machine
    thermocouple.main();

    // Check if ready and read temperature
    if (thermocouple.isReady()) {
        float temp_c = thermocouple.getTemperatureCelsius();
        float temp_f = thermocouple.getTemperatureFahrenheit();

        // Check if thermocouple is connected
        if (thermocouple.isThermocoupleConnected()) {
            // Temperature is valid: temp_c, temp_f
        }
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

// SysTick callback (increment system tick)
void HAL_SYSTICK_Callback(void) {
    system_tick_ms++;
}

int main() {
    HAL_Init();
    // Configure clocks, SPI, GPIO, etc.

    setup();

    while (1) {
        loop();
        HAL_Delay(10); // 10ms loop
    }
}
