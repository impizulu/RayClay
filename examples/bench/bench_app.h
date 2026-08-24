/*
================================================================================
    bench_app.h - the shared contract for the standardised benchmark/showcase apps
================================================================================

    RayClay ships 5 complex production apps (messenger, notes, gallery, trader,
    platformer) that DOUBLE as the showcase gallery AND the standardised
    perf benchmark - one source = the thing we ship = the thing we benchmark.

    This header holds the ONE contract they all share. Each app declares its own
    per-app AppState + the four functions

        void <app>_seed  (AppState *st, unsigned seed);            // = bench_seed
        void <app>_update(AppState *st, const AppCtx *ctx);        // advance by ctx->dt
        void <app>_layout(AppState *st, const AppCtx *ctx);        // the FROZEN core UI
        void <app>_bench_step(AppState *st, const AppInputSink *in, int frame);
        void <app>_demo_chrome(AppState *st, const AppCtx *ctx);   // demo-only overlay

    The AppCtx + AppInputSink types are IDENTICAL across every app, so the
    deterministic harness driver is ONE generic template parameterised by the app,
    not five bespoke drivers. (AppState is per-app; a bench driver includes one
    app header per translation unit.)

    ONE SOURCE, TWO MODES: DEMO (real clock/RNG/input - the shippable showcase) and
    BENCH (an injected fixed dt + fixed seed + a self-driven scripted scenario,
    headless, reaching a quiescent freeze so the benchmark's two-frame-difference Ir is
    byte-identical). Only the clock/seed/input SOURCE differs; the pure core is the same.
================================================================================
*/
#ifndef BENCH_APP_H
#define BENCH_APP_H

#include "rayclay.h"   /* RC_Arena, referenced by AppCtx */

typedef enum { APP_DEMO = 0, APP_BENCH } AppMode;

/* Per-frame context - decouples the pure layout from RC_App* (headless-safe). The
   demo runner fills it from the live app; the bench harness fills it with fixed
   values (dt = fixed 1/60, 0 to freeze; fb 1280x720; zoom 1; arena = NULL). */
typedef struct {
    RC_Arena *arena;    /* scratch for rcFormat (demo chrome only); bench = NULL */
    float     dt;       /* injected seconds/frame; 0 == the freeze */
    int       fbWidth;  /* logical viewport width  */
    int       fbHeight; /* logical viewport height */
    float     zoom;     /* display zoom factor (bench = 1.0) */
    AppMode   mode;     /* gates the demo-only chrome overlay */
} AppCtx;

/* The DEMO half of that fill: read the live clock/size/zoom off the running app.
   Every demo runner's main.c needs exactly this, so it lives here once rather than
   six times. (The BENCH half is the harness filling the same struct with its fixed
   values; it never calls this.)
   static inline for the same reason the per-app query helpers are: the headless
   bench translation units include this header but do NOT link the L3 runner, so an
   unused static inline is never emitted and naming rcApp* here costs them nothing. */
static inline AppCtx app_demo_ctx(RC_App *app) {
    RC_Dimensions d = rcGetWindowDimensions();
    AppCtx ctx = {
        .arena    = rcAppArena(app),
        .dt       = rcAppFrameTime(app),
        .fbWidth  = (int)d.width,
        .fbHeight = (int)d.height,
        .zoom     = rcAppZoom(app),
        .mode     = APP_DEMO,
    };
    return ctx;
}

/* The input seam (B3). An app CANNOT include test/rc_input_mock.h (it lives in
   test/, and examples must not depend on it), so the harness fills this abstract
   sink with adapters that forward to the mock. Members STAGE this-frame input only
   (the harness owns the clock + frame advance + the press/release edge tracking).
   Each app's <app>_bench_step drives EVERY interaction through it, so the bench
   exercises the real hit-test / focus / caret cost path - never a direct state write. */
typedef enum { APP_MBTN_LEFT = 0, APP_MBTN_RIGHT } AppMouseButton;

typedef struct AppInputSink {
    void *ctx;                                            /* harness-owned opaque */
    void (*move)  (void *ctx, float x, float y);          /* pointer position (level) */
    void (*button)(void *ctx, AppMouseButton b, bool down); /* button held; edges = harness */
    void (*wheel) (void *ctx, float dx, float dy);       /* scroll notches this frame */
    void (*text)  (void *ctx, unsigned int codepoint);  /* one typed char this frame */
    void (*key)   (void *ctx, int semanticKey, bool down); /* editing key (minimal) */
} AppInputSink;

#endif /* BENCH_APP_H */
