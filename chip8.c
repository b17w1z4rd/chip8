/*
 * chip8.c - a complete CHIP-8 interpreter in one file.
 *
 * Build (headless, zero deps - renders the framebuffer as ASCII, great for
 * debugging and CI):
 *     gcc -O2 -Wall chip8.c -o chip8
 *     ./chip8 demo.ch8
 *
 * Build (graphical, needs SDL2 - real display, keypad input, optional beep):
 *     gcc -O2 -Wall -DUSE_SDL chip8.c -o chip8 $(sdl2-config --cflags --libs)
 *     # or: gcc ... -DUSE_SDL chip8.c -o chip8 -lSDL2
 *     ./chip8 roms/whatever.ch8
 *
 * The CPU core (c8_* functions + struct Chip8) touches nothing but its own
 * arrays. That's deliberate: when you later swap the CHIP-8 core for an ARM or
 * RISC-V core, this platform layer (SDL / ASCII) stays exactly as-is.
 *
 * ---- Quirk switches --------------------------------------------------------
 * CHIP-8 has a few ambiguous ops where the original COSMAC VIP and later
 * interpreters disagree. These are the #1 reason a fresh emulator "runs but
 * some games are broken". Flip these to chase compatibility with a given ROM.
 */
#define LEGACY_SHIFT 0   /* 1 => 8XY6/8XYE set Vx = Vy first, then shift        */
#define LEGACY_MEM   0   /* 1 => FX55/FX65 increment I by X+1 afterwards         */
/* (BNNN below uses the classic "jump to NNN + V0"; some ROMs expect BXNN.)     */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef USE_SDL
#include <SDL2/SDL.h>
#endif

#define MEM_SIZE   4096
#define V_COUNT    16
#define STACK_SIZE 16
#define WIDTH      64
#define HEIGHT     32
#define PROG_START 0x200
#define FONT_START 0x050

/* Standard 4x5 hex font, glyphs 0-F. Loaded at FONT_START. */
static const uint8_t FONT[80] = {
    0xF0,0x90,0x90,0x90,0xF0, /* 0 */  0x20,0x60,0x20,0x20,0x70, /* 1 */
    0xF0,0x10,0xF0,0x80,0xF0, /* 2 */  0xF0,0x10,0xF0,0x10,0xF0, /* 3 */
    0x90,0x90,0xF0,0x10,0x10, /* 4 */  0xF0,0x80,0xF0,0x10,0xF0, /* 5 */
    0xF0,0x80,0xF0,0x90,0xF0, /* 6 */  0xF0,0x10,0x20,0x40,0x40, /* 7 */
    0xF0,0x90,0xF0,0x90,0xF0, /* 8 */  0xF0,0x90,0xF0,0x10,0xF0, /* 9 */
    0xF0,0x90,0xF0,0x90,0x90, /* A */  0xE0,0x90,0xE0,0x90,0xE0, /* B */
    0xF0,0x80,0x80,0x80,0xF0, /* C */  0xE0,0x90,0x90,0x90,0xE0, /* D */
    0xF0,0x80,0xF0,0x80,0xF0, /* E */  0xF0,0x80,0xF0,0x80,0x80  /* F */
};

typedef struct {
    uint8_t  mem[MEM_SIZE];
    uint8_t  V[V_COUNT];          /* V0-VF; VF is the flag register            */
    uint16_t I;                   /* 12-bit address register                   */
    uint16_t pc;                  /* program counter                           */
    uint16_t stack[STACK_SIZE];
    uint8_t  sp;
    uint8_t  delay;               /* delay timer, decrements at 60Hz           */
    uint8_t  sound;               /* sound timer, beep while > 0               */
    uint8_t  gfx[WIDTH * HEIGHT]; /* framebuffer, 1 byte per pixel (0/1)       */
    uint8_t  keys[16];            /* current key state, 1 = down               */
    int      draw_flag;           /* set when gfx changed, cleared by platform */
} Chip8;

static void c8_init(Chip8 *c) {
    memset(c, 0, sizeof(*c));
    memcpy(c->mem + FONT_START, FONT, sizeof(FONT));
    c->pc = PROG_START;
    srand((unsigned)time(NULL));
}

/* Load a ROM at 0x200. Returns byte count, or -1 on error. */
static int c8_load(Chip8 *c, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(c->mem + PROG_START, 1, MEM_SIZE - PROG_START, f);
    fclose(f);
    return (int)n;
}

/* Decrement the two timers. Call this at ~60Hz, independent of CPU speed. */
static void c8_tick_timers(Chip8 *c) {
    if (c->delay) c->delay--;
    if (c->sound) c->sound--;
}

