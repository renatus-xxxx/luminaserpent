/*
 * LUMINA SERPENT for MSX2 / MSX2+ - an original side-scrolling STG.
 *
 * Built with z88dk (sccz80) + msxalib / msxclib by yamayamaru
 * (used unmodified). The MSX port of the X68000 original
 * (elf2x68k, ~/workspace/x68k/github/luminaserpent/): same systems -
 * the bending "Light Serpent" laser fed by a flight-history ring,
 * RESONANCE -> PRISM BURST, item-rotation power-ups
 * (THRUST / COMET / DUAL / PRISM / ECHO / HALO), prism crystals and
 * the boss ABYSS PRISM LEVIATHAN shielded by 4 orbiting shards.
 * Sizes / speeds are rescaled from 512x480 to SCREEN 5 (256x212).
 *
 * Horizontal scrolling follows Konami's SPACE MANBOW (MSX, 1989):
 * a coarse scroll in 8-dot steps combined with a 1-dot fine scroll,
 * and the fine-scroll implementation switches by VDP:
 *   - V9958 (MSX2+ / turboR): hardware scroll R#26/R#27 with
 *     R#25 MSK (left 8 dots masked, so the border does not flip)
 *     and SP2 (two-page, 512-dot wide ring). Coarse steps are also
 *     done by R#26; new 8-dot columns are streamed into the hidden
 *     half of the ring. No bitmap copy ever happens.
 *   - V9938 (MSX2): SET ADJUST (R#18) provides the fine scroll
 *     -7..0 dots (the border flips, exactly as in Manbow on MSX2),
 *     and every 8 dots the bitmap play area is shifted left 8 dots
 *     with one HMMM command and a new column is drawn at the right
 *     edge - Manbow's software coarse step, bitmap version.
 * A line-interrupt screen split (H.KEYI hook, R#19) keeps the top
 * HUD (lines 0..15) and the bottom equipment band (184..211)
 * un-scrolled on both paths: the split applies scroll=0 above line
 * 16 and below line 184, the game scroll in between.
 *
 * Audio: PSG direct OUT (ports 0xA0/0xA1, register shadow), the
 * STAR BREAKER (MSX) sequencer: A = arpeggio, B = bass, C = SE,
 * advanced once per frame from wait_frame(). Screen changes follow
 * the established pattern: silence -> draw -> restart BGM.
 * Input: PPI matrix read directly and latched during the frame
 * wait (short pulses are not missed), joystick merged per frame.
 */

#include <string.h>
#include <msxalib.h>
#include <msxclib.h>
#include "font.h"
#include "title_img.h"
#include "planet_img.h"

#define JIFFY   (*(volatile unsigned char *)0xfc9e)
#define ACPAGE  (*(unsigned char *)0xfaf6)
#define ATRBAS  (*(unsigned int *)0xf928)

/* ---- palette indices (SCREEN 5) ---- */
#define C_BLACK   0
#define C_BG      1    /* deep space          */
#define C_BG2     2    /* space mid / nebula  */
#define C_PURPLE  3
#define C_EARTHD  4    /* planet shade        */
#define C_EARTH   5    /* planet ocean        */
#define C_LAND    6
#define C_CYAN    7
#define C_ICE     8
#define C_GREEN   9
#define C_YELLOW  10
#define C_ORANGE  11
#define C_RED     12
#define C_PINK    13
#define C_WHITE   14
#define C_BLINK   15   /* palette-animated colour (prompt/WARNING) */

/* ---- layout ---- */
#define HUD_H     16
#define PLAY_Y0   16
#define PLAY_Y1   183
#define PLAY_H    (PLAY_Y1 - PLAY_Y0 + 1)
#define BAND_Y    184

#define STAGE_LEN 2800
#define WORLD_EARTH_X 760
#define EARTH_CY  104
#define EARTH_R   58

static const short sin64_tab[64] = {
  0, 25, 50, 74, 98, 120, 142, 162, 180, 197, 212, 225, 236, 244, 250, 254,
  256, 254, 250, 244, 236, 225, 212, 197, 180, 162, 142, 120, 98, 74, 50, 25,
  0, -25, -50, -74, -98, -120, -142, -162, -180, -197, -212, -225, -236, -244, -250, -254,
  -256, -254, -250, -244, -236, -225, -212, -197, -180, -162, -142, -120, -98, -74, -50, -25
};
static int sin64(int idx) { return sin64_tab[idx & 63]; }
static int cos64(int idx) { return sin64_tab[(idx + 16) & 63]; }
static int iabs(int v) { return v < 0 ? -v : v; }

static unsigned char last_jiffy;
static long frame_count;

/* ============================================================
 * VDP register access / VDP identification
 * wrport/rdport live in msxalib.c. getstat_m() is NOT used in
 * this file: its GETSTAT loads BC from memory and leaves a
 * layout-dependent garbage byte in B, which makes its port
 * accesses miss the VDP under WebMSX (measured garbage values
 * 0x99 / 0xB2 / 0xB7 for the same register across builds; an
 * odd garbage value also hangs command-wait polls forever).
 * vdp_stat() below is the safe replacement: B forced to 0,
 * interrupts masked across the select/read pair, R#15 restored
 * to 0 before returning. (msxalib's own GETSTAT carries the
 * same B = 0 patch so its internal WAITVDP polls are safe.)
 * ============================================================ */
extern char wrport;
extern char rdport;

static unsigned char vdp_stat_val;
static void vdp_stat(int r)
{
#asm
    ld      hl,2
    add     hl,sp
    ld      d,(hl)          ; r
    di
    ld      a,(_wrport)
    inc     a
    ld      c,a
    ld      b,0
    ld      a,d
    out     (c),a           ; value = r
    ld      a,8fh
    out     (c),a           ; select status register via R#15
    ld      a,(_rdport)
    inc     a
    ld      c,a
    ld      b,0
    in      a,(c)
    ld      (_vdp_stat_val),a
    ld      a,(_wrport)
    inc     a
    ld      c,a
    ld      b,0
    xor     a
    out     (c),a           ; restore R#15 = 0
    ld      a,8fh
    out     (c),a
    ei
#endasm
}

static int vr_reg, vr_val;
static void vdp_reg(int reg, int val)
{
    vr_reg = reg;
    vr_val = val;
    /* Keep the BIOS register shadows in sync for the registers a
       BIOS/DOS path might restore from them (R#0 mode, R#2 page):
       a stale shadow can silently resurrect an old value - e.g.
       R#0 with IE1 set - long after we wrote the register. */
    if (reg == 0) *(unsigned char *)0xf3df = (unsigned char)val;
    if (reg == 2) *(unsigned char *)0xf3e1 = (unsigned char)val;
#asm
    ld      a,(_wrport)
    inc     a
    ld      c,a
    ld      b,0
    di
    ld      a,(_vr_val)
    out     (c),a
    ld      a,(_vr_reg)
    or      80h
    out     (c),a
    ei
#endasm
}

/* TITLETRACE: paint a colour bar (CPU VRAM writes only, no VDP
   commands, so it cannot hang on a command wait) at the top-left
   of the given page after each init/title step. The bar colour =
   the step number, read off the frozen screen. */
#ifdef TITLETRACE
static unsigned char trace_buf[24];
static void trace_step(int n, unsigned int base)
{
    int y, i;
    unsigned char b = (unsigned char)((n << 4) | (n & 15));
    for (i = 0; i < 24; i++) trace_buf[i] = b;
    for (y = 0; y < 12; y++)
        ldirvm(base + (unsigned int)(y * 128), (char *)trace_buf, 24);
#ifdef TRACESLOW
    {
        unsigned int t0 = JIFFY;
        while ((unsigned int)(JIFFY - t0) < 150) { }
    }
#endif
}
#else
#define trace_step(n, base)
#endif

/* VDP id from status register 1, bits 5..1:
   V9938 -> 0, V9958 -> 2. */
static int is_v9958;
static void detect_vdp(void)
{
#ifdef DETECT_OFF
    is_v9958 = 0;   /* bisect build: force the V9938 path */
    return;
#else
    vdp_stat(1);
    is_v9958 = (((vdp_stat_val >> 1) & 0x1f) == 2);
#endif
}

/* turboR CPU: on a turboR switch the CPU to R800 DRAM mode
   through the BIOS CHGCPU entry at 0x0180 (A = 0x82), the same
   mode HELIX WING selects. The DOS environment needs its own
   delivery, worked out with a diagnostic build on the FS-A1GT:
   the DOS interslot stubs proved unreliable for the guard
   reads, and any code that flips a slot must not itself sit in
   the page being flipped (the same lesson as the split
   handler). So the trampoline below is copied to a BSS buffer
   outside page 0 and run from there: it flips ONLY the page-0
   PPI field to the main ROM primary (EXPTBL[0]; that primary
   slot's secondary register still holds its boot-time page-0
   selection), reads MSXVER (0x002D) and the CHGCPU opcode
   (0x0180) as guards, calls CHGCPU when both pass, and restores
   the PPI from the stack. While flipped it stores nothing to
   memory; registers it relies on across the BIOS call are
   stacked. On any other machine the guards fail and the game
   runs on the Z80 exactly as before. */
static unsigned char is_turbor;
static void cpu_trampoline_src(void)
{
#asm
    di
    in      a,(0a8h)
    push    af
    and     0fch
    ld      b,a
    ld      a,(0fcc1h)
    and     3
    or      b
    out     (0a8h),a
    ld      d,0
    ld      a,(0002dh)
    cp      3
    jr      nz,_ctr_jp
    set     0,d
_ctr_jp:
    ld      a,(00180h)
    cp      0c3h
    jr      nz,_ctr_go
    set     1,d
_ctr_go:
    bit     0,d
    jr      z,_ctr_restore
    bit     1,d
    jr      z,_ctr_restore
    push    de
    push    ix
    ld      a,082h
    call    00180h
    pop     ix
    pop     de
    set     2,d
_ctr_restore:
    pop     af
    out     (0a8h),a
    ei
    ld      a,d
    and     4
    jr      z,_ctr_store
    ld      a,1
_ctr_store:
    ld      (_is_turbor),a
    ret
_cpu_trampoline_end:
#endasm
}
extern unsigned char cpu_trampoline_end;
static unsigned char cpu_tramp_buf[96];
static void cpu_init_r800(void)
{
    unsigned int sz;
    is_turbor = 0;
    if ((unsigned int)cpu_tramp_buf < 0x4000) return;
    sz = (unsigned int)&cpu_trampoline_end
       - (unsigned int)cpu_trampoline_src;
    if (sz > sizeof(cpu_tramp_buf)) return;
    memcpy(cpu_tramp_buf, (unsigned char *)cpu_trampoline_src, sz);
    ((void (*)(void))cpu_tramp_buf)();
}

static void wait_vdp_cmd(void)
{
    do {
        vdp_stat(2);
    } while (vdp_stat_val & 1);
}

/* ---- HMMM (VRAM->VRAM high-speed copy) via command registers ---- */
static unsigned char hmmm_par[15];
static void vdp_hmmm(int sx, int sy, int dx, int dy, int nx, int ny)
{
    hmmm_par[0] = (unsigned char)(sx & 0xff);
    hmmm_par[1] = (unsigned char)((sx >> 8) & 1);
    hmmm_par[2] = (unsigned char)(sy & 0xff);
    hmmm_par[3] = (unsigned char)((sy >> 8) & 3);
    hmmm_par[4] = (unsigned char)(dx & 0xff);
    hmmm_par[5] = (unsigned char)((dx >> 8) & 1);
    hmmm_par[6] = (unsigned char)(dy & 0xff);
    hmmm_par[7] = (unsigned char)((dy >> 8) & 3);
    hmmm_par[8] = (unsigned char)(nx & 0xff);
    hmmm_par[9] = (unsigned char)((nx >> 8) & 3);
    hmmm_par[10] = (unsigned char)(ny & 0xff);
    hmmm_par[11] = (unsigned char)((ny >> 8) & 3);
    hmmm_par[12] = 0;
    hmmm_par[13] = 0;          /* argument: DIX=0, DIY=0 */
    hmmm_par[14] = 0xd0;       /* HMMM command */
    wait_vdp_cmd();
#asm
    ld      a,(_wrport)
    inc     a
    ld      c,a
    ld      b,0
    di
    ld      a,32
    out     (c),a
    ld      a,91h           ; register 17 + 80h (indirect select)
    out     (c),a
    inc     c
    inc     c               ; command data port (wrport + 2)
    ld      hl,_hmmm_par
    ld      b,15
    otir
    ei
#endasm
    wait_vdp_cmd();
}

/* ============================================================
 * Line-interrupt screen split (H.KEYI hook)
 *   split_stage 0: next line interrupt is at line 16 -> apply the
 *                  game scroll, re-arm at line 184.
 *   split_stage 1: next line interrupt is at line 184 -> apply
 *                  scroll 0, re-arm at line 16.
 * The VBlank period and the HUD/band lines therefore always run
 * with scroll 0 on both VDP paths. The handler only writes the
 * scroll registers (and R#19); it reads status register 1 to
 * clear the line-interrupt flag and restores R#15 to 0.
 * ============================================================ */
static unsigned char split_stage;
static unsigned char hook_calls;   /* DIAG5: incremented by the handler */
static unsigned char hook_do_arm, hook_do_apply;   /* bisect flags */
static unsigned char cur_r26, cur_r27, cur_adj;

static void split_handler(void);   /* defined at the end of this file: the H.KEYI target must live in high memory (page 2/3), never in page 0/1 - see the note at its definition */

static unsigned char hook_saved[5];
static unsigned char hook_installed;
static unsigned char hook_armed;
static unsigned char hook_img[512];   /* relocated handler (BSS, page 2) */
extern void split_handler_tail(void);
static void split_install(void)
{
#ifdef NOHOOK
    return;   /* diagnostic build: no line-interrupt screen split */
#else
    unsigned char *hook = (unsigned char *)0xfd9a;   /* H.KEYI */
    int i;
    for (i = 0; i < 5; i++) hook_saved[i] = hook[i];
    /* Relocate the handler into page 2 (see split_handler_tail). */
    {
        unsigned char *srcb = (unsigned char *)split_handler;
        unsigned char *endb = (unsigned char *)split_handler_tail;
        int hlen = (int)(endb - srcb);
        int i2;
        if (hlen < 0 || hlen > (int)sizeof(hook_img))
            hlen = (int)sizeof(hook_img);
        for (i2 = 0; i2 < hlen; i2++) hook_img[i2] = srcb[i2];
    }
    split_stage = 0;
    cur_r26 = cur_r27 = cur_adj = 0;
#if defined(HOOK_LVL) && HOOK_LVL == 1
    hook_do_arm = 0; hook_do_apply = 0;   /* clear S#1 only */
#elif defined(HOOK_LVL) && HOOK_LVL == 2
    hook_do_arm = 1; hook_do_apply = 0;   /* + R#19 re-arm */
#else
    hook_do_arm = 1; hook_do_apply = 1;   /* full handler */
#endif
    vdp_reg(19, 16);
    hook[0] = 0xc3;                                  /* JP handler */
    hook[1] = (unsigned char)((int)hook_img & 0xff);
    hook[2] = (unsigned char)((int)hook_img >> 8);
#if defined(HOOK_LVL) && HOOK_LVL == 0
    /* diagnostic: IE1 stays off (superseded by the frame_sync gate) */
#endif
    hook_installed = 1;
    /* IE1 is armed PERMANENTLY. The pre-v1.1 design armed it only
       inside frame_sync's JIFFY wait, so the stage machine stalled
       for the ~9 display frames of each game frame's drawing phase
       and the VDP kept the last-applied scroll on EVERY line -
       HUD and equipment band included. Whenever the frozen scroll
       was the game scroll, those lines showed the ring window at
       the game offset, i.e. page 1's uninitialised VRAM (a 0x55
       fill on many machines/emulators): the flickering bright-blue
       bands reported from WebMSX play. Permanent arming is safe
       because every main-thread VDP port sequence is DI-bounded
       (vdp_reg, vdp_stat, palett, boxfil/line_m/cpym2v, vdp_hmmm,
       blit_rle) and spr_flush wraps its ldirvm calls in DI; the
       handler itself only ever writes complete control-port pairs
       and restores R#15 = 0. The handler also force-resyncs its
       stage on the BIOS VBlank call (FH = 0), so a stage missed
       inside a DI stretch re-aligns on the next display frame. */
    hook_armed = 1;
    vdp_stat(1);          /* drop any stale line-IRQ flag first */
    vdp_reg(0, 0x16);     /* IE1 on */
#endif
}
static void split_remove(void)
{
#ifdef NOHOOK
    return;
#else
    unsigned char *hook = (unsigned char *)0xfd9a;
    int i;
    hook_armed = 0;          /* handler becomes a no-op first */
    hook_installed = 0;
    vdp_reg(0, 0x06);        /* IE1 off */
    vdp_reg(19, 0);
    for (i = 0; i < 5; i++) hook[i] = hook_saved[i];
    if (is_v9958) {
        vdp_reg(25, 0);
        vdp_reg(26, 0);
        vdp_reg(27, 0);
        vdp_reg(2, 0x1f);
    } else {
        vdp_reg(18, 0);
    }
#endif
}

