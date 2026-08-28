#!/usr/bin/env python3
"""Create a ROM-free RG Nano ELF and its runtime restoration manifest.

Every symbol in the target's data sections whose bytes appear verbatim in the
retail Emerald ROM is recorded and zeroed. The RG Nano runtime validates the
ELF build id and the user's ROM before restoring those bytes in memory.

Usage:
  make_asset_holes.py analyze <unstripped-elf> <rom.gba>
  make_asset_holes.py build <unstripped-elf> <rom.gba> <input-elf> <output-elf> <manifest.bin>

Manifest format: ROM SHA-1 (20 bytes), GNU build id (20 bytes), little-endian
u32 entry count, then entries of u32 virtualAddress, u32 size, u32 romOffset.
"""

import bisect
import hashlib
import struct
import sys

from elftools.elf.elffile import ELFFile

MIN_SIZE = 32
EXPECTED_ROM_SHA1 = "f3ae088181bf583e55daf962a92bb46f4f1d07b7"


def collect_entries(elf_path, rom):
    entries = []
    matched = unmatched = 0
    with open(elf_path, "rb") as stream:
        elf = ELFFile(stream)
        symtab = elf.get_section_by_name(".symtab")
        if symtab is None:
            raise SystemExit("no .symtab: pass the unstripped ELF as the analysis input")
        data_sections = [
            section
            for section in elf.iter_sections()
            if section.name in (".rodata", ".data.rel.ro", ".data", "script_data")
        ]
        ranges = [
            (section["sh_addr"], section["sh_addr"] + section["sh_size"], section)
            for section in data_sections
        ]

        def section_for(address):
            for low, high, section in ranges:
                if low <= address < high:
                    return low, high, section
            return None, None, None

        address_symbols = []
        for symbol in symtab.iter_symbols():
            address = symbol["st_value"]
            low, high, section = section_for(address)
            if section is not None:
                address_symbols.append((address, symbol["st_size"], high))
        address_symbols.sort()

        candidates = []
        for index, (address, size, section_end) in enumerate(address_symbols):
            if size == 0:
                next_address = section_end
                for following in range(index + 1, len(address_symbols)):
                    if address_symbols[following][0] > address:
                        next_address = min(address_symbols[following][0], section_end)
                        break
                size = next_address - address
            if size >= MIN_SIZE:
                candidates.append((address, size))

        candidates.sort()
        pruned = []
        for address, size in candidates:
            if pruned and address + size <= pruned[-1][0] + pruned[-1][1]:
                continue
            pruned.append((address, size))

        # ARM32 REL relocations keep their addends in the target word. Never
        # zero or restore such a word, because the dynamic loader owns it.
        relocation_offsets = []
        for section_name in (".rel.dyn", ".rela.dyn"):
            relocation_section = elf.get_section_by_name(section_name)
            if relocation_section is not None:
                relocation_offsets.extend(
                    relocation["r_offset"] for relocation in relocation_section.iter_relocations()
                )
        relocation_offsets.sort()

        def split_around_relocations(address, size):
            parts = []
            start = address
            end = address + size
            index = bisect.bisect_left(relocation_offsets, start - 3)
            while index < len(relocation_offsets) and relocation_offsets[index] < end:
                relocation = relocation_offsets[index]
                if relocation + 4 > start:
                    if relocation > start:
                        parts.append((start, relocation - start))
                    start = relocation + 4
                index += 1
            if end > start:
                parts.append((start, end - start))
            return [(part_address, part_size) for part_address, part_size in parts if part_size >= MIN_SIZE]

        pruned = [
            part
            for address, size in pruned
            for part in split_around_relocations(address, size)
        ]

        def match_range(data, address):
            rom_offset = rom.find(data)
            if rom_offset >= 0:
                return [(address, len(data), rom_offset)]
            stripped = data.rstrip(b"\x00")
            if MIN_SIZE <= len(stripped) < len(data):
                rom_offset = rom.find(stripped)
                if rom_offset >= 0:
                    return [(address, len(stripped), rom_offset)]
            if len(data) < MIN_SIZE * 2:
                return []
            midpoint = (len(data) // 2) & ~3
            return match_range(data[:midpoint], address) + match_range(data[midpoint:], address + midpoint)

        # Each miss recursively halves and re-scans the 16 MiB ROM, so this pass
        # takes tens of minutes. Report progress so a long run is visibly alive.
        total_candidates = len(pruned)
        for candidate_index, (address, size) in enumerate(pruned):
            if candidate_index % 250 == 0:
                print(
                    f"  scanning candidate {candidate_index}/{total_candidates} "
                    f"({matched / 1e6:.1f} MB matched so far)",
                    file=sys.stderr,
                    flush=True,
                )
            low, _high, section = section_for(address)
            data = section.data()[address - low : address - low + size]
            if len(data) != size:
                continue
            found = match_range(data, address)
            entries.extend(found)
            matched += sum(entry[1] for entry in found)
            unmatched += size - sum(entry[1] for entry in found)

    entries.sort()
    merged = []
    for address, size, rom_offset in entries:
        if merged:
            previous_address, previous_size, previous_rom = merged[-1]
            if (
                address <= previous_address + previous_size
                and rom_offset == previous_rom + (address - previous_address)
            ):
                merged[-1] = (
                    previous_address,
                    max(previous_address + previous_size, address + size) - previous_address,
                    previous_rom,
                )
                continue
        merged.append((address, size, rom_offset))
    return merged, matched, unmatched


def virtual_to_file_offset(elf, virtual_address):
    for segment in elf.iter_segments():
        if segment["p_type"] != "PT_LOAD":
            continue
        if segment["p_vaddr"] <= virtual_address < segment["p_vaddr"] + segment["p_filesz"]:
            return virtual_address - segment["p_vaddr"] + segment["p_offset"]
    return None


def read_build_id(elf):
    section = elf.get_section_by_name(".note.gnu.build-id")
    if section is None:
        return None
    for note in section.iter_notes():
        if note["n_type"] == "NT_GNU_BUILD_ID":
            return bytes.fromhex(note["n_desc"])
    return None


def main():
    if len(sys.argv) not in (4, 7) or sys.argv[1] not in ("analyze", "build"):
        raise SystemExit(__doc__)
    mode = sys.argv[1]
    analysis_elf = sys.argv[2]
    rom_path = sys.argv[3]
    rom = open(rom_path, "rb").read()
    rom_sha1 = hashlib.sha1(rom).hexdigest()
    if len(rom) != 16 * 1024 * 1024 or rom_sha1 != EXPECTED_ROM_SHA1:
        raise SystemExit(
            f"expected 16 MiB US Emerald v1.0 ({EXPECTED_ROM_SHA1}), got {len(rom)} bytes ({rom_sha1})"
        )

    entries, matched, unmatched = collect_entries(analysis_elf, rom)
    total = matched + unmatched
    print(
        f"symbols matched into ROM: {matched / 1e6:.1f} MB "
        f"({matched * 100 // max(total, 1)}% of {total / 1e6:.1f} MB candidate data)"
    )
    print(f"manifest entries after merge: {len(entries)}")
    if mode == "analyze":
        return

    input_elf, output_elf, manifest_path = sys.argv[4:7]
    data = bytearray(open(input_elf, "rb").read())
    with open(input_elf, "rb") as stream:
        elf = ELFFile(stream)
        build_id = read_build_id(elf)
        if build_id is None or len(build_id) != 20:
            raise SystemExit("target ELF has no 20-byte GNU build id")
        zeroed = 0
        for virtual_address, size, _rom_offset in entries:
            file_offset = virtual_to_file_offset(elf, virtual_address)
            if file_offset is None:
                raise SystemExit(f"virtual address {virtual_address:#x} is not file-backed by PT_LOAD")
            data[file_offset : file_offset + size] = bytes(size)
            zeroed += size
    open(output_elf, "wb").write(data)
    with open(manifest_path, "wb") as manifest:
        manifest.write(bytes.fromhex(rom_sha1))
        manifest.write(build_id)
        manifest.write(struct.pack("<I", len(entries)))
        for entry in entries:
            manifest.write(struct.pack("<III", *entry))
    print(f"zeroed {zeroed / 1e6:.1f} MB -> {output_elf}")
    print(f"manifest -> {manifest_path}")


if __name__ == "__main__":
    main()
