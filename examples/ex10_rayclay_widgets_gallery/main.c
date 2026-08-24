/*
================================================================================
    main.c - RayClay widgets gallery (the worked example + visual test)
================================================================================

    A thin consumer of the RayClay public C API. It opens one window with
    rcRunApp and lays out a gallery that exercises every render-command
    path the v1 renderer supports so the renderer can be verified by eye:

        RECTANGLE  - colour swatches + filled panels
        rounded    - per-corner radii (all / single side / single corner)
        BORDER     - stroked boxes with assorted widths + radii
        TEXT       - several sizes, colours, and a wrapped multi-line block
        IMAGE      - the one PNG (raster) beside the procedural vector logo
        CUSTOM     - every bundled procedural icon, plus a LIVE section that
                     re-colours them every frame (an icon is code, not pixels)
        SCISSOR    - a fixed-height, vertically scrolled (clipped) list

    plus the native interactive widgets (buttons, checkbox/toggle/radio, text
    inputs, slider, progress, combo, menus, context menu, tooltip, modal), the
    dataviz + layout widgets (rcChart, rcSparkline, RC_Table, RC_SplitPane) and
    the styling extensions (gradient, shadow, overlay, floating) - one labelled
    section each, so any widget can be found and copied from a single file.

    It uses only the RC_ API (no raw GLFW or renderer calls), reads its colours
    from the active RC_Style, and animates a per-frame counter to show the loop is live.

    Zero-asset by design: the bundled font + the procedural icons need no files.
    The one demo PNG is read from RC_DEMO_LOGO (injected by CMake): a
    REPO-RELATIVE path on desktop, so run this one from the repo root; on web it
    is the preloaded VFS path. It degrades to a procedural card when absent.
    Build target: rayclay_ex10_rayclay_widgets_gallery.
================================================================================
*/

#include "rayclay.h"

#include "icons/rc_icons_settings.h"
#include "icons/rc_icons_panel_left.h"
#include "icons/rc_icons_panel_right.h"
#include "icons/rc_icons_minus.h"
#include "icons/rc_icons_x.h"
#include "icons/rc_icons_expand.h"
#include "icons/rc_icons_shrink.h"
#include "icons/rc_icons_maximize.h"
#include "icons/rc_icons_minimize.h"
#include "icons/rc_icons_rayclay_logo.h"        /* full-colour logo: rcIcon...(size)         */
#include "icons/rc_icons_rayclay_logo_mono.h"   /* line-art logo:   rcIcon...(size, colour)  */

/* The one demo image. CMake injects a REPO-RELATIVE path, so rcLoadImage finds
   it only when the gallery is launched from the repository root - that is the
   documented way to run it, and it is also why demo_image.h exists: launched
   from anywhere else the load fails and the section falls back to bytes it
   synthesises in memory, so the picture is never simply missing.
   Everything else is zero-asset: the bundled font + the procedural icons. */
#ifndef RC_DEMO_LOGO
    #define RC_DEMO_LOGO "examples/assets/logos/rayclay-logo-1024.png"
#endif

#include "demo_image.h"

/* Font slots, in the order they are loaded into RC_AppOptions.fontSizes. With no
   fontPath these are baked from the BUNDLED face (zero-asset), so the demo still
   has a real size ladder. The slot index is the .font value in RC_TextOptions. */
typedef enum { F_SMALL = 0, F_BODY, F_TITLE, F_BIG, F_COUNT } AppFont;

/* Room for the zoom-stop labels. RayClay's bundled ladder is Chrome's 17 stops;
   an app may supply its own, so this is a CAP the picker clamps to rather than a
   count it assumes - a longer ladder shows its first ZOOM_STOPS_MAX entries
   instead of writing past the array. */
enum { ZOOM_STOPS_MAX = 24 };

typedef struct {
    long frame;        /* advanced once per update; proves the loop + rcFormat */
    int  clicks;       /* incremented by the Primary/Default buttons            */
    bool showDetails;  /* checkbox state                                        */
    bool darkMode;     /* toggle state - drives the active theme                */
    char name[64];     /* rcTextInput buffer (developer-owned)                 */
    char secret[32];   /* rcTextInput password buffer                          */
    char draft[320];   /* rcTextArea buffer; SEEDED in main() - see below       */
    float volume;      /* rcSlider value (0..1); feeds % readout + inspector   */
    int  quality;      /* rcRadio group selection (0=Low, 1=Medium, 2=High)    */
    int  preset;       /* rcCombo selected index                               */
    bool modalOpen;    /* rcBeginModal open state (demo dialog)                 */
    bool inspectorOpen;   /* NON-modal panel open state (inspector_panel)       */
    bool inspectorSticky; /* -> .noBackdropDismiss; the flag that keeps it open */
    float splitFrac;     /* RC_SplitPane pane-1 share (dataviz showcase)         */
    float prevZoom;      /* last-seen zoom factor (badge change detection)      */
    float zoomBadgeSecs; /* zoom-badge time-to-live in seconds; >0 = visible    */
    bool  opticalZoom;   /* zoom-mode toggle: false = layout reflow, true = optical */
    /* Zoom-stop picker. The LABELS are ours to own because rcCombo borrows its
       items for the frame and wants C strings; the FACTORS are never copied -
       they are read from rcAppZoomLadder every frame, so this cannot drift from
       what the keyboard walks. 17 is Chrome's bundled table; the cap only has
       to hold whatever ladder an app supplies, and it clamps rather than
       overruns if one is longer. */
    int   zoomStop;                              /* index into the resolved ladder */
    char  zoomLabel[ZOOM_STOPS_MAX][8];          /* "125%" + NUL, ours to terminate */
    const char *zoomLabelPtr[ZOOM_STOPS_MAX];    /* the const char *const * rcCombo wants */

    /* Drag-to-zoom demo (section_charts) - the x window over demo_zoom,
       in DATA units. Owning these two floats IS the zoom feature: immediate
       mode re-plots at whatever range they hold. */
    float zoomLo, zoomHi;
    bool  brushing;       /* true between the press edge and the release edge  */
    float brushA, brushB; /* live brush edges, also in DATA units              */
    int   tipPlace;       /* rcCombo index == RC_ChartTooltipPlace (gal_chart)  */
    bool  hoverGuide;     /* -> .hoverGuide:   vertical rule at the hovered x   */
    bool  hoverMarkers;   /* -> .hoverMarkers: colour-matched dot per series    */

    /* Drag-scrub demo (section_gestures) - the pointer + button reads. */
    float    scrub;        /* the value being dragged (0..100)                  */
    float    scrubAtPress; /* its value when this drag started                  */
    float    scrubAnchorX; /* rcPointer().x when this drag started              */
    bool     scrubbing;    /* true between the press edge and the release edge  */
    int      scrubCommits; /* completed drags; proves the release edge fired    */

    /* Keyboard demo (section_keyboard) - edge vs level, and a portable shortcut. */
    int      spacePresses; /* rcKeyPressed edges: one per press, never repeat    */
    int      spaceReleases;/* rcKeyReleased edges: pairs with the above          */
    int      submits;      /* PRIMARY+Enter accelerator fires                    */
    int      nudge;        /* arrows adjust it; see the logical-key note there   */

    /* Clipboard demo (section_clipboard). Nothing is probed at startup - the note
       above section_clipboard explains why every available probe lies on web. */
    int      clipState;    /* 0 = not exercised, 1 = text delivered, 2 = answered with none */
    int      clipCopies;   /* successful rcClipboardSet calls                   */
    RC_ClipboardToken clipToken; /* the read in flight; 0 = none                */
    int      clipWait;     /* frames left before we call the read abandoned     */
    char     clipLast[128];/* last text collected, copied OUT of RayClay memory */

    /* Image-lifecycle demo (section_images). rcUnloadImage is the only call in the
       public API that frees a GPU resource, and the easiest one to forget: measured
       here, re-decoding without it costs ~2.3 MB per load, so a screen that swaps
       images walks past a gigabyte in seconds. The buttons only RECORD an intent;
       update() acts on it, because a layout callback declares a frame rather than
       changing what the frame draws from. */
    int      imageAction; /* 0 = idle, 1 = free the texture, 2 = decode it again  */
    int      imageLoads;  /* successful image decodes this run                    */
    bool     imageFromMem;/* the PNG was not on disk, so the card was synthesised */

    /* Display/scheduling readout (section_display). The frame stamp is what makes
       a park OBSERVABLE: an on-demand app that really slept advances it barely at
       all between arming a wake and being woken by it. */
    bool     continuous;      /* rcAppSetContinuousRendering state (the toggle)   */
    long     wakeArmedFrame;  /* st->frame when rcAppRequestFrameAfter was armed  */
    int      wakesArmed;      /* how many one-shot wakes this run                 */

    /* Live-icon demo (section_live_icons). */
    float    hue;        /* base hue in [0,1), advanced once per update()       */
    float    hueSpeed;   /* hue cycles per second (the Speed slider)            */
    float    hueSpread;  /* per-icon hue offset -> a colour wave across the row */
    bool     hueFrozen;  /* pause, to inspect one frame's colours               */
    bool     animHeld;   /* update()'s rcIsModalOpen() sample - see the note there */
    unsigned rng;        /* xorshift32 state; Randomise re-rolls hue + spread   */

    /* Own-arena demo (section_arena). The RUNNER's arena is reset every frame;
       this one is ours, so what we put in it lives until WE reset it. */
    RC_Arena   logArena; /* created and freed in main()                         */
    RC_String *logLines; /* the ARRAY lives in the arena too - see section_arena */
    int        logCount; /* entries in use, 0..LOG_MAX                          */
    int        logSeq;   /* ever-increasing, so a Clear is visible in the text  */
} AppState;

/* Entries kept by the own-arena demo. Deliberately small: filling it is the point. */
#define LOG_MAX 6

/* ---------------------------------------------------------------------------
   Live-icon support: a PRNG and a hue ramp, both written out longhand because
   examples stay pure-RC_ (no <stdlib.h> rand(), no <math.h> trig).
   --------------------------------------------------------------------------- */

/* xorshift32 (Marsaglia, "Xorshift RNGs") - three shifts, period 2^32-1, never
   returns 0 and never reaches 0 from a non-zero seed. Ample for picking colours,
   and self-contained, so the demo behaves identically on desktop and web. */
static unsigned xorshift32(unsigned *state) {
    unsigned x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* A uniform float in [0,1). Takes the top 24 bits - a float's mantissa width -
   so every value is exactly representable and no rounding collapses two draws. */
static float rng_unit(unsigned *state) {
    return (float)(xorshift32(state) >> 8) * (1.0f / 16777216.0f);
}

/* Fold a hue back into [0,1) with one truncation (callers keep |hue| < 2, so a
   loop would be wasted work). */
static float hue_wrap(float hue) {
    hue -= (float)(int)hue;
    return hue < 0.0f ? hue + 1.0f : hue;
}

/* HSV -> RGB. The hue wheel is six linear ramps, so this needs no trigonometry:
   pick the sextant, then interpolate the one channel that is moving. `hue` must
   be in [0,1) (see hue_wrap); sat and val are in [0,1]. */
static RC_Color hue_color(float hue, float sat, float val) {
    float h = hue * 6.0f;          /* [0,6) -> sextant index + fraction */
    int   i = (int)h;
    float f = h - (float)i;
    float p = val * (1.0f - sat);
    float q = val * (1.0f - sat * f);
    float t = val * (1.0f - sat * (1.0f - f));
    float r, g, b;
    switch (i) {
        case 0:  r = val; g = t;   b = p;   break;
        case 1:  r = q;   g = val; b = p;   break;
        case 2:  r = p;   g = val; b = t;   break;
        case 3:  r = p;   g = q;   b = val; break;
        case 4:  r = t;   g = p;   b = val; break;
        default: r = val; g = p;   b = q;   break;   /* i == 5 (and, defensively, 6) */
    }
    return (RC_Color){ r * 255.0f, g * 255.0f, b * 255.0f, 255.0f };
}

/* A small section heading (muted, all-caps label). */
static void section_heading(const char *title) {
    rcTextC(title, .font = F_SMALL, .color = rcGetStyle().textMuted);
}

/* One fixed-size, medium-rounded colour swatch. (borderRadius is a char[] in
   the DSL, so it must be a literal at the call site - hence varying-radius
   boxes are written out explicitly in section_rounding rather than passed in.) */
static void swatch(RC_Color color) {
    rcBox(.w = "56px", .h = "40px", .bg = color, .borderRadius = "all-md") {}
}

/* One icon tile: a square surface that brightens on hover (rcIsHovered flows
   through the runner's pointer feed end-to-end). The icon's colour is a plain
   argument, so the SAME tile serves the static grid and the live one below. */
static void icon_tile(const char *id, RC_IconCallback icon, RC_Color color) {
    rcBox(.id = id, .w = "44px", .h = "44px", .align = "cc", .borderRadius = "all-lg",
           .bg = rcIsHovered(id) ? rcGetStyle().surfaceAlt : rcGetStyle().surface) {
        icon(22.0f, color);
    }
}

static void section_rectangles(void) {
    rcColumn(.w = "grow", .bg = rcGetStyle().surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("RECTANGLES");
        rcRow(.gap = 10, .align = "cl") {
            swatch(rcGetStyle().primary);
            swatch(rcGetStyle().danger);
            swatch(rcGetStyle().surfaceAlt);
            rcMargin(.w = "24px");   /* rcMargin spacer, here a fixed 24px gap before the named palette */
            swatch(RC_INDIGO_500);
            swatch(RC_EMERALD_500);
            swatch(RC_AMBER_500);
        }
    }
}

static void section_rounding(void) {
    rcColumn(.w = "grow", .bg = rcGetStyle().surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("ROUNDED CORNERS (per-corner)");
        rcRow(.gap = 10, .align = "cl") {
            rcBox(.w = "56px", .h = "40px", .bg = RC_SLATE_600, .borderRadius = "all-sm")  {}
            rcBox(.w = "56px", .h = "40px", .bg = RC_SLATE_600, .borderRadius = "all-lg")  {}
            rcBox(.w = "56px", .h = "40px", .bg = RC_SLATE_600, .borderRadius = "all-2xl") {}
            rcBox(.w = "56px", .h = "40px", .bg = RC_SLATE_600, .borderRadius = "all-full"){}
            rcBox(.w = "56px", .h = "40px", .bg = RC_SLATE_600, .borderRadius = "t-xl")    {} /* top only  */
            rcBox(.w = "56px", .h = "40px", .bg = RC_SLATE_600, .borderRadius = "l-xl")    {} /* left only */
            rcBox(.w = "56px", .h = "40px", .bg = RC_SLATE_600, .borderRadius = "tr-2xl")  {} /* one corner */
        }
    }
}

static void section_gradients(void) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("GRADIENTS (Tier S - per-vertex, no shader)");
        rcRow(.gap = 10, .align = "cl") {
            /* The .gradient field needs an .id (it is keyed by it). Each box
               replaces its flat fill with a two-stop linear gradient. */
            rcBox(.id = "GradV", .w = "56px", .h = "40px", .borderRadius = "all-md",
                   .gradient = { .from = RC_INDIGO_600, .to = RC_ROSE_600,   .dir = "v" }) {}
            rcBox(.id = "GradH", .w = "56px", .h = "40px", .borderRadius = "all-md",
                   .gradient = { .from = RC_EMERALD_500, .to = RC_INDIGO_500, .dir = "h" }) {}
            rcBox(.id = "GradD", .w = "56px", .h = "40px", .borderRadius = "all-md",
                   .gradient = { .from = RC_AMBER_500, .to = RC_ROSE_600,    .dir = "d" }) {}
            rcBox(.id = "GradU", .w = "56px", .h = "40px", .borderRadius = "all-md",
                   .gradient = { .from = RC_INDIGO_500, .to = RC_EMERALD_500, .dir = "u" }) {}
            /* Large radius + pill: the gradient honours the rounded geometry. */
            rcBox(.id = "GradRound", .w = "56px", .h = "40px", .borderRadius = "all-2xl",
                   .gradient = { .from = RC_INDIGO_600, .to = RC_AMBER_500,   .dir = "v" }) {}
            rcBox(.id = "GradPill", .w = "56px", .h = "40px", .borderRadius = "all-full",
                   .gradient = { .from = RC_ROSE_600, .to = RC_INDIGO_600,    .dir = "h" }) {}
        }
    }
}

