/*
================================================================================
    trader_app.c - the trader app's pure-RC_ GUI + bench hooks
================================================================================

    The single implementation TU. It defines the four-function app contract
    (seed / update / layout / bench_step) + a demo-only chrome overlay, and it
    carries the header-only backend implementation (TRADER_BACKEND_IMPLEMENTATION).

    PURE RC_ API - RayClay types only, no layout-engine call, no <system>
    include (any raw-memory need is routed through trader_backend.h). The FROZEN
    core (seed/update/layout, incl. the modals) calls ZERO rcFormat: EVERY number
    is a backend fixed buffer (priceStr/changeStr/plStr/positionStr/cashStr/estCostStr/
    book levels) and every summary is assembled from those buffers + literals - so the
    arena-less bench core never dereferences a NULL arena. rcFormat is used ONLY in
    trader_demo_chrome (guarded by ctx->arena).

    The candlestick chart is drawn with pure-RC_ rcBox rects (no RC_CustomDrawData):
    each candle is a vertical Column of five stacked, integer-px-height segments
    (top gap / upper wick / body / lower wick / bottom gap) mapped from the chart-wide
    cent range - deterministic + machine-invariant.

    Build target: rayclay_bench_trader
================================================================================
*/
#define TRADER_BACKEND_IMPLEMENTATION
#include "trader_app.h"

/* Nav-rail glyphs from the shared example icon set (as the messenger/notes do). */
#include "icons/rc_icons_panel_left.h"
#include "icons/rc_icons_expand.h"
#include "icons/rc_icons_settings.h"

/* The frozen bench scenario length: warmup frames, then HOLD. Retune this and the
   click coordinates to its measurement budget when it wires the app. */
#define TRADER_BENCH_WARMUP 64

/* The instrument the bench selects + freezes on (a dramatic-chart ticker). Selected
   BEFORE any watchlist scroll (like notes) so the frozen scene is scroll-independent. */
#define TRADER_BENCH_PICK 2      /* NVDA */

#define TR_UP   0x10b981         /* gain: green */
#define TR_DOWN 0xef4444         /* loss: red   */

/* Stable ids for the watchlist rows (the layout engine needs a stable id per interactive element). */
static const char *const WATCH_IDS[TR_MAX_INSTRUMENTS] = {
    "wr00", "wr01", "wr02", "wr03", "wr04", "wr05", "wr06", "wr07",
    "wr08", "wr09", "wr10", "wr11", "wr12", "wr13", "wr14", "wr15",
    "wr16", "wr17", "wr18", "wr19", "wr20", "wr21", "wr22", "wr23",
};

static const char *const ORDER_TYPES[] = { "Market", "Limit" };
static const char *const WATCH_VIEWS[] = { "All", "Gainers", "Losers" };

/* ── small helpers ───────────────────────────────────────────────────────── */

static RC_Color tr_updown(int64_t v) { return rcHex((uint32_t)(v >= 0 ? TR_UP : TR_DOWN)); }

