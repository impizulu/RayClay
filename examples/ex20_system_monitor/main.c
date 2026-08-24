/* ============================================================================
 *  ex20 - System Monitor
 * ============================================================================
 *
 *  A live resource monitor: an animated branded titlebar you can fold away with
 *  a keystroke, four stat cards, a rolling load chart, a per-core strip and a
 *  sortable 128-row process table. Desktop and web from this one source, no
 *  #ifdef, no asset files.
 *
 *  WHY THIS APP EXISTS, beyond looking like a monitor. Three things in RayClay
 *  have no worked example anywhere else, and all three are things you hit on
 *  your first serious app:
 *
 *  1. A BRANDED WINDOW. `.titlebar.custom` hands you the band; rcWindowControlButton
 *     puts the real minimise/maximise/close actions wherever you draw them, and
 *     RC_ID_WINDOW_DRAG / RC_ID_WINDOW_NODRAG decide what moves the window and
 *     what stays clickable. Press the primary modifier + T to fold the bar down
 *     to a drag rail and back. As it folds, rcAppSetTitlebarHeight walks the OS
 *     drag strip down with it - the band you DRAW and the strip the OS DRAGS BY
 *     are two different rectangles, and keeping them equal is your job.
 *     Measured on Windows 2026-08-21 by asking the window manager directly
 *     (WM_NCHITTEST): open, the caption runs to exactly .titlebarHeight and
 *     stops; folded, it is gone; reopened, it returns. The setter does reach
 *     the OS. But the top SM_CYSIZEFRAME + SM_CXPADDEDBORDER pixels are the
 *     RESIZE BORDER, which outranks the caption - 4 + 4 = 8 at 96 DPI, more on
 *     a high-DPI display. A strip that thin therefore has no draggable pixel
 *     left on Windows, which is why the 5 px rail below is not a drag region
 *     there. Both metrics scale with DPI, so no fixed pixel value is safe.
 *
 *  2. YOUR OWN ELEMENT MACRO. rcBeginComponent / rcEndComponent exist so you can
 *     build a `sysCard(...)` that takes the same designated-initialiser options
 *     rcBox does. `card` below is that, in full, with the trade-offs written down.
 *
 *  3. PACING WORK IN AN ON-DEMAND APP. A RayClay window parks when
 *     nothing changes, and a monitor is the app that most wants to wake on a
 *     cadence. How that is done here - and the sharp edge that shaped it - is in
 *     the comment above `update`. Read that one before copying this file.
 *
 *  REAL NUMBERS AND SIMULATED ONES ARE KEPT APART, ON PURPOSE. The two cards on
 *  the left are this process, read through rcProcessCpuPercent and
 *  rcProcessMemoryBytes, and are true on every desktop target. Everything else
 *  - cores, host memory, the NIC, the process table - comes from
 *  sysmon_host.h, which is one deterministic model behind two functions. Replace
 *  those two with /proc, sysctl or the Windows performance counters and this file
 *  does not change by a line. A portable C library cannot read a host's process
 *  table, and pretending otherwise would either break "the same picture on every
 *  target" or bury three collectors in an example about layout.
 *
 *  Zero-asset: the bundled font and procedural icon headers, nothing to install.
 * ========================================================================= */

#include "rayclay.h"

#include "icons/rc_icons_rayclay_logo.h"
#include "icons/rc_icons_chart_column.h"
#include "icons/rc_icons_minimize.h"
#include "icons/rc_icons_maximize.h"
#include "icons/rc_icons_shrink.h"
#include "icons/rc_icons_x.h"

#include "sysmon_host.h"

/* Font slots, in load order into RC_AppOptions.fontSizes; the index is the
   .font value in RC_TextOptions. Baked from the bundled face (zero-asset). */
enum { F_MICRO = 0, F_SMALL, F_BODY, F_STAT, F_COUNT };

enum {
    HISTORY      = 90,   /* samples kept for the charts                        */
    BAR_OPEN_H   = 48,   /* titlebar height, expanded                          */
    BAR_RAIL_H   = 5,    /* ... and folded. Measured: NOT draggable on Windows  */
    ROW_H        = 26,   /* process-table CELL height - NOT the row pitch      */
    CELL_PAD     = 4,    /* explicit, because 0 does not mean 0; see the table  */
    ROW_PITCH    = ROW_H + 2 * CELL_PAD
};

/* Seconds between samples when nothing else is going on. rcProcessCpuPercent
   asks for "a steady, coarse cadence (~1 Hz)" because OS CPU accounting is
   1-10 ms granular, so anything faster reads noise rather than load. */
#define SAMPLE_PERIOD 1.0

/* Fraction of the remaining distance an animation covers per frame, and the
   distance at which it snaps and stops asking for frames. See ease_to.

   These two numbers ARE the app's idle cost, which is not obvious until you
   measure it. A new sample every second retargets the load rail, so the app is
   awake for however long the ease takes: at k=0.22 with a 0.25 snap on a 0..100
   scale that was ~25 frames per sample - 107 of 115 frames in an 8 s run were
   the animation asking for itself, and the window never parked. At k=0.35 with
   a 1.0 snap the same motion reads the same and settles in about a third of
   that. An easing curve is a power budget. */
#define EASE_K    0.35f
#define EASE_SNAP 1.0f

/* Sortable columns. ONE table drives both the clickable header row and the
   rcBeginTable column widths, so the two can never drift apart - two arrays
   holding the same widths is a bug waiting for the first column resize. */