static void section_shadows(void) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("SHADOWS (Tier S - tessellated alpha-ring, no shader)");
        /* Shadows are keyed by .id and drawn BEHIND the element's fill, so each
           card needs a visible .bg (or .gradient) to anchor the shadow. Extra
           row gap + padding leaves room for the soft edges. */
        rcRow(.gap = 28, .align = "cl", .py = 14) {
            /* Soft drop shadow - the default "card" lift. */
            rcBox(.id = "ShDrop", .w = "64px", .h = "48px", .bg = s.surfaceAlt,
                   .borderRadius = "all-lg",
                   .shadow = { .color = { 0, 0, 0, 110 }, .y = 4, .blur = 12 }) {}
            /* Larger, softer blur. */
            rcBox(.id = "ShSoft", .w = "64px", .h = "48px", .bg = s.surfaceAlt,
                   .borderRadius = "all-lg",
                   .shadow = { .color = { 0, 0, 0, 90 }, .y = 8, .blur = 22 }) {}
            /* Offset to the lower-right (a "lifted off the page" look). */
            rcBox(.id = "ShCast", .w = "64px", .h = "48px", .bg = s.surfaceAlt,
                   .borderRadius = "all-lg",
                   .shadow = { .color = { 0, 0, 0, 120 }, .x = 8, .y = 8, .blur = 10 }) {}
            /* Negative spread - a tight shadow hugging the box. */
            rcBox(.id = "ShTight", .w = "64px", .h = "48px", .bg = s.surfaceAlt,
                   .borderRadius = "all-lg",
                   .shadow = { .color = { 0, 0, 0, 140 }, .y = 6, .blur = 8, .spread = -3 }) {}
            /* Coloured glow: no offset + a wide blur reads as a halo. */
            rcBox(.id = "ShGlow", .w = "64px", .h = "48px", .bg = s.surfaceAlt,
                   .borderRadius = "all-full",
                   .shadow = { .color = { 99, 102, 241, 170 }, .blur = 18 }) {}
            /* Shadow + gradient: the gradient supplies the fill the shadow anchors to. */
            rcBox(.id = "ShGrad", .w = "64px", .h = "48px", .borderRadius = "all-lg",
                   .gradient = { .from = RC_INDIGO_600, .to = RC_ROSE_600, .dir = "v" },
                   .shadow   = { .color = { 0, 0, 0, 120 }, .y = 6, .blur = 14 }) {}
        }
    }
}

/* One mini-card (flat swatches + caption) shown under a given subtree overlay
   tint. The overlay recolours the card background, every swatch, and the caption
   uniformly in one pass - mix(content, overlay.rgb, overlay.a). */
static void overlay_card(const char *label, RC_Color overlay) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "grow", .gap = 6, .p = 10, .bg = s.surfaceAlt,
              .borderRadius = "all-lg", .overlay = overlay) {
        rcRow(.gap = 6) {
            rcBox(.w = "grow", .h = "22px", .bg = RC_INDIGO_500,  .borderRadius = "all-sm") {}
            rcBox(.w = "grow", .h = "22px", .bg = RC_EMERALD_500, .borderRadius = "all-sm") {}
        }
        rcBox(.w = "grow", .h = "22px", .bg = RC_AMBER_500, .borderRadius = "all-sm") {}
        rcTextC(label, .font = F_SMALL, .color = s.text);
    }
}

static void section_overlay(void) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("OVERLAY TINT (Tier S - whole-subtree mix, no shader)");
        /* The same mini-card under four overlay tints: .overlay applies to the
           element AND all its children at once (white lightens, black is a
           scrim, a hue washes). */
        rcRow(.gap = 14, .align = "tl") {
            overlay_card("none",       rcColor("transparent"));
            overlay_card("white 30%",  rcColor("rgba(255,255,255,0.3)"));
            overlay_card("black 45%",  rcColor("rgba(0,0,0,0.45)"));
            overlay_card("indigo 40%", rcColor("rgba(99,102,241,0.4)"));
        }
    }
}

static void section_floating(void) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("FLOATING (anchor a box out of flow - tooltip/menu/popover)");
        /* An anchor "button" with a popover that floats out of layout flow,
           anchored to the button's bottom-left, drawn above sibling content. */
        rcBox(.id = "fl_anchor", .w = "160px", .h = "40px", .bg = s.primary,
               .borderRadius = "all-md", .align = "cc") {
            rcTextL("Anchor button", .color = s.surface);
            rcColumn(.id = "fl_popover",
                      .floating = { .to      = RC_ATTACH_PARENT,
                                    .parent  = RC_ANCHOR_BOTTOM_LEFT,
                                    .element = RC_ANCHOR_TOP_LEFT,
                                    .offset  = { 0, 6 },
                                    .zIndex = 1000 },
                      .bg = s.surfaceAlt, .p = 10, .gap = 4, .borderRadius = "all-md",
                      .border = { .color = s.border, .width = "1px" }) {
                rcTextL("Floating popover", .color = s.text);
                rcTextL("anchored under the button", .color = s.textMuted);
            }
        }
        /* Content below, which the popover overlaps (proving z-order + out-of-flow). */
        rcRow(.gap = 10, .align = "cl") {
            rcBox(.w = "120px", .h = "48px", .bg = s.surfaceAlt, .borderRadius = "all-md") {}
            rcBox(.w = "120px", .h = "48px", .bg = s.surfaceAlt, .borderRadius = "all-md") {}
        }
        /* Tooltip: hover and dwell (~0.5s) to reveal a floating label on top. It
           passes clicks through to whatever is underneath (.tooltip needs an .id). */
        rcBox(.id = "tip_hover", .w = "160px", .h = "40px", .bg = s.surfaceAlt,
               .borderRadius = "all-md", .align = "cc",
               .tooltip = "Tooltips float on top and pass clicks through") {
            rcTextL("Hover me for a tooltip", .color = s.text);
        }

        /* Menu (click to open) + context menu (right-click the target). Both are
           builders on the same floating popup; choosing an item or clicking away
           dismisses. */
        rcRow(.gap = 10, .align = "cl") {
            if (rcBeginMenu("menu_edit", "Edit")) {
                rcMenuItem("Undo");
                rcMenuItem("Redo");
                rcMenuItem("Preferences...");
                rcEndMenu();
            }
            rcBox(.id = "ctx_target", .w = "200px", .h = "40px", .bg = s.surfaceAlt,
                   .borderRadius = "all-md", .align = "cc") {
                rcTextL("Right-click me", .color = s.textMuted);
            }
        }
        if (rcBeginContextMenu("ctx_menu", "ctx_target")) {
            rcMenuItem("Cut");
            rcMenuItem("Copy");
            rcMenuItem("Paste");
            rcEndContextMenu();
        }
    }
}

/* ---------------------------------------------------------------------------
   ZOOM: the end-developer's zoom toolbox in one panel.

   Out of the box a RayClay desktop app zooms like a browser, and the whole
   point of that is that you should not have to be told. This panel exists
   because one thing IS invisible: the RESET binding. Everything here is read
   back from the engine (rcAppZoom / rcAppZoomMode) rather than mirrored from
   app state, so the panel cannot drift from what the window is doing.

   The presets are absolute on purpose. A "zoom in" button would have to
   restate the library's step to agree with the keyboard, and a step restated
   in an example is a step that goes stale the day the default changes.
   rcAppSetZoom takes a factor, so the presets ask for the factor they name and
   cannot disagree with anything.

   There is no pan gesture yet. Under optical zoom the pan is cursor-anchored
   only - it keeps the point under the cursor still across a factor change - so
   magnifying past the window edge gives you nowhere to travel. The slot below
   says so rather than leaving you to discover it.
   --------------------------------------------------------------------------- */
/** Index of the ladder stop nearest `factor`, comparing in LOG space.
 *
 *  Zoom is multiplicative, so 50% is as far from 100% as 100% is from 200% -
 *  a linear |a-b| would call 100% the nearest stop to 145% when 150% is the
 *  obvious answer. Comparing ratios needs no logarithm: for ascending stops the
 *  nearest in log space is whichever of the two neighbours has the smaller
 *  max(a/b, b/a), and that is decided by whether factor^2 exceeds their product.
 */
static int nearest_stop(const float *stops, uint16_t count, float factor)
{
    uint16_t i;

    if (count == 0)
        return 0;
    for (i = 0; i + 1 < count; i++) {
        if (factor * factor <= stops[i] * stops[i + 1])
            return (int)i;
    }
    return (int)count - 1;
}

static void section_zoom(RC_App *app, AppState *st)
{
    RC_Style s      = rcGetStyle();
    float    factor = rcAppZoom(app);
    bool     optical = rcAppZoomMode(app) == RC_ZOOM_OPTICAL;

    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("ZOOM  (rcAppZoom / rcAppSetZoom / rcAppZoomLadder)");

        rcRow(.gap = 14, .align = "cl") {
            rcText(rcFormat(rcAppArena(app), "%.0f%%", factor * 100.0f),
                    .font = F_BIG, .color = s.text);
            rcColumn(.gap = 2) {
                rcTextC(optical ? "optical - magnify the rendered surface"
                                : "layout - reflow, like a browser",
                         .font = F_SMALL, .color = s.textMuted);
                /* Spelled per-target on purpose. The same binary runs on the
                   web, where the BROWSER owns Ctrl +/-/wheel and RayClay
                   deliberately does not intercept them - so a caption that
                   simply promised the shortcuts would be false on one of the
                   two targets this example ships to. The picker below and
                   rcAppSetZoom work everywhere; drag-to-pan does too. */
                rcTextL("Desktop: Ctrl and + / - walk the stops below, Ctrl 0 "
                        "resets to 100%, Ctrl and the wheel is continuous and "
                        "lands between them (Cmd on macOS). Web: the browser "
                        "owns those keys, so use the picker.",
                        .font = F_SMALL, .color = s.textMuted);
            }
        }

        /* THE PRESET LIST IS READ BACK FROM THE ENGINE, NEVER RESTATED.
           Hard-coding 50/100/150/200 here would be a second copy of a table the
           library owns, which is the divergence rcAppZoomLadder exists to
           prevent: change the stops and a restated list keeps offering factors
           the keyboard never visits, with nothing to warn you. So this reads the
           RESOLVED ladder - this app's own .ladder if it set one, RayClay's
           bundled Chrome table otherwise. */
        {
            uint16_t stopCount = 0;
            const float *stops = rcAppZoomLadder(app, &stopCount);

            if (!stops || stopCount == 0) {
                /* The documented "no ladder to show" answer: this app asked for
                   continuous keyboard zoom with .step. Draw NOTHING rather than
                   fall back to a list the keys would not walk - an honest empty
                   is the whole reason the getter can return NULL. */
                rcTextL("continuous keyboard zoom (.zoom.step is set), so there "
                        "are no stops to list.",
                        .font = F_SMALL, .color = s.textMuted);
            } else {
                /* The labels are COPIED into storage this app owns, and LIFETIME
                   is the reason - not termination. rcFormat's .chars is
                   contractually a valid NUL-terminated C string, so it can be
                   handed straight to anything taking a const char *; what it
                   cannot do is survive. It is frame-ARENA memory, gone at the
                   next rcArenaReset, while rcCombo only borrows `items` and its
                   contract requires them to outlive the frame. Point it at the
                   arena and it reads freed bytes. rcStrCopy truncates to fit and
                   always terminates, so the fixed buffers stay safe. */
                if (stopCount > ZOOM_STOPS_MAX)
                    stopCount = ZOOM_STOPS_MAX;
                for (uint16_t i = 0; i < stopCount; i++) {
                    RC_String pct = rcFormat(rcAppArena(app), "%d%%",
                                              (int)(stops[i] * 100.0f + 0.5f));
                    rcStrCopy(st->zoomLabel[i], pct.chars, sizeof st->zoomLabel[i]);
                    st->zoomLabelPtr[i] = st->zoomLabel[i];
                }
                /* Keep the selection honest: the keyboard and the wheel move the
                   factor behind our back, so the combo shows the nearest stop
                   rather than the last thing clicked. */
                st->zoomStop = nearest_stop(stops, stopCount, factor);

                rcRow(.gap = 10, .align = "cl") {
                    rcTextL("Stops", .font = F_SMALL, .color = s.textMuted);
                    rcBox(.w = "110px") {
                        if (rcCombo("cb_zoomstop", &st->zoomStop,
                                     st->zoomLabelPtr, (int)stopCount))
                            rcAppSetZoom(app, stops[st->zoomStop]);
                    }
                    rcToggle("tg_zoommode", &st->opticalZoom);
                    rcTextC(optical ? "Optical" : "Layout",
                             .font = F_SMALL, .color = s.textMuted);
                }
            }
        }

        /* PAN. Enabled for this app in main() - it is off by default, because a
           browser does not pan and that is the out-of-the-box contract. */
        rcRow(.gap = 8, .align = "cl") {
            rcTextL("PAN:", .font = F_SMALL, .color = s.textMuted);
            rcTextC(optical
                    ? "hold Space and drag with the left button, Figma-style. "
                      "Space is suppressed while a text field has focus, so "
                      "typing a space never drags the view."
                    : "layout zoom reflows into the window, so there is nothing "
                      "outside it to reach - switch to optical to pan.",
                    .font = F_SMALL,
                    .color = optical ? s.textMuted : s.warning);
        }
    }
}

