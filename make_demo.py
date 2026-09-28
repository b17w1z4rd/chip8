#!/usr/bin/env python3
"""
A ~40-line CHIP-8 assembler. Writes demo.ch8, which draws the 16 hex font
glyphs (0-F) in a 4x4 grid. It exercises font addressing, XOR draw, register
arithmetic, conditional skips, and jumps -- a decent smoke test for the core.

Programs load at 0x200, so that's where addresses count from.
"""

ORG = 0x200
program = []   # list of ("mnemonic", *args); a bare "label:" string marks a spot

def emit(*items):
    program.extend(items)

# ---- the program -----------------------------------------------------------
emit(
    ("LDB", 0, 0),        # V0 = digit (0..15)
    ("LDB", 1, 0),        # V1 = x
    ("LDB", 2, 0),        # V2 = y
    ("LDB", 3, 0),        # V3 = column (0..3)
    "draw:",
    ("LDF", 0),           # I = font sprite for V0
    ("DRW", 1, 2, 5),     # draw 5-row glyph at (x, y)
    ("ADDB", 0, 1),       # digit++
    ("ADDB", 1, 5),       # x += 5
    ("ADDB", 3, 1),       # col++
    ("SEB", 3, 4),        # if col == 4: skip the jump back (start a new row)
    ("JP", "draw"),
    "newline:",
    ("LDB", 1, 0),        # x = 0
    ("ADDB", 2, 6),       # y += 6
    ("LDB", 3, 0),        # col = 0
    ("SNEB", 0, 16),      # if all 16 drawn: skip the jump back (halt)
    ("JP", "halt"),
    ("JP", "draw"),
    "halt:",
    ("JP", "halt"),       # spin forever
)

# ---- two-pass assembly -----------------------------------------------------
# pass 1: every instruction is 2 bytes, so compute each label's address.
labels, addr = {}, ORG
for item in program:
    if isinstance(item, str):
        labels[item[:-1]] = addr        # strip trailing ':'
    else:
        addr += 2

def encode(ins):
    op, *a = ins
    if op == "LDB":  return 0x6000 | (a[0] << 8) | a[1]            # LD  Vx, byte
    if op == "ADDB": return 0x7000 | (a[0] << 8) | a[1]            # ADD Vx, byte
    if op == "SEB":  return 0x3000 | (a[0] << 8) | a[1]            # SE  Vx, byte
    if op == "SNEB": return 0x4000 | (a[0] << 8) | a[1]            # SNE Vx, byte
    if op == "LDF":  return 0xF029 | (a[0] << 8)                   # LD  F, Vx
    if op == "DRW":  return 0xD000 | (a[0] << 8) | (a[1] << 4) | a[2]
    if op == "JP":   return 0x1000 | labels[a[0]]                  # JP  addr
    raise ValueError(op)

rom = bytearray()
for item in program:
    if not isinstance(item, str):
        w = encode(item)
        rom += bytes([w >> 8, w & 0xFF])

with open("demo.ch8", "wb") as f:
    f.write(rom)
print(f"wrote demo.ch8 ({len(rom)} bytes)")