/* Parse the order-form qty buffer to an int (pure C; no libc atoi in the .c). */
static int qty_to_int(const char *s) {
    int v = 0;
    for (int i = 0; s[i] && i < 9; i++) {
        if (s[i] < '0' || s[i] > '9')
            break;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

/* Parse a "142.50"-style price buffer to integer cents (pure C; the backend keeps
   cents). Up to two fractional digits are read; anything non-numeric yields 0, which
   the backend reads as "use the market price". */
static int px_to_cents(const char *s) {
    int64_t whole = 0;
    int i = 0;
    for (; s[i] && s[i] != '.' && i < 9; i++) {
        if (s[i] < '0' || s[i] > '9')
            return 0;
        whole = whole * 10 + (s[i] - '0');
    }
    int frac = 0;
    if (s[i] == '.') {
        i++;
        if (s[i] >= '0' && s[i] <= '9') frac += (s[i++] - '0') * 10;
        if (s[i] >= '0' && s[i] <= '9') frac +=  s[i]   - '0';
    }
    int64_t cents = whole * 100 + frac;
    if (cents > 2000000000LL) cents = 2000000000LL;   /* clamp like the backend buffers */
    return (int)cents;
}

/* Lowercase one ASCII byte (the instrument corpus is ASCII/Latin-1). */
static char tr_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

/* Case-insensitive substring test: does `hay` contain `needle`? An empty needle
   matches everything (an empty search box filters nothing). Pure C, no libc. */
static bool tr_ci_contains(const char *hay, const char *needle) {
    if (!needle[0])
        return true;
    for (int i = 0; hay[i]; i++) {
        int j = 0;
        while (needle[j] && tr_lower(hay[i + j]) == tr_lower(needle[j]))
            j++;
        if (!needle[j])
            return true;
    }
    return false;
}

/* A nav-rail icon button; a hand-rolled Box button opts into the pointer cursor. */
static bool nav_button(const char *id, RC_IconCallback icon, bool active) {
    RC_Style s = rcGetStyle();
    rcBox(.id = id, .w = "44px", .h = "44px", .align = "cc",
           .bg = active ? s.surfaceAlt : (rcIsHovered(id) ? s.surface : RC_TRANSPARENT),
           .borderRadius = "all-lg") {
        icon(20.0f, active ? s.primary : s.textMuted);
    }
    return rcClicked(id);
}

/* A Buy/Sell segmented-toggle button (Buy tints green, Sell red when active). */
static bool seg_button(const char *id, const char *label, bool active, uint32_t tint) {
    RC_Style s = rcGetStyle();
    rcBox(.id = id, .w = "grow", .h = "34px", .align = "cc", .borderRadius = "all-md",
           .bg = active ? rcHex(tint) : (rcIsHovered(id) ? s.surfaceAlt : s.surface)) {
        rcTextC(label, .font = F_BODY, .color = active ? RC_WHITE : s.textMuted);
    }
    return rcClicked(id);
}

/* A chart-timeframe tab (1D/1W/1M/1Y): reslices the candle window (below), active underline. */
static bool tf_tab(const char *id, const char *label, bool active) {
    RC_Style s = rcGetStyle();
    rcColumn(.id = id, .px = 8, .py = 4, .gap = 4, .align = "cc") {
        rcTextC(label, .font = F_SMALL, .color = active ? s.text : s.textMuted);
        rcBox(.w = "18px", .h = "2px",
               .bg = active ? s.primary : RC_TRANSPARENT, .borderRadius = "all-full") {}
    }
    return rcClicked(id);
}

/* A watchlist filter pill (All / Gainers / Losers). */
static bool watch_pill(const char *id, const char *label, bool active) {
    RC_Style s = rcGetStyle();
    rcBox(.id = id, .px = 10, .py = 5, .align = "cc", .borderRadius = "all-full",
           .bg = active ? s.primary : (rcIsHovered(id) ? s.surfaceAlt : RC_TRANSPARENT)) {
        rcTextC(label, .font = F_SMALL, .color = active ? RC_WHITE : s.textMuted);
    }
    return rcClicked(id);
}

/* A watchlist row: symbol + name + price + change% (colored). The whole row is a
   click target (rcClicked turns the styled row into a button). */
static bool watch_row(const char *id, const TrInstrument *it, bool active) {
    RC_Style s = rcGetStyle();
    rcRow(.id = id, .w = "grow", .h = "52px", .px = 10, .gap = 8, .align = "cl",
           .bg = active ? s.surfaceAlt : (rcIsHovered(id) ? s.surface : RC_TRANSPARENT),
           .borderRadius = "all-md") {
        rcColumn(.w = "grow", .gap = 2) {
            rcTextC(it->symbol, .font = F_BODY, .color = s.text);
            rcBox(.w = "grow", .overflow = "hidden") {
                rcTextC(it->name, .font = F_SMALL, .color = s.textMuted, .wrap = "n");
            }
        }
        rcColumn(.gap = 2, .align = "tr") {
            rcTextC(it->priceStr, .font = F_BODY, .color = s.text);
            rcTextC(it->changeStr, .font = F_SMALL, .color = tr_updown(it->changeBps));
        }
    }
    return rcClicked(id);
}

/* One order-book level: price (colored by side) + size + a proportional depth bar.
   `barPx` is precomputed against a >=1 max size (div-by-zero guarded by the caller). */
static void book_level(const TrLevel *lv, bool bid, int barPx) {
    RC_Style s = rcGetStyle();
    rcRow(.w = "grow", .h = "20px", .px = 8, .gap = 8, .align = "cl") {
        rcBox(.w = "72px") {
            rcTextC(lv->priceStr, .font = F_SMALL, .color = rcHex((uint32_t)(bid ? TR_UP : TR_DOWN)));
        }
        rcTextC(lv->sizeStr, .font = F_SMALL, .color = s.textMuted);
        rcBox(.w = "grow") {}
        rcBox(.wType = RC_PX(barPx), .h = "8px", .borderRadius = "all-sm",
               .bg = rcAlpha(rcHex((uint32_t)(bid ? TR_UP : TR_DOWN)), 90)) {}
    }
}

/* The candlestick chart: the trailing `count` candles (the active timeframe reslices
   the window), each a vertical Column of five stacked, integer-px-height segments mapped
   from the SHOWN window's cent range. Pure RC_ rects, deterministic (integer math,
   range guarded >= 1). Fewer candles simply grow wider (a natural short-window zoom). */
static void candle_chart(const TrInstrument *it, int count) {
    if (count > TR_MAX_CANDLES) count = TR_MAX_CANDLES;
    if (count < 1)             count = 1;
    int start = TR_MAX_CANDLES - count;               /* show the trailing `count` candles */
    int32_t lo = it->candles[start].low, hi = it->candles[start].high;
    for (int c = start + 1; c < TR_MAX_CANDLES; c++) {
        if (it->candles[c].low  < lo) lo = it->candles[c].low;
        if (it->candles[c].high > hi) hi = it->candles[c].high;
    }
    int32_t range = hi - lo;
    if (range < 1) range = 1;                         /* flat-series div-by-zero guard */
    const int chart_h = 220;                          /* chart pixel height */

    rcRow(.w = "grow", .hType = RC_PX(chart_h), .gap = 2) {
        for (int c = start; c < TR_MAX_CANDLES; c++) {
            const TrCandle *cd = &it->candles[c];
            bool up = (cd->close >= cd->open);
            int32_t bodyTop = up ? cd->close : cd->open;   /* the higher price */
            int32_t bodyBot = up ? cd->open  : cd->close;
            int topGap = (int)((int64_t)(hi - cd->high)      * chart_h / range);
            int upWick = (int)((int64_t)(cd->high - bodyTop) * chart_h / range);
            int body   = (int)((int64_t)(bodyTop - bodyBot)  * chart_h / range);
            int loWick = (int)((int64_t)(bodyBot - cd->low)  * chart_h / range);
            if (body < 1) body = 1;                          /* a doji still shows a line */
            int botGap = chart_h - topGap - upWick - body - loWick;   /* absorbs the clamp + rounding */
            if (botGap < 0) botGap = 0;
            RC_Color col = rcHex((uint32_t)(up ? TR_UP : TR_DOWN));
            rcColumn(.w = "grow", .hType = RC_PX(chart_h), .align = "tc") {
                if (topGap > 0) rcBox(.hType = RC_PX(topGap)) {}
                if (upWick > 0) rcBox(.w = "2px", .hType = RC_PX(upWick), .bg = col) {}
                rcBox(.w = "grow", .hType = RC_PX(body), .bg = col) {}
                if (loWick > 0) rcBox(.w = "2px", .hType = RC_PX(loWick), .bg = col) {}
                if (botGap > 0) rcBox(.hType = RC_PX(botGap)) {}
            }
        }
    }
}

/* ── regions ─────────────────────────────────────────────────────────────── */

/* The custom titlebar: brand + a gradient market-status pill + the bundled window
   controls. RC_ID_WINDOW_DRAG makes the band draggable, so it carries NO app widget
   (a toggle here loses its click to the OS window-move); the theme toggle lives in
   Settings. Only the RC_ID_WINDOW_* controls are drag-exempt. */
static void trader_topbar(void) {
    RC_Style s = rcGetStyle();
    rcRow(.id = RC_ID_WINDOW_DRAG, .w = "grow", .h = "52px", .bg = s.chrome,
           .px = 14, .gap = 10, .align = "cl") {
        rcBox(.w = "26px", .h = "26px", .align = "cc",
               .bg = s.primary, .borderRadius = "all-md") {
            rcTextL("RC", .font = F_SMALL, .color = RC_WHITE);
        }
        rcTextL("RayClay Markets", .font = F_HEAD, .color = s.text);
        /* the market-status pill: a gradient fill REQUIRES a stable .id */
        rcBox(.id = "mkt_pill", .px = 10, .py = 3, .align = "cc", .borderRadius = "all-full",
               .gradient = { .from = rcHex(TR_UP), .to = rcHex(0x059669), .dir = "h" }) {
            rcTextL("MARKET OPEN", .font = F_SMALL, .color = RC_WHITE);
        }
        rcBox(.w = "grow") {}
        rcWindowControls();
    }
}

static void trader_navrail(AppState *st) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "64px", .h = "grow", .bg = s.surface,
              .py = 12, .gap = 6, .align = "tc") {
        if (nav_button("nav_markets",   rcIconPanelLeft, st->navTab == 0)) st->navTab = 0;
        if (nav_button("nav_portfolio", rcIconExpand,    st->navTab == 1)) st->navTab = 1;
        rcBox(.w = "grow", .h = "grow") {}
        if (nav_button("nav_settings", rcIconSettings, false)) {
            st->modalSettings = true;
            st->modalConfirm  = false;
        }
    }
}

