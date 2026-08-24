/*
================================================================================
    main.c - RayClay `trader`: the demo runner (showcase mode)
================================================================================

    The thin desktop/web entry point for the trader benchmark/showcase app. It is
    the ONLY translation unit that touches the RC_ runner (RC_App*): it opens a real
    window with real clock + input, seeds the app once (lazily, so the renderer is
    up), and drives the SAME pure core (trader_update / trader_layout) the benchmark
    harness drives - only the clock/seed/input source differs (see trader_app.h).
    The demo adds a floating perf HUD via trader_demo_chrome; the bench never runs it.

    Same source -> native desktop window AND web (cmake --preset web). Zero-asset:
    the bundled font + procedural chrome + rect-drawn candlesticks, no files to ship.
    Headless smoke: RAYCLAY_MAX_FRAMES=N -> opens, draws N frames, exits 0.

    App #3 of the standardised production benchmark/showcase apps
    (messenger -> notes -> trader -> platformer -> gallery -> opsdash).

    Build target: rayclay_bench_trader
================================================================================
*/
#include "trader_app.h"

static void demo_update(RC_App *app, void *userData) {
    AppState *st = (AppState *)userData;
    if (!st->seeded)                        /* seed once the renderer/GL is up */
        trader_seed(st, 0x517A7Eu);         /* a fixed demo seed */
    AppCtx ctx = app_demo_ctx(app);
    trader_update(st, &ctx);
}

static void demo_layout(RC_App *app, void *userData) {
    AppState *st = (AppState *)userData;
    AppCtx ctx = app_demo_ctx(app);
    trader_layout(st, &ctx);
    trader_demo_chrome(st, &ctx);
}

int main(void) {
    static AppState state;   /* zero-init; trader_seed fills it on frame 1 */

    static const float font_sizes[F_COUNT] = {
        [F_SMALL] = 12.0f,
        [F_BODY]  = 14.0f,
        [F_MD]    = 16.0f,
        [F_HEAD]  = 20.0f,
        [F_TITLE] = 26.0f,
        [F_HERO]  = 52.0f,
    };

    rcSetStyle(rcStyleDark());

    RC_AppOptions opts = {
        .width               = 1280,
        .height              = 720,
        .title               = "RayClay Markets",
        .clearColor          = rcGetStyle().background,
        .fontSizes           = font_sizes,
        .fontCount           = F_COUNT,
        .scratchArenaBytes   = 4096,      /* backs rcFormat in the demo HUD only */
        .startLayoutElements = 8192,      /* the densest app: watchlist + book + ~48 candle rects */
        .nativeFrame         = true,
        .titlebarHeight      = 52,
        .titlebar            = { .custom = true },   /* the topbar IS the titlebar */
        .updateCallback            = demo_update,
        .layoutCallback            = demo_layout,
        .userData            = &state,
        /* A live market feed advances on its own clock and the HUD reports FPS,
           so this demo must draw every frame. RC_RENDER_ON_DEMAND is the
           default and a self-animating app parks once the window goes idle.
           A normal app should NOT copy this: stay on demand and call
           rcAppRequestFrame() when your state changes (ex03, ex10), or
           rcAppRequestFrameAfter() for a timed step (ex12) - that is what holds
           an idle window at ~0 CPU. Only the DEMO runner is affected; the
           headless bench harness injects a fixed dt and never calls rcRunApp. */
        .renderMode          = RC_RENDER_CONTINUOUS,
    };

    return rcRunApp(&opts);
}
