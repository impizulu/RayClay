/*
================================================================================
    opsdash_app.c - the ops-dashboard app's pure-RC_ GUI + bench hooks
================================================================================

    The single implementation TU. It defines the five-function app contract
    (seed / update / layout / demo_chrome / bench_step) and carries the header-only
    backend implementation (OPSDASH_BACKEND_IMPLEMENTATION).

    PURE RC_ API - RayClay types only, no layout-engine call, no <system> include
    (any raw-memory need is routed through opsdash_backend.h). The FROZEN core
    (seed/update/layout) calls ZERO rcFormat: every dynamic string is a backend fixed
    buffer and every element id comes from a static table, so the arena-less bench
    core never dereferences a NULL arena. rcFormat is used ONLY in
    opsdash_demo_chrome (guarded by ctx->arena).

    THE STATIC/DYNAMIC SPLIT, stated where it is easy to break: ops_inventory and
    ops_nav read ONLY fields that ops_seed wrote, plus the hover/selection state a
    pointer moves. ops_telemetry is the only part that reads OpsLive. If a future
    edit makes the inventory depend on the live band - a "current p99" column on every
    card, say - this app stops being a partially-static subject and L1 loses its
    scene. That is the one invariant here worth guarding.

    Build target: rayclay_bench_opsdash
================================================================================
*/
#define OPSDASH_BACKEND_IMPLEMENTATION
#include "opsdash_app.h"

/* The frozen bench scenario length: warmup frames, then HOLD. Retune this and the
   coordinates to its measurement budget when it wires the app. */
#define OPSDASH_BENCH_WARMUP 80

/* Layout constants (px). RayClay scales these by the display zoom for us, so the
   numbers below are logical units and the scene reflows rather than pixelates. */
#define OPS_NAV_W      190
#define OPS_RAIL_W     260
#define OPS_CARD_W     240
#define OPS_SPARK_H     56
#define OPS_TOPBAR_H    52

/* Stable ids. The frozen core cannot format an id (no arena), and an id that is not
   stable frame to frame breaks hover, so both interactive sets get a static table.
   Never shorten these to a shared buffer: the layout engine retains the POINTER
   for its inspector and its duplicate-id reports, so a per-frame scratch id garbles
   both even though the hashing itself would still be correct. */
static const char *const CARD_IDS[OPS_SVC_COUNT] = {
    "svc00", "svc01", "svc02", "svc03", "svc04", "svc05", "svc06", "svc07",
    "svc08", "svc09", "svc10", "svc11", "svc12", "svc13", "svc14", "svc15",
    "svc16", "svc17", "svc18", "svc19", "svc20", "svc21", "svc22", "svc23",
    "svc24", "svc25", "svc26", "svc27", "svc28", "svc29", "svc30", "svc31",
    "svc32", "svc33", "svc34", "svc35", "svc36", "svc37", "svc38", "svc39",
    "svc40", "svc41", "svc42", "svc43", "svc44", "svc45", "svc46", "svc47",
};
static const char *const NAV_IDS[OPS_GROUP_COUNT] = {
    "nav0", "nav1", "nav2", "nav3", "nav4", "nav5", "nav6", "nav7",
};

static const char *const TIER_LABEL[4] = { "T?", "T1", "T2", "T3" };
static const char *const HEALTH_LABEL[OPS_HEALTH_COUNT] = { "OK", "WARN", "DOWN" };

/* ── small helpers ───────────────────────────────────────────────────────── */

/* Health reads as a colour everywhere in this app, so it is resolved in one place -
   which is why the theme growing success/warning was a one-function change here.
   All three states now follow a consumer's custom style; none reaches past it.

   Note: the two new states take the *Hover shades, and that is not a typo. They are
   the exact constants this app already shipped, so the migration moves no pixels - a
   theming change should be provable by comparing frames, and it was. The asymmetry
   with danger (which sits on the base) is inherited from the palette these three
   were picked from by hand, not a claim that hover is the right tier for a resting
   indicator. */