static void trader_watchlist(AppState *st) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "300px", .h = "grow", .bg = s.surface, .p = 10, .gap = 8) {
        rcTextL("Markets", .font = F_TITLE, .color = s.text);
        rcTextInput("search", st->search, sizeof st->search, .placeholder = "Search symbol");
        rcRow(.w = "grow", .gap = 6) {
            if (watch_pill("wf_all",  WATCH_VIEWS[0], st->watchFilter == 0)) st->watchFilter = 0;
            if (watch_pill("wf_gain", WATCH_VIEWS[1], st->watchFilter == 1)) st->watchFilter = 1;
            if (watch_pill("wf_lose", WATCH_VIEWS[2], st->watchFilter == 2)) st->watchFilter = 2;
        }
        rcColumn(.id = "WatchScroll", .w = "grow", .h = "grow", .scroll = "v", .gap = 3, .pr = 12) {
            int n = tr_count(&st->store);
            for (int i = 0; i < n; i++) {
                const TrInstrument *it = tr_at(&st->store, i);
                if (!it)                    /* i < tr_count() so never NULL; guard silences -fanalyzer */
                    continue;
                /* filter by change sign + the search box, but ALWAYS keep the
                   selected row visible (its detail/order panels are what's open) */
                if (i != st->store.selected) {
                    if (st->watchFilter == 1 && it->changeBps <  0) continue;
                    if (st->watchFilter == 2 && it->changeBps >= 0) continue;
                    if (!tr_ci_contains(it->symbol, st->search) &&
                        !tr_ci_contains(it->name,   st->search)) continue;
                }
                if (watch_row(WATCH_IDS[i], it, i == st->store.selected))
                    tr_select(&st->store, i);
            }
        }
    }
}