typedef enum SortKey {
    SORT_NAME = 0, SORT_PID, SORT_CPU, SORT_MEM, SORT_STATE, SORT_COUNT
} SortKey;

/* `right` rather than an align string: RC_ComponentOptions.align is a char[3],
   which C99 lets you initialise from a string LITERAL but not from a pointer
   expression, so a per-column align code cannot be forwarded into .align. The
   header cells align with a grow spacer instead, which is the flexbox answer
   anyway; the body cells pass their literal directly. */
static const struct { const char *label, *w; bool right; } COLUMN[SORT_COUNT] = {
    { "PROCESS", "grow",  false },
    { "PID",     "78px",  true  },
    { "CPU %",   "88px",  true  },
    { "MEMORY",  "104px", true  },
    { "STATE",   "96px",  false }
};

typedef struct AppState {
    SysHost host;

    /* This process, read from RayClay. selfCpu is -1 where the platform has no
       process view at all (a browser tab), which the card renders as a dash
       rather than a zero - a zero would be a lie about a busy tab. */
    float   selfCpu;
    size_t  selfMem;

    /* History, oldest first, newest last. Plain arrays rather than ring buffers
       because rcChart and rcSparkline take a contiguous span: a ring would have
       to be linearised into a scratch copy every frame anyway, so the memmove
       below is the cheaper of the two and much the clearer. */
    float   cpuHist[HISTORY];   /* simulated: busiest core                  */
    float   memHist[HISTORY];   /* simulated: host memory used, percent      */
    float   selfHist[HISTORY];  /* REAL: this process's own CPU              */
    int     samples;
    double  lastSampleAt;       /* rcAppTime() when the last real sample ran  */

    /* Process table. `order` is an index permutation - the model is never
       reordered, so a selection survives a re-sort and the simulated pids stay
       put. Re-sorted after each sample, which is why it is insertion sort. */
    int     order[SYS_PROCS];
    SortKey sortKey;
    bool    sortDesc;

    /* Titlebar fold + the animation that carries it. */
    bool    barOpen;
    float   barH;        /* animated, px                                       */
    float   loadShown;   /* animated 0..100, drives the bar's load rail        */

    bool    animating;   /* an ease is still in flight; ask for another frame */
    bool    primed;      /* the history and the CPU interval are seeded        */
} AppState;

/* ===========================================================================
   YOUR OWN ELEMENT MACRO

   rcBox / rcRow / rcColumn are rcComponent with a different defaults record.
   rcBeginComponent + rcEndComponent are public precisely so an app can write a
   fourth one, and `card` is that: it takes the same designated-initialiser
   options every other element takes, and layers a surface colour, padding,
   radius and border underneath them, so a call site says only what is different
   about THAT card.

   WHY THE DEFAULTS GO IN THE SECOND ARGUMENT AND NOT INTO THE OPTIONS LITERAL.
   The obvious version of this macro pastes its defaults in front of __VA_ARGS__
   inside one designated-initialiser list and lets a later designator win. That
   compiles, and it warns (-Winitializer-overrides) at every call site that
   overrides a default - which is every interesting one. rcBeginComponent takes
   a SEPARATE base declaration precisely so the two do not have to share a
   literal, and rcParseComponentOptions turns an options record into that base,
   so the defaults can still be written in the same vocabulary as the call site.
   Three public functions instead of two, and this is what the third is for.

   Three honest caveats, because a macro you copy should come with them:

   - The bundled macros detect a `goto` or `return` out of the element body and
     say so; that check lives behind a private symbol, so a hand-rolled pair
     cannot make it. Escaping an element body is unsupported either way (you get
     a completed frame that is the WRONG frame) - here you just get no warning.
     `break` and `continue` are safe, as they are in rcBox.
   - A default is only overridable to a NON-ZERO value. The options record has
     no "unset" state distinct from zero, so `.gap = 0` at a call site reads the
     same as saying nothing and the default wins. Put a field here only when you
     will never need zero back; everything else belongs at the call site. That
     is why .bg is not defaulted below.
   - A ZERO BASE DECLARATION LAYS ITS CHILDREN OUT LEFT TO RIGHT, and nothing in
     RC_ComponentOptions can change that - direction lives in the base record,
     and the bases that carry it are the library's own rci_core_row_defaults and
     rci_core_column_defaults, which an app should not reach for. So this macro
     opens the card and then an rcColumn inside it: the card is the padded,
     bordered surface, the column is the stack. Discovered by looking at the
     window, not by reading the code -
     it compiles perfectly either way and every card is simply sideways.
   =========================================================================== */

static RC_ElementDeclaration card_defaults(void)
{
    RC_ElementDeclaration zero = { 0 };

    return rcParseComponentOptions(
        (RC_ComponentOptions){ ._reserved = 0, .p = 14,
                               .borderRadius = "all-lg" }, zero);
}

#define SYS__JOIN2(a, b) a##b
#define SYS__JOIN(a, b)  SYS__JOIN2(a, b)

/* The outer loop runs the body once and always closes the card; rcColumn brings
   its own matching pair and its own `break` scoping, so the body reads exactly
   like any other element body. __LINE__ keeps the guard unique so cards nest. */
#define card(...) SYS__CARD_(SYS__JOIN(sys__card_, __LINE__), __VA_ARGS__)
#define SYS__CARD_(guard, ...)                                                 \
    for (int guard = 1;                                                        \
         guard && (rcBeginComponent(                                           \
                       (RC_ComponentOptions){ ._reserved = 0, __VA_ARGS__ },   \
                       card_defaults()), 1);                                   \
         rcEndComponent(), guard = 0)                                          \
        rcColumn(.w = "grow", .h = "grow", .gap = 8)

