// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

/**
 * @file fault_handlers.hpp
 * @brief Cortex-M33 fault handler data structures and declarations.
 *
 * FaultInfo is placed in a .noinit section so its contents survive a warm
 * reset (watchdog, software reset). The magic field distinguishes a valid
 * capture from uninitialised memory.
 *
 * How to use
 * ----------
 * 1. Add fault_handlers.cpp to your project's source list alongside
 *    chipz_isrs.cpp.
 * 2. Remove the HardFault_Handler / MemManage_Handler / BusFault_Handler /
 *    UsageFault_Handler stubs from chipz_isrs.cpp (or they will conflict).
 * 3. Optionally implement chipz_fault_report() in your application to emit
 *    the dump over UART or ITM/SWO immediately at fault time.
 * 4. On the next boot after a fault, check g_fault_info.magic == kFaultMagic
 *    and forward the struct to fault_decoder.py for analysis.
 *
 * Linker script
 * -------------
 * g_fault_info is placed in the ".noinit" section so the C startup library
 * does not zero it across warm resets.  Add the following to your linker
 * script if it is not already present:
 *
 *   .noinit (NOLOAD) :
 *   {
 *     KEEP(*(.noinit))
 *   } >RAM
 *
 * Without this section the fault data is still captured but may be erased
 * when the CPU resets and the startup code initialises BSS.
 *
 * Python decoder
 * --------------
 * Use tools/fault_decoder.py to decode a FaultInfo dump:
 *
 *   python fault_decoder.py --elf firmware.elf --map firmware.map \
 *                           --binary fault_dump.bin
 *
 * The struct layout uses only uint32_t fields so the decoder can parse it
 * with no padding ambiguity regardless of toolchain or host architecture.
 */

#pragma once

#include <cstdint>

namespace chipz {
namespace port {
namespace stm32h5xx {

/// Identifies which exception produced the capture.
enum class FaultType : uint32_t {
    HardFault  = 0,
    MemManage  = 1,
    BusFault   = 2,
    UsageFault = 3,
};

/**
 * Complete register snapshot captured at the moment of a Cortex-M33 fault.
 *
 * All fields are uint32_t so that the Python decoder can parse the binary
 * image with struct.unpack('<71I', ...) without alignment or padding concerns.
 *
 * Field offsets (byte offset from start of struct, word index in parentheses):
 *
 *   magic        0x000  ( 0)
 *   fault_type   0x004  ( 1)
 *   r0           0x008  ( 2)
 *   r1           0x00C  ( 3)
 *   r2           0x010  ( 4)
 *   r3           0x014  ( 5)
 *   r12          0x018  ( 6)
 *   lr           0x01C  ( 7)   return address at fault
 *   pc           0x020  ( 8)   faulting / next instruction
 *   xpsr         0x024  ( 9)
 *   s[0..15]     0x028  (10–25) FPU S0–S15 (valid when fpu_active==1)
 *   fpscr        0x068  (26)
 *   fpu_active   0x06C  (27)   1 = S0–S15 / FPSCR are valid
 *   sp           0x070  (28)   active stack pointer at fault entry
 *   exc_return   0x074  (29)   EXC_RETURN in LR on exception entry
 *   msp          0x078  (30)
 *   psp          0x07C  (31)
 *   control      0x080  (32)   CONTROL register (FPCA, SPSEL, nPRIV)
 *   cfsr         0x084  (33)   Configurable Fault Status (MMFSR|BFSR|UFSR)
 *   hfsr         0x088  (34)   HardFault Status
 *   dfsr         0x08C  (35)   Debug Fault Status
 *   mmfar        0x090  (36)   MemManage Fault Address (valid when cfsr[MMARVALID])
 *   bfar         0x094  (37)   BusFault Address (valid when cfsr[BFARVALID])
 *   afsr         0x098  (38)   Auxiliary Fault Status (implementation-defined)
 *   stack_snapshot[0..31]
 *                0x09C  (39–70) 32 words from fault-entry SP upward
 *
 * Total: 71 words = 284 bytes.
 */
struct FaultInfo {
    // Sentinel written last — marks the capture as complete and valid.
    uint32_t magic;

