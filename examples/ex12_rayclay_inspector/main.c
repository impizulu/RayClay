/*
================================================================================
    ex12_rayclay_inspector - a RayClay app for hand-testing RayClay itself
================================================================================

    A CONTROL COLUMN on the left with a live DIAGNOSTICS SIDEBAR on the right:
    a resource panel on top and an interleaved [APP]/[RAY] log below it. As you
    interact, it answers at a glance: did that click register? did that resize
    happen, and to what? did anything error?

    This is a tool, not a showcase. Every control on the left changes what the
    app shows or produces - the checkbox changes the log's COLUMN SET, the radios
    and the combo FILTER the log (and the header says how many lines they hid),
    the slider narrows the span the graphs plot, and the two dialogs export to
    the clipboard and inspect live library state. A widget that only logs that it
    was clicked belongs in the widgets gallery (ex10). In a diagnostic app an
    inert control is worse than no control, because it invites you to trust it.

    It dogfoods the library features a smooth RayClay should provide for a test
    harness - built on FIRST-CLASS calls, not shims (the owner's directive:
    "you are the developer; demand a smooth experience"). The whole diagnostics
    sidebar stands on native calls:

        rcSetLogSink        RayClay's own diagnostics into the [RAY] log
                             (what a SINK buys is the diagnostics landing in the
                              app's OWN UI, on every platform - NOT visibility:
                              stderr is not gone on the web, Emscripten routes it
                              to console.error. See docs/web-build.md.)
        rcClipboardSet       export the filtered log, or both metric histories as
                             CSV, from the modal EXPORT dialog
        RC_SplitPane         the controls | diagnostics divider you DRAG to resize
        RC_Table             the [APP]/[RAY] log as a scrolling grid: a sticky
                             header, body scroll by the table's own id, and
                             rcScrollToBottom to follow the newest line
        rcChart             the fps / frame-time trend - two series, dual y axes
        rcSparkline         the live CPU% and resident-memory strips
        rcProcessCpuPercent / rcProcessMemoryBytes - this process's OS CPU + RAM

    The PERFORMANCE panel also reads rcAppFPS / rcAppFrameTime and the runner's
    scratch-arena occupancy as numbers plus rcProgress bars. History is sampled
    on two coarse cadences - render metrics ~10 Hz, OS metrics ~1 Hz (the CPU
    reading accounts time since its previous call, so it must stay coarse) - and
    kept chronologically, so it feeds the chart and sparklines with no copy.

    Single-source (like ex10): the same .c compiles to desktop AND web with no
    per-target fork, and is pure RC_ (no layout-engine call, no libc include). Honours
    RAYCLAY_MAX_FRAMES for headless CI smoke - the log sink writes to an in-memory
    ring, so nothing depends on stderr.  Build target: rayclay_ex12_rayclay_inspector.

    Because that ring swallows the runner's "rendered N of N budgeted frames",
    this is the one example whose render a stdout-grepping smoke test cannot
    witness. It therefore witnesses itself: frameEndCallback counts PRESENTED
    frames and main returns 3 if that count is zero, so exit 0 from this binary
    means it drew.  See the block at the end of main().
================================================================================
*/

#include "rayclay.h"
#include "inspector_log.h"

/* Font slots, in load order into RC_AppOptions.fontSizes; the index is the
   .font value in RC_TextOptions. Baked from the bundled face (zero-asset). */
typedef enum {
    F_SMALL = 0,   /* log lines, muted captions */
    F_BODY,        /* labels, values            */
    F_HEAD,        /* sidebar + section headings */
    F_TITLE,       /* the top strip title        */
    F_COUNT
} InspFont;

/* Rolling history depth for the resource graphs (samples per series). At the
   render cadence (~10 Hz) that spans ~12 s; at the OS cadence (~1 Hz), ~2 min. */
#define INSP_HIST 120

/* Clipboard export scratch. 32 KB covers a typical 256-line log ring; a ring
   full of maximal bodies would exceed it, and then the export is truncated and
   SAYS SO rather than silently handing over a prefix. Lives in the static AppState, so the size
   costs BSS rather than stack. */
#define INSP_EXPORT_CAP 32768

typedef struct {
    long  frame;              /* advanced once per update; proves the loop is live */

    /* --- inspector controls (developer-owned, driven by pointer) ---
       EVERY control here changes what the tool SHOWS or PRODUCES. A widget that
       only logs that it was clicked demonstrates the widget, not the tool, and
       belongs in the gallery (ex10) - in a diagnostic app a dead control is
       worse than no control, because it invites you to trust it. */
    int   clicks;            /* click-registered probe: "did that click land?"   */
    bool  darkMode;          /* theme toggle -> rcSetStyle AND rcAppSetClearColor */
    bool  barShown;          /* the titlebar fold; see layout()                  */
    bool  showDetail;        /* log table: add the SEQ and LEVEL columns         */
    int   levelFilter;       /* log: 0 = all, 1 = warnings and errors, 2 = errors */
    int   srcFilter;         /* log: 0 = both sources, 1 = [APP], 2 = [RAY]      */
    float histFrac;          /* [0,1] share of the history the graphs plot       */
    bool  exportOpen;        /* the EXPORT modal                                 */
    bool  inspectOpen;       /* the non-modal live INSPECT panel                 */
    bool  inspectSticky;     /* -> .noBackdropDismiss (keeps it open)            */
    int   exportKind;        /* 0 = the filtered log, 1 = the metrics as CSV     */
    int   exportCopies;      /* successful rcClipboardSet calls this run         */
    bool  exportTrunc;       /* the buffer filled before the data ran out        */
    int   exportLen;         /* bytes live in exportBuf                          */
    char  exportBuf[INSP_EXPORT_CAP];

    int   trig_badfont;      /* >0: draw one out-of-range-font element, then decrement */

    InspectorLog log;        /* rcSetLogSink target + the app's own [APP] notes */

    /* --- render-metric history: ~10 Hz, ~12 s window. Stored chronologically
       (oldest first) so rcChart plots it without a ring-to-linear copy. --- */
    float  perf_fps[INSP_HIST];
    float  perf_ms[INSP_HIST];      /* frame time, milliseconds        */
    int    perf_len;                /* live samples (<= INSP_HIST)     */
    double perf_accum_ms;           /* cadence accumulator             */

    /* --- OS-metric history: ~1 Hz, ~2 min window. rcProcessCpuPercent accounts
       CPU since its previous call, so it is read on this coarse cadence only. --- */
    float  sys_cpu[INSP_HIST];      /* CPU %, clamped >= 0 for the plot */
    float  sys_mem[INSP_HIST];      /* resident memory, MB             */
    int    sys_len;
    double sys_accum_ms;
    float  cur_cpu;                 /* last CPU% reading (< 0 => unavailable, e.g. web)  */
    float  cur_mem_mb;              /* last resident MB   (< 0 => platform offers none)  */

    /* --- scheduler history, sampled once per FRAME.
       The counters are cumulative, so we store the delta: the raw value only
       ramps, which hides the thing you are looking for - a wake loop is a rate
       that spikes, not a total that grows.
       Per frame, not per second, and that is deliberate. A per-second rate needs
       a wall clock and RayClay's public API exposes none: rcAppFrameTime is
       EXPONENTIALLY SMOOTHED, so accumulating it to build a cadence runs short -
       badly so on demand, where real one-second gaps report as ~100 ms. Labelling
       a per-frame delta "per second" would be inventing a number, so the row says
       what it is and the headline metric below (spurious/waits) is a RATIO, which
       needs no clock at all. --- */
    float    sched_spur[INSP_HIST]; /* spurious wakes since last frame */
    float    sched_adm[INSP_HIST];  /* frames admitted since last frame */
    int      sched_len;
    uint64_t sched_prev_spur;       /* previous sample, for differencing */
    uint64_t sched_prev_adm;
    uint64_t sched_prev_waits;
    int      sched_dry;             /* consecutive samples with NO new park */
    RC_SchedStats sched_cur;        /* last snapshot; the rows read this */
    bool     continuous;            /* the render-mode toggle, see sched_panel */

    float cur_arena_occ;            /* scratch-arena occupancy, read end-of-layout */

    int   last_win_w, last_win_h;   /* resize detection for the [APP] log */
    long  last_total;               /* log.total at last follow; drives rcScrollToBottom */
    float split_frac;               /* pane-1 (gallery) share of the RC_SplitPane; drag-updated */

    /* Frames this process has actually PRESENTED, counted in frameEndCallback -
       the render witness, see frame_end and main. Not a duplicate of the
       PERFORMANCE panel's rcAppFPS: that is a rate over a window, this is a
       lifetime total, and only the total can distinguish "slow" from "never". */
    unsigned long frames_drawn;
} AppState;

