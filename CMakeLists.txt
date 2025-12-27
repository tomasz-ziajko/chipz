cmake_minimum_required(VERSION 3.14)

project(chipz
    VERSION 0.1.0
    DESCRIPTION "C++ library for embedded chip drivers"
    LANGUAGES CXX
)

# Specify C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Option to build examples
option(CHIPZ_BUILD_EXAMPLES "Build chipz examples" ON)
option(CHIPZ_BUILD_TESTS "Build chipz tests" OFF)

# Create header-only library
add_library(chipz INTERFACE)

# Specify include directories
target_include_directories(chipz INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Require C++17
target_compile_features(chipz INTERFACE cxx_std_17)

# Add alias for use with add_subdirectory
add_library(chipz::chipz ALIAS chipz)

# Platform-specific optional libraries
option(CHIPZ_ENABLE_FREERTOS "Build FreeRTOS platform support (C++ allocator)" OFF)

if(CHIPZ_ENABLE_FREERTOS)
    # FreeRTOS platform library (compiled, not header-only)
    # Note: This library requires FreeRTOS headers to be in the include path.
    # When used in a project with stm32cubemx, link it after stm32cubemx so
    # it inherits the FreeRTOS include directories.
    add_library(chipz_platform_freertos OBJECT
        src/platform/freertos/cpp_allocator.cpp
    )

    target_include_directories(chipz_platform_freertos PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )

    # Inherit include paths from parent if available
    # This allows the platform library to find FreeRTOS.h
    if(TARGET stm32cubemx)
        target_link_libraries(chipz_platform_freertos PUBLIC stm32cubemx)
    endif()

    target_compile_features(chipz_platform_freertos PUBLIC cxx_std_17)

    add_library(chipz::platform::freertos ALIAS chipz_platform_freertos)

    message(STATUS "Chipz: FreeRTOS platform support enabled")
    message(STATUS "  Note: Ensure FreeRTOS headers are in include path")
    message(STATUS "  Note: Link with chipz::platform::freertos in your application")
endif()

# Examples
if(CHIPZ_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Tests
if(CHIPZ_BUILD_TESTS)
    enable_testing()

    # Fetch Google Test
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_subdirectory(tests)
endif()

# Installation
include(GNUInstallDirs)

install(TARGETS chipz
    EXPORT chipzTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT chipzTargets
    FILE chipzTargets.cmake
    NAMESPACE chipz::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/chipz
)

# Create config file
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
