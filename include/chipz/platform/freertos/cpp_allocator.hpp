#ifndef CHIPZ_PLATFORM_FREERTOS_CPP_ALLOCATOR_HPP
#define CHIPZ_PLATFORM_FREERTOS_CPP_ALLOCATOR_HPP

/**
 * @file cpp_allocator.hpp
 * @brief FreeRTOS C++ allocator - redirects new/delete to FreeRTOS heap
 *
 * @section problem Problem
 * By default, C++ new/delete operators use the standard library malloc/free,
 * which allocates from the system heap defined by _Min_Heap_Size in the linker
 * script (typically 512 bytes). This tiny heap is insufficient for C++ applications
 * using classes, templates, std::vector, std::function, and other STL components.
 *
 * When the system heap is exhausted, heap corruption occurs, leading to hard faults,
 * invalid function pointer calls, and unpredictable behavior.
 *
 * @section solution Solution
 * This file overrides the global C++ operator new/delete to use FreeRTOS
 * pvPortMalloc/vPortFree instead of the system heap. All C++ allocations
 * will then use the larger FreeRTOS heap (configurable via configTOTAL_HEAP_SIZE).
 *
 * @section usage Usage
 *
 * 1. Include this header in your project
 * 2. Link the cpp_allocator.cpp implementation file
 * 3. Increase FreeRTOS heap size in FreeRTOSConfig.h:
 *
 * @code{.c}
 * #define configTOTAL_HEAP_SIZE  ((size_t)32768)  // 32KB recommended for C++
 * @endcode
 *
 * 4. In CMakeLists.txt:
 * @code{.cmake}
 * target_sources(your_app PRIVATE
 *     ${CHIPZ_SOURCE_DIR}/src/platform/freertos/cpp_allocator.cpp
 * )
 * @endcode
 *
 * @section technical Technical Details
 *
 * This overrides the following operators:
 * - operator new(size_t)
 * - operator new[](size_t)
 * - operator delete(void*)
 * - operator delete[](void*)
 * - operator delete(void*, size_t)   // C++14 sized delete
 * - operator delete[](void*, size_t) // C++14 sized delete
 *
 * All allocations go through pvPortMalloc/vPortFree, which uses the FreeRTOS
 * heap configured in FreeRTOSConfig.h. The implementation is in cpp_allocator.cpp
 * to ensure these operators are defined exactly once (ODR compliance).
 *
 * @warning This must be linked into your application BEFORE any C++ code that
 *          uses dynamic allocation. Link order matters!
 *
 * @note This is compatible with all FreeRTOS heap implementations (heap_1 through heap_5),
 *       but heap_4 or heap_5 are recommended for C++ applications due to their support
 *       for allocation and deallocation.
 *
 * @author Chipz Library
 * @date 2025-12-27
 */

// No inline implementations - these MUST be in cpp_allocator.cpp to avoid ODR violations

#endif // CHIPZ_PLATFORM_FREERTOS_CPP_ALLOCATOR_HPP
