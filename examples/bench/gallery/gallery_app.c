/*
================================================================================
    gallery_app.c - the gallery app's pure-RC_ GUI + bench hooks
================================================================================

    The single implementation TU. It defines the four-function app contract
    (seed / update / layout / bench_step) + a demo-only chrome overlay, and it
    carries the header-only backend implementation (GALLERY_BACKEND_IMPLEMENTATION).

    PURE RC_ API - RayClay types only, no layout-engine call, no <system>
    include (any raw-memory / string need is routed through gallery_backend.h). The
    FROZEN core (seed/update/layout, incl. the modal) calls ZERO rcFormat: every
    dynamic string is a backend fixed buffer (dim "96 x 72", the result count) or a
    stable literal, and element ids come from a static table - so the arena-less
    bench core never dereferences a NULL arena. rcFormat is used ONLY in
    gallery_demo_chrome (guarded by ctx->arena).

    IMAGES: the 12 "photos" are procedurally generated as valid 32-bit BMPs by the
    backend and decoded ONCE per process through the PUBLIC rcLoadImageFromMemory -
    exercising the real stb_image decode + GPU upload (the gallery's B9 cost centre).
    Decoded handles live in a process-global cache (g_images) so a re-seed re-copies
    rather than re-decodes: the capture backend's image-handle pool is a 16-slot
    one-way static, so re-decoding would drain it. Every draw path degrades to a
    placeholder if a handle is NULL (the library does not auto-substitute one).

    Build target: rayclay_bench_gallery
================================================================================
*/
#define GALLERY_BACKEND_IMPLEMENTATION
#include "gallery_app.h"

/* The frozen bench scenario length: warmup frames, then HOLD. Retune this and the
   click coordinates to your own measurement budget and layout. */
#define GALLERY_BENCH_WARMUP 80

/* The image the bench selects + freezes on (a captioned landscape). SEEDED as the
   default selection (like notes' default-open note) so the frozen scene is
   click-coordinate-independent - robust to grid reflow. */
#define GALLERY_BENCH_PICK 4

/* Layout constants (px). The grid column count is derived from the live viewport
   width so the grid reflows; the bench viewport is fixed (1280x720) => deterministic. */
#define GAL_DETAIL_W  380
#define GAL_THUMB_W   190
#define GAL_THUMB_H   132
#define GAL_CELL_W    (GAL_THUMB_W + 12)
#define GAL_GRID_GAP  14

/* The caption text the bench types into the selected photo's rcTextArea (ASCII). It is
   APPENDED to the seeded caption, making it long enough to SOFT-WRAP across rows - the
   per-line measure cost the text area adds. A genuine newline inserts only via the Enter
   KEY channel (in->key with an ENTER semantic), NOT via in->text: rc_textedit's char
   filter drops control bytes incl. '\n' - so add an Enter press to reach the
   multiline-EDIT (hard newline) determinism case. */
static const char GAL__TYPED[] = " Shot at dawn, in soft golden light.";

/* Stable ids for the thumbnail cells (the layout engine needs a stable id per interactive element;
   an rcFormat id is impossible in the arena-less frozen core, so a static table). */
static const char *const THUMB_IDS[GAL_IMG_COUNT] = {
    "thumb00", "thumb01", "thumb02", "thumb03", "thumb04", "thumb05",
    "thumb06", "thumb07", "thumb08", "thumb09", "thumb10", "thumb11",
};

/* Process-global one-time image cache. It survives gallery_seed's AppState memset
   (a re-seed re-COPIES these handles, never re-decodes) and its handles are
   deterministic by load order - so the two frozen frames render identical handles. */
static bool     g_imagesLoaded = false;
static RC_Image g_images[GAL_IMG_COUNT];

/* ── small helpers ───────────────────────────────────────────────────────── */

/* A tag pill (rounded, muted). */
static void gallery_chip(const char *label) {
    RC_Style s = rcGetStyle();
    rcBox(.px = 9, .py = 3, .align = "cc", .borderRadius = "all-full", .bg = s.surfaceAlt) {
        rcTextC(label, .font = F_SMALL, .color = s.textMuted);
    }
}