static RC_Color ops_health_color(uint8_t health) {
    RC_Style s = rcGetStyle();
    if (health == OPS_DOWN) return s.danger;
    if (health == OPS_WARN) return s.warningHover;
    return s.successHover;
}

/* A filled status dot. */
static void ops_dot(uint8_t health) {
    rcBox(.w = "8px", .h = "8px", .borderRadius = "all-full", .bg = ops_health_color(health)) {}
}

/* A tag pill (rounded, muted). */
static void ops_chip(const char *label) {
    RC_Style s = rcGetStyle();
    rcBox(.px = 8, .py = 2, .align = "cc", .borderRadius = "all-full", .bg = s.surfaceAlt) {
        rcTextC(label, .font = F_SMALL, .color = s.textMuted);
    }
}

/* Does this service pass the current nav + health filters? Pure read of seeded state
   plus two UI toggles - deliberately NOT a function of the live band. */
static bool ops_visible(const AppState *st, const OpsService *sv) {
    if (st->group >= 0 && sv->group != (uint8_t)st->group) return false;
    if (st->onlyUnhealthy && sv->health == OPS_OK)         return false;
    return true;
}

/* ── the STATIC island: nav + inventory ──────────────────────────────────── */

/* One service card. Everything drawn here was written by ops_seed and never again -
   EXCEPT the hover and selection tints, which is the point: this is a static subtree
   that a pointer can still repaint, and an island cache that misses it draws a card
   that never lights up. */
static void ops_card(AppState *st, int idx) {
    RC_Style          s  = rcGetStyle();
    const OpsService *sv = &st->store.svc[idx];
    const char       *id = CARD_IDS[idx];
    bool sel = (idx == st->selected);
    bool hov = rcIsHovered(id);

    rcColumn(.id = id, .w = "240px", .p = 10, .gap = 7, .borderRadius = "all-md",
             .bg = sel ? s.surfaceAlt : (hov ? s.surface : s.background),
             .border = { .color = sel ? s.primary : s.border, .width = "all-1" }) {
        rcRow(.w = "grow", .gap = 7, .align = "cl") {
            ops_dot(sv->health);
            rcTextC(sv->name, .font = F_BODY, .color = s.text);
            rcBox(.w = "grow") {}
            ops_chip(TIER_LABEL[sv->tier <= 3 ? sv->tier : 0]);
        }
        rcRow(.w = "grow", .gap = 6, .align = "cl") {
            rcTextC(sv->owner, .font = F_SMALL, .color = s.textMuted);
            rcBox(.w = "grow") {}
            rcTextC(OPS_REGIONS[sv->region & 3u], .font = F_SMALL, .color = s.textMuted);
        }
        rcRow(.w = "grow", .gap = 12, .align = "cl") {
            rcTextC(sv->rps, .font = F_MONO, .color = s.text);
            rcTextC(sv->p99, .font = F_MONO, .color = s.textMuted);
            rcBox(.w = "grow") {}
            rcTextC(HEALTH_LABEL[sv->health < OPS_HEALTH_COUNT ? sv->health : 0],
                    .font = F_SMALL, .color = ops_health_color(sv->health));
        }
    }
}

/* The left nav: eight fixed groups with a seeded roll-up badge. */
static void ops_nav(AppState *st) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "190px", .h = "grow", .p = 12, .gap = 4, .bg = s.surface) {
        rcTextL("FLEET", .font = F_SMALL, .color = s.textMuted);
        rcBox(.h = "6px") {}
        for (int g = 0; g < OPS_GROUP_COUNT; g++) {
            const char *id  = NAV_IDS[g];
            bool        act = (st->group == g);
            rcRow(.id = id, .w = "grow", .px = 9, .py = 7, .gap = 8, .align = "cl",
                  .borderRadius = "all-sm",
                  .bg = act ? s.surfaceAlt : (rcIsHovered(id) ? s.background : RC_TRANSPARENT)) {
                ops_dot(st->store.groupHealth[g]);
                rcTextC(OPS_GROUPS[g], .font = F_BODY,
                        .color = act ? s.text : s.textMuted);
            }
        }
    }
}

