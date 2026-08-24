/*
================================================================================
    platformer_app.c - the platformer app's pure-RC_ GUI + bench hooks
================================================================================

    The single implementation TU. It defines the four-function app contract
    (seed / update / layout / bench_step) + a demo-only chrome overlay, and it
    carries the header-only backend implementation (PLATFORMER_BACKEND_IMPLEMENTATION).

    PURE RC_ API - RayClay types only, no layout-engine call, no <system>
    include (all raw-memory + fixed-point sim lives in platformer_backend.h), so
    this file passes test/check-examples-pure-rc.sh unchanged. The FROZEN core
    (seed/update/layout) calls ZERO rcFormat: the only dynamic strings (score /
    distance / best) are formatted by the backend into fixed-width buffers on the
    deterministic tick, so their byte length is machine-invariant and the bench's
    two measured frames do byte-identical work. rcFormat is used ONLY in the
    demo-only chrome.

    THE SCENE IS FLOATING: every sprite (parallax, terrain, coin, hero, particle)
    is a floating child of "Stage", positioned by an integer .offset and
    marked RC_CAPTURE_PASSTHROUGH so the tap reaches the Stage/JUMP button beneath
    it. Only the JUMP button + HUD controls capture the pointer. Explicit zIndex
    bands keep the controls above the scene regardless of emission order.

    Build target: rayclay_bench_platformer
================================================================================
*/
#define PLATFORMER_BACKEND_IMPLEMENTATION
#include "platformer_app.h"

/* The frozen bench scenario length: warmup frames, then HOLD. the bench harness tunes this
   + the click coordinates to its measurement budget when it wires the app. */
#define PLATFORMER_BENCH_WARMUP 128

/* Stacking bands (M3): explicit z per layer so the interactive controls are always
   topmost for the hit test, independent of the order sprites are emitted. */
#define Z_FAR       10
#define Z_MID       20
#define Z_TERRAIN   30
#define Z_TUFT      35
#define Z_COIN      40
#define Z_HERO      50
#define Z_PARTICLE  60
#define Z_HUD       90
#define Z_JUMP     100

/* The scene palette (explicit hex - the game scene has its own twilight look, not
   the RC_Style chrome theme, which still dresses the titlebar + HUD text). */
#define C_SKY_TOP    0x0f1836
#define C_SKY_BOT    0x5c3b6e
#define C_GRASS      0x46b45f
#define C_DIRT       0x5a3f2a
#define C_COIN       0xfacc15
#define C_COIN_HI    0xfde68a
#define C_HERO_BODY  0x22d3ee
#define C_HERO_HEAD  0xfef3c7
#define C_HERO_LEG   0x0e7490
#define C_DUST       0xcbb89a

/* ── the shared floating recipe ──────────────────────────────────────────────── */

/* A decorative scene sprite: attach to the parent (Stage) at an integer pixel
   offset, on the given z band, PASSTHROUGH so it never eats the tap (H1). The
   integer offset is exact as a float while it stays within the stage (< 2^24). */
static RC_Float scene_float(int sx, int sy, int z) {
    return (RC_Float){ .to = RC_ATTACH_PARENT, .parent = RC_ANCHOR_TOP_LEFT,
                       .element = RC_ANCHOR_TOP_LEFT, .offset = { (float)sx, (float)sy },
                       .zIndex = (int16_t)z, .capture = RC_CAPTURE_PASSTHROUGH };
}

/* ── scene layers (each reads the frozen World, culls, emits floating rects) ──── */

/* Far parallax: low hills that drift at 1/4 the scroll speed. */
static void scene_parallax_far(const PlWorld *w, int sw) {
    for (int i = 0; i < w->hillCount; i++) {
        int sx = pl_parallax_x(w->hill[i].x, w->scrollX, 1, 4);
        if (!pl_visible(sx, w->hill[i].w, 460, sw))
            continue;
        int sy = PL_GROUND_LO - w->hill[i].h;      /* rise from the horizon line */
        rcBox(.wType = RC_PX(w->hill[i].w), .hType = RC_PX(w->hill[i].h),
               .bg = rcHex(w->hill[i].tint), .borderRadius = "all-lg",
               .floating = scene_float(sx, sy, Z_FAR)) {}
    }
}

