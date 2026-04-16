#!/usr/bin/env python3
"""
chipz Cortex-M33 fault decoder
================================
Parses a FaultInfo snapshot captured by the chipz fault handlers and resolves
addresses to source locations using an ELF and MAP file from an arm-none-eabi
build.

Usage
-----
    python fault_decoder.py --elf firmware.elf --map firmware.map \\
                            --binary fault_dump.bin

    python fault_decoder.py --elf firmware.elf \\
                            --gdb fault_gdb.txt

    python fault_decoder.py --elf firmware.elf \\
                            --hex "DE C0 AD DE 00 00 00 00 ..."

Input formats
-------------
--binary <file>     Raw binary dump of the FaultInfo struct (284 bytes).
                    Obtain with GDB:
                        (gdb) dump binary memory dump.bin &g_fault_info \\
                                    ((char*)&g_fault_info + sizeof(g_fault_info))

--gdb <file>        Text file containing GDB memory dump output.
                    Obtain with GDB:
                        (gdb) set logging on
                        (gdb) x/71xw &g_fault_info
                        (gdb) set logging off
                    The file may contain other GDB output — only lines with
                    hex words are parsed.

--hex <string or file>
                    Space/comma/newline-separated hex bytes or words.
                    Each token is treated as a uint32_t (little-endian host
                    representation).  Prefix "0x" is optional.

Requirements
------------
    arm-none-eabi-addr2line  in PATH  (resolve addresses to file:line)
    arm-none-eabi-nm         in PATH  (optional, symbol lookup fallback)

Both tools ship with the arm-none-eabi-gcc toolchain from Arm or apt/brew.
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


# ---------------------------------------------------------------------------
# FaultInfo struct layout
# ---------------------------------------------------------------------------
# Must match chipz/port/stm32h5xx/fault_handlers.hpp exactly.
# All fields are uint32_t — 71 words = 284 bytes total.

FAULT_INFO_WORDS = 71
FAULT_INFO_SIZE  = FAULT_INFO_WORDS * 4
FAULT_INFO_FMT   = f"<{FAULT_INFO_WORDS}I"  # little-endian unsigned ints

FAULT_MAGIC = 0xDEAD_C0DE

FAULT_TYPES = {0: "HardFault", 1: "MemManage", 2: "BusFault", 3: "UsageFault"}

# Word indices within the unpacked tuple (see fault_handlers.hpp field table)
F_MAGIC      = 0
F_FAULT_TYPE = 1
F_R0         = 2
F_R1         = 3
F_R2         = 4
F_R3         = 5
F_R12        = 6
F_LR         = 7
F_PC         = 8
F_XPSR       = 9
F_S          = slice(10, 26)   # S0–S15 (16 words)
F_FPSCR      = 26
F_FPU_ACTIVE = 27
F_SP         = 28
F_EXC_RETURN = 29
F_MSP        = 30
F_PSP        = 31
F_CONTROL    = 32
F_CFSR       = 33
F_HFSR       = 34
F_DFSR       = 35
F_MMFAR      = 36
F_BFAR       = 37
F_AFSR       = 38
F_SNAPSHOT   = slice(39, 71)   # stack_snapshot[0..31] (32 words)


@dataclass
class FaultInfo:
    """Decoded FaultInfo struct."""
    magic:       int
    fault_type:  int
    r0:  int; r1: int; r2: int; r3: int; r12: int
    lr:  int
    pc:  int
    xpsr: int
    s:          list[int]
    fpscr:      int
    fpu_active: int
    sp:          int
    exc_return:  int
    msp:         int
    psp:         int
    control:     int
    cfsr:  int
    hfsr:  int
    dfsr:  int
    mmfar: int
    bfar:  int
    afsr:  int
    stack_snapshot: list[int]

    @classmethod
    def from_words(cls, w: tuple[int, ...]) -> "FaultInfo":
        return cls(
            magic       = w[F_MAGIC],
            fault_type  = w[F_FAULT_TYPE],
            r0=w[F_R0], r1=w[F_R1], r2=w[F_R2], r3=w[F_R3], r12=w[F_R12],
            lr          = w[F_LR],
            pc          = w[F_PC],
            xpsr        = w[F_XPSR],
            s           = list(w[F_S]),
            fpscr       = w[F_FPSCR],
            fpu_active  = w[F_FPU_ACTIVE],
            sp          = w[F_SP],
            exc_return  = w[F_EXC_RETURN],
            msp         = w[F_MSP],
            psp         = w[F_PSP],
            control     = w[F_CONTROL],
            cfsr        = w[F_CFSR],
            hfsr        = w[F_HFSR],
            dfsr        = w[F_DFSR],
            mmfar       = w[F_MMFAR],
            bfar        = w[F_BFAR],
            afsr        = w[F_AFSR],
            stack_snapshot = list(w[F_SNAPSHOT]),
        )


# ---------------------------------------------------------------------------
# Register bit decoders
# ---------------------------------------------------------------------------

def decode_cfsr(cfsr: int) -> list[str]:
    """Return a list of set CFSR bit names with descriptions."""
    bits = []

    # MMFSR — bits [7:0]
    mmfsr_bits = [
        (7, "MMARVALID",   "MMFAR holds a valid fault address"),
        (5, "MLSPERR",     "MemManage fault during lazy FP state preservation"),
        (4, "MSTKERR",     "MemManage fault on exception stacking"),
        (3, "MUNSTKERR",   "MemManage fault on exception unstacking"),
        (1, "DACCVIOL",    "Data access violation (read MMFAR for address)"),
        (0, "IACCVIOL",    "Instruction access violation"),
    ]
    for bit, name, desc in mmfsr_bits:
        if cfsr & (1 << bit):
            bits.append(f"MMFSR.{name}: {desc}")

    # BFSR — bits [15:8]
    bfsr_bits = [
        (15, "BFARVALID",   "BFAR holds a valid fault address"),
        (13, "LSPERR",      "BusFault during lazy FP state preservation"),
        (12, "STKERR",      "BusFault on exception stacking"),
        (11, "UNSTKERR",    "BusFault on exception unstacking"),
        (10, "IMPRECISERR", "Imprecise data bus error (BFAR may not be valid)"),
        ( 9, "PRECISERR",   "Precise data bus error (read BFAR for address)"),
        ( 8, "IBUSERR",     "Instruction bus error"),
    ]
    for bit, name, desc in bfsr_bits:
        if cfsr & (1 << bit):
            bits.append(f"BFSR.{name}: {desc}")

    # UFSR — bits [31:16]
    ufsr_bits = [
        (25, "DIVBYZERO",   "Divide by zero (CCR.DIV_0_TRP must be set)"),
        (24, "UNALIGNED",   "Unaligned memory access"),
        (20, "STKOF",       "Stack overflow (Cortex-M33 specific)"),
        (19, "NOCP",        "No coprocessor — FPU instruction without FPU enabled"),
        (18, "INVPC",       "Invalid PC on exception return (LR was corrupted)"),
        (17, "INVSTATE",    "Invalid CPU state (EPSR.T=0 or EPSR.IT!=0)"),
        (16, "UNDEFINSTR",  "Undefined instruction"),
    ]
    for bit, name, desc in ufsr_bits:
        if cfsr & (1 << bit):
            bits.append(f"UFSR.{name}: {desc}")

    return bits


def decode_hfsr(hfsr: int) -> list[str]:
    """Return a list of set HFSR bit names with descriptions."""
    bits = []
    if hfsr & (1 << 31):
        bits.append("DEBUGEVT: Triggered by a debug event (BKPT or watchpoint)")
    if hfsr & (1 << 30):
        bits.append("FORCED:   Escalated to HardFault from a configurable fault "
                    "(MemManage/BusFault/UsageFault was not enabled or nested)")
    if hfsr & (1 << 1):
        bits.append("VECTTBL:  Vector table read fault (bad VTOR or corrupted table)")
    return bits


def decode_exc_return(exc_return: int) -> list[str]:
    """Decode the EXC_RETURN value into human-readable properties."""
    lines = []
    spsel   = (exc_return >> 2) & 1
    mode    = (exc_return >> 3) & 1
    ftype   = (exc_return >> 4) & 1
    secure  = (exc_return >> 6) & 1

    lines.append(f"  Stack used:   {'PSP (Thread stack)' if spsel  else 'MSP (Main/Handler stack)'}")
    lines.append(f"  Return to:    {'Thread mode'        if mode   else 'Handler mode'}")
    lines.append(f"  Frame type:   {'Standard (no FPU)'  if ftype  else 'Extended (FPU S0–S15 stacked)'}")
    if (exc_return >> 28) == 0xF:
        lines.append(f"  Security:     {'Secure'             if secure else 'Non-Secure'} (TrustZone active)")
    return lines


def decode_xpsr(xpsr: int) -> str:
    """Decode xPSR into a readable string."""
    isr = xpsr & 0x1FF
    n   = (xpsr >> 31) & 1
    z   = (xpsr >> 30) & 1
    c   = (xpsr >> 29) & 1
    v   = (xpsr >> 28) & 1
    t   = (xpsr >> 24) & 1
    isr_names = {0: "Thread", 2: "NMI", 3: "HardFault", 4: "MemManage",
                 5: "BusFault", 6: "UsageFault", 11: "SVCall",
                 12: "DebugMon", 14: "PendSV", 15: "SysTick"}
    isr_str = isr_names.get(isr, f"IRQ{isr - 16}" if isr >= 16 else f"Reserved({isr})")
    flags = f"{'N' if n else '-'}{'Z' if z else '-'}{'C' if c else '-'}{'V' if v else '-'}"
    thumb = "Thumb" if t else "ARM(!)"
    return f"ISR={isr_str}  flags={flags}  {thumb}"


def decode_control(control: int) -> str:
    """Decode CONTROL register."""
    npriv = control & 1
    spsel = (control >> 1) & 1
    fpca  = (control >> 2) & 1
    return (f"nPRIV={'Unprivileged' if npriv else 'Privileged'}  "
            f"SPSEL={'PSP' if spsel else 'MSP'}  "
            f"FPCA={'FPU active' if fpca else 'FPU inactive'}")


# ---------------------------------------------------------------------------
# Input parsers
# ---------------------------------------------------------------------------

def load_binary(path: str) -> bytes:
    data = Path(path).read_bytes()
    if len(data) < FAULT_INFO_SIZE:
        sys.exit(f"ERROR: Binary file is {len(data)} bytes; expected at least "
                 f"{FAULT_INFO_SIZE} bytes for FaultInfo.")
    return data[:FAULT_INFO_SIZE]


def load_gdb(path: str) -> bytes:
    """Parse GDB x/Nxw output into raw bytes.

    Accepts output like:
        0x20001000 <g_fault_info>: 0xdeadc0de  0x00000000  0x08001234  ...
        0x20001010 <g_fault_info+16>: 0x00000000  ...
    """
    raw = Path(path).read_text(errors="replace")
    # Extract all hex words (0x followed by exactly 8 hex digits)
    words = re.findall(r"0x([0-9a-fA-F]{8})", raw)
    if len(words) < FAULT_INFO_WORDS:
        sys.exit(f"ERROR: GDB dump contains {len(words)} hex words; "
                 f"need {FAULT_INFO_WORDS} for FaultInfo.")
    data = b"".join(int(w, 16).to_bytes(4, "little") for w in words[:FAULT_INFO_WORDS])
    return data


def load_hex(text_or_path: str) -> bytes:
    """Parse a hex string (bytes or words) into raw bytes."""
    # Try treating as a file path first
    p = Path(text_or_path)
    if p.exists():
        text = p.read_text(errors="replace")
    else:
        text = text_or_path

    # Split on whitespace, commas, or semicolons; strip 0x prefix
    tokens = re.split(r"[\s,;]+", text.strip())
    tokens = [t for t in tokens if t]
    values = []
    for tok in tokens:
        tok = tok.lstrip("0xX")
        if not tok:
            continue
        values.append(int(tok, 16))

    # If tokens look like bytes (≤ 2 hex digits or value ≤ 0xFF), pack as bytes
    if all(v <= 0xFF for v in values):
        data = bytes(values)
    else:
        # Assume uint32_t values
        data = b"".join(v.to_bytes(4, "little") for v in values)

    if len(data) < FAULT_INFO_SIZE:
        sys.exit(f"ERROR: Hex input produced {len(data)} bytes; "
                 f"need {FAULT_INFO_SIZE} for FaultInfo.")
    return data[:FAULT_INFO_SIZE]


def unpack_fault_info(data: bytes) -> FaultInfo:
    words = struct.unpack(FAULT_INFO_FMT, data)
    return FaultInfo.from_words(words)


# ---------------------------------------------------------------------------
# Address resolution
# ---------------------------------------------------------------------------

def addr2line(elf: str, addr: int, tool_prefix: str = "arm-none-eabi-") -> Optional[str]:
    """Call addr2line and return 'function\nfile:line' or None on failure."""
    tool = f"{tool_prefix}addr2line"
    try:
        result = subprocess.run(
            [tool, "-e", elf, "-f", "-C", "-p", f"0x{addr:08x}"],
            capture_output=True, text=True, timeout=5,
        )
        out = result.stdout.strip()
        if out and "??" not in out:
            return out
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass
    return None


def nm_symbols(elf: str, tool_prefix: str = "arm-none-eabi-") -> list[tuple[int, int, str]]:
    """Return list of (start_addr, size, name) for text symbols via nm."""
    tool = f"{tool_prefix}nm"
    try:
        result = subprocess.run(
            [tool, "-S", "--size-sort", elf],
            capture_output=True, text=True, timeout=10,
        )
        syms = []
        for line in result.stdout.splitlines():
            parts = line.split()
            if len(parts) >= 4 and parts[2].lower() in ("t", "w"):
                try:
                    syms.append((int(parts[0], 16), int(parts[1], 16), parts[3]))
                except ValueError:
                    pass
        return sorted(syms)
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return []


def find_symbol_in_map(map_path: str, addr: int) -> Optional[str]:
    """Find the nearest symbol at or before addr in a GNU ld MAP file."""
    text = Path(map_path).read_text(errors="replace")

    # Collect (address, symbol_name) pairs from the map
    # GNU ld MAP lines look like:
    #   0x0800abcd                symbol_name
    symbols: list[tuple[int, str]] = []
    for line in text.splitlines():
        m = re.match(r"^\s+(0x[0-9a-fA-F]+)\s+(\S+)\s*$", line)
        if m:
            try:
                symbols.append((int(m.group(1), 16), m.group(2)))
            except ValueError:
                pass

    if not symbols:
        return None

    symbols.sort(key=lambda x: x[0])
    best: Optional[tuple[int, str]] = None
    for sym_addr, sym_name in symbols:
        if sym_addr <= addr:
            best = (sym_addr, sym_name)
        else:
            break

    if best:
        offset = addr - best[0]
        return f"{best[1]}+0x{offset:x}" if offset else best[1]
    return None


def is_flash_address(addr: int) -> bool:
    """Return True if the address looks like STM32H5xx flash (0x0800_0000…)."""
    return 0x0800_0000 <= addr < 0x0900_0000


def resolve(addr: int, elf: Optional[str], map_path: Optional[str],
            tool_prefix: str, nm_cache: Optional[list]) -> str:
    """Resolve an address to a source description string."""
    if addr == 0 or addr == 0xFFFF_FFFF:
        return "(null)"

    if elf:
        resolved = addr2line(elf, addr, tool_prefix)
        if resolved:
            return resolved

    if map_path:
        sym = find_symbol_in_map(map_path, addr)
        if sym:
            return f"<{sym}>"

    if elf and nm_cache is not None:
        for start, size, name in reversed(nm_cache):
            if start <= addr < start + max(size, 1):
                return f"<{name}+0x{addr-start:x}>"

    return f"<unresolved>"


# ---------------------------------------------------------------------------
# Source context display
# ---------------------------------------------------------------------------

def show_source_context(location: str, src_root: Optional[str],
                        context_lines: int = 5) -> None:
    """Print source context around a file:line location from addr2line output.

    location may look like:
        0x08001234 at /path/to/file.cpp:42
    or (with -p flag):
        main() at src/main.cpp:42
    """
    m = re.search(r"at (.+):(\d+)", location)
    if not m:
        return

    filepath = m.group(1)
    lineno   = int(m.group(2))

    # Try absolute path first, then relative to src_root
    candidates = [Path(filepath)]
    if src_root:
        rel = Path(filepath)
        for part in rel.parts:
            candidates.append(Path(src_root) / Path(*rel.parts[rel.parts.index(part):]))

    src_file: Optional[Path] = None
    for c in candidates:
        if c.exists():
            src_file = c
            break

    if not src_file:
        return

    try:
        lines = src_file.read_text(errors="replace").splitlines()
    except OSError:
        return

    lo = max(0, lineno - context_lines - 1)
    hi = min(len(lines), lineno + context_lines)
    print()
    print(f"  {'':>4}  {src_file}")
    print(f"  {'':>4}  {'─' * 60}")
    for i, src_line in enumerate(lines[lo:hi], start=lo + 1):
        marker = ">>>" if i == lineno else "   "
        print(f"  {i:>4}  {marker} {src_line}")
    print(f"  {'':>4}  {'─' * 60}")


# ---------------------------------------------------------------------------
# Report printer
# ---------------------------------------------------------------------------

SEP = "=" * 72
SEP2 = "-" * 72


def print_report(fi: FaultInfo, elf: Optional[str], map_path: Optional[str],
                 src_root: Optional[str], tool_prefix: str) -> None:
    nm_cache = nm_symbols(elf, tool_prefix) if elf else None

    def r(addr: int) -> str:
        return resolve(addr, elf, map_path, tool_prefix, nm_cache)

    print(SEP)
    print(" chipz Cortex-M33 Fault Report")
    print(SEP)

    # --- Validity check ---
    if fi.magic != FAULT_MAGIC:
        print(f"\n  WARNING: magic = 0x{fi.magic:08X} (expected 0x{FAULT_MAGIC:08X})")
        print("  Data may be uninitialised or the struct layout has changed.\n")
    else:
        print()

    fault_name = FAULT_TYPES.get(fi.fault_type, f"Unknown({fi.fault_type})")
    print(f"  Fault type:  {fault_name}")
    print()

    # --- Faulting location ---
    print(SEP2)
    print("  Faulting location")
    print(SEP2)
    pc_resolved = r(fi.pc)
    print(f"  PC  = 0x{fi.pc:08X}  →  {pc_resolved}")
    if elf and src_root:
        show_source_context(pc_resolved, src_root)

    lr_resolved = r(fi.lr)
    print(f"  LR  = 0x{fi.lr:08X}  →  {lr_resolved}  (return address)")
    print()

    # --- Stacked registers ---
    print(SEP2)
    print("  Stacked registers (hardware exception frame)")
    print(SEP2)
    print(f"  R0  = 0x{fi.r0:08X}    R1  = 0x{fi.r1:08X}")
    print(f"  R2  = 0x{fi.r2:08X}    R3  = 0x{fi.r3:08X}")
    print(f"  R12 = 0x{fi.r12:08X}")
    print(f"  xPSR= 0x{fi.xpsr:08X}  →  {decode_xpsr(fi.xpsr)}")
    print()

    # --- Stack and mode ---
    print(SEP2)
    print("  Stack and execution mode")
    print(SEP2)
    print(f"  SP (active at fault) = 0x{fi.sp:08X}")
    print(f"  MSP                  = 0x{fi.msp:08X}")
    print(f"  PSP                  = 0x{fi.psp:08X}")
    print(f"  CONTROL              = 0x{fi.control:08X}  →  {decode_control(fi.control)}")
    print(f"  EXC_RETURN           = 0x{fi.exc_return:08X}")
    for line in decode_exc_return(fi.exc_return):
        print(f"  {line}")
    print()

    # --- FPU state ---
    if fi.fpu_active:
        print(SEP2)
        print("  FPU extension frame (S0–S15 were stacked)")
        print(SEP2)
        for i in range(0, 16, 4):
            row = "  " + "    ".join(
                f"S{i+j:02d}=0x{fi.s[i+j]:08X}" for j in range(4) if i + j < 16
            )
            print(row)
        print(f"  FPSCR = 0x{fi.fpscr:08X}")
        print()

    # --- SCB fault registers ---
    print(SEP2)
    print("  SCB fault-status registers")
    print(SEP2)
    print(f"  CFSR  = 0x{fi.cfsr:08X}")
    cfsr_bits = decode_cfsr(fi.cfsr)
    for b in cfsr_bits:
        print(f"           {b}")
    if not cfsr_bits:
        print("           (no bits set)")

    print(f"  HFSR  = 0x{fi.hfsr:08X}")
    for b in decode_hfsr(fi.hfsr):
        print(f"           {b}")

    print(f"  DFSR  = 0x{fi.dfsr:08X}")
    print(f"  AFSR  = 0x{fi.afsr:08X}")
    print()

    # --- Fault address (if valid) ---
    if fi.cfsr & (1 << 7):   # MMARVALID
        print(f"  MMFAR = 0x{fi.mmfar:08X}  ← MemManage fault address (valid)")
    if fi.cfsr & (1 << 15):  # BFARVALID
        print(f"  BFAR  = 0x{fi.bfar:08X}  ← BusFault address (valid)")
    if (fi.cfsr & ((1 << 7) | (1 << 15))):
        print()

    # --- Stack trace from snapshot ---
    print(SEP2)
    print("  Call-chain candidates (flash addresses in stack snapshot)")
    print(SEP2)
    print("  The snapshot covers 32 words from the fault-entry SP.")
    print("  Words 0–7 (or 0–25 with FPU) are the exception frame itself.")
    print()

    # Filter snapshot for addresses that look like they're in flash and are
    # Thumb-mode targets (bit 0 is set in Thumb BL targets stored on stack)
    candidates_found = False
    for i, word in enumerate(fi.stack_snapshot):
        # LR values pushed by BL have bit 0 clear but point just past the call
        # site; addresses stored as Thumb function pointers have bit 0 set.
        # Check both: strip bit 0 for lookup, display original.
        lookup_addr = word & ~1
        if is_flash_address(lookup_addr):
            label = f"  snapshot[{i:2d}] (SP+0x{i*4:02X}) = 0x{word:08X}  →  {r(lookup_addr)}"
            print(label)
            candidates_found = True

    if not candidates_found:
        print("  (no flash addresses found in snapshot — "
              "stack may have been corrupted)")
    print()

    # --- Quick diagnosis ---
    print(SEP2)
    print("  Quick diagnosis")
    print(SEP2)
    if fi.hfsr & (1 << 30):  # FORCED
        print("  ● HardFault was escalated from a lower-priority configurable fault.")
        print("    The root cause is in the CFSR bits above.")
    if fi.cfsr & (1 << 1):   # DACCVIOL
        print("  ● Data access violation — code tried to read/write a forbidden address.")
        if fi.cfsr & (1 << 7):
            print(f"    Faulting memory address: 0x{fi.mmfar:08X}")
    if fi.cfsr & (1 << 10):  # IMPRECISERR
        print("  ● Imprecise BusFault — the faulting instruction is NOT at PC.")
        print("    The actual bad access may have occurred several instructions earlier.")
        print("    Tip: enable write buffering disable (SCnSCB->ACTLR |= ACTLR_DISDEFWBUF)")
        print("    to convert to a PRECISERR and pinpoint the instruction.")
    if fi.cfsr & (1 << 9):   # PRECISERR
        print("  ● Precise BusFault — the faulting instruction is at the PC above.")
        if fi.cfsr & (1 << 15):
            print(f"    Faulting memory address: 0x{fi.bfar:08X}")
    if fi.cfsr & (1 << 16):  # UNDEFINSTR
        print("  ● Undefined instruction — possible causes:")
        print("    - FPU instruction executed but FPU not enabled (check CPACR)")
        print("    - Code/data pointer mismatch (calling a data address as code)")
        print("    - Corrupted instruction memory")
    if fi.cfsr & (1 << 20):  # STKOF
        print("  ● Stack overflow detected by the MSPLIM/PSPLIM stack limit registers.")
    if fi.cfsr & (1 << 18):  # INVPC
        print("  ● Invalid PC on exception return — LR was corrupted before return.")
        print("    Check that no code wrote over the stack between push and pop.")
    if fi.cfsr & (1 << 25):  # DIVBYZERO
        print("  ● Divide-by-zero (CCR.DIV_0_TRP is set).")
    print()
    print(SEP)


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Decode a chipz Cortex-M33 FaultInfo snapshot.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )

    # Input source (mutually exclusive)
    src = parser.add_mutually_exclusive_group(required=True)
    src.add_argument("--binary", metavar="FILE",
                     help="Raw binary dump of FaultInfo struct (284 bytes).")
    src.add_argument("--gdb", metavar="FILE",
                     help="GDB x/71xw output text file.")
    src.add_argument("--hex", metavar="HEX_OR_FILE",
                     help="Space/comma-separated hex bytes or uint32_t words "
                          "(or a file path containing them).")

    # Symbol resolution
    parser.add_argument("--elf", metavar="FILE",
                        help="arm-none-eabi ELF binary (for addr2line).")
    parser.add_argument("--map", metavar="FILE",
                        help="GNU linker MAP file (fallback symbol lookup).")
    parser.add_argument("--src", metavar="DIR",
                        help="Source root directory for source context display.")
    parser.add_argument("--tool-prefix", default="arm-none-eabi-", metavar="PREFIX",
                        help="Toolchain prefix (default: arm-none-eabi-).")
    parser.add_argument("--context-lines", type=int, default=5, metavar="N",
                        help="Source context lines above/below fault (default: 5).")

    args = parser.parse_args()

    # Load raw bytes
    if args.binary:
        data = load_binary(args.binary)
    elif args.gdb:
        data = load_gdb(args.gdb)
    else:
        data = load_hex(args.hex)

    fi = unpack_fault_info(data)

    print_report(
        fi,
        elf=args.elf,
        map_path=args.map,
        src_root=args.src,
        tool_prefix=args.tool_prefix,
    )


if __name__ == "__main__":
    main()