/* ---------------------------------------------------------------------------
   rcSetLogSink target: RayClay's own diagnostics arrive here. `msg` is the bare
   single-line body (no prefix, no newline) and is only valid for the call, so we
   COPY it into the ring at once. It can fire mid-frame from inside a RayClay
   call, so it does one cheap thing and never re-enters RayClay.
   --------------------------------------------------------------------------- */
static void on_log(RC_LogLevel level, const char *msg, void *user)
{
    AppState *st = (AppState *)user;
    insp_log_push(&st->log, INSP_SRC_RAY, (int)level, msg);
}

/* ---------------------------------------------------------------------------
   RC_AppOptions.frameEndCallback: the runner calls this LAST on a frame it
   actually PRESENTED, and never for an idle frame it skipped. That contract is
   what makes a count taken here a render witness rather than a liveness one -
   see main() for why this example needs its own.
   --------------------------------------------------------------------------- */
static void frame_end(RC_App *app, void *userData)
{
    AppState *st = (AppState *)userData;
    (void)app;
    st->frames_drawn++;
}

/* --- small helpers --------------------------------------------------------- */

/* Append one [APP] line. Callers hand an RC_String: rcFormat (arena) when the
   line carries values, rcStringFromCStr when it is a fixed sentence - that one
   wraps the literal without copying, so it costs no arena at all. We copy the
   bytes here either way, so the arena's per-frame lifetime is never a problem. */
static void applog(AppState *st, RC_String s)
{
    insp_log_push_len(&st->log, INSP_SRC_APP, RC_LOG_INFO, s.chars, s.length);
}

static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* How many of the NEWEST samples the graphs plot, from the "Graph window"
   slider. A chart needs two points to be a line, so the window never narrows
   below that unless the history itself is shorter. */
static int hist_window(const AppState *st, int len)
{
    int n = (int)((float)len * st->histFrac + 0.5f);
    if (len < 2)
        return len;
    return n < 2 ? 2 : n;
}

/* Warning amber - the chart's frame-time series and warning-level log lines. */
static const RC_Color INSP_AMBER = { 245, 158, 11, 255 };

static void section_heading(const char *title)
{
    RC_Style s = rcGetStyle();
    rcTextC(title, .font = F_HEAD, .color = s.text);
}

/* --- log filtering ---------------------------------------------------------
   ONE predicate, used by the table, the counter and the export. Three copies of
   this rule would be three chances for the exported file to disagree with the
   panel it was exported from, which is the one thing a diagnostic tool may not
   do. --------------------------------------------------------------------- */
static bool log_visible(const AppState *st, const InspLogLine *l)
{
    if (st->levelFilter == 1 && l->level != RC_LOG_WARNING && l->level != RC_LOG_ERROR)
        return false;
    if (st->levelFilter == 2 && l->level != RC_LOG_ERROR)
        return false;
    if (st->srcFilter == 1 && l->source != INSP_SRC_APP)
        return false;
    if (st->srcFilter == 2 && l->source != INSP_SRC_RAY)
        return false;
    return true;
}

static int log_visible_count(const AppState *st)
{
    int count = insp_log_count(&st->log), n = 0;
    for (int i = 0; i < count; i++) {
        if (log_visible(st, insp_log_at(&st->log, i)))
            n++;
    }
    return n;
}

static const char *level_name(int level)
{
    return level == RC_LOG_ERROR   ? "ERROR"
         : level == RC_LOG_WARNING ? "WARN"
         :                           "info";
}

/* --- clipboard export ------------------------------------------------------
   Built into a fixed buffer with a bounded appender rather than by concatenating
   arena strings: the arena is a PER-FRAME allocator, and an export is assembled
   across hundreds of rcFormat calls in one frame. Appending by (chars, length)
   also means nothing here depends on rcFormat's result being null-terminated -
   RC_String is length-counted, and that is the half of the contract that is
   actually promised.
   --------------------------------------------------------------------------- */

/* Append `s`, stopping at capacity. Returns false once full, so the caller can
   stop early and report a truncation instead of shipping a silent prefix. */
static bool exp_add(AppState *st, RC_String s)
{
    int i = 0;
    while (i < s.length && st->exportLen < INSP_EXPORT_CAP - 1)
        st->exportBuf[st->exportLen++] = s.chars[i++];
    st->exportBuf[st->exportLen] = '\0';
    return i == s.length;
}

static bool exp_lit(AppState *st, const char *literal)
{
    return exp_add(st, rcStringFromCStr(literal));
}

/* Render the CURRENTLY VISIBLE log to exportBuf, in the panel's own order. */
static void export_build_log(RC_App *app, AppState *st)
{
    RC_Arena *mem = rcAppArena(app);
    int count = insp_log_count(&st->log);
    bool ok;

    st->exportLen = 0;
    st->exportBuf[0] = '\0';
    /* SPACE-ALIGNED, not tab-separated. A log export is pasted into a bug
       report or a chat window, so it has to read as text - and RayClay draws no
       glyph and no advance for '\t', so a tabbed line renders as one run-on
       string in the preview below (and in any RayClay text). The metrics export
       is the machine-readable one; that is what its CSV is for. */
    ok = exp_lit(st, "# RayClay Inspector - log export\n"
                     "# seq  src  level  message\n");
    for (int i = 0; i < count && ok; i++) {
        const InspLogLine *l = insp_log_at(&st->log, i);
        if (!log_visible(st, l))
            continue;
        ok = exp_add(st, rcFormat(mem, "%-5ld %-4s %-5s  %s\n", l->seq,
                                   l->source == INSP_SRC_RAY ? "RAY" : "APP",
                                   level_name(l->level), l->msg));
    }
    st->exportTrunc = !ok;
}

/* Render both metric histories to exportBuf as CSV. The two series are sampled
   on DIFFERENT cadences (~10 Hz render, ~1 Hz OS), so they are emitted as two
   tables rather than joined into one - a shared row index would imply a
   correspondence between them that does not exist. */
static void export_build_metrics(RC_App *app, AppState *st)
{
    RC_Arena *mem = rcAppArena(app);
    bool ok;

    st->exportLen = 0;
    st->exportBuf[0] = '\0';
    ok = exp_lit(st, "# RayClay Inspector - metrics export\n"
                     "# render metrics, sampled ~10 Hz\n"
                     "sample,fps,frame_ms\n");
    for (int i = 0; i < st->perf_len && ok; i++)
        ok = exp_add(st, rcFormat(mem, "%d,%.1f,%.3f\n", i,
                                   st->perf_fps[i], st->perf_ms[i]));
    if (ok)
        ok = exp_lit(st, "\n# OS metrics, sampled ~1 Hz\n"
                         "sample,cpu_percent,resident_mb\n");
    for (int i = 0; i < st->sys_len && ok; i++)
        ok = exp_add(st, rcFormat(mem, "%d,%.1f,%.2f\n", i,
                                   st->sys_cpu[i], st->sys_mem[i]));
    st->exportTrunc = !ok;
}

static void export_build(RC_App *app, AppState *st)
{
    if (st->exportKind == 0)
        export_build_log(app, st);
    else
        export_build_metrics(app, st);
}

/* --- the live INSPECT panel ------------------------------------------------ */

/* One key/value row. Fixed label column so the values line up into a readable
   second column rather than ragging with the label text. */