/* Loaded once in update() (rcLoadImage needs the renderer up); drawn below.
   Single-instance demo, so a file-static is simpler than threading AppState. */
static RC_Image g_demo_image;

/** Put a picture in g_demo_image, and report whether it came from memory.
 *
 *  The FILE is tried first because that is the call a real app makes, and
 *  because reading a PNG off disk is the thing this section is demonstrating.
 *
 *  RC_DEMO_LOGO is not one path - CMake injects a different one per target.
 *  The web build gets an absolute, root-anchored URL ("/assets/..."), which has
 *  no working directory to get wrong. The NATIVE build gets a path relative to
 *  the repository root, so it only resolves when the process was launched from
 *  there - and RayClay exposes no way to ask where the executable lives.
 *
 *  A synthesised card stands in when the load fails, which keeps the section
 *  truthful from every working directory and happens to demonstrate the second
 *  decoder entry point at the same time.
 */
static bool demo_image_load(void) {
    unsigned char bmp[DEMO_CARD_CAP];   /* ~37 KB, one-shot path only */
    int len;

    g_demo_image = rcLoadImage(RC_DEMO_LOGO);
    if (g_demo_image.handle)
        return false;

    len = demo_card_bmp(bmp, (int)sizeof bmp);
    if (len > 0)
        g_demo_image = rcLoadImageFromMemory(bmp, len);
    return true;
}

static void section_images(RC_App *app, AppState *st) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("IMAGE (raster PNG) vs LOGO (procedural vector)");
        rcRow(.gap = 14, .align = "cl") {
            /* Left: the decoded raster - two sizes + a tint. Whether the bytes
               came off disk or out of demo_image.h, an RC_Image is an RC_Image:
               nothing below this point knows or cares which decoder ran. */
            if (g_demo_image.handle) {
                rcBox(.w = "96px", .h = "96px", .image = &g_demo_image) {}
                rcBox(.w = "56px", .h = "56px", .image = &g_demo_image) {}
                rcBox(.w = "96px", .h = "96px", .image = &g_demo_image,
                       .bg = rcColor("#6366f1c8")) {}
            } else {
                /* Only reachable via "Free texture" below - a failed decode is
                   covered by the fallback, so an empty frame here means the
                   texture was deliberately released. */
                rcBox(.w = "96px", .h = "96px", .align = "cc",
                       .bg = s.surfaceAlt, .borderRadius = "all-lg") {
                    rcTextL("freed", .font = F_SMALL, .color = s.textMuted);
                }
            }
            rcMargin(.w = "16px");
            /* Right: the SAME logo as a resolution-free vector icon (no file).
               rcIconRayClayLogo takes (size) alone because its SVG bakes a
               multi-colour palette - the colours are part of the artwork. The
               line-art variant in LIVE ICONS bakes none, so it takes a colour
               too and can be re-tinted every frame. */
            rcBox(.w = "96px", .h = "96px", .align = "cc") { rcIconRayClayLogo(96.0f); }
            rcBox(.w = "56px", .h = "56px", .align = "cc") { rcIconRayClayLogo(56.0f); }
        }

        /* ------------------------------------------------------------------
           THE LIFECYCLE, which is the part that is easy to get wrong.

           A vector icon costs nothing to "free" - it is code. A raster image is
           a GPU texture plus its decoded pixels, and RayClay hands you exactly
           one call to release it: rcUnloadImage.

           The one thing to copy from this section is the guard in update():
           Re-decode refuses while a texture is still resident. rcLoadImage
           decodes and uploads afresh every call - it does NOT cache by path - so
           assigning a second RC_Image over a live one strands the first texture
           where nothing can ever reach it again.

           And the cost is not just memory. There is a HARD CEILING of 128 live
           images; measured on this very PNG, reloading without freeing succeeds
           126 times and then EVERY later load fails for the rest of the process
           (.handle comes back NULL, permanently). Freeing first, the same run
           does 200 with no failures. Free, then load.
           ------------------------------------------------------------------ */
        rcRow(.gap = 8, .align = "cl") {
            if (rcButton("img_free", "Free texture", RC_BTN_DEFAULT))
                st->imageAction = 1;
            if (rcButton("img_load", "Re-decode", RC_BTN_DEFAULT))
                st->imageAction = 2;
            RC_String tally = rcFormat(rcAppArena(app), "%s   decodes: %d",
                                        g_demo_image.handle ? "resident" : "freed",
                                        st->imageLoads);
            rcText(tally, .font = F_SMALL, .color = s.textMuted);
        }
        /* Name the entry point that produced what is on screen. A demo that
           quietly substitutes one source for another teaches the wrong thing;
           the whole point of the fallback is that you can SEE which ran. */
        if (st->imageFromMem)
            rcTextL("rcLoadImageFromMemory - synthesised in demo_image.h, because "
                    "the PNG is not at the relative path this build was given. "
                    "Launch from the repository root to decode the real file.",
                    .font = F_SMALL, .color = s.warning);
        else
            rcTextL("rcLoadImage - decoded from the PNG on disk.",
                    .font = F_SMALL, .color = s.textMuted);
    }
}

static void section_borders(void) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("BORDERS");
        rcRow(.gap = 10, .align = "cl") {
            rcBox(.w = "56px", .h = "40px", .borderRadius = "all-md",
                   .border = { .color = s.border, .width = "1px" }) {}
            rcBox(.w = "56px", .h = "40px", .borderRadius = "all-lg",
                   .border = { .color = s.primary, .width = "all-2px" }) {}
            rcBox(.w = "56px", .h = "40px", .borderRadius = "all-full",
                   .border = { .color = s.danger, .width = "all-3px" }) {}
            rcBox(.w = "56px", .h = "40px", .bg = s.surfaceAlt, .borderRadius = "all-md",
                   .border = { .color = s.text, .width = "1px" }) {}
        }
    }
}

static void section_text(void) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 10,
              .borderRadius = "all-xl") {
        section_heading("TEXT");
        rcTextL("Big heading", .font = F_BIG, .color = s.text);
        rcTextL("Title text in the primary accent",
            .font = F_TITLE, .color = s.primary);
        rcTextL("Body copy in muted grey. RayClay measures, wraps and draws "
                 "each glyph from a stb_truetype atlas via sokol_gl.",
            .font = F_BODY, .color = s.textMuted, .lineHeight = 26);
        rcTextL("Small label / caption", .font = F_SMALL, .color = s.danger);
        rcTextL("Latin-1: àâäéèêëîïôöùûüç ñ - ¿Olé?  « £ © ® »",
            .font = F_BODY, .color = s.text);
        /* CSS overflow: the dev chooses how content that exceeds the box is
           handled. Here a long unbreakable word (.wrap = "n") is cut off by
           .overflow = "hidden" instead of spilling past the 160px box. */
        rcBox(.w = "160px", .h = "28px", .overflow = "hidden", .px = 8,
               .bg = RC_SLATE_600, .borderRadius = "all-sm", .align = "cl") {
            rcTextL("supercalifragilisticexpialidocious",
                .font = F_SMALL, .color = s.text, .wrap = "n");
        }
    }
}

static void section_icons(void) {
    RC_Color ink = rcGetStyle().text;
    rcColumn(.w = "grow", .bg = rcGetStyle().surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("ICONS (CUSTOM ELEMENTS)");
        rcRow(.gap = 8, .align = "cl") {
            icon_tile("ic_settings",    rcIconSettings,   ink);
            icon_tile("ic_panel_left",  rcIconPanelLeft,  ink);
            icon_tile("ic_panel_right", rcIconPanelRight, ink);
            icon_tile("ic_minus",       rcIconMinus,      ink);
            icon_tile("ic_x",           rcIconX,          ink);
        }
        rcRow(.gap = 8, .align = "cl") {
            icon_tile("ic_expand",   rcIconExpand,   ink);
            icon_tile("ic_shrink",   rcIconShrink,   ink);
            icon_tile("ic_maximize", rcIconMaximize, ink);
            icon_tile("ic_minimize", rcIconMinimize, ink);
        }
    }
}

/* The payoff of a procedural icon over a raster one.

   RC_IconCallback is `void (*)(float size, RC_Color color)` and the renderer calls it
   during EVERY frame's custom pass - so both arguments are per-frame values. The
   geometry is re-stroked at exactly the size and colour the app asks for: nothing
   is cached, no texture is re-uploaded, and no asset is re-exported. A PNG bakes
   its pixels once, at export time; these nine icons and the logo below do not.

   The logo here is converted from an all-`currentColor` SVG
   (examples/assets/icons/rayclay-logo-mono.svg) by ex11. That one detail
   decides the signature: an SVG
   whose paints are all `currentColor` emits rcIcon...(size, colour) and is
   tintable at runtime; bake one concrete colour into the SVG and the generated
   icon takes (size) alone, because its palette is then part of the artwork. */
/* ---------------------------------------------------------------------------
   Gestures - the pointer reads (rcPointer + rcPointerPressed / rcPointerDown /
   rcPointerReleased).

   rcClicked answers "was this element activated?", which is a COMPLETED press-
   then-release over one element, so it cannot describe a gesture still in
   flight. These four can: latch on the press, track while held, commit on the
   release.

   A drag-SCRUB is the demo because it is correct using only what RayClay
   exposes. The value moves by how far the pointer TRAVELLED - a delta between
   two rcPointer() reads - and a delta needs no knowledge of where the field is
   or how wide it is. Anything that must map a pointer position onto CONTENT
   (a brush across a chart, drag-select over a plot) needs the element's rect,
   and no rc* call reports one yet - see docs/widgets.md, "Zoom, pan and brush".

   Note there is no rcAppRequestFrame here: a drag IS pointer input, and input
   admits a frame under the on-demand contract, so the frames arrive for
   free while the mouse moves.
   --------------------------------------------------------------------------- */
static void section_gestures(RC_App *app, AppState *st) {
    RC_Style s = rcGetStyle();
    /* rcPointer() is CONTENT space - zoom and pan already undone - so it is
       directly comparable with layout geometry at any zoom. Never mix it with a
       raw OS coordinate. Latched once per frame, so x and y cannot straddle a
       move mid-layout. */
    RC_Vec2 p = rcPointer();

    if (rcPointerPressed(RC_POINTER_LEFT) && rcIsHovered("scrub_field")) {
        st->scrubbing    = true;
        st->scrubAtPress = st->scrub;
        st->scrubAnchorX = p.x;
    } else if (st->scrubbing && rcPointerDown(RC_POINTER_LEFT)) {
        /* 0.5 units per content px: a full 0..100 sweep is a 200px drag. */
        float v = st->scrubAtPress + (p.x - st->scrubAnchorX) * 0.5f;

        st->scrub = v < 0.0f ? 0.0f : (v > 100.0f ? 100.0f : v);
    } else if (st->scrubbing && rcPointerReleased(RC_POINTER_LEFT)) {
        st->scrubbing = false;
        st->scrubCommits++;
    }

    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("GESTURES  (rcPointer + the button reads)");
        rcTextL("Press and drag sideways anywhere on the field. Keep dragging past its edge - the value keeps tracking, and the release still lands.",
                 .font = F_SMALL, .color = s.textMuted);

        /* Highlighting on .scrubbing rather than on hover is the point: the
           gesture owns the field until the button comes up, wherever the
           pointer has wandered to by then. */
        rcBox(.id = "scrub_field", .w = "grow", .h = "56px",
               .bg = st->scrubbing ? s.primary : s.surfaceAlt,
               .borderRadius = "all-lg", .align = "cc",
               .tooltip = "Drag left/right to scrub") {
            RC_String v = rcFormat(rcAppArena(app), "%.1f", st->scrub);

            rcText(v, .font = F_BIG,
                    .color = st->scrubbing ? s.surface : s.text);
        }

        rcRow(.w = "grow", .gap = 10, .align = "cl") {
            RC_String st8 = rcFormat(rcAppArena(app), "%s   committed drags: %d",
                                        st->scrubbing ? "dragging" : "idle",
                                        st->scrubCommits);

            rcText(st8, .font = F_SMALL, .color = s.textMuted);
            rcBox(.w = "grow") {}
            if (rcButton("scrub_reset", "Reset", RC_BTN_DEFAULT)) {
                st->scrub        = 50.0f;
                st->scrubCommits = 0;
            }
        }
    }
}