/* Mid parallax: building silhouettes that drift at 1/2 the scroll speed. */
static void scene_parallax_mid(const PlWorld *w, int sw) {
    for (int i = 0; i < w->bldgCount; i++) {
        int sx = pl_parallax_x(w->bldg[i].x, w->scrollX, 1, 2);
        if (!pl_visible(sx, w->bldg[i].w, 160, sw))
            continue;
        int sy = PL_GROUND_LO - w->bldg[i].h;
        rcBox(.wType = RC_PX(w->bldg[i].w), .hType = RC_PX(w->bldg[i].h),
               .bg = rcHex(w->bldg[i].tint), .borderRadius = "all-sm",
               .floating = scene_float(sx, sy, Z_MID)) {}
    }
}

/* The terrain heightfield: each visible segment is a dirt column with a grass cap.
   The huge flat-tail segment is clamped to the visible window so its rect stays small. */
static void scene_terrain(const PlWorld *w, int sw, int sh) {
    for (int i = 0; i < w->segCount; i++) {
        int x0 = pl_ground_x(w, w->seg[i].startX);
        int x1 = pl_ground_x(w, w->seg[i].endX);
        if (x1 < -8 || x0 > sw + 8)
            continue;
        if (x0 < -8)     x0 = -8;
        if (x1 > sw + 8) x1 = sw + 8;
        int wpx = x1 - x0;
        if (wpx < 1)
            continue;
        int top = w->seg[i].topY;
        rcBox(.wType = RC_PX(wpx), .hType = RC_PX(sh - top), .bg = rcHex(C_DIRT),
               .floating = scene_float(x0, top, Z_TERRAIN)) {
            rcBox(.w = "grow", .h = "6px", .bg = rcHex(C_GRASS)) {}
        }
    }
}

/* Foreground grass tufts standing on the terrain (full scroll speed). */
static void scene_tufts(const PlWorld *w, int sw) {
    for (int i = 0; i < w->tuftCount; i++) {
        int sx = pl_ground_x(w, w->tuft[i].x);
        if (!pl_visible(sx, w->tuft[i].w, 40, sw))
            continue;
        int sy = pl_ground_top(w, w->tuft[i].x) - w->tuft[i].h;
        rcBox(.wType = RC_PX(w->tuft[i].w), .hType = RC_PX(w->tuft[i].h),
               .bg = rcHex(w->tuft[i].tint), .borderRadius = "all-sm",
               .floating = scene_float(sx, sy, Z_TUFT)) {}
    }
}

/* Uncollected coins: a gold disc with a light pip. */
static void scene_coins(const PlWorld *w, int sw) {
    for (int i = 0; i < w->coinCount; i++) {
        if (w->coin[i].collected)
            continue;
        int sx = pl_ground_x(w, w->coin[i].x) - PL_COIN_R;
        int sy = w->coin[i].y - PL_COIN_R;
        if (!pl_visible(sx, PL_COIN_R * 2, 40, sw))
            continue;
        rcBox(.wType = RC_PX(PL_COIN_R * 2), .hType = RC_PX(PL_COIN_R * 2), .align = "cc",
               .borderRadius = "all-full", .bg = rcHex(C_COIN),
               .floating = scene_float(sx, sy, Z_COIN)) {
            rcBox(.w = "8px", .h = "8px", .borderRadius = "all-full",
                   .bg = rcHex(C_COIN_HI)) {}
        }
    }
}

/* The hero: head + body + two legs whose heights alternate by the run phase while
   grounded (tucked equal while airborne). A fixed-size floating box at the fixed
   screen x; its foot rides the terrain via heroFoot256 (always >= 0). */