/* A "label ....... value" row for the details modal (precomputed strings only). */
static void info_row(const char *label, const char *value) {
    RC_Style s = rcGetStyle();
    rcRow(.w = "grow", .align = "cl") {
        rcTextC(label, .font = F_SMALL, .color = s.textMuted);
        rcBox(.w = "grow") {}
        rcTextC(value, .font = F_BODY, .color = s.text);
    }
}

/* Aspect-fit (contain) an image inside a fixed WxH cell, centred on a surfaceAlt
   letterbox, or a "?" placeholder if the handle is NULL (rcLoadImageFromMemory can
   fail - the app degrades, never derefs a NULL). .image STRETCHES the texture to the
   element, so a raw fixed box squashes non-landscape photos; this mirrors
   gallery_detail's fit math so the thumbnail and the preview frame a photo the same. */
static void thumb_image(const RC_Image *img, const GalImage *m, int w, int h) {
    RC_Style s = rcGetStyle();
    rcBox(.wType = RC_PX(w), .hType = RC_PX(h), .align = "cc",
           .bg = s.surfaceAlt, .borderRadius = "all-sm") {
        if (img && img->handle && m->w > 0 && m->h > 0) {
            float scw = (float)w / (float)m->w;
            float sch = (float)h / (float)m->h;
            float sc  = scw < sch ? scw : sch;
            int dw = (int)((float)m->w * sc);
            int dh = (int)((float)m->h * sc);
            if (dw < 1) dw = 1;
            if (dh < 1) dh = 1;
            rcBox(.image = img, .wType = RC_PX(dw), .hType = RC_PX(dh),
                   .borderRadius = "all-sm") {}
        } else {
            rcTextL("?", .font = F_TITLE, .color = s.textMuted);
        }
    }
}

/* One thumbnail cell: image + title, the whole cell a click target. */
static void gallery_thumb(AppState *st, int imgIdx) {
    RC_Style s = rcGetStyle();
    const char *id = THUMB_IDS[imgIdx];
    bool sel = (imgIdx == st->selected);
    rcColumn(.id = id, .wType = RC_PX(GAL_CELL_W), .p = 6, .gap = 6, .borderRadius = "all-md",
              .bg = sel ? s.surfaceAlt : (rcIsHovered(id) ? s.surface : RC_TRANSPARENT)) {
        thumb_image(&st->images[imgIdx], &st->store.img[imgIdx], GAL_THUMB_W, GAL_THUMB_H);
        rcBox(.w = "grow", .overflow = "hidden") {
            rcTextC(st->store.img[imgIdx].title, .font = F_SMALL,
                     .color = sel ? s.primary : s.text, .wrap = "n");
        }
    }
    if (rcClicked(id))
        st->selected = imgIdx;
}

/* ── regions ─────────────────────────────────────────────────────────────── */

/* The custom titlebar: brand + spacer + the bundled window controls. RC_ID_WINDOW_DRAG
   makes the band draggable; on web these window verbs are inert. The theme toggle lives
   in gallery_toolbar, NOT here: an interactive widget inside the drag band loses its
   click to the OS window-move (only RC_ID_WINDOW_* controls are exempt). */
static void gallery_topbar(void) {
    RC_Style s = rcGetStyle();
    rcRow(.id = RC_ID_WINDOW_DRAG, .w = "grow", .h = "52px", .bg = s.chrome,
           .px = 14, .gap = 10, .align = "cl") {
        rcBox(.w = "26px", .h = "26px", .align = "cc",
               .bg = s.primary, .borderRadius = "all-md") {
            rcTextL("RC", .font = F_SMALL, .color = RC_WHITE);
        }
        rcTextL("RayClay Gallery", .font = F_TITLE, .color = s.text);
        rcBox(.w = "grow") {}
        rcWindowControls();
    }
}

