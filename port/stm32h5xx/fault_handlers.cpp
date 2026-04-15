// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

/**
 * @file fault_handlers.cpp
 * @brief Cortex-M33 HardFault, MemManage, BusFault, and UsageFault handlers.
 *
 * Each handler is a two-stage trampoline.
 *
 * Stage 1 — naked assembly
 * ~~~~~~~~~~~~~~~~~~~~~~~~
 * Runs before the compiler can corrupt any registers.  Determines which stack
 * (MSP / PSP) was active by inspecting bit 2 of EXC_RETURN (which the
 * hardware loads into LR on exception entry), then branches to the common C
 * handler with three arguments in R0–R2 (AAPCS):
 *
 *   R0 = pointer to the stacked exception frame
 *   R1 = EXC_RETURN value
 *   R2 = FaultType enum value (0–3)
 *
 * Stage 2 — C handler (chipz_fault_handler_c)
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Reads the stacked exception frame, the optional FPU extension frame, all
 * SCB fault-status registers, a raw stack snapshot, and stores everything in
 * g_fault_info (placed in .noinit so it survives a warm reset).  Then it
 * calls the weak chipz_fault_report() hook and halts with a BKPT instruction
 * so that an attached debugger catches the fault immediately.
 *
 * Notes
 * -----
 * - Include this file alongside chipz_isrs.cpp.
 * - Remove the stub handlers (HardFault_Handler etc.) from chipz_isrs.cpp —
 *   both files defining the same symbol is a linker error.
 * - BKPT(0) with no debugger attached causes a Lockup on Cortex-M33 which
 *   is reset by the watchdog — acceptable for a fault handler.
 *
 * EXC_RETURN bit meaning (Cortex-M33, no TrustZone)
 * --------------------------------------------------
 *  Bit  2 (SPSEL)  0 = MSP was active stack, 1 = PSP
 *  Bit  3 (Mode)   0 = returned from Handler, 1 = Thread mode
 *  Bit  4 (FType)  0 = extended frame (FPU pushed S0–S15/FPSCR), 1 = standard
 *  Bits 31:28      All 1s (0xF) on non-TrustZone devices
 *
 * Stacked exception frame layout
 * --------------------------------
 * Standard frame (EXC_RETURN bit 4 == 1):
 *   frame[0]  R0          frame[4]  R12
 *   frame[1]  R1          frame[5]  LR (at fault)
 *   frame[2]  R2          frame[6]  PC (at fault)
 *   frame[3]  R3          frame[7]  xPSR
 *
 * Extended frame (EXC_RETURN bit 4 == 0, FPU was active):
 *   frame[0..7]   as above
 *   frame[8..23]  S0–S15
 *   frame[24]     FPSCR
 *   frame[25]     reserved (alignment padding)
 */

#include "fault_handlers.hpp"
#include "stm32h5xx_hal.h"

namespace chipz {
namespace port {
namespace stm32h5xx {

// Placed in .noinit so the C startup library does not zero it on reset.
// Survives watchdog and software resets; contents remain valid across boots.
__attribute__((section(".noinit")))
FaultInfo g_fault_info;

} // namespace stm32h5xx
} // namespace port
} // namespace chipz

// ---------------------------------------------------------------------------
// Default (weak) report hook.
// Replace in application code to emit the capture over UART/ITM before halt.
// ---------------------------------------------------------------------------

extern "C" __attribute__((weak))
void chipz_fault_report(const chipz::port::stm32h5xx::FaultInfo&) {}

// ---------------------------------------------------------------------------
// Common C handler — called by each assembly trampoline.
//
//   frame      Pointer to the base of the stacked exception frame.
//   exc_ret    EXC_RETURN value (was in LR on exception entry).
//   fault_val  FaultType enum value cast to uint32_t.
// ---------------------------------------------------------------------------