static void scene_hero(const PlWorld *w) {
    int footY = w->heroFoot256 >> 8;               /* operand is non-negative (H3) */
    int sx    = PL_HERO_SCREEN_X - PL_HERO_W / 2;
    int sy    = footY - PL_HERO_H;
    int phase = (int)(w->step % PL_RUN_PHASES);
    bool run  = w->grounded;
    int legA  = run ? (phase < PL_RUN_PHASES / 2 ? 12 : 5) : 8;
    int legB  = run ? (phase < PL_RUN_PHASES / 2 ? 5 : 12) : 8;
    rcBox(.wType = RC_PX(PL_HERO_W), .hType = RC_PX(PL_HERO_H), .align = "cc",
           .floating = scene_float(sx, sy, Z_HERO)) {
        rcColumn(.align = "cc", .gap = 1) {
            rcBox(.w = "12px", .h = "12px", .borderRadius = "all-full",
                   .bg = rcHex(C_HERO_HEAD)) {}
            rcBox(.w = "20px", .h = "14px", .borderRadius = "all-md",
                   .bg = rcHex(C_HERO_BODY)) {}
            rcRow(.h = "12px", .gap = 4, .align = "bc") {
                rcBox(.w = "6px", .hType = RC_PX(legA), .borderRadius = "all-sm",
                       .bg = rcHex(C_HERO_LEG)) {}
                rcBox(.w = "6px", .hType = RC_PX(legB), .borderRadius = "all-sm",
                       .bg = rcHex(C_HERO_LEG)) {}
            }
        }
    }
}

/* Dust kicked up while running: a fading disc drifting back off-screen. Positions
   come via integer division (well-defined even if a particle drifts to x < 0). */
static void scene_particles(const PlWorld *w, int sw) {
    for (int i = 0; i < PL_MAX_PARTICLES; i++) {
        if (w->part[i].ttl <= 0)
            continue;
        int sx = w->part[i].x256 / PL_SUB;
        int sy = w->part[i].y256 / PL_SUB;
        if (!pl_visible(sx, 8, 20, sw))
            continue;
        int sz = 3 + w->part[i].ttl / 6;
        rcBox(.wType = RC_PX(sz), .hType = RC_PX(sz), .borderRadius = "all-full",
               .bg = rcAlpha(rcHex(C_DUST), (unsigned char)(w->part[i].ttl * 10)),
               .floating = scene_float(sx, sy, Z_PARTICLE)) {}
    }
}

/* The score/distance/best HUD (top-left, PASSTHROUGH). The big COINS number is
   F_HERO=48 - the crisp-text-persists heading every app carries. All values come
   from the backend's fixed-width buffers (rcFormat-free). */
static void scene_hud(const PlWorld *w) {
    rcBox(.px = 16, .py = 10,
           .floating = { .to = RC_ATTACH_PARENT, .parent = RC_ANCHOR_TOP_LEFT,
                         .element = RC_ANCHOR_TOP_LEFT, .offset = { 4, 4 },
                         .zIndex = Z_HUD, .capture = RC_CAPTURE_PASSTHROUGH }) {
        rcRow(.gap = 20, .align = "cl") {
            rcColumn(.gap = 0) {
                rcTextL("COINS", .font = F_SMALL, .color = rcAlpha(RC_WHITE, 200));
                rcTextC(w->scoreStr, .font = F_HERO, .color = rcHex(C_COIN));
            }
            rcColumn(.gap = 0) {
                rcTextL("DIST", .font = F_SMALL, .color = rcAlpha(RC_WHITE, 200));
                rcTextC(w->distStr, .font = F_TITLE, .color = RC_WHITE);
            }
            rcColumn(.gap = 0) {
                rcTextL("BEST", .font = F_SMALL, .color = rcAlpha(RC_WHITE, 200));
                rcTextC(w->bestStr, .font = F_TITLE, .color = rcAlpha(RC_WHITE, 220));
            }
        }
    }
}

/* The one interactive control: a round JUMP button (bottom-right, CAPTURE, top z). */
static void scene_jump_button(void) {
    bool hov = rcIsHovered("JumpBtn");
    rcBox(.id = "JumpBtn", .w = "116px", .h = "116px", .align = "cc",
           .bg = rcAlpha(rcHex(C_HERO_BODY), hov ? 235 : 180), .borderRadius = "all-full",
           .floating = { .to = RC_ATTACH_PARENT, .parent = RC_ANCHOR_BOTTOM_RIGHT,
                         .element = RC_ANCHOR_BOTTOM_RIGHT, .offset = { -28, -28 },
                         .zIndex = Z_JUMP, .capture = RC_CAPTURE_ON }) {
        rcTextL("JUMP", .font = F_TITLE, .color = RC_WHITE);
    }
}