/* ===========================================================================
   Small helpers
   =========================================================================== */

/* Move *cur a fixed fraction of the way to target; snap and report "arrived"
   when the gap closes. Returns true while still moving, which is exactly the
   condition for asking the runner for another frame - so an animation here
   costs frames only while it is visibly happening and then parks.

   Frame-rate dependent by construction: at 144 Hz this settles sooner than at
   60. A time-based curve would need a wall clock, and RayClay does not expose
   one (see update()). For a 200 ms chrome transition the difference is not
   perceptible; for anything longer, drive it from real elapsed time. */
static bool ease_to(float *cur, float target, float k, float snap)
{
    float gap = target - *cur;

    if (gap < 0.0f) gap = -gap;
    if (gap <= snap) {
        *cur = target;
        return false;
    }
    *cur += (target - *cur) * k;
    return true;
}

/* Load-band colour, from the theme rather than the raw palette, so an app that
   installs its own RC_Style gets its own greens and ambers. The BASES are the
   600 shades and the *Hover tokens the 500s; the lighter ones read better as a
   fill behind text, which is why the healthy states use Hover. */
static RC_Color load_color(float percent)
{
    RC_Style s = rcGetStyle();

    if (percent >= 85.0f) return s.danger;
    if (percent >= 60.0f) return s.warningHover;
    return s.successHover;
}

static const char *state_label(SysProcState st)
{
    switch (st) {
    case SYS_RUNNING: return "running";
    case SYS_STOPPED: return "stopped";
    default:          return "sleeping";
    }
}

/* Push one value onto a fixed-length history, oldest out at the front. */
static void history_push(float *hist, int n, float v)
{
    int i;

    for (i = 0; i + 1 < n; i++)
        hist[i] = hist[i + 1];
    hist[n - 1] = v;
}

/* ===========================================================================
   Sorting

   Insertion sort, and deliberately not qsort. Three reasons, in the order they
   mattered: C99's qsort has no context parameter, so the sort key would have to
   become a file-static that the comparator reads - a hidden input to a pure
   function; the array is already almost ordered on every re-sort but the first,
   which is the case insertion sort is linear on and quicksort is not; and 128
   rows once a second is not a place to spend a library call. Flip the column
   and it is O(n^2) once, on 128 items, in a frame that was going to redraw
   anyway.
   =========================================================================== */

/* Ordering for one column. Returns true when a must come before b under the
   current key, ascending; the caller applies the direction. */
static bool proc_before(const SysHost *h, SortKey key, int a, int b)
{
    const SysProc *p = &h->proc[a], *q = &h->proc[b];

    switch (key) {
    case SORT_PID:   return p->pid    < q->pid;
    case SORT_CPU:   return p->cpu    < q->cpu;
    case SORT_MEM:   return p->memMiB < q->memMiB;
    case SORT_STATE: return p->state  < q->state;
    case SORT_NAME:
    default: {
        const char *x = p->name, *y = q->name;
        while (*x && *x == *y) { x++; y++; }
        return (unsigned char)*x < (unsigned char)*y;
    }
    }
}

static void resort(AppState *st)
{
    int i, j;

    for (i = 1; i < st->host.procCount; i++) {
        int key = st->order[i];

        for (j = i - 1; j >= 0; j--) {
            bool before = proc_before(&st->host, st->sortKey, key, st->order[j]);

            if (st->sortDesc) before = !before;
            if (!before) break;
            st->order[j + 1] = st->order[j];
        }
        st->order[j + 1] = key;
    }
}

/* ===========================================================================
   Sampling
   =========================================================================== */

/* @p readReal is false only while priming the charts on the first frame. A rate
   needs an interval, and 90 back-to-back calls have none between them, so
   priming the REAL series would fill it with 90 meaningless readings and then
   draw them. The simulated series has no such problem - it advances one step
   per call by definition - so only the real half opts out. */
static void sample(AppState *st, bool readReal)
{
    float busiest = 0.0f;
    int i;

    sysmon_host_sample(&st->host);

    /* The only two readings in this app that are real. rcProcessCpuPercent
       measures the interval since the PREVIOUS call, so calling it exactly once
       per sample is what makes the number mean anything; its first call has no
       interval and answers 0, and on web there is no OS process behind the tab
       at all and it answers -1. */
    if (readReal) {
        st->selfCpu = rcProcessCpuPercent();
        st->selfMem = rcProcessMemoryBytes();
    }

    for (i = 0; i < SYS_CORES; i++)
        if (st->host.core[i] > busiest) busiest = st->host.core[i];

    /* Only a real reading enters the real history: -1 is "no such thing on
       this platform", and charting it would draw a line below the axis. */
    if (readReal && st->selfCpu >= 0.0f)
        history_push(st->selfHist, HISTORY, st->selfCpu);
    history_push(st->cpuHist, HISTORY, busiest);
    history_push(st->memHist, HISTORY,
                 st->host.memUsedMiB / st->host.memTotalMiB * 100.0f);
    st->samples++;
    resort(st);
}

