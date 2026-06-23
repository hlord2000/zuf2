#!/usr/bin/env python3

import argparse
import pathlib
import struct
import sys


UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID = 0x00002000
PAYLOAD_SIZE = 256


def parse_int(value):
    return int(value, 0)


def checksum_ok(raw):
    return (sum(raw) & 0xFF) == 0


def read_hex(path):
    upper = 0
    segment = 0
    memory = {}

    for lineno, line in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        if not line.startswith(":"):
            raise ValueError(f"{path}:{lineno}: invalid Intel HEX record")

        raw = bytes.fromhex(line[1:])
        if len(raw) < 5 or not checksum_ok(raw):
            raise ValueError(f"{path}:{lineno}: invalid Intel HEX checksum")

        count = raw[0]
        addr = (raw[1] << 8) | raw[2]
        rectype = raw[3]
        data = raw[4:4 + count]

        if len(data) != count:
            raise ValueError(f"{path}:{lineno}: truncated Intel HEX record")

        if rectype == 0x00:
            base = upper + segment + addr
            for offset, byte in enumerate(data):
                memory[base + offset] = byte
        elif rectype == 0x01:
            break
        elif rectype == 0x02:
            if count != 2:
                raise ValueError(f"{path}:{lineno}: invalid segment record")
            segment = (((data[0] << 8) | data[1]) << 4)
            upper = 0
        elif rectype == 0x04:
            if count != 2:
                raise ValueError(f"{path}:{lineno}: invalid linear address record")
            upper = (((data[0] << 8) | data[1]) << 16)
            segment = 0
        elif rectype in (0x03, 0x05):
            continue
        else:
            raise ValueError(f"{path}:{lineno}: unsupported Intel HEX record {rectype}")

    if not memory:
        raise ValueError(f"{path}: no data records found")

    return memory


def read_binary(path, base):
    data = path.read_bytes()
    return {base + offset: byte for offset, byte in enumerate(data)}


def filter_memory(memory, base, max_size):
    if base is None and max_size is None:
        return memory

    if base is None:
        raise ValueError("--max-size requires --base")

    end = base + max_size if max_size is not None else None
    filtered = {}

    for addr, byte in memory.items():
        if addr < base:
            continue
        if end is not None and addr >= end:
            continue
        filtered[addr] = byte

    if not filtered:
        raise ValueError("no input bytes remain after address filtering")

    return filtered


def memory_to_blocks(memory):
    start = min(memory)
    end = max(memory) + 1
    first_block = start // PAYLOAD_SIZE
    last_block = (end + PAYLOAD_SIZE - 1) // PAYLOAD_SIZE
    blocks = []

    for block_index in range(first_block, last_block):
        addr = block_index * PAYLOAD_SIZE
        payload = bytearray([0xFF] * PAYLOAD_SIZE)
        present = False

        for offset in range(PAYLOAD_SIZE):
            value = memory.get(addr + offset)
            if value is not None:
                payload[offset] = value
                present = True

        if present:
            blocks.append((addr, bytes(payload)))

    return blocks


def write_uf2(blocks, output, family):
    total = len(blocks)

    with output.open("wb") as fp:
        for block_no, (addr, payload) in enumerate(blocks):
            data = payload + bytes(476 - len(payload))
            fp.write(struct.pack(
                "<IIIIIIII476sI",
                UF2_MAGIC_START0,
                UF2_MAGIC_START1,
                UF2_FLAG_FAMILY_ID,
                addr,
                len(payload),
                block_no,
                total,
                family,
                data,
                UF2_MAGIC_END,
            ))


def main(argv):
    parser = argparse.ArgumentParser(description="Convert Zephyr firmware to UF2 for zuf2.")
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("-o", "--output", type=pathlib.Path)
    parser.add_argument("-f", "--family", type=parse_int, default=0x016F65E4)
    parser.add_argument("-b", "--base", type=parse_int,
                        help="base address for .bin input or address filter for .hex input")
    parser.add_argument("--max-size", type=parse_int,
                        help="maximum bytes to include from --base for .hex input")
    args = parser.parse_args(argv)

    suffix = args.input.suffix.lower()
    if suffix == ".hex":
        memory = read_hex(args.input)
        memory = filter_memory(memory, args.base, args.max_size)
    elif suffix == ".bin":
        if args.base is None:
            raise ValueError(".bin input requires --base")
        memory = read_binary(args.input, args.base)
    else:
        raise ValueError("input must be .hex or .bin")

    blocks = memory_to_blocks(memory)
    if not blocks:
        raise ValueError("no UF2 blocks generated")

    output = args.output
    if output is None:
        output = args.input.with_suffix(".uf2")

    write_uf2(blocks, output, args.family)
    print(f"wrote {output} ({len(blocks)} blocks, family 0x{args.family:08x})")


if __name__ == "__main__":
    try:
        main(sys.argv[1:])
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)