/* The inventory: the large unchanged subtree, inside a scroll container so the clip
   and scroll axes of the invalidation matrix are exercised by the subject itself. */
static void ops_inventory(AppState *st, const AppCtx *ctx) {
    RC_Style s = rcGetStyle();

    /* Columns are derived from the live viewport so the grid genuinely reflows; the
       bench viewport is fixed (1280x720), so the bench arm stays deterministic. */
    int avail = ctx->fbWidth - OPS_NAV_W - OPS_RAIL_W - 48;
    int cols  = avail / (OPS_CARD_W + 12);
    if (cols < 1) cols = 1;
    if (cols > 4) cols = 4;

    rcColumn(.id = "InvScroll", .w = "grow", .h = "grow", .scroll = "v",
             .p = 16, .gap = 12) {
        for (int i = 0; i < OPS_SVC_COUNT; ) {
            /* One row of up to `cols` VISIBLE cards. Filtered-out services are skipped
               without opening an element, so a filtered grid declares fewer elements
               rather than declaring hidden ones. */
            int placed = 0;
            rcRow(.w = "grow", .gap = 12, .align = "tl") {
                while (i < OPS_SVC_COUNT && placed < cols) {
                    if (ops_visible(st, &st->store.svc[i]))
                        { ops_card(st, i); placed++; }
                    i++;
                }
                rcBox(.w = "grow") {}
            }
            if (placed == 0) break;   /* the filter emptied the tail */
        }
        rcBox(.h = "8px") {}
        rcTextL("End of fleet", .font = F_SMALL, .color = s.textMuted);
    }
}

/* ── the DYNAMIC rail ────────────────────────────────────────────────────── */

/* The sparkline: 24 bars re-read from the live ring every frame. Bars are drawn as
   plain boxes rather than a chart so the rail's cost is vertex emission, not chart
   machinery - this app's job is to isolate the static/dynamic split, not to re-test
   the chart widget the gallery app already covers. */
static void ops_sparkline(const OpsLive *live) {
    RC_Style s = rcGetStyle();
    rcRow(.w = "grow", .h = "56px", .gap = 2, .align = "bl") {
        for (int i = 0; i < OPS_SPARK_COUNT; i++) {
            int   v = live->spark[i];
            int   h = 4 + (v * (OPS_SPARK_H - 4)) / 100;
            bool  hot = v > 75;
            rcBox(.w = "grow", .hType = RC_PX(h), .borderRadius = "t-sm",
                  .bg = hot ? s.warningHover : s.primary) {}
        }
    }
}

