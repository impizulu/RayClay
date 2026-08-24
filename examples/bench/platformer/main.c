/*
================================================================================
    main.c - RayClay `platformer`: the demo runner (showcase mode)
================================================================================

    The thin desktop/web entry point for the platformer benchmark/showcase app. It
    is the ONLY translation unit that touches the RC_ runner (RC_App*): it opens a
    real window with real clock + input, seeds the app once (lazily, so the renderer
    is up), and drives the SAME pure core (platformer_update / platformer_layout) the
    benchmark harness drives - only the clock/seed/input source differs (see
    platformer_app.h). The demo adds a floating perf HUD via platformer_demo_chrome;
    the bench never runs it.

    The app is an AUTO-RUNNER: the hero runs on its own; the ONE action is JUMP - tap
    anywhere (rcClicked, release edge) or press the on-screen JUMP button (rcPressed,
    press edge - one scene, both edges). No keyboard is used, so
    the same source is playable on desktop, web AND mobile with no #ifdef.

    Same source -> native desktop window AND web (cmake --preset web). Zero-asset:
    the bundled font + procedural rect-drawn scene, no files to ship. Headless smoke:
    RAYCLAY_MAX_FRAMES=N -> opens, draws N frames, exits 0.

    App #4 of the standardised production benchmark/showcase apps
    (messenger -> notes -> trader -> platformer -> gallery -> opsdash).

    Build target: rayclay_bench_platformer
================================================================================
*/
#include "platformer_app.h"

static void demo_update(RC_App *app, void *userData) {
    AppState *st = (AppState *)userData;
    if (!st->seeded)                        /* seed once the renderer/GL is up */
        platformer_seed(st, 0x1A2B3C4Du);   /* a fixed demo seed */
    AppCtx ctx = app_demo_ctx(app);
    platformer_update(st, &ctx);
}

static void demo_layout(RC_App *app, void *userData) {
    AppState *st = (AppState *)userData;
    AppCtx ctx = app_demo_ctx(app);
    platformer_layout(st, &ctx);
    platformer_demo_chrome(st, &ctx);
}

int main(void) {
    static AppState state;   /* zero-init; platformer_seed fills it on frame 1 */

    static const float font_sizes[F_COUNT] = {
        [F_SMALL] = 12.0f,
        [F_BODY]  = 14.0f,
        [F_MD]    = 18.0f,
        [F_TITLE] = 28.0f,
        [F_HERO]  = 48.0f,
    };

    rcSetStyle(rcStyleDark());

    RC_AppOptions opts = {
        .width               = 1280,
        .height              = 720,
        .title               = "RayClay Runner",
        .clearColor          = rcGetStyle().background,
        .fontSizes           = font_sizes,
        .fontCount           = F_COUNT,
        .scratchArenaBytes   = 4096,      /* backs rcFormat in the demo HUD only */
        .startLayoutElements = 2048,      /* ~100-120 floating scene roots at peak, w/ headroom (M4) */
        .nativeFrame         = true,
        .titlebarHeight      = 52,
        .titlebar            = { .custom = true },   /* the topbar IS the titlebar */
        .updateCallback            = demo_update,
        .layoutCallback            = demo_layout,
        .userData            = &state,
        /* A game steps its own physics, so it must draw every frame.
           RC_RENDER_ON_DEMAND is the default and an app that animates without
           asking for a frame simply stops once the window goes idle.
           Most apps should NOT copy this: stay on demand and call
           rcAppRequestFrame() when your state changes (ex03, ex10) or
           rcAppRequestFrameAfter() for a timed step (ex12). That is what keeps
           an idle RayClay window at ~0 CPU. */
        .renderMode          = RC_RENDER_CONTINUOUS,
    };

    return rcRunApp(&opts);
}
