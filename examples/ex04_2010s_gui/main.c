/*
================================================================================
    main.c - RayClay ex04: 2010s Material Design tasks app
================================================================================

    A flat Material-Design (circa 2014) inbox/tasks app: a bold INDIGO app bar,
    a PINK floating action button, WHITE cards with soft drop shadows on a light
    gray canvas, medium-radius corners and generous whitespace - the Google
    Material look of the early 2010s. Add tasks, check them off (they mute + get
    a "done" flag), delete a card, and filter completed items with a toggle.

    Same source -> desktop AND web (no #ifdef; .nativeFrame is ignored on web).
    Zero-asset: the bundled Latin-1 font is baked at runtime; icons are drawn
    procedurally. No files are loaded.

    Aesthetic: 2010s / flat Material Design (Material 2014).

    Build target: rayclay_ex04_2010s_gui
================================================================================
*/

#include "rayclay.h"

#include "icons/rc_icons_x.h"

/* ── types ───────────────────────────────────────────────────────────────── */

#define MAX_TASKS 32

/* Font ladder baked from the bundled face at these sizes - zero-asset. */
typedef enum { F_SMALL = 0, F_BODY, F_TITLE, F_COUNT } AppFont;

typedef struct {
    char text[48];
    bool done;
} Task;

typedef struct {
    Task  tasks[MAX_TASKS];
    int   count;
    char  input[48];   /* new-task text-input buffer            */
    bool  showDone;    /* Settings: include completed in the list */
    int   pendingDelete; /* -1 = none. Applied at the top of the next frame -
                            see the note at delete_task. Note: -1, not 0, since
                            a zero-init would mean "delete task 0". */
} AppState;

/* ── helpers ─────────────────────────────────────────────────────────────── */

static void add_task(AppState *st) {
    if (st->count >= MAX_TASKS || !st->input[0])
        return;
    rcStrCopy(st->tasks[st->count].text, st->input, sizeof st->tasks[0].text);
    st->tasks[st->count].done = false;
    st->count++;
    st->input[0] = '\0';
}

static void delete_task(AppState *st, int i) {
    for (int j = i; j < st->count - 1; j++)
        st->tasks[j] = st->tasks[j + 1];
    st->count--;
}

/* ── app bar ─────────────────────────────────────────────────────────────── */

/* The indigo Material app bar doubles as the native drag region (desktop) and
   carries the title and the window controls. */
static void appbar(void) {
    /* Chrome, not content. RC_AppOptions.titlebarHeight freezes the OS drag
       strip in physical px, so a band that grew with the content zoom would
       stop matching the strip the OS lets you drag. Measured before this
       existed: at 2x zoom the drawn band was exactly twice the draggable one. */
    rcUnzoomed() {
        rcRow(.id = RC_ID_WINDOW_DRAG, .w = "grow", .h = "56px", .bg = RC_INDIGO_500,
               .px = 16, .gap = 12, .align = "cl",
               .shadow = { rcAlpha(RC_BLACK, 40), 0, 2, 6, 0 }) {
            rcTextL("Tasks", .font = F_TITLE, .color = RC_WHITE);
            rcBox(.w = "grow") {}
            rcWindowControls();
        }
    }
}

/* ── one task card ───────────────────────────────────────────────────────── */

/* A white Material card: soft shadow, medium radius, a bound checkbox, the task
   text (muted colour when done - no strikethrough), and a delete X wired
   through rcClicked. Returns the requested delete index, or -1. */