static void trader_detail(AppState *st) {
    RC_Style s = rcGetStyle();
    const TrInstrument *it = tr_selected(&st->store);
    if (!it)
        return;                         /* no selection: guard BEFORE opening the element
                                           (a return inside would skip the close) */
    rcColumn(.w = "grow", .h = "grow", .bg = s.background, .p = 16, .gap = 12) {
        /* header: symbol + name + a "..." actions menu (each item is a real rcMenuItem) */
        rcRow(.w = "grow", .align = "cl", .gap = 10) {
            rcTextC(it->symbol, .font = F_HEAD, .color = s.text);
            rcTextC(it->name, .font = F_SMALL, .color = s.textMuted);
            rcBox(.w = "grow") {}
            if (rcBeginMenu("menu_actions", "...")) {
                rcMenuItem("Add to watchlist");
                rcMenuItem("Set price alert");
                rcMenuItem("View fundamentals");
                rcEndMenu();
            }
        }
        /* the HERO price (F_HERO = 52px, the crisp-text-persists heading) + change */
        rcRow(.align = "bl", .gap = 12) {
            rcTextC(it->priceStr, .font = F_HERO, .color = s.text);
            rcTextC(it->changeStr, .font = F_MD, .color = tr_updown(it->changeBps));
        }
        /* timeframe tabs: each selects how many trailing candles the chart shows */
        rcRow(.gap = 2, .align = "cl") {
            static const char *const TFS[] = { "1D", "1W", "1M", "1Y" };
            static const char *const TF_IDS[] = { "tf0", "tf1", "tf2", "tf3" };
            for (int i = 0; i < 4; i++)
                if (tf_tab(TF_IDS[i], TFS[i], st->tf == i)) st->tf = i;
        }
        /* the chart card: a gradient + shadow REQUIRE a stable .id (the gradient anchors
           the shadow). Themed so it reads right in Light mode too (a hardcoded navy looks
           wrong on white); the candle colours keep their own green/red semantics. */
        static const int TF_CANDLES[] = { 12, 24, 36, 48 };   /* 1D/1W/1M/1Y trailing window */
        RC_Color cardFrom = st->darkMode ? rcHex(0x1b2433) : s.surface;
        RC_Color cardTo   = st->darkMode ? rcHex(0x0e1420) : s.surfaceAlt;
        rcBox(.id = "chart_card", .w = "grow", .p = 12, .borderRadius = "all-lg",
               .gradient = { .from = cardFrom, .to = cardTo, .dir = "v" },
               .shadow = { .color = rcAlpha(RC_BLACK, st->darkMode ? 90 : 30),
                           .y = 6.0f, .blur = 20.0f }) {
            candle_chart(it, TF_CANDLES[st->tf]);
        }
        /* the order book: asks (red, high->low) then bids (green) - dense numeric grid */
        rcTextL("Order book", .font = F_SMALL, .color = s.textMuted);
        rcColumn(.w = "grow", .gap = 1) {
            int maxSz = 1;
            for (int i = 0; i < TR_MAX_LEVELS; i++) {
                if (st->store.asks[i].size > maxSz) maxSz = st->store.asks[i].size;
                if (st->store.bids[i].size > maxSz) maxSz = st->store.bids[i].size;
            }
            for (int i = TR_MAX_LEVELS - 1; i >= 0; i--)
                book_level(&st->store.asks[i], false, (int)((int64_t)st->store.asks[i].size * 60 / maxSz) + 1);
            for (int i = 0; i < TR_MAX_LEVELS; i++)
                book_level(&st->store.bids[i], true, (int)((int64_t)st->store.bids[i].size * 60 / maxSz) + 1);
        }
    }
}

