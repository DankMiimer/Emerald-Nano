#!/usr/bin/env python3
"""Emerald save editor for dual-screen testing.

Reads/edits the 128KB flash save the port uses (same format as a cart).
Offsets come from tools/dualscreen/print_save_offsets.c (the decomp's own
headers); rerun it if the structs ever change.

Usage:
  savetool.py info <save>
  savetool.py teleport <save> <mapGroup> <mapNum> <x> <y>
  savetool.py heal <save>
  savetool.py money <save> <amount>

Map ids are in include/constants/map_groups.h, e.g. Littleroot = 0 9,
Sootopolis = 0 7, Route 121 = 0 36 (group 0 is the overworld group).
"""
import struct
import sys

SECTOR_SIZE = 4096
SECTOR_DATA_SIZE = 3968
SECTOR_SIGNATURE = 0x08012025
SECTORS_PER_SLOT = 14

SIZEOF_SB1 = 15752
SIZEOF_SB2 = 3884
SIZEOF_POKEMON = 100
SB1_POS = 0
SB1_LOCATION = 4
SB1_CONTINUE_WARP = 12
SB1_PARTY_COUNT = 564
SB1_PARTY = 568
SB1_MONEY = 1168
SB2_PLAYER_NAME = 0
SB2_PLAYTIME_HOURS = 14
SB2_ENCRYPTION_KEY = 172

# Sector footer sizes per sector id (checksummed range).
def section_size(sector_id):
    if sector_id == 0:
        return SIZEOF_SB2
    if 1 <= sector_id <= 4:
        remaining = SIZEOF_SB1 - (sector_id - 1) * SECTOR_DATA_SIZE
        return min(SECTOR_DATA_SIZE, remaining)
    return SECTOR_DATA_SIZE  # Pokémon storage sectors are full


def checksum(data):
    total = 0
    for (word,) in struct.iter_unpack("<I", data[: len(data) & ~3]):
        total = (total + word) & 0xFFFFFFFF
    return ((total >> 16) + (total & 0xFFFF)) & 0xFFFF


def decode_text(raw):
    out = []
    for b in raw:
        if b == 0xFF:
            break
        if 0xA1 <= b <= 0xAA: out.append(chr(ord("0") + b - 0xA1))
        elif 0xBB <= b <= 0xD4: out.append(chr(ord("A") + b - 0xBB))
        elif 0xD5 <= b <= 0xEE: out.append(chr(ord("a") + b - 0xD5))
        elif b == 0x00: out.append(" ")
    return "".join(out)


class Save:
    def __init__(self, path):
        self.path = path
        self.raw = bytearray(open(path, "rb").read())
        if len(self.raw) < SECTOR_SIZE * 28:
            sys.exit("not a 128KB Emerald save")
        self.slot = self._pick_slot()
        # sector id -> physical sector index
        self.sector_of = {}
        base = self.slot * SECTORS_PER_SLOT
        for i in range(base, base + SECTORS_PER_SLOT):
            sid, _, sig, _ = self._footer(i)
            if sig == SECTOR_SIGNATURE:
                self.sector_of[sid] = i

    def _footer(self, sector):
        off = sector * SECTOR_SIZE
        return struct.unpack_from("<HHII", self.raw, off + 0xFF4)

    def _slot_counter(self, slot):
        counters = []
        for i in range(slot * SECTORS_PER_SLOT, (slot + 1) * SECTORS_PER_SLOT):
            sid, _, sig, counter = self._footer(i)
            if sig != SECTOR_SIGNATURE:
                return -1
            counters.append(counter)
        return max(counters)

    def _pick_slot(self):
        a, b = self._slot_counter(0), self._slot_counter(1)
        if a < 0 and b < 0:
            sys.exit("no valid save slot found")
        return 0 if a >= b else 1

    def read_section(self, first_id, size):
        out = bytearray()
        sid = first_id
        while len(out) < size:
            sector = self.sector_of[sid]
            chunk = min(SECTOR_DATA_SIZE, size - len(out))
            off = sector * SECTOR_SIZE
            out += self.raw[off : off + chunk]
            sid += 1
        return out

    def write_section(self, first_id, data):
        sid = first_id
        written = 0
        while written < len(data):
            sector = self.sector_of[sid]
            chunk = min(SECTOR_DATA_SIZE, len(data) - written)
            off = sector * SECTOR_SIZE
            self.raw[off : off + chunk] = data[written : written + chunk]
            new_sum = checksum(self.raw[off : off + section_size(sid)])
            struct.pack_into("<H", self.raw, off + 0xFF6, new_sum)
            written += chunk
            sid += 1

    def save(self):
        open(self.path, "wb").write(self.raw)


