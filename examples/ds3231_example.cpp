#include <chipz/chipz.hpp>
#include <iostream>
#include <ctime>
#include <iomanip>

// Mock I2C interface for demonstration
class MockI2CInterface : public chipz::CommunicationInterface {
public:
    bool transmit(const uint8_t* data, size_t length) override {
        ensureBufferSize(tx_buffer_, length);
        std::cout << "  [Mock I2C] Writing " << length << " bytes to DS3231\n";
        std::cout << "    Data: ";
        for (size_t i = 0; i < length; ++i) {
            std::cout << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<int>(data[i]) << " ";
        }
        std::cout << std::dec << "\n";
        return true;
    }

    bool receive(uint8_t* buffer, size_t length) override {
        ensureBufferSize(rx_buffer_, length);
        std::cout << "  [Mock I2C] Reading " << length << " bytes from DS3231\n";

        // Simulate reading time: 2025-12-26 14:30:45, Thursday
        if (length == 7) {
            rx_buffer_[0] = 0x45; // Seconds: 45 (BCD)
            rx_buffer_[1] = 0x30; // Minutes: 30 (BCD)
            rx_buffer_[2] = 0x14; // Hours: 14 (BCD, 24-hour mode)
            rx_buffer_[3] = 0x05; // Day of week: 5 (Thursday)
            rx_buffer_[4] = 0x26; // Day of month: 26 (BCD)
            rx_buffer_[5] = 0x12; // Month: 12 (BCD)
            rx_buffer_[6] = 0x25; // Year: 25 (BCD, representing 2025)
        }
        // Simulate reading temperature: 23.75°C
        else if (length == 2) {
            rx_buffer_[0] = 23;   // Integer part
            rx_buffer_[1] = 0xC0; // Fraction: 0b11000000 = 0.75°C
        }

        for (size_t i = 0; i < length; ++i) {
            buffer[i] = rx_buffer_[i];
        }
        return true;
    }
};

int main() {
    std::cout << "DS3231 Real-Time Clock Example\n";
    std::cout << "================================\n\n";

    // Create mock I2C interface
    MockI2CInterface i2c;

    // Create DS3231 RTC with I2C interface
    chipz::devices::DS3231<MockI2CInterface> rtc(i2c);

    if (!rtc.initialize()) {
        std::cerr << "Failed to initialize DS3231\n";
        return 1;
    }

    std::cout << "Device initialized: " << rtc.getDeviceId() << "\n\n";

    // Example 1: Set date and time using std::tm
    std::cout << "=== Setting Time ===\n";
    std::tm set_time = {};
    set_time.tm_sec = 0;         // Seconds (0-59)
    set_time.tm_min = 30;        // Minutes (0-59)
    set_time.tm_hour = 14;       // Hours (0-23)
    set_time.tm_mday = 26;       // Day of month (1-31)
    set_time.tm_mon = 11;        // Month (0-11, so 11 = December)
    set_time.tm_year = 125;      // Years since 1900 (125 = 2025)
    set_time.tm_wday = 4;        // Day of week (0-6, 0=Sunday, 4=Thursday)

    if (rtc.setTime(set_time)) {
        std::cout << "Time set successfully!\n";
        std::cout << "Set to: 2025-12-26 14:30:00 (Thursday)\n\n";
    } else {
        std::cerr << "Failed to set time\n\n";
    }

    // Example 2: Read current time
    std::cout << "=== Reading Time ===\n";
    std::tm current_time;
    if (rtc.getTime(current_time)) {
        std::cout << "Current time read successfully!\n";

        // Format and display the time
        char time_str[100];
        std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S (%A)", &current_time);
        std::cout << "Time: " << time_str << "\n\n";
    } else {
        std::cerr << "Failed to read time\n\n";
    }

    // Example 3: Read temperature
    std::cout << "=== Reading Temperature ===\n";
    chipz::devices::DS3231<MockI2CInterface>::Temperature temp;
    if (rtc.getTemperature(temp)) {
        std::cout << "Temperature: " << static_cast<int>(temp.integer)
                  << "." << std::setw(2) << std::setfill('0')
                  << static_cast<int>(temp.fraction) << "°C\n";
    } else {
        std::cerr << "Failed to read temperature\n";
    }

    return 0;
}