static void inspect_row(const char *label, RC_String value)
{
    RC_Style s = rcGetStyle();
    rcRow(.w = "grow", .gap = 10, .align = "cl") {
        rcBox(.w = "148px") { rcTextC(label, .font = F_SMALL, .color = s.textMuted); }
        rcBox(.w = "grow", .align = "cl") { rcText(value, .font = F_SMALL, .color = s.text); }
    }
}

/* Everything the app can ask RayClay about its own surface, re-read from public
   getters EVERY FRAME. This is the panel the "Live inspect" button owes you:
   it is non-modal precisely so you can resize, zoom and drag the split while
   watching these numbers answer. */
static void inspect_panel(RC_App *app, AppState *st)
{
    RC_Style      s    = rcGetStyle();
    RC_Arena     *mem  = rcAppArena(app);
    RC_Dimensions d    = rcGetWindowDimensions();
    RC_ZoomMode   zm   = rcAppZoomMode(app);

    rcColumn(.w = "480px", .gap = 6) {
        rcTextL("LIVE STATE", .font = F_HEAD, .color = s.text);
        /* Logical pixels, NOT physical: this is the space the layout is solved
           in, so it is the number that explains what you are looking at. */
        inspect_row("window (logical)",
                    rcFormat(mem, "%.0f x %.0f px", d.width, d.height));
        inspect_row("zoom",
                    rcFormat(mem, "%.0f%%  -  %s", rcAppZoom(app) * 100.0f,
                              zm == RC_ZOOM_OPTICAL ? "optical magnify" : "layout reflow"));
        inspect_row("frame",
                    rcFormat(mem, "%.0f fps, %.2f ms",
                              rcAppFPS(app), rcAppFrameTime(app) * 1000.0f));
        inspect_row("scratch arena",
                    rcFormat(mem, "%d%% of %d bytes",
                              (int)(st->cur_arena_occ * 100.0f + 0.5f),
                              (int)mem->bufferLength));
        inspect_row("log ring",
                    rcFormat(mem, "%ld pushed, %d held, %d shown",
                              st->log.total, insp_log_count(&st->log),
                              log_visible_count(st)));
        inspect_row("split pane",
                    rcFormat(mem, "%.0f%% controls / %.0f%% diagnostics",
                              st->split_frac * 100.0f, (1.0f - st->split_frac) * 100.0f));
        /* Reads false in a stock build: RC_DEBUG_TOOLS defaults to 0, so the
           overlay is opt-in and debugToggleKey warns once and does nothing. */
        inspect_row("debug tools",
                    rcStringFromCStr(rcAppIsDebugEnabled(app)
                                     ? "enabled (-DRC_DEBUG_TOOLS=1)"
                                     : "off (the default; opt in at build time)"));
        inspect_row("clicks registered", rcFormat(mem, "%d", st->clicks));
        rcTextL("Resize the window, zoom with the primary modifier and +/-/0, or "
                "drag the split - every row re-reads the library each frame.",
                .font = F_SMALL, .color = s.textMuted);
    }
}

/* --- inspector controls (left) --------------------------------------------- */

/* Interactive controls, each wired to an [APP] log line on a DISCRETE change (a
   click, a toggle, a selection) - never per-frame, so dragging the slider does
   not flood the log. */
static void controls(RC_App *app, AppState *st)
{
    RC_Style s = rcGetStyle();

    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("BUTTONS");
        rcRow(.gap = 10, .align = "cl") {
            if (rcButton("btn_primary", "Primary", RC_BTN_PRIMARY)) {
                st->clicks++;
                applog(st, rcFormat(rcAppArena(app), "click ok: btn 'Primary' (clicks=%d)", st->clicks));
            }
            if (rcButton("btn_default", "Default", RC_BTN_DEFAULT)) {
                st->clicks++;
                applog(st, rcFormat(rcAppArena(app), "click ok: btn 'Default' (clicks=%d)", st->clicks));
            }
            if (rcButton("btn_reset", "Reset", RC_BTN_DANGER)) {
                st->clicks = 0;
                applog(st, rcStringFromCStr("click ok: btn 'Reset' (clicks=0)"));
            }
        }
    }

    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("VIEW");
        rcRow(.gap = 16, .align = "cl") {
            if (rcCheckbox("cb_detail", "Seq + level columns", &st->showDetail))
                applog(st, rcFormat(rcAppArena(app), "log columns = %s",
                                     st->showDetail ? "seq/level/src/message" : "src/message"));
            rcTextL("Theme", .font = F_SMALL, .color = s.textMuted);
            if (rcToggle("tg_dark", &st->darkMode))
                applog(st, rcFormat(rcAppArena(app), "theme = %s",
                                     st->darkMode ? "dark" : "light"));
            rcTextC(st->darkMode ? "Dark" : "Light", .font = F_SMALL, .color = s.textMuted);
        }

        /* The graph window as a FRACTION, not a sample count: the history fills
           over the first ~12 s, and a fixed count would mean something different
           at second 3 than at second 30. */
        rcRow(.gap = 12, .align = "cl") {
            rcTextL("Graph window", .font = F_SMALL, .color = s.textMuted);
            rcBox(.w = "180px") { rcSlider("sl_hist", &st->histFrac, 0.1f, 1.0f); }
            RC_String win = rcFormat(rcAppArena(app), "newest %d of %d samples",
                                        hist_window(st, st->perf_len), st->perf_len);
            rcText(win, .font = F_SMALL, .color = s.textMuted);
        }
    }

    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("LOG FILTERS");
        rcRow(.gap = 16, .align = "cl") {
            rcTextL("Level", .font = F_SMALL, .color = s.textMuted);
            if (rcRadio("rb_all", "All", &st->levelFilter, 0))
                applog(st, rcStringFromCStr("log level filter = all"));
            if (rcRadio("rb_warn", "Warnings+", &st->levelFilter, 1))
                applog(st, rcStringFromCStr("log level filter = warnings and errors"));
            if (rcRadio("rb_err", "Errors", &st->levelFilter, 2))
                applog(st, rcStringFromCStr("log level filter = errors only"));
        }

        rcRow(.gap = 12, .align = "cl") {
            rcTextL("Source", .font = F_SMALL, .color = s.textMuted);
            static const char *const sources[] = { "APP + RAY", "APP only", "RAY only" };
            rcBox(.w = "170px") {
                if (rcCombo("cb_source", &st->srcFilter, sources, 3))
                    applog(st, rcFormat(rcAppArena(app), "log source filter = %s",
                                         sources[st->srcFilter]));
            }
            /* The filter's effect, as a number. A filter you cannot see the
               result of is indistinguishable from one that is not wired up -
               which is exactly what this app was accused of, correctly. */
            RC_String shown = rcFormat(rcAppArena(app), "%d of %d lines shown",
                                        log_visible_count(st), insp_log_count(&st->log));
            rcText(shown, .font = F_SMALL, .color = s.textMuted);
        }
    }

    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("EXPORT  /  INSPECT");
        rcRow(.gap = 10, .align = "cl") {
            if (rcButton("btn_export", "Export...", RC_BTN_DEFAULT)) {
                st->exportOpen = true;
                export_build(app, st);
                applog(st, rcStringFromCStr("export dialog opened"));
            }
            if (rcButton("btn_inspect", "Live inspect", RC_BTN_DEFAULT)) {
                st->inspectOpen = true;
                applog(st, rcStringFromCStr("live inspect panel opened"));
            }
            RC_String copied = rcFormat(rcAppArena(app), "clipboard copies: %d",
                                         st->exportCopies);
            rcText(copied, .font = F_SMALL, .color = s.textMuted);
        }

        /* MODAL, deliberately: an export is a snapshot, and the app behind it is
           still generating log lines. Freezing interaction while you choose what
           to copy is what makes the byte count on the button true when you press
           it. The content is rebuilt on open and on every choice below. */
        if (rcBeginModal("dlg_export", &st->exportOpen)) {
            rcTextL("Export to clipboard", .font = F_HEAD, .color = s.text);
            rcRow(.gap = 16, .align = "cl") {
                if (rcRadio("ex_log", "Filtered log", &st->exportKind, 0))
                    export_build(app, st);
                if (rcRadio("ex_csv", "Metrics (CSV)", &st->exportKind, 1))
                    export_build(app, st);
            }
            RC_String size = rcFormat(rcAppArena(app), "%d bytes%s",
                                       st->exportLen,
                                       st->exportTrunc ? "  (TRUNCATED - buffer full)" : "");
            rcText(size, .font = F_SMALL,
                    .color = st->exportTrunc ? s.danger : s.textMuted);

            /* A preview, because a clipboard copy is otherwise invisible: you
               cannot tell an empty export from a working one until you paste it
               somewhere else. Scrolls, so a long export is inspectable here. */
            rcColumn(.id = "ExportPreview", .w = "560px", .h = "220px", .scroll = "v",
                      .bg = s.surfaceAlt, .p = 10, .borderRadius = "all-md") {
                rcTextC(st->exportBuf, .font = F_SMALL, .color = s.text);
            }
            /* Inside the modal scope on purpose: a scrollbar is a floating
               element, so declaring it out with the others would layer it
               against the root rather than above the dialog. */
            rcScrollbar("ExportPreview");

            rcRow(.gap = 10, .align = "cl") {
                if (rcButton("ex_copy", "Copy", RC_BTN_PRIMARY)) {
                    /* exportBuf is null-terminated by exp_add, so the C-string
                       clipboard call is safe by construction rather than by
                       assumption about someone else's allocator. */
                    rcClipboardSet(st->exportBuf);
                    st->exportCopies++;
                    applog(st, rcFormat(rcAppArena(app), "copied %d bytes of %s to the clipboard",
                                         st->exportLen,
                                         st->exportKind == 0 ? "log" : "metrics CSV"));
                    st->exportOpen = false;
                }
                if (rcButton("ex_cancel", "Cancel", RC_BTN_DEFAULT))
                    st->exportOpen = false;
            }
            rcEndModal();
        }

        /* NON-MODAL, equally deliberately: the whole point is to watch these
           numbers move WHILE you resize, zoom and click the app behind. The PAIR
           (RC_MODALITY_NON_MODAL + noBackdropDismiss) is what keeps it open while
           the app stays live - see ex10 for the full trap. */
        RC_ModalOptions opts = { .modality = RC_MODALITY_NON_MODAL,
                                 .noBackdropDismiss = st->inspectSticky };
        if (rcBeginModalEx("insp_live", &st->inspectOpen, opts)) {
            inspect_panel(app, st);
            rcRow(.gap = 10, .align = "cl") {
                rcCheckbox("pp_sticky", "Stay open when I click away", &st->inspectSticky);
                if (rcButton("pp_close", "Close", RC_BTN_DEFAULT))
                    st->inspectOpen = false;
            }
            rcEndModal();
        }
    }

    /* (A "SCROLL REGION" panel of sixteen dummy rows lived here. It demonstrated
       scrolling and nothing else - the log table and the export preview are both
       real scroll containers doing real work - so it was removed rather than
       kept as furniture. Widget-for-its-own-sake belongs in ex10.) */

    /* Error triggers: buttons that provoke a REAL RayClay diagnostic so the
       [RAY] stream reddens on demand - what makes this a test harness. The
       contract is behavioural ("this reddens the log"); each trigger is finalised
       by RUNNING, not assumed, so only verified ones ship. */
    rcColumn(.w = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .borderRadius = "all-xl") {
        section_heading("ERROR TRIGGERS");
        rcRow(.gap = 10, .align = "cl") {
            if (rcButton("trig_font", "Invalid font", RC_BTN_DANGER)) {
                st->trig_badfont = 1;   /* draw one out-of-range-font element below */
                applog(st, rcStringFromCStr("trigger: invalid font slot (expect [RAY] error)"));
            }
            if (rcButton("trig_clear", "Clear log", RC_BTN_GHOST)) {
                st->log.total = 0;      /* reset the ring; nothing to free */
            }
        }
        /* The probe: an out-of-range font slot (F_COUNT..) makes RayClay log an
           RC_LOG_ERROR and fall back to the default font. Drawn for a single
           frame after the click, in danger colour, so the cause is visible. */
        if (st->trig_badfont > 0) {
            rcTextC("bad-font probe (invalid slot 240)", .font = 240, .color = s.danger);
            st->trig_badfont--;
        }
    }
}