/* The line interrupt is armed permanently at split_install()
   (see there for why that is safe); frame_sync only waits for
   the next VBlank tick. */
static void wait_frame(void);   /* defined with the audio section */

static void frame_sync(void)
{
    wait_frame();
}

/* ============================================================
 * Background: deterministic column generator
 * Any 8-dot column of the world can be rendered from its world X
 * alone (gradient bands + wavy nebula + hashed star cells + the
 * planet at WORLD_EARTH_X), which is what both scroll paths need:
 * the MSX2 path draws one column at the right edge after each
 * HMMM shift, the V9958 path streams columns into the ring.
 * ============================================================ */
static unsigned char sqrt_tab[EARTH_R + 1];
static void init_sqrt_tab(void)
{
    int a, y;
    for (a = 0; a <= EARTH_R; a++) {
        int best = 0;
        for (y = 0; y <= EARTH_R; y++)
            if (y * y <= EARTH_R * EARTH_R - a * a) best = y;
        sqrt_tab[a] = (unsigned char)best;
    }
}

static unsigned int cell_hash(int cx, int cy)
{
    unsigned int h = (unsigned int)cx * 40503u + (unsigned int)cy * 22697u;
    h ^= h << 3;
    h ^= h >> 5;
    return h;
}

/* draw the 8-dot column for world X = wx into (lx, page) */
static void draw_slice(int lx, int page, long wx)
{
    int sx, cy, y;
    ACPAGE = (unsigned char)page;
    /* gradient bands */
    boxfil(lx, PLAY_Y0, lx + 7, 63, C_BG, 0);
    boxfil(lx, 64, lx + 7, 111, C_BG2, 0);
    boxfil(lx, 112, lx + 7, 159, C_BG, 0);
    boxfil(lx, 160, lx + 7, PLAY_Y1, C_BG2, 0);
    /* nebula: wavy purple band, checker-dithered by column */
    if ((wx & 8) == 0) {
        int c = 96 + (sin64((int)(wx >> 4) & 63) >> 4);
        boxfil(lx, c - 5, lx + 7, c + 5, C_PURPLE, 0);
    }
    /* stars: one per 16x16 world cell, hashed position */
    {
        int cx0 = (int)(wx >> 4);
        for (cy = 1; cy <= 11; cy++) {
            unsigned int h = cell_hash(cx0, cy);
            int swx = cx0 * 16 + 1 + (int)(h % 13);
            int sy;
            if (swx < wx || swx >= wx + 8) continue;
            sy = cy * 16 + 2 + (int)((h >> 4) & 7);
            if (sy < PLAY_Y0 || sy > PLAY_Y1) continue;
            sx = lx + (swx - (int)wx);
            boxfil(sx, sy, sx + ((h & 0x200) ? 1 : 0), sy,
                   (h >> 9) & 1 ? C_WHITE : C_ICE, 0);
        }
    }
    /* the planets are baked images now (tools/gen_title_art.py):
       the title and game Earths are blitted over the slices by
       title_screen() / play_game() - see planet_img.h. The old
       procedural flat circle was removed from the slices. */
}

/* ============================================================
 * Scroll engine state
 * ============================================================ */
static int disp_page;          /* page the display shows at scroll 0 */
static long scroll_x;          /* world X at the left edge (+fine) */
static long base_x;            /* MSX2: coarse base (scroll_x & ~7) */
static long streamed_x;        /* V9958: world X streamed up to */

static void scroll_setup_stage(void)
{
    int vx;
    scroll_x = 0;
    if (is_v9958) {
        disp_page = 0;   /* V9958 ring: pages 0+1, left = 0 */
        vdp_reg(25, 3);        /* MSK + SP2: two-page ring, masked */
        vdp_reg(2, 0x3f);      /* SP2 ring = pages 0+1 (measured on
                                  V9958: R#2 bit5 gates the ring's
                                  second page; base is page 0) */
        vdp_reg(26, 0);
        vdp_reg(27, 0);
        cur_r26 = cur_r27 = 0;
        /* The slices only paint the play lines; the rest of page 1
           keeps whatever VRAM power-on/BIOS state it had (a 0x55
           fill on many machines - bright blue with this palette).
           If a split stage ever leaks the ring window onto the
           HUD/band lines, that garbage is what shows. Clear both
           pages up front so a leak shows black, not garbage. */
        ACPAGE = 0;
        boxfil(0, 0, 255, 211, C_BLACK, 0);
        ACPAGE = 1;
        boxfil(0, 0, 255, 211, C_BLACK, 0);
        for (vx = 0; vx < 512; vx += 8)
            draw_slice(vx & 255, (vx >> 8), vx);
        streamed_x = 512;
    } else {
        disp_page = 0;
        vdp_reg(18, 0);
        cur_adj = 0;
        for (vx = 0; vx < 256; vx += 8)
            draw_slice(vx, 0, vx);
        base_x = 0;
    }
}

/* fill the visible screen with the world at the given offset
   (title screen: the planet view). Play-area columns only. */
static void scroll_fill_title(long w0)
{
    int lx;
    if (is_v9958) {
        for (lx = 0; lx < 256; lx += 8)
            draw_slice(lx, 0, w0 + lx);
    } else {
        for (lx = 0; lx < 256; lx += 8)
            draw_slice(lx, 0, w0 + lx);
    }
}

static void scroll_update(int scrolling)
{
    if (!scrolling) return;
    scroll_x++;
#ifdef NOSCROLL
    /* Diagnostic variant: the logic distance still advances
       (spawning and the stage length work as usual) but the
       background is neither scrolled nor streamed - the initial
       ring fill stays on screen, frozen. */
    cur_r26 = 0;
    cur_r27 = 0;
    cur_adj = 0;
    return;
#else
    if (is_v9958) {
        int L = (int)(scroll_x & 511);
        cur_r26 = (unsigned char)(((L + 7) >> 3) & 63);
        cur_r27 = (unsigned char)((8 - (L & 7)) & 7);
        while (streamed_x < scroll_x + 264) {
            int vx = (int)(streamed_x & 511);
            draw_slice(vx & 255, (vx >> 8), streamed_x);
            streamed_x += 8;
        }
    } else {
        long newbase = scroll_x & ~7L;
        cur_adj = (unsigned char)((-(int)(scroll_x & 7)) & 15);
        if (newbase != base_x) {
            vdp_hmmm(8, PLAY_Y0, 0, PLAY_Y0, 248, PLAY_H);
            draw_slice(248, 0, newbase + 248);
            base_x = newbase;
        }
    }
#endif
}

static void scroll_stop(void)
{
    /* Freeze: the split handler keeps applying the last scroll
       values (cur_r26/cur_r27 on V9958, cur_adj on V9938), so the
       background stays exactly where it stopped for the boss. */
}

/* ============================================================
 * Audio (PSG direct OUT, register shadow) - STAR BREAKER engine
 *   A: arpeggio (BGM)   B: bass (BGM)   C: sound effects
 * BGM scenes: 0 title (ambient, slow), 1 stage (active synth),
 * 2 boss (driving). Stage chords follow the X68000 original:
 * Cm add9 -> Ab -> Eb -> Bb.
 * ============================================================ */
static const unsigned int note_period[12] = {
    428, 404, 381, 360, 340, 320, 302, 285, 269, 254, 240, 226
};
static unsigned int period_for(int midi)
{
    unsigned int p = note_period[midi % 12];
    int oct = midi / 12 - 1;
    while (oct > 4) { p >>= 1; oct--; }
    while (oct < 4) { p <<= 1; oct++; }
    return p;
}
#define BGM_STEPS 64
static const unsigned char bgm_chords[3][4][4] = {
    { {57,60,64,71}, {53,57,60,65}, {55,59,62,67}, {50,53,57,62} },
    { {60,63,67,74}, {56,60,63,70}, {63,67,70,74}, {58,62,65,72} },
    { {57,60,64,69}, {53,57,60,65}, {48,52,55,64}, {52,56,59,64} }
};
static const unsigned char bgm_roots[3][4] = {
    { 45, 41, 43, 38 }, { 48, 44, 39, 46 }, { 45, 41, 48, 40 }
};
static const unsigned char bgm_step_frames_tab[3] = { 6, 3, 2 };
static const unsigned char arp_idx[16] = {
    0, 1, 2, 3,  0, 1, 2, 3,  0, 1, 2, 3,  2, 1, 2, 3
};
static int bgm_mode, bgm_step, bgm_frame, bgm_on, bgm_step_frames;

static const unsigned int *se_notes;
static const unsigned char *se_durs;
static int se_len, se_pos, se_timer, se_active;
static void se_start(const unsigned int *notes, const unsigned char *durs, int len)
{
    se_notes = notes; se_durs = durs; se_len = len;
    se_pos = 0; se_timer = 0; se_active = 1;
}
static const unsigned int se_shot_n[] = { 320 };
static const unsigned char se_shot_d[] = { 2 };
static const unsigned int se_hit_n[] = { 110 };
static const unsigned char se_hit_d[] = { 3 };
static const unsigned int se_explo_n[] = { 90, 140, 220, 340 };
static const unsigned char se_explo_d[] = { 3, 3, 4, 5 };
static const unsigned int se_power_n[] = { 214, 170, 143, 107, 90 };
static const unsigned char se_power_d[] = { 4, 4, 4, 4, 7 };
static const unsigned int se_miss_n[] = { 200, 260, 340, 460, 620 };
static const unsigned char se_miss_d[] = { 4, 4, 4, 4, 6 };
static const unsigned int se_laser_n[] = { 760 };
static const unsigned char se_laser_d[] = { 2 };
#define SE_SHOT   se_start(se_shot_n, se_shot_d, 1)
#define SE_HIT    se_start(se_hit_n, se_hit_d, 1)
#define SE_EXPLO  se_start(se_explo_n, se_explo_d, 4)
#define SE_POWER  se_start(se_power_n, se_power_d, 5)
#define SE_MISS   se_start(se_miss_n, se_miss_d, 5)
#define SE_LASER  se_start(se_laser_n, se_laser_d, 1)

static unsigned char psg_shadow[16];
static void psg_write(int reg, int val)
{
#asm
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    inc hl
    ld a,(hl)
    out (0a0h),a
    ld a,e
    out (0a1h),a
#endasm
}
static void psg_set(int reg, int val)
{
    if (psg_shadow[reg] != (unsigned char)val) {
        psg_shadow[reg] = (unsigned char)val;
        psg_write(reg, val);
    }
}
static void psg_tone(int ch, unsigned int period, int vol)
{
    psg_set(ch * 2, period & 0xff);
    psg_set(ch * 2 + 1, (period >> 8) & 0x0f);
    psg_set(8 + ch, vol);
}
static void audio_init(void)
{
    int i;
    for (i = 0; i < 16; i++) psg_shadow[i] = 0xff;
    gicini();
    psg_set(7, 0x38);
    psg_set(8, 0); psg_set(9, 0); psg_set(10, 0);
    bgm_step = 0; bgm_frame = 0; bgm_on = 1; bgm_mode = 0;
    bgm_step_frames = bgm_step_frames_tab[0];
    se_active = 0;
}
static void audio_stop(void)
{
    psg_set(8, 0); psg_set(9, 0); psg_set(10, 0); psg_set(7, 0x3f);
}
static void audio_silence(void)
{
    bgm_on = 0; se_active = 0;
    psg_set(8, 0); psg_set(9, 0); psg_set(10, 0);
}
static void bgm_restart(int mode)
{
    bgm_mode = mode;
    bgm_step_frames = bgm_step_frames_tab[mode];
    bgm_step = 0; bgm_frame = 0; bgm_on = 1;
}
static void audio_update(void)
{
    if (se_active) {
        if (se_timer == 0) {
            if (se_pos >= se_len) {
                se_active = 0;
                psg_set(10, 0);
            } else {
                psg_tone(2, se_notes[se_pos], 13);
                se_timer = se_durs[se_pos];
                se_pos++;
            }
        }
        if (se_active) se_timer--;
    }
    if (bgm_on) {
        int bar = bgm_step >> 4;
        if (bgm_frame == 0) {
            psg_tone(0, period_for(bgm_chords[bgm_mode][bar][arp_idx[bgm_step & 15]]), 12);
            if ((bgm_step & 3) == 0) {
                int sub = (bgm_step >> 2) & 3;
                psg_tone(1, period_for(bgm_roots[bgm_mode][bar] + ((sub & 1) ? 12 : 0)), 9);
            }
        } else {
            psg_set(8, 12 - bgm_frame * (12 / bgm_step_frames));
        }
        psg_set(9, 9 - (((bgm_step & 3) * bgm_step_frames + bgm_frame)
                        / (bgm_step_frames + 1)));
        bgm_frame++;
        if (bgm_frame >= bgm_step_frames) {
            bgm_frame = 0;
            bgm_step = (bgm_step + 1) & (BGM_STEPS - 1);
        }
    }
}

/* ============================================================
 * Input: PPI matrix latched during the frame wait + joystick.
 * Row 8: bit7 RIGHT, bit6 DOWN, bit5 UP, bit4 LEFT, bit0 SPACE
 * (bit = 0 while pressed). Row 7: bit2 ESC.
 * ============================================================ */
static unsigned char kbd_row(int row)
{
#asm
    ld hl,2
    add hl,sp
    ld e,(hl)
    in a,(0aah)
    ld d,a
    and 0f0h
    or e
    out (0aah),a
    in a,(0a9h)
    ld l,a
    ld h,0
    ld a,d
    out (0aah),a
#endasm
}
static unsigned char lat_left, lat_right, lat_up, lat_down, lat_trig, lat_esc;
static void poll_input(void)
{
    unsigned char r8 = kbd_row(8);
    unsigned char r7;
    if ((r8 & 0x80) == 0) lat_right = 1;
    if ((r8 & 0x40) == 0) lat_down = 1;
    if ((r8 & 0x20) == 0) lat_up = 1;
    if ((r8 & 0x10) == 0) lat_left = 1;
    if ((r8 & 0x01) == 0) lat_trig = 1;
    r7 = kbd_row(7);
    if ((r7 & 0x04) == 0) lat_esc = 1;
}
static void merge_joystick(void)
{
    unsigned char st = gtstck(1);
    if (st == 1 || st == 2 || st == 8) lat_up = 1;
    if (st == 4 || st == 5 || st == 6) lat_down = 1;
    if (st == 6 || st == 7 || st == 8) lat_left = 1;
    if (st == 2 || st == 3 || st == 4) lat_right = 1;
    if (gttrig(1)) lat_trig = 1;
}
static void wait_frame(void)
{
    unsigned char now;
    lat_left = lat_right = lat_up = lat_down = lat_trig = lat_esc = 0;
    do {
        poll_input();
        now = JIFFY;
    } while (now == last_jiffy);
    last_jiffy = now;
    frame_count++;
    audio_update();
}
/* wait for a fresh trigger press (1) or ESC (0) */
static int wait_trigger_or_esc(void)
{
    for (;;) {
        frame_sync();
        merge_joystick();
        if (lat_esc) return 0;
        if (!lat_trig) break;
    }
    for (;;) {
        frame_sync();
        merge_joystick();
        if (lat_esc) return 0;
        if (lat_trig) return 1;
    }
}

/* ============================================================
 * Graphics helpers (SCREEN 5)
 * ============================================================ */
static void setup_palette(void)
{
    palett(0, 0, 0, 0);
    palett(1, 0, 0, 1);
    palett(2, 0, 1, 3);
    palett(3, 2, 0, 4);
    palett(4, 0, 2, 5);
    palett(5, 0, 4, 7);
    palett(6, 1, 5, 2);
    palett(7, 0, 6, 7);
    palett(8, 3, 6, 7);
    palett(9, 2, 7, 2);
    palett(10, 7, 6, 0);
    palett(11, 7, 3, 0);
    palett(12, 7, 0, 0);
    palett(13, 7, 2, 5);
    palett(14, 7, 7, 7);
    palett(15, 0, 6, 7);
}