/* A faint how-to-play hint (bottom-centre, PASSTHROUGH). */
static void scene_hint(void) {
    rcBox(.px = 12, .py = 6, .bg = rcAlpha(RC_BLACK, 120), .borderRadius = "all-full",
           .floating = { .to = RC_ATTACH_PARENT, .parent = RC_ANCHOR_BOTTOM_CENTER,
                         .element = RC_ANCHOR_BOTTOM_CENTER, .offset = { 0, -28 },
                         .zIndex = Z_HUD, .capture = RC_CAPTURE_PASSTHROUGH }) {
        rcTextL("Tap anywhere or press JUMP to leap", .font = F_SMALL,
                 .color = rcAlpha(RC_WHITE, 205));
    }
}

/* ── the custom titlebar (nativeFrame + .titlebar.custom) ─────────────────────── */
static void platformer_topbar(void) {
    RC_Style s = rcGetStyle();
    rcRow(.id = RC_ID_WINDOW_DRAG, .w = "grow", .h = "52px", .bg = s.chrome,
           .px = 14, .gap = 10, .align = "cl") {
        rcBox(.w = "26px", .h = "26px", .align = "cc",
               .bg = s.primary, .borderRadius = "all-md") {
            rcTextL("R", .font = F_BODY, .color = RC_WHITE);
        }
        rcTextL("RayClay Runner", .font = F_MD, .color = s.text);
        rcBox(.w = "grow") {}
        rcWindowControls();
    }
}

/* ── the four-function contract ──────────────────────────────────────────────── */

void platformer_seed(AppState *st, unsigned seed) {
    pl_memzero(st, sizeof *st);
    pl_world_seed(&st->world, seed);
    st->jumpQueued = false;
    st->seeded     = true;
}

void platformer_update(AppState *st, const AppCtx *ctx) {
    if (ctx->dt <= 0.0f)                    /* H2: dt <= 0 is a STRICT no-op (the freeze) */
        return;
    if (st->jumpQueued)
        pl_world_jump(&st->world);          /* grounded-gated inside; idempotent airborne */
    st->jumpQueued = false;
    pl_world_step(&st->world);              /* one fixed tick: scroll, physics, coins, dust, HUD */
}

void platformer_layout(AppState *st, const AppCtx *ctx) {
    rcSetStyle(rcStyleDark());
    RC_Style s = rcGetStyle();
    const PlWorld *w = &st->world;
    /* DEMO fills the LIVE window so a maximised showcase has no bare bands; BENCH
       holds the fixed design space (PL_DESIGN_W/H) so the two measured frames stay
       byte-identical - the live viewport must NEVER enter the frozen bench layout.
       Only ever GROW past the design size (a smaller window just clips, never gaps). */
    int sw = PL_DESIGN_W, sh = PL_DESIGN_H;
    if (ctx->mode == APP_DEMO) {
        if (ctx->fbWidth       > sw) sw = ctx->fbWidth;
        if (ctx->fbHeight - 52 > sh) sh = ctx->fbHeight - 52;   /* Stage = window - titlebar */
    }

    rcColumn(.id = "Root", .w = "grow", .h = "grow", .bg = s.background) {
        platformer_topbar();
        /* Stage: the play area + the twilight sky gradient (its stable id "Stage"
           satisfies the gradient id rule; it is also the tap-to-jump target). The
           scene layers below attach to it as floating children, back to front. */
        rcBox(.id = "Stage", .w = "grow", .h = "grow",
               .gradient = { .from = rcHex(C_SKY_TOP), .to = rcHex(C_SKY_BOT), .dir = "v" }) {
            scene_parallax_far(w, sw);
            scene_parallax_mid(w, sw);
            scene_terrain(w, sw, sh);
            scene_tufts(w, sw);
            scene_coins(w, sw);
            scene_hero(w);
            scene_particles(w, sw);
            scene_hud(w);
            scene_hint();
            scene_jump_button();
        }
    }

    /* Read the tap AFTER the scene is declared, unioning two DISJOINT sources on
       DELIBERATELY DIFFERENT EDGES - one scene demonstrating both:

         rcPressed("JumpBtn")  PRESS edge. Fires on the way DOWN and cannot be
                               cancelled. Correct HERE and only here: a jump is a
                               single non-destructive action whose LATENCY is the
                               whole point, and it is a real button that captures
                               its own taps.
         rcClicked("Stage")    RELEASE edge (the default). The Stage is the WHOLE
                               PLAYFIELD, reached through the PASSTHROUGH scene
                               sprites; an eager activation there would fire on any
                               press anywhere with no way to slide off and take it
                               back.

       OR-accumulate into jumpQueued (set true only; consumed + cleared by the next
       dt>0 tick) so a queued jump can never be lost. */
    if (rcPressed("JumpBtn") || rcClicked("Stage"))
        st->jumpQueued = true;

    /* The bare Stage is a game playfield, not a button: polling it for taps armed
       the web clickable-hand over the whole surface. Restore the arrow everywhere
       except the JUMP button, whose own pointer cursor must survive (an explicit
       set wins over the poll's default). */
    if (!rcIsHovered("JumpBtn"))
        rcSetCursor(RC_CURSOR_DEFAULT);
}