    // Fault classification (see FaultType).
    uint32_t fault_type;

    // -----------------------------------------------------------------------
    // Exception frame pushed by the hardware onto MSP or PSP on entry.
    // -----------------------------------------------------------------------
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;    ///< LR at the time of the fault (caller return address).
    uint32_t pc;    ///< PC at the time of the fault (faulting instruction).
    uint32_t xpsr;  ///< Program Status Register.

    // -----------------------------------------------------------------------
    // FPU extension frame (valid only when fpu_active == 1).
    // The hardware pushes S0–S15 and FPSCR when FPCA == 1 in CONTROL
    // (extended exception frame). EXC_RETURN bit 4 == 0 signals this case.
    // -----------------------------------------------------------------------
    uint32_t s[16];       ///< Floating-point registers S0–S15.
    uint32_t fpscr;       ///< Floating-Point Status and Control Register.
    uint32_t fpu_active;  ///< 1 when S0–S15 / FPSCR above are valid.

    // -----------------------------------------------------------------------
    // Stack pointer and execution mode at the time of the fault.
    // -----------------------------------------------------------------------
    uint32_t sp;          ///< Active stack pointer (MSP or PSP) at fault entry.
    uint32_t exc_return;  ///< EXC_RETURN value in LR on exception entry.
    uint32_t msp;         ///< Main Stack Pointer.
    uint32_t psp;         ///< Process Stack Pointer.
    uint32_t control;     ///< CONTROL register (FPCA, SPSEL, nPRIV).

    // -----------------------------------------------------------------------
    // SCB fault status registers.
    // -----------------------------------------------------------------------
    uint32_t cfsr;   ///< Configurable Fault Status (MMFSR | BFSR | UFSR).
    uint32_t hfsr;   ///< HardFault Status Register.
    uint32_t dfsr;   ///< Debug Fault Status Register.
    uint32_t mmfar;  ///< MemManage Fault Address (valid when cfsr[MMARVALID]).
    uint32_t bfar;   ///< BusFault Address Register (valid when cfsr[BFARVALID]).
    uint32_t afsr;   ///< Auxiliary Fault Status Register.

    // -----------------------------------------------------------------------
    // Raw stack snapshot for call-chain reconstruction.
    // Covers kStackSnapshotWords words starting from the fault-entry SP,
    // which includes the exception frame at [0..7] (or [0..25] with FPU).
    // -----------------------------------------------------------------------
    static constexpr uint32_t kStackSnapshotWords = 32u;
    uint32_t                  stack_snapshot[kStackSnapshotWords];
};

static_assert(sizeof(FaultInfo) == 71 * sizeof(uint32_t),
              "FaultInfo size mismatch — update fault_decoder.py struct format");

/// Written to FaultInfo::magic only after a complete, valid capture.
static constexpr uint32_t kFaultMagic = 0xDEAD'C0DEu;

/// Fault capture in .noinit RAM — survives warm resets.
extern FaultInfo g_fault_info;

}  // namespace stm32h5xx
}  // namespace port
}  // namespace chipz

/**
 * Application-overridable report hook called after the fault registers have
 * been captured.  The weak default is a no-op.  Override to forward the dump
 * over UART, ITM/SWO, or any other channel before the CPU halts.
 *
 * This function is called from a fault handler context — avoid allocating
 * memory or calling code that might itself fault.
 *
 * Example:
 * @code
 *   extern "C"
 *   void chipz_fault_report(const chipz::port::stm32h5xx::FaultInfo& info) {
 *       char buf[32];
 *       snprintf(buf, sizeof(buf), "FAULT pc=%08lx\r\n", info.pc);
 *       HAL_UART_Transmit(&huart2, (uint8_t*)buf, strlen(buf), 50);
 *   }
 * @endcode
 */
extern "C" void chipz_fault_report(const chipz::port::stm32h5xx::FaultInfo& info);
