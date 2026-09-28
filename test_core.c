/*
 * test_core.c - independent unit test for the tricky CHIP-8 opcodes.
 *
 * We compile chip8.c with its main() renamed out of the way, so we can call
 * the real c8_* core directly and assert on register/memory state -- the
 * things the visual demo can't show. Focuses on the documented gotchas:
 * carry/borrow flag ordering, the Vx==VF aliasing case, and BCD.
 *
 *   gcc -O2 -Wall test_core.c -o test_core && ./test_core
 */
#include <assert.h>
#include <stdio.h>

/* Pull in the core, but rename chip8.c's own main() out of the way so ours
 * wins. The rename is scoped to just this include -- no build flags, and
 * chip8.c stays untouched. */
#define main chip8_cli_main
#include "chip8.c"
#undef main

/* Hand-assemble one opcode into memory at pc and single-step it. */
static void step(Chip8 *c, uint16_t op) {
    c->mem[c->pc]     = op >> 8;
    c->mem[c->pc + 1] = op & 0xFF;
    c8_cycle(c);
}

static int failures = 0;
#define CHECK(desc, cond) do { \
    if (cond) { printf("  ok   %s\n", desc); } \
    else      { printf("  FAIL %s\n", desc); failures++; } \
} while (0)

int main(void) {
    Chip8 c;

    /* 8XY4 ADD with carry: 200 + 100 = 300 -> wraps to 44, VF=1 */
    c8_init(&c);
    step(&c, 0x6AC8);            /* LD VA, 200 */
    step(&c, 0x6B64);            /* LD VB, 100 */
    step(&c, 0x8AB4);            /* ADD VA, VB */
    CHECK("8XY4 sum wraps to 44", c.V[0xA] == 44);
    CHECK("8XY4 sets carry VF=1", c.V[0xF] == 1);

    /* 8XY4 into VF itself: result must be the CARRY, not the sum.
     * This is the "flag written last" claim in the source. */
    c8_init(&c);
    step(&c, 0x6FC8);            /* LD VF, 200 */
    step(&c, 0x6064);            /* LD V0, 100 */
    step(&c, 0x8F04);            /* ADD VF, V0  (X == F) */
    CHECK("8XY4 with Vx==VF yields carry, not sum", c.V[0xF] == 1);

    /* 8XY5 SUB: 50 - 30 = 20, no borrow so VF=1 */
    c8_init(&c);
    step(&c, 0x6032);            /* LD V0, 50 */
    step(&c, 0x611E);            /* LD V1, 30 */
    step(&c, 0x8015);            /* SUB V0, V1 */
    CHECK("8XY5 difference is 20", c.V[0] == 20);
    CHECK("8XY5 no-borrow sets VF=1", c.V[0xF] == 1);

    /* 8XY5 with borrow: 30 - 50 = 236 (wrap), VF=0 */
    c8_init(&c);
    step(&c, 0x601E);            /* LD V0, 30 */
    step(&c, 0x6132);            /* LD V1, 50 */
    step(&c, 0x8015);            /* SUB V0, V1 */
    CHECK("8XY5 borrow wraps to 236", c.V[0] == 236);
    CHECK("8XY5 borrow clears VF=0", c.V[0xF] == 0);

    /* 8XY6 SHR: LSB goes to VF */
    c8_init(&c);
    step(&c, 0x6005);            /* LD V0, 5 (0b101) */
    step(&c, 0x8006);            /* SHR V0 */
    CHECK("8XY6 shifts right to 2", c.V[0] == 2);
    CHECK("8XY6 captures LSB in VF", c.V[0xF] == 1);

    /* FX33 BCD of 234 -> [2,3,4] at I */
    c8_init(&c);
    c.I = 0x300;
    step(&c, 0x60EA);            /* LD V0, 234 */
    step(&c, 0xF033);            /* LD B, V0 */
    CHECK("FX33 hundreds digit = 2", c.mem[0x300] == 2);
    CHECK("FX33 tens digit = 3",     c.mem[0x301] == 3);
    CHECK("FX33 ones digit = 4",     c.mem[0x302] == 4);

    /* FX55/FX65 round-trip: store V0..V3, clobber, reload */
    c8_init(&c);
    c.I = 0x400;
    for (int i = 0; i < 4; i++) c.V[i] = 0x10 + i;
    step(&c, 0xF355);            /* LD [I], V0..V3 */
    for (int i = 0; i < 4; i++) c.V[i] = 0;
    step(&c, 0xF365);            /* LD V0..V3, [I] */
    CHECK("FX55/FX65 round-trips registers",
          c.V[0]==0x10 && c.V[1]==0x11 && c.V[2]==0x12 && c.V[3]==0x13);

    printf("\n%s\n", failures ? "SOME TESTS FAILED" : "all core opcode tests passed");
    return failures ? 1 : 0;
}
