/*
================================================================================
    main.c - RayClay Icon Converter (interactive SVG to RayClay icon header tool)
================================================================================

    A desktop-only developer tool: scan a directory of *.svg files, preview each
    one live at native size, and export it as a header-only RayClay icon
    (<stem>.h, the same shape as the bundled rc_icons_*.h) that renders through
    the same rc_gfx seam as the bundled icons. Left = sources (dir + scan + selectable list); centre = live preview
    (viewBox / op-count / colour / warnings); right = export (out dir, per-file
    and batch export, a rolling status log).

    DESKTOP-ONLY dev tool: it reads the filesystem (dirent / <windows.h>) and
    bundles an SVG parser, so a platform #ifdef IS allowed here (unlike the
    portable examples). This example is deliberately NON-pure-RC_ by design: it
    vendors rc_svg2icon.h (a single-header SVG-to-IR parser + emitter) alongside the
    RayClay DSL rather than staying inside the RC_ surface. Everything else keeps
    the RayClay tenets: zero per-frame heap (parsing happens only on scan /
    selection / export, never per frame), all state in one struct threaded via
    .userData, and every fopen/parse is null- and again-checked so a bad file
    logs and continues - it never crashes.

    Build target: rayclay_ex11_rayclay_icon_converter
================================================================================
*/

#include "rayclay.h"   /* one include: the icon scaffolding (rcIconDraw*) rides along */

/* This app PREVIEWS what it parsed, so it opts into rc_svg2icon_draw and pays
   for the icon runtime the preview needs. An offline generator built on the same
   header leaves this undefined and links against nothing but libm. */
#define RC_SVG2ICON_PREVIEW
#include "rc_svg2icon.h"

#include <stdio.h>
#include <stdarg.h>   /* va_list: log_fmt      */
#include <string.h>
#include <stdlib.h>   /* qsort                 */
#include <ctype.h>    /* tolower: name_cmp + stem */
#ifdef _WIN32
    #include <windows.h>  /* FindFirstFileA directory scan */
    #include <direct.h>   /* _mkdir */
#else
    #include <dirent.h>   /* opendir/readdir directory scan (no dirent on MSVC) */
    #include <sys/stat.h> /* mkdir  */
#endif

/* Font ladder baked from the bundled face - zero-asset, like the other examples. */
typedef enum { F_SMALL = 0, F_BODY, F_TITLE, F_COUNT } AppFont;

/* ── bounded, heap-free state ───────────────────────────────────────────────
   Every collection is a fixed array so the whole tool runs with ZERO per-frame
   (indeed per-run) heap allocation. rcFormat uses the app's frame arena. */

#define ICONV_MAX_FILES  256   /* scanned *.svg entries                        */
#define ICONV_NAME_CAP   128   /* per-entry file name (stem + ".svg")          */
#define ICONV_DIR_CAP    512   /* input / output directory buffers             */
#define ICONV_SVG_CAP    (64 * 1024)  /* bounded SVG read buffer               */
#define ICONV_EMIT_CAP   (64 * 1024)  /* bounded header emit buffer            */
#define ICONV_LOG_LINES  64    /* status-log ring                              */
#define ICONV_LOG_CAP    160   /* per-log-line text                            */

typedef struct {
    char files[ICONV_MAX_FILES][ICONV_NAME_CAP]; /* names as scanned           */
    char rowId[ICONV_MAX_FILES][8];              /* stable "f0".."f255" ids    */
    int  fileCount;
    int  selected;               /* index into files[], or -1                  */

    char inDir[ICONV_DIR_CAP];   /* source directory (rcTextInput buffer)     */
    char outDir[ICONV_DIR_CAP];  /* export directory (rcTextInput buffer)     */

    RcSvgIcon icon;              /* the currently-parsed selection             */
    bool      haveIcon;          /* icon holds a valid parse                   */
    bool      strokeDark;        /* preview stroke colour: dark vs light       */

    char log[ICONV_LOG_LINES][ICONV_LOG_CAP]; /* status-log ring               */
    int  logCount;               /* number of live lines (caps at ring size)   */
    int  logHead;                /* index of the OLDEST live line              */

    bool pendingScan;            /* seeded from argv[1]: scan on first frame   */
} AppState;