static unsigned char pix[4 + 24 * 24];
static void draw_char5(int x, int y, unsigned char ch, int fg, int bg, int scale)
{
    const unsigned char *glyph;
    int w = 8 * scale;
    int r, c, i, j, k;
    k = 4;
    if (ch < 0x20 || ch > 0x7f) ch = 0x20;
    glyph = (const unsigned char *)font_buf + (ch - 0x20) * 8;
    pix[0] = (unsigned char)w;
    pix[1] = 0;
    pix[2] = (unsigned char)w;
    pix[3] = 0;
    for (r = 0; r < 8; r++) {
        unsigned char bits = glyph[r];
        for (i = 0; i < scale; i++) {
            for (c = 0; c < 8; c++) {
                unsigned char col = (bits & (0x80 >> c)) ? (unsigned char)fg
                                                         : (unsigned char)bg;
                for (j = 0; j < scale; j++)
                    pix[k++] = col;
            }
        }
    }
    cpym2v((char *)pix, 0, x, y, disp_page, 0);
}
static void draw_str(int x, int y, const char *s, int fg, int bg, int scale)
{
    while (*s) {
        draw_char5(x, y, (unsigned char)*s, fg, bg, scale);
        x += 8 * scale + (scale > 1 ? 2 : 0);
        s++;
    }
}
static void draw_str_centered(int y, const char *s, int fg, int bg, int scale)
{
    int n = 0;
    const char *p = s;
    while (*p) { n++; p++; }
    draw_str((256 - (n * (8 * scale + (scale > 1 ? 2 : 0)))) / 2, y, s, fg, bg, scale);
}

/* ---- baked title logo: PackBits RLE blit straight to VRAM
   (the STAR BREAKER asm blitter, with the page base added) ---- */
static unsigned char *tb_src;
static unsigned char *tb_ptr;
static unsigned int tb_addr;
static unsigned char tb_wb;
static unsigned char tb_rows;
static void blit_rle(void)
{
#asm
        ld      a,(_wrport)
        inc     a
        ld      c,a
        ld      a,2
        out     (c),a
        ld      a,8fh
        out     (c),a
_tb_wait:
        in      a,(c)
        and     1
        jr      nz,_tb_wait
        xor     a
        out     (c),a
        ld      a,8fh
        out     (c),a
        di
        ld      hl,(_tb_src)
        ld      (_tb_ptr),hl
_tb_row:
        ld      hl,(_tb_addr)
        ld      a,(_wrport)
        inc     a
        ld      c,a
        ld      a,h
        rlca
        rlca
        and     3
        out     (c),a
        ld      a,8eh
        out     (c),a
        ld      a,l
        out     (c),a
        ld      a,h
        and     3fh
        or      40h
        out     (c),a
        dec     c
        ld      hl,(_tb_ptr)
        ld      a,(_tb_wb)
        ld      e,a
_tb_tok:
        ld      a,e
        or      a
        jr      z,_tb_rowdone
        ld      a,(hl)
        inc     hl
        cp      80h
        jr      c,_tb_lit
        jr      z,_tb_tok
        neg
        inc     a
        ld      b,a
        ld      d,a
        ld      a,(hl)
        inc     hl
_tb_rep:
        out     (c),a
        djnz    _tb_rep
        ld      a,e
        sub     d
        ld      e,a
        jr      _tb_tok
_tb_lit:
        inc     a
        ld      b,a
        ld      d,a
_tb_litloop:
        outi
        jr      nz,_tb_litloop
        ld      a,e
        sub     d
        ld      e,a
        jr      _tb_tok
_tb_rowdone:
        ld      (_tb_ptr),hl
        ld      hl,(_tb_addr)
        ld      de,128
        add     hl,de
        ld      (_tb_addr),hl
        ld      a,(_tb_rows)
        dec     a
        ld      (_tb_rows),a
        jr      nz,_tb_row
        ei
        ret
#endasm
}
static void blit_img(const unsigned char *data, int x, int y, int wb, int h, int page)
{
    tb_src = (unsigned char *)data;
    tb_addr = (unsigned int)page * 32768u + (unsigned int)y * 128 + x / 2;
    tb_wb = (unsigned char)wb;
    tb_rows = (unsigned char)h;
    blit_rle();
}

/* ============================================================
 * Sprites: patterns 0..15 are authored below, 16..24 (boss tiles),
 * 25..41 (laser slopes), 42..58 (PRISM slopes) and 59..60 (item
 * letters, two dynamic slots) are generated at init.
 * Per-frame the used planes are packed in priority order and both
 * the attribute table (128 bytes) and the colour table are flushed
 * with single ldirvm calls, because plane assignment is dynamic.
 * ============================================================ */
#define PAT_SHIP    0
#define PAT_SHIP_A  1
#define PAT_OPTION  2
#define PAT_FAN     3
#define PAT_WEAVER  4
#define PAT_ORB     5
#define PAT_CARRIER 6
#define PAT_DART    7
#define PAT_CRYSTAL 8
#define PAT_CAPSULE 9
#define PAT_PBUL    10
#define PAT_EBUL    11
#define PAT_MISSILE 12
#define PAT_SPARK   13
#define PAT_SHARD   14
#define PAT_HALO    15
#define PAT_BOSS    16   /* ..24 (3x3 tiles) */
#define PAT_BEAM    25   /* ..41 slopes -8..+8 */
#define PAT_PRISM   42   /* ..58 slopes -8..+8 */
#define PAT_LETTER  59   /* ..60 dynamic */

static unsigned char pat_data[61][32];
static unsigned char pat_col[61][16];

static void pat_from_art(int idx, const char *art[], const unsigned char *cols)
{
    int r, c;
    for (r = 0; r < 16; r++) {
        unsigned char bl = 0, br = 0;
        for (c = 0; c < 8; c++) {
            if (art[r][c] == '#') bl |= 0x80 >> c;
            if (art[r][c + 8] == '#') br |= 0x80 >> c;
        }
        pat_data[idx][r] = bl;
        pat_data[idx][16 + r] = br;
        pat_col[idx][r] = cols[r];
    }
}
static void pat_fill_col(int idx, unsigned char col)
{
    int r;
    for (r = 0; r < 16; r++) pat_col[idx][r] = col;
}

static const char *art_ship[16] = {
    "................",
    ".....#..........",
    "....###.........",
    "....####........",
    "..##########....",
    ".############...",
    ".###############",
    "################",
    ".###############",
    ".############...",
    "..##########....",
    "....####........",
    "....###.........",
    ".....#..........",
    "................",
    "................"
};
static const unsigned char col_ship[16] = {
    0,8,8,7,7,7,7,14,7,7,7,7,8,8,0,0
};
static const char *art_ship_a[16] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    ".........###....",
    ".........####...",
    ".........###....",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................"
};
static const unsigned char col_ship_a[16] = {
    0,0,0,0,0,0,14,14,14,0,0,0,0,0,0,0
};
static const char *art_option[16] = {
    "................",
    "................",
    "................",
    "................",
    ".....######.....",
    "....########....",
    "...##########...",
    "...##########...",
    "...##########...",
    "...##########...",
    "....########....",
    ".....######.....",
    "................",
    "................",
    "................",
    "................"
};
static const unsigned char col_option[16] = {
    0,0,0,0,7,7,8,14,8,7,7,7,0,0,0,0
};
static const char *art_fan[16] = {
    "................",
    ".....#....#.....",
    "......#..#......",
    "...#..####..#...",
    "....#.####.#....",
    ".....######.....",
    "..############..",
    "..############..",
    "..############..",
    "..############..",
    ".....######.....",
    "....#.####.#....",
    "...#..####..#...",
    "......#..#......",
    ".....#....#.....",
    "................"
};
static const unsigned char col_fan[16] = {
    0,11,11,11,10,10,10,10,10,10,10,11,11,11,11,0
};
static const char *art_weaver[16] = {
    "................",
    "................",
    ".........##.....",
    "........####....",
    "..############..",
    ".##############.",
    "################",
    "################",
    "################",
    ".##############.",
    "..############..",
    "........####....",
    ".........##.....",
    "................",
    "................",
    "................"
};
static const unsigned char col_weaver[16] = {
    0,0,9,9,9,9,9,14,9,9,9,9,9,0,0,0
};
static const char *art_orb[16] = {
    "................",
    ".....######.....",
    "...##########...",
    "..############..",
    "..############..",
    ".##############.",
    ".##############.",
    ".##############.",
    ".##############.",
    ".##############.",
    ".##############.",
    "..############..",
    "..############..",
    "...##########...",
    ".....######.....",
    "................"
};
static const unsigned char col_orb[16] = {
    0,13,13,13,13,3,3,14,3,3,13,13,13,13,13,0
};
static const char *art_carrier[16] = {
    "................",
    "..############..",
    ".##############.",
    ".##############.",
    "################",
    "################",
    "################",
    "################",
    "################",
    "################",
    "################",
    "################",
    ".##############.",
    ".##############.",
    "..############..",
    "................"
};
static const unsigned char col_carrier[16] = {
    0,11,11,4,4,4,12,12,4,4,4,4,11,11,11,0
};
static const char *art_dart[16] = {
    "................",
    "................",
    "................",
    "..........######",
    ".......#########",
    "....############",
    ".###############",
    "################",
    "################",
    ".###############",
    "....############",
    ".......#########",
    "..........######",
    "................",
    "................",
    "................"
};
static const unsigned char col_dart[16] = {
    0,0,0,10,10,10,10,14,10,10,10,10,10,0,0,0
};
static const char *art_crystal[16] = {
    ".......##.......",
    "......####......",
    "......####......",
    ".....######.....",
    ".....######.....",
    "....########....",
    "....########....",
    "...##########...",
    "....########....",
    "....########....",
    ".....######.....",
    ".....######.....",
    "......####......",
    "......####......",
    ".......##.......",
    "................"
};
static const unsigned char col_crystal[16] = {
    8,8,7,7,13,13,14,7,7,13,13,8,8,7,7,0
};
static const char *art_capsule[16] = {
    "................",
    "...##########...",
    "..############..",
    "..##........##..",
    "..#..........#..",
    "..#..........#..",
    "..#..........#..",
    "..#..........#..",
    "..#..........#..",
    "..#..........#..",
    "..#..........#..",
    "..#..........#..",
    "..############..",
    "...##########...",
    "................",
    "................"
};
static const unsigned char col_capsule[16] = {
    0,14,14,14,14,14,14,14,14,14,14,14,14,14,0,0
};
static const char *art_pbul[16] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "..##########....",
    "..##########....",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................"
};
static const unsigned char col_pbul[16] = {
    0,0,0,0,0,0,0,10,10,0,0,0,0,0,0,0
};
static const char *art_ebul[16] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "......####......",
    ".....######.....",
    ".....######.....",
    ".....######.....",
    "......####......",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................"
};
static const unsigned char col_ebul[16] = {
    0,0,0,0,0,13,13,14,13,13,0,0,0,0,0,0
};
static const char *art_missile[16] = {
    "................",
    "................",
    "................",
    "................",
    "....##..........",
    "....###.........",
    "..###########...",
    "..#############.",
    "..#############.",
    "..###########...",
    "....###.........",
    "....##..........",
    "................",
    "................",
    "................",
    "................"
};
static const unsigned char col_missile[16] = {
    0,0,0,0,11,11,11,14,11,11,11,11,0,0,0,0
};
static const char *art_spark[16] = {
    ".......##.......",
    ".......##.......",
    "...#...##...#...",
    "....#..##..#....",
    ".....#####......",
    "################",
    "################",
    ".....#####......",
    "....#..##..#....",
    "...#...##...#...",
    ".......##.......",
    ".......##.......",
    "................",
    "................",
    "................",
    "................"
};
static const unsigned char col_spark[16] = {
    10,10,10,10,11,11,14,11,10,10,10,10,0,0,0,0
};
static const char *art_shard[16] = {
    "................",
    ".......##.......",
    "......####......",
    "......####......",
    ".....######.....",
    ".....######.....",
    "....########....",
    "....########....",
    "....########....",
    ".....######.....",
    ".....######.....",
    "......####......",
    "......####......",
    ".......##.......",
    "................",
    "................"
};
static const unsigned char col_shard[16] = {
    0,8,7,7,13,13,14,7,13,13,8,8,7,7,0,0
};
static const char *art_halo[16] = {
    ".....######.....",
    "...##########...",
    "..###......###..",
    "..##........##..",
    ".##..........##.",
    ".#............#.",
    ".#............#.",
    ".#............#.",
    ".#............#.",
    ".#............#.",
    ".##..........##.",
    "..##........##..",
    "..###......###..",
    "...##########...",
    ".....######.....",
    "................"
};
static const unsigned char col_halo[16] = {
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,0
};

/* boss body pixel function over 48x48 (ported idx_boss_t, simplified) */
static int boss_pix(int x, int y)
{
    int dx = x - 24, dy = y - 24;
    int ad = iabs(dx) + iabs(dy);
    int d2 = dx * dx + dy * dy;
    if (ad > 23) return 0;
    if (d2 <= 25) return 1;              /* core */
    if (ad >= 21) return 1;              /* rim */
    if (dx == 0 || dy == 0 || iabs(dx) == iabs(dy)) return 1;
    return ((x + y) & 2) ? 1 : 0;        /* facets */
}
static void gen_boss_patterns(void)
{
    int t, r, c;
    for (t = 0; t < 9; t++) {
        int tx = (t % 3) * 16, ty = (t / 3) * 16;
        for (r = 0; r < 16; r++) {
            unsigned char bl = 0, br = 0;
            int gy = ty + r;
            for (c = 0; c < 8; c++) {
                if (boss_pix(tx + c, gy)) bl |= 0x80 >> c;
                if (boss_pix(tx + 8 + c, gy)) br |= 0x80 >> c;
            }
            pat_data[PAT_BOSS + t][r] = bl;
            pat_data[PAT_BOSS + t][16 + r] = br;
            if (gy >= 19 && gy <= 29 && tx == 16)
                pat_col[PAT_BOSS + t][r] = C_PINK;
            else
                pat_col[PAT_BOSS + t][r] = ((t + r) & 1) ? C_CYAN : C_ICE;
        }
    }
}
/* laser segment patterns: 3-dot (beam) / 5-dot (prism) thick line
   from (0, 8 - s/2) to (15, 8 + s - s/2) for slope s in -8..8 */
static void gen_beam_patterns(void)
{
    int s, r, c, prism;
    for (prism = 0; prism < 2; prism++) {
        int thick = prism ? 2 : 1;
        for (s = -8; s <= 8; s++) {
            int idx = (prism ? PAT_PRISM : PAT_BEAM) + s + 8;
            for (r = 0; r < 16; r++) {
                unsigned char bl = 0, br = 0;
                for (c = 0; c < 16; c++) {
                    int cy = 8 + (s * c) / 16;
                    int on = iabs(r - cy) <= thick;
                    if (on) {
                        if (c < 8) bl |= 0x80 >> c;
                        else br |= 0x80 >> (c - 8);
                    }
                }
                pat_data[idx][r] = bl;
                pat_data[idx][16 + r] = br;
                pat_col[idx][r] = prism ? C_PINK : C_CYAN;
            }
            /* bright core rows near the axis ends */
            pat_col[idx][8 - s / 2] = C_WHITE;
        }
    }
}
/* item letter overlay: build a pattern from the 8x8 font glyph,
   a ring, or a star; written into dynamic slot 59/60 */
static void gen_letter_pattern(int slot, int type)
{
    int r, c;
    for (r = 0; r < 32; r++) pat_data[slot][r] = 0;
    if (type < 6) {
        static const char letters[6] = { 'T', 'C', 'D', 'P', 'E', 'H' };
        const unsigned char *g = font_buf + (letters[type] - 0x20) * 8;
        for (r = 0; r < 8; r++) {
            unsigned char bits = g[r];
            /* glyph goes to rows 4..11, cols 4..11: the left byte
               holds cols 0..7 (glyph cols 0..3 in its low half) */
            pat_data[slot][4 + r] = (bits >> 4) & 0x0f;
            pat_data[slot][16 + 4 + r] = (bits << 4) & 0xf0;
        }
        pat_fill_col(slot, C_YELLOW);
    } else {
        /* bonus star */
        for (r = 0; r < 16; r++) {
            unsigned char bl = 0, br = 0;
            for (c = 0; c < 16; c++) {
                int dx = c - 8, dy = r - 8;
                int on = (dx == 0 && iabs(dy) <= 5) ||
                         (dy == 0 && iabs(dx) <= 5) ||
                         (iabs(dx) == iabs(dy) && iabs(dx) <= 3);
                if (on) {
                    if (c < 8) bl |= 0x80 >> c;
                    else br |= 0x80 >> (c - 8);
                }
            }
            pat_data[slot][r] = bl;
            pat_data[slot][16 + r] = br;
        }
        pat_fill_col(slot, C_YELLOW);
    }
    sprite(slot * 4, (char *)pat_data[slot]);
}

