/**
 * @file cpp_allocator.cpp
 * @brief FreeRTOS C++ memory allocator implementation
 *
 * This file overrides the global C++ operator new/delete to redirect all
 * C++ dynamic memory allocations to the FreeRTOS heap (pvPortMalloc/vPortFree)
 * instead of the standard library malloc/free.
 *
 * @section rationale Why This Is Needed
 *
 * Embedded systems using FreeRTOS typically have two separate heaps:
 * 1. System heap (newlib malloc/free) - Defined by _Min_Heap_Size in linker script
 * 2. FreeRTOS heap (pvPortMalloc/vPortFree) - Defined by configTOTAL_HEAP_SIZE
 *
 * By default:
 * - C++ new/delete uses system heap (typically 512 bytes - too small!)
 * - FreeRTOS tasks, queues, etc. use FreeRTOS heap (configurable, often 8-32 KB)
 *
 * This leads to:
 * - System heap exhaustion from C++ objects (std::vector, std::function, lambdas)
 * - Heap corruption causing hard faults and undefined behavior
 * - Wasted FreeRTOS heap space that C++ code can't access
 *
 * @section solution The Solution
 *
 * By overriding operator new/delete globally, we redirect all C++ allocations
 * to the larger FreeRTOS heap, eliminating the system heap as a bottleneck.
 *
 * @section requirements Requirements
 *
 * - FreeRTOS heap implementation that supports both allocation and deallocation
 *   (heap_4 or heap_5 recommended, NOT heap_1 which is allocate-only)
 * - Increase configTOTAL_HEAP_SIZE to accommodate C++ objects (32 KB recommended)
 * - Link this file into your application build
 *
 * @section compatibility Compatibility
 *
 * This works with:
 * - FreeRTOS heap_4 (Best for most applications)
 * - FreeRTOS heap_5 (Multiple memory regions)
 * - FreeRTOS heap_2 (Legacy, no coalescing)
 *
 * This does NOT work with:
 * - FreeRTOS heap_1 (Allocate-only, no free support)
 * - FreeRTOS heap_3 (Wraps malloc/free, creates circular dependency)
 *
 * @author Chipz Library
 * @date 2025-12-27
 */

#include "FreeRTOS.h"

// Note: These operators MUST NOT be in a namespace or extern "C" block for proper override
// They must match the exact signature expected by the C++ runtime

/**
 * @brief Override global operator new to use FreeRTOS heap
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or nullptr on failure
 *
 * @note Unlike standard new, this does NOT throw std::bad_alloc on failure.
 *       It returns nullptr instead (new(std::nothrow) behavior).
 */
void* operator new(size_t size) {
    return pvPortMalloc(size);
}

/**
 * @brief Override global operator new[] for array allocation
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or nullptr on failure
 */
void* operator new[](size_t size) {
    return pvPortMalloc(size);
}

/**
 * @brief Override global operator delete to use FreeRTOS heap
 * @param ptr Pointer to memory to free (can be nullptr)
 *
 * @note Safe to call with nullptr (standard C++ behavior)
 */
void operator delete(void* ptr) noexcept {
    vPortFree(ptr);
}

/**
 * @brief Override global operator delete[] for array deallocation
 * @param ptr Pointer to memory to free (can be nullptr)
 */
void operator delete[](void* ptr) noexcept {
    vPortFree(ptr);
}

/**
 * @brief Override sized delete operator (C++14)
 * @param ptr Pointer to memory to free
 * @param size Size hint (ignored, FreeRTOS tracks this internally)
 *
 * @note The size parameter is provided by the compiler but FreeRTOS
 *       doesn't use it - the allocator tracks sizes internally.
 */
void operator delete(void* ptr, size_t size) noexcept {
    (void)size;  // Unused - FreeRTOS heap manages size internally
    vPortFree(ptr);
}

/**
 * @brief Override sized delete[] operator (C++14)
 * @param ptr Pointer to memory to free
 * @param size Size hint (ignored)
 */
void operator delete[](void* ptr, size_t size) noexcept {
    (void)size;  // Unused
    vPortFree(ptr);
}