static void trader_orderpanel(AppState *st) {
    RC_Style s = rcGetStyle();
    const TrInstrument *it = tr_selected(&st->store);
    rcColumn(.w = "320px", .h = "grow", .bg = s.surface, .p = 14, .gap = 10) {
        rcTextL("Order", .font = F_TITLE, .color = s.text);
        rcRow(.w = "grow", .gap = 6) {
            if (seg_button("seg_buy",  "Buy",  st->orderSide == TR_BUY,  TR_UP))   st->orderSide = TR_BUY;
            if (seg_button("seg_sell", "Sell", st->orderSide == TR_SELL, TR_DOWN)) st->orderSide = TR_SELL;
        }
        rcColumn(.w = "grow", .gap = 4) {
            rcTextL("Quantity", .font = F_SMALL, .color = s.textMuted);
            rcTextInput("qty_in", st->qty, sizeof st->qty, .placeholder = "0");
        }
        rcColumn(.w = "grow", .gap = 4) {
            rcTextL("Order type", .font = F_SMALL, .color = s.textMuted);
            rcCombo("cb_ordertype", &st->orderType, ORDER_TYPES, 2);
        }
        if (st->orderType == 1) {
            rcColumn(.w = "grow", .gap = 4) {
                rcTextL("Limit price", .font = F_SMALL, .color = s.textMuted);
                rcTextInput("limit_in", st->limitPx, sizeof st->limitPx, .placeholder = "0.00");
            }
        }
        rcRow(.w = "grow", .align = "cl") {
            rcTextL("Est. cost", .font = F_SMALL, .color = s.textMuted);
            rcBox(.w = "grow") {}
            rcTextC(st->store.estCostStr, .font = F_BODY, .color = s.text);
        }
        if (rcButton("btn_place", "Place order", RC_BTN_PRIMARY)) {
            /* the Settings "Confirm dialogs" toggle gates the confirm step (else fill now) */
            if (st->confirmDialogs) {
                st->modalConfirm  = true;
                st->modalSettings = false;
            } else {
                tr_place_order(&st->store, (TrSide)st->orderSide, qty_to_int(st->qty));
            }
        }
        rcBox(.w = "grow", .h = "1px", .bg = s.border) {}
        /* positions table (all held instruments; P/L colored by the P/L sign) */
        rcRow(.w = "grow", .align = "cl") {
            rcTextL("Positions", .font = F_SMALL, .color = s.textMuted);
            rcBox(.w = "grow") {}
            rcTextC(st->store.cashStr, .font = F_SMALL, .color = s.text);
        }
        rcColumn(.id = "PosScroll", .w = "grow", .h = "grow", .scroll = "v", .gap = 4, .pr = 12) {
            int n = tr_count(&st->store);
            for (int i = 0; i < n; i++) {
                const TrInstrument *p = tr_at(&st->store, i);
                if (!p || p->position == 0)   /* !p: i < tr_count() so never NULL; guard silences -fanalyzer */
                    continue;
                int64_t pl = (int64_t)p->position * (p->price - p->avgCost);
                rcRow(.w = "grow", .h = "34px", .px = 8, .align = "cl", .gap = 8,
                       .bg = s.surfaceAlt, .borderRadius = "all-md") {
                    rcBox(.w = "60px") { rcTextC(p->symbol, .font = F_SMALL, .color = s.text); }
                    rcTextC(p->priceStr, .font = F_SMALL, .color = s.textMuted);
                    rcBox(.w = "grow") {}
                    rcTextC(p->plStr, .font = F_SMALL, .color = tr_updown(pl));
                }
            }
        }
    }
    (void)it;
}