/* The toolbar (below the titlebar): the search box that filters the grid, the
   "N photos / N results" count (precomputed by the backend on each filter), and the
   theme toggle pinned far-right. The toggle lives HERE, not in the titlebar, because
   this band is NOT a drag region - so its click reaches the widget (fix for a
   drag-eaten toggle that made dark mode unreachable on desktop). */
static void gallery_toolbar(AppState *st) {
    RC_Style s = rcGetStyle();
    rcRow(.w = "grow", .h = "56px", .bg = s.surface, .px = 16, .gap = 12, .align = "cl") {
        rcBox(.w = "320px") {
            if (rcTextInput("search", st->search, sizeof st->search,
                             .placeholder = "Search photos"))
                gallery_filter(&st->store, st->search);   /* no-op at freeze (no keystroke) */
        }
        rcTextC(st->store.countStr, .font = F_SMALL, .color = s.textMuted);
        rcBox(.w = "grow") {}
        rcRow(.gap = 8, .align = "cl") {
            rcTextC(st->darkMode ? "Dark" : "Light", .font = F_SMALL, .color = s.textMuted);
            rcToggle("tg_theme", &st->darkMode);
        }
    }
}

/* The clipped-scroll thumbnail grid (the B9 cost centre). Column count is derived
   from the live viewport width, floored at 1 (a narrow pane / 500% zoom must never
   divide by zero). visibleCount == 0 is an explicit empty-state branch. */
static void gallery_grid(AppState *st, const AppCtx *ctx) {
    RC_Style s = rcGetStyle();
    /* Derive the grid width in the REFLOWED (logical / zoom) space: under the default
       RC_ZOOM_LAYOUT the cells are laid out at window/zoom, so the column count must
       reflow with the zoom too (a headline feature). zoom is 1.0 at bench -> a no-op there. */
    int gridW = (int)((float)ctx->fbWidth / (ctx->zoom > 0.0f ? ctx->zoom : 1.0f))
                - GAL_DETAIL_W - 40;
    if (gridW < GAL_CELL_W) gridW = GAL_CELL_W;
    int cols = gridW / (GAL_CELL_W + GAL_GRID_GAP);
    if (cols < 1) cols = 1;

    rcColumn(.id = "GridScroll", .w = "grow", .h = "grow", .scroll = "v",
              .bg = s.background, .p = 16, .gap = GAL_GRID_GAP) {
        int n = st->store.visibleCount;
        if (n <= 0) {
            rcColumn(.w = "grow", .h = "grow", .align = "cc") {
                rcTextL("No photos match your search.", .font = F_MD, .color = s.textMuted);
            }
        } else {
            int rows = (n + cols - 1) / cols;
            for (int r = 0; r < rows; r++) {
                rcRow(.w = "grow", .gap = GAL_GRID_GAP) {
                    for (int c = 0; c < cols; c++) {
                        int k = r * cols + c;
                        if (k < n)
                            gallery_thumb(st, st->store.visible[k]);
                    }
                }
            }
        }
    }
}

/* The detail pane: the selected image (aspect-fit or placeholder), the 56px title,
   metadata, an EDITABLE multiline caption (rcTextArea), and an info button. */
