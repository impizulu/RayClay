/*
================================================================================
    main.c - RayClay "1990s" example (Windows 95 "Display Properties" dialog)
================================================================================

    A period-accurate Win95 settings dialog: battleship-silver surfaces, hard
    SQUARE corners, two-tone 3D bevels (light top-left, dark bottom-right), a
    solid-blue caption bar, and a three-tab property sheet. Same source ->
    native desktop window AND web. Zero-asset: bundled font baked at runtime;
    procedural window-control icons only.

    1990s aesthetic: chunky raised push buttons that press "in" when active,
    and inset sunken panels for content and controls.

    Tabs: Background (wallpaper) . Screen Saver (timeout) . Appearance (scheme).

    Build target: rayclay_ex02_1990s_gui
================================================================================
*/

#include "rayclay.h"

/* Monochrome control glyphs from the shared example assets (compiled in). */
#include "icons/rc_icons_minus.h"
#include "icons/rc_icons_maximize.h"
#include "icons/rc_icons_shrink.h"   /* the restore glyph, when the window IS maximised */
#include "icons/rc_icons_x.h"

/* Font ladder baked from the bundled face at these sizes - zero-asset. */
typedef enum { F_BODY = 0, F_TITLE, F_COUNT } AppFont;

typedef struct {
    int  tab;           /* 0=Background 1=Screen Saver 2=Appearance    */

    /* Background tab */
    int  wallpaper;     /* combo selection                             */
    int  placement;     /* radio group: 0=Center 1=Tile 2=Stretch      */
    bool pattern;       /* checkbox                                    */

    /* Screen Saver tab */
    int   saver;        /* combo selection                            */
    float wait;         /* slider minutes (1..60)                     */
    bool  password;     /* checkbox                                   */

    /* Appearance tab */
    int   scheme;       /* combo selection                            */
    int   item;         /* combo selection                            */
    float itemSize;     /* slider (0..30)                             */

    bool  applied;      /* Apply acknowledged; any control change clears it */
} AppState;

/* Win95 silver bevel tones. Kept local so the whole 3D look tunes in one place. */
#define BEVEL_LIGHT RC_GRAY_100   /* highlight edge (top-left)     */
#define BEVEL_DARK  RC_GRAY_600   /* shadow edge (bottom-right)    */
#define BEVEL_FACE  RC_GRAY_300   /* the silver control face       */

/* ── bevel push button ─────────────────────────────────────────────────────
   A two-tone 3D bevel can't be one border (that draws a uniform ring), so it is
   NESTED and each ring is ONE-SIDED via padding: the outer box pads TOP+LEFT
   only, so its highlight tone shows on just those two edges; the inner box pads
   BOTTOM+RIGHT only, so its shadow tone shows there; the silver face sits on
   top. Raised = light TL / dark BR; swapping the tones flips it to sunken, which
   is how Win95 shows an active tab or a held button. rcClicked turns the whole
   stack into one button. Returns true on the frame it is clicked. */
static bool bevel_button(const char *id, const char *label, bool sunken) {
    RC_Color tl = sunken ? BEVEL_DARK  : BEVEL_LIGHT;   /* top-left edge     */
    RC_Color br = sunken ? BEVEL_LIGHT : BEVEL_DARK;     /* bottom-right edge */

    rcBox(.id = id, .bg = tl, .pt = 2, .pl = 2) {
        rcBox(.bg = br, .pb = 2, .pr = 2, .w = "grow", .h = "grow") {
            rcBox(.bg = BEVEL_FACE, .px = 12, .py = 5, .align = "cc",
                   .w = "grow", .h = "grow") {
                rcTextC(label, .font = F_BODY, .color = RC_BLACK, .textAlign = "c");
            }
        }
    }
    return rcClicked(id);
}

/* ── sunken inset panel ─────────────────────────────────────────────────────
   The inverse bevel used for content areas and control wells: dark edge on
   top-left, light edge on bottom-right, so the face reads as pressed INTO the
   silver. Same one-sided-ring trick as bevel_button, tones swapped. Callers
   pass the well body via the brace body, so this is a plain two-box wrapper. */
#define SUNKEN_OPEN(_id, ...) \
    rcBox(.id = (_id), .bg = BEVEL_DARK, .pt = 2, .pl = 2, \
           __VA_ARGS__) \
        rcBox(.bg = BEVEL_LIGHT, .pb = 2, .pr = 2, \
               .w = "grow", .h = "grow")