/* The Portfolio view (nav tab 1): a full-width holdings table - every open position
   with its share count, live price and unrealised P/L, plus portfolio cash. Every
   number is a precomputed backend buffer, so the frozen core still calls no rcFormat. */
static void trader_portfolio(AppState *st) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "grow", .h = "grow", .bg = s.background, .p = 16, .gap = 12) {
        rcRow(.w = "grow", .align = "cl", .gap = 10) {
            rcTextL("Portfolio", .font = F_TITLE, .color = s.text);
            rcBox(.w = "grow") {}
            rcTextL("Cash", .font = F_SMALL, .color = s.textMuted);
            rcTextC(st->store.cashStr, .font = F_BODY, .color = s.text);
        }
        /* column headings (align the numeric ones right, matching the rows below) */
        rcRow(.w = "grow", .px = 10, .gap = 10, .align = "cl") {
            rcBox(.w = "70px") { rcTextL("Symbol", .font = F_SMALL, .color = s.textMuted); }
            rcBox(.w = "grow")        { rcTextL("Name",   .font = F_SMALL, .color = s.textMuted); }
            rcBox(.w = "80px",  .align = "cr") { rcTextL("Shares", .font = F_SMALL, .color = s.textMuted); }
            rcBox(.w = "90px",  .align = "cr") { rcTextL("Price",  .font = F_SMALL, .color = s.textMuted); }
            rcBox(.w = "110px", .align = "cr") { rcTextL("P/L",    .font = F_SMALL, .color = s.textMuted); }
        }
        rcColumn(.id = "PortScroll", .w = "grow", .h = "grow", .scroll = "v", .gap = 4, .pr = 12) {
            int n = tr_count(&st->store);
            for (int i = 0; i < n; i++) {
                const TrInstrument *p = tr_at(&st->store, i);
                if (!p || p->position == 0)   /* !p: i < tr_count() so never NULL; guard silences -fanalyzer */
                    continue;
                int64_t pl = (int64_t)p->position * (p->price - p->avgCost);
                rcRow(.w = "grow", .h = "44px", .px = 10, .gap = 10, .align = "cl",
                       .bg = s.surfaceAlt, .borderRadius = "all-md") {
                    rcBox(.w = "70px") { rcTextC(p->symbol, .font = F_BODY, .color = s.text); }
                    rcBox(.w = "grow", .overflow = "hidden") {
                        rcTextC(p->name, .font = F_SMALL, .color = s.textMuted, .wrap = "n");
                    }
                    rcBox(.w = "80px",  .align = "cr") { rcTextC(p->positionStr, .font = F_BODY, .color = s.text); }
                    rcBox(.w = "90px",  .align = "cr") { rcTextC(p->priceStr,    .font = F_BODY, .color = s.text); }
                    rcBox(.w = "110px", .align = "cr") { rcTextC(p->plStr,        .font = F_BODY, .color = tr_updown(pl)); }
                }
            }
        }
    }
}

/* The modals - placed OUTSIDE the root so their scrim covers the whole window. The
   confirm summary is assembled from backend buffers + literals (NO rcFormat: the
   modal renders during warmup where ctx->arena is NULL). */
