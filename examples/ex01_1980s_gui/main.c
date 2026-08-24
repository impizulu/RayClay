/*
================================================================================
    main.c - RayClay "1980s desktop" example (early Macintosh calculator)

    A working four-function calculator in the strict 1-bit look of an early
    Macintosh (System 1, 1984): pure black-on-white, thin 1px black borders and
    square hand-drawn chrome. Same source -> desktop AND web. Zero-asset:
    bundled Latin-1 font baked at runtime; a hand-drawn close box; no images,
    no file loads.

    The whole aesthetic is one custom monochrome RC_Style installed at startup;
    the era's one window control is a 14px close box wired to the runner via
    RC_ID_WINDOW_CLOSE, and every key is a raw box that inverts on hover for a
    crisp 1-bit "pressed" feel.

    Build target: rayclay_ex01_1980s_gui
================================================================================
*/

#include "rayclay.h"

/* Font ladder baked from the bundled face at these sizes - zero-asset. */
typedef enum { F_BODY = 0, F_DISPLAY, F_COUNT } AppFont;

/* Calculator state (lives in .userData - zero per-frame heap allocation). */
typedef struct {
    double acc;    /* accumulated left-hand operand                       */
    double cur;    /* number currently being entered / shown              */
    char   op;     /* pending operator '+','-','*','/', or 0 for none     */
    bool   fresh;  /* true when the next digit should start a new number  */
} AppState;

/* ── pure state helpers (no libc - arithmetic only) ──────────────────────── */

/* Append one decimal digit (0-9) to the number being entered. */
static void calc_digit(AppState *st, int d) {
    if (st->fresh) {
        st->cur   = 0;
        st->fresh = false;
    }
    st->cur = st->cur * 10.0 + (double)d;
}

/* Fold the pending operator over (acc, cur) and store the result in acc. */
static double calc_eval(double acc, double cur, char op) {
    switch (op) {
    case '+': return acc + cur;
    case '-': return acc - cur;
    case '*': return acc * cur;
    case '/': return cur != 0.0 ? acc / cur : 0.0;
    default:  return cur;
    }
}

/* Press an operator key: resolve any pending op, then arm the next one. '=' is
   just calc_op(st, 0) - it folds the pending op and leaves none armed. The fold
   only runs when an operand was actually entered (fresh == false), so changing
   or repeating an operator never double-applies the current value. */
static void calc_op(AppState *st, char op) {
    if (!st->fresh)
        st->acc = st->op ? calc_eval(st->acc, st->cur, st->op) : st->cur;
    st->cur   = st->acc;
    st->op    = op;
    st->fresh = true;
}

/* Press 'C': back to a pristine zero. */
static void calc_clear(AppState *st) {
    st->acc = st->cur = 0;
    st->op    = 0;
    st->fresh = true;
}

/* ── one calculator key ──────────────────────────────────────────────────── */

/* A raw box restyled into a 1-bit key: white by default, INVERTED to black on
   hover (the era's press feedback). Returns true on the frame it is clicked. */
static bool key(const char *id, const char *label) {
    bool hot = rcIsHovered(id);
    rcBox(.id = id, .w = "grow", .h = "44px", .align = "cc",
           .bg = hot ? RC_BLACK : RC_WHITE,
           .border = { .color = RC_BLACK, .width = "1px" }) {
        rcTextC(label, .font = F_BODY, .color = hot ? RC_WHITE : RC_BLACK);
    }
    return rcClicked(id);
}

/* ── layout ──────────────────────────────────────────────────────────────── */

