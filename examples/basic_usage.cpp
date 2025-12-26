#include <chipz/chipz.hpp>
#include <iostream>
#include <array>

// Mock communication interface for demonstration
// In real usage, you would use actual I2C, SPI, or GPIO implementations
class MockCommInterface : public chipz::CommunicationInterface {
public:
    bool transmit(const uint8_t* data, size_t length) override {
        // Ensure buffer is large enough (grows on demand)
        ensureBufferSize(tx_buffer_, length);

        std::cout << "  [Mock] Transmitting " << length << " bytes\n";
        return true;
    }

    bool receive(uint8_t* buffer, size_t length) override {
        // Ensure buffer is large enough (grows on demand)
        ensureBufferSize(rx_buffer_, length);

        std::cout << "  [Mock] Receiving " << length << " bytes\n";
        return true;
    }

    // Note: isReady(), getTxBuffer(), getRxBuffer(), getBufferSize()
    // are inherited from CommunicationInterface base class
    // No need to override unless custom behavior is needed
};

int main() {
    std::cout << "chipz library v"
              << chipz::VERSION_MAJOR << "."
              << chipz::VERSION_MINOR << "."
              << chipz::VERSION_PATCH << "\n\n";

    // Create mock communication interfaces
    // In real usage, these would be I2C, SPI, GPIO implementations
    MockCommInterface i2c;   // For DS3231
    MockCommInterface gpio;  // For HD44780
    MockCommInterface spi;   // For MAX6675

    // Example: DS3231 RTC with I2C interface
    chipz::devices::DS3231<MockCommInterface> rtc(i2c);
    std::cout << "Device: " << rtc.getDeviceId() << "\n";

    if (rtc.initialize()) {
        std::cout << "DS3231 initialized successfully\n";
    }

    // Example: HD44780 LCD with GPIO interface
    chipz::devices::HD44780<MockCommInterface>::Config lcdConfig{
        chipz::devices::HD44780<MockCommInterface>::InterfaceMode::Bit4,
        chipz::devices::HD44780<MockCommInterface>::DisplaySize::Size16x2,
        true,  // cursor visible
        false  // cursor blink
    };
    chipz::devices::HD44780<MockCommInterface> lcd(gpio, lcdConfig);
    std::cout << "\nDevice: " << lcd.getDeviceId() << "\n";

    if (lcd.initialize()) {
        std::cout << "HD44780 initialized successfully\n";
    }

    // Example: MAX6675 Thermocouple with SPI interface
    chipz::devices::MAX6675<MockCommInterface> thermocouple(spi);
    std::cout << "\nDevice: " << thermocouple.getDeviceId() << "\n";

    if (thermocouple.initialize()) {
        std::cout << "MAX6675 initialized successfully\n";
    }

    // Demonstrate polymorphic usage via Peripheral base class
    std::cout << "\n--- Polymorphic Usage ---\n";
    chipz::Peripheral* devices[] = {&rtc, &lcd, &thermocouple};

    for (auto* device : devices) {
        std::cout << "Calling main() on " << device->getDeviceId() << "\n";
        device->main();
    }

    // Demonstrate automatic registration feature
    std::cout << "\n--- Automatic Registration ---\n";
    std::cout << "Number of registered peripherals: " << chipz::Peripheral::getCount() << "\n";

    // Initialize all peripherals at once using static method
    std::cout << "\nInitializing all peripherals...\n";
    if (chipz::Peripheral::initializeAll()) {
        std::cout << "All peripherals initialized successfully!\n";
    }

    // Run main() on all registered peripherals automatically
    std::cout << "\nRunning main() on all registered peripherals...\n";
    chipz::Peripheral::runAllMain();

    // Check if all peripherals are ready
    std::cout << "\nAll peripherals ready: "
              << (chipz::Peripheral::allReady() ? "Yes" : "No") << "\n";

    return 0;
}
