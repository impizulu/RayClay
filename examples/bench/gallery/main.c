/*
================================================================================
    main.c - RayClay `gallery`: the demo runner (showcase mode)
================================================================================

    The thin desktop/web entry point for the gallery benchmark/showcase app. It is
    the ONLY translation unit that touches the RC_ runner (RC_App*): it opens a real
    window with real clock + input, seeds the app once (lazily, so the renderer +
    GL are up before rcLoadImageFromMemory uploads the textures), and drives the
    SAME pure core (gallery_update / gallery_layout) the benchmark harness drives -
    only the clock/seed/input source differs (see gallery_app.h). The demo adds a
    floating perf HUD via gallery_demo_chrome; the bench never runs it.

    Same source -> native desktop window AND web (cmake --preset web). Zero-asset:
    the 12 "photos" are procedurally generated as in-memory BMPs (no files to ship).
    Headless smoke: RAYCLAY_MAX_FRAMES=N -> opens, draws N frames, exits 0.

    App #5 of the standardised production benchmark/showcase apps
    (messenger -> notes -> trader -> platformer -> gallery -> opsdash).

    Build target: rayclay_bench_gallery
================================================================================
*/
#include "gallery_app.h"

static void demo_update(RC_App *app, void *userData) {
    AppState *st = (AppState *)userData;
    if (!st->seeded)                        /* seed once the renderer/GL is up (textures upload here) */
        gallery_seed(st, 0x6A11E70u);       /* a fixed demo seed */
    AppCtx ctx = app_demo_ctx(app);
    gallery_update(st, &ctx);
}

static void demo_layout(RC_App *app, void *userData) {
    AppState *st = (AppState *)userData;
    AppCtx ctx = app_demo_ctx(app);
    gallery_layout(st, &ctx);
    gallery_demo_chrome(st, &ctx);
}

int main(void) {
    static AppState state;   /* zero-init; gallery_seed fills it + loads images on frame 1 */

    static const float font_sizes[F_COUNT] = {
        [F_SMALL] = 12.0f,
        [F_BODY]  = 14.0f,
        [F_MD]    = 18.0f,
        [F_TITLE] = 30.0f,
        [F_HERO]  = 56.0f,
    };

    rcSetStyle(rcStyleDark());

    RC_AppOptions opts = {
        .width               = 1280,
        .height              = 720,
        .title               = "RayClay Gallery",
        .clearColor          = rcGetStyle().background,
        .fontSizes           = font_sizes,
        .fontCount           = F_COUNT,
        .scratchArenaBytes   = 4096,      /* backs rcFormat in the demo HUD only */
        .startLayoutElements = 4096,      /* the thumbnail grid + detail pane + modal */
        .nativeFrame         = true,
        .titlebarHeight      = 52,
        .titlebar            = { .custom = true },   /* the topbar IS the titlebar */
        .updateCallback            = demo_update,
        .layoutCallback            = demo_layout,
        .userData            = &state,
        /* This app's own scene is static, but its demo HUD prints a live fps
           readout - and a per-frame readout IS an animation: the text changes
           every frame, so the picture never settles and the app can never park.
           A benchmark HUD that reports 1 fps because it parked would be worse
           than useless, so the demo runner draws continuously.
           The lesson for a real app is the opposite one: do not put a live fps
           or frame counter in your UI unless you mean to pay for it. An
           ordinary app stays on demand and parks at ~0 CPU (see ex04).
           Only the DEMO runner is affected; the headless bench harness injects
           a fixed dt and never calls rcRunApp. */
        .renderMode          = RC_RENDER_CONTINUOUS,
    };

    return rcRunApp(&opts);
}