static void setup_sprites(void)
{
    int i;
    pat_from_art(PAT_SHIP, art_ship, col_ship);
    pat_from_art(PAT_SHIP_A, art_ship_a, col_ship_a);
    pat_from_art(PAT_OPTION, art_option, col_option);
    pat_from_art(PAT_FAN, art_fan, col_fan);
    pat_from_art(PAT_WEAVER, art_weaver, col_weaver);
    pat_from_art(PAT_ORB, art_orb, col_orb);
    pat_from_art(PAT_CARRIER, art_carrier, col_carrier);
    pat_from_art(PAT_DART, art_dart, col_dart);
    pat_from_art(PAT_CRYSTAL, art_crystal, col_crystal);
    pat_from_art(PAT_CAPSULE, art_capsule, col_capsule);
    pat_from_art(PAT_PBUL, art_pbul, col_pbul);
    pat_from_art(PAT_EBUL, art_ebul, col_ebul);
    pat_from_art(PAT_MISSILE, art_missile, col_missile);
    pat_from_art(PAT_SPARK, art_spark, col_spark);
    pat_from_art(PAT_SHARD, art_shard, col_shard);
    pat_from_art(PAT_HALO, art_halo, col_halo);
    gen_boss_patterns();
    gen_beam_patterns();
    inispr(2);
    for (i = 0; i < 61; i++)
        sprite(i * 4, (char *)pat_data[i]);
}

/* ---- dynamic sprite assembly ---- */
/* The logical sprite list is larger than the VDP's 32
   planes: spr_flush registers a rotating window of the list
   each frame, so in the busiest scenes every sprite is on
   screen in turn instead of the tail of the list being
   dropped outright. 80 covers every real scene (the absolute
   worst case of all caps at once is ~84 and never occurs:
   the spawner is off during the boss, etc.). */
#define MAX_SPR 80
static unsigned char spr_attr[MAX_SPR * 4];
static unsigned char spr_col[MAX_SPR * 16];
static int spr_n;
static int spr_fixed;   /* leading planes that never rotate */
static void spr_reset(void) { spr_n = 0; spr_fixed = 0; }
static void spr_add(int x, int ytop, int pat)
{
    int i;
    if (spr_n >= MAX_SPR) return;
    if (x <= -16 || x >= 256 || ytop <= -16 || ytop >= 212) return;
    i = spr_n++;
    spr_attr[i * 4 + 0] = (unsigned char)(ytop - 1);
    if (x < 0) {
        spr_attr[i * 4 + 1] = (unsigned char)(x + 32);
        spr_attr[i * 4 + 3] = 0x80;    /* early clock */
    } else {
        spr_attr[i * 4 + 1] = (unsigned char)x;
        spr_attr[i * 4 + 3] = 0;
    }
    spr_attr[i * 4 + 2] = (unsigned char)(pat * 4);
    memcpy(&spr_col[i * 16], pat_col[pat], 16);
}
/* VRAM block write for the sprite tables. Interrupts are off
   ONLY around the address setup: once the address is latched,
   the byte stream can be interrupted safely - the split
   handler's control-port pairs are complete pairs (the byte
   toggle ends where it started) and it never touches the data
   port, R#14 or the address latch, so the stream resumes at
   the right address. (BIOS LDIRVM cannot be used here: its own
   address setup is not protected, and wrapping the whole call
   in DI would delay a line IRQ by up to ~2.6 ms - about 40
   display lines - visibly tearing the split.) */
static unsigned int vw_addr, vw_len;
static unsigned char *vw_src;
static void vram_write(void)
{
#asm
        ld      a,(_wrport)
        inc     a
        ld      c,a                 ; control port
        ld      b,0
        di
        ld      a,(_vw_addr+1)
        rlca
        rlca
        and     3                   ; address bits 15..14
        out     (c),a               ; R#14 = address high bits
        ld      a,8eh
        out     (c),a
        ld      a,(_vw_addr)
        out     (c),a               ; address low byte
        ld      a,(_vw_addr+1)
        and     3fh
        or      40h                 ; write setup
        out     (c),a
        ei
        ld      hl,(_vw_src)
        ld      de,(_vw_len)
        ld      a,(_wrport)
        ld      c,a                 ; C = VRAM data port
_vw_loop:
        ld      a,e
        or      d
        jr      z,_vw_done
        ld      b,e
        ld      a,d
        or      a
        jr      z,_vw_otir
        ld      b,0                 ; a full 256-byte chunk
_vw_otir:
        otir
        ld      a,d
        or      a
        jr      z,_vw_zero
        dec     d
        jr      _vw_loop
_vw_zero:
        ld      de,0
        jr      _vw_loop
_vw_done:
#endasm
}

/* ---- title logo: 1bpp outline mask + code-drawn beam ----
   The full-colour logo bake (5,117 B) pushed BSS past the DOS
   memory ceiling (~0xC838; the program silently returned to DOS
   at startup - v1.8 size bisect). The logo is outline-only by
   design, so it ships as a PackBits 1bpp mask (1,989 B) expanded
   to nibbles here (7 on the outline, 1 elsewhere), and the beam
   (two solid rows: white core over pink) is filled directly. */
static unsigned char mask_bits[30];
static unsigned char mask_row[120];
static void blit_logo_mask(int page)
{
    const unsigned char *p = tlogo_mask;
    unsigned int addr = (unsigned int)page * 32768u
                      + (unsigned int)TLOGO_Y * 128 + TLOGO_X / 2;
    int r, i, k, n;
    unsigned char ctrl, b;
    for (r = 0; r < TLOGO_H; r++) {
        i = 0;
        while (i < 30) {
            ctrl = *p++;
            if (ctrl == 0x80) continue;
            if (ctrl > 0x80) {
                n = 257 - ctrl; b = *p++;
                while (n-- > 0) mask_bits[i++] = b;
            } else {
                n = ctrl + 1;
                while (n-- > 0) mask_bits[i++] = *p++;
            }
        }
        for (i = 0; i < 30; i++) {
            b = mask_bits[i];
            for (k = 0; k < 4; k++) {
                mask_row[i * 4 + k] = (unsigned char)
                    (((b & 0x80) ? 0x70 : 0x10) | ((b & 0x40) ? 0x07 : 0x01));
                b = (unsigned char)(b << 2);
            }
        }
        vw_addr = addr; vw_src = mask_row; vw_len = TLOGO_WB;
        vram_write();
        addr += 128;
    }
    for (i = 0; i < TLOGO_WB; i++) mask_row[i] = 0xEE;
    vw_addr = (unsigned int)page * 32768u
            + (unsigned int)(TLOGO_Y + TLOGO_BEAM_Y) * 128 + TLOGO_X / 2;
    vw_src = mask_row; vw_len = TLOGO_WB;
    vram_write();
    for (i = 0; i < TLOGO_WB; i++) mask_row[i] = 0xDD;
    vw_addr += 128;
    vram_write();
}

/* Sprite priority management.
   The VDP has 32 planes and shows at most 8 sprites per
   scanline. Two mechanisms share the work:
   - The FIXED zone (the first spr_fixed entries: the
     player group only) is registered on the front planes
     every frame, so the ship is rock solid.
   - The ROTATING zone (everything else) may be longer than
     the planes left for it (32 - spr_fixed). Instead of
     dropping the overflow outright (the old behaviour: in
     the boss fight the laser and the missiles simply never
     got a plane), spr_flush registers a WINDOW of the zone
     and advances the window by its own size each frame, so
     every entry is registered in turn - everything is drawn,
     everything flickers. Inside the window the plane order
     also rotates by 8 per frame, so the per-scanline drop
     victims cycle too. Scenes that fit in 32 planes behave
     exactly like plain rotation (window = whole zone). */
static unsigned char spr_rot;
static unsigned int spr_win;
static void spr_write_seg(int src, int dst, int count)
{
    if (count <= 0) return;
    vw_addr = ATRBAS - 512 + (unsigned int)dst * 16;
    vw_src = spr_col + src * 16;
    vw_len = (unsigned int)count * 16;
    vram_write();
    vw_addr = ATRBAS + (unsigned int)dst * 4;
    vw_src = spr_attr + src * 4;
    vw_len = (unsigned int)count * 4;
    vram_write();
}
static void spr_flush(void)
{
    static unsigned char parkbuf[32 * 4];
    static int park_init = 0;
    int i;
    int F = spr_fixed;
    int rn = spr_n - F;              /* rotating-zone count */
    int W, off = 0, r = 0;
    if (!park_init) {
        for (i = 0; i < 32; i++) {
            parkbuf[i * 4 + 0] = 216;
            parkbuf[i * 4 + 1] = 0;
            parkbuf[i * 4 + 2] = 0;
            parkbuf[i * 4 + 3] = 0;
        }
        park_init = 1;
    }
    if (spr_n == 0) {
        vw_addr = ATRBAS;
        vw_src = parkbuf;
        vw_len = 32 * 4;
        vram_write();
        return;
    }
    W = rn < (32 - F) ? rn : (32 - F);
    if (rn > W) {
        off = (int)(spr_win % (unsigned int)rn);
        spr_win += (unsigned int)W;
    } else {
        spr_win = 0;
    }
    if (W > 0) {
        r = spr_rot % W;
        spr_rot = (unsigned char)(spr_rot + 8);
    }
    spr_write_seg(0, 0, F);
    /* The window slice of the rotating zone, internally
       rotated by r: display order is j = r..W-1 then 0..r-1,
       and the logical index F + off + j wraps once at the
       zone end, so the whole mapping is at most three
       contiguous source runs - generated with adds and
       compares only (a modulo per entry cost ~2 fps). */
    {
        int pass;
        for (pass = 0; pass < 2; pass++) {
            int j0 = pass == 0 ? r : 0;
            int cnt = pass == 0 ? (W - r) : r;
            int dst = F + (pass == 0 ? 0 : (W - r));
            int src;
            if (cnt <= 0) continue;
            src = F + off + j0;
            if (src >= F + rn) src -= rn;
            if (src + cnt > F + rn) {
                int first = F + rn - src;
                spr_write_seg(src, dst, first);
                spr_write_seg(F, dst + first, cnt - first);
            } else {
                spr_write_seg(src, dst, cnt);
            }
        }
    }
    if (F + W < 32) {
        vw_addr = ATRBAS + (unsigned int)(F + W) * 4;
        vw_src = parkbuf;
        vw_len = (unsigned int)(32 - F - W) * 4;
        vram_write();
    }
}
static void hide_sprites(void)
{
    spr_reset();
    spr_flush();
}

/* ============================================================
 * Game state
 * ============================================================ */
enum { ST_TITLE, ST_PLAY, ST_OVER, ST_CLEAR, ST_EXIT, ST_DEMO };
enum { PH_NORMAL, PH_WARN, PH_BOSS, PH_DYING };

static int demo_mode;
static int phase;
static int warn_t, dying_t;
static long score, hi_score;
static int lives;
static int px, py;                 /* player centre */
static int speed_level, has_missile, has_double, has_laser;
static int options_n, shield_hp;
static int resonance, prism_t;
static int laser_len;
static int shot_cd, missile_cd, option_cd;
static int invincible_t, player_dead, respawn_t;
static int item_rot;
static int boss_x, boss_y, boss_t, boss_core_hp, boss_flash;
static int shard_hp[4], shard_x[4], shard_y[4], shard_flash[4];
static int boss_atk1, boss_atk2, boss_atk3;
static int spawn_idx;
static long next_spawn_dist;

static unsigned char hist_x[256], hist_y[256];

typedef struct { unsigned char active, type, drop, flash; short x, y, base_y; unsigned int t; unsigned char phase; short hp; short vy; } Enemy;
static Enemy enemies[12];
typedef struct { unsigned char active, kind; short x, y, vx, vy; unsigned char t; } PBul;
static PBul pbul[16];
typedef struct { unsigned char active; int x, y, vx, vy; } EBul;  /* 8.8 fixed */
static EBul ebul[24];
typedef struct { unsigned char active, type; short x, y, oy; unsigned char t; } Item;
static Item items[4];
typedef struct { unsigned char active; short x, y; unsigned char t; } Spark;
static Spark sparks[6];

static const short enemy_score[6] = { 100, 200, 300, 500, 100, 1000 };
static const unsigned char enemy_pat[6] = {
    PAT_FAN, PAT_WEAVER, PAT_ORB, PAT_CARRIER, PAT_DART, PAT_CRYSTAL
};
static int enemy_radius(int type)
{
    static const unsigned char r[6] = { 8, 8, 10, 12, 6, 14 };
    return r[type];
}
static void add_score(int n)
{
    score += n;
    if (score > hi_score) hi_score = score;
}

/* ---- HUD ---- */
static long hud_score_shown;
static int hud_lives_shown, hud_res_shown, hud_res_col, band_dirty;
static void draw_hud_static(void)
{
    ACPAGE = (unsigned char)disp_page;
    boxfil(0, 0, 255, HUD_H - 1, C_BLACK, 0);
    draw_str(12, 4, "SCORE", C_ICE, C_BLACK, 1);   /* x>=8: clear of the MSK mask */
    draw_str(112, 4, "RESONANCE", C_ICE, C_BLACK, 1);
    /* resonance bar outline x 188..238, lives digit at x 246 */
    boxfil(188, 4, 238, 4, C_CYAN, 0);
    boxfil(188, 11, 238, 11, C_CYAN, 0);
    boxfil(188, 4, 188, 11, C_CYAN, 0);
    boxfil(238, 4, 238, 11, C_CYAN, 0);
    draw_str(12 + 48, 4, "000000", C_WHITE, C_BLACK, 1);
    hud_score_shown = -1;
    hud_res_shown = -1;
    hud_res_col = -1;
    hud_lives_shown = -1;
}
static void draw_hud_values(void)
{
    static const long div10[6] = { 100000, 10000, 1000, 100, 10, 1 };
    int i;
    if (score != hud_score_shown) {
        for (i = 0; i < 6; i++) {
            int d = (int)((score / div10[i]) % 10);
            if (hud_score_shown < 0 ||
                (int)((hud_score_shown / div10[i]) % 10) != d)
                draw_char5(52 + i * 8, 4, (unsigned char)('0' + d),
                           C_WHITE, C_BLACK, 1);
        }
        hud_score_shown = score;
    }
    {
        int w = prism_t > 0 ? 46 : resonance * 46 / 1000;
        int col = prism_t > 0 ? C_PINK : C_CYAN;
        if (w != hud_res_shown || col != hud_res_col) {
            /* Stream the fill with the CPU (vram_write), not the
               VDP's LMMV boxfil: command fills issued while the
               line-split handler is armed can be disturbed and
               paint over the RESONANCE label (measured: its C/E
               glyphs lost their middle rows exactly while the
               gauge was non-empty). The streamed path latches its
               address under DI and is immune. 46 px = 23 packed
               bytes on each of the 4 fill rows, always inside
               the outline (x 190..235, y 6..9). */
            static unsigned char grow[23];
            int i, y;
            for (i = 0; i < 23; i++) {
                int p0 = i * 2, p1 = i * 2 + 1;
                grow[i] = (unsigned char)(((p0 < w) ? (col << 4) : 0) |
                                          ((p1 < w) ? col : 0));
            }
            for (y = 6; y <= 9; y++) {
                vw_addr = (unsigned int)disp_page * 0x8000u +
                          (unsigned int)y * 128 + 95;
                vw_src = grow;
                vw_len = 23;
                vram_write();
            }
            hud_res_shown = w;
            hud_res_col = col;
        }
    }
    if (lives != hud_lives_shown) {
        draw_char5(246, 4, (unsigned char)('0' + (lives < 0 ? 0 : lives)),
                   C_YELLOW, C_BLACK, 1);
        hud_lives_shown = lives;
    }
}

