/**
 * @file tja1145_stm32.cpp
 * @brief TJA1145 CAN Transceiver example for STM32 with SPI
 */

#include <chipz/chipz.hpp>
#include "stm32f4xx_hal.h" // Replace with your STM32 family

// STM32 HAL SPI handle (configured in main.c or STM32CubeMX)
extern SPI_HandleTypeDef hspi1;

// Chip select GPIO
#define TJA_CS_PIN GPIO_PIN_4
#define TJA_CS_PORT GPIOA

// SPI interface wrapper
chipz::SPIInterface spi(
    // transmit function
    [](const uint8_t* data, size_t length) -> bool {
        HAL_GPIO_WritePin(TJA_CS_PORT, TJA_CS_PIN, GPIO_PIN_RESET);
        bool result = HAL_SPI_TransmitReceive_IT(&hspi1,
                                                   const_cast<uint8_t*>(data),
                                                   const_cast<uint8_t*>(data),
                                                   length) == HAL_OK;
        return result;
    },
    // receive function
    [](uint8_t* buffer, size_t length) -> bool {
        HAL_GPIO_WritePin(TJA_CS_PORT, TJA_CS_PIN, GPIO_PIN_RESET);
        uint8_t dummy[length];
        bool result = HAL_SPI_TransmitReceive_IT(&hspi1, dummy, buffer, length) == HAL_OK;
        return result;
    },
    // chip select function
    [](bool select) {
        HAL_GPIO_WritePin(TJA_CS_PORT, TJA_CS_PIN,
                         select ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }
);

// TJA1145 configuration
chipz::devices::TJA1145<chipz::SPIInterface>::Config tja_config = {
    true,  // enableWakeupOnCan
    false  // wakeupOnExtendedId
};

// TJA1145 CAN transceiver instance
chipz::devices::TJA1145<chipz::SPIInterface> can_transceiver(
    spi,
    tja_config,
    nullptr // Optional: SPI transmission disabled check function
);

void setup() {
    // Initialize TJA1145
    if (!can_transceiver.initialize()) {
        // Error: handle initialization failure
        while(1);
    }

    // Request normal mode (CAN active)
    can_transceiver.requestStateChangeToNormalMode();
}

void loop() {
    // Call main() to run the CAN transceiver state machine
    can_transceiver.main();

    // Check current state
    if (can_transceiver.isReady()) {
        auto state = can_transceiver.getState();
        // State is Normal or Sleep

        // To enter sleep mode:
        // can_transceiver.requestStateChangeToSleepMode();

        // To request power down:
        // can_transceiver.requestPowerDown(true);
        // if (can_transceiver.getPowerDownPermission()) {
        //     // Safe to power down
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
        HAL_Delay(10); // 10ms loop
    }
}