static void ops_telemetry(AppState *st, const AppCtx *ctx) {
    RC_Style        s    = rcGetStyle();
    const OpsLive  *live = &st->store.live;
    (void)ctx;

    rcColumn(.w = "260px", .h = "grow", .p = 14, .gap = 14, .bg = s.surface) {
        /* The incident banner. Its tint follows the triangle pulse, so this element
           changes on EVERY frame - the animation axis, and the reason a whole-frame
           "nothing changed" check can never short-circuit this scene. */
        rcColumn(.w = "grow", .p = 11, .gap = 5, .borderRadius = "all-md",
                 .bg = rcAlpha(s.danger, (uint8_t)(40 + (int)(live->pulse * 70.0f)))) {
            rcRow(.w = "grow", .gap = 7, .align = "cl") {
                ops_dot(OPS_DOWN);
                rcTextL("ACTIVE INCIDENT", .font = F_SMALL, .color = s.text);
            }
            rcTextL("gateway-api elevated errors", .font = F_SMALL, .color = s.textMuted);
            rcRow(.w = "grow", .align = "cl") {
                rcTextL("open for", .font = F_SMALL, .color = s.textMuted);
                rcBox(.w = "grow") {}
                rcTextC(live->upText, .font = F_MONO, .color = s.text);
            }
        }

        rcTextL("p99 LATENCY", .font = F_SMALL, .color = s.textMuted);
        ops_sparkline(live);

        /* Two readouts whose DIGITS roll over time. That is the atlas/font axis: the
           counter reaches glyphs the atlas has not baked yet, so a cache keyed on
           "the text did not move" is still wrong when the glyph generation changes. */
        rcRow(.w = "grow", .align = "cl") {
            rcTextL("now", .font = F_SMALL, .color = s.textMuted);
            rcBox(.w = "grow") {}
            rcTextC(live->p99Text, .font = F_MD, .color = s.text);
        }
        rcRow(.w = "grow", .align = "cl") {
            rcTextL("requests", .font = F_SMALL, .color = s.textMuted);
            rcBox(.w = "grow") {}
            rcTextC(live->reqText, .font = F_MD, .color = s.text);
        }

        rcBox(.w = "grow", .h = "1px", .bg = s.border) {}

        /* The selected service's SEEDED detail. It changes only when the selection
           does, which makes it the third state in the matrix: neither per-frame like
           the band above nor never like the grid behind. */
        rcTextL("SELECTED", .font = F_SMALL, .color = s.textMuted);
        {
            const OpsService *sv = &st->store.svc[st->selected];
            rcColumn(.w = "grow", .gap = 5) {
                rcTextC(sv->name, .font = F_MD, .color = s.text);
                rcRow(.w = "grow", .gap = 6, .align = "cl") {
                    ops_chip(TIER_LABEL[sv->tier <= 3 ? sv->tier : 0]);
                    ops_chip(OPS_REGIONS[sv->region & 3u]);
                }
                rcTextC(sv->owner, .font = F_SMALL, .color = s.textMuted);
            }
        }
    }
}

/* ── the app contract ────────────────────────────────────────────────────── */

void opsdash_seed(AppState *st, unsigned seed) {
    ops_memzero(st, sizeof *st);
    ops_seed(&st->store, seed);
    st->group    = -1;      /* all groups */
    st->selected = 0;
    st->seeded   = true;
}

void opsdash_update(AppState *st, const AppCtx *ctx) {
    ops_tick(&st->store, ctx->dt);   /* dt == 0 freezes the band; see opsdash_app.h */
}

void opsdash_layout(AppState *st, const AppCtx *ctx) {
    RC_Style s = rcGetStyle();

    /* Selection and filters are driven from the layout pass because that is where the
       hit test lives; both write only AppState, never the backend model. */
    for (int i = 0; i < OPS_SVC_COUNT; i++)
        if (rcIsHovered(CARD_IDS[i]) && rcPointerPressed(RC_POINTER_LEFT))
            st->selected = i;
    for (int g = 0; g < OPS_GROUP_COUNT; g++)
        if (rcIsHovered(NAV_IDS[g]) && rcPointerPressed(RC_POINTER_LEFT))
            st->group = (st->group == g) ? -1 : g;

    rcColumn(.w = "grow", .h = "grow", .bg = s.background) {
        rcRow(.id = RC_ID_WINDOW_DRAG, .w = "grow", .hType = RC_PX(OPS_TOPBAR_H),
              .px = 16, .gap = 12, .align = "cl", .bg = s.chrome) {
            rcTextL("Fleet Operations", .font = F_TITLE, .color = s.text);
            rcBox(.w = "grow") {}
            /* The border is load-bearing, not decoration: in the dark theme s.surface
               and s.chrome are close enough that an unbordered pill on the titlebar
               reads as bare text and nobody discovers it is a control. Caught by
               rendering the scene and looking at it, which is the only way this class
               of defect ever surfaces. */
            rcRow(.id = "filt_unhealthy", .px = 10, .py = 5, .gap = 7, .align = "cc",
                  .borderRadius = "all-full",
                  .border = { .color = st->onlyUnhealthy ? s.primary : s.border,
                              .width = "all-1" },
                  .bg = st->onlyUnhealthy ? s.primary
                                          : (rcIsHovered("filt_unhealthy") ? s.surfaceAlt
                                                                           : s.surface)) {
                rcTextC("Unhealthy only", .font = F_SMALL,
                        .color = st->onlyUnhealthy ? s.background : s.text);
            }
        }
        rcRow(.w = "grow", .h = "grow") {
            ops_nav(st);
            ops_inventory(st, ctx);
            ops_telemetry(st, ctx);
        }
    }

    if (rcIsHovered("filt_unhealthy") && rcPointerPressed(RC_POINTER_LEFT))
        st->onlyUnhealthy = !st->onlyUnhealthy;
}

