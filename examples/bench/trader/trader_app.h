/*
================================================================================
    trader_app.h - the trader app CONTRACT (declarations only)
================================================================================

    The single header both drivers include:
      * the DEMO runner (main.c) - a real window, real clock, real input;
      * the BENCH harness (the test/ suite) - includes THIS header for the
        symbols, links trader_app.c as a C object, drives it with a fixed dt + seed
        + synthetic input under the capture seam.

    It declares ONLY types + prototypes (no RayClay DSL, no libc), so it is clean
    to include from C AND C++ - the app is consumed as a linkable object.

    ONE SOURCE, TWO MODES (the benchmark/showcase duality): the pure core
    (trader_seed/update/layout) runs identically in both modes; only the
    clock/seed/input SOURCE differs, and trader_demo_chrome adds a demo-only overlay.
    The app is a stock-trading terminal: a dense watchlist, an instrument detail with
    a big price ticker + a candlestick chart + an order book, an order form + confirm
    dialog, and a positions table. Its dominant cost (B9) is HIGH ELEMENT COUNT (dense
    tickers/grids + ~48 candlestick rects) + gradient/shadow fill + frequent numeric
    relayout - a distinct cost path from messenger (many-small-runs) and notes
    (one-large-wrapped-body).

    Build target: rayclay_bench_trader
================================================================================
*/
#ifndef TRADER_APP_H
#define TRADER_APP_H

#include "bench_app.h"        /* the shared AppCtx / AppInputSink / AppMode contract */
#include "trader_backend.h"   /* TrStore, embedded by value in AppState              */

/* The frozen bench scenario's version. Bump ONLY when the scripted path's rendered
   output changes. the bench harness emits it in the trend marker "SCENE trader vN". */
#define TRADER_BENCH_VERSION 2

/* Font ladder - baked from the bundled face. F_HERO=52 is the big price ticker: it
   exercises the crisp-text-PERSISTS path (a large heading must stay crisp at 2x+ HiDPI
   / zoom and NEVER vanish - owner priority; the atlas overflow HOLDS the last scale). */
typedef enum { F_SMALL = 0, F_BODY, F_MD, F_HEAD, F_TITLE, F_HERO, F_COUNT } TradeFont;

/* App state - a FLAT, memset-able POD blob, so trader_seed can memset-then-set and
   the bench harness's run-twice determinism gate holds. The selected instrument lives in the
   store (tr_select / tr_selected), not here. */
typedef struct {
    TrStore store;           /* the backend, BY VALUE (the seed's memset zeroes it)   */
    int   navTab;            /* nav rail: 0 markets / 1 portfolio (switches the body)  */
    int   watchFilter;       /* watchlist pills: 0 All / 1 Gainers / 2 Losers (REAL)  */
    int   tf;                /* chart timeframe tab 0..3 (reslices the candle window) */
    int   orderSide;         /* TR_BUY / TR_SELL (segmented toggle)                   */
    int   orderType;         /* 0 Market / 1 Limit (combo)                            */
    char  search[24];        /* watchlist search (case-insensitive symbol/name filter)*/
    char  qty[8];            /* order qty input -> pushed to the backend for est cost */
    char  limitPx[12];       /* limit-price input (parsed for est cost + Limit fill)  */
    bool  modalConfirm;      /* the order-confirm dialog                              */
    bool  modalSettings;     /* the settings dialog (mutually exclusive)              */
    bool  darkMode;          /* theme toggle                                          */
    bool  confirmDialogs;    /* settings toggle: gate the confirm dialog on Place order */
    bool  seeded;            /* demo lazy-init guard                                  */
} AppState;

/* The four-function app contract (+ the demo-only chrome). No RC_App / window handle;
   the input seam is the shared AppInputSink (bench_app.h). */
void trader_seed  (AppState *st, unsigned seed);              /* = bench_seed: memset then build */
void trader_update(AppState *st, const AppCtx *ctx);         /* advance market by ctx->dt; push qty */
void trader_layout(AppState *st, const AppCtx *ctx);        /* the FROZEN core UI (both modes) */
void trader_demo_chrome(AppState *st, const AppCtx *ctx);   /* demo-only overlay (never in bench) */
void trader_bench_step(AppState *st, const AppInputSink *in, int frame); /* bench-only script */

#endif /* TRADER_APP_H */