static void layout(RC_App *app, void *userData) {
    AppState *st = (AppState *)userData;

    /* Window body: one white sheet filling the frame. */
    rcColumn(.id = "Root", .w = "grow", .h = "grow", .bg = RC_WHITE) {
        /* Title bar - draggable; System-1 style: close box left, centred name.
           The 14px box fits the 22px bar (rcWindowControls' 38px cluster
           would overflow it) and closes via the RC_ID_WINDOW_CLOSE contract. */
        /* Chrome, not content: RC_AppOptions.titlebarHeight freezes the OS drag
           strip in physical px, so a band that grew with the content zoom would
           stop matching the strip the OS lets you drag. Measured before this
           existed: at 2x zoom the drawn band was exactly twice the draggable one. */
        rcUnzoomed() {
            rcRow(.id = RC_ID_WINDOW_DRAG, .w = "grow", .h = "22px", .bg = RC_WHITE,
                   .px = 8, .gap = 8, .align = "cl",
                   .border = { .color = RC_BLACK, .width = "1px" }) {
                bool over = rcIsHovered(RC_ID_WINDOW_CLOSE);
                rcBox(.id = RC_ID_WINDOW_CLOSE, .w = "14px", .h = "14px",
                       .bg = over ? RC_BLACK : RC_WHITE,
                       .border = { .color = RC_BLACK, .width = "1px" }) {}
                rcBox(.w = "grow", .align = "cc") {
                    rcTextL("Calculator", .font = F_BODY, .color = RC_BLACK);
                }
                /* Mirror spacer keeps the title optically centred. */
                rcBox(.w = "14px") {}
            }
        }

        /* Menu bar - File / Edit, black text on white, square.
           Deliberately no .h here. A fixed height does not shrink its
           children; they overflow it and draw through the bar's own bottom
           rule. Omitting .h means FIT (rayclay.h: NULL or "" -> FIT), so the
           bar is always exactly as tall as the triggers inside it. */
        rcRow(.id = "MenuBar", .w = "grow", .bg = RC_WHITE,
               .px = 8, .gap = 16, .align = "cl",
               .border = { .color = RC_BLACK, .width = "1px" }) {
            if (rcBeginMenu("m_file", "File")) {
                if (rcMenuItem("Quit")) rcAppRequestClose(app);
                rcEndMenu();
            }
            if (rcBeginMenu("m_edit", "Edit")) {
                if (rcMenuItem("Clear")) calc_clear(st);
                rcEndMenu();
            }
        }

        /* Calculator face. */
        rcColumn(.id = "Face", .w = "grow", .h = "grow", .p = 12, .gap = 8) {
            /* Right-aligned numeric display. */
            rcBox(.id = "Display", .w = "grow", .h = "48px", .align = "cr",
                   .px = 10, .bg = RC_WHITE,
                   .border = { .color = RC_BLACK, .width = "1px" }) {
                RC_String v = rcFormat(rcAppArena(app), "%g", st->cur);
                rcText(v, .font = F_DISPLAY, .color = RC_BLACK);
            }
            /* 4-column button grid. */
            rcRow(.w = "grow", .gap = 8) {
                if (key("k7", "7")) calc_digit(st, 7);
                if (key("k8", "8")) calc_digit(st, 8);
                if (key("k9", "9")) calc_digit(st, 9);
                if (key("kdiv", "\xc3\xb7")) calc_op(st, '/');
            }
            rcRow(.w = "grow", .gap = 8) {
                if (key("k4", "4")) calc_digit(st, 4);
                if (key("k5", "5")) calc_digit(st, 5);
                if (key("k6", "6")) calc_digit(st, 6);
                if (key("kmul", "\xc3\x97")) calc_op(st, '*');
            }
            rcRow(.w = "grow", .gap = 8) {
                if (key("k1", "1")) calc_digit(st, 1);
                if (key("k2", "2")) calc_digit(st, 2);
                if (key("k3", "3")) calc_digit(st, 3);
                if (key("ksub", "-")) calc_op(st, '-');
            }
            rcRow(.w = "grow", .gap = 8) {
                if (key("kclr", "C")) calc_clear(st);
                if (key("k0", "0")) calc_digit(st, 0);
                if (key("keq", "=")) calc_op(st, 0);
                if (key("kadd", "+")) calc_op(st, '+');
            }
        }
    }
}

/* ── entry point ─────────────────────────────────────────────────────────── */

int main(void) {
    AppState state = { .cur = 0 };

    static const float fontSizes[F_COUNT] = {
        [F_BODY]    = 18.0f,
        [F_DISPLAY] = 30.0f,
    };

    /* Strict 1-bit monochrome: black on white. Built from Light so widgets
       (menus, controls) inherit sane metrics, then flattened.
       Note: .radius is an app-applied default (rc_theme.h: "metrics an app can
       apply to its containers"), not something the widget layer reads - so
       setting it to 0 does not square rcBeginMenu's chips, which carry the
       widget layer's own corner. The square look here is the chrome this file
       draws itself. */
    RC_Style mono   = rcStyleLight();
    mono.background = RC_WHITE;
    mono.surface    = RC_WHITE;
    mono.surfaceAlt = RC_WHITE;
    mono.chrome     = RC_WHITE;
    mono.text       = RC_BLACK;
    mono.textMuted  = RC_BLACK;
    mono.border     = RC_BLACK;
    mono.primary    = RC_BLACK;
    mono.radius     = 0.0f;
    rcSetStyle(mono);

    RC_AppOptions opts = {
        .width          = 320,
        .height         = 360,
        .title          = "Calculator",
        .fontSizes      = fontSizes,
        .fontCount      = F_COUNT,
        .scratchArenaBytes = 4096,   /* backs rcFormat (the display readout) */
        .nativeFrame    = true,
        .titlebarHeight = 22,
        .titlebar       = { .custom = true },   /* we draw the System-1 bar ourselves */
        .layoutCallback       = layout,
        .userData       = &state,
    };

    return rcRunApp(&opts);
}