/* ── Win95 caption button ───────────────────────────────────────────────────
   A 16x14 raised silver square with a black glyph, tagged with a window-control
   id so the runner performs the OS action. Hand-rolled: the bundled 38px
   traffic-light cluster neither fits the 24px band nor the 1995 look. */
static void caption_button(const char *winId, RC_IconCallback icon) {
    rcBox(.id = winId, .bg = BEVEL_LIGHT, .pt = 1, .pl = 1) {
        rcBox(.bg = BEVEL_DARK, .pb = 1, .pr = 1) {
            rcBox(.bg = BEVEL_FACE, .w = "14px", .h = "12px", .align = "cc") {
                icon(8.0f, RC_BLACK);
            }
        }
    }
}

/* ── a form-field label (shared body font/colour) ───────────────────────── */
static void field_label(const char *label) {
    rcTextC(label, .font = F_BODY, .color = RC_BLACK);
}

/* ── tab: Background ───────────────────────────────────────────────────────── */
static void tab_background(AppState *st) {
    static const char *const walls[] = {
        "(None)", "Clouds", "Bubbles", "Setup", "Forest", "Waves",
    };
    rcColumn(.w = "grow", .h = "grow", .p = 14, .gap = 12) {
        field_label("Wallpaper:");
        rcBox(.w = "220px") {
            if (rcCombo("bg_wall", &st->wallpaper, walls, 6)) st->applied = false;
        }
        field_label("Display:");
        rcRow(.gap = 16, .align = "cl") {
            if (rcRadio("bg_center",  "Center",  &st->placement, 0)) st->applied = false;
            if (rcRadio("bg_tile",    "Tile",    &st->placement, 1)) st->applied = false;
            if (rcRadio("bg_stretch", "Stretch", &st->placement, 2)) st->applied = false;
        }
        if (rcCheckbox("bg_pat", "Show desktop pattern", &st->pattern)) st->applied = false;
    }
}

/* ── tab: Screen Saver ─────────────────────────────────────────────────────── */
static void tab_screensaver(RC_App *app, AppState *st) {
    static const char *const savers[] = {
        "(None)", "Flying Windows", "Mystify", "Starfield", "Marquee",
    };
    rcColumn(.w = "grow", .h = "grow", .p = 14, .gap = 12) {
        field_label("Screen Saver:");
        rcBox(.w = "220px") {
            if (rcCombo("ss_pick", &st->saver, savers, 5)) st->applied = false;
        }
        rcRow(.gap = 10, .align = "cl") {
            field_label("Wait:");
            rcBox(.w = "grow") {
                if (rcSlider("ss_wait", &st->wait, 1.0f, 60.0f)) st->applied = false;
            }
            RC_String mins = rcFormat(rcAppArena(app), "%d min",
                                         (int)(st->wait + 0.5f));
            rcText(mins, .font = F_BODY, .color = RC_BLACK);
        }
        if (rcCheckbox("ss_pw", "Password protected", &st->password)) st->applied = false;
    }
}

/* ── tab: Appearance ───────────────────────────────────────────────────────── */
static void tab_appearance(AppState *st) {
    static const char *const schemes[] = {
        "Windows Standard", "Desert", "Eggplant", "Rose", "Teal (VGA)",
    };
    static const char *const items[] = {
        "Desktop", "Active Title Bar", "Menu", "Window", "Icon",
    };
    rcColumn(.w = "grow", .h = "grow", .p = 14, .gap = 12) {
        field_label("Scheme:");
        rcBox(.w = "260px") {
            if (rcCombo("ap_scheme", &st->scheme, schemes, 5)) st->applied = false;
        }
        field_label("Item:");
        rcBox(.w = "260px") {
            if (rcCombo("ap_item", &st->item, items, 5)) st->applied = false;
        }
        rcRow(.gap = 10, .align = "cl") {
            field_label("Size:");
            rcBox(.w = "grow") {
                if (rcSlider("ap_size", &st->itemSize, 0.0f, 30.0f)) st->applied = false;
            }
        }
    }
}

/* ── layout ──────────────────────────────────────────────────────────────── */