/* ── status log (fixed ring) ────────────────────────────────────────────────
   Append one line; when full, the oldest is overwritten. No allocation. */

/* Claim the next ring slot and return it. Both front doors below format STRAIGHT
   into the slot, so a message never needs a caller-side scratch buffer. */
static char *log_slot(AppState *st) {
    int idx;
    if (st->logCount < ICONV_LOG_LINES) {
        idx = (st->logHead + st->logCount) % ICONV_LOG_LINES;
        st->logCount++;
    } else {
        idx = st->logHead;
        st->logHead = (st->logHead + 1) % ICONV_LOG_LINES;
    }
    return st->log[idx];
}

/* Fixed message. */
static void log_line(AppState *st, const char *msg) {
    /* snprintf never overflows the fixed slot and always NUL-terminates. */
    snprintf(log_slot(st), ICONV_LOG_CAP, "%s", msg ? msg : "");
}

/* Formatted message - the same ring, minus the snprintf-into-a-local ceremony.
   RC_PRINTF_FMT (rayclay.h, a no-op on MSVC) keeps -Wformat checking every call
   site's literal; it sits among the declaration specifiers because GCC rejects a
   trailing attribute on a function DEFINITION. */
static void RC_PRINTF_FMT(2, 3) log_fmt(AppState *st, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(log_slot(st), ICONV_LOG_CAP, fmt, ap);
    va_end(ap);
}

/* ── filesystem helpers ─────────────────────────────────────────────────────
   All are defensive: a NULL/failed handle just returns, it never crashes. */

/* Case-insensitive ".svg" suffix test. */
static bool ends_with_svg(const char *name) {
    size_t n = strlen(name);
    if (n < 4) return false;
    const char *e = name + (n - 4);
    return (e[0] == '.') &&
           (e[1] == 's' || e[1] == 'S') &&
           (e[2] == 'v' || e[2] == 'V') &&
           (e[3] == 'g' || e[3] == 'G');
}

/* Ordering for the scanned list: case-insensitive, with a bytewise tie-break
   so case-equal names ("A.svg" vs "a.svg") still sort deterministically
   (qsort itself is unstable). */
static int name_cmp(const void *a, const void *b) {
    const char *x = (const char *)a, *y = (const char *)b;
    for (; *x && *y; x++, y++) {
        int cx = tolower((unsigned char)*x), cy = tolower((unsigned char)*y);
        if (cx != cy) return cx - cy;
    }
    if (*x || *y) return (unsigned char)*x - (unsigned char)*y;
    return strcmp((const char *)a, (const char *)b);
}

/* Scan st->inDir for *.svg into st->files[], sorted, cap ICONV_MAX_FILES.
   Clears any prior scan + selection first. Logs the outcome. */