void opsdash_demo_chrome(AppState *st, const AppCtx *ctx) {
    if (ctx->mode != APP_DEMO || !ctx->arena)
        return;
    /* A floating perf HUD - demo-only, PASSTHROUGH so it never blocks the titlebar
       controls. rcFormat is fine here (never in the bench path). */
    RC_String hud = rcFormat(ctx->arena, "%.0f fps \xc2\xb7 %s req \xc2\xb7 zoom %.2f",
                             ctx->dt > 0.0f ? 1.0f / ctx->dt : 0.0f,
                             st->store.live.reqText, (double)ctx->zoom);
    rcBox(.id = "demo_hud", .bg = rcAlpha(RC_BLACK, 150), .px = 10, .py = 5,
          .borderRadius = "all-full",
          .floating = { .to = RC_ATTACH_ROOT, .parent = RC_ANCHOR_BOTTOM_RIGHT,
                        .element = RC_ANCHOR_BOTTOM_RIGHT, .offset = { -16, -16 },
                        .capture = RC_CAPTURE_PASSTHROUGH }) {
        rcText(hud, .font = F_SMALL, .color = RC_WHITE);
    }
}

void opsdash_bench_step(AppState *st, const AppInputSink *in, int frame) {
    (void)st;   /* every action is synthetic input (B3); the seeded selection is 0 */
    /* The frozen scripted scenario. At/after OPSDASH_BENCH_WARMUP the app HOLDS (a
       strict no-op) so a double-rendered frame is byte-identical.

       The script's job here is different from the other five apps. It is not trying
       to reach a busy frame - it is trying to leave the scene in the state L1 has to
       get right: the inventory scrolled (so the clip rect is not the identity), a card
       selected (so one card in the static grid differs from its neighbours), and the
       pointer parked off-canvas at the hold so no hover tint depends on a real clock.

       COORDS are a first draft for 1280x720: the nav is the left 190px band, the grid
       the middle, the rail the right 260px. Retune them for your own layout. */
    if (!in || frame >= OPSDASH_BENCH_WARMUP)
        return;                                        /* the HOLD */

    if (frame >= 6 && frame < 14) {
        in->move(in->ctx, 520.0f, 380.0f);             /* hover the grid, then scroll it */
        in->wheel(in->ctx, 0.0f, -1.0f);
    } else if (frame == 20) {
        in->move(in->ctx, 520.0f, 300.0f);             /* select a card: press ... */
        in->button(in->ctx, APP_MBTN_LEFT, true);
    } else if (frame == 21) {
        in->button(in->ctx, APP_MBTN_LEFT, false);      /* ... release */
    } else if (frame == 30) {
        in->move(in->ctx, 90.0f, 150.0f);              /* filter to one nav group: press ... */
        in->button(in->ctx, APP_MBTN_LEFT, true);
    } else if (frame == 31) {
        in->button(in->ctx, APP_MBTN_LEFT, false);
    } else if (frame == 40) {
        in->move(in->ctx, 90.0f, 150.0f);              /* ... and back to all groups */
        in->button(in->ctx, APP_MBTN_LEFT, true);
    } else if (frame == 41) {
        in->button(in->ctx, APP_MBTN_LEFT, false);
    } else if (frame == OPSDASH_BENCH_WARMUP - 1) {
        in->move(in->ctx, -100.0f, -100.0f);           /* park off-canvas into the hold */
    }
}