/* --- diagnostics sidebar (right) ------------------------------------------- */

/* One resource row: label, a first-class rcProgress bar, and the numeric value
   on the right. `frac` drives the bar (clamped); `id` is the bar's element id. */
static void resource_row(const char *id, const char *label, RC_String value, float frac)
{
    RC_Style s = rcGetStyle();
    rcRow(.w = "grow", .align = "cl", .gap = 8) {
        rcBox(.w = "64px") { rcTextC(label, .font = F_SMALL, .color = s.textMuted); }
        rcBox(.w = "grow") { rcProgress(id, clamp01(frac)); }
        rcBox(.w = "96px", .align = "cr") { rcText(value, .font = F_SMALL, .color = s.text); }
    }
}

/* PERFORMANCE panel: FPS + frame-time (runner-smoothed) and the runner's own
   scratch-arena occupancy - all from library calls that exist today. Shows the
   current value and the worst value across the ~12s history window. */
static void resource_panel(RC_App *app, AppState *st)
{
    RC_Style s = rcGetStyle();

    float fps = rcAppFPS(app);
    float ms  = rcAppFrameTime(app) * 1000.0f;

    float fps_min = fps, ms_max = ms;
    for (int i = 0; i < st->perf_len; i++) {
        if (st->perf_fps[i] < fps_min) fps_min = st->perf_fps[i];
        if (st->perf_ms[i]  > ms_max)  ms_max  = st->perf_ms[i];
    }

    rcColumn(.id = "ResPanel", .w = "grow", .h = "30vh",
              .bg = s.surface, .p = 14, .gap = 10, .borderRadius = "all-lg") {
        rcRow(.w = "grow", .align = "cl") { section_heading("PERFORMANCE"); }
        /* FPS as a fraction of a 120 target so 60 sits mid-bar and headroom shows. */
        resource_row("res_fps", "FPS",
                     rcFormat(rcAppArena(app), "%.0f (min %.0f)", fps, fps_min),
                     fps / 120.0f);
        /* frame-time against a 33 ms (30 fps) ceiling: full bar == a slow frame. */
        resource_row("res_ms", "frame",
                     rcFormat(rcAppArena(app), "%.1f ms (max %.1f)", ms, ms_max),
                     ms / 33.0f);
        /* the runner's per-frame scratch arena; occupancy measured end-of-layout. */
        resource_row("res_arena", "scratch",
                     rcFormat(rcAppArena(app), "%d%%", (int)(st->cur_arena_occ * 100.0f + 0.5f)),
                     st->cur_arena_occ);

        /* The fps/frame-time trend as ONE chart with two series on SEPARATE y
           axes: fps reads high, frame-time reads low, so a shared axis would
           flatten one of them. rcChart GROWs to fill, hence the sized box; the
           .y and .label pointers are borrowed until rcRender, which the static
           history arrays and string literals satisfy. */
        /* The "Graph window" slider narrows the plot to the NEWEST n samples.
           Both series are plain arrays, so the window is a pointer offset and a
           count - no copy, and the chart never sees the hidden prefix. */
        int pn = hist_window(st, st->perf_len);
        int po = st->perf_len - pn;
        rcBox(.w = "grow", .h = "96px") {
            RC_Series ser[] = {
                { .y = st->perf_fps + po, .count = pn, .kind = RC_SERIES_LINE,
                  .color = s.primary,  .label = "fps" },
                { .y = st->perf_ms + po,  .count = pn, .kind = RC_SERIES_LINE,
                  .color = INSP_AMBER, .label = "ms",  .axis = 1 },
            };
            rcChart("perf_chart", ser, 2,
                     (RC_ChartOptions){ .legend = true, .x = { .hide = true },
                                        .y = { .grid = true },
                                        .tooltip = RC_CHART_TOOLTIP_NEAREST });
        }
    }
}