static void scan_dir(AppState *st) {
    st->fileCount = 0;
    st->selected  = -1;
    st->haveIcon  = false;

    const char *dir = st->inDir[0] ? st->inDir : ".";
#ifdef _WIN32
    char pat[ICONV_DIR_CAP + 8];
    snprintf(pat, sizeof pat, "%s\\*.svg", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        /* No *.svg match is a normal empty scan, not an open failure. */
        if (GetLastError() != ERROR_FILE_NOT_FOUND) {
            log_line(st, "scan: cannot open directory");
            return;
        }
    } else {
        do {
            if (st->fileCount >= ICONV_MAX_FILES) break;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (!ends_with_svg(fd.cFileName)) continue;
            snprintf(st->files[st->fileCount], ICONV_NAME_CAP, "%.*s",
                     ICONV_NAME_CAP - 1, fd.cFileName);
            st->fileCount++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(dir);
    if (!d) {
        log_line(st, "scan: cannot open directory");
        return;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && st->fileCount < ICONV_MAX_FILES) {
        if (!ends_with_svg(ent->d_name)) continue;
        snprintf(st->files[st->fileCount], ICONV_NAME_CAP, "%.*s",
                 ICONV_NAME_CAP - 1, ent->d_name);
        st->fileCount++;
    }
    closedir(d);
#endif

    if (st->fileCount > 1) {
        qsort(st->files, (size_t)st->fileCount, ICONV_NAME_CAP, name_cmp);
    }

    log_fmt(st, "scanned %.110s: %d svg file(s)", dir, st->fileCount);
}

/* Join dir + '/' + leaf into out[cap] (skips a redundant separator). */
static void path_join(char *out, int cap, const char *dir, const char *leaf) {
    if (dir && dir[0]) {
        size_t n = strlen(dir);
        char sep = (dir[n - 1] == '/' || dir[n - 1] == '\\') ? '\0' : '/';
        if (sep) snprintf(out, cap, "%s/%s", dir, leaf);
        else     snprintf(out, cap, "%s%s", dir, leaf);
    } else {
        snprintf(out, cap, "%s", leaf);
    }
}

/* mkdir the export dir (idempotent; ignores "already exists"). */
static void ensure_dir(const char *dir) {
    if (!dir || !dir[0]) return;
#ifdef _WIN32
    _mkdir(dir);
#else
    mkdir(dir, 0755);
#endif
}

/* Read a whole file into buf[cap] (NUL-terminated). Returns bytes read, or -1
   on any failure. Truncates silently to cap-1 if larger (bounded). */
static int read_file(const char *path, char *buf, int cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t got = fread(buf, 1, (size_t)(cap - 1), f);
    fclose(f);
    buf[got] = '\0';
    return (int)got;
}

/* ── selection / export actions ─────────────────────────────────────────────
   These run only on user action (click / button), never per frame. */

/* Read + parse files[idx] out of the input directory into *out. Both actions
   below open the same way, and sharing the preamble is what keeps the tool to
   ONE ICONV_SVG_CAP buffer: a second copy of it here was 64 KB of .bss the tool
   never used at the same time. Returns false, having logged why, on failure. */
static bool load_svg_icon(AppState *st, int idx, RcSvgIcon *out) {
    char path[ICONV_DIR_CAP + ICONV_NAME_CAP];
    path_join(path, (int)sizeof path, st->inDir[0] ? st->inDir : ".",
              st->files[idx]);

    static char svg[ICONV_SVG_CAP];   /* one static buffer (shared across calls): bounded, off the stack */
    int len = read_file(path, svg, ICONV_SVG_CAP);
    if (len < 0) {
        log_fmt(st, "open failed: %s", st->files[idx]);
        return false;
    }
    if (!rc_svg2icon_parse(svg, len, 16, 7.5f, out) || !out->ok) {
        log_fmt(st, "parse failed: %.60s (%.80s)", st->files[idx], out->err);
        return false;
    }
    return true;
}

/* Load + parse files[idx] into st->icon, updating the preview. Logs failures. */
static void select_file(AppState *st, int idx) {
    if (idx < 0 || idx >= st->fileCount) return;
    st->selected = idx;
    st->haveIcon = load_svg_icon(st, idx, &st->icon);
}

/* Parse + emit one scanned file to <outDir>/<stem>.h. Logs the outcome. */
static void export_one(AppState *st, int idx) {
    if (idx < 0 || idx >= st->fileCount) return;

    static RcSvgIcon tmp;   /* keep the big struct off the stack */
    if (!load_svg_icon(st, idx, &tmp)) return;

    /* Lowercase stem: names the output file (<stem>.h) AND seeds the icon name -
       rc_svg2icon_emit PascalCases it, so the one canonical transform lives there.

       The trap: strip the extension, not the first dot. A dot is legal anywhere
       in a file name, and "chevron.right.svg" is an ordinary way to name an
       asset. Halting at the first '.' made its stem "chevron", so it wrote
       chevron.h - silently overwriting whatever "chevron.svg" had produced
       moments earlier, because the writer below opens with "wb". The boundary is
       CHECKED rather than assumed: scan_dir only admits names ends_with_svg
       accepts, and this asks the same function rather than trusting that it did.
       Note: a dot surviving in the stem is harmless downstream - rc_svg__pascal
       splits on every non-alnum byte, so "chevron.right" emits ChevronRight. */
    const char *name    = st->files[idx];
    size_t      nameLen = strlen(name);
    size_t      stemLen = ends_with_svg(name) ? nameLen - 4 : nameLen;
    char        stem[ICONV_NAME_CAP];
    int         si = 0;

    for (size_t i = 0; i < stemLen; i++) {
        if (si < (int)sizeof stem - 1) stem[si++] = (char)tolower((unsigned char)name[i]);
    }
    stem[si] = '\0';
    if (si == 0) snprintf(stem, sizeof stem, "icon");

    static char emit[ICONV_EMIT_CAP];
    int written = rc_svg2icon_emit(&tmp, stem, st->files[idx], 3,
                                   emit, ICONV_EMIT_CAP);
    if (written < 0) {
        log_fmt(st, "emit overflow: %s", st->files[idx]);
        return;
    }

    ensure_dir(st->outDir[0] ? st->outDir : "out");

    char outLeaf[ICONV_NAME_CAP];
    /* Cap the stem so the ".h" suffix always fits (gcc format-truncation). */
    snprintf(outLeaf, sizeof outLeaf, "%.*s.h", (int)sizeof outLeaf - 3, stem);
    char outPath[ICONV_DIR_CAP + ICONV_NAME_CAP];
    path_join(outPath, (int)sizeof outPath, st->outDir[0] ? st->outDir : "out",
              outLeaf);

    FILE *of = fopen(outPath, "wb");
    if (!of) {
        log_fmt(st, "write failed: %s.h", stem);
        return;
    }
    size_t wrote = fwrite(emit, 1, (size_t)written, of);
    fclose(of);

    if (wrote == (size_t)written) log_fmt(st, "generated %s.h", stem);
    else                          log_fmt(st, "short write: %s.h", stem);
}

/* Export every scanned file. Each failure logs + continues (never aborts). */
static void export_all(AppState *st) {
    if (st->fileCount == 0) {
        log_line(st, "export all: nothing scanned");
        return;
    }
    for (int i = 0; i < st->fileCount; i++) export_one(st, i);
    log_fmt(st, "export all: %d file(s) processed", st->fileCount);
}

/* ── live preview custom element ─────────────────────────────────────────────
   rcIconEmit emits a size x size CUSTOM element whose callback draws the parsed
   ops exactly as a generated header would - via rc_svg2icon_draw. */
static void preview_cb(RC_BoundingBox b, RC_Color c, const void *ud) {
    rc_svg2icon_draw((const RcSvgIcon *)ud, b, c);
}

/* ── left column: sources ───────────────────────────────────────────────────*/

static void panel_sources(AppState *st) {
    RC_Style s = rcGetStyle();
    rcColumn(.id = "col_src", .w = "280px", .h = "grow",
              .bg = s.surface, .p = 14, .gap = 10,
              .border = { .color = s.border, .width = "1px" }) {
        rcTextL("Sources", .font = F_TITLE, .color = s.text);
        rcTextL("Input directory", .font = F_SMALL, .color = s.textMuted);
        rcBox(.w = "grow") {
            rcTextInput("in_dir", st->inDir, sizeof st->inDir,
                         .placeholder = "./icons");
        }
        if (rcButton("btn_scan", "Scan", RC_BTN_PRIMARY)) scan_dir(st);

        /* Only emit the hint when empty; a "" text node is a dead gap otherwise. */
        if (!st->fileCount) {
            rcTextL("(no *.svg found - Scan a directory)",
                     .font = F_SMALL, .color = s.textMuted);
        }

        /* Scrollable, selectable file list. */
        rcColumn(.id = "list_files", .w = "grow", .h = "grow", .scroll = "v",
                  .bg = s.surfaceAlt, .p = 6, .gap = 2,
                  .borderRadius = "all-md") {
            for (int i = 0; i < st->fileCount; i++) {
                bool sel = (i == st->selected);
                rcRow(.id = st->rowId[i], .w = "grow", .h = "30px", .px = 10,
                       .align = "cl",
                       .bg = sel ? s.primary
                                 : (rcIsHovered(st->rowId[i]) ? s.surface
                                                               : RC_TRANSPARENT),
                       .borderRadius = "all-sm") {
                    rcTextC(st->files[i], .font = F_BODY,
                             .color = sel ? RC_WHITE : s.text, .wrap = "n");
                }
                if (rcClicked(st->rowId[i])) select_file(st, i);
            }
        }
        /* The scrollbar for "list_files" is declared once in layout(). */
    }
}

/* ── centre column: live preview ────────────────────────────────────────────
   The preview panel itself is inlined into layout() (below) so rcIconEmit and
   the arena-backed metadata share one column; this helper draws just the
   arena-backed metadata + warnings (rcFormat needs the app frame arena). */
static void preview_meta(RC_App *app, AppState *st) {
    RC_Style s = rcGetStyle();
    if (!st->haveIcon) return;
    const RcSvgIcon *ic = &st->icon;
    RC_Arena *ar = rcAppArena(app);

    RC_String name = rcFormat(ar, "file: %s",
                                 st->selected >= 0 ? st->files[st->selected] : "?");
    RC_String view = rcFormat(ar, "viewBox: %.0f x %.0f", ic->viewW, ic->viewH);
    RC_String info = rcFormat(ar, "ops: %d  \xc2\xb7  %s", ic->opCount,
                                 ic->colored ? "colored" : "mono");
    rcText(name, .font = F_SMALL, .color = s.textMuted);
    rcText(view, .font = F_SMALL, .color = s.textMuted);
    rcText(info, .font = F_SMALL, .color = s.textMuted);
    for (int i = 0; i < ic->warnCount && i < RC_SVG_MAX_WARN; i++) {
        RC_String w = rcFormat(ar, "warn: %s", ic->warn[i] ? ic->warn[i] : "?");
        rcText(w, .font = F_SMALL, .color = s.danger);
    }
}

/* ── right column: export ───────────────────────────────────────────────────*/

static void panel_export(AppState *st) {
    RC_Style s = rcGetStyle();
    rcColumn(.id = "col_exp", .w = "320px", .h = "grow",
              .bg = s.surface, .p = 14, .gap = 10,
              .border = { .color = s.border, .width = "1px" }) {
        rcTextL("Export", .font = F_TITLE, .color = s.text);
        rcTextL("Output directory", .font = F_SMALL, .color = s.textMuted);
        rcBox(.w = "grow") {
            rcTextInput("out_dir", st->outDir, sizeof st->outDir,
                         .placeholder = "out");
        }
        rcRow(.w = "grow", .gap = 8) {
            if (rcButton("btn_exp_sel", "Export selected", RC_BTN_PRIMARY)) {
                if (st->selected >= 0) export_one(st, st->selected);
                else                   log_line(st, "export: no file selected");
            }
            if (rcButton("btn_exp_all", "Export all", RC_BTN_DEFAULT)) {
                export_all(st);
            }
        }

        rcTextL("Status log", .font = F_SMALL, .color = s.textMuted);
        rcColumn(.id = "list_log", .w = "grow", .h = "grow", .scroll = "v",
                  .bg = s.surfaceAlt, .p = 8, .gap = 2,
                  .borderRadius = "all-md") {
            if (st->logCount == 0) {
                rcTextL("(no activity yet)", .font = F_SMALL, .color = s.textMuted);
            }
            for (int i = 0; i < st->logCount; i++) {
                int idx = (st->logHead + i) % ICONV_LOG_LINES;
                /* rcTextC BORROWS the pointer, which is exactly right here: the
                   ring lives in the app's own static state, so it outlives the
                   frame. No arena copy needed just to reach an RC_String. */
                rcTextC(st->log[idx], .font = F_SMALL, .color = s.text, .wrap = "n");
            }
        }
        /* Scrollbar overlay for "list_log" is drawn once in layout(). */
    }
}

/* ── app callbacks ──────────────────────────────────────────────────────────*/

static void update(RC_App *app, void *userData) {
    (void)app;
    AppState *st = (AppState *)userData;
    /* Deferred first-frame scan seeded from argv[1]. */
    if (st->pendingScan) {
        st->pendingScan = false;
        scan_dir(st);
    }
}

/* ── custom titlebar glyphs (RC_TitlebarButtonIcons) ────────────────────────*/
/*
    This app draws NO titlebar of its own - the runner draws the bundled bar. But
    the bar's glyphs are replaceable PER BUTTON, and this is the example that
    ought to show it: every header this tool writes exposes
    `void <name>(float size, RC_Color color)`, and that IS RC_IconCallback. So a
    header produced by this converter drops straight onto a window control with
    no adapter.

    The pair below is the same shape every bundled glyph uses
    (rayclay/icons/rc_icons_titlebar_*.h): a draw callback that lays strokes out
    in a square viewBox, and a thin RC_IconCallback wrapper handing it to
    rcIconEmit. Coordinates are viewBox units, so one set of numbers is correct
    at every icon size and at every zoom.

    THREE RULES THE BAR APPLIES, and all three are visible in this window:
      1. a NULL state falls back to `normal` - maximize sets only .normal below,
         so its hover and press reuse that one glyph;
      2. an ALL-NULL set keeps the bundled glyph - close is left untouched;
      3. the glyph is drawn in the COLOUR THE BAR PASSES IN (the style's text
         colour, lifted toward white as the hover fill strengthens) => a glyph
         must honour its `color` argument and never choose its own, or it will
         go invisible against the filled slab.

    WARN close is deliberately NOT overridden. Minimize and maximize are safe to
    restyle; the control a user reaches for when an app misbehaves is not. Leave
    it conventional unless you have a strong reason not to.
*/
enum { TBAR_VIEWBOX = 16 };          /* the bundled glyphs' viewBox, matched */

static void tbar_draw_minimize(RC_BoundingBox bounds, RC_Color color,
                               const void *userData) {
    (void)userData;
    const float vb = (float)TBAR_VIEWBOX, stroke = 2.05f;

    /* A downward chevron rather than the bundled dash - different enough that
       "did the override take?" is answered by looking at the window. */
    rcIconDrawRoundLine(bounds,  4.5f, 6.0f,  8.0f, 9.5f, vb, stroke, color);
    rcIconDrawRoundLine(bounds,  8.0f, 9.5f, 11.5f, 6.0f, vb, stroke, color);
}

static void tbar_draw_minimize_hover(RC_BoundingBox bounds, RC_Color color,
                                     const void *userData) {
    (void)userData;
    const float vb = (float)TBAR_VIEWBOX, stroke = 2.05f;

    /* The hover state: the same chevron, plus the rail it collapses onto. Two
       glyphs for one button is the whole point of the per-state struct. */
    rcIconDrawRoundLine(bounds,  4.5f, 5.0f,  8.0f, 8.5f, vb, stroke, color);
    rcIconDrawRoundLine(bounds,  8.0f, 8.5f, 11.5f, 5.0f, vb, stroke, color);
    rcIconDrawRoundLine(bounds,  4.5f, 11.5f, 11.5f, 11.5f, vb, stroke, color);
}

static void tbar_draw_maximize(RC_BoundingBox bounds, RC_Color color,
                               const void *userData) {
    (void)userData;
    rcIconDrawRoundedRectStroke(bounds, 4.0f, 4.0f, 8.0f, 8.0f, 1.6f,
                                (float)TBAR_VIEWBOX, 1.9f, color);
}

/* The RC_IconCallback wrappers. Thin by design: rcIconEmit is what places the
   glyph and applies the unzoom scale, so a wrapper that did more than this
   would be doing the bar's job. */
static void tbar_minimize(float size, RC_Color color) {
    rcIconEmit(size, color, tbar_draw_minimize, NULL);
}

static void tbar_minimize_hover(float size, RC_Color color) {
    rcIconEmit(size, color, tbar_draw_minimize_hover, NULL);
}

static void tbar_maximize(float size, RC_Color color) {
    rcIconEmit(size, color, tbar_draw_maximize, NULL);
}

static void layout(RC_App *app, void *userData) {
    AppState *st = (AppState *)userData;
    rcSetStyle(rcStyleDark());
    RC_Style s = rcGetStyle();

    /* No hand-rolled titlebar: the runner draws the BUNDLED bar (title +
       window controls) above this layout under nativeFrame - zero app code. */
    rcColumn(.id = "Root", .w = "grow", .h = "grow", .bg = s.background) {
        rcRow(.id = "Body", .w = "grow", .h = "grow") {
            panel_sources(st);
            /* Centre = preview + its arena-backed metadata, stacked. */
            rcColumn(.id = "col_center", .w = "grow", .h = "grow",
                      .bg = s.background, .p = 18, .gap = 12, .align = "tc") {
                rcTextL("Preview", .font = F_TITLE, .color = s.text);
                /* The toggle flips the whole card between a dark-on-light and a
                   light-on-dark rendering so an icon can be checked both ways:
                   a dark stroke wants a light card, a light stroke the surface. */
                RC_Color prevBg    = st->strokeDark ? RC_WHITE : s.surface;
                RC_Color strokeCol = st->strokeDark ? s.background : RC_WHITE;
                rcColumn(.id = "prev_panel", .w = "grow", .h = "240px",
                          .bg = prevBg, .align = "cc",
                          .borderRadius = "all-xl",
                          .border = { .color = s.border, .width = "1px" }) {
                    if (st->haveIcon) {
                        rcIconEmit(180.0f, strokeCol, preview_cb, &st->icon);
                    } else {
                        rcTextL("Select an SVG to preview",
                                 .font = F_SMALL, .color = s.textMuted);
                    }
                }
                rcRow(.gap = 8, .align = "cl") {
                    rcTextL("Stroke", .font = F_SMALL, .color = s.textMuted);
                    rcTextC(st->strokeDark ? "dark" : "light",
                             .font = F_SMALL, .color = s.textMuted);
                    rcToggle("tg_stroke", &st->strokeDark);
                }
                rcColumn(.w = "grow", .gap = 2) {
                    preview_meta(app, st);
                }
            }
            panel_export(st);
        }
    }
    rcScrollbar("list_files");
    rcScrollbar("list_log");
}

/* ── entry point ────────────────────────────────────────────────────────────*/

int main(int argc, char **argv) {
    static AppState state;   /* large fixed arrays => keep it out of the stack */
    state.selected   = -1;
    state.strokeDark = true;

    /* Pre-format stable per-row element ids ("f0".."f255"). */
    for (int i = 0; i < ICONV_MAX_FILES; i++) {
        snprintf(state.rowId[i], sizeof state.rowId[i], "f%d", i);
    }

    /* Input-dir default: argv[1] if given, else the examples' shared assets
       folder (a RELATIVE repo path - a shipped binary is pointed elsewhere via
       argv[1] or the in-app field). Kept a plain source literal so no build-time
       absolute / home path is baked into the binary. */
    if (argc > 1 && argv[1] && argv[1][0]) {
        snprintf(state.inDir, sizeof state.inDir, "%s", argv[1]);
        state.pendingScan = true;   /* scan on the first frame */
    } else {
        snprintf(state.inDir, sizeof state.inDir, "%s", "examples/assets/icons");
    }
    snprintf(state.outDir, sizeof state.outDir, "%s", "out");

    static const float fontSizes[F_COUNT] = {
        [F_SMALL] = 13.0f,
        [F_BODY]  = 15.0f,
        [F_TITLE] = 20.0f,
    };

    rcSetStyle(rcStyleDark());

    RC_AppOptions opts = {
        .width            = 1000,
        .height           = 680,
        .title            = "RayClay Icon Converter",
        .clearColor       = rcGetStyle().background,
        .fontSizes        = fontSizes,
        .fontCount        = F_COUNT,
        .scratchArenaBytes = 16384,
        .nativeFrame      = true,   /* borderless + the BUNDLED titlebar (runner-drawn) */
        /* Per-button glyph overrides on that bundled bar - see the block above
           tbar_draw_minimize for the three fallback rules this exercises.
           `close` is absent on purpose: an all-NULL set keeps the bundled glyph. */
        .titlebar = {
            .minimize = { .normal = tbar_minimize, .hover = tbar_minimize_hover },
            .maximize = { .normal = tbar_maximize },
        },
        .updateCallback         = update,
        .layoutCallback         = layout,
        .userData         = &state,
    };

    return rcRunApp(&opts);
}
