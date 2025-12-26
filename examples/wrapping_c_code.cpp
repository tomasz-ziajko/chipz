#include <chipz/chipz.hpp>
#include <iostream>
#include <cstring>

// ============================================================================
// Example: Wrapping C HAL functions (simulated STM32 HAL)
// ============================================================================

// Simulated C HAL functions (in real code, these would be from stm32h5xx_hal.h, etc.)
namespace SimulatedHAL {
    // Simulated I2C HAL function (like HAL_I2C_Mem_Read_IT)
    int HAL_I2C_Mem_Read(uint8_t device_addr, uint8_t mem_addr, uint8_t* data, uint16_t size) {
        std::cout << "  [HAL] I2C Read: dev=0x" << std::hex << (int)device_addr
                  << " mem=0x" << (int)mem_addr
                  << " size=" << std::dec << size << "\n";
        // Simulate reading data (return dummy values)
        std::memset(data, 0x42, size);
        return 0; // Success
    }

    // Simulated I2C HAL function (like HAL_I2C_Mem_Write_IT)
    int HAL_I2C_Mem_Write(uint8_t device_addr, uint8_t mem_addr, const uint8_t* data, uint16_t size) {
        std::cout << "  [HAL] I2C Write: dev=0x" << std::hex << (int)device_addr
                  << " mem=0x" << (int)mem_addr
                  << " size=" << std::dec << size << "\n";
        return 0; // Success
    }

    // Simulated SPI HAL function (like HAL_SPI_TransmitReceive_IT)
    int HAL_SPI_TransmitReceive(uint8_t* tx_buf, uint8_t* rx_buf, uint16_t size) {
        std::cout << "  [HAL] SPI Transfer: size=" << size << "\n";
        // Simulate SPI transfer (copy TX to RX with some modification)
        for (uint16_t i = 0; i < size; ++i) {
            rx_buf[i] = tx_buf[i] ^ 0xFF; // Simple simulation
        }
        return 0; // Success
    }

    // Simulated GPIO function for chip select
    void HAL_GPIO_WritePin(bool state) {
        std::cout << "  [HAL] GPIO CS: " << (state ? "LOW (selected)" : "HIGH (deselected)") << "\n";
    }
}

// ============================================================================
// Example: Creating wrapper functions for chipz interfaces
// ============================================================================

// Wrapper for I2C read - adapts HAL to chipz interface
int i2c_read_wrapper(uint8_t device_address, uint8_t mem_address,
                     uint8_t* data, uint16_t size) {
    return SimulatedHAL::HAL_I2C_Mem_Read(device_address, mem_address, data, size);
}

// Wrapper for I2C write - adapts HAL to chipz interface
int i2c_write_wrapper(uint8_t device_address, uint8_t mem_address,
                      const uint8_t* data, uint16_t size) {
    return SimulatedHAL::HAL_I2C_Mem_Write(device_address, mem_address, data, size);
}

// Wrapper for SPI transfer - adapts HAL to chipz interface
int spi_transfer_wrapper(uint8_t* tx_buffer, uint8_t* rx_buffer, uint16_t size) {
    return SimulatedHAL::HAL_SPI_TransmitReceive(tx_buffer, rx_buffer, size);
}

// Wrapper for chip select control
void chip_select_wrapper(bool select) {
    SimulatedHAL::HAL_GPIO_WritePin(select);
}

// ============================================================================
// Main example
// ============================================================================

int main() {
    std::cout << "=== chipz: Wrapping C HAL Code Example ===\n\n";

    // -------------------------------------------------------------------------
    // Example 1: DS3231 RTC with I2C (wrapping C I2C HAL)
    // -------------------------------------------------------------------------
    std::cout << "--- Example 1: DS3231 with I2C ---\n";

    // Create I2C interface wrapping C HAL functions
    chipz::interfaces::I2CInterface i2c(
        0x68,                   // DS3231 I2C address
        i2c_read_wrapper,       // Wrap HAL_I2C_Mem_Read
        i2c_write_wrapper       // Wrap HAL_I2C_Mem_Write
    );

    // Create DS3231 driver using the I2C interface
    chipz::devices::DS3231<chipz::interfaces::I2CInterface> rtc(i2c);

    std::cout << "Device: " << rtc.getDeviceId() << "\n";

    if (rtc.initialize()) {
        std::cout << "DS3231 initialized successfully\n";
    }

    // Set memory address and perform a read
    std::cout << "\nReading time from DS3231:\n";
    i2c.setMemoryAddress(0x00); // Time registers start at 0x00
    uint8_t time_data[7];
    if (i2c.receive(time_data, 7)) {
        std::cout << "Successfully read 7 bytes from time registers\n";
    }

    // -------------------------------------------------------------------------
    // Example 2: MAX6675 Thermocouple with SPI (wrapping C SPI HAL)
    // -------------------------------------------------------------------------
    std::cout << "\n--- Example 2: MAX6675 with SPI ---\n";

    // Create SPI interface wrapping C HAL functions
    chipz::interfaces::SPIInterface spi(
        spi_transfer_wrapper,   // Wrap HAL_SPI_TransmitReceive
        chip_select_wrapper     // Wrap GPIO chip select control
    );

    // Create MAX6675 driver using the SPI interface
    chipz::devices::MAX6675<chipz::interfaces::SPIInterface> thermocouple(spi);

    std::cout << "Device: " << thermocouple.getDeviceId() << "\n";

    if (thermocouple.initialize()) {
        std::cout << "MAX6675 initialized successfully\n";
    }

    // Perform SPI read
    std::cout << "\nReading temperature from MAX6675:\n";
    uint8_t temp_data[2];
    if (spi.receive(temp_data, 2)) {
        std::cout << "Successfully read 2 bytes from thermocouple\n";
        std::cout << "Raw data: 0x" << std::hex
                  << (int)temp_data[0] << " 0x" << (int)temp_data[1]
                  << std::dec << "\n";
    }

    // -------------------------------------------------------------------------
    // Example 3: Demonstrating polymorphism
    // -------------------------------------------------------------------------
    std::cout << "\n--- Example 3: Polymorphic Usage ---\n";

    // Can still use base class pointer for polymorphism
    chipz::Peripheral* devices[] = {&rtc, &thermocouple};

    for (auto* device : devices) {
        std::cout << "Calling main() on " << device->getDeviceId() << "\n";
        device->main();
    }

    std::cout << "\n=== Example Complete ===\n";
    return 0;
}