/* One SYSTEM row: label, the current value right-aligned, and a live rcSparkline
   of the history to its right - the "live resource strip" the sparkline is for.
   `hist`/`histLen` are borrowed until rcRender (the static rings satisfy that). */
static void sys_row(const char *sparkId, const char *label, RC_String value,
                    const float *hist, int histLen, RC_Color color)
{
    RC_Style s = rcGetStyle();
    rcRow(.w = "grow", .align = "cl", .gap = 8) {
        rcBox(.w = "40px") { rcTextC(label, .font = F_SMALL, .color = s.textMuted); }
        rcBox(.w = "88px", .align = "cr") { rcText(value, .font = F_SMALL, .color = s.text); }
        rcBox(.w = "grow", .h = "22px") {
            rcSparkline(sparkId, hist, histLen,
                         (RC_SparklineOptions){ .kind = RC_SERIES_AREA, .color = color });
        }
    }
}

/* SYSTEM panel: this process's OS CPU% and resident memory, sampled ~1 Hz. Both
   read "n/a" where the platform offers no view (CPU on web; memory if the OS
   reading is 0) - the panel never prints a sentinel like -1. */
static void system_panel(RC_App *app, AppState *st)
{
    RC_Style s = rcGetStyle();

    float cpu_peak = 0.0f;
    for (int i = 0; i < st->sys_len; i++) {
        if (st->sys_cpu[i] > cpu_peak) cpu_peak = st->sys_cpu[i];
    }

    RC_String cpu_val = st->cur_cpu < 0.0f
        ? rcStringFromCStr("n/a")
        : rcFormat(rcAppArena(app), "%.0f%% (pk %.0f)", st->cur_cpu, cpu_peak);
    RC_String mem_val = st->cur_mem_mb < 0.0f
        ? rcStringFromCStr("n/a")
        : rcFormat(rcAppArena(app), "%.1f MB", st->cur_mem_mb);

    rcColumn(.id = "SysPanel", .w = "grow", .h = "21vh",
              .bg = s.surface, .p = 14, .gap = 10, .borderRadius = "all-lg") {
        rcRow(.w = "grow", .align = "cl") { section_heading("SYSTEM"); }
        /* Same window as the chart, so the two panels always describe the same
           span of time - a sparkline showing two minutes beside a chart showing
           twelve seconds invites exactly the wrong comparison. */
        int sn = hist_window(st, st->sys_len);
        int so = st->sys_len - sn;
        sys_row("spk_cpu", "CPU", cpu_val, st->sys_cpu + so, sn, s.primary);
        sys_row("spk_mem", "RAM", mem_val, st->sys_mem + so, sn, INSP_AMBER);
        /* Anticipate the "this is broken" reaction. rcProcessCpuPercent is a
           share of ONE core, like top, so a figure in the hundreds is correct
           and routine: it counts the WHOLE process, and a software GL driver
           (any headless or CI run) rasterises on a pool of worker threads even
           though RayClay's own code is single-threaded. A reading of 1188% on a
           24-core box is what that looks like. */
        rcTextL("CPU is a share of ONE core, like top: over 100% means several "
                "cores. It counts the whole process - a software GL driver's "
                "worker threads included.",
                .font = F_SMALL, .color = s.textMuted);
    }
}

/* SCHEDULER panel: the frame-admission counters, plotted WHILE they move.
   Why this panel exists: `spurious` is the most diagnostic number the library
   produces - a climbing rate is the signature of a wake loop. rcAppSchedStats
   reports it live rather than only in a summary at exit, which makes it
   readable live, and this panel is what that buys.

   Warning: the panel is meaningless unless the app parks, and this app does not by
   default. Measured on ex23, 3 s idle: under RC_RENDER_CONTINUOUS the
   scheduler admitted 508 frames with 0 waits and 0 spurious - it never parks, so
   every counter here would sit at a permanent zero that means "not applicable",
   not "healthy". That is why the toggle below is part of the panel rather than a
   convenience: flip to ON DEMAND and the counters come alive. The contrast IS the
   lesson. */
static void sched_row(const char *label, RC_String value, RC_Color color)
{
    RC_Style s = rcGetStyle();
    rcRow(.w = "grow", .align = "cl", .gap = 8) {
        rcBox(.w = "104px") { rcTextC(label, .font = F_SMALL, .color = s.textMuted); }
        rcBox(.w = "grow", .align = "cl") { rcText(value, .font = F_SMALL, .color = color); }
    }
}

static void sched_panel(RC_App *app, AppState *st)
{
    RC_Style      s   = rcGetStyle();
    RC_Arena     *mem = rcAppArena(app);
    RC_SchedStats sc  = st->sched_cur;

    /* The denominator is `waits`, never `admitted`. spurious/admitted compares
       two different populations and reads alarmingly high on a healthy idle app:
       an app that parks a lot and draws rarely is doing exactly what it should. */
    float wasted = sc.waits ? 100.0f * (float)sc.spurious / (float)sc.waits : 0.0f;

    /* The mode is read from evidence, not from our own flag, because
       RAYCLAY_RENDER_MODE overrides the app's choice at startup and there is no
       public reader for the mode - a mirrored bool would confidently mislabel the
       very thing this panel exists to show.
       The evidence is the delta, never the total. See the sampler in update():
       `waits` is cumulative and nothing resets it, so `waits > 0` latches on the
       first park and then reports "parked" for the rest of the process. */
    bool parking = st->sched_dry < INSP_HIST;

    /* Collapsed when the app does not park, and that is not a space saving - it
       is the honest reading. Every counter below is about the park loop, so in
       continuous mode they are all a structural zero meaning "not applicable".
       Showing six zero rows would invite exactly the misreading the panel exists to
       prevent, and it would cost the LOG panel below a third of the sidebar. */
    /* "fit", not a vh fraction. This panel's contents change with the mode, and
       a fixed height cannot hold both arms: the first version pinned 22vh and the
       rows drew straight through the ones below when the copy grew. The sidebar's
       LOG below takes "grow", so it absorbs whatever this leaves. */
    rcColumn(.id = "SchedPanel", .w = "grow", .h = "fit",
              .bg = s.surface, .p = 14, .gap = 8, .borderRadius = "all-lg") {
        rcRow(.w = "grow", .align = "cl", .gap = 10) {
            section_heading("SCHEDULER");
            /* Short on purpose - this sits beside a button in a narrow sidebar, and
               the longer wording wrapped into it. */
            rcTextC(parking ? "parking" : "not parking",
                    .font = F_SMALL, .color = parking ? s.primary : s.textMuted);
            rcBox(.w = "grow") {}
            if (rcButton("btn_mode", st->continuous ? "go on demand" : "go continuous",
                         RC_BTN_DEFAULT)) {
                st->continuous = !st->continuous;
                rcAppSetContinuousRendering(app, st->continuous);
                insp_log_push(&st->log, INSP_SRC_APP, RC_LOG_INFO,
                              st->continuous
                                  ? "render mode -> CONTINUOUS (the scheduler stops parking)"
                                  : "render mode -> ON DEMAND (the counters come alive)");
            }
        }

        if (!parking) {
            /* One honest line instead of six zeros. */
            rcTextL("Counters are about PARKS, so they stay 0 while the app draws "
                    "every frame. Switch to on demand to see them move.",
                    .font = F_SMALL, .color = s.textMuted);
        } else {
            int n = hist_window(st, st->sched_len);
            int o = st->sched_len - n;
            /* The label carries the unit, and it must, because there is no public
               wall clock: these are per-FRAME deltas, never per second. Spell it
               out - an earlier revision labelled them "spur +" and left "per frame"
               in a source comment no reader of the app ever sees. */
            sys_row("spk_spur", "spur/frame", rcFormat(mem, "%llu total", (unsigned long long)sc.spurious),
                    st->sched_spur + o, n, INSP_AMBER);
            sys_row("spk_adm", "adm/frame", rcFormat(mem, "%llu total", (unsigned long long)sc.admitted),
                    st->sched_adm + o, n, s.primary);

            /* The amber is deliberately suppressed in this app, and the number is
               NOT. This inspector arms rcAppRequestFrameAfter every frame so the
               panel stays live while the app is idle - which is the whole point of
               it - and each of those calls currently costs one EXTRA park that is
               charged as spurious. Measured here, on demand, 6 s: 12 waits and 6
               spurious against 7 admitted, a flat 50%, where an app that does not
               self-wake scores 0%. So the amber would fire permanently, on waste
               this app manufactures by measuring. A warning colour that can never
               go out is worse than none: it teaches the reader to ignore it.
               The figure stays honest - hiding it would be the worse lie. Filed
               to the library with the three-arm measurement. */
            sched_row("wasted parks",
                      rcFormat(mem, "%.0f%%  (%llu of %llu)", wasted,
                               (unsigned long long)sc.spurious,
                               (unsigned long long)sc.waits),
                      s.text);
            rcTextL("this app wakes itself 1x/sec to stay live; each self-wake "
                    "costs one extra park, so the % above is its own cost.",
                    .font = F_SMALL, .color = s.textMuted);
            sched_row("repaints", rcFormat(mem, "%llu drawn outside the admit path",
                                           (unsigned long long)sc.refreshRepaints), s.textMuted);

            /* The per-reason breakdown. rcFrameReasonName gives the label, so adding a
               reason to the enum extends this loop with no edit here. Bound with
               RC_FRAME_REASON_COUNT and stop BELOW it: it is the array length, not a
               reason, so `<=` walks off the end. */
            rcRow(.w = "grow", .gap = 10, .align = "cl") {
                for (int i = 0; i < RC_FRAME_REASON_COUNT; i++) {
                    if (!sc.byReason[i]) continue;
                    rcTextC(rcFormat(mem, "%s %llu", rcFrameReasonName((RC_FrameReason)i),
                                     (unsigned long long)sc.byReason[i]).chars,
                            .font = F_SMALL, .color = s.textMuted);
                }
            }
            /* Anticipate "expose is broken". It is not: nothing in the library raises
               it, so a zero here is the LIBRARY saying it cannot report, which prints
               identically to "this never happened" and is a different claim.
               Gated on the counter, not written as a constant truth. The sentence
               is only true while the reason has no producer, and the day one is wired
               this note RETIRES ITSELF instead of becoming a confident falsehood. A
               claim that cannot notice it has gone stale does not belong on screen. */
            if (!sc.byReason[RC_FRAME_EXPOSE]) {
                rcTextL("expose reads 0 because nothing raises it yet - that is "
                        "\"cannot report\", not \"did not happen\".",
                        .font = F_SMALL, .color = s.textMuted);
            }
        }
    }
}