/* ===========================================================================
   PACING AN ON-DEMAND APP - read this before copying the file

   A RayClay window parks when nothing changes, so the app must decide
   when to wake. rcAppRequestFrameAfter is that: "wake me in N seconds", re-armed
   every frame here, which is the polled-deadline contract the whole on-demand
   design rests on.

   The other half is rcAppTime(app): monotonic seconds since the app started,
   the same call on desktop and in a browser. Sampling is gated on it, so the
   cadence is measured rather than inferred and the charts can honestly say
   seconds.

   Do not accumulate rcAppFrameTime instead - that is the trap this file was
   built around before the clock existed. It is smoothed, and above 1.0 s it is
   discarded rather than clamped (rc_app.c), and a parked app is exactly where
   that bites: measured on this tree, an on-demand app woken by a 1.0 s deadline
   reports a constant 0.100952 s per frame, so summing it over a 10 s run totals
   0.91 s - eleven times short. The same accumulator over a continuous run
   totals 9.95 s of the same 10, so it is the parking, not the instrument.
   rcAppFrameTime answers "how long was the last frame", never "what time is it".

   Why the gate is a wall clock and not an input test, and why that is worth
   copying: gating on "did the user do nothing this frame?" holds the history
   while the pointer is moving - a system monitor that stops monitoring while
   you use the mouse. A wall-clock gate samples on time whatever the user is doing, and it
   also makes rcProcessCpuPercent's interval uniform, which is what makes that
   number mean anything.
   =========================================================================== */

static void update(RC_App *app, void *userData)
{
    AppState *st  = userData;
    double    now = rcAppTime(app);

    /* Primary modifier + T folds the bar. RC_MOD_PRIMARY is Cmd on a native
       macOS build and Ctrl everywhere else, including a browser on a Mac -
       binding Ctrl by hand would be wrong on one of the two. The letter query
       is logical, not positional, so it is T on every keyboard layout. */
    if (rcModDown(RC_MOD_PRIMARY) && rcKeyPressed(RC_KEY_T))
        st->barOpen = !st->barOpen;

    if (!st->primed) {
        /* First frame: prime rcProcessCpuPercent (its first call has no
           interval to divide by) and fill the history so the charts open with
           a shape instead of a flat line growing in from the left. */
        int i;

        for (i = 0; i < HISTORY; i++)
            sample(st, false);
        sample(st, true);          /* one real read, to start the CPU interval */
        st->lastSampleAt = now;
        st->primed = true;
    } else if (now - st->lastSampleAt >= SAMPLE_PERIOD) {
        sample(st, true);
        st->lastSampleAt = now;
    }

    st->animating  = ease_to(&st->barH,
                             st->barOpen ? (float)BAR_OPEN_H : (float)BAR_RAIL_H,
                             EASE_K, EASE_SNAP);
    st->animating |= ease_to(&st->loadShown, st->cpuHist[HISTORY - 1],
                             EASE_K, EASE_SNAP);

    /* Keep the OS drag strip with the band. The band we draw animates between
       48 px and the 5 px rail, but the strip the OS lets the user drag by is a
       separate rectangle, fixed at .titlebarHeight when the window was created.
       Leave it alone and a folded bar keeps swallowing clicks across the full
       48 px the open bar covers. An unchanged value is a no-op, so this is free
       to call every frame and needs no "did it change?" bookkeeping. */
    rcAppSetTitlebarHeight(app, (int)st->barH);

    if (st->animating) rcAppRequestFrame(app);
    else               rcAppRequestFrameAfter(app, SAMPLE_PERIOD);
}

/* ===========================================================================
   The titlebar

   `.titlebar.custom` on the app means the runner draws nothing and this band is
   the whole chrome. The ids carry the behaviour: RC_ID_WINDOW_DRAG makes a
   region move the window, RC_ID_WINDOW_NODRAG carves a clickable hole back out
   of it, and rcWindowControlButton tags itself. All three are inert on web,
   where the browser tab is the chrome - the same source, no #ifdef, one target
   quietly drawing less.
   =========================================================================== */

static void window_controls(void)
{
    /* The middle slab shows what the NEXT click does - the one bit of chrome
       state a custom band could not read before rcIsWindowMaximized. It answers
       false on web, where nothing maximises and the slab emits nothing anyway,
       so the swap costs no #ifdef and no second source. */
    RC_IconCallback maxGlyph = rcIsWindowMaximized() ? rcIconShrink
                                                     : rcIconMaximize;

    /* Not wrapped in RC_ID_WINDOW_NODRAG: each control carries its own
       RC_ID_WINDOW_* id and the drag hit-test already treats those as holes. */
    rcRow(.gap = 2, .align = "cc") {
        rcWindowControlButton(RC_WINCTL_MINIMIZE, rcIconMinimize, 14.0f);
        rcWindowControlButton(RC_WINCTL_MAXIMIZE, maxGlyph,       14.0f);
        rcWindowControlButton(RC_WINCTL_CLOSE,    rcIconX,        14.0f);
    }
}

/* The load rail: a full-width gradient track whose lit portion follows the
   animated load. It is the reason the bar reads as alive at one sample per
   second - the value steps, the rail glides. */
static void load_rail(AppState *st, float height)
{
    RC_Style s = rcGetStyle();
    RC_Color lit = load_color(st->loadShown);
    float pct = st->loadShown;

    if (pct < 1.5f) pct = 1.5f;    /* keep a visible sliver at idle */

    rcBox(.id = "tb_rail_track", .w = "grow", .hType = RC_PX(height),
           .bg = rcAlpha(s.border, 90)) {
        rcBox(.id = "tb_rail_lit", .h = "grow", .wType = RC_PCT(pct),
               .gradient = { .from = rcAlpha(lit, 120), .to = lit, .dir = "h" }) {}
    }
}

