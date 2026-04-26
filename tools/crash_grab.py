#!/usr/bin/env python3

import argparse
import os
import re
import socket
import subprocess
import time
import tomllib


FAULT_TYPES = {0: "HardFault", 1: "MemManage", 2: "BusFault", 3: "UsageFault"}
FAULT_MAGIC = 0xDEADC0DE


def resolve_symbol(elf, tool_prefix, symbol):
    result = subprocess.run(
        [f"{tool_prefix}nm", "--defined-only", "--demangle", elf],
        capture_output=True, text=True, check=True,
    )
    for line in result.stdout.splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3 and parts[2].endswith(symbol):
            return int(parts[0], 16)
    raise ValueError(f"Symbol '{symbol}' not found in {elf}")


def parse_map_symbols(map_path):
    """Return sorted list of (address, name) for all code symbols in the map."""
    symbols = []
    with open(map_path) as f:
        for line in f:
            # Lines like:  "                0x00000000080014ac  chipz::foo()"
            m = re.match(r'^\s+(0x[0-9a-fA-F]{8,})\s+([^\s].+)$', line)
            if m:
                addr = int(m.group(1), 16)
                name = m.group(2).strip()
                # Keep only flash addresses with a plausible function name
                if 0x08000000 <= addr < 0x08100000 and not name.startswith("0x"):
                    symbols.append((addr, name))
    symbols.sort()
    return symbols


def lookup_function(symbols, pc):
    """Return the nearest symbol at or below pc."""
    lo, hi = 0, len(symbols) - 1
    result = None
    while lo <= hi:
        mid = (lo + hi) // 2
        if symbols[mid][0] <= pc:
            result = symbols[mid]
            lo = mid + 1
        else:
            hi = mid - 1
    return result


def parse_mdw(raw):
    """Extract all hex words from OpenOCD mdw output."""
    words = []
    for line in raw.splitlines():
        # Each line: "0xADDR: w0 w1 w2 w3"
        if ":" in line:
            _, _, values = line.partition(":")
            words.extend(int(w, 16) for w in values.split())
    return words


def print_fault(words, symbols):
    magic       = words[0]
    fault_type  = words[1]
    r0, r1, r2, r3, r12, lr, pc, xpsr = words[2:10]
    fpu_active  = words[27]
    sp          = words[28]
    exc_return  = words[29]
    msp, psp    = words[30], words[31]
    control     = words[32]
    cfsr        = words[33]
    hfsr        = words[34]
    mmfar       = words[36]
    bfar        = words[37]

    print(f"  Fault type : {FAULT_TYPES.get(fault_type, f'unknown ({fault_type})')}")
    print()
    print(f"  PC         : 0x{pc:08x}")
    fn = lookup_function(symbols, pc)
    if fn:
        print(f"               {fn[1]} (+0x{pc - fn[0]:x})")
    print(f"  LR         : 0x{lr:08x}")
    fn = lookup_function(symbols, lr & ~1)
    if fn:
        print(f"               {fn[1]} (+0x{(lr & ~1) - fn[0]:x})")
    print()
    print(f"  R0         : 0x{r0:08x}")
    print(f"  R1         : 0x{r1:08x}")
    print(f"  R2         : 0x{r2:08x}")
    print(f"  R3         : 0x{r3:08x}")
    print(f"  R12        : 0x{r12:08x}")
    print(f"  xPSR       : 0x{xpsr:08x}")
    print()
    print(f"  SP         : 0x{sp:08x}")
    print(f"  MSP        : 0x{msp:08x}")
    print(f"  PSP        : 0x{psp:08x}")
    print(f"  EXC_RETURN : 0x{exc_return:08x}")
    print(f"  CONTROL    : 0x{control:08x}")
    print()
    print(f"  CFSR       : 0x{cfsr:08x}")
    print(f"  HFSR       : 0x{hfsr:08x}")
    print(f"  MMFAR      : 0x{mmfar:08x}")
    print(f"  BFAR       : 0x{bfar:08x}")
    if fpu_active:
        print()
        s_regs = words[10:26]
        print(f"  FPU active, S0-S15: {[f'0x{v:08x}' for v in s_regs]}")


def wait_for_port(host, port, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            socket.create_connection((host, port), timeout=0.5).close()
            return
        except OSError:
            time.sleep(0.2)
    raise TimeoutError(f"OpenOCD did not open port {port} within {timeout}s")


def tcl_cmd(sock, cmd):
    sock.sendall((cmd + "\x1a").encode())
    buf = b""
    while not buf.endswith(b"\x1a"):
        buf += sock.recv(4096)
    return buf[:-1].decode()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("config", help="path to TOML config file")
    args = parser.parse_args()

    with open(args.config, "rb") as f:
        cfg = tomllib.load(f)

    config_dir = os.path.dirname(os.path.abspath(args.config))
    elf = os.path.join(config_dir, cfg["elf"])
    map_file = os.path.join(config_dir, cfg["map"])

    addr = resolve_symbol(elf, cfg["tool_prefix"], "g_fault_info")
    symbols = parse_map_symbols(map_file)

    suppress = cfg.get("suppress_debugger_output", False)
    devnull = open(os.devnull, "w") if suppress else None

    server = cfg["server"]
    proc = subprocess.Popen(
        [server["bin"]] + server["args"],
        cwd=config_dir,
        stdout=devnull,
        stderr=devnull,
    )

    try:
        wait_for_port(cfg["host"], cfg["port"], cfg["timeout"])
        with socket.create_connection((cfg["host"], cfg["port"])) as sock:
            if suppress:
                print("OpenOCD connection successful")
            raw = tcl_cmd(sock, f"mdw 0x{addr:08x} 71")
            words = parse_mdw(raw)
            if len(words) < 71:
                print("ERROR: incomplete read")
            elif words[0] != FAULT_MAGIC:
                print(f"No fault recorded (magic=0x{words[0]:08x})")
            else:
                print("=== FAULT CAPTURED ===")
                print_fault(words, symbols)
            tcl_cmd(sock, "shutdown")
    finally:
        proc.wait()
        if devnull:
            devnull.close()


if __name__ == "__main__":
    main()
