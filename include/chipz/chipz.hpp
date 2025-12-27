#ifndef CHIPZ_HPP
#define CHIPZ_HPP

// Main chipz library header
// Include this to get access to all chipz functionality

#include "peripheral.hpp"
#include "communication_interface.hpp"
#include "interfaces/i2c_interface.hpp"
#include "interfaces/spi_interface.hpp"
#include "devices/ds3231.hpp"
#include "devices/hd44780.hpp"
#include "devices/max6675.hpp"
#include "devices/pcf8574.hpp"
#include "devices/tja1145.hpp"
#include "devices/mcp795w.hpp"

namespace chipz {
    // Library version
    constexpr int VERSION_MAJOR = 0;
    constexpr int VERSION_MINOR = 1;
    constexpr int VERSION_PATCH = 0;
}

#endif // CHIPZ_HPP