static void titlebar(RC_App *app, AppState *st)
{
    RC_Style s = rcGetStyle();
    float railH = st->barH < (float)BAR_RAIL_H ? st->barH : (float)BAR_RAIL_H;
    float bandH = st->barH - railH;

    /* Chrome, not content. The OS drag strip is in physical px; without this
       scope the drawn band grows with the content zoom and stops matching the
       strip the OS lets you drag. The fold animation is unaffected - barH is
       still in the same px it always was.
       That is the zoom half. The fold half is rcAppSetTitlebarHeight in
       `update`, which walks the strip down with the band. Both are the same
       question - does the rectangle the OS drags by still match the one we
       drew - and a custom titlebar has to answer it on both axes. */
    rcUnzoomed() {
        rcColumn(.id = RC_ID_WINDOW_DRAG, .w = "grow", .hType = RC_PX(st->barH),
                  .bg = s.chrome) {
            /* The band collapses to nothing while the rail stays: a folded titlebar
               that kept no drag region would leave a borderless window that cannot
               be moved, and the keystroke that folded it is not discoverable from
               a window you have just made unmovable. */
            if (bandH >= 6.0f) {
                rcRow(.w = "grow", .hType = RC_PX(bandH), .px = 12, .gap = 10,
                       .align = "cl") {
                    rcIconRayClayLogo(22.0f);
                    rcColumn(.gap = 0) {
                        rcTextL("System Monitor", .font = F_BODY, .color = s.text);
                        rcTextL("RayClay " RC_VERSION " \xc2\xb7 one source, every target",
                                 .font = F_MICRO, .color = s.textMuted);
                    }
                    rcBox(.w = "grow") {}

                    /* A sparkline in the chrome. Nothing here is interactive, so it
                       needs no NODRAG - press it and the window moves, which is
                       what a titlebar should do. */
                    rcBox(.id = "tb_spark", .w = "132px", .h = "22px") {
                        rcSparkline("tb_spark_line", st->cpuHist, HISTORY,
                                     (RC_SparklineOptions){ .kind = RC_SERIES_AREA,
                                                            .color = load_color(st->loadShown),
                                                            .min = 0.0f, .max = 100.0f });
                    }
                    rcText(rcFormat(rcAppArena(app), "%3.0f%%", (double)st->loadShown),
                            .font = F_SMALL, .color = load_color(st->loadShown));

                    /* The fold chip IS interactive, so it opts out of the drag by
                       hand - the desktop twin of CSS -webkit-app-region: no-drag. */
                    rcRow(.id = RC_ID_WINDOW_NODRAG, .gap = 6, .align = "cc") {
                        if (rcButton("tb_fold", "fold", RC_BTN_GHOST))
                            st->barOpen = false;
                    }
                    window_controls();
                }
            }
            load_rail(st, railH);
        }
    }
}

/* Shown only while the bar is folded, so a pointer-only user is never stranded
   without the shortcut. A monitor is exactly the app people leave folded. */
static void fold_hint(AppState *st)
{
    RC_Style s = rcGetStyle();

    if (st->barOpen) return;
    rcRow(.id = "fold_hint", .px = 10, .py = 5, .gap = 8, .align = "cc",
           .bg = s.surfaceAlt, .borderRadius = "all-full",
           .border = { .color = s.border, .width = "1px" }) {
        rcTextL("titlebar folded", .font = F_MICRO, .color = s.textMuted);
        if (rcButton("hint_show", "show", RC_BTN_GHOST))
            st->barOpen = true;
    }
}

/* ===========================================================================
   Panels
   =========================================================================== */

/* One stat card, through the macro defined at the top of this file. */
/* @p sparkId must differ from @p id: a widget's id IS an element id, so reusing
   the card's id for the sparkline inside it declares the same id twice and the
   loser silently takes the winner's box. The runtime says so - "declared with a
   duplicate ID" - which is how this one was caught. */
static void stat_card(const char *id, const char *sparkId, const char *label,
                       const char *value, const char *unit, RC_Color accent,
                       const float *spark, float sparkMax, const char *tip)
{
    RC_Style s = rcGetStyle();

    card(.id = id, .w = "grow", .h = "grow", .bg = s.surface, .tooltip = tip,
          .border = { .color = s.border, .width = "1px" }) {
        rcTextC(label, .font = F_MICRO, .color = s.textMuted);
        rcRow(.gap = 4, .align = "bl") {
            rcTextC(value, .font = F_STAT, .color = accent);
            rcTextC(unit, .font = F_SMALL, .color = s.textMuted);
        }
        if (spark) {
            rcBox(.w = "grow", .h = "26px") {
                /* sparkMax 0 leaves the axis auto-fitting. Pin it (100 for a
                   percentage) when the series barely moves, or auto-fit
                   magnifies the noise into a solid block that reads as full;
                   leave it auto when the series can exceed the pin, or the
                   interesting part is clipped off the top - a process's CPU is
                   a percentage of ONE core and passes 100 routinely. */
                rcSparkline(sparkId, spark, HISTORY,
                             (RC_SparklineOptions){ .kind = RC_SERIES_AREA,
                                                    .color = accent,
                                                    .min = 0.0f, .max = sparkMax });
            }
        }
    }
}

