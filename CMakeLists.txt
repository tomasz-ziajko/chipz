cmake_minimum_required(VERSION 3.14)

project(chipz
    VERSION 0.1.0
    DESCRIPTION "C++ library for embedded chip drivers"
    LANGUAGES CXX
)

# Specify C++ standard
set(CMAKE_CXX_STANDARD 20)
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
target_compile_features(chipz INTERFACE cxx_std_20)

# Add alias for use with add_subdirectory
add_library(chipz::chipz ALIAS chipz)

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