static void gallery_detail(AppState *st) {
    RC_Style s = rcGetStyle();
    int i = st->selected;
    if (i < 0 || i >= GAL_IMG_COUNT)
        return;                              /* guard BEFORE opening the element */
    /* Sync the caption buffer to the selected image in THIS pass (the grid may have
       just changed the selection above), so the detail card never shows a one-frame
       stale caption. Guarded => a no-op at the freeze (captionOf == selected already). */
    if (st->captionOf != i) {
        gallery_load_caption(&st->store, i, st->caption, sizeof st->caption);
        st->captionOf = i;
    }
    const GalImage *m = &st->store.img[i];
    const RC_Image *img = &st->images[i];

    rcColumn(.wType = RC_PX(GAL_DETAIL_W), .h = "grow", .bg = s.surface,
              .p = 18, .gap = 12) {
        /* the preview: aspect-fit inside a fixed frame, or a placeholder if NULL */
        rcBox(.w = "grow", .h = "230px", .align = "cc",
               .bg = s.surfaceAlt, .borderRadius = "all-lg") {
            if (img->handle && m->w > 0 && m->h > 0) {
                int maxW = GAL_DETAIL_W - 60, maxH = 214;
                float scw = (float)maxW / (float)m->w;
                float sch = (float)maxH / (float)m->h;
                float sc  = scw < sch ? scw : sch;
                int dw = (int)((float)m->w * sc);
                int dh = (int)((float)m->h * sc);
                if (dw < 1) dw = 1;
                if (dh < 1) dh = 1;
                rcBox(.image = img, .wType = RC_PX(dw), .hType = RC_PX(dh),
                       .borderRadius = "all-md") {}
            } else {
                rcTextL("No preview", .font = F_MD, .color = s.textMuted);
            }
        }
        /* the HERO title (F_HERO = 56px, the crisp-text-persists heading) */
        rcBox(.w = "grow", .overflow = "hidden") {
            rcTextC(m->title, .font = F_HERO, .color = s.text, .wrap = "n");
        }
        rcRow(.gap = 10, .align = "cl") {
            rcTextC(m->dim, .font = F_SMALL, .color = s.textMuted);
            gallery_chip(m->tag);
        }
        /* the editable multiline caption - a real text area over the app's buffer */
        rcTextL("Caption", .font = F_SMALL, .color = s.textMuted);
        rcTextArea("gal_caption", st->caption, sizeof st->caption, .rows = 4);

        rcBox(.w = "grow", .h = "grow") {}
        if (rcButton("btn_info", "Photo info", RC_BTN_DEFAULT))
            st->modalInfo = true;
    }
}

/* The photo-details modal - OUTSIDE the root so the scrim covers the window. Every
   line is a precomputed backend string or a literal (NO rcFormat: a modal can render
   during warmup where ctx->arena is NULL). */
static void gallery_modal(AppState *st) {
    RC_Style s = rcGetStyle();
    int i = st->selected;
    if (i < 0 || i >= GAL_IMG_COUNT)
        return;
    const GalImage *m = &st->store.img[i];

    if (rcBeginModal("modal_info", &st->modalInfo)) {
        rcColumn(.w = "360px", .bg = s.surface, .p = 18, .gap = 12,
                  .borderRadius = "all-xl") {
            rcTextL("Photo details", .font = F_TITLE, .color = s.text);
            info_row("Title", m->title);
            info_row("Dimensions", m->dim);
            info_row("Category", m->tag);
            rcRow(.gap = 8) {
                if (rcButton("btn_info_ok", "Close", RC_BTN_PRIMARY))
                    st->modalInfo = false;
            }
        }
        rcEndModal();
    }
}

/* ── the app contract ────────────────────────────────────────────────────── */

void gallery_seed(AppState *st, unsigned seed) {
    gallery_memzero(st, sizeof *st);          /* B2: zero all (incl. padding) THEN set */
    gallery_backend_seed(&st->store, seed);   /* metadata + precomputed strings + filter */
    st->selected  = GALLERY_BENCH_PICK;       /* default selection (coord-independent) */
    st->captionOf = -1;                       /* force a caption sync on the first update */
    st->darkMode  = true;
    st->seeded    = true;

    /* One-time process decode of the 12 procedural BMPs (see g_images note above). */
    if (!g_imagesLoaded) {
        unsigned char buf[GAL_BMP_CAP];       /* ~37 KB, on the stack, seed-path only */
        for (int i = 0; i < GAL_IMG_COUNT; i++) {
            size_t len = gallery_encode_bmp(&st->store, i, buf, sizeof buf);
            g_images[i] = len ? rcLoadImageFromMemory(buf, (int)len) : (RC_Image){0};
        }
        g_imagesLoaded = true;
    }
    for (int i = 0; i < GAL_IMG_COUNT; i++)
        st->images[i] = g_images[i];
}

