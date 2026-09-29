#!/usr/bin/env python3
r"""Author SimpleLoadoutSystem.esp - the loadouts' storage, as raw Oblivion (TES4) records: no editor needed,
byte-for-byte reproducible, reviewable in a diff (the Skyrim mod's build-esp.py, in Oblivion's file format).

ONE CONTAINER PER LOADOUT, AND SAFE STORAGE, with Oblivion Remastered's two Altar-side rules kept out of play (plan,
2026-09-29): no NEW base object (a new EditorID needs a UE asset pairing or its references crash when their cell
loads) and no cell that ever loads (a new interior cell needs an Altar-side entry to load; ours is never entered):

    GLOB 0x800  SLSOR_ActiveLoadout       the active loadout (-1 = none); the game's save keeps it (OBSE64 has no co-save)
    CELL 0x801  SLSOR_StorageCell         interior holding cell, no doors, nothing ever enters it
    REFR 0x802 .. 0x80B                   ten PERSISTENT references of Oblivion.esm's ChestClutterLower01Empty
                                          (0x000086C1, no items, DATA flags 0: NO "Respawns", so the engine never resets its
                                          contents - the same base as a player-home chest), owned by the player
                                          (XOWN 0x7). A persistent reference lives in memory and in the save whether
                                          or not its cell is loaded.

One master (Oblivion.esm), so our records take on-disk index 01; the DLL finds them by object index whatever the
runtime load index is (Loadouts.cpp Find).

Usage:  python build-esp.py [out.esp]
"""
import os
import struct
import sys

PLUGIN = "SimpleLoadoutSystem.esp"
AUTHOR = "ApocryphaRealm"
DESCRIPTION = "Simple Loadout System - one safe storage container per loadout"
LOADOUTS = 10

CHEST_BASE = 0x000086C1    # Oblivion.esm ChestClutterLower01Empty: no items, no Respawns flag (read from the ESM 2026-09-29; the first choice, ChestHouseTreasuryMiddle02, spawned loot in every reference)
PLAYER_NPC = 0x00000007    # the Player's actor base, the owner of every container
GLOBAL = 0x01000800
CELL = 0x01000801
FIRST_REF = 0x01000802


def sub(tag: bytes, data: bytes) -> bytes:
    if len(data) > 0xFFFF:
        raise ValueError("subrecord %s too long" % tag)
    return tag + struct.pack("<H", len(data)) + data


def zstr(s: str) -> bytes:
    return s.encode("cp1252") + b"\x00"


def record(tag: bytes, formid: int, data: bytes, flags: int = 0) -> bytes:
    # Oblivion: a 20-byte header - type, data size, flags, form ID, version control (no form version field)
    return tag + struct.pack("<IIII", len(data), flags, formid, 0) + data


def group(label: bytes, gtype: int, content: bytes) -> bytes:
    # Oblivion: a 20-byte header - "GRUP", size including the header, label, type, version control
    return b"GRUP" + struct.pack("<I", 20 + len(content)) + label + struct.pack("<iI", gtype, 0) + content


def u32(v: int) -> bytes:
    return struct.pack("<I", v)


def i32(v: int) -> bytes:
    return struct.pack("<i", v)


def glob() -> bytes:
    # FNAM: the value's type ('s' short, 'l' long, 'f' float); FLTV: the value, always stored as a float
    body = sub(b"EDID", zstr("SLSOR_ActiveLoadout")) + sub(b"FNAM", b"l") + sub(b"FLTV", struct.pack("<f", -1.0))
    return record(b"GLOB", GLOBAL, body)


def cell() -> bytes:
    # DATA (1 byte): 0x01 Is Interior Cell; XCLL: the interior lighting block, 36 bytes in Oblivion - ambient,
    # directional, fog colour, fog near, fog far, rotation XY, rotation Z, directional fade, fog clip. The remaster's
    # loader SKIPS THE WHOLE PLUGIN on a 48-byte block (found by bisection 2026-09-29: the same file with a 36-byte
    # block loaded, and the game's plugin list simply omitted the file otherwise, with nothing logged).
    lighting = struct.pack("<IIIffffff", 0x00404040, 0x00404040, 0x00000000, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0)
    body = (sub(b"EDID", zstr("SLSOR_StorageCell")) +
            sub(b"FULL", zstr("Loadout Storage")) +
            sub(b"DATA", struct.pack("<B", 0x01)) +
            sub(b"XCLL", lighting))
    return record(b"CELL", CELL, body)


def reference(slot: int) -> bytes:
    body = (sub(b"EDID", zstr("SLSOR_Storage%02d" % (slot + 1))) +
            sub(b"NAME", u32(CHEST_BASE)) +
            sub(b"XOWN", u32(PLAYER_NPC)) +
            sub(b"DATA", struct.pack("<6f", 100.0 * slot, 0.0, 0.0, 0.0, 0.0, 0.0)))
    return record(b"REFR", FIRST_REF + slot, body, flags=0x00000400)   # 0x400 Persistent


def main() -> None:
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "dist", "OblivionRemastered", "Content", "Dev", "ObvData", "Data", PLUGIN)

    refs = b"".join(reference(i) for i in range(LOADOUTS))
    # Interior cell block / sub-block: the last and second-to-last decimal digits of the cell's object index.
    index = CELL & 0xFFFFFF
    block, subblock = index % 10, (index // 10) % 10
    cell_children = group(u32(CELL), 6, group(u32(CELL), 8, refs))   # 6 cell children, 8 persistent children
    cells = group(b"CELL", 0, group(i32(block), 2, group(i32(subblock), 3, cell() + cell_children)))
    body = group(b"GLOB", 0, glob()) + cells

    records = 2 + LOADOUTS                      # GLOB, CELL and the references
    groups = 2 + 2 + 2                          # the two top groups, block + sub-block, cell children + persistent
    hedr = sub(b"HEDR", struct.pack("<fII", 1.0, records + groups, (FIRST_REF + LOADOUTS) & 0xFFFFFF))
    tes4 = record(b"TES4", 0, hedr + sub(b"CNAM", zstr(AUTHOR)) + sub(b"SNAM", zstr(DESCRIPTION)) +
                  sub(b"MAST", zstr("Oblivion.esm")) + sub(b"DATA", struct.pack("<Q", 0)))
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "wb") as f:
        f.write(tes4 + body)
    print(f"build-esp: wrote {out} ({len(tes4) + len(body)} bytes): 1 global, 1 cell, {LOADOUTS} persistent "
          f"containers of {CHEST_BASE:08X}; cell block {block} sub-block {subblock}")


if __name__ == "__main__":
    main()