/* Keyboard: the two reads people confuse, plus the one query that makes a shortcut
   portable. Everything here is a COUNTER rather than a timed flash on purpose - a
   per-frame countdown would stall under the on-demand default, because a key
   held with no new event produces no frames. That is the same edge/level
   distinction this section is about, so the demo obeys it instead of fighting it. */
static void section_keyboard(RC_App *app, AppState *st) {
    RC_Style s    = rcGetStyle();
    bool     held = rcKeyDown(RC_KEY_SPACE);   /* LEVEL: true every frame it is down */

    /* EDGE: one count per physical press. Auto-repeat does NOT re-fire it, which is
       what you want for a command and the wrong thing for "move while held". */
    if (rcKeyPressed(RC_KEY_SPACE))
        st->spacePresses++;

    /* The closing edge. Counting both halves is how you SEE that they pair up:
       auto-repeat inflates neither, so the two readouts stay equal once the key
       is back up. A press-only counter cannot tell "still held" from "over", so
       anything that must end when the key ends - push-to-talk, a charge meter -
       belongs on this edge rather than on a timer. */
    if (rcKeyReleased(RC_KEY_SPACE))
        st->spaceReleases++;

    /* RC_MOD_PRIMARY is Cmd on a native macOS build and Ctrl everywhere else, so one
       line of source is the correct accelerator on every platform - do not test
       RC_KEY_LEFT_CTRL yourself. Enter rather than S because a browser keeps its own
       Ctrl+S, and an example that only works on desktop is not an example. */
    if (rcModDown(RC_MOD_PRIMARY) && rcKeyPressed(RC_KEY_ENTER))
        st->submits++;

    /* Arrows, not WASD: letter keys are LOGICAL, so RC_KEY_W is wherever the user's
       layout puts W - it is not the physical key next to A on AZERTY. */
    if (rcKeyPressed(RC_KEY_LEFT))  st->nudge--;
    if (rcKeyPressed(RC_KEY_RIGHT)) st->nudge++;

    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("KEYBOARD  (rcKeyPressed / rcKeyDown / rcModDown)");
        rcTextL("Click the window first, then try it: Space, the Left/Right arrows, and Cmd+Enter (Ctrl+Enter off macOS).",
                 .font = F_SMALL, .color = s.textMuted);

        rcRow(.w = "grow", .gap = 10, .align = "cl") {
            /* The level read drives a live chip: it lights on the press edge and
               clears on the release edge, and both of those are input, so the frame
               you need is always there. */
            rcBox(.w = "120px", .h = "44px", .align = "cc", .borderRadius = "all-lg",
                   .bg = held ? s.primary : s.surfaceAlt) {
                rcTextL("SPACE", .font = F_SMALL,
                         .color = held ? s.surface : s.textMuted);
            }
            rcColumn(.w = "grow", .gap = 4) {
                RC_String edge = rcFormat(rcAppArena(app),
                                             "pressed %d / released %d        rcKeyDown now: %s",
                                             st->spacePresses, st->spaceReleases,
                                             held ? "yes" : "no");
                RC_String acc  = rcFormat(rcAppArena(app),
                                             "PRIMARY+Enter submits: %d        arrows: %d",
                                             st->submits, st->nudge);

                rcText(edge, .font = F_SMALL, .color = s.text);
                rcText(acc,  .font = F_SMALL, .color = s.textMuted);
            }
            if (rcButton("kbd_reset", "Reset", RC_BTN_DEFAULT)) {
                st->spacePresses  = 0;
                st->spaceReleases = 0;
                st->submits       = 0;
                st->nudge         = 0;
            }
        }
    }
}

/* Copy/paste against the real system clipboard.

   Two things make this worth reading rather than skimming:

   1. A read is REQUEST-then-POLL, not a blocking get. RayClay shapes it that way
      because a browser resolves navigator.clipboard.readText() through a promise,
      so a synchronous read cannot exist there at all. This is the portable form,
      and it is the only one that works on both targets: the web clipboard is real
      and enabled by default, but it is asynchronous, so code built around
      rcClipboardGet compiles there and then never reads anything.

   2. The status line below is EARNED by the user's own Paste, not probed at
      startup. There is no capability query in the API, and both of the probes you
      would reach for first report "no clipboard" on a platform whose clipboard
      works perfectly:
        - rcClipboardGet answers only under a SYNCHRONOUS backend. Web's default
          backend is asynchronous by nature, not absent, so a get-based probe sees
          NULL there forever.
        - An unprompted read at startup is refused by a browser on merit: a
          clipboard READ needs a secure context (https or localhost) and a user
          gesture.
      Requesting from inside the button handler satisfies both, and it is what a
      real app does anyway. One source, no #ifdef, honest on every target. */
static void section_clipboard(RC_App *app, AppState *st) {
    RC_Style s = rcGetStyle();

    /* Collect an outstanding read. Poll never blocks, never allocates and never
       issues a request; it answers exactly once. It returns NULL for "still
       pending", "denied" and "already collected" alike, so bound the wait rather
       than polling forever - the bundled text field does the same. */
    if (st->clipToken) {
        const char *got = rcClipboardPoll(st->clipToken);
        if (got) {
            rcStrCopy(st->clipLast, got, sizeof st->clipLast);  /* RayClay owns `got` */
            st->clipToken = 0;
            st->clipState = 1;
        } else if (--st->clipWait <= 0) {
            rcStrCopy(st->clipLast, "(no text delivered)", sizeof st->clipLast);
            st->clipToken = 0;
            st->clipState = 2;
        } else {
            /* A countdown measured in frames only counts if those frames happen.
               A delivery wakes the app by itself, so the case this covers is the
               one the timeout exists for: a backend that never answers at all.
               Without this the app parks after the single frame the button asked
               for and the status reads "waiting..." forever. */
            rcAppRequestFrame(app);
        }
    }

    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("CLIPBOARD  (rcClipboardSet / Request + Poll)");

        rcRow(.w = "grow", .gap = 8, .align = "cl") {
            rcBox(.w = "10px", .h = "10px", .borderRadius = "all-full",
                   .bg = st->clipState == 1 ? s.successHover
                       : st->clipState == 2 ? s.warningHover
                                            : s.textMuted) {}
            /* Three calls rather than a ternary: rcTextL takes a string LITERAL,
               so its length is known at compile time. For a runtime C string use
               rcTextC; rcText takes an RC_String, e.g. an rcFormat result. */
            if (st->clipState == 1) {
                rcTextL("Reads work here - copy in another app, then press Paste again.",
                         .font = F_SMALL, .color = s.textMuted);
            } else if (st->clipState == 2) {
                rcTextL("Answered with no text: an empty clipboard and a refused read look alike.",
                         .font = F_SMALL, .color = s.textMuted);
            } else {
                rcTextL("Press Paste to exercise a read - a browser grants one only on a user gesture.",
                         .font = F_SMALL, .color = s.textMuted);
            }
        }

        rcRow(.w = "grow", .gap = 10, .align = "cl") {
            if (rcButton("clip_copy", "Copy a line", RC_BTN_PRIMARY)) {
                RC_String line = rcFormat(rcAppArena(app),
                                             "RayClay copied this at frame %ld.", st->frame);
                /* rcFormat hands back a LENGTH-counted RC_String; the clipboard
                   takes a C string, and the arena always NUL-terminates. */
                rcClipboardSet(line.chars);
                st->clipCopies++;
            }
            if (rcButton("clip_paste", "Paste", RC_BTN_DEFAULT)) {
                st->clipToken   = rcClipboardRequest();
                st->clipWait    = 30;
                st->clipLast[0] = '\0';
                /* Under the on-demand default a pending read needs a frame to be
                   collected in. A synchronous backend has already answered by now,
                   but an async one has not - so ask for the frames the countdown
                   above is measured in rather than assuming they arrive. */
                rcAppRequestFrame(app);
            }
        }

        RC_String stat = rcFormat(rcAppArena(app), "copies: %d        pasted: %s",
                                     st->clipCopies,
                                     st->clipToken     ? "waiting..."
                                     : st->clipLast[0] ? st->clipLast
                                                       : "(nothing yet)");
        rcText(stat, .font = F_SMALL, .color = s.text);
        rcTextL("A read is mirrored into a fixed buffer: text over 4095 bytes arrives truncated, with one warning naming both sizes.",
                 .font = F_SMALL, .color = s.textMuted);
        rcTextL("On the web this is the browser's own clipboard, so it needs https or localhost; on an insecure origin a read is refused, not crashed.",
                 .font = F_SMALL, .color = s.textMuted);
    }
}

static void section_live_icons(RC_App *app, AppState *st) {
    RC_Style s = rcGetStyle();
    /* One base colour per frame; each tile offsets the hue so the row reads as a
       travelling wave rather than nine identical swatches. */
    RC_Color live = hue_color(st->hue, 0.85f, 1.0f);
    RC_Color wave[9];
    for (int i = 0; i < 9; i++)
        wave[i] = hue_color(hue_wrap(st->hue + (float)i * st->hueSpread), 0.85f, 1.0f);

    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("LIVE ICONS (colour is a per-frame argument)");

        rcRow(.gap = 14, .align = "cl") {
            rcBox(.w = "96px", .h = "96px", .align = "cc") {
                rcIconRayClayLogoMono(96.0f, live);
            }
            rcColumn(.w = "grow", .gap = 4) {
                rcTextL("RC_IconCallback(size, colour)", .font = F_SMALL, .color = s.text);
                RC_String rgb = rcFormat(rcAppArena(app), "rgb(%d, %d, %d)",
                                            (int)live.r, (int)live.g, (int)live.b);
                rcText(rgb, .font = F_SMALL, .color = s.textMuted);
                rcTextL("redrawn from vectors every frame",
                         .font = F_SMALL, .color = s.textMuted);
            }
        }

        rcRow(.gap = 8, .align = "cl") {
            icon_tile("lv_settings",    rcIconSettings,   wave[0]);
            icon_tile("lv_panel_left",  rcIconPanelLeft,  wave[1]);
            icon_tile("lv_panel_right", rcIconPanelRight, wave[2]);
            icon_tile("lv_minus",       rcIconMinus,      wave[3]);
            icon_tile("lv_x",           rcIconX,          wave[4]);
        }
        rcRow(.gap = 8, .align = "cl") {
            icon_tile("lv_expand",   rcIconExpand,   wave[5]);
            icon_tile("lv_shrink",   rcIconShrink,   wave[6]);
            icon_tile("lv_maximize", rcIconMaximize, wave[7]);
            icon_tile("lv_minimize", rcIconMinimize, wave[8]);
        }

        /* Randomise jumps the base hue and re-rolls the wave's spacing; Freeze
           holds the current frame's colours so a single one can be inspected. */
        rcRow(.gap = 12, .align = "cl") {
            if (rcButton("btn_hue_rand", "Randomise", RC_BTN_DEFAULT)) {
                st->hue       = rng_unit(&st->rng);
                st->hueSpread = 0.02f + 0.14f * rng_unit(&st->rng);
            }
            rcToggle("tg_hue_freeze", &st->hueFrozen);
            /* Name what the toggle COSTS, not just which way it is set: this is
               the clearest place in the gallery to show the bargain, since
               flipping it moves this window between the two states live.
               The third state is not the toggle at all - it is update() having
               sampled rcIsModalOpen() and stood the animation down. */
            rcTextC(st->animHeld  ? "Held - a modal dialog is open (rcIsModalOpen)"
                    : st->hueFrozen ? "Frozen - this window is parked at ~0 CPU"
                                    : "Cycling - requesting a frame per tick",
                     .font = F_SMALL, .color = s.textMuted);
        }
        rcRow(.gap = 12, .align = "cl") {
            rcTextL("Speed", .font = F_SMALL, .color = s.textMuted);
            rcBox(.w = "160px") { rcSlider("sl_hue_speed", &st->hueSpeed, 0.02f, 0.60f); }
            RC_String cps = rcFormat(rcAppArena(app), "%.2f cyc/s", st->hueSpeed);
            rcText(cps, .font = F_SMALL, .color = s.textMuted);
        }
    }
}

/* ── owning an arena ─────────────────────────────────────────────────────────
   Every other rcFormat in this file writes into rcAppArena(app) - the RUNNER's
   scratch, which is reset for you at the top of every frame. That is exactly
   right for a label you rebuild each frame, and exactly WRONG for anything that
   has to survive into the next one.

   So own one. rcArenaInit takes a byte budget up front, rcArenaAlloc is a
   pointer bump, and rcArenaReset reclaims EVERYTHING at once in O(1). There is
   no per-allocation free - that is the trade, and the reason a bump allocator
   is worth having.

   The footgun, and it is why the array is allocated here rather than sitting
   in AppState: rcArenaReset rewinds the WHOLE arena, not just the text. Any
   pointer you took from it before the reset - including this entry array - is
   dangling afterwards, so a reset must be followed by re-allocating whatever
   you meant to keep. Clear does exactly that, in that order.

   rcFormat cannot overrun it: on a full arena it returns the empty string,
   leaves the bump pointer unadvanced, and warns once through the log. */
static void arena_log_clear(AppState *st) {
    rcArenaReset(&st->logArena);
    /* MUST come after the reset, and its result MUST be re-stored: the previous
       array pointer died with the rewind above. */
    st->logLines = (RC_String *)rcArenaAlloc(&st->logArena,
                                             sizeof(RC_String) * LOG_MAX);
    st->logCount = 0;
}