/* ---- equipment band ---- */
static const char *slot_names[6] = {
    "T THRUST", "C COMET", "D DUAL", "P PRISM", "E ECHO", "H HALO"
};
static int slot_owned(int i)
{
    switch (i) {
        case 0: return speed_level > 0;
        case 1: return has_missile;
        case 2: return has_double;
        case 3: return has_laser;
        case 4: return options_n > 0;
        case 5: return shield_hp > 0;
    }
    return 0;
}
static void draw_band_static(void)
{
    ACPAGE = (unsigned char)disp_page;
    boxfil(0, BAND_Y, 255, 211, C_BLACK, 0);
    boxfil(0, BAND_Y, 255, BAND_Y, C_BG2, 0);
    band_dirty = 0x3f;
}
static void draw_band_one(void)
{
    int i;
    for (i = 0; i < 6; i++) {
        if (band_dirty & (1 << i)) {
            int x = 12 + (i % 3) * 84;   /* clear of the MSK mask */
            int y = BAND_Y + 4 + (i / 3) * 12;
            char buf[16];
            const char *n = slot_names[i];
            int k = 0;
            band_dirty &= ~(1 << i);
            ACPAGE = (unsigned char)disp_page;
            boxfil(x, y, x + 63, y + 7, C_BLACK, 0);
            while (n[k]) { buf[k] = n[k]; k++; }
            if (i == 0 && speed_level > 0) {
                buf[k++] = ' ';
                buf[k++] = (char)('0' + speed_level);
            }
            if (i == 4 && options_n > 0) {
                buf[k++] = ' ';
                buf[k++] = (char)('0' + options_n);
            }
            if (i == 5 && shield_hp > 0) {
                buf[k++] = ' ';
                buf[k++] = (char)('0' + shield_hp);
            }
            buf[k] = 0;
            draw_str(x, y, buf, slot_owned(i) ? C_WHITE : C_BG2,
                     C_BLACK, 1);
            return;      /* one slot per frame at most */
        }
    }
}

/* ============================================================
 * Entities
 * ============================================================ */
static void spawn_eb(int x, int y, int vx, int vy)
{
    int i;
    for (i = 0; i < 24; i++) if (!ebul[i].active) {
        ebul[i].active = 1;
        ebul[i].x = x << 8; ebul[i].y = y << 8;
        ebul[i].vx = vx; ebul[i].vy = vy;
        return;
    }
}
static void spawn_eb_aimed(int x, int y, int speed_fp)
{
    int dx = px - x, dy = py - y;
    int ax = iabs(dx), ay = iabs(dy);
    int dist = ax > ay ? ax : ay;
    int vx, vy, it;
    long v = (long)dx * dx + (long)dy * dy;
    if (dist < 1) dist = 1;
    for (it = 0; it < 6; it++) dist = (dist + (int)(v / dist)) / 2;
    vx = (int)((long)dx * speed_fp / dist);
    vy = (int)((long)dy * speed_fp / dist);
    spawn_eb(x, y, vx, vy);
}
static void spawn_spark(int x, int y)
{
    int i;
    for (i = 0; i < 6; i++) if (!sparks[i].active) {
        sparks[i].active = 1; sparks[i].x = x; sparks[i].y = y; sparks[i].t = 0;
        return;
    }
}
static int choose_item_type(void)
{
    int k;
    for (k = 0; k < 6; k++) {
        int t = (item_rot + k) % 6;
        int sat = (t == 0 && speed_level >= 5) ||
                  (t == 1 && has_missile) ||
                  (t == 2 && has_double) ||
                  (t == 3 && has_laser) ||
                  (t == 4 && options_n >= 2) ||
                  (t == 5 && shield_hp >= 4);
        if (!sat) { item_rot = (t + 1) % 6; return t; }
    }
    return 6;
}
static void drop_item(int x, int y)
{
    int i;
    for (i = 0; i < 4; i++) if (!items[i].active) {
        items[i].active = 1;
        items[i].x = x; items[i].y = y; items[i].oy = y; items[i].t = 0;
        items[i].type = (unsigned char)choose_item_type();
        gen_letter_pattern(PAT_LETTER + (i & 1), items[i].type);
        return;
    }
}
static void spawn_enemy(int type, int x, int y, int drop, int phase_)
{
    int i;
    for (i = 0; i < 12; i++) if (!enemies[i].active) {
        Enemy *e = &enemies[i];
        e->active = 1; e->type = (unsigned char)type; e->drop = (unsigned char)drop;
        e->x = x; e->y = y; e->base_y = y; e->t = 0;
        e->phase = (unsigned char)phase_; e->flash = 0;
        switch (type) {
            case 0: e->hp = 1; break;
            case 1: e->hp = 2; break;
            case 2: e->hp = 6; break;
            case 3: e->hp = 14; break;
            case 4: e->hp = 1; e->vy = (y < py) ? 1 : -1; break;
            case 5: e->hp = 36; break;
        }
        return;
    }
}
static void spawner(void)
{
    int pat, base;
    if (scroll_x < next_spawn_dist) return;
    pat = spawn_idx % 6; spawn_idx++;
    base = 28 + (int)rnd(130);
    switch (pat) {
        case 0: { int i; for (i = 0; i < 5; i++) spawn_enemy(0, 264 + i * 20, base + i * 5, i == 4, i * 5); next_spawn_dist += 190; break; }
        case 1: { int i; for (i = 0; i < 4; i++) spawn_enemy(1, 264 + i * 28, base, 0, i * 9); next_spawn_dist += 200; break; }
        case 2: { int i; for (i = 0; i < 3; i++) spawn_enemy(4, 264 + i * 24, (i % 2) ? 40 : 160, i == 2, 0); next_spawn_dist += 170; break; }
        case 3: spawn_enemy(2, 264, 48, 0, 0); spawn_enemy(2, 288, 152, 1, 20); next_spawn_dist += 210; break;
        case 4: spawn_enemy(3, 268, base + 24, 1, 0); spawn_enemy(0, 296, base, 0, 0); spawn_enemy(0, 296, base + 56, 0, 10); next_spawn_dist += 230; break;
        case 5:
            if (scroll_x > 800) { spawn_enemy(5, 268, 104, 1, 0); next_spawn_dist += 270; }
            else { int i; for (i = 0; i < 5; i++) spawn_enemy(0, 264 + i * 20, base + i * 5, i == 4, i * 5); next_spawn_dist += 190; }
            break;
    }
}
static void spawn_pb(int x, int y, int kind)
{
    int i;
    for (i = 0; i < 16; i++) if (!pbul[i].active) {
        pbul[i].active = 1; pbul[i].kind = (unsigned char)kind;
        pbul[i].x = x; pbul[i].y = y; pbul[i].t = 0;
        if (kind == 0) { pbul[i].vx = 6; pbul[i].vy = 0; }
        else if (kind == 1) { pbul[i].vx = 5; pbul[i].vy = -4; }
        else { pbul[i].vx = 4; pbul[i].vy = 2; }
        return;
    }
}

static void apply_item(int t)
{
    switch (t) {
        case 0: if (speed_level < 5) speed_level++; break;
        case 1: has_missile = 1; break;
        case 2: has_double = 1; has_laser = 0; laser_len = 0; break;
        case 3: has_laser = 1; has_double = 0; break;
        case 4: if (options_n < 2) options_n++; break;
        case 5: shield_hp = 4; break;
        case 6: add_score(950); break;
    }
    if (t != 6) band_dirty = 0x3f;
    SE_POWER;
    add_score(50);
}
static void reset_powers(void)
{
    speed_level = 0; has_missile = 0; has_double = 0; has_laser = 0;
    options_n = 0; shield_hp = 0; laser_len = 0; resonance = 0; prism_t = 0;
    band_dirty = 0x3f;
}

static int boss_shielded(void)
{
    int i;
    for (i = 0; i < 4; i++) if (shard_hp[i] > 0) return 1;
    return 0;
}
static void start_boss(void)
{
    int i;
    phase = PH_BOSS;
    boss_x = 300; boss_y = 104; boss_t = 0;
    boss_core_hp = 140; boss_flash = 0;
    for (i = 0; i < 4; i++) { shard_hp[i] = 22; shard_flash[i] = 0; }
    boss_atk1 = 58; boss_atk2 = 230; boss_atk3 = 140;
    bgm_restart(2);
}

/* damage to a point: returns 1 when the point is consumed */
static int damage_boss_point(int bx, int by, int dmg)
{
    int i;
    if (phase != PH_BOSS) return 0;
    for (i = 0; i < 4; i++) {
        if (shard_hp[i] > 0) {
            int dx = bx - shard_x[i], dy = by - shard_y[i];
            if (dx * dx + dy * dy < 12 * 12) {
                shard_hp[i] -= dmg; shard_flash[i] = 3;
                if (shard_hp[i] <= 0) {
                    spawn_spark(shard_x[i], shard_y[i]);
                    add_score(500);
                    SE_EXPLO;
                } else SE_HIT;
                return 1;
            }
        }
    }
    {
        int dx = bx - boss_x, dy = by - boss_y;
        if (iabs(dx) + iabs(dy) < 26) {
            if (boss_shielded()) {
                spawn_spark(bx, by);
                return 1;
            }
            boss_core_hp -= dmg; boss_flash = 3;
            if (boss_core_hp <= 0) {
                phase = PH_DYING; dying_t = 215;
                add_score(10000);
                SE_EXPLO;
            } else SE_HIT;
            return 1;
        }
    }
    return 0;
}
static void damage_enemy(Enemy *e, int dmg)
{
    e->hp -= dmg;
    e->flash = 3;
    if (e->hp <= 0) {
        e->active = 0;
        spawn_spark(e->x, e->y);
        add_score(enemy_score[e->type]);
        if (e->type == 5) {
            resonance += 220;
            if (resonance > 1000) resonance = 1000;
        }
        if (e->drop) drop_item(e->x, e->y);
        SE_EXPLO;
    } else {
        SE_HIT;
    }
}
static void hit_player(void)
{
    if (demo_mode || player_dead || invincible_t > 0 || phase == PH_DYING) return;
    if (shield_hp > 0) {
        shield_hp--;
        invincible_t = 60;
        band_dirty |= 1 << 5;
        SE_HIT;
        return;
    }
    player_dead = 1;
    respawn_t = 80;
    laser_len = 0;
    spawn_spark(px, py);
    SE_MISS;
}

/* laser Y at a screen X: from the flight history (5 frames of
   delay per 16-dot segment), the X68000 original's method */
static int laser_y_at(int lx)
{
    int d = lx - (px + 14);
    int seg = d <= 0 ? 0 : (d >> 4);   /* == clamp((lx-px-14)/16) */
    int idx;
    idx = (int)((frame_count - seg * 5) & 255);
    return hist_y[idx];
}

/* ============================================================
 * Updates
 * ============================================================ */
/* Laser cadence gates: free-running replacements for the old
   (frame_count % 5) / (frame_count % 15) tests - frame_count is
   a long, so those were long-modulo library calls every frame
   the laser was out. Same cadence, phase offset only. */
static unsigned char gate5, gate15;
static void update_player(int trig)
{
    int speed = 2 + (speed_level + 1) / 2;
    int dx = 0, dy = 0;
    if (demo_mode) {
        /* attract AI: chase the nearest item, else the nearest
           enemy, else hold the centre; always firing */
        int tx = 48, ty = 104, i, best = 30000;
        for (i = 0; i < 4; i++) if (items[i].active) {
            int d = iabs(items[i].x - px) + iabs(items[i].y - py);
            if (d < best) { best = d; tx = items[i].x; ty = items[i].y; }
        }
        if (best == 30000) {
            for (i = 0; i < 12; i++) if (enemies[i].active) {
                int d = iabs(enemies[i].x - px) + iabs(enemies[i].y - py);
                if (d < best) { best = d; tx = 60; ty = enemies[i].y; }
            }
        }
        if (px < tx - 4) dx = 1; else if (px > tx + 4) dx = -1;
        if (py < ty - 3) dy = 1; else if (py > ty + 3) dy = -1;
        trig = 1;
    } else {
        if (lat_left) dx = -1;
        if (lat_right) dx = 1;
        if (lat_up) dy = -1;
        if (lat_down) dy = 1;
    }
    px += dx * speed;
    py += dy * speed;
    if (px < 12) px = 12;
    if (px > 238) px = 238;
    if (py < PLAY_Y0 + 8) py = PLAY_Y0 + 8;
    if (py > PLAY_Y1 - 8) py = PLAY_Y1 - 8;

    if (shot_cd > 0) shot_cd--;
    if (missile_cd > 0) missile_cd--;
    if (option_cd > 0) option_cd--;

    /* ---- PRISM laser ---- */
    if (has_laser && trig) {
        int reach, i, curv = 0, prev_y = py;
        if (laser_len < 232) laser_len += 16;
        reach = px + 14 + laser_len;
        if (reach > 255) reach = 255;
        for (i = 0; i < 14; i++) {
            int lx = px + 14 + i * 16;
            int ly;
            if (lx > reach) break;
            ly = hist_y[(int)((frame_count - i * 5) & 255)];
            curv += iabs(ly - prev_y);
            prev_y = ly;
        }
        if (prism_t > 0) {
            /* already bursting */
        } else {
            resonance += curv / 5;
            if (resonance >= 1000) {
                resonance = 0;
                prism_t = 600;
                SE_POWER;
            }
        }
        /* damage along the beam, thinned to every 5th frame */
        if (++gate5 >= 5) {
            gate5 = 0;
            int dmg = prism_t > 0 ? 2 : 1;
            for (i = 0; i < 12; i++) if (enemies[i].active) {
                Enemy *e = &enemies[i];
                if (e->x >= px + 10 && e->x <= reach &&
                    iabs(e->y - laser_y_at(e->x)) < 12)
                    damage_enemy(e, dmg);
            }
            if (phase == PH_BOSS) {
                for (i = 0; i < 4; i++)
                    if (shard_hp[i] > 0 &&
                        shard_x[i] >= px + 10 && shard_x[i] <= reach &&
                        iabs(shard_y[i] - laser_y_at(shard_x[i])) < 12) {
                        damage_boss_point(shard_x[i], shard_y[i], dmg);
                        break;
                    }
                if (boss_x >= px + 10 && boss_x <= reach &&
                    iabs(boss_y - laser_y_at(boss_x)) < 20)
                    damage_boss_point(boss_x, boss_y, dmg);
            }
        }
        if (++gate15 >= 15) { gate15 = 0; SE_LASER; }
        /* options and missiles stay useful with the laser */
        if (options_n > 0 && option_cd == 0) {
            int oi;
            for (oi = 0; oi < options_n; oi++) {
                int idx = (int)((frame_count - (oi + 1) * 24) & 255);
                spawn_pb(hist_x[idx] + 8, hist_y[idx], 0);
            }
            option_cd = 9;
        }
        if (has_missile && missile_cd == 0) {
            spawn_pb(px + 4, py + 6, 2);
            missile_cd = 26;
        }
    } else {
        laser_len = 0;
        if (resonance > 0 && prism_t == 0) resonance--;
        if (trig && shot_cd == 0) {
            spawn_pb(px + 10, py, 0);
            if (has_double) spawn_pb(px + 6, py - 6, 1);
            shot_cd = 6;
            SE_SHOT;
        }
        if (options_n > 0 && trig && option_cd == 0) {
            int oi;
            for (oi = 0; oi < options_n; oi++) {
                int idx = (int)((frame_count - (oi + 1) * 24) & 255);
                spawn_pb(hist_x[idx] + 8, hist_y[idx], 0);
            }
            option_cd = 9;
        }
        if (has_missile && trig && missile_cd == 0) {
            spawn_pb(px + 4, py + 6, 2);
            missile_cd = 22;
        }
    }
    if (prism_t > 0) prism_t--;
}