/* The live log as an RC_Table: a sticky "SRC | MESSAGE" header, oldest-first
   terminal order, and the body scrolled by the table's own id ("LogScroll") so
   rcScrollbar and rcScrollToBottom (in layout()) drive it with no extra model.
   Cells are ours, so each carries its own level/source colour. */
static void log_panel(RC_App *app, AppState *st)
{
    RC_Style s = rcGetStyle();

    rcColumn(.id = "LogPanel", .w = "grow", .h = "grow",
              .bg = s.surface, .p = 14, .gap = 8, .borderRadius = "all-lg") {
        rcRow(.w = "grow", .align = "cl", .gap = 8) {
            section_heading("LOG");
            /* Say when a filter is hiding lines. A log that silently omits its
               own contents is a tool that lies to you. */
            int held = insp_log_count(&st->log), shown = log_visible_count(st);
            RC_String n = shown == held
                ? rcFormat(rcAppArena(app), "%d lines", held)
                : rcFormat(rcAppArena(app), "%d of %d lines (filtered)", shown, held);
            rcText(n, .font = F_SMALL,
                    .color = shown == held ? s.textMuted : INSP_AMBER);
        }
        /* "grow" width = GROW; "tl" keeps a wrapped message top-aligned in its
           cell. The SEQ and LEVEL columns are opt-in: they are what you want
           when correlating with an external trace, and noise the rest of the
           time, so the column SET is what the checkbox actually changes. */
        RC_TableColumn wide[] = {
            { .header = "SEQ",     .w = "48px", .align = "tl" },
            { .header = "SRC",     .w = "52px", .align = "tl" },
            { .header = "LEVEL",   .w = "56px", .align = "tl" },
            { .header = "MESSAGE", .w = "grow", .align = "tl" },
        };
        RC_TableColumn narrow[] = {
            { .header = "SRC",     .w = "52px", .align = "tl" },
            { .header = "MESSAGE", .w = "grow", .align = "tl" },
        };
        RC_TableColumn *cols = st->showDetail ? wide : narrow;
        int ncols = st->showDetail ? 4 : 2;

        rcBox(.w = "grow", .h = "grow", .bg = s.surfaceAlt, .borderRadius = "all-md") {
            if (rcBeginTable("LogScroll", cols, ncols, (RC_TableOptions){0})) {
                int count = insp_log_count(&st->log);
                for (int i = 0; i < count; i++) {
                    const InspLogLine *l = insp_log_at(&st->log, i);
                    if (!log_visible(st, l))
                        continue;
                    bool ray = l->source == INSP_SRC_RAY;
                    RC_Color c = l->level == RC_LOG_ERROR   ? s.danger
                                 : l->level == RC_LOG_WARNING ? INSP_AMBER
                                 : ray                        ? s.textMuted
                                 :                              s.text;
                    rcTableRow();
                    if (st->showDetail) {
                        rcText(rcFormat(rcAppArena(app), "%ld", l->seq),
                                .font = F_SMALL, .color = s.textMuted);
                        rcTableNext();
                    }
                    rcTextC(ray ? "[RAY]" : "[APP]", .font = F_SMALL,
                             .color = ray ? s.primary : s.textMuted);
                    rcTableNext();
                    if (st->showDetail) {
                        rcTextC(level_name(l->level), .font = F_SMALL, .color = c);
                        rcTableNext();
                    }
                    /* No .wrap: word-wrap is the DSL default, so a long line wraps
                       inside the GROW cell. (An explicit "w" is unparsable - caught
                       by this very harness's own [RAY] log during development.) */
                    rcTextC(l->msg, .font = F_SMALL, .color = c);
                }
                rcEndTable();
            }
        }
    }
}

/* --- the frame (layoutCallback) -------------------------------------------------- */

