/*
================================================================================
    opsdash_app.h - the ops-dashboard app's contract (state + the five hooks)
================================================================================

    RayClay `opsdash`: a service-operations dashboard, and the benchmark suite's
    PARTIALLY-STATIC subject.

    WHY THIS APP EXISTS. The other five bench apps are each dominated by one cost:
    reader is text, feed is layout, titlebar is vertex emission, gallery is layout +
    renderer. None of them answers the question the render-only static-island
    experiment (L1) asks - "what does it cost to re-declare a subtree that did not
    change?" - because in a scene where everything changes there is nothing to skip,
    and in a scene where nothing changes there is nothing to get wrong. A scene that
    cannot fail an experiment is worth as little as a gate that cannot fail.

    So the frame is cut deliberately:

      LARGE + STATIC   a 48-service inventory grid and an 8-group nav. Written once
                       by ops_seed and never again.
      SMALL + CHANGING a telemetry rail: a 24-sample latency sparkline, a rolling
                       request counter, an incident clock, a pulsing banner.

    AND THE SIX INVALIDATION AXES ARE ALL PRESENT ON PURPOSE, because L1's kill
    criterion is a GAP in the invalidation matrix, and an axis the subject does not
    exercise is an axis the matrix is never tested on:

      clip + scroll   the inventory lives inside a scroll container
      zoom / DPR      every size is honoured through ctx->zoom
      atlas / font    the counter rolls through digit glyphs over time
      hover / focus   hovering a card mutates the STATIC island - the single most
                      likely thing an "unchanged subtree" cache gets wrong
      animation       the banner pulse advances every frame

    One binary, both arms. ops_tick returns immediately when ctx->dt == 0, so the
    injected-dt freeze the bench harness already has (bench_app.h) turns this same
    scene fully static. The L1 ceiling is therefore measurable against its own
    subject, with no second app, no build flag, and nothing to keep in sync.

    Build target: rayclay_bench_opsdash
================================================================================
*/
#ifndef OPSDASH_APP_H
#define OPSDASH_APP_H

#include "bench_app.h"          /* the shared AppCtx / AppInputSink / AppMode contract */
#include "opsdash_backend.h"    /* OpsStore: the fleet + the live telemetry band       */

/* The frozen bench scenario's version. Bump ONLY when the scripted path's rendered
   output changes; the harness emits it in the trend marker "SCENE opsdash vN". */
#define OPSDASH_BENCH_VERSION 1

/* Font ladder, baked from the bundled face. F_MONO is the telemetry readout: it is a
   SIZE, not a face - RayClay ships one family, so a dashboard's "monospace numbers"
   look is carried by the zero-padded fixed-width strings the backend formats, not by
   a second TTF (a second face would break the zero-asset promise). */
typedef enum { F_SMALL = 0, F_BODY, F_MONO, F_MD, F_TITLE, F_COUNT } OpsdashFont;

/* App state - a FLAT, memset-able POD, so opsdash_seed can memset-then-set and the
   harness's run-twice determinism gate holds. */
typedef struct {
    OpsStore store;      /* the backend model, BY VALUE (seed zeroes it) */
    int      selected;   /* focused service, 0..OPS_SVC_COUNT-1 */
    int      group;      /* left-nav filter; -1 = all groups */
    bool     onlyUnhealthy; /* filter toggle: hide OPS_OK services */
    bool     seeded;     /* demo lazy-init guard */
} AppState;

/* The five-function app contract. No RC_App / window handle; the input seam is the
   shared AppInputSink (bench_app.h). */
void opsdash_seed  (AppState *st, unsigned seed);            /* = bench_seed */
void opsdash_update(AppState *st, const AppCtx *ctx);        /* advances ONLY the live band */
void opsdash_layout(AppState *st, const AppCtx *ctx);        /* the FROZEN core UI (both modes) */
void opsdash_demo_chrome(AppState *st, const AppCtx *ctx);   /* demo-only overlay (never in bench) */
void opsdash_bench_step(AppState *st, const AppInputSink *in, int frame); /* bench-only script */

#endif /* OPSDASH_APP_H */