static void update_enemies(void)
{
    int i;
    for (i = 0; i < 12; i++) {
        Enemy *e = &enemies[i];
        if (!e->active) continue;
        e->t++;
        if (e->flash > 0) e->flash--;
        switch (e->type) {
            case 0: e->x -= 2; e->y = e->base_y + (sin64((e->t >> 1) + e->phase) * 13 >> 8); break;
            case 1: e->x -= 2; e->y = e->base_y + (sin64((e->t >> 1) + e->phase) * 17 >> 8); break;
            case 2:
                if (e->x > 200) e->x -= 2;
                else if (e->t > 380) e->x -= 2;
                e->y = e->base_y + (sin64(e->t >> 2) * 4 >> 8);
                if (e->x <= 200 && (e->t % 95) == 50)
                    spawn_eb_aimed(e->x - 10, e->y, 500);
                break;
            case 3:
                e->x -= 1;
                if ((e->t % 160) == 80) {
                    spawn_eb(e->x - 12, e->y, -560, -200);
                    spawn_eb(e->x - 14, e->y, -610, 0);
                    spawn_eb(e->x - 12, e->y, -560, 200);
                }
                break;
            case 4: e->x -= 4; e->y += e->vy; break;
            case 5:
                if (e->x > 200) e->x -= 1;
                else e->y = 104 + (sin64(e->t >> 1) * 45 >> 8);
                if (e->x <= 200 && (e->t % 85) == 42) {
                    spawn_eb_aimed(e->x - 14, e->y, 480);
                    spawn_eb(e->x - 14, e->y, -450, -140);
                    spawn_eb(e->x - 14, e->y, -450, 140);
                }
                break;
        }
        if (e->type != 4) {
            if (e->y < PLAY_Y0 + 8) e->y = PLAY_Y0 + 8;
            if (e->y > PLAY_Y1 - 8) e->y = PLAY_Y1 - 8;
        }
        if (e->x < -20 || (e->type == 4 && (e->y < PLAY_Y0 || e->y > PLAY_Y1))) {
            e->active = 0;
            continue;
        }
        if (!player_dead && invincible_t == 0) {
            int dx = e->x - px, dy;
            int r;
            if (dx > 22 || dx < -22) continue;   /* r <= 20 */
            dy = e->y - py;
            if (dy > 22 || dy < -22) continue;
            r = enemy_radius(e->type) + 6;
            if (dx * dx + dy * dy < r * r) {
                damage_enemy(e, 99);
                hit_player();
            }
        }
    }
}

static void update_bullets(void)
{
    int i, j;
    for (i = 0; i < 16; i++) {
        PBul *b = &pbul[i];
        if (!b->active) continue;
        b->t++;
        b->x += b->vx;
        b->y += b->vy;
        if (b->kind == 2 && b->t == 16) b->vy = 0;
        if (b->x > 258 || b->y < PLAY_Y0 || b->y > PLAY_Y1) {
            b->active = 0;
            continue;
        }
        {
            int consumed = 0;
            for (j = 0; j < 12 && !consumed; j++) if (enemies[j].active) {
                Enemy *e = &enemies[j];
                int dx = b->x - e->x, dy;
                int r;
                if (dx > 20 || dx < -20) continue;   /* r <= 18 */
                dy = b->y - e->y;
                if (dy > 20 || dy < -20) continue;
                r = enemy_radius(e->type) + 4;
                if (dx * dx + dy * dy < r * r) {
                    damage_enemy(e, b->kind == 2 ? 2 : 1);
                    consumed = 1;
                }
            }
            if (!consumed && phase == PH_BOSS)
                consumed = damage_boss_point(b->x, b->y, b->kind == 2 ? 2 : 1);
            if (consumed) b->active = 0;
        }
    }
    for (i = 0; i < 24; i++) {
        EBul *b = &ebul[i];
        int bx, by;
        if (!b->active) continue;
        b->x += b->vx;
        b->y += b->vy;
        bx = b->x >> 8; by = b->y >> 8;
        if (bx < -8 || bx > 260 || by < PLAY_Y0 || by > PLAY_Y1) {
            b->active = 0;
            continue;
        }
        if (!player_dead && invincible_t == 0) {
            int dx = bx - px, dy;
            if (dx > 10 || dx < -10) continue;   /* r = 9 */
            dy = by - py;
            if (dy > 10 || dy < -10) continue;
            if (dx * dx + dy * dy < 9 * 9) {
                b->active = 0;
                hit_player();
            }
        }
    }
    for (i = 0; i < 4; i++) {
        Item *c = &items[i];
        if (!c->active) continue;
        c->t++;
        c->x -= 1;
        c->y = c->oy + (sin64(c->t >> 1) * 5 >> 8);
        if (c->x < -16) { c->active = 0; continue; }
        if (!player_dead) {
            int dx = c->x - px, dy;
            if (dx > 21 || dx < -21) continue;   /* r = 20 */
            dy = c->y - py;
            if (dy > 21 || dy < -21) continue;
            if (dx * dx + dy * dy < 20 * 20) {
                c->active = 0;
                apply_item(c->type);
            }
        }
    }
    for (i = 0; i < 6; i++) {
        if (sparks[i].active) {
            sparks[i].t++;
            if (sparks[i].t >= 10) sparks[i].active = 0;
        }
    }
}

static void update_boss(void)
{
    int i;
    boss_t++;
    if (boss_flash > 0) boss_flash--;
    if (boss_x > 196) boss_x--;
    boss_y = 104 + (sin64(boss_t >> 2) * 44 >> 8);
    for (i = 0; i < 4; i++) {
        int a;
        if (shard_hp[i] <= 0) continue;
        if (shard_flash[i] > 0) shard_flash[i]--;
        a = (boss_t >> 1) + i * 16;
        shard_x[i] = boss_x + (cos64(a) * 30 >> 8);
        shard_y[i] = boss_y + (sin64(a) * 23 >> 8);
    }
    {
        int enraged = !boss_shielded() && boss_core_hp < 60;
        if (--boss_atk1 <= 0) {
            spawn_eb_aimed(boss_x - 16, boss_y, enraged ? 610 : 500);
            boss_atk1 = enraged ? 42 : 58;
        }
        if (--boss_atk2 <= 0) {
            for (i = 0; i < 12; i++) {
                int a = i * 5 + (boss_t & 7);
                spawn_eb(boss_x, boss_y, cos64(a) * 2, sin64(a) * 2);
            }
            boss_atk2 = 230;
        }
        if (--boss_atk3 <= 0) {
            for (i = 0; i < 4; i++)
                if (shard_hp[i] > 0) spawn_eb_aimed(shard_x[i], shard_y[i], 420);
            boss_atk3 = 140;
        }
    }
    if (!player_dead && invincible_t == 0) {
        int dx = boss_x - px, dy = boss_y - py;
        if (dx <= 34 && dx >= -34 && dy <= 34 && dy >= -34 &&
            dx * dx + dy * dy < 32 * 32) hit_player();
        for (i = 0; i < 4; i++) {
            if (shard_hp[i] > 0) {
                int sx = shard_x[i] - px, sy = shard_y[i] - py;
                if (sx <= 22 && sx >= -22 && sy <= 22 && sy >= -22 &&
                    sx * sx + sy * sy < 20 * 20) hit_player();
            }
        }
    }
}

/* WARNING panel restore: slices under the panel are redrawn a
   few at a time while the boss enters */
static int restore_left;
static void restore_panel_step(void)
{
    int n = 2;
    int was = restore_left;
    while (n-- > 0 && restore_left < 22) {
        int lx = 40 + restore_left * 8;
        if (is_v9958) {
#ifdef NOSCROLL
            draw_slice(lx, 0, lx);   /* frozen window: ring slot = world x */
#else
            int vx = (int)((scroll_x + lx) & 511);
            draw_slice(vx & 255, (vx >> 8), scroll_x + lx);
#endif
        } else {
            draw_slice(lx, 0, base_x + lx);
        }
        restore_left++;
    }
    /* the cascade repainted plain slices over the baked Earth:
       put the Earth back once the panel area is whole again */
    if (was < 22 && restore_left >= 22)
        blit_img(gpl_data, GPL_X, GPL_Y, GPL_WB, GPL_H, disp_page);
}

static void render(void)
{
    int i;
    spr_reset();
    /* FIXED zone (never rotated, always the front planes):
       ONLY the player group. The boss used to be fixed too,
       but a fixed boss did not save it - and worse, the real
       boss-fight problem turned out to be the laser: a flat
       beam's segments all share the same scanlines, so the
       8-per-line limit drops every segment past the first
       few no matter how the planes are registered. The only
       display-level cure is to rotate the beam's own plane
       order so the surviving segments cycle along its
       length - which needs the beam (and everything else)
       in the rotating zone. The boss flickers; accepted -
       it is big, slow and always roughly half on screen,
       while the ship, the one sprite the player tracks
       continuously, stays solid. */
    if (!player_dead && (invincible_t == 0 || (invincible_t & 2) == 0)) {
        if (shield_hp > 0) spr_add(px - 8, py - 8, PAT_HALO);
        spr_add(px - 8, py - 8, PAT_SHIP_A);
        spr_add(px - 8, py - 8, PAT_SHIP);
        for (i = 0; i < options_n; i++) {
            int idx = (int)((frame_count - (i + 1) * 24) & 255);
            spr_add(hist_x[idx] - 8, hist_y[idx] - 8, PAT_OPTION);
        }
    }
    spr_fixed = spr_n;
    /* ROTATING zone: priorities cycle every frame (spr_flush),
       so nothing here stays invisible - it flickers instead.
       Add order decides the window layout when the list is
       longer than the planes left: the player's own bullets
       first, then the laser, the boss, its shards, enemies,
       items, sparks and enemy bullets last. */
    for (i = 0; i < 16; i++)
        if (pbul[i].active)
            spr_add(pbul[i].x - 8, pbul[i].y - 8,
                    pbul[i].kind == 2 ? PAT_MISSILE : PAT_PBUL);
    /* laser segments */
    if (laser_len > 0 && !player_dead) {
        int nseg = laser_len / 16;
        int base_pat = prism_t > 0 ? PAT_PRISM : PAT_BEAM;
        for (i = 0; i < nseg; i++) {
            int x0 = px + 14 + i * 16;
            /* segment endpoints are history slots i and i+1 -
               the same values laser_y_at(x0) / laser_y_at(x0+16)
               compute, without the per-call division */
            int ya = hist_y[(int)((frame_count - i * 5) & 255)];
            int yb = hist_y[(int)((frame_count - (i + 1) * 5) & 255)];
            int s = yb - ya;
            if (s > 8) s = 8;
            if (s < -8) s = -8;
            spr_add(x0, ya - 8 + s / 2, base_pat + s + 8);
        }
    }
    /* boss core (rotating zone - see the fixed-zone note) */
    if (phase == PH_BOSS || phase == PH_DYING) {
        int t;
        for (t = 0; t < 9; t++)
            spr_add(boss_x - 24 + (t % 3) * 16, boss_y - 24 + (t / 3) * 16,
                    PAT_BOSS + t);
    }
    /* boss shards */
    if (phase == PH_BOSS || phase == PH_DYING) {
        for (i = 0; i < 4; i++)
            if (shard_hp[i] > 0)
                spr_add(shard_x[i] - 8, shard_y[i] - 8, PAT_SHARD);
    }
    /* enemies */
    for (i = 0; i < 12; i++)
        if (enemies[i].active)
            spr_add(enemies[i].x - 8, enemies[i].y - 8, enemy_pat[enemies[i].type]);
    /* items */
    for (i = 0; i < 4; i++)
        if (items[i].active) {
            spr_add(items[i].x - 8, items[i].y - 8, PAT_LETTER + (i & 1));
            spr_add(items[i].x - 8, items[i].y - 8, PAT_CAPSULE);
        }
    /* sparks */
    for (i = 0; i < 6; i++)
        if (sparks[i].active)
            spr_add(sparks[i].x - 8, sparks[i].y - 8, PAT_SPARK);
    /* enemy bullets */
    for (i = 0; i < 24; i++)
        if (ebul[i].active)
            spr_add((ebul[i].x >> 8) - 8, (ebul[i].y >> 8) - 8, PAT_EBUL);
    spr_flush();
}

/* ============================================================
 * Screens / game flow
 * ============================================================ */
#ifdef BENCH
#define BENCH_FRAMES 1800
static long bench_frames, bench_slips;
static unsigned int bench_prev_tick;
static int bench_tick_started;
#endif

static int play_game(void)
{
    int i;
    audio_silence();
    palett(15, 0, 6, 7);       /* C_BLINK back to steady cyan */
    /* reset state */
    score = 0;
    lives = 3;
    px = 36; py = 104;
    phase = PH_NORMAL;
    reset_powers();
    resonance = 0; prism_t = 0; laser_len = 0;
    shot_cd = missile_cd = option_cd = 0;
    invincible_t = 0; player_dead = 0; respawn_t = 0;
    item_rot = 0;
    spawn_idx = 0; next_spawn_dist = 120;
    for (i = 0; i < 12; i++) enemies[i].active = 0;
    for (i = 0; i < 16; i++) pbul[i].active = 0;
    for (i = 0; i < 24; i++) ebul[i].active = 0;
    for (i = 0; i < 4; i++) items[i].active = 0;
    for (i = 0; i < 6; i++) sparks[i].active = 0;
    for (i = 0; i < 256; i++) { hist_x[i] = (unsigned char)px; hist_y[i] = (unsigned char)py; }
    scroll_setup_stage();
    blit_img(gpl_data, GPL_X, GPL_Y, GPL_WB, GPL_H, disp_page);
    draw_hud_static();
    draw_hud_values();
    draw_band_static();
    restore_left = 22;
    bgm_restart(1);

    for (;;) {
        frame_sync();
#ifdef BENCH
        {
            unsigned int tick = *(volatile unsigned int *)0xfc9e;
            if (bench_tick_started &&
                (unsigned int)(tick - bench_prev_tick) >= 2)
                bench_slips++;
            bench_prev_tick = tick;
            bench_tick_started = 1;
        }
        bench_frames++;
        if (bench_frames >= BENCH_FRAMES) return ST_EXIT;
#endif
        merge_joystick();
        if (demo_mode) {
            if (lat_trig || lat_esc || lat_left || lat_right || lat_up || lat_down)
                return ST_TITLE;
        } else if (lat_esc) {
            return ST_TITLE;
        }

        if (phase == PH_NORMAL) {
            scroll_update(1);
            spawner();
            if (scroll_x >= STAGE_LEN) {
                phase = PH_WARN;
                warn_t = 190;
                scroll_stop();
                /* WARNING panel (background restored later) */
                ACPAGE = (unsigned char)disp_page;
                boxfil(40, 88, 215, 119, C_BLACK, 0);
                boxfil(40, 88, 215, 88, C_RED, 0);
                boxfil(40, 119, 215, 119, C_RED, 0);
                draw_str_centered(96, "WARNING", C_BLINK, C_BLACK, 2);
                draw_str_centered(112, "ABYSS PRISM LEVIATHAN", C_RED, C_BLACK, 1);
                restore_left = 0;
                bgm_restart(2);
            }
        } else if (phase == PH_WARN) {
            if (--warn_t <= 0) start_boss();
        } else if (phase == PH_DYING) {
            if ((dying_t & 7) == 0)
                spawn_spark(boss_x - 16 + (int)rnd(32), boss_y - 16 + (int)rnd(32));
            if (--dying_t <= 0) return ST_CLEAR;
        }
        if (restore_left < 22 && phase == PH_BOSS) restore_panel_step();

        /* player death / respawn */
        if (player_dead) {
            if (--respawn_t <= 0) {
                lives--;
                if (lives < 0) return ST_OVER;
                reset_powers();
                px = 36; py = 104;
                invincible_t = 120;
                player_dead = 0;
                for (i = 0; i < 24; i++) ebul[i].active = 0;
                draw_hud_values();
            }
        } else {
            update_player(lat_trig);
            hist_x[frame_count & 255] = (unsigned char)px;
            hist_y[frame_count & 255] = (unsigned char)py;
        }
        if (invincible_t > 0) invincible_t--;

        update_enemies();
        update_bullets();
        if (phase == PH_BOSS) update_boss();
        render();
        draw_hud_values();
        draw_band_one();
    }
}

static void draw_message(const char *big, int col, const char *sub)
{
    audio_silence();
    ACPAGE = (unsigned char)disp_page;
    boxfil(28, 76, 227, 132, C_BLACK, 0);
    boxfil(28, 76, 227, 76, C_CYAN, 0);
    boxfil(28, 132, 227, 132, C_CYAN, 0);
    boxfil(28, 76, 28, 132, C_CYAN, 0);
    boxfil(227, 76, 227, 132, C_CYAN, 0);
    draw_str_centered(84, big, col, C_BLACK, 2);
    draw_str_centered(116, sub, C_BLINK, C_BLACK, 1);
    hide_sprites();
}