static void layout(RC_App *app, void *userData)
{
    AppState *st = (AppState *)userData;
    rcSetStyle(st->darkMode ? rcStyleDark() : rcStyleLight());
    RC_Style s = rcGetStyle();
    /* A runtime theme switch has to move the window too. rcSetStyle changes every
       colour the UI draws with, but it cannot reach the window BEHIND the layout:
       the clear colour is resolved once at creation and rc_theme.h has no RC_App to
       reach. Without this line the old theme's background stays wherever your layout
       does not cover the window - and it is ALL you see on a frame RayClay holds back
       while it grows the layout arena. Safe to call every frame: the setter is
       change-gated, so setting the colour already in force returns immediately. */
    rcAppSetClearColor(app, s.background);

    rcColumn(.id = "Root", .w = "grow", .h = "grow", .bg = s.background) {
        /* THE BUNDLED BAND, DRAWN BY THE APP. RC_AppOptions.titlebar.custom tells
           the runner to draw no titlebar; rcTitlebar then emits exactly the same
           band wherever it is called, still tagged RC_ID_WINDOW_DRAG and still
           carrying working OS controls. That is what makes it foldable: a bar the
           runner owns is always there, and this one is a line of layout.

           A FOLDED BAR MUST LEAVE A DRAG REGION. Under nativeFrame the window is
           borderless, so drawing nothing would leave a window that cannot be
           moved - and the keystroke that folded it is not discoverable from a
           window you have just made unmovable. The rail is 4 px of the accent
           colour and is the whole drag target. (Desktop only, by the same
           contract: on web rcTitlebar emits nothing and the rail is inert, since
           the browser tab is already the chrome.) */
        if (st->barShown) {
            rcTitlebar(&(RC_TitlebarOptions){ .title = "RayClay Inspector" });
        } else {
            /* The folded rail is chrome too. rcTitlebar above counter-scales
               itself; this hand-built rail did not, so a folded window at 2x zoom
               drew an 8 px rail over a 4 px drag strip. */
            rcUnzoomed() {
                rcBox(.id = RC_ID_WINDOW_DRAG, .w = "grow", .h = "4px",
                       .bg = s.primary) {}
            }
        }

        /* App toolbar: a distinct surface (NOT s.chrome) so it reads as the app's
           own strip, not a second titlebar. The bundled titlebar above already
           carries the product name, so this shows only the subtitle + readout. */
        rcRow(.w = "grow", .h = "44px", .bg = s.surface, .px = 14, .gap = 14, .align = "cl") {
            rcBox(.w = "grow", .h = "grow", .overflow = "hidden", .align = "cl") {
                rcTextL("live test harness",
                         .font = F_TITLE, .color = s.text, .wrap = "n");
            }
            rcTextC(st->barShown ? "Ctrl/Cmd+T hides the titlebar"
                                  : "Ctrl/Cmd+T shows the titlebar",
                     .font = F_SMALL, .color = s.textMuted, .wrap = "n");
            RC_String readout = rcFormat(rcAppArena(app), "%.0f FPS - frame %ld",
                                            rcAppFPS(app), st->frame);
            rcText(readout, .font = F_SMALL, .color = s.textMuted);
        }

        /* A draggable split: gallery (pane 1) | 6px handle | diagnostics (pane 2).
           st->split_frac is pane-1's share, drag-updated + clamped to [0.05, 0.95];
           this replaces the old fixed RC_VW(25) sidebar, so the sidebar now resizes. */
        rcRow(.id = "Content", .w = "grow", .h = "grow", .p = 16) {
            if (rcBeginSplitPane("shell", RC_SPLIT_ROW, &st->split_frac,
                                  (RC_SplitOptions){0})) {
                /* LEFT pane: the reflowing widget gallery. */
                rcColumn(.id = "ColControls", .w = "grow", .h = "grow",
                          .scroll = "v", .gap = 16) {
                    controls(app, st);
                }
                rcSplitHandle();
                /* RIGHT pane: the diagnostics sidebar. */
                /* The sidebar scrolls, and that is a correctness fix rather than a
                   nicety. Its panels have CONTENT-DEPENDENT height - the scheduler
                   one changes with the render mode - and without a scroll the column
                   silently overflows on a short window: the last panel is pushed off
                   the bottom and the ones above draw through each other. The left
                   pane has scrolled since it was written; this one had not. */
                rcColumn(.id = "Sidebar", .w = "grow", .h = "grow", .gap = 16,
                          .scroll = "v") {
                    resource_panel(app, st);
                    system_panel(app, st);
                    sched_panel(app, st);
                    log_panel(app, st);
                }
                rcEndSplitPane();
            }
        }
    }

    /* Follow the newest line, but ONLY while the user is parked at the bottom: if
       they scroll up to read history, stop yanking them down; resume when they
       scroll back. rcIsScrolledToBottom reads the scroll settled BEFORE this frame's
       new line (and is true when the content fits). Driven off the MONOTONIC log.total
       (NOT insp_log_count, which saturates at INSP_LOG_CAP); "Clear log" zeroes total
       (< last_total) so the compare fires once on the now-empty log and re-syncs.
       THIS RUNS BEFORE rcScrollbar ON PURPOSE: the bar samples the scroll offset as it
       declares itself, so following afterwards would draw the thumb a frame stale. */
    if (st->log.total != st->last_total) {
        if (rcIsScrolledToBottom("LogScroll"))
            rcScrollToBottom("LogScroll");
        st->last_total = st->log.total;
    }

    /* Scrollbars for the scroll containers (the log's is the table body, named by the
       id passed to rcBeginTable). Declared inside the layout - each is a floating
       element that layers itself above the container it names. */
    rcScrollbar("ColControls");
    rcScrollbar("Sidebar");
    rcScrollbar("LogScroll");

    /* Record the scratch-arena occupancy now that the frame's formatting is done
       - update() samples this into the history ring next frame. */
    RC_Arena *arena = rcAppArena(app);
    st->cur_arena_occ = arena->bufferLength
                      ? (float)((double)arena->currOffset / (double)arena->bufferLength)
                      : 0.0f;
}

/* --- per-frame update (updateCallback) ------------------------------------------- */

/* Append one chronological sample to a paired history (the render metrics, or
   the OS metrics - both rings have the same shape), scrolling the window down
   once it is full. O(window), and the window is 120. The fastest caller is
   every frame: the scheduler sampler runs unconditionally, so under continuous
   rendering this is ~120 float copies per frame, not per tenth of a second.
   That is still far below anything measurable beside a frame's real work - but
   quote the per-frame figure, because a cost note that names a slower caller
   than the real one is how a real cost gets waved through.
   It keeps the arrays in the exact left-to-right order rcChart and rcSparkline
   plot, with no ring bookkeeping. The two series move together, so one call keeps
   them index-aligned by construction rather than by two callers agreeing to. */
static void hist_push(float *series_a, float *series_b, int *len,
                      float a, float b)
{
    if (*len < INSP_HIST) {
        series_a[*len] = a;
        series_b[*len] = b;
        (*len)++;
        return;
    }
    for (int i = 1; i < INSP_HIST; i++) {
        series_a[i - 1] = series_a[i];
        series_b[i - 1] = series_b[i];
    }
    series_a[INSP_HIST - 1] = a;
    series_b[INSP_HIST - 1] = b;
}

