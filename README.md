# chip8

![Language: C](https://img.shields.io/badge/language-C99-blue.svg)
![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)
![Platform: Linux](https://img.shields.io/badge/platform-Linux-lightgrey.svg)

A complete **CHIP-8 interpreter** in a single file of portable C — with the CPU
core cleanly separated from I/O, an ASCII and an SDL2 front-end, a tiny
assembler for the demo ROM, and unit tests for the opcodes emulators most
often get wrong.

Built as a from-scratch reverse-engineering exercise: the point isn't to play
PONG (though you can), it's to have *written* the fetch–decode–execute loop, the
sprite XOR blitter, and the flag semantics by hand. The core touches nothing but
its own arrays, so the same platform layer can later host an ARM or RISC-V core.

```
####   #  #### ####     0 1 2 3
#  #  ##     #    #
#  #   #  #### ####     4 5 6 7
#  #   #  #       #
####  ### #### ####     8 9 A B
                        C D E F
#  # #### #### ####
#  # #    #       #     ( the demo ROM drawing the
#### #### ####   #        built-in hex font, 0-F )
   #    # #  #  #
   # #### ####  #
```

## Features

- **Full instruction set** — all 35 CHIP-8 opcodes, including BCD, sprite draw
  with collision detection, and the delay/sound timers.
- **Core / platform split** — the CPU (`c8_*` functions + `struct Chip8`) has
  zero I/O dependencies. Swap the front-end, or later swap the whole core, without
  touching the other half.
- **Two build modes** — a zero-dependency headless build that renders the
  framebuffer as ASCII (great for CI and debugging), and an SDL2 build with a
  real display and keypad input.
- **Tested** — `test_core.c` asserts on the tricky opcodes (carry/borrow flag
  ordering, the `Vx == VF` aliasing case, BCD, and `FX55`/`FX65` load-store) and
  passes the third-party [Timendus `corax+`](https://github.com/Timendus/chip8-test-suite)
  opcode ROM.
- **Quirk switches** — compile-time toggles for the ambiguous COSMAC-VIP-vs-later
  behaviours that break some ROMs.

## Install & build

Requires a C compiler and `make`. The SDL2 build additionally needs `libsdl2-dev`.

```sh
git clone https://github.com/b17w1z4rd/chip8.git
cd chip8

make            # headless build (no dependencies)     -> ./chip8
make test       # build + run the opcode unit tests
make run        # build headless + run the ASCII font demo
```

Graphical build (Debian / Ubuntu / Parrot / Kali):

```sh
sudo apt install libsdl2-dev
make chip8-sdl                     # -> ./chip8-sdl
./chip8-sdl path/to/game.ch8
```

## Usage

```sh
./chip8      <rom.ch8> [steps]     # headless: run N instructions, print the screen as ASCII
./chip8-sdl  <rom.ch8>             # graphical: real-time display + input
```

The COSMAC keypad is mapped onto the left of the keyboard:

```
 CHIP-8            Keyboard
1 2 3 C            1 2 3 4
4 5 6 D    -->     Q W E R
7 8 9 E            A S D F
A 0 B F            Z X C V
```

`Esc` quits.

### Getting ROMs

- **Test suite:** [Timendus/chip8-test-suite](https://github.com/Timendus/chip8-test-suite)
  — start with `1-chip8-logo.ch8`, then `3-corax+.ch8` (opcode correctness) and
  `5-quirks.ch8` (tells you how to set the quirk switches below).
- **Games:** public-domain classics (PONG, BRIX, TETRIS, INVADERS) are widely
  mirrored on GitHub.

## Testing

```sh
make test
```

Runs 13 assertions against the CPU core directly — the semantics that a visual
demo can't show. Expected output ends with:

```
all core opcode tests passed
```

## Project layout

| File            | What it is                                                          |
|-----------------|---------------------------------------------------------------------|
| `chip8.c`       | The emulator: CPU core + two front-ends (headless ASCII, SDL2).     |
| `make_demo.py`  | A ~40-line CHIP-8 assembler; writes `demo.ch8` (draws the hex font). |
| `test_core.c`   | Unit tests for the tricky opcodes (carry/borrow, BCD, load-store).  |
| `Makefile`      | Build targets: `chip8`, `chip8-sdl`, `test`, `run`, `clean`.        |

## Quirks

A handful of opcodes behave differently on the original COSMAC VIP versus later
interpreters — the #1 reason an emulator "runs but some games glitch". Toggle
these two `#define`s at the top of `chip8.c` if a given ROM misbehaves, then
rebuild:

| Switch          | Effect when set to `1`                                         |
|-----------------|---------------------------------------------------------------|
| `LEGACY_SHIFT`  | `8XY6` / `8XYE` read from `Vy` before shifting (not `Vx`).     |
| `LEGACY_MEM`    | `FX55` / `FX65` increment `I` by `X+1` afterwards.             |

Run `5-quirks.ch8` from the test suite to see which settings a given ROM expects.

## Roadmap

- [ ] Sound-timer beep in the SDL build (`SDL_QueueAudio`)
- [ ] Runtime `--legacy` flag instead of compile-time quirk switches
- [ ] A CHIP-8 disassembler (reuses the decode table in `c8_cycle`)
- [ ] Graduate the platform layer to an ARM / RISC-V core for firmware study

## References

- [Cowgod's CHIP-8 Technical Reference](http://devernay.free.fr/hacks/chip8/C8TECH10.HTM)
- [Guide to making a CHIP-8 emulator — Tobias V. Langhoff](https://tobiasvl.github.io/blog/write-a-chip-8-emulator/)
- [Timendus CHIP-8 test suite](https://github.com/Timendus/chip8-test-suite)

## License

MIT — see [LICENSE](LICENSE).