static int title_screen(void)
{
    int blink = 0, frame = 0, idle = 0;
    audio_silence();
    if (is_v9958) {
        disp_page = 0;   /* V9958 ring: pages 0+1, left = 0 */
        vdp_reg(25, 3);
        vdp_reg(2, 0x3f);
        vdp_reg(26, 0);
        vdp_reg(27, 0);
    } else {
        disp_page = 0;
        vdp_reg(18, 0);
    }
    cur_r26 = cur_r27 = cur_adj = 0;
    ACPAGE = (unsigned char)disp_page;
    trace_step(8, is_v9958 ? 0x8000 : 0);
    boxfil(0, 0, 255, 211, C_BLACK, 0);
    trace_step(9, is_v9958 ? 0x8000 : 0);
    scroll_fill_title(600);
    trace_step(10, is_v9958 ? 0x8000 : 0);
    blit_img(tpl_data, TPL_X, TPL_Y, TPL_WB, TPL_H, disp_page);
    blit_logo_mask(disp_page);
    trace_step(11, is_v9958 ? 0x8000 : 0);
    draw_str_centered(122, "BEND THE LASER - FEED RESONANCE", C_ICE, C_BG, 1);
    draw_str_centered(140, "PRESS SPACE TO START", C_BLINK, C_BG, 1);
    draw_str_centered(164, "CURSOR / STICK : MOVE", C_WHITE, C_BG, 1);
    draw_str_centered(176, "SPACE / TRIGGER : SHOT", C_WHITE, C_BG, 1);
    trace_step(12, is_v9958 ? 0x8000 : 0);
    bgm_restart(0);
    kilbuf();
    trace_step(13, is_v9958 ? 0x8000 : 0);
    for (;;) {
        frame_sync();
        trace_step(14, is_v9958 ? 0x8000 : 0);
        merge_joystick();
        if (!lat_trig) break;
    }
    for (;;) {
        frame_sync();
        merge_joystick();
        frame++;
        if (++idle >= 300) return ST_DEMO;
        if ((frame & 31) == 0) {
            blink = !blink;
            if (blink) palett(15, 0, 0, 1);
            else palett(15, 0, 6, 7);
        }
        /* decor: the ship and a prism crystal */
        spr_reset();
        spr_add(116, 148, PAT_SHIP_A);
        spr_add(116, 148, PAT_SHIP);
        spr_add(196, 56 + (sin64(frame >> 2) * 6 >> 8), PAT_CRYSTAL);
        spr_flush();
        if (lat_trig) return ST_PLAY;
        if (lat_esc) idle = 0;   /* ESC on title: no DOS exit */
    }
}

#if !defined(DIAG) && !defined(DIAG2) && !defined(DIAG4) && !defined(DIAG5) && !defined(DIAG6) && !defined(DIAG8) && !defined(DIAG12) && !defined(DIAG15)
int main(void)
{
    int state = ST_TITLE;
    ginit();
    srnd();
    color(15, 1, 1);
    screen(5);
    trace_step(1, 0);
    setup_palette();
    trace_step(2, 0);
    detect_vdp();
    cpu_init_r800();
    trace_step(3, 0);
    init_sqrt_tab();
    trace_step(4, 0);
    setup_sprites();
    trace_step(5, 0);
    audio_init();
    trace_step(6, 0);
    last_jiffy = JIFFY;
    frame_count = 0;
    split_install();
    trace_step(7, 0);

#ifdef BENCH
    {
        unsigned int start = *(volatile unsigned int *)0xfc9e;
        unsigned int ticks;
        long fps100;
        bench_frames = 0;
        bench_slips = 0;
        bench_tick_started = 0;
        demo_mode = 1;
        while (bench_frames < BENCH_FRAMES)
            play_game();
        ticks = (unsigned int)((*(volatile unsigned int *)0xfc9e - start) & 0xffff);
        audio_stop();
        hide_sprites();
        split_remove();
        color(15, 1, 1);
        screen(0);
        fps100 = ticks ? (long)BENCH_FRAMES * 6000L / (long)ticks : 0;
        printf("BENCH frames=%d jiffy=%u fps=%ld.%02ld slips=%ld\n",
               BENCH_FRAMES, ticks, fps100 / 100, fps100 % 100, bench_slips);
        printf("VDP=%s scroll=%s\n", is_v9958 ? "V9958" : "V9938",
               is_v9958 ? "HW R26/R27" : "ADJUST R18+HMMM");
        return 0;
    }
#endif

    while (state != ST_EXIT) {
        if (state == ST_TITLE) {
            state = title_screen();
        } else if (state == ST_DEMO) {
            demo_mode = 1;
            state = play_game();
            demo_mode = 0;
#ifdef RECMODE
            /* Recording build only: hold the ALL CLEAR panel for
               a few seconds like a real clear does, instead of
               cutting straight back to the title. */
            if (state == ST_CLEAR) {
                int t;
                draw_message("ALL CLEAR", C_GREEN, "SPACE: RETRY  ESC: TITLE");
                bgm_restart(0);
                for (t = 0; t < 300; t++) frame_sync();
            }
#endif
            if (state == ST_OVER || state == ST_CLEAR) state = ST_TITLE;
        } else if (state == ST_PLAY) {
            state = play_game();
            if (state == ST_OVER) {
                draw_message("GAME OVER", C_RED, "SPACE: RETRY  ESC: TITLE");
                bgm_restart(0);
                state = wait_trigger_or_esc() ? ST_PLAY : ST_TITLE;
            } else if (state == ST_CLEAR) {
                draw_message("ALL CLEAR", C_GREEN, "SPACE: RETRY  ESC: TITLE");
                bgm_restart(0);
                state = wait_trigger_or_esc() ? ST_PLAY : ST_TITLE;
            }
        }
    }

    audio_stop();
    hide_sprites();
    split_remove();
    color(15, 1, 1);
    screen(0);
    return 0;
}
#endif /* !DIAG */

#ifdef DIAG
/* Startup diagnostic: paint a full-screen marker colour after
   each init step (both display pages, ~1 s apart). The colour the
   screen freezes on identifies the failing step:
     RED    = screen(5) + palette done
     GREEN  = init_sqrt_tab done
     YELLOW = setup_sprites done
     PURPLE = audio_init done
     PINK   = split_install done
     WHITE  = title slice fill done
     ICE    = logo + title texts done
     ORANGE = 300 wait_frame() calls survived
   A text-mode printf at the start reports the raw VDP detect. */
static void diag_delay(int n)
{
    unsigned char t = JIFFY;
    int c = 0;
    while (c < n) {
        if (JIFFY != t) { t = JIFFY; c++; }
    }
}
static void diag_marker(int col)
{
    ACPAGE = 0;
    boxfil(0, 0, 255, 211, col, 0);
    ACPAGE = 1;
    boxfil(0, 0, 255, 211, col, 0);
    ACPAGE = 0;
    diag_delay(50);
}
int main(void)
{
    int i;
    ginit();
    srnd();
    color(15, 1, 1);
    screen(0);
    printf("DIAG start\n");
    detect_vdp();
    printf("detect ok: is_v9958=%d\n", is_v9958);
    screen(5);
    setup_palette();
    diag_marker(C_RED);
    init_sqrt_tab();
    diag_marker(C_GREEN);
    setup_sprites();
    diag_marker(C_YELLOW);
    audio_init();
    diag_marker(C_PURPLE);
    last_jiffy = JIFFY;
    frame_count = 0;
    split_install();
    diag_marker(C_PINK);
    if (is_v9958) {
        disp_page = 0;   /* V9958 ring: pages 0+1, left = 0 */
        vdp_reg(25, 3);
        vdp_reg(2, 0x3f);
        vdp_reg(26, 0);
        vdp_reg(27, 0);
    } else {
        disp_page = 0;
        vdp_reg(18, 0);
    }
    cur_r26 = cur_r27 = cur_adj = 0;
    scroll_fill_title(600);
    diag_marker(C_WHITE);
    blit_logo_mask(disp_page);
    draw_str_centered(140, "PRESS SPACE TO START", C_BLINK, C_BG, 1);
    diag_marker(C_ICE);
    for (i = 0; i < 300; i++) wait_frame();
    diag_marker(C_ORANGE);
    audio_stop();
    split_remove();
    color(15, 1, 1);
    screen(0);
    printf("DIAG COMPLETE is_v9958=%d frames=%ld\n", is_v9958, frame_count);
    return 0;
}
#endif /* DIAG */

#ifdef DIAG8
/* DIAG7 twin with the ARMED split path: identical init markers,
   but the survival loop uses frame_sync() (split_arm/disarm with
   IE1 on and the H.KEYI handler live) instead of wait_frame().
   Completing prints the handler call count. */
static void diag8_delay(int n)
{
    unsigned char t = JIFFY;
    int c = 0;
    while (c < n) {
        if (JIFFY != t) { t = JIFFY; c++; }
    }
}
static void diag8_marker(int col)
{
    ACPAGE = 0;
    boxfil(0, 0, 255, 211, col, 0);
    ACPAGE = 1;
    boxfil(0, 0, 255, 211, col, 0);
    ACPAGE = 0;
    diag8_delay(50);
}
int main(void)
{
    int i;
    ginit();
    srnd();
    color(15, 1, 1);
    screen(0);
    printf("DIAG8 start\n");
    detect_vdp();
    printf("detect ok: is_v9958=%d\n", is_v9958);
    screen(5);
    setup_palette();
    diag8_marker(C_RED);
    init_sqrt_tab();
    diag8_marker(C_GREEN);
    setup_sprites();
    diag8_marker(C_YELLOW);
    audio_init();
    diag8_marker(C_PURPLE);
    last_jiffy = JIFFY;
    frame_count = 0;
    split_install();
    diag8_marker(C_PINK);
    if (is_v9958) {
        disp_page = 0;   /* V9958 ring: pages 0+1, left = 0 */
        vdp_reg(25, 3);
        vdp_reg(2, 0x3f);
        vdp_reg(26, 0);
        vdp_reg(27, 0);
    } else {
        disp_page = 0;
        vdp_reg(18, 0);
    }
    cur_r26 = cur_r27 = cur_adj = 0;
    scroll_fill_title(600);
    diag8_marker(C_WHITE);
    blit_logo_mask(disp_page);
    draw_str_centered(140, "PRESS SPACE TO START", C_BLINK, C_BG, 1);
    diag8_marker(C_ICE);
    for (i = 0; i < 300; i++) frame_sync();
    diag8_marker(C_ORANGE);
    audio_stop();
    split_remove();
    color(15, 1, 1);
    screen(0);
    printf("DIAG8 COMPLETE is_v9958=%d calls=%d\n", is_v9958, hook_calls);
    {
        unsigned char *hk = (unsigned char *)0xfd9a;
        printf("HOOK BYTES %02x %02x %02x\n", hk[0], hk[1], hk[2]);
    }
    return 0;
}
#endif /* DIAG8 */

#ifdef DIAG12
/* Minimal armed-exposure probe: install the hook with PERMARM
   (armed from the start, IE1 never enabled), wait a few jiffies
   in graphics mode, then return to text mode and report the
   handler call count, split_stage, and the live H.KEYI bytes. */
int main(void)
{
    unsigned char *hk = (unsigned char *)0xfd9a;
    int i;
    ginit();
    srnd();
    color(15, 1, 1);
    screen(0);
    printf("DIAG12 start\n");
    detect_vdp();
    screen(5);
    setup_palette();
    last_jiffy = JIFFY;
    frame_count = 0;
    split_install();
    {
        unsigned char t = JIFFY;
        int c = 0;
        while (c < 10) {
            if (JIFFY != t) { t = JIFFY; c++; }
        }
    }
    screen(0);
    printf("DIAG12 calls=%d stage=%d armed=%d\n",
           hook_calls, split_stage, hook_armed);
    printf("HOOK %02x %02x %02x saved %02x %02x %02x\n",
           hk[0], hk[1], hk[2],
           hook_saved[0], hook_saved[1], hook_saved[2]);
    for (i = 0; i < 5; i++) { }
    return 0;
}
#endif /* DIAG12 */

#ifdef DIAG15
/* DIAG12's short flow with a full-screen colour marker after
   every step, so the frozen colour names the dying step:
     RED    = screen(5) + palette done
     GREEN  = split_install done
     WHITE  = the 10-jiffy wait survived
     ORANGE = back in text mode, about to print the report */
static void diag15_delay(int n)
{
    unsigned char t = JIFFY;
    int c = 0;
    while (c < n) {
        if (JIFFY != t) { t = JIFFY; c++; }
    }
}
static void diag15_marker(int col)
{
    ACPAGE = 0;
    boxfil(0, 0, 255, 211, col, 0);
    ACPAGE = 1;
    boxfil(0, 0, 255, 211, col, 0);
    ACPAGE = 0;
    diag15_delay(50);
}
int main(void)
{
    unsigned char *hk = (unsigned char *)0xfd9a;
    ginit();
    srnd();
    color(15, 1, 1);
    screen(0);
    printf("DIAG15 start\n");
    detect_vdp();
    screen(5);
    setup_palette();
    diag15_marker(C_RED);
    last_jiffy = JIFFY;
    frame_count = 0;
    split_install();
    diag15_marker(C_GREEN);
    diag15_delay(10);
    diag15_marker(C_WHITE);
    screen(0);
    diag15_marker(C_ORANGE);
    printf("DIAG15 calls=%d stage=%d armed=%d\n",
           hook_calls, split_stage, hook_armed);
    printf("HOOK %02x %02x %02x saved %02x %02x %02x\n",
           hk[0], hk[1], hk[2],
           hook_saved[0], hook_saved[1], hook_saved[2]);
    return 0;
}
#endif /* DIAG15 */

#ifdef DIAG2
/* Staged split-install diagnostic. Markers (paint + ~2 s delay):
     RED    screen(5)+palette, GREEN sqrt, YELLOW sprites,
     PURPLE audio
     WHITE  hook bytes written only (no R#19, no IE1), delay survived
     ICE    R#19=16 armed (still no IE1), delay survived
     ORANGE IE1 on = full split install, 5 s delay survived
     PINK   64 boxfil stripes drawn with the split active
     CYAN   title slice fill done
   The colour it freezes on identifies the killing sub-step. */
static void diag2_delay(int n)
{
    unsigned char t = JIFFY;
    int c = 0;
    while (c < n) {
        if (JIFFY != t) { t = JIFFY; c++; }
    }
}
static void diag2_marker(int col, int delay)
{
    ACPAGE = 0;
    boxfil(0, 0, 255, 211, col, 0);
    ACPAGE = 1;
    boxfil(0, 0, 255, 211, col, 0);
    ACPAGE = 0;
    if (delay) diag2_delay(delay);
}
int main(void)
{
    int i;
    unsigned char *hook = (unsigned char *)0xfd9a;   /* H.KEYI */
    ginit();
    srnd();
    color(15, 1, 1);
    screen(0);
    printf("DIAG2 start\n");
    detect_vdp();
    printf("detect ok: is_v9958=%d\n", is_v9958);
    screen(5);
    setup_palette();
    diag2_marker(C_RED, 100);
    init_sqrt_tab();
    diag2_marker(C_GREEN, 100);
    setup_sprites();
    diag2_marker(C_YELLOW, 100);
    audio_init();
    diag2_marker(C_PURPLE, 100);
    last_jiffy = JIFFY;
    frame_count = 0;
    /* --- staged split install --- */
    for (i = 0; i < 5; i++) hook_saved[i] = hook[i];
    split_stage = 0;
    cur_r26 = cur_r27 = cur_adj = 0;
    hook[0] = 0xc3;
    hook[1] = (unsigned char)((int)split_handler & 0xff);
    hook[2] = (unsigned char)((int)split_handler >> 8);
    diag2_marker(C_WHITE, 120);          /* hook only */
    vdp_reg(19, 16);
    diag2_marker(C_ICE, 120);            /* R#19 armed, IE1 still off */
    vdp_reg(0, 0x16);
    diag2_marker(C_ORANGE, 300);         /* IE1 on: interrupts flowing */
    /* VDP command traffic with the split active (status-poll race?) */
    ACPAGE = 0;
    for (i = 0; i < 64; i++)
        boxfil(i * 4, 16, i * 4 + 3, 183, (i & 1) ? C_CYAN : C_PINK, 0);
    diag2_marker(C_PINK, 120);
    if (is_v9958) {
        disp_page = 0;   /* V9958 ring: pages 0+1, left = 0 */
        vdp_reg(25, 3);
        vdp_reg(2, 0x3f);
        vdp_reg(26, 0);
        vdp_reg(27, 0);
    } else {
        disp_page = 0;
        vdp_reg(18, 0);
    }
    cur_r26 = cur_r27 = cur_adj = 0;
    scroll_fill_title(600);
    diag2_marker(C_CYAN, 120);
    audio_stop();
    split_remove();
    color(15, 1, 1);
    screen(0);
    printf("DIAG2 COMPLETE is_v9958=%d\n", is_v9958);
    return 0;
}
#endif /* DIAG2 */

