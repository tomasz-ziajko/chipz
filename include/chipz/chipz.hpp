// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_HPP
#define CHIPZ_HPP

// Main chipz library header
// Include this to get access to all chipz functionality

#include <chipz/devices/ds3231.hpp>
#include <chipz/devices/hd44780.hpp>
#include <chipz/devices/max6675.hpp>
#include <chipz/devices/mcp795w.hpp>
#include <chipz/devices/tja1145.hpp>

#include "core/chip.hpp"
#include "core/communication_interface.hpp"
#include "core/completion_sources/external_completion_source.hpp"
#include "core/completion_sources/timer_completion_source.hpp"
#include "core/concepts.hpp"
#include "core/core.hpp"
#include "core/timer_interface.hpp"
#include "interfaces/i2c_interface.hpp"
#include "interfaces/parallel_interface.hpp"
#include "interfaces/spi_interface.hpp"
#include "interfaces/uart_interface.hpp"
#include "network/can/can_interface.hpp"

namespace chipz {
// Library version
constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 1;
constexpr int VERSION_PATCH = 0;
}  // namespace chipz

#endif  // CHIPZ_HPP
