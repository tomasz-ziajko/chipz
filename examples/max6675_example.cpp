#include <chipz/devices/max6675.hpp>
#include <iostream>
#include <iomanip>

int main() {
    std::cout << "MAX6675 Thermocouple Reader Example\n";
    std::cout << "====================================\n\n";

    chipz::devices::MAX6675 thermocouple;

    if (!thermocouple.initialize()) {
        std::cerr << "Failed to initialize MAX6675\n";
        return 1;
    }

    std::cout << "Device initialized: " << thermocouple.getDeviceId() << "\n\n";

    // Example: Read temperature
    chipz::devices::MAX6675::Reading reading;

    if (thermocouple.readTemperature(reading, chipz::devices::MAX6675::TemperatureUnit::Celsius)) {
        if (reading.valid) {
            std::cout << std::fixed << std::setprecision(2);
            std::cout << "Temperature: " << reading.temperature << "°C\n";

            if (reading.thermocoupleOpen) {
                std::cout << "WARNING: Thermocouple not connected!\n";
            }
        } else {
            std::cout << "Invalid reading\n";
        }
    } else {
        std::cout << "Failed to read temperature\n";
    }

    // Example: Check connection
    if (thermocouple.isThermocoupleConnected()) {
        std::cout << "Thermocouple is connected\n";
    } else {
        std::cout << "Thermocouple is NOT connected\n";
    }

    return 0;
}