static void cards(RC_App *app, AppState *st)
{
    RC_Arena *mem = rcAppArena(app);
    RC_Style s = rcGetStyle();
    const SysHost *h = &st->host;

    /* A FIXED height on the row, not fit. The card macro puts a growing column
       inside each card, and a growing child of a fit-height parent overflows the
       parent border - visible as a sparkline drawn below its own card. Sizing
       the row also stops one card with a sparkline making its neighbours short. */
    rcRow(.w = "grow", .h = "122px", .gap = 12) {
        /* -1 is "this platform has no process view", not "idle". A browser tab
           has no OS process to look at and a 0% would be a false reading. */
        stat_card("c_cpu", "c_cpu_spark", "THIS PROCESS \xc2\xb7 CPU",
                   /* "n/a", not an em dash: the bundled face is Latin-1, so
                      U+2014 draws as the missing-glyph box. Stay inside
                      Latin-1 unless you ship a font that covers more. */
                   st->selfCpu < 0.0f ? "n/a"
                                      : rcFormat(mem, "%.1f", (double)st->selfCpu).chars,
                   st->selfCpu < 0.0f ? "no reading here" : "% of one core",
                   st->selfCpu < 0.0f ? s.textMuted : s.primary,
                   st->selfCpu < 0.0f ? NULL : st->selfHist, 0.0f,
                   "Real: rcProcessCpuPercent(), sampled once per interval");

        stat_card("c_rss", "c_rss_spark", "THIS PROCESS \xc2\xb7 RESIDENT",
                   st->selfMem == 0 ? "n/a"
                       : rcFormat(mem, "%.1f", (double)st->selfMem / (1024.0 * 1024.0)).chars,
                   st->selfMem == 0 ? "no reading here" : "MiB", s.primary, NULL, 0.0f,
                   "Real: rcProcessMemoryBytes() - RSS, working set, or the wasm heap");

        stat_card("c_mem", "c_mem_spark", "HOST MEMORY",
                   rcFormat(mem, "%.1f", (double)(h->memUsedMiB / 1024.0f)).chars,
                   rcFormat(mem, "of %.0f GiB", (double)(h->memTotalMiB / 1024.0f)).chars,
                   load_color(h->memUsedMiB / h->memTotalMiB * 100.0f), st->memHist, 100.0f,
                   "Simulated: sysmon_host.h");

        stat_card("c_net", "c_net_spark", "NETWORK",
                   rcFormat(mem, "%.0f", (double)h->netRxKiB).chars,
                   rcFormat(mem, "KiB/s down \xc2\xb7 %.0f up", (double)h->netTxKiB).chars,
                   s.successHover, NULL, 0.0f,
                   "Simulated: sysmon_host.h");
    }
}

static const char *const CORE_FILL_ID[SYS_CORES] = {
    "core_fill_0", "core_fill_1", "core_fill_2", "core_fill_3",
    "core_fill_4", "core_fill_5", "core_fill_6", "core_fill_7"
};

static void core_strip(RC_App *app, AppState *st)
{
    RC_Arena *mem = rcAppArena(app);
    RC_Style s = rcGetStyle();
    int i;

    card(.id = "p_cores", .w = "300px", .h = "grow", .bg = s.surface,
          .border = { .color = s.border, .width = "1px" }) {
        rcRow(.w = "grow", .gap = 8, .align = "cl") {
            rcIconChartColumn(15.0f, s.textMuted);
            rcTextL("LOGICAL CORES", .font = F_MICRO, .color = s.textMuted);
        }
        for (i = 0; i < SYS_CORES; i++) {
            float v = st->host.core[i];

            rcRow(.w = "grow", .gap = 8, .align = "cc") {
                rcText(rcFormat(mem, "cpu%d", i), .font = F_MICRO,
                        .color = s.textMuted);
                rcBox(.w = "grow", .h = "10px", .bg = rcAlpha(s.border, 110),
                       .borderRadius = "all-full") {
                    /* Literal ids, not rcFormat'd ones. An id built in the
                       per-frame arena hashes correctly, but the debug inspector
                       and any duplicate-id report keep the POINTER and read it
                       after the arena has been reset - so ids want storage that
                       outlives the frame. */
                    rcBox(.id = CORE_FILL_ID[i], .h = "grow", .wType = RC_PCT(v),
                           .bg = load_color(v), .borderRadius = "all-full") {}
                }
                rcBox(.w = "40px", .align = "cr") {
                    rcText(rcFormat(mem, "%3.0f%%", (double)v), .font = F_MICRO,
                            .color = s.text);
                }
            }
        }
    }
}

static void load_chart(RC_App *app, AppState *st)
{
    RC_Style s = rcGetStyle();

    (void)app;
    card(.id = "p_chart", .w = "grow", .h = "grow", .bg = s.surface,
          .border = { .color = s.border, .width = "1px" }) {
        rcRow(.w = "grow", .gap = 8, .align = "cl") {
            rcTextL("BUSIEST CORE \xc2\xb7 LAST 90 SECONDS", .font = F_MICRO,
                     .color = s.textMuted);
            rcBox(.w = "grow") {}
            rcTextL("1 s per sample, timed by rcAppTime - see update()",
                     .font = F_MICRO, .color = s.textMuted);
        }
        rcBox(.w = "grow", .h = "grow") {
            RC_Series ser[] = {
                { .y = st->cpuHist, .count = HISTORY, .kind = RC_SERIES_AREA,
                  .label = "busiest core %", .color = s.primary }
            };
            rcChart("sys_chart", ser, 1,
                     (RC_ChartOptions){ .y = { .grid = true, .min = 0.0f, .max = 100.0f },
                                        .tooltip = RC_CHART_TOOLTIP_NEAREST });
        }
    }
}

