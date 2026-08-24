/*
================================================================================
    platformer_app.h - the platformer app CONTRACT (declarations only)
================================================================================

    The single header both drivers include:
      * the DEMO runner (main.c) - a real window, real clock, real input;
      * the BENCH harness (the test/ suite) - includes THIS header for the
        symbols, links platformer_app.c as a C object, drives it with a fixed dt +
        seed + synthetic input under the capture seam.

    It declares ONLY types + prototypes (no RayClay DSL, no libc), so it is clean
    to include from C AND C++ - the app is consumed as a linkable object.

    ONE SOURCE, TWO MODES (the benchmark/showcase duality): the pure core
    (platformer_seed/update/layout) runs identically in both modes; only the
    clock/seed/input SOURCE differs, and platformer_demo_chrome adds a demo-only
    overlay. The app is an AUTO-RUNNER (the RUN never ends; the course is finite): the
    hero auto-runs at a fixed screen
    x while the world scrolls past it; the ONE action is JUMP via rcPressed (tap
    anywhere / a JUMP button - no keyboard, works desktop+web+mobile from one
    source). Its dominant cost (B9) is a FLOATING-POSITIONED SCENE: dozens of
    parallax/terrain/coin/hero/particle rects, each a floating tree-root
    re-attached at a NEW integer offset every frame - a distinct cost path from
    messenger (many text runs), notes (one large wrapped body) and trader (dense
    flex grids + gradient/shadow).

    Build target: rayclay_bench_platformer
================================================================================
*/
#ifndef PLATFORMER_APP_H
#define PLATFORMER_APP_H

#include "bench_app.h"            /* the shared AppCtx / AppInputSink / AppMode contract */
#include "platformer_backend.h"   /* PlWorld, embedded by value in AppState              */

/* The frozen bench scenario's version. Bump ONLY when the scripted path's rendered
   output changes. the bench harness emits it in the trend marker "SCENE platformer vN". */
#define PLATFORMER_BENCH_VERSION 2

/* Font ladder - baked from the bundled face. F_HERO=48 is the big score readout: it
   exercises the crisp-text-PERSISTS path (a large heading must stay crisp at 2x+ HiDPI
   / zoom and NEVER vanish - owner priority; the atlas overflow HOLDS the last scale). */
typedef enum { F_SMALL = 0, F_BODY, F_MD, F_TITLE, F_HERO, F_COUNT } PlatFont;

/* App state - a FLAT, memset-able POD blob, so platformer_seed can memset-then-build
   and the bench harness's run-twice determinism gate holds. All simulation lives in `world`. */
typedef struct {
    PlWorld world;        /* the deterministic auto-runner sim, BY VALUE            */
    bool    jumpQueued;   /* OR-accumulated by layout: rcPressed on the BUTTON (press edge),
                             rcClicked on the bare Stage (release edge); consumed on a dt>0 tick */
    bool    seeded;       /* demo lazy-init guard                                   */
} AppState;

/* The four-function app contract (+ the demo-only chrome). No RC_App / window handle;
   the input seam is the shared AppInputSink (bench_app.h). */
void platformer_seed  (AppState *st, unsigned seed);            /* = bench_seed: memset then build */
void platformer_update(AppState *st, const AppCtx *ctx);        /* dt<=0 STRICT no-op; else jump+tick */
void platformer_layout(AppState *st, const AppCtx *ctx);        /* the FROZEN core UI (both modes) */
void platformer_demo_chrome(AppState *st, const AppCtx *ctx);   /* demo-only overlay (never in bench) */
void platformer_bench_step(AppState *st, const AppInputSink *in, int frame); /* bench-only script */

#endif /* PLATFORMER_APP_H */
