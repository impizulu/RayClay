/*
================================================================================
    main.c - SVG Live: the three ways to put vector art on screen, side by side
================================================================================

    RayClay draws the same vector artwork by three routes. They are not
    alternatives you pick at random - each is right for a different job - and
    they are indistinguishable on screen, which is exactly why the choice is
    hard to see. This app puts all three next to each other on ONE artwork:

      BY PATH     point at the .svg, in the layout, like <img src> in HTML.
                    -> rcSvg("assets/icons/settings.svg", size, color)
                  Nothing in your app struct, nothing to load, nothing to free.
                  THIS IS THE DEFAULT and it is what you want almost always.

      FROM A      you call rcLoadSvg / rcLoadSvgFromMemory yourself and hold the
      HANDLE      handle, so the lifetime is yours.
                    -> rcSvgHandle(svg, size, color)
                  For markup with no file behind it - generated at run time, or
                  a string in your source - and for freeing on your own schedule.

      GENERATED   the .svg converted at BUILD time into a C header of icon ops
                  (tools/svg_to_rayclay_icon.py, or ex11). Compiled in: no file
                  to ship, no parser in the binary, nothing that can fail.
                    -> rcIconSettings(size, color)

    All three end in the same draw path, so the three panels below should be
    identical. That is the point: choosing between them is a question about YOUR
    BUILD, not about how it looks.

    Watch the "Unload" button - it is the whole lesson in one click. It frees
    the handle behind "Drawn from a handle you own"; "Drawn straight from the
    file" keeps drawing, because the library owns that one and you own this one.
    (Naming the panels rather than numbering them is deliberate: reorder PANEL[]
    below and a sentence counting from the left would quietly become wrong.)

    RayClay does not rasterise SVG - it converts shapes into icon ops. Paths,
    line, polyline, polygon, rect, circle and ellipse work; gradients, <text>,
    filters and embedded images are skipped with a warning naming the way out.
    For artwork needing those, export a PNG and use rcLoadImage.

    Build target: rayclay_ex24_svg_live
================================================================================
*/
#include "rayclay.h"

#include "icons/rc_icons_chart_column.h"
#include "icons/rc_icons_expand.h"
#include "icons/rc_icons_folder.h"
#include "icons/rc_icons_maximize.h"
#include "icons/rc_icons_minimize.h"
#include "icons/rc_icons_minus.h"
#include "icons/rc_icons_panel_left.h"
#include "icons/rc_icons_panel_right.h"
#include "icons/rc_icons_settings.h"
#include "icons/rc_icons_shrink.h"
#include "icons/rc_icons_x.h"

/* Where the .svg sources live. Repo-relative by default so the app runs from
   the repo root exactly as the docs say; the web build preloads them under
   /assets/icons/ and overrides this. Never use an absolute path - it would bake
   this machine's home directory into the binary. */
#ifndef RC_EX24_SVG_DIR
    #define RC_EX24_SVG_DIR "examples/assets/icons/"
#endif

enum { BAR_H = 44, RAIL_W = 216, PAD = 18 };

/* One row of the rail: the same artwork reachable by every route we can offer.
   `draw` is the generated header's function; `file` is the .svg it came from. */
typedef struct {
    const char *label;
    const char *file;                       /* NULL => lives only in the source */
    void      (*draw)(float, RC_Color);     /* NULL => no generated header      */
} Art;

/* An SVG written into the source, not read from disk - what rcLoadSvgFromMemory
   is for. There is no path to point at, so it is the one artwork here the BY
   PATH route cannot draw, which is precisely when you reach for a handle. */
static const char INLINE_SVG[] =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32' fill='none'"
    " stroke='currentcolor' stroke-linecap='round' stroke-linejoin='round' stroke-width='2'>"
    "<path d='M13 2 L13 6 11 7 8 4 4 8 7 11 6 13 2 13 2 19 6 19 7 21 4 24 8 28 11 25 13 26"
    " 13 30 19 30 19 26 21 25 24 28 28 24 25 21 26 19 30 19 30 13 26 13 25 11 28 8 24 4 21 7"
    " 19 6 19 2 Z'/>"
    "<circle cx='16' cy='16' r='4'/>"
    "</svg>";