/* The clickable header row. It is hand-drawn rather than left to rcBeginTable's
   own header because a header cell has to be a hit target, and the widths come
   from the same COLUMN table the body uses so the two columns always line up. */
static void table_header(AppState *st)
{
    RC_Style s = rcGetStyle();
    int i;

    rcRow(.w = "grow", .h = "30px", .bg = s.surfaceAlt, .gap = 0) {
        for (i = 0; i < SORT_COUNT; i++) {
            static const char *ID[SORT_COUNT] = {
                "th_name", "th_pid", "th_cpu", "th_mem", "th_state"
            };
            bool active = (st->sortKey == (SortKey)i);
            bool hot = rcIsHovered(ID[i]);

            rcRow(.id = ID[i], .w = COLUMN[i].w, .h = "grow", .px = 8, .gap = 4,
                   .align = "cl",
                   .bg = hot ? rcAlpha(s.primary, 30) : s.surfaceAlt) {
                if (COLUMN[i].right) rcBox(.w = "grow") {}
                rcTextC(COLUMN[i].label, .font = F_MICRO,
                         .color = active ? s.primary : s.textMuted);
                /* The caret only exists on the active column, so the header
                   never has to reserve space for one that is not there. */
                if (active) {
                    rcTextC(st->sortDesc ? "v" : "^",
                             .font = F_MICRO, .color = s.primary);
                }
                if (!COLUMN[i].right) rcBox(.w = "grow") {}
            }
            if (hot && rcPointerPressed(RC_POINTER_LEFT)) {
                if (active) {
                    st->sortDesc = !st->sortDesc;
                } else {
                    st->sortKey = (SortKey)i;
                    /* Text reads best A-Z, numbers biggest-first: the default
                       direction follows the column, not the last one used. */
                    st->sortDesc = (i != SORT_NAME);
                }
                resort(st);
            }
        }
    }
}

static void process_table(RC_App *app, AppState *st)
{
    RC_Arena *mem = rcAppArena(app);
    RC_Style s = rcGetStyle();
    RC_TableColumn cols[SORT_COUNT];
    int i;

    for (i = 0; i < SORT_COUNT; i++) {
        RC_TableColumn c = { 0 };

        c.header = NULL;                 /* the clickable header row is ours */
        c.w      = COLUMN[i].w;
        rcStrCopy(c.align, COLUMN[i].right ? "cr" : "cl", sizeof c.align);
        cols[i] = c;
    }

    card(.id = "p_procs", .w = "grow", .h = "grow", .bg = s.surface, .gap = 0,
          .border = { .color = s.border, .width = "1px" }) {
        rcRow(.w = "grow", .gap = 8, .align = "cl", .pb = 10) {
            rcTextL("PROCESSES", .font = F_MICRO, .color = s.textMuted);
            rcText(rcFormat(mem, "%d simulated \xc2\xb7 click a column to sort",
                              st->host.procCount),
                    .font = F_MICRO, .color = s.textMuted);
        }
        table_header(st);
        rcBox(.w = "grow", .h = "grow") {
            if (rcBeginTable("proc_table", cols, SORT_COUNT,
                              (RC_TableOptions){ .cellPadding = RC_VAL(CELL_PAD) })) {
                /* 128 rows is small enough to declare in full, and virtualizing
                   it anyway is the point: the cost of a row is paid at DECLARE
                   time, not at draw time, so a table that grows to a real host's
                   thousands does not change shape here.

                PITCH IS NOT CELL HEIGHT, and the difference is easy to get
                   wrong in a way that compiles. rcVirtualList sizes its spacers
                   from the number you pass, so it must be the distance from one
                   row's top to the next: the cell height PLUS the table's cell
                   padding, top and bottom. Pass the cell height alone and the
                   content is under-reported by 2 x padding per row - a third of
                   the list at these sizes - and the scrollbar stops agreeing
                   with the rows.
                   Setting the padding explicitly is what makes ROW_PITCH
                   derivable at all - inherit the default and the pitch depends
                   on a number the call site never names. RC_VAL is what says
                   "this exact value": a zero-init RC_TableOptions still gets the
                   house 6 px, and RC_VAL(0) is genuinely flush. */
                rcVirtualList(row, "proc_table", st->host.procCount,
                               (float)ROW_PITCH) {
                    const SysProc *p = &st->host.proc[st->order[row.index]];
                    RC_Color rowBg = (row.index & 1) ? rcAlpha(s.border, 30)
                                                     : s.surface;

                    rcTableRow();
                    /* Every cell is pinned to ROW_H. rcVirtualList sizes its
                       spacers from that number, so a row that is not really
                       that tall skews the scroll position. */
                    rcBox(.w = "grow", .hType = RC_PX(ROW_H), .px = 8,
                           .align = "cl", .bg = rowBg) {
                        rcTextC(p->name, .font = F_SMALL, .color = s.text);
                    }
                    rcTableNext();
                    rcBox(.w = "grow", .hType = RC_PX(ROW_H), .px = 8,
                           .align = "cr", .bg = rowBg) {
                        rcText(rcFormat(mem, "%d", p->pid), .font = F_SMALL,
                                .color = s.textMuted);
                    }
                    rcTableNext();
                    rcBox(.w = "grow", .hType = RC_PX(ROW_H), .px = 8,
                           .align = "cr", .bg = rowBg) {
                        rcText(rcFormat(mem, "%.1f", (double)p->cpu),
                                .font = F_SMALL, .color = load_color(p->cpu));
                    }
                    rcTableNext();
                    rcBox(.w = "grow", .hType = RC_PX(ROW_H), .px = 8,
                           .align = "cr", .bg = rowBg) {
                        rcText(rcFormat(mem, "%.0f MiB", (double)p->memMiB),
                                .font = F_SMALL, .color = s.textMuted);
                    }
                    rcTableNext();
                    rcBox(.w = "grow", .hType = RC_PX(ROW_H), .px = 8,
                           .align = "cl", .bg = rowBg) {
                        rcTextC(state_label(p->state), .font = F_MICRO,
                                 .color = p->state == SYS_RUNNING ? s.successHover
                                        : p->state == SYS_STOPPED ? s.danger
                                                                  : s.textMuted);
                    }
                }
                rcEndTable();
            }
            rcScrollbar("proc_table");
        }
    }
}