void platformer_demo_chrome(AppState *st, const AppCtx *ctx) {
    if (ctx->mode != APP_DEMO || !ctx->arena)
        return;
    /* A floating perf/status HUD - demo-only, PASSTHROUGH so it never blocks the
       JUMP button or titlebar. Bottom-left, clear of the HUD/JUMP/hint. */
    RC_String hud = rcFormat(ctx->arena, "%.0f fps \xc2\xb7 %d coins \xc2\xb7 %d px",
                                ctx->dt > 0.0f ? 1.0f / ctx->dt : 0.0f,
                                st->world.score, st->world.scrollX);
    rcBox(.id = "demo_hud", .bg = rcAlpha(RC_BLACK, 150), .px = 10, .py = 5,
           .borderRadius = "all-full",
           .floating = { .to = RC_ATTACH_ROOT, .parent = RC_ANCHOR_BOTTOM_LEFT,
                         .element = RC_ANCHOR_BOTTOM_LEFT, .offset = { 16, -16 },
                         .zIndex = Z_JUMP + 10, .capture = RC_CAPTURE_PASSTHROUGH }) {
        rcText(hud, .font = F_SMALL, .color = RC_WHITE);
    }
}

void platformer_bench_step(AppState *st, const AppInputSink *in, int frame) {
    (void)st;   /* the platformer has no external event: EVERY action is synthetic input */
    /* The frozen scripted scenario. EVERY user action goes through the input sink (B3).
       At/after PLATFORMER_BENCH_WARMUP the app HOLDS - a strict no-op - so a
       double-rendered frame is byte-identical.

       DETERMINISM AT THE HOLD: the hero must be GROUNDED with jumpQueued == false at
       the freeze (H2). The scripted jumps fire early (frames 4-40), so their arcs
       (airtime ~50 ticks) all land well before WARMUP=128. Before the hold the pointer
       is parked OFF-CANVAS so no control renders hovered and no dwell clock ticks.
       COORDS are a FIRST DRAFT for 1280x720 (the JUMP button sits at ~x1192,y598);
       retune them and WARMUP to your budget. */
    if (!in || frame >= PLATFORMER_BENCH_WARMUP)
        return;                                        /* the HOLD */

    if (frame == 4 || frame == 20 || frame == 40) {
        in->move(in->ctx, 1192.0f, 598.0f);           /* over the JUMP button */
        in->button(in->ctx, APP_MBTN_LEFT, true);      /* press -> rcPressed -> queue a jump */
    } else if (frame == 5 || frame == 21 || frame == 41) {
        /* The release fires NOTHING now that the button reads the press edge, and it
           stays exactly for that reason: dropping it would leave the left button held
           down for the rest of the run, changing Stage's release edge and the hover
           state at the HOLD. The EDGE moved; the input script did not. */
        in->button(in->ctx, APP_MBTN_LEFT, false);
    } else if (frame == PLATFORMER_BENCH_WARMUP - 2) {
        in->move(in->ctx, -100.0f, -100.0f);           /* park OFF-canvas before the hold */
    }
}
