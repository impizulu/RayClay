/*
================================================================================
    trader_backend.h - the trading-sim app's non-GUI model + logic
================================================================================

    A header-only, raylib-style backend for the RayClay `trader` benchmark/showcase
    app: a seeded market of instruments (each with an OHLC candle series), an order
    book, a portfolio of positions, and a fill log. PURE C99 with ZERO RayClay
    dependency, deterministic under a seed (no wall-clock, no rand(), no file I/O).

    ALL prices are integer CENTS (exact, machine-invariant - no float rounding drift).
    Every DISPLAYED number is formatted by THIS backend into a fixed buffer on the
    deterministic step (priceStr / changeStr / plStr / estCostStr / cashStr / book
    levels), so the pure-RC_ GUI never calls rcFormat in its frozen core: a formatted
    string is identical on every machine at any given frame. The prices walk during
    warmup and PIN at the freeze (a dt <= 0 step is a strict no-op).

    Usage (stb-style single implementation, in exactly one TU):
        #define TRADER_BACKEND_IMPLEMENTATION
        #include "trader_backend.h"

    This header owns tr_memzero so the pure-RC_ GUI TU stays free of <system> includes.

    Build target: rayclay_bench_trader
================================================================================
*/
#ifndef TRADER_BACKEND_H
#define TRADER_BACKEND_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef TRDEF
#define TRDEF static
#endif

#define TR_MAX_INSTRUMENTS 24
#define TR_MAX_CANDLES     48
#define TR_MAX_ORDERS      32
#define TR_MAX_LEVELS       8      /* order-book depth per side          */
#define TR_PRICE_FLOOR    100      /* $1.00 - no walked price may go below (div-by-zero + sign safety) */

typedef enum { TR_BUY = 0, TR_SELL } TrSide;

/* One OHLC candle (cents). */
typedef struct { int32_t open, high, low, close; } TrCandle;

typedef struct {
    char      symbol[8];                 /* "AAPL"                                    */
    char      name[24];                  /* "Apple Inc."                              */
    int32_t   price;                     /* current price, cents                      */
    int32_t   prevClose;                 /* window-open reference for the day change  */
    int32_t   changeBps;                 /* (price-prevClose)*10000/prevClose (bps)   */
    int32_t   position;                  /* shares held (portfolio)                   */
    int32_t   avgCost;                   /* average cost basis, cents (for P/L)       */
    char      priceStr[12];              /* precomputed "142.03" (zero-padded)        */
    char      changeStr[10];             /* precomputed "+1.23%" (signed, zero-padded)*/
    char      plStr[16];                 /* precomputed signed P/L, e.g. "+1240.00"    */
    char      positionStr[12];           /* precomputed share count (signed if short) */
    TrCandle  candles[TR_MAX_CANDLES];
    uint32_t  rng;                       /* per-instrument walk state (seed-time ONLY) */
} TrInstrument;

typedef struct {
    int32_t price, size;                 /* cents, shares                             */
    char    priceStr[12];
    char    sizeStr[10];
} TrLevel;

typedef struct { char line[40]; } TrOrder;   /* precomputed "BUY 100 AAPL @ 142.03"  */

typedef struct {
    TrInstrument inst[TR_MAX_INSTRUMENTS];
    int32_t      count;
    int32_t      selected;               /* open instrument index (-1 = none)         */
    TrLevel      bids[TR_MAX_LEVELS];     /* order book for the SELECTED instrument     */
    TrLevel      asks[TR_MAX_LEVELS];
    TrOrder      orders[TR_MAX_ORDERS];   /* fill log                                   */
    int32_t      orderCount;
    int32_t      cash;                   /* portfolio cash, cents (can go negative)    */
    char         cashStr[16];
    char         estCostStr[16];         /* qty x price for the order form (see tr_set_qty) */
    int32_t      orderPx;                /* limit-price override, cents (0 = use the market)  */
    uint32_t     step;                   /* COUNT-driven live-tick counter (not wall clock) */
    uint32_t     rng;                    /* seed-time xorshift state                    */
} TrStore;