/* Fetch-decode-execute exactly one instruction. */
static void c8_cycle(Chip8 *c) {
    uint16_t op  = (c->mem[c->pc] << 8) | c->mem[c->pc + 1];
    c->pc += 2;

    uint8_t  X   = (op >> 8) & 0x0F;
    uint8_t  Y   = (op >> 4) & 0x0F;
    uint8_t  N   =  op       & 0x0F;
    uint8_t  NN  =  op       & 0xFF;
    uint16_t NNN =  op       & 0x0FFF;

    switch (op >> 12) {
    case 0x0:
        if (op == 0x00E0) {                 /* CLS  */
            memset(c->gfx, 0, sizeof(c->gfx));
            c->draw_flag = 1;
        } else if (op == 0x00EE) {           /* RET  */
            c->pc = c->stack[--c->sp];
        }                                    /* 0NNN (legacy machine call) ignored */
        break;

    case 0x1: c->pc = NNN; break;                          /* JP  NNN            */
    case 0x2: c->stack[c->sp++] = c->pc; c->pc = NNN; break;/* CALL NNN           */
    case 0x3: if (c->V[X] == NN)      c->pc += 2; break;    /* SE  Vx, NN         */
    case 0x4: if (c->V[X] != NN)      c->pc += 2; break;    /* SNE Vx, NN         */
    case 0x5: if (c->V[X] == c->V[Y]) c->pc += 2; break;    /* SE  Vx, Vy         */
    case 0x6: c->V[X] = NN; break;                          /* LD  Vx, NN         */
    case 0x7: c->V[X] += NN; break;                         /* ADD Vx, NN (no cf) */

    case 0x8:
        switch (N) {
        case 0x0: c->V[X]  = c->V[Y]; break;               /* LD  Vx, Vy         */
        case 0x1: c->V[X] |= c->V[Y]; break;               /* OR                 */
        case 0x2: c->V[X] &= c->V[Y]; break;               /* AND                */
        case 0x3: c->V[X] ^= c->V[Y]; break;               /* XOR                */
        case 0x4: {                                        /* ADD Vx, Vy (carry) */
            uint16_t sum = c->V[X] + c->V[Y];
            c->V[X] = (uint8_t)sum;
            c->V[0xF] = sum > 0xFF;         /* flag written last: X==F is fine   */
        } break;
        case 0x5: {                                        /* SUB Vx, Vy         */
            uint8_t nb = c->V[X] >= c->V[Y];
            c->V[X] -= c->V[Y];
            c->V[0xF] = nb;                 /* VF = NOT borrow                   */
        } break;
        case 0x6: {                                        /* SHR Vx             */
            uint8_t lsb = c->V[X] & 1;
#if LEGACY_SHIFT
            c->V[X] = c->V[Y];
#endif
            c->V[X] >>= 1;
            c->V[0xF] = lsb;
        } break;
        case 0x7: {                                        /* SUBN Vx, Vy        */
            uint8_t nb = c->V[Y] >= c->V[X];
            c->V[X] = c->V[Y] - c->V[X];
            c->V[0xF] = nb;
        } break;
        case 0xE: {                                        /* SHL Vx             */
            uint8_t msb = (c->V[X] >> 7) & 1;
#if LEGACY_SHIFT
            c->V[X] = c->V[Y];
#endif
            c->V[X] <<= 1;
            c->V[0xF] = msb;
        } break;
        }
        break;

    case 0x9: if (c->V[X] != c->V[Y]) c->pc += 2; break;   /* SNE Vx, Vy         */
    case 0xA: c->I = NNN; break;                            /* LD  I, NNN         */
    case 0xB: c->pc = NNN + c->V[0]; break;                 /* JP  V0, NNN        */
    case 0xC: c->V[X] = (rand() & 0xFF) & NN; break;        /* RND Vx, NN         */

    case 0xD: {                                            /* DRW Vx, Vy, N      */
        uint8_t x0 = c->V[X] % WIDTH;      /* start pos wraps ...               */
        uint8_t y0 = c->V[Y] % HEIGHT;
        c->V[0xF] = 0;
        for (uint8_t row = 0; row < N; row++) {
            if (y0 + row >= HEIGHT) break; /* ... but sprites clip at the edges */
            uint8_t bits = c->mem[c->I + row];
            for (uint8_t col = 0; col < 8; col++) {
                if (x0 + col >= WIDTH) break;
                if (bits & (0x80 >> col)) {
                    int idx = (x0 + col) + (y0 + row) * WIDTH;
                    if (c->gfx[idx]) c->V[0xF] = 1;   /* collision => set flag  */
                    c->gfx[idx] ^= 1;                 /* sprites XOR onto screen */
                }
            }
        }
        c->draw_flag = 1;
    } break;

    case 0xE:
        if (NN == 0x9E) { if ( c->keys[c->V[X] & 0xF]) c->pc += 2; } /* SKP  */
        else if (NN == 0xA1) { if (!c->keys[c->V[X] & 0xF]) c->pc += 2; } /* SKNP */
        break;

    case 0xF:
        switch (NN) {
        case 0x07: c->V[X] = c->delay; break;              /* LD Vx, DT          */
        case 0x0A: {                                       /* LD Vx, K (blocking)*/
            int pressed = -1;
            for (int k = 0; k < 16; k++) if (c->keys[k]) { pressed = k; break; }
            if (pressed < 0) c->pc -= 2;   /* no key yet: re-run this op         */
            else c->V[X] = (uint8_t)pressed;
        } break;
        case 0x15: c->delay = c->V[X]; break;              /* LD DT, Vx          */
        case 0x18: c->sound = c->V[X]; break;              /* LD ST, Vx          */
        case 0x1E: c->I += c->V[X]; break;                 /* ADD I, Vx          */
        case 0x29: c->I = FONT_START + (c->V[X] & 0xF) * 5; break; /* LD F, Vx    */
        case 0x33:                                         /* LD B, Vx (BCD)     */
            c->mem[c->I]     =  c->V[X] / 100;
            c->mem[c->I + 1] = (c->V[X] / 10) % 10;
            c->mem[c->I + 2] =  c->V[X] % 10;
            break;
        case 0x55:                                         /* LD [I], V0..Vx     */
            for (int i = 0; i <= X; i++) c->mem[c->I + i] = c->V[i];
#if LEGACY_MEM
            c->I += X + 1;
#endif
            break;
        case 0x65:                                         /* LD V0..Vx, [I]     */
            for (int i = 0; i <= X; i++) c->V[i] = c->mem[c->I + i];
#if LEGACY_MEM
            c->I += X + 1;
#endif
            break;
        }
        break;
    }
}

