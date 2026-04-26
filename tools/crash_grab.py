#!/usr/bin/env python3

import argparse
import os
import socket
import struct
import subprocess
import sys
import time
import tomllib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fault_decoder


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


def parse_mdw(raw):
    words = []
    for line in raw.splitlines():
        if ":" in line:
            _, _, values = line.partition(":")
            words.extend(int(w, 16) for w in values.split())
    return words


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
    elf        = os.path.join(config_dir, cfg["elf"])
    map_file   = os.path.join(config_dir, cfg["map"])
    src_root   = os.path.join(config_dir, cfg.get("src", ""))
    tool_prefix = cfg.get("tool_prefix", "arm-none-eabi-")

    addr = resolve_symbol(elf, tool_prefix, "g_fault_info")

    suppress = cfg.get("suppress_debugger_output", False)
    devnull  = open(os.devnull, "w") if suppress else None

    server = cfg["server"]
    proc = subprocess.Popen(
        [server["bin"]] + server["args"],
        cwd=config_dir,
        stdout=devnull,
        stderr=devnull,
    )

    try:
        wait_for_port(cfg["host"], cfg["port"], cfg["timeout"])
        if suppress:
            print("OpenOCD connection successful")
        with socket.create_connection((cfg["host"], cfg["port"])) as sock:
            raw   = tcl_cmd(sock, f"mdw 0x{addr:08x} {fault_decoder.FAULT_INFO_WORDS}")
            words = parse_mdw(raw)
            if len(words) < fault_decoder.FAULT_INFO_WORDS:
                print("ERROR: incomplete read")
            elif words[0] != fault_decoder.FAULT_MAGIC:
                print(f"No fault recorded (magic=0x{words[0]:08x})")
            else:
                data = struct.pack(f"<{fault_decoder.FAULT_INFO_WORDS}I", *words)
                fi   = fault_decoder.unpack_fault_info(data)
                fault_decoder.print_report(
                    fi,
                    elf=elf,
                    map_path=map_file,
                    src_root=src_root,
                    tool_prefix=tool_prefix,
                )
            tcl_cmd(sock, "shutdown")
    finally:
        proc.wait()
        if devnull:
            devnull.close()


if __name__ == "__main__":
    main()