def cmd_info(save):
    sb2 = save.read_section(0, SIZEOF_SB2)
    sb1 = save.read_section(1, SIZEOF_SB1)
    name = decode_text(sb2[SB2_PLAYER_NAME : SB2_PLAYER_NAME + 8])
    hours, = struct.unpack_from("<H", sb2, SB2_PLAYTIME_HOURS)
    minutes = sb2[SB2_PLAYTIME_HOURS + 2]
    group, num, _warp = struct.unpack_from("<bbb", sb1, SB1_LOCATION)
    x, y = struct.unpack_from("<hh", sb1, SB1_POS)
    key, = struct.unpack_from("<I", sb2, SB2_ENCRYPTION_KEY)
    money, = struct.unpack_from("<I", sb1, SB1_MONEY)
    count = sb1[SB1_PARTY_COUNT]
    print(f"slot {save.slot}  player {name}  time {hours}h{minutes:02d}m")
    print(f"map group {group} num {num}  pos ({x},{y})  money {money ^ key}")
    print(f"party of {count}:")
    for i in range(min(count, 6)):
        mon = SB1_PARTY + i * SIZEOF_POKEMON
        level = sb1[mon + 84]
        hp, max_hp = struct.unpack_from("<HH", sb1, mon + 86)
        print(f"  #{i + 1}  Lv{level}  {hp}/{max_hp} HP")


def cmd_teleport(save, group, num, x, y):
    sb1 = save.read_section(1, SIZEOF_SB1)
    warp = struct.pack("<bbbbhh", group, num, -1, 0, x, y)
    sb1[SB1_LOCATION : SB1_LOCATION + 8] = warp
    sb1[SB1_CONTINUE_WARP : SB1_CONTINUE_WARP + 8] = warp
    struct.pack_into("<hh", sb1, SB1_POS, x, y)
    save.write_section(1, sb1)
    save.save()
    print(f"teleported to map {group},{num} at ({x},{y})")


def cmd_heal(save):
    sb1 = save.read_section(1, SIZEOF_SB1)
    count = sb1[SB1_PARTY_COUNT]
    for i in range(min(count, 6)):
        mon = SB1_PARTY + i * SIZEOF_POKEMON
        max_hp, = struct.unpack_from("<H", sb1, mon + 88)
        struct.pack_into("<H", sb1, mon + 86, max_hp)
        struct.pack_into("<I", sb1, mon + 80, 0)  # status
    save.write_section(1, sb1)
    save.save()
    print(f"healed {count} party members")


def cmd_money(save, amount):
    sb2 = save.read_section(0, SIZEOF_SB2)
    sb1 = save.read_section(1, SIZEOF_SB1)
    key, = struct.unpack_from("<I", sb2, SB2_ENCRYPTION_KEY)
    struct.pack_into("<I", sb1, SB1_MONEY, (amount & 0xFFFFFFFF) ^ key)
    save.write_section(1, sb1)
    save.save()
    print(f"money set to {amount}")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    command, path = sys.argv[1], sys.argv[2]
    save = Save(path)
    if command == "info":
        cmd_info(save)
    elif command == "teleport":
        cmd_teleport(save, *(int(v) for v in sys.argv[3:7]))
    elif command == "heal":
        cmd_heal(save)
    elif command == "money":
        cmd_money(save, int(sys.argv[3]))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
