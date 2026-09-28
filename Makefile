CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra

# Headless build: no dependencies, renders the framebuffer as ASCII.
chip8: chip8.c
	$(CC) $(CFLAGS) chip8.c -o chip8

# Graphical build: needs SDL2 (libsdl2-dev / brew install sdl2).
chip8-sdl: chip8.c
	$(CC) $(CFLAGS) -DUSE_SDL chip8.c -o chip8-sdl `sdl2-config --cflags --libs`

demo.ch8: make_demo.py
	python3 make_demo.py

run: chip8 demo.ch8
	./chip8 demo.ch8

# Independent unit test for the tricky opcodes (carry/borrow, BCD, load/store).
test: test_core.c chip8.c
	$(CC) $(CFLAGS) test_core.c -o test_core
	./test_core

clean:
	rm -f chip8 chip8-sdl demo.ch8 test_core

.PHONY: run test clean