#ifdef DIAG4
/* Hook-mechanics diagnostic. A minimal counter handler is put on
   H.KEYI. The main thread does NOT wait on JIFFY; it busy-loops
   and repaints two hex bytes at the top-left every ~0.25 s:
     left  = hook call counter (low byte)
     right = JIFFY low byte
   Screenshots over time discriminate:
     counter frozen at 00, JIFFY frozen -> hook never called safely
     counter advancing, JIFFY frozen    -> BIOS skips its own
        VBlank work after the hook (calling-convention problem)
     both advancing                     -> hook is fine; the
        freeze lives in the VDP status polling instead
   Afterwards the original hook bytes are restored and a plain
   JIFFY delay + marker proves the machine recovered. */
static void hook_counter(void)
{
#asm
    push    af
    ld      a,(_hook_calls)
    inc     a
    ld      (_hook_calls),a
    pop     af
#endasm
}
static void diag4_busy(void)
{
    volatile long k;
    for (k = 0; k < 9000; k++) { }
}
int main(void)
{
    int i, iter;
    unsigned char *hook = (unsigned char *)0xfd9a;   /* H.KEYI */
    unsigned char saved[5];
    char buf[16];
    ginit();
    srnd();
    color(15, 1, 1);
    screen(0);
    printf("DIAG4 handler at %04x\n", (unsigned int)hook_counter);
    screen(5);
    setup_palette();
    ACPAGE = 0;
    boxfil(0, 0, 255, 211, C_RED, 0);
    diag4_busy(); diag4_busy(); diag4_busy(); diag4_busy();
    for (i = 0; i < 5; i++) saved[i] = hook[i];
    hook_calls = 0;
    hook[0] = 0xc3;
    hook[1] = (unsigned char)((int)hook_counter & 0xff);
    hook[2] = (unsigned char)((int)hook_counter >> 8);
    ACPAGE = 0;
    boxfil(0, 0, 255, 211, C_WHITE, 0);
    for (iter = 0; iter < 48; iter++) {
        sprintf(buf, "%02X %02X", hook_calls, JIFFY);
        ACPAGE = 0;
        boxfil(0, 0, 90, 20, C_BLACK, 0);
        draw_str(2, 2, buf, C_YELLOW, C_BLACK, 2);
        diag4_busy();
    }
    for (i = 0; i < 5; i++) hook[i] = saved[i];
    ACPAGE = 0;
    boxfil(0, 0, 255, 211, C_GREEN, 0);
    {
        unsigned char t = JIFFY;
        int c = 0;
        while (c < 120) {
            if (JIFFY != t) { t = JIFFY; c++; }
        }
    }
    color(15, 1, 1);
    screen(0);
    printf("DIAG4 COMPLETE calls=%u\n", hook_calls);
    return 0;
}
#endif /* DIAG4 */

#ifdef DIAG5
/* Full staged split-install with live counters. Each stage:
   paint stage colour, observe window (~4 s of busy-loop refresh
   of "stage digit, hook_calls, JIFFY" at top-left), then a tight
   JIFFY poll (90 ticks), then an OK block at (232,188).
   Stages: 1 hook bytes (full handler), 2 R#19=16, 3 IE1 on,
   4 64 boxfil stripes, 5 title slice fill. The frozen screen
   shows exactly which stage/activity died:
   counters frozen -> died before/during that stage's window;
   OK block missing -> died in the tight poll after the window;
   OK block present but next stage absent -> died at transition. */
static void diag5_busy(void)
{
    volatile long k;
    for (k = 0; k < 9000; k++) { }
}
static void diag5_observe(char digit)
{
    int iter;
    char buf[24];
    for (iter = 0; iter < 24; iter++) {
        sprintf(buf, "%c %02X %02X", digit, hook_calls, JIFFY);
        ACPAGE = 0;
        boxfil(0, 0, 110, 20, C_BLACK, 0);
        draw_str(2, 2, buf, C_YELLOW, C_BLACK, 2);
        diag5_busy();
    }
}
static void diag5_polltest(void)
{
    unsigned char t = JIFFY;
    int c = 0;
    while (c < 90) {
        if (JIFFY != t) { t = JIFFY; c++; }
    }
    ACPAGE = 0;
    boxfil(232, 188, 247, 203, C_WHITE, 0);
}
static void diag5_stage(int col, char digit)
{
    ACPAGE = 0;
    boxfil(0, 0, 255, 211, col, 0);
    ACPAGE = 1;
    boxfil(0, 0, 255, 211, col, 0);
    ACPAGE = 0;
    diag5_observe(digit);
    diag5_polltest();
}
int main(void)
{
    int i;
    unsigned char *hook = (unsigned char *)0xfd9a;   /* H.KEYI */
    ginit();
    srnd();
    color(15, 1, 1);
    screen(0);
    printf("DIAG5 start\n");
    detect_vdp();
    printf("detect ok: is_v9958=%d handler=%04x\n",
           is_v9958, (unsigned int)split_handler);
    screen(5);
    setup_palette();
    init_sqrt_tab();
    setup_sprites();
    audio_init();
    last_jiffy = JIFFY;
    frame_count = 0;
    hook_calls = 0;
    /* stage 1: hook bytes only (full split handler) */
    for (i = 0; i < 5; i++) hook_saved[i] = hook[i];
    split_stage = 0;
    cur_r26 = cur_r27 = cur_adj = 0;
    hook[0] = 0xc3;
    hook[1] = (unsigned char)((int)split_handler & 0xff);
    hook[2] = (unsigned char)((int)split_handler >> 8);
    diag5_stage(C_WHITE, '1');
    /* stage 2: arm R#19 */
    vdp_reg(19, 16);
    diag5_stage(C_ICE, '2');
    /* stage 3: IE1 on */
    vdp_reg(0, 0x16);
    diag5_stage(C_ORANGE, '3');
    /* stage 4: VDP command traffic */
    ACPAGE = 0;
    for (i = 0; i < 64; i++)
        boxfil(i * 4, 16, i * 4 + 3, 183, (i & 1) ? C_CYAN : C_PINK, 0);
    diag5_stage(C_PINK, '4');
    /* stage 5: title slice fill */
    if (is_v9958) {
        disp_page = 0;   /* V9958 ring: pages 0+1, left = 0 */
        vdp_reg(25, 3);
        vdp_reg(2, 0x3f);
        vdp_reg(26, 0);
        vdp_reg(27, 0);
    } else {
        disp_page = 0;
        vdp_reg(18, 0);
    }
    cur_r26 = cur_r27 = cur_adj = 0;
    scroll_fill_title(600);
    diag5_stage(C_CYAN, '5');
    audio_stop();
    split_remove();
    color(15, 1, 1);
    screen(0);
    printf("DIAG5 COMPLETE calls=%u\n", hook_calls);
    return 0;
}
#endif /* DIAG5 */

#ifdef DIAG6
/* DIAG2 refined: extra no-op stage (RED repaint) between the
   hook-only stage and the R#19 stage, so a freeze is attributable
   to exactly one sub-step. Every marker paints both pages, then
   tight-polls JIFFY for 90 ticks. Text mode first prints the
   original H.KEYI bytes. Stages:
     PURPLE audio done (pre-hook control)
     WHITE  hook bytes written
     RED    nothing changed (second poll test)
     GREEN  R#19 = 16 armed
     YELLOW IE1 on (R#0 = 0x16)
     PINK   64 boxfil stripes
     CYAN   scroll mode set + title slice fill
   Then DIAG6 COMPLETE on the text screen. */
static void diag6_delay(int n)
{
    unsigned char t = JIFFY;
    int c = 0;
    while (c < n) {
        if (JIFFY != t) { t = JIFFY; c++; }
    }
}
static void diag6_marker(int col)
{
    ACPAGE = 0;
    boxfil(0, 0, 255, 211, col, 0);
    ACPAGE = 1;
    boxfil(0, 0, 255, 211, col, 0);
    ACPAGE = 0;
    diag6_delay(90);
}
int main(void)
{
    int i;
    unsigned char *hook = (unsigned char *)0xfd9a;   /* H.KEYI */
    ginit();
    srnd();
    color(15, 1, 1);
    screen(0);
    printf("DIAG6 hook orig: %02x %02x %02x %02x %02x\n",
           hook[0], hook[1], hook[2], hook[3], hook[4]);
    detect_vdp();
    printf("detect ok: is_v9958=%d\n", is_v9958);
    screen(5);
    setup_palette();
    init_sqrt_tab();
    setup_sprites();
    audio_init();
    last_jiffy = JIFFY;
    frame_count = 0;
    diag6_marker(C_PURPLE);
    for (i = 0; i < 5; i++) hook_saved[i] = hook[i];
    split_stage = 0;
    cur_r26 = cur_r27 = cur_adj = 0;
    hook[0] = 0xc3;
    hook[1] = (unsigned char)((int)split_handler & 0xff);
    hook[2] = (unsigned char)((int)split_handler >> 8);
    diag6_marker(C_WHITE);
    diag6_marker(C_RED);
    vdp_reg(19, 16);
    diag6_marker(C_GREEN);
    vdp_reg(0, 0x16);
    diag6_marker(C_YELLOW);
    ACPAGE = 0;
    for (i = 0; i < 64; i++)
        boxfil(i * 4, 16, i * 4 + 3, 183, (i & 1) ? C_CYAN : C_PINK, 0);
    diag6_marker(C_PINK);
    if (is_v9958) {
        disp_page = 0;   /* V9958 ring: pages 0+1, left = 0 */
        vdp_reg(25, 3);
        vdp_reg(2, 0x3f);
        vdp_reg(26, 0);
        vdp_reg(27, 0);
    } else {
        disp_page = 0;
        vdp_reg(18, 0);
    }
    cur_r26 = cur_r27 = cur_adj = 0;
    scroll_fill_title(600);
    diag6_marker(C_CYAN);
    audio_stop();
    split_remove();
    color(15, 1, 1);
    screen(0);
    printf("DIAG6 COMPLETE is_v9958=%d\n", is_v9958);
    return 0;
}
#endif /* DIAG6 */

/* ---- split_handler: H.KEYI target. Defined LAST so the linker
   places it at the top of the image (page 2). A hook handler in
   page 0/1 is unreachable or wrongly reached when the BIOS/DOS
   interrupt path switches the low slots: the CPU then executes
   foreign bytes as the handler and control is lost (measured:
   wild execution inside draw_slice boxfil calls). ---- */
static void split_handler(void)
{
#ifdef HOOK_MIN
    /* bare-ret handler: isolates hook mechanics from handler body */
    return;
#else
#asm
    push    af
    push    bc
    push    hl
    ld      b,0             ; port accesses must use B = 0 (see
                            ; the getstat_m note at the top)
    ; --- Always read S#1 first: it is the only way to know why
    ; we were called, and the read clears FH. The stage machine
    ; must advance ONLY on a real line interrupt (FH = 1): the
    ; BIOS also calls H.KEYI at VBlank with FH = 0, and letting
    ; those calls advance the stage desynchronises R#19 from
    ; split_stage until a frame ends with IE1 on, R#19 = 184,
    ; stage = 1 and a stale FH that the disarmed early-out never
    ; clears - the interrupt then re-asserts forever and the
    ; main thread starves (the 2026-10-25 title-loop flood).
    ld      a,(_wrport)
    inc     a
    ld      c,a
    ld      a,1
    out     (c),a
    ld      a,8fh
    out     (c),a
    ld      a,(_rdport)
    inc     a
    ld      c,a
    in      a,(c)
    ld      h,a             ; keep S#1 (HL is saved, restored at exit)
    ld      a,(_wrport)
    inc     a
    ld      c,a
    xor     a
    out     (c),a
    ld      a,8fh
    out     (c),a
    ; not a line interrupt: this is the BIOS VBlank call. Force
    ; the stage machine back into sync (stage 0, next line IRQ
    ; at line 16) so a stage missed inside a main-thread DI
    ; stretch re-aligns on the next display frame instead of
    ; drifting - with permanent arming a stale stage would
    ; otherwise leave the game scroll applied across the
    ; HUD/band lines. In the healthy case the machine is
    ; already in exactly this state, so the resync is a no-op.
    ld      a,h
    and     1
    jr      nz,_sph_line
    ld      a,(_hook_armed)
    or      a
    jr      z,_sph_exit_early
    xor     a
    ld      (_split_stage),a
    ld      a,16
    out     (c),a
    ld      a,93h           ; register 19 (line interrupt)
    out     (c),a
#if defined(DIAGBORD)
    ld      a,5             ; DIAGBORD: border = blue at VBlank resync
    out     (c),a
    ld      a,87h           ; register 7 (border colour)
    out     (c),a
#endif
    jr      _sph_exit_early
_sph_line:
    ; a real line interrupt, but the split is not armed: FH is
    ; cleared above, which is all that is needed - touch nothing.
    ; (The handler body is beyond jr range of this gate, so the
    ; early exit has its own pop/ret copy here; body jumps
    ; still land on _sph_done at the end.)
    ld      a,(_hook_armed)
    or      a
    jr      nz,_sph_body
_sph_exit_early:
    pop     hl
    pop     bc
    pop     af
    ret
_sph_body:
#if defined(DIAG5) || defined(DIAG8) || defined(DIAG12) || defined(DIAG15)
    ld      a,(_hook_calls)
    inc     a
    ld      (_hook_calls),a
#endif
    ; component-bisect flags (diagnostic builds)
    ld      a,(_hook_do_arm)
    or      a
    jr      z,_sph_done
    ; which stage?
    ld      a,(_split_stage)
    or      a
    jr      nz,_sph_stage2
    ; ---- line 16 reached: apply game scroll ----
    ld      a,(_hook_do_apply)
    or      a
    jr      z,_sph_arm2
    ld      a,(_is_v9958)
    or      a
    jr      z,_sph_adj1
    ld      a,(_cur_r26)
    out     (c),a
    ld      a,9ah           ; register 26
    out     (c),a
    ld      a,(_cur_r27)
    out     (c),a
    ld      a,9bh           ; register 27
    out     (c),a
    jr      _sph_arm2
_sph_adj1:
    ld      a,(_cur_adj)
    out     (c),a
    ld      a,92h           ; register 18 (SET ADJUST)
    out     (c),a
_sph_arm2:
    ld      a,184
    out     (c),a
    ld      a,93h           ; register 19 (line interrupt)
    out     (c),a
#if defined(DIAGBORD)
    ld      a,12            ; DIAGBORD: border = red at stage 0
    out     (c),a
    ld      a,87h           ; register 7 (border colour)
    out     (c),a
#endif
    ld      a,1
    ld      (_split_stage),a
    jr      _sph_done
_sph_stage2:
    ; ---- line 184 reached: scroll back to 0 ----
    ld      a,(_hook_do_apply)
    or      a
    jr      z,_sph_arm1
    ld      a,(_is_v9958)
    or      a
    jr      z,_sph_adj2
    xor     a
    out     (c),a
    ld      a,9ah
    out     (c),a
    xor     a
    out     (c),a
    ld      a,9bh
    out     (c),a
    jr      _sph_arm1
_sph_adj2:
    xor     a
    out     (c),a
    ld      a,92h
    out     (c),a
_sph_arm1:
    ld      a,16
    out     (c),a
    ld      a,93h
    out     (c),a
#if defined(DIAGBORD)
    ld      a,9             ; DIAGBORD: border = green at stage 1
    out     (c),a
    ld      a,87h           ; register 7 (border colour)
    out     (c),a
#endif
    xor     a
    ld      (_split_stage),a
_sph_done:
    pop     hl
    pop     bc
    pop     af
#endasm
#endif /* HOOK_MIN */
}

/* End marker of the handler's machine code: this function's entry
   address is the first byte after the handler. split_install
   copies the bytes between split_handler and here into a BSS
   buffer in page 2 and points H.KEYI at the copy: a hook target
   in page 0/1 is not reliably reachable when the BIOS/DOS
   interrupt path calls H.KEYI with the low slots switched, and
   the CPU then executes foreign bytes as the handler (the
   2026-10-25 WebMSX / openMSX hang: wild execution during the
   title fill). */
void split_handler_tail(void)
{
#asm
    ret
#endasm
}