static void section_arena(RC_App *app, AppState *st) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("YOUR OWN ARENA (rcArenaInit / Alloc / Reset / Free)");

        rcRow(.gap = 8, .align = "cl") {
            if (rcButton("arena_add", "Log an event", RC_BTN_PRIMARY)
                && st->logLines && st->logCount < LOG_MAX) {
                /* Formatted into OUR arena, so it is still here next frame -
                   the same call against rcAppArena(app) would be gone. */
                st->logLines[st->logCount++] =
                    rcFormat(&st->logArena, "event %d  -  logged on frame %ld",
                             ++st->logSeq, st->frame);
            }
            if (rcButton("arena_clear", "Clear", RC_BTN_DEFAULT))
                arena_log_clear(st);
        }

        /* currOffset / bufferLength are public fields: the bump pointer is not a
           secret, and watching it climb is the clearest picture of what an arena
           is. Note it does NOT fall back as entries are added, only on reset. */
        /* size_t through %lu with an explicit cast, not %zu: mingw's CRT only
           honours %zu when __USE_MINGW_ANSI_STDIO is on, and examples must read
           the same on every target. */
        RC_String used = rcFormat(rcAppArena(app),
                                  "%lu of %lu bytes used   %d of %d entries%s",
                                  (unsigned long)st->logArena.currOffset,
                                  (unsigned long)st->logArena.bufferLength,
                                  st->logCount, LOG_MAX,
                                  st->logCount >= LOG_MAX ? "   (full)" : "");
        rcText(used, .font = F_SMALL, .color = s.textMuted);

        rcColumn(.w = "grow", .bg = s.surfaceAlt, .p = 10, .gap = 6,
                  .borderRadius = "all-lg") {
            if (st->logCount == 0) {
                rcTextL("Nothing logged yet - the arena is empty.",
                         .font = F_SMALL, .color = s.textMuted);
            } else {
                for (int i = 0; i < st->logCount; i++)
                    rcText(st->logLines[i], .font = F_SMALL, .color = s.text);
            }
        }
    }
}

/* A fixed-height, vertically scrolled list. The clip + child offset exercise
   the renderer's SCISSOR_START / SCISSOR_END path; scroll the wheel over it.

   It also drives that list from CODE, because the wheel is not the only way to
   move a scroll container and the calls that do it are easy to miss:
   rcScrollToTop / rcScrollToBottom jump to an end, rcScrollBy nudges by a
   pixel delta, and rcGetScrollInfo reads back where it landed. That readback
   is what you need for a position indicator, a "back to top" affordance, or
   restoring a saved position when a view reopens.

   The buttons are declared before the list on purpose. A scroll call takes
   effect where you make it, so driving the container BEFORE it is laid out
   moves it on THIS frame; the same call made after the container lands a frame
   late. It is the same ordering rule rcScrollbar follows.

   One sign trap left, and it is worth knowing which way round it is:
     rcScrollBy(id, 0, dy)   positive-DOWN, like the DOM's element.scrollBy
     RC_ScrollInfo.offsetY   positive-DOWN, like element.scrollTop  -> AGREES
     rcScrollDeltaY()        positive-UP, and in wheel NOTCHES, not pixels
   So rcScrollBy(id, 0, 160) raises rcGetScrollInfo(id).offsetY by 160 - those
   two compose directly. Only the raw WHEEL delta runs the other way (it keeps
   GLFW's convention), so that is the one to negate and scale. */
static void section_scroll(RC_App *app) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("SCROLL + SCISSOR");

        /* Drive the container first - see the ordering note above. */
        rcRow(.gap = 8, .align = "cl") {
            if (rcButton("scr_top", "Top", RC_BTN_DEFAULT))
                rcScrollToTop("ScrollArea");
            if (rcButton("scr_pgup", "Page up", RC_BTN_DEFAULT))
                rcScrollBy("ScrollArea", 0.0f, -160.0f);  /* up = NEGATIVE */
            if (rcButton("scr_pgdn", "Page down", RC_BTN_DEFAULT))
                rcScrollBy("ScrollArea", 0.0f, +160.0f);  /* ~one viewport */
            if (rcButton("scr_bottom", "Bottom", RC_BTN_DEFAULT))
                rcScrollToBottom("ScrollArea");
        }

        /* .found stays false until the container has been laid out once, so the
           first frame reports "not laid out" instead of a confident 0 of 0. */
        RC_ScrollInfo sc = rcGetScrollInfo("ScrollArea");
        RC_String pos;
        if (sc.found) {
            int pct = sc.maxOffsetY > 0.0f
                    ? (int)(sc.offsetY / sc.maxOffsetY * 100.0f + 0.5f) : 100;
            pos = rcFormat(rcAppArena(app),
                           "offset %.0f of %.0f px  (%d%%)   wheel %+.0f notches this frame",
                           sc.offsetY, sc.maxOffsetY, pct, rcScrollDeltaY());
        } else {
            pos = rcFormat(rcAppArena(app), "ScrollArea has not been laid out yet");
        }
        rcText(pos, .font = F_SMALL, .color = s.textMuted);

        rcColumn(.id = "ScrollArea", .w = "grow", .h = "180px", .scroll = "v",
                  .bg = s.surfaceAlt, .p = 10, .gap = 8, .borderRadius = "all-lg") {
            for (int i = 0; i < 20; i++) {
                rcRow(.w = "grow", .h = "32px", .bg = s.surface, .align = "cl",
                       .px = 12, .borderRadius = "all-md") {
                    /* Distinct per-row label so scrolling is visibly different
                       row-to-row. (Identical rows looked static under the wheel:
                       a 40px notch equals the 32px+8px row pitch, so each notch
                       re-aligned identical rows.) */
                    RC_String label = rcFormat(rcAppArena(app),
                        "Row %2d  -  scrolled & clipped to the box", i + 1);
                    rcText(label, .font = F_SMALL, .color = s.textMuted);
                }
            }
        }
    }
}

/* Native interactive widgets - buttons, a checkbox, and a toggle, all bound to
   app state. Proves hit-testing + the immediate-mode interaction core end
   to end (click a button -> counter changes; toggle -> theme switches). */
/* A NON-MODAL popup: rcBeginModalEx with .modality = RC_MODALITY_NON_MODAL - the same call
   as the modal dialog below, minus the scrim. The app behind stays live, so this
   panel can be left open while you keep working - the classic detached inspector.

   THE TRAP, and the reason this example exists: RC_MODALITY_NON_MODAL ALONE is not "leave it
   open and keep working". Modality and DISMISSAL are separate axes, and turning
   off the first does not touch the second - an outside press still closes the
   panel. But with no scrim, "outside" means *anywhere in the app*, so the very
   first click the user makes into the thing they wanted to keep using dismisses the
   panel they wanted to keep open. "Stays open" is the PAIR:

       .modality = RC_MODALITY_NON_MODAL, .noBackdropDismiss = true

   The checkbox flips exactly that second flag at runtime, so both halves are
   reachable: uncheck it, click anywhere in the gallery, and the panel disappears.

   (rcBeginModalEx always centers its panel. A real inspector would dock to an
   edge; that needs a plain .floating Box, not this call - see section_floating.) */
static void inspector_panel(RC_App *app, AppState *st) {
    RC_Style s = rcGetStyle();
    RC_ModalOptions opts = {
        .modality          = RC_MODALITY_NON_MODAL,          /* no scrim -> the app stays live */
        .noBackdropDismiss = st->inspectorSticky,   /* ...and THIS is what keeps it open */
    };
    if (!rcBeginModalEx("inspector", &st->inspectorOpen, opts))
        return;

    rcTextL("Inspector (non-modal)", .font = F_BODY, .color = s.text);
    rcTextL("Leave me open. Drag Volume behind me and watch these move.",
             .font = F_SMALL, .color = s.textMuted);

    /* These read the SAME state the widgets behind are editing. They keep updating
       while the panel is open, which is the proof that nothing is being blocked -
       under the modal dialog, the scrim would make every one of them frozen. */
    RC_String live = rcFormat(rcAppArena(app),
                                 "volume %d%%   quality %d   clicks %d   frame %ld",
                                 (int)(st->volume * 100.0f + 0.5f),
                                 st->quality, st->clicks, st->frame);
    rcText(live, .font = F_SMALL, .color = s.text);

    rcCheckbox("insp_sticky", "Stay open when I click away", &st->inspectorSticky);
    rcTextC(st->inspectorSticky
                 ? "RC_MODALITY_NON_MODAL + noBackdropDismiss: clicks outside are ignored."
                 : "RC_MODALITY_NON_MODAL alone: the next click outside CLOSES this panel.",
             .font = F_SMALL, .color = st->inspectorSticky ? s.textMuted : s.danger);

    if (rcButton("insp_close", "Close", RC_BTN_DEFAULT))
        st->inspectorOpen = false;

    rcEndModal();
}

static void section_widgets(RC_App *app, AppState *st) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("WIDGETS (native, interactive)");
        rcRow(.gap = 10, .align = "cl") {
            if (rcButton("btn_primary", "Primary", RC_BTN_PRIMARY)) st->clicks++;
            if (rcButton("btn_default", "Default", RC_BTN_DEFAULT)) st->clicks++;
            if (rcButton("btn_reset",   "Reset",   RC_BTN_DANGER))  st->clicks = 0;
            if (rcButton("btn_ghost",   "Ghost",   RC_BTN_GHOST))   st->clicks++;
        }
        RC_String clicks = rcFormat(rcAppArena(app), "clicks: %d", st->clicks);
        rcText(clicks, .font = F_SMALL, .color = s.textMuted);

        rcRow(.gap = 16, .align = "cl") {
            rcCheckbox("cb_details", "Show details", &st->showDetails);
            rcToggle("tg_dark", &st->darkMode);
            rcTextC(st->darkMode ? "Dark theme" : "Light theme",
                     .font = F_SMALL, .color = s.textMuted);
        }
        /* The zoom-MODE switch lives in the ZOOM section, beside the factor
           readout and the bindings it belongs with. */
        if (st->showDetails) {
            rcTextL("Details shown because the checkbox is checked.",
                     .font = F_SMALL, .color = s.textMuted);
        }

        /* Text inputs (stb_textedit-backed; click to focus, type, Ctrl+A/C/V).
           The accents in the placeholder are deliberate and load-bearing: text YOU
           supply renders the full Latin-1 window, so this reads "Zoe Muller" with
           its umlauts intact. It is also a standing canary - a change in the
           placeholder bake shows up here as "Zo? M?ller" the moment anyone looks.
           (TYPED input is still filtered to ASCII in v1; that is a separate limit.) */
        rcRow(.gap = 10, .align = "cl") {
            rcBox(.w = "220px") {
                rcTextInput("in_name", st->name, sizeof st->name,
                             .placeholder = "Your name (e.g. Zoë Müller)");
            }
            rcBox(.w = "160px") {
                rcTextInput("in_secret", st->secret, sizeof st->secret,
                             .placeholder = "Password", .password = true);
            }
            /* rcIsFocused asks the FIELD, by id, rather than tracking focus
               yourself - which matters because focus also moves by Tab and by
               clicking away, so an app-owned "isEditing" flag drifts out of sync
               the first time the user does either. Read it to gate the things
               that should only apply while typing: a live validation hint, or
               suppressing a global single-key shortcut so it does not swallow
               the keystroke. */
            rcBox(.w = "10px", .h = "10px", .borderRadius = "all-full",
                   .bg = (rcIsFocused("in_name") || rcIsFocused("in_secret"))
                       ? s.successHover : s.textMuted) {}
        }
        RC_String hello = rcFormat(rcAppArena(app),
                                      st->name[0] ? "Hello, %s!" : "(type a name above)",
                                      st->name);
        rcText(hello, .font = F_SMALL, .color = s.textMuted);

        /* SELECTABLE TEXT - a multiline field, because selection is the thing
           the single-line inputs above cannot show off.
           rcTextArea is rcTextInput with .multiline preset: Enter inserts a
           newline, long lines soft-wrap at the box width, Up/Down move by ROW
           rather than by character, and the view scrolls to keep the caret in
           sight. Everything a reader expects of selection - drag, double-click
           a word, Ctrl+A, Ctrl+C/V - is the vendored stb_textedit state machine
           driving YOUR buffer; there is no widget object holding the string.
           Say what it is not. Selection here exists because the field is
           EDITABLE. The labels, headings and table cells everywhere else in this
           gallery are DRAWN TEXT, and drawn text cannot be selected or copied.
           A reader who sees selection working in one box will reasonably assume
           it works everywhere, so the caption below refuses that reading rather
           than leaving them to discover it. */
        rcBox(.w = "grow", .py = 4) {
            rcTextArea("draft", st->draft, sizeof st->draft, .rows = 5,
                        .font = F_SMALL,
                        .placeholder = "Type here, then drag across what you typed");
        }
        rcTextL("Selection works in that box because it is an EDITABLE field. "
                 "Labels and table cells elsewhere in this gallery are drawn text, "
                 "so they cannot be selected or copied.",
                 .font = F_SMALL, .color = s.textMuted);

        /* MODAL vs NON-MODAL, side by side - the contrast is the point. The modal
           draws a scrim and the app behind goes dead until you answer it; the
           inspector draws no scrim and the gallery keeps working underneath. */
        rcRow(.gap = 10, .align = "cl") {
            if (rcButton("btn_modal", "Open dialog", RC_BTN_DEFAULT))
                st->modalOpen = true;
            if (rcButton("btn_inspector", "Open inspector", RC_BTN_DEFAULT))
                st->inspectorOpen = true;
        }
        if (rcBeginModal("dlg_demo", &st->modalOpen)) {
            rcTextL("Delete this item?", .color = s.text);
            rcTextL("This can't be undone.", .font = F_SMALL, .color = s.textMuted);
            rcRow(.gap = 10, .align = "cl") {
                if (rcButton("dlg_cancel", "Cancel", RC_BTN_DEFAULT)) st->modalOpen = false;
                if (rcButton("dlg_delete", "Delete", RC_BTN_DANGER))  st->modalOpen = false;
            }
            rcEndModal();
        }
        inspector_panel(app, st);
    }
}