/* The declared:drawn ratio is the first thing to look at when a frame feels
   expensive, and rcAppFrameCounts needs no build knob to answer - unlike
   rcAppPerfFrame, it is always compiled in. A monitor is the right place to
   show it: this row is the app reporting on itself.

   WHY READING IT HERE IS LEGITIMATE, since the counts are snapshotted where the
   render walk begins and this runs inside layoutCallback. What we print is the
   PREVIOUS drawn frame, by design - correct numbers for an earlier frame rather
   than torn numbers for this one. For a once-a-second monitor that is
   invisible. Read it in RC_AppOptions.frameEndCallback instead when you want
   this frame's own figures, which is what a profiler or a perf gate wants.

   Both fields are 0 until the first frame has been drawn, so the very first
   status bar honestly reads "0 -> 0" rather than inventing a measurement.

   The row is counting itself, one frame late. That is the correct behaviour for
   a self-observing app and not a rounding error: an overlay you add costs
   elements too, and this number is the one that says how many. */
static void status_bar(RC_App *app, AppState *st)
{
    RC_Arena *mem = rcAppArena(app);
    RC_Style s = rcGetStyle();
    RC_FrameCounts fc = rcAppFrameCounts(app);

    rcRow(.w = "grow", .h = "26px", .px = 4, .gap = 14, .align = "cl") {
        rcText(rcFormat(mem, "sample %d", st->samples), .font = F_MICRO,
                .color = s.textMuted);
        rcText(rcFormat(mem, "%.0f fps while awake", (double)rcAppFPS(app)),
                .font = F_MICRO, .color = s.textMuted);
        /* The process table is virtualized, so this stays flat as the row count
           grows - which is the whole reason the ratio is worth watching. */
        /* ASCII "->" on purpose: the bundled face is Latin-1, so U+2192 would
           draw as the missing-glyph box. U+00B7 elsewhere in this file is fine. */
        rcText(rcFormat(mem, "%u declared -> %u drawn",
                        (unsigned)fc.declared, (unsigned)fc.drawCommands),
                .font = F_MICRO, .color = s.textMuted);
        rcTextL("parks between samples \xc2\xb7 real: this process \xc2\xb7 "
                 "simulated: host (sysmon_host.h)",
                 .font = F_MICRO, .color = s.textMuted);
        rcBox(.w = "grow") {}
        fold_hint(st);
    }
}

/* ===========================================================================
   Frame
   =========================================================================== */

static void layout(RC_App *app, void *userData)
{
    AppState *st = userData;
    RC_Style s = rcGetStyle();

    rcColumn(.id = "root", .w = "grow", .h = "grow", .bg = s.background) {
        titlebar(app, st);
        rcColumn(.w = "grow", .h = "grow", .p = 14, .gap = 12) {
            cards(app, st);
            rcRow(.w = "grow", .h = "232px", .gap = 12) {
                load_chart(app, st);
                core_strip(app, st);
            }
            process_table(app, st);
            status_bar(app, st);
        }
    }
}

int main(void)
{
    static AppState state;
    float fontSizes[F_COUNT];
    int i;

    fontSizes[F_MICRO] = 11.0f;
    fontSizes[F_SMALL] = 13.0f;
    fontSizes[F_BODY]  = 15.0f;
    fontSizes[F_STAT]  = 26.0f;

    rcSetStyle(rcStyleDark());

    sysmon_host_init(&state.host, 0x53595300u);
    for (i = 0; i < SYS_PROCS; i++)
        state.order[i] = i;
    state.sortKey  = SORT_CPU;
    state.sortDesc = true;
    state.barOpen  = true;
    state.barH     = (float)BAR_OPEN_H;

    RC_AppOptions opts = {
        .width  = 1240,
        .height = 820,
        .title  = "RayClay System Monitor",
        .clearColor = rcGetStyle().background,
        .fontSizes = fontSizes,
        .fontCount = F_COUNT,
        /* Backs every rcFormat in a frame: ~20 virtualized rows x 4 formatted
           cells, plus the cards, the core strip and the status bar. */
        .scratchArenaBytes = 16384,
        .nativeFrame    = true,
        .titlebarHeight = BAR_OPEN_H,
        .titlebar       = { .custom = true },  /* the band above IS the titlebar */
        .updateCallback  = update,
        .layoutCallback  = layout,
        .userData  = &state,
        /* The default, spelled out because it is the whole point of this app:
           it parks between samples and update() asks for the next wake. */
        .renderMode = RC_RENDER_ON_DEMAND,
    };

    return rcRunApp(&opts);
}
