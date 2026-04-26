#!/usr/bin/env python3

import argparse
import os
import socket
import subprocess
import time
import tomllib


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


def wait_for_port(host, port, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            socket.create_connection((host, port), timeout=0.5).close()
            return
        except OSError:
            time.sleep(0.2)
    raise TimeoutError(f"OpenOCD did not open port {port} within {timeout}s")


def tcl_send(sock, cmd):
    sock.sendall((cmd + "\x1a").encode())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("config", help="path to TOML config file")
    args = parser.parse_args()

    with open(args.config, "rb") as f:
        cfg = tomllib.load(f)

    config_dir = os.path.dirname(os.path.abspath(args.config))
    elf = os.path.join(config_dir, cfg["elf"])
    addr = resolve_symbol(elf, cfg["tool_prefix"], "g_fault_info")
    print(f"g_fault_info @ 0x{addr:08x}")

    server = cfg["server"]
    proc = subprocess.Popen(
        [server["bin"]] + server["args"],
        cwd=config_dir,
    )

    try:
        wait_for_port(cfg["host"], cfg["port"], cfg["timeout"])
        with socket.create_connection((cfg["host"], cfg["port"])) as sock:
            print("OpenOCD connection successful")
            time.sleep(5)
            tcl_send(sock, "shutdown")
    finally:
        proc.wait()


if __name__ == "__main__":
    main()