/* Paths are joined by the preprocessor (adjacent string literals concatenate),
   so a path costs no run-time formatting and no scratch arena. The file entry
   is first and is what the app opens on, deliberately: an example teaches by
   weight as much as by words, and rcSvg(path) is the route to reach for. */
static const Art ART[] = {
    { "settings",     RC_EX24_SVG_DIR "settings.svg",     rcIconSettings    },
    { "settings (in C source)", NULL,                     rcIconSettings    },
    { "folder",       RC_EX24_SVG_DIR "folder.svg",       rcIconFolder      },
    { "chart-column", RC_EX24_SVG_DIR "chart-column.svg", rcIconChartColumn },
    { "panel-left",   RC_EX24_SVG_DIR "panel-left.svg",   rcIconPanelLeft   },
    { "panel-right",  RC_EX24_SVG_DIR "panel-right.svg",  rcIconPanelRight  },
    { "maximize",     RC_EX24_SVG_DIR "maximize.svg",     rcIconMaximize    },
    { "minimize",     RC_EX24_SVG_DIR "minimize.svg",     rcIconMinimize    },
    { "expand",       RC_EX24_SVG_DIR "expand.svg",       rcIconExpand      },
    { "shrink",       RC_EX24_SVG_DIR "shrink.svg",       rcIconShrink      },
    { "minus",        RC_EX24_SVG_DIR "minus.svg",        rcIconMinus       },
    { "x",            RC_EX24_SVG_DIR "x.svg",            rcIconX           },
};
enum { ART_COUNT = (int)(sizeof ART / sizeof ART[0]) };

/* Six tints, so "colour is a per-call argument" is something you do, not read. */
static const RC_Color TINTS[] = {
    { 226, 232, 240, 255 }, { 124,  92, 255, 255 }, {  34, 211, 238, 255 },
    {  52, 211, 153, 255 }, { 251, 191,  36, 255 }, { 248, 113, 113, 255 },
};
enum { TINT_COUNT = (int)(sizeof TINTS / sizeof TINTS[0]) };

/* The three routes, in the order a developer should consider them. */
enum { R_PATH, R_OWNED, R_GEN, R_COUNT };

static const struct {
    const char *id;
    const char *title;
} PANEL[R_COUNT] = {
    { "pan_path",  "Drawn straight from the file" },
    { "pan_owned", "Drawn from a handle you own"  },
    { "pan_gen",   "Generated at build time"      },
};

typedef struct {
    RC_Svg *svg;        /* the HANDLE route only; swapped, never accumulated */
    int     sel;        /* index into ART                                    */
    int     tint;
    float   size;       /* points, driven by the slider                      */
    bool    unloadPending;  /* see the Unload button; freed at the NEXT frame */
    bool    loadFailed;     /* the LAST load attempt returned NULL - diagnostic
                               only. st->svg remains the lifetime answer; this
                               exists so a failed load and a deliberate Unload
                               do not read identically to the user. */
} AppState;

/* Load the selected artwork into the handle we own, replacing whatever was
   live. The order matters: unload first, so only one handle is ever alive.
   rcUnloadSvg NULLs the pointer for us, which is why the double-free this shape
   usually invites cannot happen - and why st->svg alone is the "is it loaded?"
   answer, with no second flag to fall out of step with it. */
static void load_selected(AppState *st)
{
    rcUnloadSvg(&st->svg);
    st->svg = ART[st->sel].file
           ? rcLoadSvg(ART[st->sel].file)
           : rcLoadSvgFromMemory(INLINE_SVG, (int)(sizeof INLINE_SVG - 1));
    st->loadFailed = (st->svg == NULL);
}

/* One preview panel: a titled card with the artwork centred inside it. The
   route is passed in rather than the drawing, so all three call sites sit in
   one switch and can be read against each other - which is the app. */