void gallery_update(AppState *st, const AppCtx *ctx) {
    (void)st;
    (void)ctx;
    /* No per-frame simulation: the gallery content is static, and the caption sync
       (the only state advance) runs in gallery_detail so a same-frame selection
       change is reflected immediately. A dt<=0 freeze is thus trivially a no-op. */
}

void gallery_layout(AppState *st, const AppCtx *ctx) {
    rcSetStyle(st->darkMode ? rcStyleDark() : rcStyleLight());
    RC_Style s = rcGetStyle();

    rcColumn(.id = "Root", .w = "grow", .h = "grow", .bg = s.background) {
        gallery_topbar();
        gallery_toolbar(st);
        rcRow(.id = "Body", .w = "grow", .h = "grow") {
            gallery_grid(st, ctx);
            gallery_detail(st);
        }
    }
    gallery_modal(st);                        /* the modal sits outside Root */

    rcScrollbar("GridScroll");
}

void gallery_demo_chrome(AppState *st, const AppCtx *ctx) {
    if (ctx->mode != APP_DEMO || !ctx->arena)
        return;
    /* A floating perf/status HUD - demo-only, PASSTHROUGH so it never blocks the
       titlebar controls. rcFormat is fine here (never in the bench path). */
    RC_String hud = rcFormat(ctx->arena, "%.0f fps \xc2\xb7 %s",
                                ctx->dt > 0.0f ? 1.0f / ctx->dt : 0.0f, st->store.countStr);
    rcBox(.id = "demo_hud", .bg = rcAlpha(RC_BLACK, 150), .px = 10, .py = 5,
           .borderRadius = "all-full",
           .floating = { .to = RC_ATTACH_ROOT, .parent = RC_ANCHOR_BOTTOM_RIGHT,
                         .element = RC_ANCHOR_BOTTOM_RIGHT, .offset = { -16, -16 },
                         .capture = RC_CAPTURE_PASSTHROUGH }) {
        rcText(hud, .font = F_SMALL, .color = RC_WHITE);
    }
}

void gallery_bench_step(AppState *st, const AppInputSink *in, int frame) {
    (void)st;   /* every action is synthetic input (B3); selection is the seeded pick */
    /* The frozen scripted scenario. At/after GALLERY_BENCH_WARMUP the app HOLDS (a
       strict no-op) so a double-rendered frame is byte-identical.

       DETERMINISM AT THE HOLD: caret blink + tooltip dwell read a REAL clock, so the
       frozen frame carries NO focused input and the pointer is parked OFF-CANVAS. The
       scene freezes on the SEEDED selection (GALLERY_BENCH_PICK) - no coordinate-
       fragile click-select. COORDS are a FIRST DRAFT for 1280x720 (the detail pane is
       the rightmost GAL_DETAIL_W band; the caption sits mid-pane); retune them. */
    if (!in || frame >= GALLERY_BENCH_WARMUP)
        return;                                        /* the HOLD */

    if (frame >= 6 && frame < 12) {
        in->move(in->ctx, 300.0f, 300.0f);            /* hover the grid, then scroll it */
        in->wheel(in->ctx, 0.0f, -1.0f);              /* (pointer over the grid first)   */
    } else if (frame == 16) {
        in->move(in->ctx, 1040.0f, 470.0f);           /* focus the caption (end-clamped) */
        in->button(in->ctx, APP_MBTN_LEFT, true);
    } else if (frame == 17) {
        in->button(in->ctx, APP_MBTN_LEFT, false);
    } else if (frame >= 20 && frame < 20 + (int)sizeof(GAL__TYPED) - 1) {
        in->text(in->ctx, (unsigned int)(unsigned char)GAL__TYPED[frame - 20]);  /* type a caption line */
    } else if (frame == GALLERY_BENCH_WARMUP - 2) {
        in->move(in->ctx, -100.0f, -100.0f);          /* blur the caption + park off-canvas: press ... */
        in->button(in->ctx, APP_MBTN_LEFT, true);
    } else if (frame == GALLERY_BENCH_WARMUP - 1) {
        in->button(in->ctx, APP_MBTN_LEFT, false);     /* ... release; pointer stays off-canvas into the hold */
    }
}