static void trader_modals(AppState *st) {
    RC_Style s = rcGetStyle();
    const TrInstrument *it = tr_selected(&st->store);

    if (rcBeginModal("modal_confirm", &st->modalConfirm)) {
        rcColumn(.w = "400px", .bg = s.surface, .p = 18, .gap = 12,
                  .borderRadius = "all-xl") {
            rcTextL("Confirm order", .font = F_TITLE, .color = s.text);
            rcRow(.w = "grow", .gap = 6, .align = "cl") {
                rcTextC(st->orderSide == TR_BUY ? "Buy" : "Sell", .font = F_BODY,
                         .color = rcHex((uint32_t)(st->orderSide == TR_BUY ? TR_UP : TR_DOWN)));
                rcTextC(st->qty[0] ? st->qty : "0", .font = F_BODY, .color = s.text);
                rcTextL("shares of", .font = F_BODY, .color = s.textMuted);
                rcTextC(it ? it->symbol : "--", .font = F_BODY, .color = s.text);
            }
            rcRow(.w = "grow", .align = "cl") {
                rcTextL("Est. cost", .font = F_SMALL, .color = s.textMuted);
                rcBox(.w = "grow") {}
                rcTextC(st->store.estCostStr, .font = F_BODY, .color = s.text);
            }
            rcRow(.gap = 8) {
                if (rcButton("btn_confirm", "Confirm", RC_BTN_PRIMARY)) {
                    tr_place_order(&st->store, (TrSide)st->orderSide, qty_to_int(st->qty));
                    st->modalConfirm = false;
                }
                if (rcButton("btn_cancel", "Cancel", RC_BTN_DEFAULT))
                    st->modalConfirm = false;
            }
        }
        rcEndModal();
    }

    if (rcBeginModal("modal_settings", &st->modalSettings)) {
        rcColumn(.w = "400px", .bg = s.surface, .p = 18, .gap = 14,
                  .borderRadius = "all-xl") {
            rcTextL("Settings", .font = F_TITLE, .color = s.text);
            rcRow(.w = "grow", .align = "cl") {
                rcTextL("Dark mode", .font = F_BODY, .color = s.text);
                rcBox(.w = "grow") {}
                rcToggle("tg_dark_set", &st->darkMode);
            }
            rcRow(.w = "grow", .align = "cl") {
                rcTextL("Confirm dialogs", .font = F_BODY, .color = s.text);
                rcBox(.w = "grow") {}
                rcToggle("tg_confirm", &st->confirmDialogs);
            }
            rcRow(.w = "grow", .align = "cl", .gap = 12) {
                rcBox(.w = "140px") {
                    rcTextL("Default view", .font = F_BODY, .color = s.text);
                }
                rcBox(.w = "grow") { rcCombo("cb_view", &st->watchFilter, WATCH_VIEWS, 3); }
            }
            rcRow(.gap = 8) {
                if (rcButton("btn_set_done", "Done", RC_BTN_PRIMARY))
                    st->modalSettings = false;
            }
        }
        rcEndModal();
    }
}

/* ── the app contract ────────────────────────────────────────────────────── */

void trader_seed(AppState *st, unsigned seed) {
    tr_memzero(st, sizeof *st);          /* B2: zero all (incl. padding) THEN set fields */
    tr_store_seed(&st->store, seed);
    st->navTab       = 0;
    st->watchFilter  = 0;
    st->tf           = 3;                 /* default to "1Y": the full 48-candle window */
    st->orderSide    = TR_BUY;
    st->orderType    = 0;
    st->darkMode     = true;
    st->confirmDialogs = true;
    st->seeded       = true;
}

void trader_update(AppState *st, const AppCtx *ctx) {
    tr_store_step(&st->store, ctx->dt);   /* dt <= 0 (freeze) => a no-op */
    /* Push the order price + qty into the backend so the est-cost buffer is precomputed
       (idempotent; the core displays only the precomputed string, never rcFormat). A
       Limit order prices at the user's price; set it BEFORE tr_set_qty reads it. */
    tr_set_order_px(&st->store, st->orderType == 1 ? px_to_cents(st->limitPx) : 0);
    tr_set_qty(&st->store, qty_to_int(st->qty));
}

void trader_layout(AppState *st, const AppCtx *ctx) {
    (void)ctx;
    rcSetStyle(st->darkMode ? rcStyleDark() : rcStyleLight());
    RC_Style s = rcGetStyle();

    rcColumn(.id = "Root", .w = "grow", .h = "grow", .bg = s.background) {
        trader_topbar();
        rcRow(.id = "Body", .w = "grow", .h = "grow") {
            trader_navrail(st);
            if (st->navTab == 1) {
                trader_portfolio(st);       /* Portfolio tab: the full-width holdings view */
            } else {
                trader_watchlist(st);       /* Markets tab: watchlist + detail + order form */
                trader_detail(st);
                trader_orderpanel(st);
            }
        }
    }
    trader_modals(st);              /* modals sit outside Root (full-window scrim) */

    /* Register every scroll container's bar; rcScrollbar no-ops for one absent this
       frame, so the inactive tab's containers cost nothing. */
    rcScrollbar("WatchScroll");
    rcScrollbar("PosScroll");
    rcScrollbar("PortScroll");
}

