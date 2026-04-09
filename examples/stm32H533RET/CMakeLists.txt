cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME stm32H533RET)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM, C, and C++ languages
enable_language(C CXX ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# chipz library — header-only, provides chipz::core, chipz::ds3231,
# chipz::max6675 targets and the chipz_add_*/chipz_generate_isrs() functions
add_subdirectory(../.. chipz)

# ---------------------------------------------------------------------------
# chipz device registration — links device targets and records ISR metadata
# ---------------------------------------------------------------------------

chipz_add_ds3231(TARGET ${CMAKE_PROJECT_NAME}
    I2C_INSTANCE I2C1
    I2C_HANDLE   hi2c1
    I2C_IFACE    g_i2c1)

chipz_add_max6675(TARGET ${CMAKE_PROJECT_NAME}
    SPI_INSTANCE SPI2
    SPI_HANDLE   hspi2
    SPI_IFACE    g_spi2)

# Generate chipz_isr_handlers.cpp: I2C1/I2C2 EV+ER handlers, SPI2 handler,
# and all HAL weak callback overrides routing to chipz notify*() functions.
chipz_generate_isrs(TARGET     ${CMAKE_PROJECT_NAME}
                    CORE       g_core
                    HAL_INCLUDE "stm32h5xx_hal.h")

# ---------------------------------------------------------------------------
# chipz application sources
# ---------------------------------------------------------------------------

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ../../port/stm32h5xx/config.cpp
    ../../port/stm32h5xx/chipz_isrs.cpp
    Chipz/app.cpp
)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Suppress GCC 13 ABI change notes for iterator parameter passing (psabi)
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wno-psabi)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    stm32cubemx
    # chipz device targets linked via chipz_add_ds3231 / chipz_add_max6675 above
)
