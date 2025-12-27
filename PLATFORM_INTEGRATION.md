# Platform Integration Guide

## Overview

The chipz library now includes platform-specific components to help integrate with various RTOS and hardware platforms. These components handle common embedded system challenges like C++ memory allocation in RTOS environments.

## Directory Structure

```
libs/
├── include/chipz/
│   └── platform/           # Platform-specific headers
│       └── freertos/
│           └── cpp_allocator.hpp
└── src/
    └── platform/           # Platform implementations (need compilation)
        └── freertos/
            └── cpp_allocator.cpp
```

## FreeRTOS C++ Allocator

### The Problem

Embedded systems using FreeRTOS typically have two separate memory heaps:

1. **System heap** (newlib malloc/free)
   - Defined by `_Min_Heap_Size` in linker script
   - Typically 512 bytes (very small!)
   - Used by C++ `new`/`delete` operators by default

2. **FreeRTOS heap** (pvPortMalloc/vPortFree)
   - Defined by `configTOTAL_HEAP_SIZE` in FreeRTOSConfig.h
   - Typically 8-32 KB (much larger)
   - Used by FreeRTOS tasks, queues, semaphores, etc.

**Issue:** C++ objects (std::vector, std::function, lambdas, classes with vtables) try to allocate from the tiny 512-byte system heap, causing:
- Heap exhaustion
- Heap corruption
- Hard faults with invalid PC addresses
- Unpredictable crashes

### The Solution

The FreeRTOS C++ allocator overrides the global `operator new/delete` to redirect all C++ allocations to the FreeRTOS heap.

**Files:**
- `include/chipz/platform/freertos/cpp_allocator.hpp` - Documentation header
- `src/platform/freertos/cpp_allocator.cpp` - Implementation (overrides new/delete)

### How to Use

#### 1. Enable in Your Project

In your project's CMakeLists.txt:

```cmake
# Enable FreeRTOS platform support
set(CHIPZ_ENABLE_FREERTOS ON CACHE BOOL "" FORCE)

# Add chipz library
add_subdirectory(libs)

# Link the platform library
target_link_libraries(your_app
    chipz::chipz
    chipz::platform::freertos  # FreeRTOS C++ allocator
)
```

#### 2. Configure FreeRTOS Heap

Increase the FreeRTOS heap size in `FreeRTOSConfig.h`:

```c
// Increase from default (often 8192) to support C++ allocations
#define configTOTAL_HEAP_SIZE  ((size_t)32768)  // 32KB recommended
```

#### 3. Use Compatible Heap Implementation

The allocator requires a FreeRTOS heap that supports both allocation and deallocation:

**Compatible:**
- ✅ **heap_4** (Best choice - coalescing, low fragmentation)
- ✅ **heap_5** (Multiple memory regions)
- ✅ heap_2 (Legacy, no coalescing)

**NOT Compatible:**
- ❌ heap_1 (Allocate-only, no free support)
- ❌ heap_3 (Wraps malloc/free, creates circular dependency)

Configure in your STM32CubeMX project or set manually in CMakeLists.txt.

### What Gets Overridden

The allocator overrides these C++ operators:

```cpp
void* operator new(size_t size);                  // Single object
void* operator new[](size_t size);                // Array allocation
void operator delete(void* ptr) noexcept;         // Single object free
void operator delete[](void* ptr) noexcept;       // Array free
void operator delete(void* ptr, size_t) noexcept; // C++14 sized delete
void operator delete[](void* ptr, size_t) noexcept;
```

All redirect to `pvPortMalloc()` / `vPortFree()`.

### Technical Details

#### Build Order Dependency

The platform library needs FreeRTOS headers. Ensure STM32CubeMX sources are added **before** the chipz library:

```cmake
# CORRECT order
add_subdirectory(cmake/stm32cubemx)  # First - provides FreeRTOS headers
add_subdirectory(libs)                # Second - uses FreeRTOS headers

# WRONG order
add_subdirectory(libs)                # Will fail - FreeRTOS.h not found
add_subdirectory(cmake/stm32cubemx)
```

The chipz CMakeLists.txt automatically detects the `stm32cubemx` target and links to it for include paths.

#### Memory Tracking

With the allocator enabled, you can monitor FreeRTOS heap usage:

```cpp
// Get current free heap
size_t free_heap = xPortGetFreeHeapSize();

// Get minimum ever free heap (detect peak usage)
size_t min_free = xPortGetMinimumEverFreeHeapSize();

printf("Heap free: %zu bytes, Peak used: %zu bytes\n",
       free_heap, configTOTAL_HEAP_SIZE - min_free);
```

### Troubleshooting

#### Build Error: "FreeRTOS.h: No such file or directory"

**Cause:** STM32CubeMX subdirectory added after chipz library.

**Fix:** Reorder subdirectories in CMakeLists.txt:
```cmake
add_subdirectory(cmake/stm32cubemx)  # Must be first
add_subdirectory(libs)
```

#### Runtime Error: Hard fault with PC in RAM

**Cause:** Still using old allocator, or heap exhausted.

**Fix:**
1. Verify `chipz::platform::freertos` is linked
2. Remove any local `cpp_alloc.cpp` files
3. Increase `configTOTAL_HEAP_SIZE`

#### Link Error: Multiple definition of 'operator new'

**Cause:** Local cpp_alloc.cpp still being compiled.

**Fix:** Remove from your sources:
```cmake
# Remove this line if it exists
# Core/Src/cpp_alloc.cpp
```

## Future Platform Support

The platform infrastructure is designed to support multiple RTOSes:

```
libs/src/platform/
├── freertos/
│   └── cpp_allocator.cpp
├── zephyr/         (future)
│   └── cpp_allocator.cpp
├── threadx/        (future)
│   └── cpp_allocator.cpp
└── bare_metal/     (future)
    └── cpp_allocator.cpp
```

Each platform can provide:
- C++ memory allocators
- Threading primitives
- Timing functions
- Platform-specific optimizations

To add a new platform:
1. Create `src/platform/<platform_name>/`
2. Add option in `libs/CMakeLists.txt`:
   ```cmake
   option(CHIPZ_ENABLE_<PLATFORM> "Enable <platform> support" OFF)
   ```
3. Add conditional library target
4. Create documentation in this file

## Example Integration

See the main fireplace project for a complete example:

```cmake
# fireplace-cpp/CMakeLists.txt
project(fireplace)

add_executable(fireplace)

# Order matters!
add_subdirectory(cmake/stm32cubemx)

set(CHIPZ_ENABLE_FREERTOS ON CACHE BOOL "" FORCE)
add_subdirectory(libs)

target_sources(fireplace PRIVATE
    Components/logic/fireplace.cpp  # Uses chipz devices
    # ... other sources
)

target_link_libraries(fireplace
    stm32cubemx
    chipz::chipz
    chipz::platform::freertos  # C++ allocator
)
```

## References

- [FreeRTOS Memory Management](https://www.freertos.org/a00111.html)
- [Chipz Library README](README.md)
- [Chipz AI Assistant Guide](CLAUDE.md)