void trader_demo_chrome(AppState *st, const AppCtx *ctx) {
    if (ctx->mode != APP_DEMO || !ctx->arena)
        return;
    /* A floating perf/status HUD - demo-only, PASSTHROUGH so it never blocks the
       titlebar controls. rcFormat is fine here (never in the bench path). */
    RC_String hud = rcFormat(ctx->arena, "%.0f fps \xc2\xb7 %d symbols \xc2\xb7 %d fills",
                                ctx->dt > 0.0f ? 1.0f / ctx->dt : 0.0f,
                                tr_count(&st->store), tr_order_count(&st->store));
    rcBox(.id = "demo_hud", .bg = rcAlpha(RC_BLACK, 150), .px = 10, .py = 5,
           .borderRadius = "all-full",
           .floating = { .to = RC_ATTACH_ROOT, .parent = RC_ANCHOR_BOTTOM_RIGHT,
                         .element = RC_ANCHOR_BOTTOM_RIGHT, .offset = { -16, -16 },
                         .capture = RC_CAPTURE_PASSTHROUGH }) {
        rcText(hud, .font = F_SMALL, .color = RC_WHITE);
    }
}

void trader_bench_step(AppState *st, const AppInputSink *in, int frame) {
    (void)st;   /* trader has no external/backend event: EVERY action is synthetic input */
    /* The frozen scripted scenario. EVERY user action goes through the input sink (B3).
       At/after TRADER_BENCH_WARMUP the app HOLDS - a strict no-op - so a double-
       rendered frame is byte-identical.

       DETERMINISM AT THE HOLD: caret blink + tooltip dwell read a REAL clock, so the
       frozen frame carries NO focused input and the pointer is parked OFF-CANVAS (the
       pre-hold blur+park). COORDS are a FIRST DRAFT for 1280x720; retune them. The
       y-bands ARE panel-correct (topbar/titlebar y 0..52; the app's own controls are
       BELOW it). The instrument SELECT happens BEFORE any watchlist scroll (the mock
       pointer starts at 0,0; the engine scrolls the container under the pointer, so the
       select doubles as parking the pointer over the list) - and the selected
       instrument determines the WHOLE frozen scene + which price the warmup nudges, so
       VERIFY store.selected == TRADER_BENCH_PICK (a dense-chart ticker) at the freeze. */
    if (!in || frame >= TRADER_BENCH_WARMUP)
        return;                                        /* the HOLD */

    if (frame == 6) {
        in->move(in->ctx, 150.0f, 270.0f);            /* select the pick instrument (BEFORE scroll) */
        in->button(in->ctx, APP_MBTN_LEFT, true);
    } else if (frame == 7) {
        in->button(in->ctx, APP_MBTN_LEFT, false);
    } else if (frame >= 10 && frame < 16) {
        in->wheel(in->ctx, 0.0f, -2.0f);              /* scroll the watchlist (pointer over it) */
    } else if (frame == 20) {
        in->move(in->ctx, 1180.0f, 190.0f);           /* focus the qty input (far-right x clamps the caret) */
        in->button(in->ctx, APP_MBTN_LEFT, true);
    } else if (frame == 21) {
        in->button(in->ctx, APP_MBTN_LEFT, false);
    } else if (frame >= 24 && frame < 27) {
        static const char QTY[] = "100";              /* type a fixed quantity */
        in->text(in->ctx, (unsigned int)(unsigned char)QTY[frame - 24]);
    } else if (frame == 32) {
        in->move(in->ctx, 1120.0f, 360.0f);           /* Place order -> opens the confirm modal */
        in->button(in->ctx, APP_MBTN_LEFT, true);
    } else if (frame == 33) {
        in->button(in->ctx, APP_MBTN_LEFT, false);
    } else if (frame == 40) {
        in->move(in->ctx, 545.0f, 430.0f);            /* Confirm -> tr_place_order (a fill appears) */
        in->button(in->ctx, APP_MBTN_LEFT, true);
    } else if (frame == 41) {
        in->button(in->ctx, APP_MBTN_LEFT, false);
    } else if (frame == TRADER_BENCH_WARMUP - 2) {
        in->move(in->ctx, -100.0f, -100.0f);          /* blur any input + park off-canvas: press ... */
        in->button(in->ctx, APP_MBTN_LEFT, true);
    } else if (frame == TRADER_BENCH_WARMUP - 1) {
        in->button(in->ctx, APP_MBTN_LEFT, false);     /* ... release; pointer stays off-canvas into the hold */
    }
}