static void panel(RC_App *app, AppState *st, int route)
{
    const Art  *art = &ART[st->sel];
    RC_Style    s   = rcGetStyle();
    const char *call;

    switch (route) {
    case R_PATH:
        call = art->file ? rcFormat(rcAppArena(app), "rcSvg(\"%s\", size, color)",
                                    art->file).chars
                         : "no file to point at - this one is a C string";
        break;
    case R_OWNED:
        call = art->file ? "rcLoadSvg(path) -> rcSvgHandle(svg, size, color)"
                         : "rcLoadSvgFromMemory(bytes) -> rcSvgHandle(...)";
        break;
    default:
        call = art->draw ? "rcIcon<Name>(size, color) - compiled in"
                         : "no generated header for this one";
        break;
    }

    rcColumn(.id = PANEL[route].id, .w = "grow", .h = "grow", .bg = s.surface,
             .p = PAD, .gap = 10, .align = "tc", .borderRadius = "all-lg",
             .border = { .color = s.border, .width = "1px" }) {
        rcTextC(PANEL[route].title, .color = s.text);
        rcTextC(call, .color = s.textMuted);
        rcBox(.w = "grow", .h = "grow", .align = "cc") {
            if (route == R_PATH && art->file) {
                rcSvg(art->file, st->size, TINTS[st->tint]);
            } else if (route == R_OWNED && st->svg) {
                rcSvgHandle(st->svg, st->size, TINTS[st->tint]);
            } else if (route == R_GEN && art->draw) {
                art->draw(st->size, TINTS[st->tint]);
            } else if (route == R_OWNED) {
                rcTextC("handle freed - press Load", .color = s.textMuted);
            }
        }
    }
}