/* The breadth widgets - a slider, a determinate progress bar, and a radio group.
   The slider reports its value as a live % readout; the progress bar animates on
   its own; the radio group is a one-of-three selection sharing a single int. */
static void section_controls(RC_App *app, AppState *st) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("MORE WIDGETS (slider / progress / radio)");

        rcRow(.gap = 12, .align = "cl") {
            rcTextL("Volume", .font = F_SMALL, .color = s.textMuted);
            rcBox(.w = "220px") { rcSlider("sl_volume", &st->volume, 0.0f, 1.0f); }
            RC_String pct = rcFormat(rcAppArena(app), "%d%%",
                                        (int)(st->volume * 100.0f + 0.5f));
            rcText(pct, .font = F_SMALL, .color = s.textMuted);
        }

        rcRow(.gap = 12, .align = "cl") {
            rcTextL("Loading", .font = F_SMALL, .color = s.textMuted);
            /* Animated on its own (a triangle wave over ~8s) so it's clearly a
               separate widget, not driven by the Volume slider above. */
            float t    = (float)(st->frame % 480) / 480.0f;
            float load = t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f;
            rcBox(.w = "260px") { rcProgress("pr_load", load); }
        }

        rcRow(.gap = 16, .align = "cl") {
            rcRadio("rb_low",  "Low",    &st->quality, 0);
            rcRadio("rb_med",  "Medium", &st->quality, 1);
            rcRadio("rb_high", "High",   &st->quality, 2);
        }
        const char *qn = st->quality == 0 ? "Low"
                       : st->quality == 1 ? "Medium" : "High";
        RC_String q = rcFormat(rcAppArena(app), "quality: %s", qn);
        rcText(q, .font = F_SMALL, .color = s.textMuted);

        /* A combo / dropdown - its popup floats above the content below it. */
        rcRow(.gap = 12, .align = "cl") {
            rcTextL("Preset", .font = F_SMALL, .color = s.textMuted);
            static const char *const presets[] = {
                "Default", "Compact", "Comfortable", "Spacious"
            };
            rcBox(.w = "220px") {
                rcCombo("cb_preset", &st->preset, presets, 4);
            }
        }
    }
}

static void update(RC_App *app, void *userData) {
    AppState *st = (AppState *)userData;
    /* Load the one demo image once (the renderer is up by the first update). */
    static bool imageReady = false;
    if (!imageReady) {
        st->imageFromMem = demo_image_load();
        imageReady       = true;
        st->imageLoads++;
    }

    /* The image-lifecycle buttons land here rather than in the layout callback:
       layout DECLARES a frame, so changing what that frame draws from while it is
       being declared is the wrong shape to teach. rcUnloadImage zeroes the handle
       it is given, which is what lets section_images test .handle to decide
       between the picture and the placeholder. */
    if (st->imageAction == 1) {
        rcUnloadImage(&g_demo_image);
    } else if (st->imageAction == 2 && !g_demo_image.handle) {
        st->imageFromMem = demo_image_load();
        st->imageLoads++;
    }
    st->imageAction = 0;

    /* Zoom-badge trigger: the runner applies zoom gestures BEFORE the
       callbacks, so rcAppZoom already reflects a gesture from this frame -
       poll-and-compare IS the change trigger, no callback needed. The first
       tick (prevZoom == 0) only seeds the baseline. */
    float z = rcAppZoom(app);
    if (z != st->prevZoom) {
        if (st->prevZoom > 0.0f)
            st->zoomBadgeSecs = 1.5f;
        st->prevZoom = z;
    }
    if (st->zoomBadgeSecs > 0.0f)
        st->zoomBadgeSecs -= rcAppFrameTime(app);

    /* Drive the zoom-mode toggle into the engine once per frame (layout reflow
       vs optical magnify); the widgets label reads it back via rcAppZoomMode. */
    rcAppSetZoomMode(app, st->opticalZoom ? RC_ZOOM_OPTICAL : RC_ZOOM_LAYOUT);

    /* Hold the animation while a MODAL dialog is up. This is what rcIsModalOpen is
       for and the only thing it is for: the library already handles its own
       layering, so this is not a guard against the dialog drawing wrongly - it is
       the app declining to burn frames on a decoration nobody can see or reach.
       The same call sites a real app would use are a video, a poll, or a
       simulation tick.

       And it discriminates, which is the half worth demonstrating: opening the
       INSPECTOR (rcBeginModalEx with RC_MODALITY_NON_MODAL, section_inspector)
       leaves this reading FALSE and the colours keep cycling, because a non-modal
       popup deliberately leaves the app behind it live. Only "Open dialog" stops
       it. Modality is the question being asked, not visibility. */
    const bool inDialog = rcIsModalOpen();
    st->animHeld = inDialog;   /* sampled ONCE, here; the label reads this back */

    /* Advance the live-icon hue. dt is clamped first: after a stall (a debugger
       breakpoint, a window drag) one huge frame would otherwise jump the colour
       an arbitrary distance around the wheel instead of stepping it. */
    if (!st->hueFrozen && !inDialog) {
        float dt = rcAppFrameTime(app);
        if (dt > 0.1f) dt = 0.1f;
        st->hue = hue_wrap(st->hue + dt * st->hueSpeed);
    }

    /* Two things here move with no input: the live-icon hue and the zoom badge
       fading out. RayClay draws only when asked, so keep asking while
       either is running - the requestAnimationFrame contract. Freeze the hue and
       let the badge expire and this window parks at ~0 CPU, which is exactly the
       the idle win (7.656 -> 0.00 CPU-s/min on this scene). */
    if ((!st->hueFrozen && !inDialog) || st->zoomBadgeSecs > 0.0f)
        rcAppRequestFrame(app);

    st->frame++;
}

/* ---------------------------------------------------------------------------
   Dataviz showcase - rcChart / rcSparkline / RC_Table / RC_SplitPane.
   Their data is file-scope + const, which
   also satisfies the "arrays are BORROWED until rcRender()" contract for free:
   static storage outlives every frame, so nothing here can dangle.
   --------------------------------------------------------------------------- */

/* Twelve months of a revenue series + its target line (the multi-series chart). */
static const float demo_rev[]    = { 42, 55, 48, 61, 58, 72, 69, 81, 77, 90, 85, 98 };
static const float demo_target[] = { 50, 52, 54, 56, 58, 60, 66, 70, 74, 80, 86, 92 };
/* Independent spot-checks over the same 12 months, drawn as SCATTER: these are
   discrete observations, and joining them with a line would imply a continuity
   the data does not have. That is the whole reason the kind exists. */
static const float demo_audit[]  = { 45, 51, 50, 63, 55, 70, 72, 79, 80, 88, 83, 96 };
/* One 16-point walk, drawn three ways to show the sparkline kinds. */
static const float demo_spark[]  = { 3, 5, 4, 7, 6, 9, 8, 6, 7, 10, 9, 12, 11, 13, 12, 15 };

/* A 16-sensor array for the many-series chart. Static, so it satisfies the
   "y/x arrays are BORROWED until rcRender()" contract for free. */
enum { MANY_SERIES = 16, MANY_POINTS = 24 };
static float demo_many[MANY_SERIES][MANY_POINTS];

static void many_series_fill(void) {
    static bool filled = false;

    if (filled)
        return;
    for (int s = 0; s < MANY_SERIES; s++) {
        for (int i = 0; i < MANY_POINTS; i++) {
            /* Deterministic, so the frame is byte-identical run to run - a
               rand() here would break the benchmark harness's comparisons. */
            int wobble = (s * 7 + i * 13) % 29;
            demo_many[s][i] = 40.0f + (float)s * 3.0f + (float)wobble;
        }
    }
    filled = true;
}

/* A long enough trace that zooming into it is worth doing. Deterministic, so
   the frame stays byte-identical run to run for the benchmark harness. */
enum { ZOOM_POINTS = 240 };
static float demo_zoom[ZOOM_POINTS];

static void zoom_series_fill(void) {
    static bool filled = false;

    if (filled)
        return;
    for (int i = 0; i < ZOOM_POINTS; i++) {
        /* A slow rise with two different ripples on it, so a zoomed-in window
           shows detail that is invisible at the full range - which is the
           whole point of being able to zoom. */
        int fast = (i * 17) % 23;
        int slow = (i * 5)  % 61;

        demo_zoom[i] = 30.0f + (float)i * 0.25f + (float)fast * 0.8f
                                 + (float)slow * 0.35f;
    }
    filled = true;
}

static void section_charts(RC_App *app, AppState *st) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12, .borderRadius = "all-xl") {
        section_heading("CHARTS  (rcChart / rcSparkline)");
        /* A multi-series chart, and the one place ALL THREE kinds meet: revenue
           BARS behind a target LINE with audit SCATTER over both, one legend, a y
           grid. One chart may mix kinds freely - pick the kind per series from
           what the data IS, not from what looks busiest. rcChart GROWs to fill, so
           it is wrapped in a sized box - the same contract as text and RC_Table. */
        rcBox(.w = "grow", .h = "160px") {
            RC_Series ser[] = {
                { .y = demo_rev,    .count = 12, .kind = RC_SERIES_BAR,  .label = "revenue" },
                { .y = demo_target, .count = 12, .kind = RC_SERIES_LINE, .label = "target",
                  .color = s.primary, .thickness = 2.0f },
                /* SCATTER draws markers and no line. Note .thickness means the
                   marker RADIUS on this kind, not a stroke width; 0 gives 3. */
                { .y = demo_audit,  .count = 12, .kind = RC_SERIES_SCATTER, .label = "audit",
                  .color = RC_AMBER_500, .thickness = 3.5f },
            };
            rcChart("gal_chart", ser, 3,
                     (RC_ChartOptions){ .legend = true, .y = { .grid = true },
                                        .tooltip      = RC_CHART_TOOLTIP_NEAREST,
                                        .tooltipPlace = (RC_ChartTooltipPlace)st->tipPlace,
                                        .hoverGuide   = st->hoverGuide,
                                        .hoverMarkers = st->hoverMarkers });
        }
        /* WHICH datum the readout names is not an option - it follows the mark
           geometry. A LINE or AREA is continuous, so every x has a reading and
           the nearest one wins; a BAR or SCATTER is a discrete mark, so the
           pointer has to be ON one or there is no readout at all. This chart
           MIXES kinds, so it stays continuous: the target line is readable at
           any x, including where no bar stands. Hover a bars-only chart and you
           will find the opposite - empty sky above a short bar reads as nothing,
           which is the point. */
        /* WHETHER there is a readout (.tooltip) and WHERE it sits (.tooltipPlace)
           are separate fields. Hover the plot and switch between them:
             cursor - the default; follows the pointer, flipping by quadrant so the
                      panel always grows away from the nearest plot edge
             corner - parks in the TOP corner opposite the pointer, so it never
                      covers the data; the better pick on a dense plot
             fixed  - pins it at .tooltipAnchor (default top-left) and ignores the
                      pointer entirely, for a dashboard that wants it to hold still
           The combo index IS the enum value, so no mapping table is needed. */
        rcRow(.w = "grow", .gap = 10, .align = "cl") {
            rcTextL("tooltip placement", .font = F_SMALL, .color = s.textMuted);
            static const char *const places[] = { "cursor", "corner", "fixed" };
            rcBox(.w = "150px") {
                rcCombo("cb_tipplace", &st->tipPlace, places, 3);
            }
            /* The panel gives you the numbers; these two say WHICH LINE each
               number came from, which is the reading a multi-series chart is
               actually for. Both are independent of .tooltip - a guide with the
               readout switched off is a legitimate crosshair. Note the markers
               land on the target LINE and the audit SCATTER but never on the
               revenue BARS: a hovered bar already shows which datum is picked,
               so a dot on it would be noise. */
            rcCheckbox("cb_hguide",   "guide",   &st->hoverGuide);
            rcCheckbox("cb_hmarkers", "markers", &st->hoverMarkers);
        }
        /* The same trace as a sparkline three ways - the bare inline form (no axes,
           the whole box IS the plot): a table cell, a dashboard tile, a live strip. */
        rcRow(.w = "grow", .gap = 10, .align = "cl") {
            rcBox(.w = "grow", .h = "32px") {
                rcSparkline("gal_sl_line", demo_spark, 16, (RC_SparklineOptions){0});
            }
            rcBox(.w = "grow", .h = "32px") {
                rcSparkline("gal_sl_area", demo_spark, 16,
                             (RC_SparklineOptions){ .kind = RC_SERIES_AREA, .color = s.primary });
            }
            rcBox(.w = "grow", .h = "32px") {
                rcSparkline("gal_sl_bar", demo_spark, 16,
                             (RC_SparklineOptions){ .kind = RC_SERIES_BAR, .color = RC_EMERALD_500 });
            }
        }

        /* MANY SERIES - the case the two-series chart above cannot answer.
           Charts draw up to 16 series. Hand rcChart more and it
           warns once, draws the first 16, and adds a "+N more" chip in the plot
           itself - whether or not a legend is on - so a truncated dashboard says
           so on screen instead of reading as "those metrics went flat".

           The cap is a READABILITY limit, not a memory one (1408 B of .bss per
           series, no binary cost). The categorical palette only holds so many
           hues a person can separate, which is why this runs to the cap: it
           shows where that line actually is. Past about a dozen, set .color
           yourself rather than letting the palette assign one.

           The RC_Series descriptors are COPIED by rcChart, so this local array
           is fine - it is the .y arrays that are borrowed, and those are static. */
        many_series_fill();
        section_heading("MANY SERIES  (16 - the cap)");
        rcBox(.w = "grow", .h = "180px") {
            RC_Series many[MANY_SERIES];

            for (int i = 0; i < MANY_SERIES; i++)
                many[i] = (RC_Series){ .y     = demo_many[i],
                                       .count = MANY_POINTS,
                                       .kind  = RC_SERIES_LINE };
            rcChart("gal_many", many, MANY_SERIES,
                     (RC_ChartOptions){ .y = { .grid = true } });
        }
        rcTextL("Sixteen auto-coloured series in one plot. Beyond ~12 hues nobody can tell two lines apart - assign .color yourself.",
                 .font = F_SMALL, .color = s.textMuted);

        /* DRAG-TO-ZOOM. There is no zoom widget and none is needed: the view is
           two floats this app owns, and immediate mode re-plots at whatever
           they hold. What the library has to supply is the MAPPING - a pointer
           position is only half of one.

           rcChartPlotRect, NOT rcGetElementBox(chartId): the chart sizes its
           plot INSIDE the box we gave it (the y gutter grows with the widest
           tick label, a legend takes a header row), so mapping against the
           outer box would be off by whatever chrome the chart chose. */
        zoom_series_fill();
        section_heading("DRAG TO ZOOM  (rcChartPlotRect + the pointer reads)");
        rcBox(.w = "grow", .h = "180px") {
            RC_Series z = { .y     = demo_zoom,
                            .count = ZOOM_POINTS,
                            .kind  = RC_SERIES_LINE,
                            .color = s.primary };
            RC_Box       plot = rcChartPlotRect("gal_zoom");
            RC_Vec2 p    = rcPointer();

            /* The WIDTH check is the readiness test, not .found - do not drop
               it. .found answers "does an element with this id exist?", and
               the engine registers an element when it OPENS while filling its box
               only when layout ENDS, so on the first frame this reads
               found = TRUE with an all-zero rect. Guarding on .found alone
               would divide by zero exactly once, on frame one. .found still
               earns its place: it catches a misspelled id. */
            if (plot.found && plot.width > 0.0f) {
                float t     = (p.x - plot.x) / plot.width;
                float dataX = st->zoomLo + t * (st->zoomHi - st->zoomLo);

                if (rcPointerPressed(RC_POINTER_LEFT) && rcIsHovered("gal_zoom")) {
                    st->brushing = true;
                    st->brushA   = st->brushB = dataX;
                } else if (st->brushing && rcPointerDown(RC_POINTER_LEFT)) {
                    st->brushB = dataX;
                } else if (st->brushing && rcPointerReleased(RC_POINTER_LEFT)) {
                    float a = st->brushA, b = st->brushB;

                    st->brushing = false;
                    if (b < a) { float sw = a; a = b; b = sw; } /* dragged right-to-left */
                    /* Reject a click with no drag, or the range collapses to a
                       single value with no way back except Reset. */
                    /* Clamp to the data domain: the pointer can be dragged
                       outside the plot, and nothing else bounds a or b. */
                    if (a < 0.0f) a = 0.0f;
                    if (b > (float)(ZOOM_POINTS - 1)) b = (float)(ZOOM_POINTS - 1);
                    if (b - a > 1.0f) { st->zoomLo = a; st->zoomHi = b; }
                }
            }
            rcChart("gal_zoom", &z, 1,
                     (RC_ChartOptions){ .y = { .grid = true },
                                        .x = { .min = st->zoomLo, .max = st->zoomHi },
                                        .tooltip = RC_CHART_TOOLTIP_NEAREST });
        }
        rcRow(.w = "grow", .gap = 10, .align = "cl") {
            RC_String rng = rcFormat(rcAppArena(app), "%s  x: %.0f - %.0f  of  0 - %d",
                                        st->brushing ? "brushing" : "drag across the plot",
                                        (double)st->zoomLo, (double)st->zoomHi,
                                        ZOOM_POINTS - 1);

            rcText(rng, .font = F_SMALL, .color = s.textMuted);
            rcBox(.w = "grow") {}
            if (rcButton("zoom_reset", "Reset", RC_BTN_DEFAULT)) {
                st->zoomLo   = 0.0f;
                st->zoomHi   = (float)(ZOOM_POINTS - 1);
                st->brushing = false;
            }
        }
    }
}

