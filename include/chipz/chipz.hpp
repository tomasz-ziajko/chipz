// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_HPP
#define CHIPZ_HPP

// Main chipz library header
// Include this to get access to all chipz functionality

#include "chip.hpp"
#include "timer_interface.hpp"
#include "core.hpp"
#include "communication_interface.hpp"
#include "concepts.hpp"
#include "interfaces/i2c_interface.hpp"
#include "interfaces/spi_interface.hpp"
#include <chipz/devices/ds3231.hpp>
#include <chipz/devices/hd44780.hpp>
#include <chipz/devices/max6675.hpp>
#include <chipz/devices/tja1145.hpp>
#include <chipz/devices/mcp795w.hpp>

namespace chipz {
    // Library version
    constexpr int VERSION_MAJOR = 0;
    constexpr int VERSION_MINOR = 1;
    constexpr int VERSION_PATCH = 0;
}

#endif // CHIPZ_HPP