static int task_card(RC_App *app, AppState *st, int i) {
    RC_Arena   *mem = rcAppArena(app);
    RC_Style    s   = rcGetStyle();
    const char *card_id  = rcFormat(mem, "card_%d", i).chars;
    const char *check_id = rcFormat(mem, "chk_%d",  i).chars;
    const char *del_id   = rcFormat(mem, "del_%d",  i).chars;
    int del = -1;

    rcBox(.id = card_id, .w = "grow", .bg = s.surface, .px = 12, .py = 8,
           .borderRadius = "all-lg",
           .shadow = { rcAlpha(RC_BLACK, 28), 0, 2, 8, 0 }) {
        rcRow(.w = "grow", .align = "cl", .gap = 12) {
            rcCheckbox(check_id, "", &st->tasks[i].done);
            rcBox(.w = "grow", .overflow = "hidden") {
                rcTextC(st->tasks[i].text, .font = F_BODY,
                         .color = st->tasks[i].done ? s.textMuted : s.text,
                         .wrap = "n");
            }
            rcBox(.id = del_id, .w = "32px", .h = "32px", .align = "cc",
                   .bg = rcIsHovered(del_id) ? s.surfaceAlt : RC_TRANSPARENT,
                   .borderRadius = "all-full") {
                rcIconX(16.0f, s.textMuted);
            }
        }
    }
    if (rcClicked(del_id))
        del = i;
    return del;
}

/* ── layout ──────────────────────────────────────────────────────────────── */

static void layout(RC_App *app, void *userData) {
    AppState *st  = (AppState *)userData;
    RC_Arena *mem = rcAppArena(app);
    RC_Style  s   = rcGetStyle();

    /* Load-bearing: the delete lands here, before anything is drawn - not at the
       end of the frame that requested it. rcTextC does not copy: the card below
       hands RayClay a pointer straight into st->tasks[i].text, and the library
       keeps that pointer until the frame is drawn. delete_task compacts the
       array, so compacting mid-frame would leave every retained pointer aimed one
       slot high and the cards below the deleted one would draw the wrong text.
       Deferring past the layout callback entirely is what makes it safe. */
    if (st->pendingDelete >= 0) {
        delete_task(st, st->pendingDelete);
        st->pendingDelete = -1;
    }

    int del = -1;   /* set by a card this frame; applied at the top of the next */

    rcColumn(.id = "Root", .w = "grow", .h = "grow", .bg = s.background) {

        appbar();

        rcColumn(.w = "grow", .h = "grow", .p = 20, .gap = 16, .bg = s.background) {

            /* ── add-task card ─────────────────────────────────────────── */
            rcRow(.id = "addcard", .w = "grow", .align = "cl", .gap = 10,
                   .bg = s.surface, .px = 12, .py = 8, .borderRadius = "all-lg",
                   .shadow = { rcAlpha(RC_BLACK, 28), 0, 2, 8, 0 }) {
                rcBox(.w = "grow") {
                    rcTextInput("new", st->input, sizeof st->input,
                                 .placeholder = "Add a task");
                }
                if (rcButton("btn_add", "Add", RC_BTN_PRIMARY))
                    add_task(st);
            }

            /* ── status + filter row ───────────────────────────────────── */
            /* Kept above the list so the bottom-right FAB never covers the
               "Show completed" toggle. */
            rcRow(.w = "grow", .align = "cl", .gap = 10) {
                /* Deliberately NOT a frame counter. A live per-frame readout is
                   itself an animation: the text changes every frame, so the app
                   can never park and the on-demand idle win (1.08 -> 0.00
                   CPU-s/min) is lost to redrawing a number nobody reads. */
                RC_String info = rcFormat(mem, "%d task%s",
                                             st->count, st->count == 1 ? "" : "s");
                rcText(info, .font = F_SMALL, .color = s.textMuted);
                rcBox(.w = "grow") {}
                rcTextL("Show completed", .font = F_SMALL, .color = s.textMuted);
                rcToggle("tg_done", &st->showDone);
            }

            /* ── task list (scrollable) ────────────────────────────────── */
            /* Counted HERE, not at the top of layout(): the "add" button above
               runs first and can raise st->count, so a count taken before it
               would show "All clear" on the very frame a task appeared. */
            int shown = 0;
            for (int i = 0; i < st->count; i++)
                if (st->showDone || !st->tasks[i].done) shown++;

            rcColumn(.id = "list", .w = "grow", .h = "grow", .scroll = "v",
                      .gap = 10) {
                if (shown == 0) {
                    rcColumn(.w = "grow", .h = "grow", .align = "cc", .gap = 6) {
                        rcTextL("All clear", .font = F_TITLE, .color = s.textMuted);
                        rcTextL("Add a task above, or tap the + button.",
                                 .font = F_SMALL, .color = s.textMuted);
                    }
                }
                for (int i = 0; i < st->count; i++) {
                    if (!st->showDone && st->tasks[i].done)
                        continue;
                    int d = task_card(app, st, i);
                    if (d >= 0) del = d;
                }
            }
        }
    }

    /* Record it and ask for one more frame; the delete itself happens at the
       top of that frame (see the note in layout()). Ask explicitly rather than
       relying on anything else to wake the app: the deferral is only correct if
       a frame actually follows, and saying so here is one line. */
    if (del >= 0) {
        st->pendingDelete = del;
        rcAppRequestFrame(app);
    }

    /* Scrollbar + FAB are floating - place them OUTSIDE the root column. */
    rcScrollbar("list");

    /* Floating action button: a circular pink Material FAB pinned bottom-right.
       Clicking it moves focus to the input (or adds the task if one is typed). */
    rcBox(.id = "fab",
           .floating = { .to = RC_ATTACH_ROOT, .parent = RC_ANCHOR_BOTTOM_RIGHT,
                         .element = RC_ANCHOR_BOTTOM_RIGHT, .offset = { -24, -24 } },
           .bg = rcIsHovered("fab") ? RC_PINK_500 : RC_PINK_400,
           .shadow = { rcAlpha(RC_BLACK, 80), 0, 4, 12, 0 },
           .borderRadius = "all-full", .w = "56px", .h = "56px", .align = "cc") {
        rcTextL("+", .font = F_TITLE, .color = RC_WHITE);
    }
    if (rcClicked("fab")) {
        if (st->input[0]) {
            add_task(st);
            /* The FAB sits OUTSIDE the root column, so this click is read after
               the list was built: the new task belongs to the next frame, and on
               demand that frame has to be asked for. */
            rcAppRequestFrame(app);
        } else {
            rcSetFocus("new");
        }
    }
}