static void layout(RC_App *app, void *userData)
{
    AppState *st = (AppState *)userData;
    RC_Style s = rcGetStyle();

    /* Free at the top of a frame, never inside the one that drew it.
       rcSvgHandle does not copy the artwork: it hands the layout a custom-draw
       element that still points at your RC_Svg, and the drawing happens after
       the callback returns. Freeing the handle further down the same callback
       would hand the renderer a dangling pointer. Deferring by one frame is the
       whole fix, and it is the same shape as deferring a list deletion. */
    if (st->unloadPending) {
        rcUnloadSvg(&st->svg);
        st->unloadPending = false;
    }

    rcColumn(.id = "root", .w = "grow", .h = "grow", .bg = s.background) {
        /* Custom titlebar: the band is the drag region (.titlebar.custom).
           The app draws its own chrome icon with the route it teaches - one
           line, no state. It names the same file the panel does, and the file
           is still parsed exactly once: the cache is keyed on the path.

           Load-bearing: the id is the behaviour. Under nativeFrame the OS
           chrome is gone, and RayClay grants a drag to exactly one thing: an
           element tagged RC_ID_WINDOW_DRAG. Any other id is treated as ordinary
           client area, so a band tagged anything else leaves the window
           unmovable. The comment above claimed the band was the drag region
           while the code never said so, which is why nothing caught it.

           Chrome, not content: .titlebarHeight freezes that strip in physical
           px, so the band must not grow with the content zoom or the visible bar
           and the draggable one drift apart. */
        rcUnzoomed() {
            rcRow(.id = RC_ID_WINDOW_DRAG, .w = "grow", .hType = RC_PX(BAR_H),
                  .px = 14, .gap = 10, .align = "cl", .bg = s.surface) {
                rcSvg(RC_EX24_SVG_DIR "settings.svg", 20.0f, s.text);
                rcTextL("SVG Live", .color = s.text);
                rcBox(.w = "grow") {}
                rcWindowControls();
            }
        }

        rcRow(.w = "grow", .h = "grow", .p = PAD, .gap = PAD) {
            /* ── rail: pick the artwork. The path panel follows with no load
                  call at all; only the handle panel has anything to do. ── */
            rcColumn(.id = "rail", .wType = RC_PX(RAIL_W), .h = "grow", .gap = 4,
                     .bg = s.surface, .p = 10, .borderRadius = "all-lg") {
                rcTextC("ARTWORK", .color = s.textMuted);
                for (int i = 0; i < ART_COUNT; i++) {
                    if (rcButton(rcFormat(rcAppArena(app), "art%d", i).chars,
                                 ART[i].label,
                                 i == st->sel ? RC_BTN_PRIMARY : RC_BTN_GHOST)) {
                        st->sel = i;
                        load_selected(st);
                    }
                }
            }

            rcColumn(.w = "grow", .h = "grow", .gap = PAD) {
                rcRow(.w = "grow", .h = "grow", .gap = PAD) {
                    for (int r = 0; r < R_COUNT; r++)
                        panel(app, st, r);
                }

                /* ── controls ─────────────────────────────────────────── */
                rcColumn(.w = "grow", .gap = 12, .bg = s.surface, .p = PAD,
                         .borderRadius = "all-lg") {
                    rcRow(.w = "grow", .gap = 12, .align = "cl") {
                        rcTextC("Size", .color = s.textMuted);
                        rcBox(.w = "grow") { rcSlider("size", &st->size, 24.0f, 220.0f); }
                        rcText(rcFormat(rcAppArena(app), "%d px", (int)st->size),
                               .color = s.text);
                    }
                    rcRow(.w = "grow", .gap = 12, .align = "cl") {
                        rcTextC("Tint", .color = s.textMuted);
                        for (int i = 0; i < TINT_COUNT; i++) {
                            /* rcClicked turns any rcBox into a button, so the SWATCH
                               itself is the control - no separate marker to read. */
                            const char *tid = rcFormat(rcAppArena(app), "t%d", i).chars;
                            rcBox(.id = tid, .wType = RC_PX(26), .hType = RC_PX(26),
                                  .bg = TINTS[i], .borderRadius = "all-sm",
                                  /* .width is a char[12], not a pointer, so it cannot take a
                                     ternary - selection is carried by the border colour alone. */
                                  .border = { .color = i == st->tint ? s.text : s.border,
                                              .width = "2px" }) {}
                            if (rcClicked(tid)) st->tint = i;
                        }
                        rcBox(.w = "grow") {}
                        /* The lifetime is yours on exactly one of the three routes,
                           so the button acts on exactly one of the three panels. */
                        if (rcButton("life", st->svg ? "Unload" : "Load", RC_BTN_DEFAULT)) {
                            if (st->svg) {
                                st->unloadPending = true;   /* freed next frame */
                                st->loadFailed = false;     /* a deliberate free is not a failure */
                            }
                            else
                                load_selected(st);
                            rcAppRequestFrame(app);
                        }
                    }
                    rcTextC(st->loadFailed
                            ? "That file did not open. The path is relative, so launch from the "
                              "repository root - or point RC_EX24_SVG_DIR at your own folder."
                            : !st->svg
                            ? "Handle freed. The other two panels are unaffected - one is the "
                              "library's to free, the other is code."
                            : ART[st->sel].file
                              ? "A file on disk. rcSvg opens it once and caches it; the handle "
                                "beside it is a second, separate copy that you own."
                              : "Markup embedded in the source - one executable, no converter, "
                                "and nothing for rcSvg to point at.",
                            .color = s.textMuted);
                }
            }
        }
    }
}

int main(void)
{
    static AppState state;
    int rc;

    rcSetStyle(rcStyleDark());
    state.tint = 1;
    state.size = 140.0f;
    load_selected(&state);   /* the handle route parses once, here - not per frame */

    RC_AppOptions opts = {
        .width = 1180, .height = 760, .title = "RayClay SVG Live",
        .clearColor = rcGetStyle().background,
        .scratchArenaBytes = 8192,          /* backs every rcFormat in a frame */
        .nativeFrame = true, .titlebarHeight = BAR_H,
        .titlebar = { .custom = true },
        .layoutCallback = layout, .userData = &state,
        .renderMode = RC_RENDER_ON_DEMAND,  /* parks between clicks           */
    };
    rc = rcRunApp(&opts);
    rcUnloadSvg(&state.svg);                    /* ours to free; rcSvg's cache is not */
    return rc;
}