/* -- queries (const, pure, per-frame; always available) ---------------------- */
static inline int                 tr_count(const TrStore *s) { return s->count; }
static inline const TrInstrument *tr_at(const TrStore *s, int i) {
    return (i >= 0 && i < s->count) ? &s->inst[i] : NULL;
}
static inline const TrInstrument *tr_selected(const TrStore *s) { return tr_at(s, s->selected); }
static inline int                 tr_order_count(const TrStore *s) { return s->orderCount; }

/* ========================================================================== */
#ifdef TRADER_BACKEND_IMPLEMENTATION

#include <string.h>

/* The non-inline API is declared + defined only under IMPLEMENTATION, so a TU that
   needs just the types + queries (main.c, the bench harness) never sees a bare
   `static` prototype (-Werror=unused-function). Forward decls first. */
TRDEF void tr_memzero(void *p, size_t n);
TRDEF void tr_store_seed(TrStore *s, unsigned seed);
TRDEF void tr_store_step(TrStore *s, float dt);   /* dt <= 0 => no-op (freeze) */
TRDEF void tr_select(TrStore *s, int i);
TRDEF void tr_set_qty(TrStore *s, int qty);       /* order-form qty -> estCostStr */
TRDEF void tr_set_order_px(TrStore *s, int px);   /* limit-price override, cents (0 = market) */
TRDEF void tr_place_order(TrStore *s, TrSide side, int qty);

TRDEF void tr_memzero(void *p, size_t n) { memset(p, 0, n); }

/* xorshift32 - deterministic, used ONLY at seed time (never on a measured frame). */
static uint32_t tr__rng(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return (*state = x);
}

/* -- manual, locale-free, sign-safe, ZERO-PADDED formatters ------------------ */

