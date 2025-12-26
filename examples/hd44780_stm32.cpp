/**
 * @file hd44780_stm32.cpp
 * @brief HD44780 LCD example for STM32 with GPIO
 */

#include <chipz/chipz.hpp>
#include "stm32f4xx_hal.h" // Replace with your STM32 family
#include <cstring>

// System tick (milliseconds)
volatile uint32_t system_tick_ms = 0;

// GPIO pin definitions (adjust for your hardware)
#define LCD_RS_PIN GPIO_PIN_0
#define LCD_RS_PORT GPIOA
#define LCD_E_PIN GPIO_PIN_1
#define LCD_E_PORT GPIOA
#define LCD_D4_PIN GPIO_PIN_4
#define LCD_D4_PORT GPIOA
#define LCD_D5_PIN GPIO_PIN_5
#define LCD_D5_PORT GPIOA
#define LCD_D6_PIN GPIO_PIN_6
#define LCD_D6_PORT GPIOA
#define LCD_D7_PIN GPIO_PIN_7
#define LCD_D7_PORT GPIOA

// GPIO interface wrapper
chipz::GPIOInterface gpio(
    // Set pin function
    [](uint8_t pin, bool state) {
        GPIO_TypeDef* port = nullptr;
        uint16_t gpio_pin = 0;

        switch (pin) {
            case 0: port = LCD_RS_PORT; gpio_pin = LCD_RS_PIN; break; // RS
            case 1: port = LCD_E_PORT; gpio_pin = LCD_E_PIN; break;   // E
            case 2: port = LCD_D4_PORT; gpio_pin = LCD_D4_PIN; break; // D4
            case 3: port = LCD_D5_PORT; gpio_pin = LCD_D5_PIN; break; // D5
            case 4: port = LCD_D6_PORT; gpio_pin = LCD_D6_PIN; break; // D6
            case 5: port = LCD_D7_PORT; gpio_pin = LCD_D7_PIN; break; // D7
        }

        if (port) {
            HAL_GPIO_WritePin(port, gpio_pin, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
    },
    // Delay microseconds function
    [](uint32_t us) {
        // Simple delay loop (adjust based on CPU clock)
        volatile uint32_t count = us * (SystemCoreClock / 1000000 / 3);
        while (count--);
    }
);

// HD44780 LCD configuration
chipz::devices::HD44780<chipz::GPIOInterface>::Config lcd_config = {
    0,  // RS pin
    1,  // E pin
    2,  // D4 pin
    3,  // D5 pin
    4,  // D6 pin
    5,  // D7 pin
    16, // columns
    2   // rows
};

// HD44780 LCD instance
chipz::devices::HD44780<chipz::GPIOInterface> lcd(
    gpio,
    lcd_config,
    []() -> uint32_t { return system_tick_ms; }
);

void setup() {
    // Initialize LCD
    if (!lcd.initialize()) {
        // Error: handle initialization failure
        while(1);
    }
}

void loop() {
    // Call main() to run the LCD state machine
    lcd.main();

    // Wait for LCD to be ready
    if (lcd.isReady()) {
        // Clear display and write text
        lcd.clear();
        lcd.writeString("Hello, World!");

        // Set cursor to second row
        lcd.setCursor(0, 1);
        lcd.writeString("STM32 + Chipz");

        // Don't run this repeatedly in actual code
        HAL_Delay(1000);
    }
}

// SysTick callback (increment system tick)
void HAL_SYSTICK_Callback(void) {
    system_tick_ms++;
}

int main() {
    HAL_Init();
    // Configure clocks, GPIO, etc.

    setup();

    while (1) {
        loop();
        HAL_Delay(10); // 10ms loop
    }
}