/* ==========================================================================
 * Platform layer. Everything above is portable; everything below is I/O.
 * ========================================================================== */

#ifdef USE_SDL

/* COSMAC keypad -> QWERTY:
 *   1 2 3 C        1 2 3 4
 *   4 5 6 D   ==>  Q W E R
 *   7 8 9 E        A S D F
 *   A 0 B F        Z X C V                                                    */
static int key_for(SDL_Keycode k) {
    switch (k) {
    case SDLK_1: return 0x1; case SDLK_2: return 0x2; case SDLK_3: return 0x3; case SDLK_4: return 0xC;
    case SDLK_q: return 0x4; case SDLK_w: return 0x5; case SDLK_e: return 0x6; case SDLK_r: return 0xD;
    case SDLK_a: return 0x7; case SDLK_s: return 0x8; case SDLK_d: return 0x9; case SDLK_f: return 0xE;
    case SDLK_z: return 0xA; case SDLK_x: return 0x0; case SDLK_c: return 0xB; case SDLK_v: return 0xF;
    default: return -1;
    }
}

#define SCALE 12
#define CPU_HZ 700           /* instructions/sec; ~600-1000 feels right         */

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <rom.ch8>\n", argv[0]); return 1; }
    Chip8 c; c8_init(&c);
    int size = c8_load(&c, argv[1]);
    if (size < 0) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *win = SDL_CreateWindow("chip8",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIDTH * SCALE, HEIGHT * SCALE, 0);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);

    const int cycles_per_frame = CPU_HZ / 60;
    uint32_t pixels[WIDTH * HEIGHT];
    int running = 1;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            else if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
                if (ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
                int k = key_for(ev.key.keysym.sym);
                if (k >= 0) c.keys[k] = (ev.type == SDL_KEYDOWN);
            }
        }

        for (int i = 0; i < cycles_per_frame; i++) c8_cycle(&c);
        c8_tick_timers(&c);

        if (c.draw_flag) {
            for (int i = 0; i < WIDTH * HEIGHT; i++)
                pixels[i] = c.gfx[i] ? 0xFFFFFFFFu : 0x000000FFu;
            SDL_UpdateTexture(tex, NULL, pixels, WIDTH * sizeof(uint32_t));
            c.draw_flag = 0;
        }
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        SDL_Delay(1000 / 60);   /* crude frame pacing; good enough for an MVP    */
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

#else  /* ---- headless: no external deps, dumps the screen as text ---- */

static void render_ascii(const Chip8 *c) {
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++)
            putchar(c->gfx[x + y * WIDTH] ? '#' : ' ');
        putchar('\n');
    }
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <rom.ch8> [steps]\n", argv[0]); return 1; }
    Chip8 c; c8_init(&c);
    int size = c8_load(&c, argv[1]);
    if (size < 0) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    fprintf(stderr, "loaded %d bytes\n", size);

    long steps = (argc >= 3) ? strtol(argv[2], NULL, 10) : 500;
    for (long i = 0; i < steps; i++) {
        c8_cycle(&c);
        if (i % 12 == 0) c8_tick_timers(&c);  /* ~CPU_HZ/60 cycles per tick     */
    }
    render_ascii(&c);
    return 0;
}

#endif