/* Right-anchored unsigned integer -> string; returns the count written. */
static int tr__uint_str(uint32_t v, char *out, int cap) {
    char tmp[12]; int n = 0;
    do { tmp[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 11);
    if (n > cap - 1) n = cap - 1;
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = '\0';
    return n;
}

/* Cents -> "D.DD" (or "-D.DD"): sign ONCE then format abs, fraction zero-padded to 2. */
static int tr__fmt_cents(int32_t cents, char *out, int cap) {
    int k = 0;
    if (cents < 0 && k < cap - 1) { out[k++] = '-'; }
    uint32_t a = (uint32_t)(cents < 0 ? -(int64_t)cents : cents);
    uint32_t whole = a / 100u, frac = a % 100u;
    k += tr__uint_str(whole, out + k, cap - k);
    if (k < cap - 1) out[k++] = '.';
    if (k < cap - 1) out[k++] = (char)('0' + (frac / 10u) % 10u);   /* zero-padded tens */
    if (k < cap - 1) out[k++] = (char)('0' + frac % 10u);           /* ones             */
    out[k] = '\0';
    return k;
}

/* Basis points -> "+P.PP%" / "-P.PP%": explicit sign, 2-dp zero-padded percent. */
static int tr__fmt_bps(int32_t bps, char *out, int cap) {
    int k = 0;
    if (k < cap - 1) out[k++] = (bps < 0) ? '-' : '+';
    uint32_t a = (uint32_t)(bps < 0 ? -(int64_t)bps : bps);
    uint32_t whole = a / 100u, frac = a % 100u;
    k += tr__uint_str(whole, out + k, cap - k);
    if (k < cap - 1) out[k++] = '.';
    if (k < cap - 1) out[k++] = (char)('0' + (frac / 10u) % 10u);
    if (k < cap - 1) out[k++] = (char)('0' + frac % 10u);
    if (k < cap - 1) out[k++] = '%';
    out[k] = '\0';
    return k;
}

/* -- the frozen corpus (immutable, ASCII/Latin-1 only) ----------------------- */
static const char *const TR__SYMBOLS[] = {
    "AAPL", "MSFT", "NVDA", "AMZN", "GOOG", "META", "TSLA", "AMD",
    "NFLX", "INTC", "ORCL", "CRM",  "ADBE", "PYPL", "UBER", "SHOP",
    "SQ",   "ARM",  "AVGO", "QCOM", "MU",   "TXN",  "NOW",  "PANW",
};
static const char *const TR__NAMES[] = {
    "Apple Inc.",       "Microsoft Corp.",  "NVIDIA Corp.",     "Amazon.com Inc.",
    "Alphabet Inc.",    "Meta Platforms",   "Tesla Inc.",       "Adv. Micro Dev.",
    "Netflix Inc.",     "Intel Corp.",      "Oracle Corp.",     "Salesforce Inc.",
    "Adobe Inc.",       "PayPal Holdings",  "Uber Techs.",      "Shopify Inc.",
    "Block Inc.",       "Arm Holdings",     "Broadcom Inc.",    "Qualcomm Inc.",
    "Micron Tech.",     "Texas Instr.",     "ServiceNow Inc.",  "Palo Alto Nets.",
};
/* Seeded base prices (cents) - a plausible spread, all well above TR_PRICE_FLOOR. */
static const int32_t TR__BASE[] = {
    18732, 42150, 89640, 17820, 15230, 49810, 24560, 16340,
    58720, 4310,  12690, 27840, 55120, 6580,  7240,  8130,
    7690,  13520, 132400, 17250, 10480, 19870, 78610, 31240,
};

static void tr__fmt_instrument(TrInstrument *it) {
    tr__fmt_cents(it->price, it->priceStr, (int)sizeof it->priceStr);
    /* day change vs the window-open reference; guard prevClose <= 0, int64 intermediate */
    if (it->prevClose > 0) {
        int64_t num = (int64_t)(it->price - it->prevClose) * 10000;
        it->changeBps = (int32_t)(num / it->prevClose);
    } else {
        it->changeBps = 0;
    }
    tr__fmt_bps(it->changeBps, it->changeStr, (int)sizeof it->changeStr);
    /* unrealized P/L = position * (price - avgCost), cents (signed). */
    int64_t pl = (int64_t)it->position * (it->price - it->avgCost);
    if (pl > 2000000000LL)  pl = 2000000000LL;      /* clamp the display buffer, never overflow */
    if (pl < -2000000000LL) pl = -2000000000LL;
    tr__fmt_cents((int32_t)pl, it->plStr, (int)sizeof it->plStr);
    /* share count (signed: a short position shows a leading '-') */
    int pk = 0;
    if (it->position < 0 && pk < (int)sizeof it->positionStr - 1)
        it->positionStr[pk++] = '-';
    uint32_t shares = (uint32_t)(it->position < 0 ? -(int64_t)it->position : it->position);
    tr__uint_str(shares, it->positionStr + pk, (int)sizeof it->positionStr - pk);
}

/* Rebuild the order book for the SELECTED instrument, centered on its price. Deterministic
   (a fixed ladder), and rebuilt on seed / select / tick so it never crosses the price. */
static void tr__rebuild_book(TrStore *s) {
    const TrInstrument *it = tr_selected(s);
    if (!it) {
        tr_memzero(s->bids, sizeof s->bids);
        tr_memzero(s->asks, sizeof s->asks);
        return;
    }
    int32_t tickSz = it->price / 500 + 1;            /* a price-proportional level step, >= 1 */
    for (int i = 0; i < TR_MAX_LEVELS; i++) {
        int32_t bidP = it->price - (int32_t)(i + 1) * tickSz;
        int32_t askP = it->price + (int32_t)(i + 1) * tickSz;
        if (bidP < TR_PRICE_FLOOR) bidP = TR_PRICE_FLOOR;
        s->bids[i].price = bidP;
        s->asks[i].price = askP;
        s->bids[i].size  = 40 + ((i * 37 + 11) % 60) * 5;   /* a fixed, deterministic depth  */
        s->asks[i].size  = 40 + ((i * 29 + 23) % 60) * 5;
        tr__fmt_cents(bidP, s->bids[i].priceStr, (int)sizeof s->bids[i].priceStr);
        tr__fmt_cents(askP, s->asks[i].priceStr, (int)sizeof s->asks[i].priceStr);
        tr__uint_str((uint32_t)s->bids[i].size, s->bids[i].sizeStr, (int)sizeof s->bids[i].sizeStr);
        tr__uint_str((uint32_t)s->asks[i].size, s->asks[i].sizeStr, (int)sizeof s->asks[i].sizeStr);
    }
}

TRDEF void tr_store_seed(TrStore *s, unsigned seed) {
    tr_memzero(s, sizeof *s);
    s->rng      = seed ? seed : 0x7a11eD00u;
    s->selected = 0;
    s->cash     = 100000000;                          /* $1,000,000 starting cash (a demo buy stays positive) */

    const int n = (int)(sizeof TR__SYMBOLS / sizeof *TR__SYMBOLS);
    s->count = (n < TR_MAX_INSTRUMENTS) ? n : TR_MAX_INSTRUMENTS;

    for (int i = 0; i < s->count; i++) {
        TrInstrument *it = &s->inst[i];
        size_t sl = strlen(TR__SYMBOLS[i]);
        if (sl >= sizeof it->symbol) sl = sizeof it->symbol - 1;
        memcpy(it->symbol, TR__SYMBOLS[i], sl); it->symbol[sl] = '\0';
        size_t nl = strlen(TR__NAMES[i]);
        if (nl >= sizeof it->name) nl = sizeof it->name - 1;
        memcpy(it->name, TR__NAMES[i], nl); it->name[nl] = '\0';

        it->rng = (uint32_t)(seed ^ (0x9e3779b9u * (uint32_t)(i + 1)));
        if (!it->rng) it->rng = 0x1234567u;

        /* Walk a 48-candle OHLC series from the base price, floored so no value <= 0. */
        int32_t px = TR__BASE[i];
        for (int c = 0; c < TR_MAX_CANDLES; c++) {
            TrCandle *cd = &it->candles[c];
            cd->open = px;
            int32_t span = px / 40 + 2;               /* volatility ~2.5% of price */
            int32_t drift = (int32_t)(tr__rng(&it->rng) % (uint32_t)(2 * span + 1)) - span;
            int32_t close = px + drift;
            if (close < TR_PRICE_FLOOR) close = TR_PRICE_FLOOR;
            int32_t wick = (int32_t)(tr__rng(&it->rng) % (uint32_t)(span + 1));
            int32_t hi = (cd->open > close) ? cd->open : close;
            int32_t lo = (cd->open > close) ? close : cd->open;
            cd->close = close;
            cd->high  = hi + wick;
            cd->low   = lo - wick;
            if (cd->low < TR_PRICE_FLOOR) cd->low = TR_PRICE_FLOOR;
            if (cd->high < cd->low) cd->high = cd->low;
            px = close;
        }
        it->price     = it->candles[TR_MAX_CANDLES - 1].close;
        it->prevClose = it->candles[0].open;          /* window-open reference (bigger, meaningful % ) */

        /* Seed a handful of open positions (a portfolio) with an avg cost near the base. */
        if (i % 5 == 0) {
            it->position = (int32_t)(10 + (tr__rng(&it->rng) % 40u) * 5u);
            it->avgCost  = TR__BASE[i] - TR__BASE[i] / 20 + (int32_t)(tr__rng(&it->rng) % 400u);
            if (it->avgCost < TR_PRICE_FLOOR) it->avgCost = TR_PRICE_FLOOR;
        }
        tr__fmt_instrument(it);
    }

    tr__fmt_cents(s->cash, s->cashStr, (int)sizeof s->cashStr);
    tr__fmt_cents(0, s->estCostStr, (int)sizeof s->estCostStr);
    tr__rebuild_book(s);
}

/* A fixed, balanced live-tick delta table (cents), indexed by the step counter so the
   nudge is COUNT-driven (machine-invariant), never a float-accumulated wall second. */
static const int32_t TR__DELTAS[] = { 6, -4, 3, -7, 9, -5, 2, -3, 8, -6, 4, -8, 5, -2, 7, -9 };

TRDEF void tr_store_step(TrStore *s, float dt) {
    if (dt <= 0.0f)                                   /* the freeze: a strict no-op */
        return;
    s->step++;
    TrInstrument *it = (s->selected >= 0 && s->selected < s->count) ? &s->inst[s->selected] : NULL;
    if (it) {
        int32_t d = TR__DELTAS[s->step % (uint32_t)(sizeof TR__DELTAS / sizeof *TR__DELTAS)];
        it->price += d;
        if (it->price < TR_PRICE_FLOOR) it->price = TR_PRICE_FLOOR;
        tr__fmt_instrument(it);
        tr__rebuild_book(s);                          /* keep the book uncrossed around the new price */
    }
}

TRDEF void tr_select(TrStore *s, int i) {
    if (i < 0 || i >= s->count)
        return;
    s->selected = i;
    tr__rebuild_book(s);
}

TRDEF void tr_set_order_px(TrStore *s, int px) {
    s->orderPx = px > 0 ? px : 0;                     /* 0 => fall back to the market price */
}

TRDEF void tr_set_qty(TrStore *s, int qty) {
    const TrInstrument *it = tr_selected(s);
    if (qty < 0) qty = 0;
    /* a limit order prices the estimate at the user's price; else the live market */
    int32_t px = s->orderPx > 0 ? s->orderPx : (it ? it->price : 0);
    int64_t cost = (int64_t)qty * px;
    if (cost > 2000000000LL) cost = 2000000000LL;     /* clamp the display buffer */
    tr__fmt_cents((int32_t)cost, s->estCostStr, (int)sizeof s->estCostStr);
}

TRDEF void tr_place_order(TrStore *s, TrSide side, int qty) {
    TrInstrument *it = (s->selected >= 0 && s->selected < s->count) ? &s->inst[s->selected] : NULL;
    if (!it || qty <= 0 || s->orderCount >= TR_MAX_ORDERS)
        return;

    /* a limit order fills at the user's price; a market order at the live price */
    int32_t fillPx = s->orderPx > 0 ? s->orderPx : it->price;
    /* update the position's average cost (buys) + cash; sells reduce the position.
       int64 notional + a clamp keeps a large user quantity from overflowing int32. */
    int64_t notional = (int64_t)qty * fillPx;
    if (side == TR_BUY) {
        int64_t newQty  = (int64_t)it->position + qty;
        int64_t newCost = (int64_t)it->position * it->avgCost + notional;
        it->position = (int32_t)newQty;
        it->avgCost  = (newQty > 0) ? (int32_t)(newCost / newQty) : it->price;
    } else {
        it->position -= qty;
    }
    int64_t cash = (int64_t)s->cash + (side == TR_BUY ? -notional : notional);
    if (cash >  2000000000LL) cash =  2000000000LL;
    if (cash < -2000000000LL) cash = -2000000000LL;
    s->cash = (int32_t)cash;
    tr__fmt_cents(s->cash, s->cashStr, (int)sizeof s->cashStr);
    tr__fmt_instrument(it);

    /* record a precomputed fill line "BUY 100 AAPL @ 142.03" */
    TrOrder *o = &s->orders[s->orderCount++];
    char *dst = o->line; int cap = (int)sizeof o->line, k = 0;
    const char *verb = (side == TR_BUY) ? "BUY " : "SELL ";
    for (int j = 0; verb[j] && k < cap - 1; j++) dst[k++] = verb[j];
    k += tr__uint_str((uint32_t)qty, dst + k, cap - k);
    if (k < cap - 1) dst[k++] = ' ';
    for (int j = 0; it->symbol[j] && k < cap - 1; j++) dst[k++] = it->symbol[j];
    const char *at = " @ ";
    for (int j = 0; at[j] && k < cap - 1; j++) dst[k++] = at[j];
    k += tr__fmt_cents(fillPx, dst + k, cap - k);
    dst[k] = '\0';
}

#endif /* TRADER_BACKEND_IMPLEMENTATION */
#endif /* TRADER_BACKEND_H */