/* ── app callbacks ───────────────────────────────────────────────────────── */

/* No updateCallback: nothing here changes except in response to input, so the runner
   parks in the OS event loop at ~0 CPU between clicks. That is the default
   and the right shape for an ordinary UI - see ex03/ex10 for apps that DO need
   to ask for frames, and ex12 for the one that needs every frame. */

/* ── entry point ─────────────────────────────────────────────────────────── */

int main(void) {
    static const char *const starters[] = {
        "Ship the Material tasks example",
        "Bake the font at runtime (zero-asset)",
        "Try the pink action button",
    };

    static AppState state = { .showDone = true, .pendingDelete = -1 };
    state.count = 3;
    for (int i = 0; i < 3; i++)
        rcStrCopy(state.tasks[i].text, starters[i], sizeof state.tasks[0].text);
    state.tasks[1].done = true;   /* seed one completed task */

    static const float fontSizes[F_COUNT] = {
        [F_SMALL] = 13.0f,
        [F_BODY]  = 16.0f,
        [F_TITLE] = 20.0f,
    };

    /* Light Material palette: gray canvas, white cards, indigo chrome/accent. */
    RC_Style st = rcStyleLight();
    st.background = RC_GRAY_100;
    st.surface    = RC_WHITE;
    st.chrome     = RC_INDIGO_500;
    st.text       = RC_GRAY_900;
    st.textMuted  = RC_GRAY_500;
    st.primary    = RC_INDIGO_500;
    st.radius     = 8.0f;
    rcSetStyle(st);

    RC_AppOptions opts = {
        .width             = 720,
        .height            = 620,
        .title             = "Tasks - RayClay",
        .clearColor        = RC_GRAY_100,
        .fontSizes         = fontSizes,
        .fontCount         = F_COUNT,
        .nativeFrame       = true,
        .titlebarHeight    = 56,
        .titlebar          = { .custom = true },   /* the Material app bar IS the titlebar */
        .layoutCallback          = layout,
        .userData          = &state,
        .scratchArenaBytes = 4096,
    };
    return rcRunApp(&opts);
}