static void update(RC_App *app, void *userData)
{
    AppState *st = (AppState *)userData;

    /* The one-line fix for "my overlay only updates when I touch the app", and
       the reason it is worth reading: EVERY sampler in this file runs inside the
       frame loop, so in on-demand mode an idle app takes NO samples at all - the
       panels freeze at whatever they last saw, which looks like a broken tool
       rather than a sleeping one. Arming a one-shot timer keeps the app genuinely
       PARKED between ticks and pays only while it is on demand.
       It costs the performance panel its meaning, and that trade is the point:
       rcAppFrameTime is the WALL GAP between frames, so once we wake ourselves at
       1 Hz the "frame" row reports OUR cadence, not RayClay's cost. The panel says
       so on screen rather than quietly reporting 1000 ms as a frame time.
       Armed unconditionally, on purpose. In continuous mode it is a no-op - the
       app is already drawing every frame and only the earliest request is kept - so
       there is nothing to gate on. Gating it on our own copy of the mode would be
       worse than useless: RAYCLAY_RENDER_MODE overrides the app's choice at startup
       and there is no public reader, so that flag can be false when the app really
       is parked, and the panel would silently never sample. */
    rcAppRequestFrameAfter(app, 1.0);

    /* Fold the titlebar. RC_MOD_PRIMARY is Cmd on a native macOS build and Ctrl
       everywhere else including a browser on a Mac, so one binding is right on
       every target; the letter query is logical, so it is T on every layout.
       Inert on web by consequence rather than by a special case - there is no
       bundled band there to fold. */
    if (rcModDown(RC_MOD_PRIMARY) && rcKeyPressed(RC_KEY_T))
        st->barShown = !st->barShown;

    /* Resize detection -> an [APP] log line. rcGetWindowDimensions is real
       logical pixels (zoom-independent). The first frame only seeds the baseline
       (last_win_* start at 0), so relaunch does not log a phantom resize. */
    RC_Dimensions dims = rcGetWindowDimensions();
    int w = (int)dims.width;
    int h = (int)dims.height;
    if (st->last_win_w != 0 && (w != st->last_win_w || h != st->last_win_h)) {
        RC_String line = rcFormat(rcAppArena(app), "resize  %dx%d -> %dx%d",
                                     st->last_win_w, st->last_win_h, w, h);
        insp_log_push_len(&st->log, INSP_SRC_APP, RC_LOG_INFO, line.chars, line.length);
    }
    st->last_win_w = w;
    st->last_win_h = h;

    double dt_ms = rcAppFrameTime(app) * 1000.0;

    /* Render metrics ~every 100 ms (a responsive ~12 s trend). */
    st->perf_accum_ms += dt_ms;
    if (st->perf_accum_ms >= 100.0) {
        hist_push(st->perf_fps, st->perf_ms, &st->perf_len,
                  rcAppFPS(app), rcAppFrameTime(app) * 1000.0f);
        st->perf_accum_ms = 0.0;
    }

    /* OS metrics ~every 1 s. rcProcessCpuPercent accounts CPU since the previous
       call, so it is called HERE and nowhere else; its first call has no interval
       and returns 0. A <0 reading (CPU on web; memory when the OS offers none) is
       kept as "unavailable" and plotted as 0, so it never spikes the sparkline. */
    st->sys_accum_ms += dt_ms;
    if (st->sys_accum_ms >= 1000.0) {
        float  cpu   = rcProcessCpuPercent();
        size_t bytes = rcProcessMemoryBytes();
        st->cur_cpu    = cpu;
        st->cur_mem_mb = bytes ? (float)bytes / (1024.0f * 1024.0f) : -1.0f;
        hist_push(st->sys_cpu, st->sys_mem, &st->sys_len,
                  cpu < 0.0f ? 0.0f : cpu,
                  st->cur_mem_mb < 0.0f ? 0.0f : st->cur_mem_mb);

        st->sys_accum_ms = 0.0;
    }

    /* Scheduler counters, sampled EVERY frame and stored as a delta.
       Not on the 1 Hz tick above, and the reason is a trap worth knowing: that
       tick accumulates rcAppFrameTime, which is exponentially SMOOTHED, so on an
       on-demand app it under-counts badly and the tick may never fire at all. This
       sampler must not inherit that - the scheduler panel is the one thing that
       has to keep working precisely when the app is asleep. */
    RC_SchedStats sc = rcAppSchedStats(app);
    hist_push(st->sched_spur, st->sched_adm, &st->sched_len,
              (float)(sc.spurious - st->sched_prev_spur),
              (float)(sc.admitted - st->sched_prev_adm));

    /* The mode oracle, and it must be a delta. `waits` is cumulative and nothing
       ever resets it - rcAppSetContinuousRendering does not - so `waits > 0` is a
       LATCH, not a reading: toggle to on demand once and back, and it answers
       "parked" forever while the app draws every frame. This panel's own text two
       rows down tells you to chart the delta, and the first version of it read the
       raw value anyway.
       Count dry samples, do not test one. A single frame can legitimately admit
       without parking when input is already queued (a fast drag), so a one-frame
       delta flickers exactly while the user is working the panel. A full window with
       no park at all is the honest statement of "this app is not parking". */
    st->sched_dry = (sc.waits != st->sched_prev_waits) ? 0 : st->sched_dry + 1;
    if (st->sched_dry > INSP_HIST)
        st->sched_dry = INSP_HIST;

    st->sched_prev_spur  = sc.spurious;
    st->sched_prev_adm   = sc.admitted;
    st->sched_prev_waits = sc.waits;
    st->sched_cur        = sc;

    st->frame++;
}

int main(void)
{
    /* AppState holds the log ring + history rings (tens of KB) and must outlive
       the run because the log sink writes into it - so it is static, which also
       keeps it off the stack. Designated init zeroes the rest. */
    static AppState state = {
        .darkMode      = true,
        .barShown      = true,
        .histFrac      = 1.0f,  /* the whole history until you narrow it */
        .inspectSticky = true,  /* open in the correct non-modal pairing */
        .split_frac    = 0.72f, /* pane-1 (controls) share; the rest is the sidebar */
        .cur_cpu     = -1.0f,   /* "n/a" until the first ~1 Hz OS sample lands */
        .cur_mem_mb  = -1.0f,
        .continuous  = true,    /* MUST match .renderMode below; the toggle reads it */
    };

    /* Zero-asset: baked from the bundled face at each size (no fontPath). */
    static const float fontSizes[F_COUNT] = {
        [F_SMALL] = 13.0f,
        [F_BODY]  = 16.0f,
        [F_HEAD]  = 20.0f,
        [F_TITLE] = 26.0f,
    };

    rcSetStyle(rcStyleDark());

    /* Install the sink BEFORE rcRunApp so diagnostics emitted while the window
       and backends come up are captured too. NULL would restore stderr. */
    rcSetLogSink(on_log, &state);

    RC_AppOptions opts = {
        .width             = 1200,
        .height            = 760,
        .title             = "RayClay Inspector",
        .clearColor        = rcGetStyle().background,
        .fontSizes         = fontSizes,
        .fontCount         = F_COUNT,
        .scratchArenaBytes = 64 * 1024,
        .nativeFrame       = true,   /* borderless + the bundled titlebar */
        /* .custom means the runner draws no band; layout() calls rcTitlebar
           itself so the same bundled bar can be folded away with a keystroke. */
        .titlebar          = { .custom = true },
        .updateCallback          = update,
        .layoutCallback          = layout,
        .frameEndCallback  = frame_end,
        .userData          = &state,
        /* The exception that proves the rule. This app MEASURES the frame
           loop, so it must have one: rcAppFrameTime is the wall gap between
           frames, so a timed step (rcAppRequestFrameAfter(app, 0.1)) would make
           the PERFORMANCE panel report its own 100 ms cadence as the frame cost
           instead of RayClay's. An inspector is therefore one of the few apps
           that genuinely needs every frame - and its CPU row reads a continuous
           app, NOT RayClay's idle figure, which is ~0 on demand. Ordinary apps
           stay on demand; see ex03/ex10 for asking for frames on purpose. */
        .renderMode        = RC_RENDER_CONTINUOUS,
    };

    int rc = rcRunApp(&opts);

    /* THE RENDER WITNESS, and it is an EXIT CODE because this example has no
       other honest channel.
       Every sibling example is proved to have drawn by grepping stdout for the
       runner's "rendered N of N budgeted frames". This one installs
       rcSetLogSink, so that line goes into the in-app ring instead - the whole
       point of the app.

       The route the budget arrives by decides who owns that line:
         - RAYCLAY_MAX_FRAMES (the environment): the library emits the witness
           PAST the sink, because a sink the APP installed must not swallow the
           reply to a question the app did not ask. The harness reads the same
           strong witness here as for every sibling.
         - RC_AppOptions.maxFrames: the sink owns it, and the line is the app's.
       This example cannot print its way out either: it is pure RC_ with no libc
       include (test/check-examples-pure-rc.sh enforces that), and RayClay
       exposes a way to RECEIVE diagnostics, not to emit one.
       => The exit code is what covers the OPTIONS route and the unbounded run,
       which is why it stays. Exit 0 MEANS "presented at least one frame".

       Do not tighten this to `!= budget`. Two independent reasons, and the
       second is measured, not assumed:
         1. the app cannot read RAYCLAY_MAX_FRAMES without getenv, and this
            example includes no libc;
         2. frames_drawn LEGITIMATELY EXCEEDS the budget. The runner's own
            "rendered N of N" counts MAIN-LOOP frames, while this callback fires
            on every PRESENTED frame - including the live-resize repaints GLFW
            drives from its window-refresh hook, which the runner excludes by
            design. Measured under Xvfb 2026-08-06: budget 30 -> 31 calls,
            budget 90 -> 91, the single extra being the refresh GLFW delivers
            when the window is first mapped. During a real resize drag it is
            many more, and under -DRC_NO_LIVE_RESIZE it is none.
       => The only assertion this callback can carry portably is "> 0". */
    if (rc == 0 && state.frames_drawn == 0)
        rc = 3;

    return rc;
}