static void layout(RC_App *app, void *userData) {
    AppState *st = (AppState *)userData;
    RC_Style  s  = rcGetStyle();

    rcColumn(.id = "Root", .w = "grow", .h = "grow", .bg = s.background) {

        /* Caption band: solid blue, white title, native drag + hand-rolled
           silver caption buttons (see caption_button). */
        /* Chrome, not content: RC_AppOptions.titlebarHeight freezes the OS drag
           strip in physical px, so a band that grew with the content zoom would
           stop matching the strip the OS lets you drag. Measured before this
           existed: at 2x zoom the drawn band was exactly twice the draggable one. */
        rcUnzoomed() {
            rcRow(.id = RC_ID_WINDOW_DRAG, .w = "grow", .h = "24px", .bg = s.chrome,
                   .px = 6, .gap = 8, .align = "cl") {
                rcTextL("Display Properties", .font = F_TITLE, .color = RC_WHITE);
                rcBox(.w = "grow") {}
                rcRow(.gap = 2, .align = "cl") {
                    caption_button(RC_ID_WINDOW_MINIMIZE, rcIconMinus);
                    /* One id, TWO actions: RC_ID_WINDOW_MAXIMIZE is maximise AND
                       restore, so the glyph must show which one the next click
                       does. rcIsWindowMaximized answers false on web, where
                       nothing maximises, so no #ifdef is needed. */
                    caption_button(RC_ID_WINDOW_MAXIMIZE,
                                   rcIsWindowMaximized() ? rcIconShrink : rcIconMaximize);
                    caption_button(RC_ID_WINDOW_CLOSE,    rcIconX);
                }
            }
        }

        rcColumn(.w = "grow", .h = "grow", .bg = s.background, .p = 10, .gap = 8) {

            /* Tab strip: three raised bevel buttons; the active one is sunken. */
            rcRow(.gap = 3) {
                if (bevel_button("tab_bg", "Background",    st->tab == 0)) st->tab = 0;
                if (bevel_button("tab_ss", "Screen Saver",  st->tab == 1)) st->tab = 1;
                if (bevel_button("tab_ap", "Appearance",    st->tab == 2)) st->tab = 2;
            }

            /* Sunken content well switching on the active tab. */
            SUNKEN_OPEN("content", .w = "grow", .h = "grow") {
                rcBox(.bg = s.surface, .w = "grow", .h = "grow") {
                    switch (st->tab) {
                    case 0: tab_background(st);           break;
                    case 1: tab_screensaver(app, st);     break;
                    case 2: tab_appearance(st);           break;
                    default: break;
                    }
                }
            }

            /* Bottom action row: an "Applied" note, then right-aligned OK / Cancel / Apply. */
            rcRow(.w = "grow", .gap = 6, .align = "cr") {
                if (st->applied)
                    rcTextL("Applied.", .font = F_BODY, .color = RC_GRAY_700);
                rcBox(.w = "grow") {}
                if (bevel_button("act_ok",     "OK",     false)) rcAppRequestClose(app);
                if (bevel_button("act_cancel", "Cancel", false)) rcAppRequestClose(app);
                if (bevel_button("act_apply",  "Apply",  false)) st->applied = true;
            }
        }
    }
}

/* ── entry point ───────────────────────────────────────────────────────────── */

int main(void) {
    AppState state = {
        .tab       = 0,
        .wallpaper = 1,
        .placement = 0,
        .pattern   = true,
        .saver     = 1,
        .wait      = 15.0f,
        .password  = false,
        .scheme    = 0,
        .item      = 1,
        .itemSize  = 10.0f,
    };

    static const float fontSizes[F_COUNT] = {
        [F_BODY]  = 14.0f,
        [F_TITLE] = 15.0f,
    };

    /* Custom Win95 palette: silver surfaces, blue caption, square corners. */
    RC_Style s      = rcStyleLight();
    s.background     = RC_GRAY_300;
    s.surface        = RC_GRAY_300;
    s.surfaceAlt     = RC_GRAY_200;
    s.chrome         = RC_BLUE_900;
    s.text           = RC_BLACK;
    s.textMuted      = RC_GRAY_700;
    s.border         = RC_GRAY_600;
    s.radius         = 0.0f;
    rcSetStyle(s);

    RC_AppOptions opts = {
        .width          = 460,
        .height         = 420,
        .title          = "Display Properties",
        .clearColor     = RC_TEAL_700,   /* the period teal - only ever seen if
                                            the opaque root does not cover the
                                            window, e.g. mid-resize */
        .fontSizes      = fontSizes,
        .fontCount      = F_COUNT,
        .scratchArenaBytes = 4096,   /* backs rcFormat (the "N min" readout) */
        .nativeFrame    = true,
        .titlebarHeight = 24,
        .titlebar       = { .custom = true },   /* we draw the Win95 bar ourselves */
        .layoutCallback       = layout,
        .userData       = &state,
    };

    return rcRunApp(&opts);
}
