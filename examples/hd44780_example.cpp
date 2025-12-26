#include <chipz/devices/hd44780.hpp>
#include <iostream>

int main() {
    std::cout << "HD44780 LCD Display Example\n";
    std::cout << "============================\n\n";

    // Configure 16x2 LCD in 4-bit mode
    chipz::devices::HD44780::Config config{
        chipz::devices::HD44780::InterfaceMode::Bit4,
        chipz::devices::HD44780::DisplaySize::Size16x2,
        true,  // cursor visible
        false  // cursor blink
    };

    chipz::devices::HD44780 lcd(config);

    if (!lcd.initialize()) {
        std::cerr << "Failed to initialize HD44780\n";
        return 1;
    }

    std::cout << "Device initialized: " << lcd.getDeviceId() << "\n";

    // Example: Display text
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Hello, chipz!");

    lcd.setCursor(1, 0);
    lcd.print("Embedded C++");

    std::cout << "Text displayed on LCD\n";

    return 0;
}