extern "C"
void chipz_fault_handler_c(uint32_t* frame, uint32_t exc_ret, uint32_t fault_val)
{
    using namespace chipz::port::stm32h5xx;

    FaultInfo& f = g_fault_info;

    // --- Standard exception frame (always present) -------------------------
    f.r0   = frame[0];
    f.r1   = frame[1];
    f.r2   = frame[2];
    f.r3   = frame[3];
    f.r12  = frame[4];
    f.lr   = frame[5];
    f.pc   = frame[6];
    f.xpsr = frame[7];

    // --- FPU extension frame -----------------------------------------------
    // EXC_RETURN bit 4 == 0 means the hardware pushed S0–S15 and FPSCR on top
    // of the standard frame (extended frame, 26 words total instead of 8).
    f.fpu_active = ((exc_ret & 0x10U) == 0U) ? 1U : 0U;
    if (f.fpu_active) {
        for (unsigned i = 0U; i < 16U; ++i) {
            f.s[i] = frame[8U + i];
        }
        f.fpscr = frame[24U];
    } else {
        for (unsigned i = 0U; i < 16U; ++i) { f.s[i] = 0U; }
        f.fpscr = 0U;
    }

    // --- Stack pointer and mode --------------------------------------------
    f.sp         = reinterpret_cast<uint32_t>(frame);
    f.exc_return = exc_ret;
    __asm volatile ("mrs %0, msp"     : "=r" (f.msp));
    __asm volatile ("mrs %0, psp"     : "=r" (f.psp));
    __asm volatile ("mrs %0, control" : "=r" (f.control));

    // --- SCB fault-status registers ----------------------------------------
    // Read and clear CFSR/HFSR to prevent stale bits from accumulating across
    // multiple faults.  The original values are already stored in f.cfsr/f.hfsr.
    f.cfsr  = SCB->CFSR;
    f.hfsr  = SCB->HFSR;
    f.dfsr  = SCB->DFSR;
    f.mmfar = SCB->MMFAR; // valid only when f.cfsr bit MMARVALID (bit 7) is set
    f.bfar  = SCB->BFAR;  // valid only when f.cfsr bit BFARVALID (bit 15) is set
    f.afsr  = SCB->AFSR;

    // Clear sticky bits so the next reset starts clean (W1C registers).
    SCB->CFSR = f.cfsr;
    SCB->HFSR = f.hfsr;

    // --- Raw stack snapshot ------------------------------------------------
    // Captures kStackSnapshotWords words starting from the exception frame
    // base (frame[0] == R0).  The Python decoder uses this to walk the call
    // chain by identifying word-aligned values that fall in flash (.text).
    for (uint32_t i = 0U; i < FaultInfo::kStackSnapshotWords; ++i) {
        f.stack_snapshot[i] = frame[i];
    }

    // --- Commit ------------------------------------------------------------
    // Write magic last so that a partial capture (e.g., fault while filling)
    // is never mistaken for a valid record by the decoder.
    f.fault_type = fault_val;
    f.magic      = kFaultMagic;

    // --- Application hook (UART, ITM, …) -----------------------------------
    chipz_fault_report(f);

    // --- Halt --------------------------------------------------------------
    // BKPT suspends execution when a debugger is attached (J-Link, ST-LINK).
    // With no debugger, the CPU enters Lockup and is reset by the watchdog.
    __BKPT(0);
    while (true) {}
}

// ---------------------------------------------------------------------------
// Assembly trampolines — naked so the compiler emits no prologue that could
// modify SP or LR before the assembly runs.
//
// Sequence for each handler:
//   1. Test EXC_RETURN bit 2 to select MSP or PSP into R0.
//   2. Copy EXC_RETURN into R1.
//   3. Load fault-type constant into R2.
//   4. Branch (tail-call) to chipz_fault_handler_c.
// ---------------------------------------------------------------------------

extern "C" {

__attribute__((naked))
void HardFault_Handler()
{
    __asm volatile (
        "tst   lr, #4                \n"
        "ite   eq                    \n"
        "mrseq r0, msp               \n"  // bit 2 == 0: MSP was active
        "mrsne r0, psp               \n"  // bit 2 == 1: PSP was active
        "mov   r1, lr                \n"  // R1 = EXC_RETURN
        "movs  r2, #0                \n"  // R2 = FaultType::HardFault
        "b     chipz_fault_handler_c \n"
    );
}

__attribute__((naked))
void MemManage_Handler()
{
    __asm volatile (
        "tst   lr, #4                \n"
        "ite   eq                    \n"
        "mrseq r0, msp               \n"
        "mrsne r0, psp               \n"
        "mov   r1, lr                \n"
        "movs  r2, #1                \n"  // R2 = FaultType::MemManage
        "b     chipz_fault_handler_c \n"
    );
}

__attribute__((naked))
void BusFault_Handler()
{
    __asm volatile (
        "tst   lr, #4                \n"
        "ite   eq                    \n"
        "mrseq r0, msp               \n"
        "mrsne r0, psp               \n"
        "mov   r1, lr                \n"
        "movs  r2, #2                \n"  // R2 = FaultType::BusFault
        "b     chipz_fault_handler_c \n"
    );
}

__attribute__((naked))
void UsageFault_Handler()
{
    __asm volatile (
        "tst   lr, #4                \n"
        "ite   eq                    \n"
        "mrseq r0, msp               \n"
        "mrsne r0, psp               \n"
        "mov   r1, lr                \n"
        "movs  r2, #3                \n"  // R2 = FaultType::UsageFault
        "b     chipz_fault_handler_c \n"
    );
}

} // extern "C"
