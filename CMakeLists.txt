cmake_minimum_required(VERSION 3.23)

project(chipz
    VERSION 0.1.0
    DESCRIPTION "C++ library for embedded chip drivers"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CHIPZ_BUILD_EXAMPLES "Build chipz examples" ON)
option(CHIPZ_BUILD_TESTS "Build chipz tests" OFF)

# Core infrastructure (peripheral base, interfaces, concepts, scheduler)
add_library(chipz_core INTERFACE)
target_sources(chipz_core INTERFACE
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/chipz/peripheral.hpp
        include/chipz/communication_interface.hpp
        include/chipz/concepts.hpp
        include/chipz/timer_interface.hpp
        include/chipz/core.hpp
        include/chipz/chipz.hpp
        include/chipz/interfaces/i2c_interface.hpp
        include/chipz/interfaces/spi_interface.hpp
)
target_compile_features(chipz_core INTERFACE cxx_std_20)
add_library(chipz::core ALIAS chipz_core)

# Test support target (shared mocks) — defined before devices so device
# CMakeLists can reference it when CHIPZ_BUILD_TESTS is ON
if(CHIPZ_BUILD_TESTS)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    include(GoogleTest)

    add_library(chipz_test_support INTERFACE)
    target_include_directories(chipz_test_support INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/support
    )
    target_link_libraries(chipz_test_support INTERFACE
        chipz::core
        GTest::gmock
    )
endif()

# Per-device targets
add_subdirectory(devices)

# Umbrella target — links all devices, preserves chipz::chipz for consumers
# who want the full library without selecting individual devices
add_library(chipz_all INTERFACE)
target_sources(chipz_all INTERFACE
    FILE_SET HEADERS
    BASE_DIRS include
    FILES include/chipz/chipz.hpp
)
target_link_libraries(chipz_all INTERFACE
    chipz::core
    chipz::ds3231
    chipz::max6675
    chipz::tja1145
    chipz::mcp795w
    chipz::hd44780
)
add_library(chipz::chipz ALIAS chipz_all)

if(CHIPZ_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(CHIPZ_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Installation
include(GNUInstallDirs)

install(TARGETS
        chipz_core
        chipz_ds3231 chipz_max6675 chipz_tja1145 chipz_mcp795w chipz_hd44780
        chipz_all
    EXPORT chipzTargets
    FILE_SET HEADERS
)

install(EXPORT chipzTargets
    FILE chipzTargets.cmake
    NAMESPACE chipz::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/chipz
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/chipzConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/chipzConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/chipz
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/chipzConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/chipzConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/chipzConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/chipz
)