static void section_table(void) {
    RC_Style s = rcGetStyle();
    /* Eight rows in a short box, so the body scrolls under the sticky header. */
    static const char *const names[]  = { "Alpha", "Bravo", "Charlie", "Delta",
                                          "Echo", "Foxtrot", "Golf", "Hotel" };
    static const char *const values[] = { "1,240", "980", "2,015", "1,660",
                                          "740", "1,905", "612", "1,430" };
    static const char *const deltas[] = { "+4.2%", "-1.1%", "+8.0%", "+2.7%",
                                          "-0.5%", "+3.3%", "-2.4%", "+1.8%" };
    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12, .borderRadius = "all-xl") {
        section_heading("TABLE  (RC_Table - sticky header, scroll by id, aligned columns)");
        /* Name GROWs; the two numeric columns are fixed-width and right-aligned.
           Column widths read as CSS strings, just like .w on a box. */
        RC_TableColumn cols[] = {
            { .header = "Name",   .w = "grow", .align = "cl" },
            { .header = "Value",  .w = "76px", .align = "cr" },
            { .header = "Change", .w = "76px", .align = "cr" },
        };
        rcBox(.w = "grow", .h = "132px") {
            /* cellPad 10 (default 6) keeps the right-aligned numerics clear of the
               overlay scrollbar that rides the table body's right edge. */
            if (rcBeginTable("gal_table", cols, 3, (RC_TableOptions){ .cellPadding = RC_VAL(10) })) {
                for (int i = 0; i < 8; i++) {
                    rcTableRow();
                    rcTextC(names[i],  .font = F_SMALL, .color = s.text);
                    rcTableNext();
                    rcTextC(values[i], .font = F_SMALL, .color = s.text);
                    rcTableNext();
                    rcTextC(deltas[i], .font = F_SMALL,
                             .color = deltas[i][0] == '+' ? s.successHover : s.danger);
                }
                rcEndTable();
            }
        }
    }
}

/* ---------------------------------------------------------------------------
   BIG TABLE - the case the eight-row table above cannot answer.

   Layout charges per DECLARED element, not per visible one, so declaring 5,000
   rows to show seven costs several times an entire gallery frame - and culling
   cannot help, because an element must be sized and positioned before anything
   knows it is offscreen. rcVirtualList emits a top spacer, the visible window
   and a bottom spacer, so the per-frame cost stops depending on the row count
   (rayclay.h measures a 3-element row at 50x cheaper by 1,000 rows and 254x by
   5,000; scale that by however many elements YOUR row declares).

   Two things here are deliberate and are the rules that bite:

   1. Every row is pinned to exactly BIGTABLE_ROW_H by a fixed-height box inside
      each cell, and rcVirtualList is told the PITCH - that height plus the cell
      padding above and below it. The spacers are computed from the number you
      pass, so passing the cell height alone under-reports the content by
      2 x padding per row and the scrollbar stops agreeing with the rows.
      Pinning the height beats guessing at font metrics; deriving the pitch from
      it beats assuming they are the same number.
      Pass the row pitch, not the cell height. They are different numbers
      whenever there is padding: a 26 px cell with 6 px above and below has a
      38 px pitch, and handing the list 26 makes it run a third short. Spell an
      explicit zero as RC_VAL(0). The pitch rule
      is unchanged: whatever padding a table carries, it is part of the pitch.
      Declaring it with RC_VAL is what makes the pitch derivable at the call
      site instead of depending on a default nobody wrote down.
   2. Cell text comes from rcFormat (the per-frame arena), never a stack buffer:
      rcTextC/rcText BORROW the pointer and the cell is drawn long after this
      scope has gone. A row *id* could be a stack buffer - ids are hashed as the
      element opens - but text cannot.

   Nothing holds the dataset in RAM: each visible cell is synthesised from its
   row index, which is the shape a real app has when rows arrive from a file,
   a socket or a database.
   --------------------------------------------------------------------------- */
enum { BIGTABLE_ROWS = 5000, BIGTABLE_ROW_H = 26,
       BIGTABLE_CELL_PAD = 4, BIGTABLE_PITCH = BIGTABLE_ROW_H + 2 * BIGTABLE_CELL_PAD };

static void section_bigtable(RC_App *app) {
    RC_Style  s   = rcGetStyle();
    RC_Arena *mem = rcAppArena(app);
    static const char *const kinds[] = { "temp", "humid", "press", "lux",
                                         "co2",  "pm25",  "volt",  "flow" };

    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12, .borderRadius = "all-xl") {
        section_heading("BIG TABLE  (rcVirtualList - 5,000 rows, ~20 declared per frame)");
        RC_TableColumn cols[] = {
            { .header = "#",       .w = "64px", .align = "cr" },
            { .header = "Sensor",  .w = "grow", .align = "cl" },
            { .header = "Reading", .w = "92px", .align = "cr" },
        };
        /* 26vh, NOT a pixel height. A scrolling viewport measured in px is not
           zoom-stable: LAYOUT zoom shrinks the logical viewport (window px /
           zoom), so a fixed 186px box eventually grows taller than the entire
           layout. rcVirtualList then sees a viewport larger than the layout,
           cannot tell that from a list reporting its own content back, and
           clamps with a warning. Run-confirmed: 18 x Ctrl+'+' on a 1400x900
           screen was enough. A viewport unit is a fraction of that same
           shrinking viewport, so the ratio - and the row count - hold at every
           zoom. Matches the 186px it replaced at the default window size. */
        rcBox(.w = "grow", .h = "26vh") {
            if (rcBeginTable("gal_bigtable", cols, 3,
                              (RC_TableOptions){ .cellPadding = RC_VAL(BIGTABLE_CELL_PAD) })) {
                rcVirtualList(row, "gal_bigtable", BIGTABLE_ROWS, BIGTABLE_PITCH) {
                    /* Deterministic stand-in for real data - no stored array. */
                    int  v   = (row.index * 37) % 900 + 100;
                    bool hot = v > 800;
                    rcTableRow();
                    rcBox(.w = "grow", .h = "26px", .px = 8, .align = "cr") {
                        rcText(rcFormat(mem, "%d", row.index + 1),
                                .font = F_SMALL, .color = s.textMuted);
                    }
                    rcTableNext();
                    rcBox(.w = "grow", .h = "26px", .px = 8, .align = "cl") {
                        rcText(rcFormat(mem, "%s-%04d", kinds[row.index & 7], row.index),
                                .font = F_SMALL, .color = s.text);
                    }
                    rcTableNext();
                    rcBox(.w = "grow", .h = "26px", .px = 8, .align = "cr") {
                        rcText(rcFormat(mem, "%d.%d", v / 10, v % 10),
                                .font = F_SMALL, .color = hot ? s.successHover : s.text);
                    }
                }
                rcEndTable();
            }
        }
        rcScrollbar("gal_bigtable");
        rcTextL("Scroll it: the frame cost is flat in row count - only the visible rows are declared.",
                .font = F_SMALL, .color = s.textMuted);
    }
}

static void section_splitpane(AppState *st) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12, .borderRadius = "all-xl") {
        section_heading("SPLIT PANE  (RC_SplitPane - drag the divider)");
        rcBox(.w = "grow", .h = "120px") {
            if (rcBeginSplitPane("gal_split", RC_SPLIT_ROW, &st->splitFrac,
                                  (RC_SplitOptions){0})) {
                rcColumn(.w = "grow", .h = "grow", .bg = s.surfaceAlt, .align = "cc",
                          .borderRadius = "all-lg") {
                    rcTextL("Pane 1", .font = F_BODY, .color = s.text);
                }
                rcSplitHandle();
                rcColumn(.w = "grow", .h = "grow", .bg = s.surfaceAlt, .align = "cc",
                          .borderRadius = "all-lg") {
                    rcTextL("Pane 2", .font = F_BODY, .color = s.textMuted);
                }
                rcEndSplitPane();
            }
        }
    }
}

/* What the app knows about its own surface, and how it schedules work.

   This is the panel you want when a layout misbehaves on a machine you do not
   own. Each reading below is something developers usually discover by guessing:

   1. CONTENT SCALE is the DISPLAY's factor - a 2x HiDPI panel reads 2.0. It is
      NOT rcAppZoom, which is a separate multiplier the APP owns. Conflating the
      two is how "but it looks right on my monitor" bugs survive review, so both
      are shown side by side and never added together.
   2. SAFE-AREA INSETS are always {0,0,0,0} on desktop, so the honest thing an
      example can show is the zero itself - plus where it stops being zero. The
      read costs nothing and is what lets one source survive a notched phone.
   3. rcGetElementBox is the layout debugger. Mind the guard: `found` means the
      id EXISTS, not that its rect is ready, so an element's first frame reports
      found with an ALL-ZERO box. `found && width > 0` is the correct test, in
      that order - the header spells out why the other order divides by zero.

   The scheduling half demonstrates the headline win directly. On-demand is the
   default, and the two ways out of it are opposites: rcAppSetContinuousRendering
   burns a frame forever, while rcAppRequestFrameAfter arms exactly ONE wake and
   goes straight back to sleep. Prefer the second whenever the work has an end -
   that is the whole reason an idle RayClay app costs 0.00 CPU. */
