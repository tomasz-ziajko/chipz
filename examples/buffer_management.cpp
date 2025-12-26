#include <chipz/chipz.hpp>
#include <iostream>

// Instrumented I2C wrapper that shows buffer allocations
class InstrumentedI2CInterface : public chipz::interfaces::I2CInterface {
public:
    InstrumentedI2CInterface(uint8_t device_address,
                             I2CReadFunction read_func,
                             I2CWriteFunction write_func)
        : I2CInterface(device_address, read_func, write_func) {}

    bool transmit(const uint8_t* data, size_t length) override {
        size_t old_size = getBufferSize();
        bool result = I2CInterface::transmit(data, length);
        size_t new_size = getBufferSize();

        if (new_size != old_size) {
            std::cout << "  [Buffer] TX buffer grew: " << old_size
                      << " -> " << new_size << " bytes\n";
        } else if (old_size == 0) {
            std::cout << "  [Buffer] TX buffer allocated: " << new_size << " bytes\n";
        } else {
            std::cout << "  [Buffer] TX buffer reused: " << new_size << " bytes\n";
        }

        return result;
    }

    bool receive(uint8_t* buffer, size_t length) override {
        size_t old_size = getBufferSize();
        bool result = I2CInterface::receive(buffer, length);
        size_t new_size = getBufferSize();

        if (new_size != old_size) {
            std::cout << "  [Buffer] RX buffer grew: " << old_size
                      << " -> " << new_size << " bytes\n";
        } else if (old_size == 0) {
            std::cout << "  [Buffer] RX buffer allocated: " << new_size << " bytes\n";
        } else {
            std::cout << "  [Buffer] RX buffer reused: " << new_size << " bytes\n";
        }

        return result;
    }
};

// Simulated I2C functions
int i2c_read(uint8_t dev, uint8_t mem, uint8_t* data, uint16_t size) {
    std::cout << "  [HAL] I2C Read: " << (int)size << " bytes\n";
    return 0;
}

int i2c_write(uint8_t dev, uint8_t mem, const uint8_t* data, uint16_t size) {
    std::cout << "  [HAL] I2C Write: " << (int)size << " bytes\n";
    return 0;
}

int main() {
    std::cout << "=== Buffer Management Demonstration ===\n\n";

    // Create I2C interface (buffers not allocated yet)
    InstrumentedI2CInterface i2c(0x68, i2c_read, i2c_write);

    std::cout << "Initial buffer size: " << i2c.getBufferSize() << " bytes\n\n";

    // First transmission: 7 bytes (allocates buffer)
    std::cout << "1. Transmit 7 bytes:\n";
    uint8_t small_data[7] = {1, 2, 3, 4, 5, 6, 7};
    i2c.transmit(small_data, 7);

    // Second transmission: 5 bytes (reuses existing buffer)
    std::cout << "\n2. Transmit 5 bytes:\n";
    uint8_t smaller_data[5] = {1, 2, 3, 4, 5};
    i2c.transmit(smaller_data, 5);

    // Third transmission: 20 bytes (grows buffer)
    std::cout << "\n3. Transmit 20 bytes:\n";
    uint8_t large_data[20];
    for (int i = 0; i < 20; ++i) large_data[i] = i;
    i2c.transmit(large_data, 20);

    // Fourth transmission: 15 bytes (reuses grown buffer)
    std::cout << "\n4. Transmit 15 bytes:\n";
    uint8_t medium_data[15];
    for (int i = 0; i < 15; ++i) medium_data[i] = i;
    i2c.transmit(medium_data, 15);

    // Fifth transmission: 100 bytes (grows buffer again)
    std::cout << "\n5. Transmit 100 bytes:\n";
    uint8_t huge_data[100];
    for (int i = 0; i < 100; ++i) huge_data[i] = i;
    i2c.transmit(huge_data, 100);

    // Sixth transmission: 50 bytes (reuses buffer)
    std::cout << "\n6. Transmit 50 bytes:\n";
    uint8_t fifty_data[50];
    for (int i = 0; i < 50; ++i) fifty_data[i] = i;
    i2c.transmit(fifty_data, 50);

    std::cout << "\nFinal buffer size: " << i2c.getBufferSize() << " bytes\n";

    std::cout << "\n=== Key Observations ===\n";
    std::cout << "- Buffer allocated on first use (lazy allocation)\n";
    std::cout << "- Buffer reused when request fits in existing size\n";
    std::cout << "- Buffer grows when larger request made\n";
    std::cout << "- Buffer never shrinks (minimizes allocations)\n";
    std::cout << "- This strategy minimizes heap fragmentation!\n";

    return 0;
}
