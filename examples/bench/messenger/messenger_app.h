/*
================================================================================
    messenger_app.h - the messenger app CONTRACT (declarations only)
================================================================================

    The single header both drivers include:
      * the DEMO runner (main.c) - opens a real window, real clock, real input;
      * the BENCH harness (the test/ suite) - includes THIS header for the
        symbols, links messenger_app.c compiled as a C object, and drives it with
        a fixed dt + seed + synthetic input under the capture seam.

    It declares ONLY types + prototypes (no RayClay DSL, no libc), so it is clean
    to include from C AND C++ - the app is consumed as a linkable object, never as
    a header dumped into a C++ TU, which keeps the DSL-heavy messenger_app.c always
    compiled as C.

    ONE SOURCE, TWO MODES (the benchmark/showcase duality): the pure core
    (messenger_seed/update/layout) runs identically in both modes; only the
    clock/seed/input SOURCE differs, and messenger_demo_chrome adds a demo-only
    overlay. See messenger_app.c.

    Build target: rayclay_bench_messenger
================================================================================
*/
#ifndef MESSENGER_APP_H
#define MESSENGER_APP_H

#include "bench_app.h"        /* the shared AppCtx / AppInputSink / AppMode contract */
#include "messenger_backend.h" /* MsgStore, embedded by value in AppState  */

/* The frozen bench scenario's version. Bump ONLY when the scripted path's
   rendered output changes (seed/fixtures/script/frozen layout) - never for
   demo-only chrome. the bench harness emits it in the trend marker "SCENE messenger vN". */
#define MESSENGER_BENCH_VERSION 2

/* Font ladder - baked from the bundled face; the index set is shared by the GUI
   (messenger_app.c) and the demo runner (main.c), so they never desync. */
typedef enum { F_SMALL = 0, F_BODY, F_HEAD, F_TITLE, F_COUNT } MsgFont;

/* App state - a FLAT, memset-able POD blob (no pointers into transient memory), so
   messenger_seed can memset-then-set and the bench harness's run-twice determinism gate
   holds. If a field ever becomes a transient pointer, that invariant breaks. */
typedef struct {
    MsgStore store;         /* the backend, BY VALUE (the seed's memset zeroes it) */
    uint64_t tick;          /* app frame counter; app-level UI timers key off it   */
    int      openConv;      /* the open conversation index                         */
    char     composer[256]; /* the rcTextInput composer buffer (ASCII)            */
    char     search[64];    /* the sidebar search buffer                           */
    bool     modalAttach;   /* the attach / emoji picker modal (rcBeginModal open) */
    bool     modalSettings; /* the settings modal (mutually exclusive with attach) */
    bool     infoOpen;      /* the right-hand info drawer (a contact's profile)    */
    bool     sidebarCollapsed; /* nav-rail toggle: hide the conversation sidebar    */
    bool     darkMode;      /* theme toggle (showcase chrome)                      */
    bool     readReceipts;  /* settings toggle (showcase)                          */
    bool     attachOriginal;/* attach modal "original quality" checkbox (showcase) */
    float    notifVolume;   /* settings slider (showcase)                          */
    int      statusCombo;   /* settings dropdown (showcase)                        */
    bool     seeded;        /* demo lazy-init guard (seed once the renderer is up) */
} AppState;

/* The four-function app contract (+ the demo-only chrome). No RC_App / window handle;
   the input seam is the shared AppInputSink (bench_app.h). */
void messenger_seed  (AppState *st, unsigned seed);              /* = bench_seed: memset then build */
void messenger_update(AppState *st, const AppCtx *ctx);         /* advance backend + UI timers by ctx->dt */
void messenger_layout(AppState *st, const AppCtx *ctx);        /* the FROZEN core UI (both modes) */
void messenger_demo_chrome(AppState *st, const AppCtx *ctx);   /* demo-only overlay (never in bench) */
void messenger_bench_step(AppState *st, const AppInputSink *in, int frame); /* bench-only script */

#endif /* MESSENGER_APP_H */