static void section_display(RC_App *app, AppState *st) {
    RC_Style s = rcGetStyle();

    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("DISPLAY & SCHEDULING  (what the app knows about itself)");

        /* Measured on the element declared further down this same section. */
        RC_Box   probe  = rcGetElementBox("diag_probe");
        RC_Insets safe  = rcGetSafeAreaInsets();
        RC_String scale = rcFormat(rcAppArena(app),
                                      "content scale %.2fx (display)      zoom %.0f%% (app)",
                                      rcGetContentScale(), rcAppZoom(app) * 100.0f);
        RC_String inset = rcFormat(rcAppArena(app),
                                      "safe-area insets  t%.0f r%.0f b%.0f l%.0f   (always 0 on desktop)",
                                      safe.top, safe.right, safe.bottom, safe.left);
        /* BOTH halves of the guard, in this order - see the note above. */
        RC_String box   = (probe.found && probe.width > 0.0f)
                        ? rcFormat(rcAppArena(app),
                                      "diag_probe box  x%.0f y%.0f  %.0f x %.0f",
                                      probe.x, probe.y, probe.width, probe.height)
                        : rcFormat(rcAppArena(app),
                                      "diag_probe box  measuring... (first frame: found, rect still 0)");

        rcText(scale, .font = F_SMALL, .color = s.text);
        rcText(inset, .font = F_SMALL, .color = s.textMuted);
        rcText(box,   .font = F_SMALL, .color = s.textMuted);

        /* The element being measured. Giving it an id is the entire requirement;
           rcGetElementBox hashes the same string and never retains it. */
        rcRow(.id = "diag_probe", .w = "grow", .h = "34px", .gap = 8,
               .px = 10, .align = "cl", .bg = s.surfaceAlt,
               .borderRadius = "all-lg") {
            rcTextL("measured element", .font = F_SMALL, .color = s.textMuted);
        }

        /* Zoom, the way a browser does it. rcAppSetZoom REFLOWS the layout
           (text re-bakes crisp at the new size) rather than scaling pixels. */
        rcRow(.w = "grow", .gap = 8, .align = "cl") {
            rcTextL("Zoom", .font = F_SMALL, .color = s.textMuted);
            if (rcButton("diag_zoom_out", "75%",  RC_BTN_DEFAULT)) rcAppSetZoom(app, 0.75f);
            if (rcButton("diag_zoom_100", "100%", RC_BTN_DEFAULT)) rcAppSetZoom(app, 1.00f);
            if (rcButton("diag_zoom_in",  "125%", RC_BTN_DEFAULT)) rcAppSetZoom(app, 1.25f);
        }

        /* Scheduling. The continuous toggle is a switch because it is a STATE you
           leave on; the wake is a button because it is a one-shot. That asymmetry
           is the API's point, not a UI choice. */
        rcRow(.w = "grow", .gap = 10, .align = "cl") {
            if (rcToggle("diag_continuous", &st->continuous)) {
                rcAppSetContinuousRendering(app, st->continuous);
            }
            rcTextL("continuous rendering", .font = F_SMALL, .color = s.textMuted);
        }
        rcRow(.w = "grow", .gap = 8, .align = "cl") {
            if (rcButton("diag_wake", "Wake me in 1s", RC_BTN_DEFAULT)) {
                rcAppRequestFrameAfter(app, 1.0);
                st->wakeArmedFrame = st->frame;
                st->wakesArmed++;
            }
            /* Frames elapsed since arming is the observable proof the app really
               parked and really came back: on-demand and idle, it barely moves. */
            RC_String w = rcFormat(rcAppArena(app),
                                      "armed %d, +%ld frames since",
                                      st->wakesArmed,
                                      st->wakesArmed ? st->frame - st->wakeArmedFrame : 0L);
            rcText(w, .font = F_SMALL, .color = s.textMuted);
        }

        /* rcAppIsDebugEnabled reports the layout inspector's state. It is a READ,
           so it stays valid however the library was built. The inspector is
           compiled OUT by default (RC_DEBUG_TOOLS=0), which is why
           this reads "off (compiled out)" in a stock build rather than plain
           "off". The two are worth distinguishing: "off" invites you to hunt for
           the toggle key, and there is nothing to find until you rebuild with
           -DRC_DEBUG_TOOLS=1. */
        {
            RC_String d = rcFormat(rcAppArena(app), "layout inspector: %s",
#if RC_DEBUG_TOOLS
                                      rcAppIsDebugEnabled(app) ? "on" : "off");
#else
                                      "off (compiled out; -DRC_DEBUG_TOOLS=1)");
#endif
            rcText(d, .font = F_SMALL, .color = s.textMuted);
        }
    }
}

static void layout(RC_App *app, void *userData) {
    AppState *st = (AppState *)userData;
    rcSetStyle(st->darkMode ? rcStyleDark() : rcStyleLight());
    RC_Style  s     = rcGetStyle();
    /* A runtime theme switch has to move the window too. rcSetStyle changes every
       colour the UI draws with, but it cannot reach the window BEHIND the layout:
       the clear colour is resolved once at creation and rc_theme.h has no RC_App to
       reach. Without this line the old theme's background stays wherever your layout
       does not cover the window - and it is ALL you see on a frame RayClay holds back
       while it grows the layout arena. Safe to call every frame: the setter is
       change-gated, so setting the colour already in force returns immediately. */
    rcAppSetClearColor(app, s.background);

    /* Sizing is the CSS-like string DSL (.w = "grow" / "380px" / "50%"); the typed
       .wType = RC_GROW / RC_PX(..) form is the equivalent fast path - it skips the
       per-frame string parse, so it suits hot loops and runtime-computed sizes.
       Both forms coexist (OPT-4) and resolve 1:1. */
    rcColumn(.id = "Root", .w = "grow", .h = "grow", .bg = s.background) {

        /* Info strip (gallery chrome). The BUNDLED titlebar above it - drawn by
           the runner under nativeFrame, zero app code - owns window drag and the
           min/max/close controls; this row is plain content. */
        rcRow(.w = "grow", .h = "44px", .bg = s.chrome,
               .px = 12, .gap = 14, .align = "cl") {
            rcIconSettings(24, s.primary);
            /* The title is the flexible element: it grows, and is the FIRST
               thing to shrink and clip when the window narrows, so the live
               readout keeps priority. (.overflow = "hidden" scissors the long
               title; .wrap = "n" keeps it on one line.) */
            rcBox(.w = "grow", .h = "grow", .overflow = "hidden", .align = "cl") {
                rcTextL("RayClay - native renderer gallery",
                    .font = F_TITLE, .color = s.text, .wrap = "n");
            }
            /* Live frame rate (exp-smoothed by the runner) AND the frame number,
               which ticks up once per update - both prove the loop is live. */
            RC_String readout = rcFormat(rcAppArena(app), "%.0f FPS  -  frame %ld",
                                            rcAppFPS(app), st->frame);
            rcText(readout, .font = F_SMALL, .color = s.textMuted);
        }

        /* Two independently scrolling columns of primitive showcases. */
        rcRow(.id = "Content", .w = "grow", .h = "grow", .p = 20, .gap = 16) {
            rcColumn(.id = "ColLeft", .w = "grow", .h = "grow", .scroll = "v",
                      .gap = 16) {
                section_rectangles();
                section_rounding();
                section_gradients();
                section_shadows();
                section_overlay();
                section_floating();
                section_images(app, st);
                section_borders();
                section_text();
                section_widgets(app, st);
                section_controls(app, st);
                section_charts(app, st);
                section_table();
                section_bigtable(app);
                section_splitpane(st);
            }
            rcColumn(.id = "ColRight", .w = "380px", .h = "grow", .scroll = "v",
                      .gap = 16) {
                section_icons();
                section_gestures(app, st);
                section_keyboard(app, st);
                section_clipboard(app, st);
                section_live_icons(app, st);
                section_scroll(app);
                section_zoom(app, st);
                section_arena(app, st);
                section_display(app, st);
            }
        }
    }

    /* Zoom badge (Chrome-style): while the timer runs, float a "125%" pill
       top-centre over everything (out of flow, root-anchored). The change
       trigger lives in update().

       It names the reset binding, and that is the whole reason it earns its
       place. Ctrl 0 has always reset the zoom (RC_ZoomOptions.bindZoomReset
       defaults to RC_KEY_0, keypad 0 too) and nothing on screen ever said so -
       the owner zoomed to 195% and had to ASK how to get back. A percentage
       alone tells you what happened; the shortcut tells you what to do about
       it. Suppressed at exactly 100%, where there is nothing to undo. */
    if (st->zoomBadgeSecs > 0.0f) {
        float zf = rcAppZoom(app);
        RC_String zl = rcFormat(rcAppArena(app), "%.0f%%", zf * 100.0f);
        rcRow(.id = "zoom_badge",
               .floating = { .to      = RC_ATTACH_ROOT,
                             .parent  = RC_ANCHOR_TOP_CENTER,
                             .element = RC_ANCHOR_TOP_CENTER,
                             .offset  = { 0, 56 },
                             .zIndex = 2000 },
               .bg = s.surfaceAlt, .px = 14, .py = 8, .align = "cc", .gap = 10,
               .borderRadius = "all-full",
               .border = { .color = s.border, .width = "1px" }) {
            rcText(zl, .font = F_BODY, .color = s.text);
            if (zf < 0.999f || zf > 1.001f)
                rcTextL("Ctrl 0 resets", .font = F_SMALL, .color = s.textMuted);
        }
    }

    /* Draggable scrollbars for the scroll containers. Each is a floating element
       declared here in the layout, so it layers itself above the container it
       names; they auto-hide when their content fits. */
    rcScrollbar("ColLeft");
    rcScrollbar("ColRight");
    rcScrollbar("ScrollArea");
    rcScrollbar("gal_table");   /* the RC_Table body, named by its id */
}

int main(void) {
    AppState st = {
        /* SEEDED, and that is the whole point: an empty text area demonstrates
           nothing. The owner asked to SEE selectable text, so there has to be
           text present on the first frame to drag across.
           ASCII only. rcTextInput's contract says pre-fill the buffer with
           ASCII: editing is byte-indexed, so a backspace over multibyte UTF-8 is
           memory-safe but can split a character and garble it. That is why this
           string has no accents while the name placeholder below deliberately
           does - a PLACEHOLDER is drawn, never edited. */
        .draft = "Drag across this line to select it. Double-click picks a word.\n"
                 "Ctrl+A selects all, Ctrl+C copies, Ctrl+V pastes.\n"
                 "\n"
                 "The editor works over YOUR buffer - this text lives in a plain\n"
                 "char array in the app, not in a widget object.",
        .frame = 0, .darkMode = true, .volume = 0.5f, .quality = 1,
        .splitFrac = 0.5f,   /* dataviz split-pane demo starts centred */
        .scrub = 50.0f,      /* drag-scrub demo starts mid-range       */
        /* Drag-to-zoom starts showing the whole trace. */
        .zoomLo = 0.0f, .zoomHi = (float)(ZOOM_POINTS - 1),
        /* The in-plot hover affordances default OFF in the library, so no chart
           changes under an existing app. A gallery has the opposite duty - show
           the capability - so gal_chart opts both IN and the checkboxes turn
           them back off, which is the comparison worth seeing. */
        .hoverGuide = true, .hoverMarkers = true,
        /* Start FROZEN so the gallery DEMONSTRATES the headline win instead of
           contradicting it. This is the page a visitor opens first, and a live
           hue makes it request a frame every tick - measured at 33 draws per
           idle rAF tick against 0.000 for every other bundled page. Frozen
           costs nothing visually (the icons are still fully coloured, they just
           do not cycle) and the toggle is one click away, so switching it on is
           an active demo of rcAppRequestFrame rather than an accident. */
        .hueFrozen = true,
        /* Open in the CORRECT pairing, so the panel behaves as a developer expects
           a non-modal panel to behave; unchecking it demonstrates the trap. */
        .inspectorSticky = true,
        /* A full hue revolution every ~7s; the wave spans ~half the wheel across
           the nine tiles. rng must start non-zero - xorshift32 fixes 0. */
        .hue = 0.55f, .hueSpeed = 0.15f, .hueSpread = 0.06f, .rng = 0x2545F491u,
    };

    static const float fontSizes[F_COUNT] = {
        [F_SMALL] = 14.0f,
        [F_BODY]  = 18.0f,
        [F_TITLE] = 30.0f,
        [F_BIG]   = 44.0f,
    };

    rcSetStyle(rcStyleDark());

    RC_AppOptions opts = {
        .width             = 1100,
        .height            = 720,
        .title             = "RayClay Gallery",
        .clearColor        = rcGetStyle().background,
        /* No fontPath: the bundled face is baked at each of fontSizes (zero-asset). */
        .fontSizes         = fontSizes,
        .fontCount         = F_COUNT,
        .scratchArenaBytes = 64 * 1024,
        /* OPT IN to drag-to-pan. It is OFF by default and that default is right:
           a browser has no pan, and out of the box a RayClay app is a browser.
           This gallery is the toolbox, so it turns on the thing an ordinary app
           should leave alone - and says so, because a reader copying from here
           needs to know which line is the exception. Only optical zoom can pan;
           layout zoom reflows into the window and has nothing outside it. */
        .zoom              = { .pan = true },
        .nativeFrame       = true,   /* borderless + the BUNDLED titlebar (runner-drawn) */
        .updateCallback          = update,
        .layoutCallback          = layout,
        .userData          = &st,
    };

    /* Our own arena for section_arena, separate from .scratchArenaBytes above:
       that one belongs to the runner and is reset every frame. 4 KiB is enough
       for LOG_MAX entries and small enough that the demo can be driven to
       "full" by hand, which is the interesting st. */
    st.logArena = rcArenaInit(4 * 1024);
    arena_log_clear(&st);   /* carves the entry array out of the fresh arena */

    /* RAYCLAY_MAX_FRAMES=N renders N frames then exits via the normal teardown
       (unset / 0 = run until the window is closed) - the runner reads the env var
       itself, so the shipped demo binary doubles as a windowed-lifecycle leak
       target under Valgrind / ASan with no app-side code. */
    int rc = rcRunApp(&opts);

    /* We allocated it, so we free it - the runner only owns the arena it made
       from .scratchArenaBytes. Freeing after rcRunApp returns is what keeps this
       binary clean as a leak target. */
    rcArenaFree(&st.logArena);
    return rc;
}
