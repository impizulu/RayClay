# RayClay cheatsheet

> **v0.8.11 quick reference card**: every public function in the RayClay API, one line each.
> Long-form material (measurements, traps, the numbers behind a claim) lives in
> **[`api-notes.md`](api-notes.md)**, so this page can stay a card.

RayClay is a pure-C99 immediate-mode GUI library: a flexbox-style layout core plus a native renderer
that builds **one source** into a desktop window *or* a browser (WASM).
Use it with a single include (`#include "rayclay.h"`) and link `librayclay`. Everything you **call**
is `camelCase` (`rcRunApp`, `rcBox`); public **types** keep the `RC_` prefix (`RC_AppOptions`); constants
and config macros are `RC_CAPS` (`RC_GROW`). The API is layered: **L1** core renderer (always on), **L2** UI helpers + element DSL
(`RC_NO_UI_HELPERS` to drop), **L3** app-loop runner (`RC_NO_APP_RUNNER` to drop).

```c
#include "rayclay.h"

int main(void) { return rcRunApp(NULL); }   // the whole app, in two lines
```

---

## CSS → RayClay: what transfers

RayClay's DSL is CSS-*like*, not CSS: a familiar subset with a few deliberate deviations.
**The per-field map lives with its audience: [`for-web-developers.md` ▸ CSS → RayClay: the per-field map](for-web-developers.md#css--rayclay-the-per-field-map).**

---

## module: core, L1 renderer (always available)

```c
// Window + frame
void            rcInitWindow(int width, int height, const char *title); // Open a GLFW window + GL context (the runner does this for you)
void            rcCloseWindow(void);                                       // Tear down the backends + window: the SPLIT-LOOP host's teardown  → docs/api-notes.md ▸ rcCloseWindow
//   Not how an rcRunApp app quits itself - a Quit button calls rcAppRequestClose(app). Calling this from a callback is safe but the teardown's TIMING is not yours to predict.
void            rcRender(RC_RenderCommandArray commands);                 // Translate one frame of render commands into draw calls (after the layout pass)
RC_Dimensions   rcGetWindowDimensions(void);                              // Window size in real logical px (zoom never scales it)
float           rcGetContentScale(void);                                  // Display device-pixel-ratio; read-only, independent of zoom  → docs/api-notes.md ▸ rcGetContentScale / rcGetWindowDimensions
void            rcSyncLayoutDimensions(void);                              // Push the layout size, floor(window / zoom), into the layout engine (once per frame before layout)
RC_String     rcStringFromCStr(const char *cstr);                        // Wrap a C string as a RC_String without copying (you keep it alive)

// Diagnostics
void            rcSetLogSink(RC_LogCallback sink, void *user);            // Route warnings/errors to your callback (NULL restores stderr)
size_t          rcProcessMemoryBytes(void);                               // This process's resident memory (0 = platform offers no reading)
float           rcProcessCpuPercent(void);                                // CPU since the PREVIOUS call, as % of ONE core (like `top`)  → docs/api-notes.md ▸ rcProcessMemoryBytes / rcProcessCpuPercent

// Fonts
uint16_t        rcLoadFont(const char *path, float size);                  // Bake a TTF/OTF at size px into the atlas; returns its fontId.
bool            rcUnloadFont(uint16_t font);                               // Release a fontId's slot so a later load can reuse it.  → docs/api-notes.md ▸ rcUnloadFont
uint16_t        rcRegisterFont(const char *family, RC_FontWeight weight, const char *path, float size);  // Bake AND index by (family, weight, size)
uint16_t        rcFont(const char *family, RC_FontWeight weight, float size);   // Resolve a registered (family, weight, size) to its fontId
//     See docs/api-notes.md, RC_FONT_LAST_CODEPOINT - widening the baked range
RC_Dimensions rcMeasureText(RC_StringSlice text, RC_TextElementConfig *config, void *userData); // The UTF-8 text-measure callback the runner registers for you

// Images  (compile out with RC_NO_IMAGE)
RC_Image        rcLoadImage(const char *path);                             // Load a PNG/JPG/BMP file into a GPU texture (check .handle != NULL).
RC_Image        rcLoadImageFromMemory(const unsigned char *bytes, int len);// Like rcLoadImage but decode an encoded image already in memory
void            rcUnloadImage(RC_Image *img);                              // Free the GPU texture behind an image and zero its handle  → docs/api-notes.md ▸ rcUnloadImage

// SVG  (compile out with RC_NO_SVG)
void            rcSvg(const char *path, float size, RC_Color color);       // Draw an SVG BY PATH straight in the layout: parsed once, cached, freed for you. START HERE
RC_Svg *        rcLoadSvg(const char *path);                               // Parse an .svg at runtime into a handle YOU own; NULL on failure (reason logged)
RC_Svg *        rcLoadSvgFromMemory(const char *bytes, int len);           // Like rcLoadSvg but parse SVG text already in memory (a baked asset, or markup built this frame)
void            rcSvgHandle(const RC_Svg *svg, float size, RC_Color color);// Draw from a handle you loaded: for markup with no file behind it, or swapping handles
void            rcUnloadSvg(RC_Svg **svg);                                 // Free a parsed SVG and NULL the caller's pointer; safe to call twice  → docs/api-notes.md ▸ Loading an SVG at runtime

// Per-frame arena + formatting
RC_Arena        rcArenaInit(size_t size);                                  // Create a bump allocator for per-frame allocations
void           *rcArenaAlloc(RC_Arena *arena, size_t size);                // Allocate size bytes from the arena
void            rcArenaReset(RC_Arena *arena);                             // Reclaim every arena allocation at once (no per-item free)
void            rcArenaFree(RC_Arena *arena);                              // Release the arena's backing buffer
RC_String     rcFormat(RC_Arena *arena, const char *fmt, ...);           // printf into arena memory; the RC_String lives until rcArenaReset
```

## module: app: L3 runner (optional; `RC_NO_APP_RUNNER` to drop, which forfeits the idle win, see build-time configuration)

```c
// The one-call entry point  (blocks until the window closes)
int        rcRunApp(const RC_AppOptions *options);       // Run the full app loop from an options struct (desktop + web)

// Split lifecycle  (for a host that owns its own frame loop)
RC_App    *rcAppCreate(const RC_AppOptions *options);    // Open the window + init backends; NULL on failure
bool       rcRunFrame(RC_App *app);                      // Render exactly one frame; false => time to stop
void       rcAppDestroy(RC_App *app);                    // Normal teardown + free the RC_App  → docs/api-notes.md ▸ rcAppDestroy

// Accessors  (use inside the update/layout callbacks)
RC_Arena  *rcAppArena(RC_App *app);                      // The runner's per-frame scratch arena (for rcFormat)  → docs/api-notes.md ▸ rcAppArena
//   Needs RC_AppOptions.scratchArenaBytes > 0; at the 0 default rcFormat returns a visible marker
void       rcAppRequestClose(RC_App *app);               // Ask the runner to exit its loop after the current frame
bool       rcAppIsDebugEnabled(const RC_App *app);       // Whether the layout inspector is currently on
float      rcAppFrameTime(const RC_App *app);            // Smoothed seconds BETWEEN frames (0 before the first timed frame)
float      rcAppFPS(const RC_App *app);                  // Reciprocal of rcAppFrameTime; see the caveat below
double     rcAppTime(const RC_App *app);                 // Monotonic seconds since windowing start-up: the clock to drive animation from  → docs/api-notes.md ▸ rcAppTime
//   Only differences are meaningful. The origin is not a date, not process start, and not the same for a second app in one process.
RC_FrameCounts rcAppFrameCounts(const RC_App *app);      // What you DECLARED vs what actually DREW  → docs/api-notes.md ▸ rcAppFrameCounts
//   .declared (before culling) · .drawCommands (survived culling). Always compiled in: no build knob

// Redraw scheduling
void       rcAppRequestFrame(RC_App *app);               // "my state changed, draw once more"
void       rcAppRequestFrameAfter(RC_App *app, double s);// Timed step: wake in s seconds, then draw
void       rcAppSetContinuousRendering(RC_App *app, bool on); // Flip the mode at runtime  → docs/api-notes.md ▸ rcAppRequestFrame / rcAppRequestFrameAfter

float       rcAppZoom(const RC_App *app);                // Current zoom factor (1.0 = 100%; 1.0 for a NULL app)
void        rcAppSetZoom(RC_App *app, float zoom);       // Set the zoom factor, clamped to [minZoom, maxZoom] (for a reset action or persisting the user's choice)
RC_ZoomMode rcAppZoomMode(const RC_App *app);            // Current zoom mode (RC_ZOOM_LAYOUT / RC_ZOOM_OPTICAL; LAYOUT for a NULL app)
void        rcAppSetZoomMode(RC_App *app, RC_ZoomMode m);// Switch layout<->optical at runtime (effect next frame; the zoom factor is preserved)
const float *rcAppZoomLadder(const RC_App *app, uint16_t *count);   // → docs/api-notes.md ▸ rcAppZoomLadder
int         rcAppTitlebarHeight(const RC_App *app);      // Caption-strip height in PHYSICAL px (0 for a NULL app)
void        rcAppSetTitlebarHeight(RC_App *app, int h);  // Re-set the caption height when your bar folds/animates. Only Windows reads it, as the drag region; never moves the window minimum; <=0 = none, unchanged = no-op
//   Windows reserves the top SM_CYSIZEFRAME + SM_CXPADDEDBORDER px as the RESIZE BORDER and it outranks the caption (4+4=8 at 96 DPI, more when scaled),
//   so a strip that thin keeps no draggable pixel there. Measured with WM_NCHITTEST. See docs/api-notes.md, rcAppTitlebarHeight / rcAppSetTitlebarHeight
RC_Color    rcAppClearColor(const RC_App *app);          // The window background actually painted (ALWAYS opaque; opaque black for a NULL app)
void        rcAppSetClearColor(RC_App *app, RC_Color color); // Change it live; rcSetStyle cannot. alpha 0 = "the theme's background AT THIS MOMENT"  → docs/api-notes.md ▸ rcAppClearColor

// Hold a subtree OUT of the zoom: chrome, not content (a custom titlebar, a HUD, a status strip)
rcUnzoomed() { ... }                                     // Block: everything inside keeps a CONSTANT on-screen size at any zoom
void  rcBeginUnzoomed(void);                             // The scope's push/pop, for when a block will not fit (opens in one
void  rcEndUnzoomed(void);                               //   helper, closes in another). Prefer rcUnzoomed(); it cannot unbalance
float rcUnzoomedScale(void);                             // 1/zoom inside a scope, exactly 1.0 outside: for YOUR OWN pixel maths

// Built-in titlebar
void       rcTitlebar(const RC_TitlebarOptions *options); // The bundled bar at the current position; NULL = defaults
void       rcWindowControlButton(RC_WindowControl c, RC_IconCallback icon, float iconSize); // One control, as a Flat Slab
void       rcWindowControls(void);                       // The standard min/max/close cluster, in one call
bool       rcIsWindowMaximized(void);                    // True while maximised; swap your custom bar's maximise glyph for a restore glyph  → docs/api-notes.md ▸ The bundled titlebar (Flat Slab)
//   Fixed chrome: content zoom never resizes or moves the bundled band (opt in with .zoomWithContent)
```

## module: layout, L2 element DSL (`RC_NO_UI_HELPERS` to drop)

```c
// Containers  (open a brace body; configure with designated initialisers)
rcBox(...) { ... }         // Generic container, top-to-bottom by default
rcColumn(...) { ... }      // Explicit vertical container
rcRow(...) { ... }         // Horizontal container
rcSeparator(...) { ... }   // A stretchy spacer that grows to fill
rcMargin(...) { ... }      // A fit-sized spacer  → docs/api-notes.md ▸ Leaving a container body early

// Typed sizing helpers (.wType / .hType): the 1:1 equivalent for a RUNTIME-COMPUTED size a string literal
// can't hold (e.g. RC_PX(row.height)), and a parse-skipping fast path. A typed axis wins over its string.
RC_FIT                      // Shrink to content
RC_GROW                    // Grow to fill the parent
RC_PX(px)                  // Fixed px pixels
RC_PCT(pct)                // pct percent of the parent
RC_VW(vw)                  // vw percent of the viewport (window) width  (like CSS vw; correct in both zoom modes)
RC_VH(vh)                  // vh percent of the viewport (window) height (like CSS vh; tracks window resize)

// Compose your own helpers
RC_ElementDeclaration   rcParseComponentOptions(RC_ComponentOptions opts, RC_ElementDeclaration base); // Build one element declaration from flat RC_ options
void                    rcBeginComponent(RC_ComponentOptions opts, RC_ElementDeclaration defaults);    // Open + configure one element (handles the id)
void                    rcEndComponent(void);                                                            // Close the element rcBeginComponent opened.

rcComponent(defaults, ...) // The macro rcBox/rcRow/rcColumn/rcSeparator/rcMargin are each one line of: pairs Begin+End
rcExpand(x)                // Identity macro. Wrap a forwarded __VA_ARGS__ in it so MSVC's traditional preprocessor expands it  → docs/api-notes.md ▸ rcComponent  (worked example in ex20_system_monitor: `card`)
```

## module: text, L2 (`RC_NO_UI_HELPERS` to drop)

```c
rcTextL(literal, ...)      // Emit a text element from a compile-time string literal
rcText(string, ...)        // Emit a text element from a runtime RC_String
rcTextC(cstr, ...)         // Emit a text element from a runtime C string  → docs/api-notes.md ▸ rcTextC

RC_TextElementConfig rcBuildTextConfig(RC_TextOptions opts); // Build one text-run config from flat RC_ options (compose your own text macros)
```

## module: widgets, L2 (`RC_NO_UI_HELPERS` to drop)

```c
// Controls  (each takes a unique string id; persistent state is yours, by pointer)
bool rcButton(const char *id, const char *label, RC_ButtonVariant variant);     // A clickable button; true on the RELEASE edge, like rcClicked (slide off before releasing and it cancels)
bool rcCheckbox(const char *id, const char *label, bool *value);                // A labelled checkbox bound to *value; true on change
bool rcToggle(const char *id, bool *value);                                     // An on/off switch bound to *value; true on change
bool rcSlider(const char *id, float *value, float min, float max);              // A horizontal slider clamped to [min, max]; true on change
void rcProgress(const char *id, float fraction);                                // A determinate progress bar (fraction 0..1; visual only)
bool rcRadio(const char *id, const char *label, int *selected, int index);      // A radio option in a *selected group; true when it becomes selected
bool rcCombo(const char *id, int *selected, const char *const *items, int count);// A dropdown of items; true on the frame the selection changes

// Text input  (native stb_textedit over YOUR buffer; TYPED input is ASCII in v1, but a .placeholder or a value you PRE-FILL renders full Latin-1)
bool rcTextInput(const char *id, char *buf, int cap, ...);                       // Single-line text field; true on a frame the buffer changed  → docs/api-notes.md ▸ rcTextInput: the caret settle
bool rcTextInputEx(const char *id, char *buf, int cap, RC_TextInputOptions opts);// As rcTextInput but options by value (C-ABI stable)
bool rcTextArea(const char *id, char *buf, int cap, ...);                        // Multiline area (Enter=newline, soft-wrap, .rows=N); true on a frame the buffer changed

// Scrolling
void rcScrollbar(const char *containerId);                                      // Draw a draggable VERTICAL scrollbar for a scroll container (v1: vertical only; horizontal overflow gets no bar)  → docs/api-notes.md ▸ Big tables: virtualize the row loop
void rcScrollBy(const char *containerId, float deltaX, float deltaY);           // Scroll a clip container by a delta, programmatically  → docs/api-notes.md ▸ rcScrollBy: the sign convention
void rcScrollToTop(const char *containerId);                                    // Scroll a clip container to the top
void rcScrollToBottom(const char *containerId);                                 // Scroll a clip container to the bottom
RC_ScrollInfo   rcGetScrollInfo(const char *containerId);                 // READ a container's position + travel, DOM-style  → docs/api-notes.md ▸ RC_ScrollInfo
bool rcIsScrolledToBottom(const char *containerId);                             // At (or past) the bottom edge? ALSO true when the content fits and there is nothing to scroll; false for an unknown id  → docs/api-notes.md ▸ clip slots
// Load-bearing: 100 clip slots per frame; .clip/.overflow/.scroll each consume one, even when nothing scrolls. See docs/api-notes.md, clip slots

rcVirtualList(var, containerId, count, rowHeight) { ... }  // Loop macro; var.index is the DATA index
RC_VirtualRow rcVirtualBegin(const char *containerId, int count, float rowHeight); // What it desugars to
bool          rcVirtualNext(RC_VirtualRow *row);           // ...the loop step  → docs/api-notes.md ▸ rcVirtualList: the three rules
rcColumn(.id = "Notes", .scroll = "v", .h = "grow") {
    rcVirtualList(row, "Notes", noteCount, 32) {                                // 32 = the REAL row height in px  → docs/api-notes.md ▸ Element ids are 32-bit hashes
        rcRow(.id = rcFormat(mem, "note%d", row.index).chars,                   // key by the DATA index; rcFormat's result is
               .w = "grow", .h = "32") { rcTextC(notes[row.index].title); }     // NUL-terminated, so .chars is a char *
    }
}  // → docs/api-notes.md ▸ rcVirtualList: the three rules

// Follow the tail WITHOUT yanking a reader who scrolled up, the log/chat/table idiom (ex12 does exactly this):
if (lineTotal != lastTotal) {                        // a NEW line arrived. Count from a MONOTONIC total, not a
    if (rcIsScrolledToBottom("log")) rcScrollToBottom("log");   // ring's saturating count. That stops firing once full
    lastTotal = lineTotal;
}

// Menus  (one open at a time; call End* only when the Begin returned true)
bool rcBeginMenu(const char *id, const char *label);                            // Open a menu button + its floating item column; true while open
void rcEndMenu(void);                                                           // Close the open menu
bool rcBeginContextMenu(const char *id, const char *targetId);                  // Open a context menu on right-click/long-press of targetId; true while open
void rcEndContextMenu(void);                                                    // Close the open context menu
bool rcMenuItem(const char *label);                                             // A row in the open menu; true on the frame it is activated

// Popups: modal dialogs + non-modal panels  (emit the body only while open; they STACK up to 8 deep; Esc dismisses the topmost first)
bool rcBeginModal(const char *id, bool *open);                                  // Open a scrim + centered panel; true while *open (Esc/scrim dismisses)
bool rcBeginModalEx(const char *id, bool *open, RC_ModalOptions opts);          // As rcBeginModal but options by value (C-ABI stable)
void rcEndModal(void);                                                          // Close the popup body (call once when Begin returned true)
bool rcIsModalOpen(void);                                                       // Is a MODAL dialog open right now? Flips on the same frame boundary the scrim does  → docs/api-notes.md ▸ rcIsModalOpen / RC_ModalOptions

// A panel that stays live while you work needs BOTH .modality and .noBackdropDismiss
if (rcBeginModalEx("inspector", &open,
        (RC_ModalOptions){ .modality = RC_MODALITY_NON_MODAL, .noBackdropDismiss = true })) {  // detached inspector: app live, panel persists
    rcEndModal();
}

// Charts  (native plotting, immediate-mode: pass this frame's data every frame)
void rcChart(const char *id, const RC_Series *series, int seriesCount, RC_ChartOptions options);
void rcSparkline(const char *id, const float *values, int count, RC_SparklineOptions options);   // → docs/api-notes.md ▸ rcSparkline

// Series/axis knobs live on the ARRAY + options, not on extra calls:
//   RC_Series  .kind (LINE/BAR/AREA/SCATTER) · .color · .label · .thickness · .axis · .points
//   RC_Axis    .min/.max · .label · .ticks · .grid · .hide   (zero-init => auto-fit + nice ticks)
// Hover readout: .tooltip (the TRIGGER) · .tooltipPlace/.tooltipAnchor/.tooltipOffset (WHERE it sits) ·
//   .hoverGuide / .hoverMarkers (draw the hover IN the plot; both independent of .tooltip)
float cpu[120]; /* your ring buffer: borrowed, must outlive rcRender() */
rcBox(.h = "40") { rcSparkline("cpu", cpu, 120, (RC_SparklineOptions){0}); }                    // inline strip: LINE, theme accent, auto-fit
rcBox(.h = "220") {
    RC_Series s[] = { { .y = rss, .count = 120, .kind = RC_SERIES_AREA, .label = "RSS" },
                      { .y = cpu, .count = 120, .kind = RC_SERIES_LINE, .label = "CPU %", .axis = 1 } };  // .axis=1 -> the right (y2) axis
    rcChart("res", s, 2, (RC_ChartOptions){ .legend = true, .y = { .grid = true }, .y2 = { .max = 100 },
                                             .tooltip = RC_CHART_TOOLTIP_NEAREST });                       // hover => "x 10 / RSS 41 / CPU % 78"
}

// Zoom/pan: no widget needed; pin .min/.max yourself
static float lo = 0.0f, hi = 500.0f;                                             // YOUR viewport over the data
if (rcIsHovered("res")) {
    float w = rcScrollDeltaY();                                                  // 0 when Ctrl+wheel zoom consumed it
    if (w != 0.0f) {
        float mid = (lo + hi) * 0.5f, half = (hi - lo) * 0.5f * (w > 0.0f ? 0.9f : 1.1f);
        lo = mid - half; hi = mid + half;
    }
}
rcChart("res", s, 2, (RC_ChartOptions){ .x = { .min = lo, .max = hi } });        // pan = add the same delta to both  → docs/api-notes.md ▸ rcChart

// Split panes  (a draggable divider between two resizable panes, the sidebar+content shell of IDEs/inspectors/dashboards; call SplitHandle BETWEEN them, End only when Begin returned true)
bool rcBeginSplitPane(const char *id, RC_SplitAxis axis, float *fraction, RC_SplitOptions opts);
void rcSplitHandle(void);                                                        // Emit the draggable divider BETWEEN the two panes (grab cursor; keeps dragging if the pointer outruns it)
void rcEndSplitPane(void);                                                       // Close the split container (call once, only when Begin returned true)
// RC_SPLIT_ROW = panes side-by-side, a vertical divider dragged left/right;  RC_SPLIT_COLUMN = stacked, a horizontal divider dragged up/down.  Zero opts -> clamp 0.05/0.95, 6px handle.
static float frac = 0.25f;                                                        // pane-1's share; yours to persist across frames
if (rcBeginSplitPane("shell", RC_SPLIT_ROW, &frac, (RC_SplitOptions){0})) {
    /* ... first pane ... */  rcSplitHandle();  /* ... second pane ... */
    rcEndSplitPane();
}

// Table  (a column-declared data grid; same Begin/Row/End discipline as the other containers)
bool rcBeginTable(const char *id, const RC_TableColumn *cols, int colCount, RC_TableOptions opts);
void rcTableRow(void);                                                           // Close any open row; open the next row AND its first cell
void rcTableNext(void);                                                          // Close the current cell; open the next column's cell (past the last column it warns once and content stays put)
void rcEndTable(void);                                                           // Close the table (call once, only when Begin returned true)  → docs/api-notes.md ▸ rcEndTable
RC_TableColumn cols[] = { { .header = "Lvl", .w = "48px", .align = "cl" }, { .header = "Message", .w = "grow", .align = "cl" } };  // widths read as CSS strings
if (rcBeginTable("log", cols, 2, (RC_TableOptions){0})) {
    for (int i = 0; i < n; i++) { rcTableRow(); rcTextC(e[i].lvl); rcTableNext(); rcTextC(e[i].msg); }
    rcEndTable();
}
rcScrollbar("log");                                                              // the table's id IS the scroll body  → docs/api-notes.md ▸ Big tables: virtualize the row loop

enum { GRID_ROW_H = 28, GRID_PAD = 4, GRID_PITCH = GRID_ROW_H + 2 * GRID_PAD };
if (rcBeginTable("grid", cols, 2, (RC_TableOptions){ .cellPadding = RC_VAL(GRID_PAD) })) {
    rcVirtualList(row, "grid", rowCount, GRID_PITCH) {   // "grid" = the table body's id; the PITCH, not the height
        rcTableRow();  rcTextC(data[row.index].name);
        rcTableNext(); rcTextC(data[row.index].value);
    }
    rcEndTable();
}  // → docs/api-notes.md ▸ rcVirtualList: the three rules
// Three rules, and breaking out of the loop costs you the trailing spacer
```

## module: interaction, element reads + small helpers (always available)

```c
// Element reads  (ask about ANY element you tagged with an .id; call inside the layout callback)
bool         rcIsHovered(const char *id);              // True while the pointer is over the element with this id (this frame)
bool         rcClicked(const char *id);                // True on the RELEASE edge: press it, then release still over it. Turns any rcBox into a button
bool         rcPressed(const char *id);                // True on the PRESS edge: eager, for a nudge/step/jump control where latency is the point
RC_SchedStats rcAppSchedStats(const RC_App *app);     // Frame-admission counters; spurious/waits is the wake-loop metric
const RC_PerfFrame *rcAppPerfFrame(const RC_App *app); // What the last DRAWN frame cost. Needs -DRC_PERF_COUNTERS=1, the one public fn not compiled in by default; read it from a frameEndCallback  → docs/api-notes.md ▸ RC_PERF_COUNTERS
const char*  rcFrameReasonName(RC_FrameReason r);     // "input", "window", … (a chart series label for byReason[])

RC_FrameReason is an index into RC_SchedStats.byReason (NOT a bitmask):
  RC_FRAME_INITIAL       the first frame after create; always drawn
  RC_FRAME_INPUT         pointer / key / text / scroll / focus activity
  RC_FRAME_WINDOW        size, framebuffer, DPI / content-scale, iconify
  RC_FRAME_EXPOSE        damage / refresh, always 0: nothing raises it today
  RC_FRAME_RESOURCE      an async backend published something
  RC_FRAME_APP           rcAppRequestFrame / rcAppRequestFrameAfter
  RC_FRAME_DEADLINE      a registered deadline came due
  RC_FRAME_INTERNAL      runner-internal (zoom settle, drag, titlebar ease)
  RC_FRAME_REASON_COUNT  8: the array length, not a reason

RC_SchedStats fields: admitted, waits, deadlineWakes, spurious, requests,
                     refreshRepaints, byReason[]
  All CUMULATIVE: difference two reads to plot. spurious/waits is the wake-loop
  share; spurious/admitted compares two different populations. Zero under a
  manual rcRunFrame loop, because the scheduler is rcRunApp's.
  spurious counts parks that woke for NOTHING. A park that ended on its own
  deadline is NOT spurious. refreshRepaints counts window-refresh callback ENTRIES: how often the OS
  ASKED for a synchronous repaint OUTSIDE the admit path, NOT frames drawn; it is incremented before
  the frame is attempted and a re-entrant callback is dropped without painting (macOS: +40 across one
  window zoom, about two frames completed), a different population from the parks
  the other fields count. DO NOT COMPUTE spurious - refreshRepaints: a repaint
  that ended no park is counted in one and not the other, so the subtraction can
  go NEGATIVE and these are uint64_t (measured: spurious 0, refreshRepaints 1).
  Read the two side by side.  // → docs/api-notes.md ▸ rcClicked and the pointer cursor
bool         rcIsFocused(const char *id);              // True while the rcTextInput with this id holds keyboard focus
void         rcSetFocus(const char *id);               // Move keyboard focus to that text input (NULL/"" clears); call on an event, not every frame
void         rcSetCursor(RC_Cursor cursor);            // This frame's cursor shape (last-writer-wins; CSS-style).  → docs/api-notes.md ▸ The gesture vocabulary

// Pointer position + buttons: the gesture vocabulary, for a gesture still in flight
RC_Vec2 rcPointer(void);                          // Pointer position, in CONTENT space  → docs/api-notes.md ▸ RC_Vec2
bool         rcPointerDown(RC_PointerButton button);       // True while the button is held: the LEVEL you track a drag with
bool         rcPointerPressed(RC_PointerButton button);    // True on the frame it goes down: the edge that STARTS a gesture (fires once; does not repeat while held)
bool         rcPointerReleased(RC_PointerButton button);   // True on the frame it goes up: the edge that COMMITS.  → docs/api-notes.md ▸ rcPointerReleased

// Element geometry: WHERE a box ended up, in the same CONTENT space rcPointer reports
RC_Box       rcGetElementBox(const char *id);          // Where that element ended up, in CONTENT space
RC_Box       rcChartPlotRect(const char *chartId);     // A chart's PLOT area (NOT its outer box)  → docs/api-notes.md ▸ RC_Box

// Pointer wheel
float        rcScrollDeltaY(void);                     // Mouse-wheel delta this frame, in lines (positive = up); 0 if none, or when Ctrl+wheel zoom consumed the wheel (zoom is on by default)
float        rcScrollDeltaX(void);                     // Horizontal wheel delta this frame, in lines; 0 when the wheel went to zoom  → docs/api-notes.md ▸ rcScrollBy: the sign convention

// Keyboard  (poll key + modifier state anywhere in the layout/update callback; RC_Key names the key, RC_Mod the modifier)
bool         rcKeyDown(RC_Key key);                    // True while the key is held this frame (a level, independent of auto-repeat): for continuous input like hold-to-move
bool         rcKeyPressed(RC_Key key);                 // True on the frame the key goes down (a fresh edge; auto-repeat does NOT re-fire): for one-shot actions.
bool         rcKeyReleased(RC_Key key);                // True on the frame the key goes up
bool         rcModDown(RC_Mod mod);  // → docs/api-notes.md ▸ rcModDown and RC_MOD_PRIMARY

// Clipboard  (a read is request-then-poll, not a blocking pull)
const char        *rcClipboardGet(void);                     // Best-effort SYNC read: the answer if one is already in hand, else NULL.
void               rcClipboardSet(const char *text);         // Replace the clipboard text (a NUL-terminated copy is taken); fire-and-forget, synchronous everywhere
RC_ClipboardToken  rcClipboardRequest(void);                 // Ask for the text without waiting; returns the token naming THIS read (0 = none).
const char        *rcClipboardPoll(RC_ClipboardToken token); // Collect that read's answer exactly once: the text, or NULL if still pending/denied/already taken.
void               rcClipboardDeliver(RC_ClipboardToken token, const char *utf8);
void               rcSetClipboardImpl(const RC_ClipboardImpl *impl); // Install a clipboard backend (COPIED BY VALUE: a stack local is fine; NULL restores the platform default).  → docs/api-notes.md ▸ rcSetClipboardImpl

// Value + string helpers  (return RayClay value types, interchangeable with brace literals)
RC_Color   rcAlpha(RC_Color color, uint8_t alpha); // The same colour with a new alpha (0..255)
RC_Color   rcRgb(r, g, b) / rcRgba(r, g, b, a);      // Compile-time colour builders (0-255 channels): name a reusable palette shade at file scope, RayClay's theme.extend.colors
RC_Color   rcHex(uint32_t rgb);                      // An opaque colour from a 0xRRGGBB hex value, e.g. .bg = rcHex(0x1e293b)
RC_Color   rcColor(const char *css);                 // A colour from a CSS string, so it reads like inline CSS: .bg = rcColor("#1e293b").
void         rcStrCopy(char *dst, const char *src, size_t dstsize); // Safe always-NUL-terminating copy (pass the FULL buffer size); no <string.h> needed
```

## module: portability hooks, RESERVED (every one is a no-op on every target that ships today)

They exist so that adding a mobile or web backend later is an *implementation*, not an API break. RayClay
ships as source, so a hook retrofitted after a release either stops an app compiling or silently changes
its meaning; reserving the signature now costs one no-op. Safe to call, and safe to ignore until you ship
to a platform that needs them.

```c
void      rcSetAppEventHandler(RC_AppEventCallback handler, void *user); // Observe lifecycle events (NULL detaches).
RC_Insets rcGetSafeAreaInsets(void);                               // Logical-px insets of the region safe for interactive content (notch, rounded corner, home indicator).
void      rcSetImeCaretRect(float x, float y, float w, float h);   // Tell the platform where the caret is (logical px) so an IME can place its candidate window. No-op today on desktop AND web; reserved for a mobile backend
void      rcSetSoftKeyboardVisible(bool visible);                  // Raise/dismiss the on-screen keyboard. No-op today on desktop AND web; reserved for a mobile backend
// The set's other two members are already public elsewhere: rcGetContentScale (core) and rcSetClipboardImpl (interaction).
```

## module: theme, application-wide style (`RC_NO_STYLE` to drop)

```c
RC_Style        rcStyleDark(void);                 // The ready-made dark theme preset
RC_Style        rcStyleLight(void);                // The ready-made light theme preset
RC_Style        rcGetStyle(void);                  // Read the active theme by value (dark until you set one)
void            rcSetStyle(RC_Style style);        // Install the active application-wide theme
//   It does not reach the window behind your layout. A runtime theme switch must also call
//   rcAppSetClearColor(app, rcGetStyle().background), or the old background stays wherever your layout does not cover.
const RC_Style *rcGetStylePtr(void);               // Borrow a pointer to the active theme (no per-call copy)
```

## module: icons (procedural, zero-asset), L2 (`RC_NO_UI_HELPERS` drops it; the scaffolding otherwise rides along with `#include "rayclay.h"`)

```c
// Bundled titlebar artwork (the Flat Slab glyphs)
void rcIconTitlebarMinimize(float size, RC_Color color); // auto-included by rayclay.h on DESKTOP: nothing extra to add
void rcIconTitlebarMaximize(float size, RC_Color color);
void rcIconTitlebarClose(float size, RC_Color color);
//   Desktop-only, and not web no-ops like the widgets: rcTitlebar / rcWindowControls stay DECLARED on web,
//   but under __EMSCRIPTEN__ "icons/rc_icons.h" skips these three headers, so rayclay.h does not declare them
//   at all and a web build that names one FAILS TO COMPILE. (gcc -E -P: 3 declarations desktop, 0 on web.)
// Packaged extra  (opt-in: the logo is not compiled in until you include its generated header)
void rcIconRayClayLogo(float size);                    // The full-colour RayClay logo (bakes its own palette; no color arg)
//   The artwork is a GENERATED header, not part of rayclay.h. Building the examples tree, `#include "icons/rc_icons_rayclay_logo.h"` resolves already.
//   Vendoring rayclay.h into your own project: copy BOTH rc_icons_rayclay_logo.h AND rc_icons_common.h out of examples/assets/icons/, and include rayclay.h FIRST.
//   Both parts are load-bearing: the generated header includes rc_icons_common.h by name, and that file includes rayclay.h back, so logo-header-first
//   gives "implicit declaration of rcIconDrawRoundLine". Every symbol the artwork needs (rcIconDraw*) is already in the amalgamated header.

// Custom icon geometry  (you name these only if you hand-write an icon instead of generating one)
RC_IconPoint      // One point in the SVG's viewBox space; the library scales it to the element's bounds
RC_ICON_MAX_PTS   // Most points one generated polyline or polygon may carry (128)  → docs/api-notes.md ▸ RC_IconPoint

// A hand-written icon is TWO functions, and ex11 generates both for you: an RC_CustomDrawCallback named rcDrawIcon<Name> that draws the geometry, and an entry point rcIcon<Name>(size, color) whose whole body is one rcIconEmit call.
//   You call rcIcon<Name>. rcDrawIcon<Name> is the callback it passes - `static inline` beside it in the same generated header, so it is yours to name only if you want the artwork somewhere else.
void rcIconEmit(float size, RC_Color color, RC_CustomDrawCallback draw, const void *userData); // The entry point.
    //   Emits a fixed square element (size x size) whose payload runs `draw` during rcRender. Header-inline.
    //   `size` is multiplied by rcUnzoomedScale() FIRST, then clamped to >= 1.0
//   Every op below is called from INSIDE `draw`, never from your layout: `bounds` is the screen rect the callback is handed.
//   Geometry, radii and strokeWidth are all in the icon's own viewBox units. The scale applied is bounds.width / viewBoxSize - height never enters. Stroke ops draw a CENTRED stroke, as in SVG.
void rcIconDrawPolyline(RC_BoundingBox bounds, const RC_IconPoint *points, int pointCount, float viewBoxSize, float strokeWidth, bool closed, RC_Color color);
    //   Stroked path: round join at every vertex, round caps; `closed` also strokes last→first. Needs >= 2 points
void rcIconDrawFilledPolygon(RC_BoundingBox bounds, const RC_IconPoint *points, int pointCount, float viewBoxSize, RC_Color color); // Solid fill, convex OR concave (ear-clipped). Needs >= 3 points
void rcIconDrawFilledCircle(RC_BoundingBox bounds, float cx, float cy, float radius, float viewBoxSize, RC_Color color); // Solid disc
void rcIconDrawFilledEllipse(RC_BoundingBox bounds, float cx, float cy, float rx, float ry, float viewBoxSize, RC_Color color); // Solid AXIS-ALIGNED ellipse: rx and ry only, no rotation
void rcIconDrawRoundLine(RC_BoundingBox bounds, float x0, float y0, float x1, float y1, float viewBoxSize, float strokeWidth, RC_Color color); // One round-capped segment
void rcIconDrawCircleStroke(RC_BoundingBox bounds, float cx, float cy, float radius, float viewBoxSize, float strokeWidth, RC_Color color); // Stroked circle
void rcIconDrawRoundedRectStroke(RC_BoundingBox bounds, float x, float y, float width, float height, float radius, float viewBoxSize, float strokeWidth, RC_Color color); // Stroked rounded rect; radius 0 keeps mitred corners
//   Both `points` ops keep the first RC_ICON_MAX_PTS (128) points and warn once; the rest are not drawn. Below the minimum point count the call draws nothing.
```

### Your own artwork: use the SVG, and pick where its bytes live

**All four routes end in the same icon ops and the same drawing code**: `rcSvg` resolves the path
and calls `rcSvgHandle`, which calls `rcIconEmit`, exactly as a generated header does. So this is a
question about your BUILD, not about how it looks (measured: 0 of 17,161 pixels differ on a mono
icon).

```c
// 1. JUST DRAW IT. Point at the file, like an <img src>. Parsed once, cached, freed for you.
rcSvg("assets/logo.svg", 96.0f, s.text);         // no handle, no app state, no unload

// 2. SHIPPING ONE EXECUTABLE. Embed the markup: no converter, no asset files
static const char LOGO_SVG[] = "<svg viewBox='0 0 24 24' ...>...</svg>";
RC_Svg *logo = rcLoadSvgFromMemory(LOGO_SVG, (int)(sizeof LOGO_SVG - 1));   // once, at startup
rcSvgHandle(logo, 96.0f, s.text);                // per frame

// 3. OWN THE LIFETIME. Free on your schedule, or swap which handle is drawn
RC_Svg *owned = rcLoadSvg("assets/logo.svg");    // ... rcSvgHandle(owned, ...) ... rcUnloadSvg(&owned)

// 4. ADVANCED. A generated header: no parser in the binary, no parse at startup, cannot fail
rcIconRayClayLogo(24.0f);                        // generate one with ex11_rayclay_icon_converter
```

**Naming a path (`rcSvg("...")` or `rcLoadSvg("...")`) costs you the single-executable property**,
and nothing warns you: the artwork becomes a file on the end user's disk. That is route 1's trade and
usually the right one; when it is not, route 2 buys the property back for the price of a string
literal. What route 4 buys over route 2, all small but real: **86,080 B** of parser stays out of the
binary (measured on `ex24`, Release + `-DRC_SIZE_OPT=ON`, stripped; ex24 **calls `rcLoadSvg`**, which is
the arm measured), the artwork is compact point arrays rather than XML text, there is no 21–41 µs parse at
startup, and a baked icon **cannot fail to load**.
→ **docs/api-notes.md ▸ Loading an SVG at runtime** · worked side-by-side: `ex24_svg_live`

**The SVG's paint decides the signature.** An SVG whose every fill/stroke is `currentColor` generates
`rcIcon<Name>(float size, RC_Color color)`, an `RC_IconCallback`, re-tintable on every frame. Bake **one**
concrete colour into the SVG and the generated icon takes `(float size)` alone, because its palette is
then part of the artwork (that is why `rcIconRayClayLogo` has no colour argument). Icons are re-stroked
from their points every frame, so keep them icon-grade: a few hundred points, circles rather than
near-circular ellipses. `ex10_rayclay_widgets_gallery` shows both forms side by side and animates the
tintable one.

---

## structures

```c
// ── value types: the values the public API hands you ────────────────────────────────────────────
RC_Color              // struct { float r, g, b, a; }: 0..255 per channel, but FLOAT members (cast when you print or compare)   (returned by rcColor / rcHex / rcRgb / rcAlpha)
RC_String             // length + chars: carry the LENGTH; the type does not promise a terminator  (returned by rcFormat / rcStringFromCStr)  → docs/api-notes.md ▸ RC_String.isStaticallyAllocated · RC_String.length
RC_Dimensions         // width + height, in pixels  (returned by rcGetWindowDimensions / rcMeasureText)
RC_Vec2               // x + y, in pixels           (returned by rcPointer)
RC_BoundingBox        // x, y, width, height: the rect passed to RC_CustomDrawCallback and every icon drawer  → docs/api-notes.md ▸ The RC_ value types are transparent aliases

RC_App                // Opaque app/runner handle (L3)
RC_AppOptions         // The whole app in one struct. 0 = default throughout; new fields append at the tail  → docs/api-notes.md ▸ RC_AppOptions in depth  (every field, in full)
//   WINDOW     width/height/title, nativeFrame, titlebarHeight, minWidth/minHeight, clearColor
//   ICON       iconBytes/iconLength (embedded, WINS) or iconPath (read once at startup)
//   FONTS      fontPath/fontSizes/fontCount, fontOversample
//   CALLBACKS  layoutCallback (omit it and you get the welcome canvas + one warning), updateCallback (optional, runs BEFORE layout),
//              frameEndCallback (optional, RC_FrameEndCallback; runs LAST, after the overlay pass),
//              userData (passed to all three)
//   BEHAVIOUR  zoom, titlebar, autoCursorsDisabled, debugToggleKey, renderWhileMinimized,
//              renderMode (RC_RENDER_ON_DEMAND by default), maxFrames/maxSeconds (honoured by rcRunApp and by a split-loop rcRunFrame host alike)
//   maxFrames forces CONTINUOUS; maxSeconds is the only bound that leaves on-demand intact
RC_ZoomOptions        // Webapp-zoom config (RC_AppOptions.zoom)  → docs/api-notes.md ▸ RC_ZoomOptions
//       disabled · minZoom/maxZoom · ladder/ladderCount · step · wheelStep · mode · pan · bindPan
//       bindZoomIn/bindZoomOut/bindZoomReset · inDisabled/outDisabled/resetDisabled/wheelDisabled
RC_TitlebarOptions    // Bundled-titlebar config (RC_AppOptions.titlebar)  → docs/api-notes.md ▸ RC_TitlebarOptions
//       custom · controlsLeft · hideTitle/hideMinimize/hideMaximize · height · iconSize · title ·
//       background / titleColor · minimize/maximize/close (per-button RC_TitlebarButtonIcons) ·
//       zoomWithContent · allowControlsClip
//       height <= 0 means UNSET, not folded: it falls back to RC_AppOptions.titlebarHeight, then to 38. Runtime folding is rcAppSetTitlebarHeight(app, 0).
//       Fold to a small POSITIVE height, or draw your own RC_ID_WINDOW_* controls. With .titlebar.custom the band is yours and only the OS strip moves.
//       Padding specificity on RC_ComponentOptions: pt/pb/pl/pr > px/py > p
RC_ClipboardImpl      // Pluggable clipboard backend (copied by value): set(user,utf8) = copy; request(user,token) = paste, arrange rcClipboardDeliver(token,…) later; user (borrowed)
RC_ClipboardToken     // uint32_t naming one clipboard read (0 = none); echo it back through rcClipboardDeliver so a late answer can't be mistaken for a newer read
RC_Insets             // Safe-area insets in logical px: top, right, bottom, left
RC_TitlebarButtonIcons// One control's normal/hover/press RC_IconCallback overrides (a NULL state falls back to normal; all-NULL = the bundled artwork)
RC_ComponentOptions   // Fields behind rcBox/rcRow/rcColumn  → docs/api-notes.md ▸ RC_ComponentOptions  (.overlay, and the CSS-string trap)
//       border · gradient · shadow · floating · image · overlay · tooltip
RC_TextOptions        // Text styling  → docs/api-notes.md ▸ RC_TextOptions
//       font · color · size · lineHeight · letterSpacing · wrap · textAlign
RC_TextInputOptions   // Text-input options: placeholder, font, password, rows, multiline
RC_ModalOptions       // Popup options, TWO INDEPENDENT axes: modality (does the app behind stay live?) + noBackdropDismiss (0 = a scrim click closes it; set it to require Esc or an explicit action)
//   A panel the user can leave open needs BOTH: RC_MODALITY_NON_MODAL *plus* noBackdropDismiss - without the second, the first click outside still closes it. See docs/api-notes.md, rcIsModalOpen / RC_ModalOptions
RC_Modality           // RC_MODALITY_MODAL (0, default: a scrim makes the app behind inert) / RC_MODALITY_NON_MODAL (no scrim: the app behind stays live and clickable)
RC_SplitOptions
//     Split-pane tuning (rcBeginSplitPane): minFraction/maxFraction clamp pane-1's share (0 = 0.05/0.95),
//     handleThickness = divider thickness in px (0 = 6). Zero-init = a 6px handle
RC_TableColumn        // One rcBeginTable column
//     header (NULL/"" = none; ALL none = no header row) · wType (typed width; zero-init = GROW) · w (CSS string, used when wType is unset) · align ("<Y><X>", {0} = top-left)
RC_TableOptions       // rcBeginTable options: cellPadding is an RC_OptFloat - zero-init = the house 6 px, RC_VAL(0) = flush, RC_VAL(n) = n (bounded 255).
RC_OptFloat / RC_VAL(x)   // "Unset" vs "zero", for option fields where 0 is a value a caller can legitimately want  → docs/api-notes.md ▸ RC_OptFloat / RC_VAL
RC_Series             // One chart dataset
RC_Axis               // One chart axis  → docs/api-notes.md ▸ RC_Series / RC_Axis
RC_ChartOptions       // rcChart options: x / y (left) / y2 (right, used when a series sets .axis=1) RC_Axis blocks · legend · fontSize (0 => 12)
//     hover: tooltip (0 => none), tooltipPlace, tooltipAnchor, tooltipOffset, hoverGuide, hoverMarkers. See docs/api-notes.md, Chart hover
RC_VirtualRow         // The rcVirtualList loop variable  → docs/api-notes.md ▸ RC_VirtualRow  (the first-frame window, and its TWO distinct diagnostics)
//       index · first · last · count · rowHeight · containerId · found
RC_Box                // What rcGetElementBox / rcChartPlotRect return  → docs/api-notes.md ▸ RC_Box
//       x · y (CONTENT-space top-left, directly comparable to rcPointer()) · width · height · found
RC_ScrollInfo         // What rcGetScrollInfo returns  → docs/api-notes.md ▸ RC_ScrollInfo
//       offsetX/offsetY · maxOffsetX/maxOffsetY · found.  DOM convention: positive-DOWN, like element.scrollTop
RC_SparklineOptions   // rcSparkline options: kind (LINE default | BAR | AREA), color ({0} => theme primary), min/max (both 0 => auto-fit this frame), thickness (0 => 1.5)
RC_Border             // Border colour + width; .width is a CSS string ("1"/"1px" or "all-1"/"all-2px"); the string form is ALL-SIDES; per-side widths via rcBeginComponent  → docs/for-web-developers.md
RC_Gradient           // Two-stop linear gradient fill (from, to, dir "v"/"h"/"d"/"u"). REQUIRES the element's .id; <=64 gradients/frame (over-cap warns once, extras drop)
RC_Shadow             // CSS-style drop shadow (color, x, y, blur, spread); linear (not Gaussian) falloff.
RC_Float              // Floating placement  → docs/api-notes.md ▸ RC_Float
//       to · toId · parent/element anchors · offset · zIndex · capture
RC_Size               // Typed sizing value (mode + value); build with RC_FIT/RC_GROW/RC_PX/RC_PCT (parent-relative) or RC_VW/RC_VH (viewport-relative)
RC_Image              // Opaque image handle: handle, width, height
RC_Svg                // Opaque parsed-SVG handle; right-sized to the artwork (a Lucide icon packs to ~288 B)
RC_Arena              // Bump allocator: buffer, bufferLength, currOffset
RC_CustomDrawData     // Payload for a custom-draw element (magic, draw fn, userData, color)
RC_Style              // Application-wide semantic theme, read by value with rcGetStyle()  → docs/api-notes.md ▸ RC_Style
//       COLOURS  background · surface · surfaceAlt · chrome · text · textMuted · primary · primaryHover ·
//                danger · dangerHover · success · successHover · warning · warningHover · border
//       METRICS  radius · padding · gap
```

## callbacks

```c
typedef void (*RC_UpdateCallback)(RC_App *app, void *userData);                         // Per-frame update hook (runs before layout)
typedef void (*RC_LayoutCallback)(RC_App *app, void *userData);                         // Per-frame layout hook (declares the frame's elements)
typedef void (*RC_FrameEndCallback)(RC_App *app, void *userData);                       // Runs LAST on a DRAWN frame, after the overlay pass: the read point for rcAppPerfFrame()
typedef void (*RC_CustomDrawCallback)(RC_BoundingBox bounds, RC_Color color, const void *userData);   // Draw callback for a CUSTOM element
typedef void (*RC_IconCallback)(float size, RC_Color color);
typedef void (*RC_AppEventCallback)(RC_AppEvent event, void *user);                     // Lifecycle-event observer (RESERVED: no backend emits these, desktop or web; registering one is a harmless no-op)
typedef void (*RC_LogCallback)(RC_LogLevel level, const char *msg, void *user);          // Log sink for rcSetLogSink; msg is the bare single-line body (no prefix/newline), user is passed through verbatim
```

## enumerations

```c
RC_FontWeight     // RC_WEIGHT_THIN(100)  EXTRALIGHT  LIGHT  REGULAR(400)  MEDIUM  SEMIBOLD  BOLD(700)  EXTRABOLD  BLACK(900). Every one is RC_WEIGHT_<NAME>
RC_ButtonVariant  // RC_BTN_DEFAULT  RC_BTN_PRIMARY  RC_BTN_DANGER  RC_BTN_GHOST
RC_SizeMode       // RC_SIZE_UNSET  RC_SIZE_FIT  RC_SIZE_GROW  RC_SIZE_FIXED  RC_SIZE_PERCENT (of parent)  RC_SIZE_VW  RC_SIZE_VH (of viewport)
RC_SplitAxis      // RC_SPLIT_ROW (0: panes side-by-side, a vertical divider)  RC_SPLIT_COLUMN (panes stacked, a horizontal divider)   (rcBeginSplitPane axis)
RC_SeriesKind     // RC_SERIES_LINE(0)  RC_SERIES_BAR (from the zero baseline)  RC_SERIES_AREA (line + translucent fill)  RC_SERIES_SCATTER (markers only)   (RC_Series.kind; one chart may MIX kinds)
RC_ChartTooltip
//     RC_CHART_TOOLTIP_NONE (0: no readout)  RC_CHART_TOOLTIP_NEAREST (readout of the nearest point on hover)
RC_ChartTooltipPlace
//     RC_TOOLTIP_PLACE_CURSOR (0: follows the pointer)  RC_TOOLTIP_PLACE_CORNER  RC_TOOLTIP_PLACE_FIXED. See docs/api-notes.md, Chart hover
RC_AttachTo       // RC_ATTACH_NONE  RC_ATTACH_PARENT  RC_ATTACH_ELEMENT  RC_ATTACH_ROOT   (floating anchor target)
RC_Anchor         // The nine box anchor points, named <Y>_<X>: RC_ANCHOR_TOP_LEFT  TOP_CENTER  TOP_RIGHT / CENTER_LEFT  RC_ANCHOR_CENTER  CENTER_RIGHT / BOTTOM_LEFT  BOTTOM_CENTER  BOTTOM_RIGHT.
RC_Capture        // RC_CAPTURE_DEFAULT  RC_CAPTURE_ON  RC_CAPTURE_PASSTHROUGH   (floating pointer capture)
RC_WindowControl  // RC_WINCTL_MINIMIZE  RC_WINCTL_MAXIMIZE  RC_WINCTL_CLOSE   (which OS action rcWindowControlButton performs)
RC_ZoomMode       // RC_ZOOM_LAYOUT (default: reflow, web-like)  RC_ZOOM_OPTICAL (magnify, canvas-like)   (RC_ZoomOptions.mode = initial; switch live via rcAppSetZoomMode)
RC_RenderMode     // RC_RENDER_ON_DEMAND(0, the default: draw on events/deadlines, else sleep)  RC_RENDER_CONTINUOUS(draw every vsync: games, sims, benches)  → docs/api-notes.md ▸ RC_RenderMode
RC_Cursor         // RC_CURSOR_DEFAULT/POINTER/TEXT/GRAB/GRABBING/NOT_ALLOWED: the shape RayClay maps per component (CSS `cursor:` names); override this frame with rcSetCursor
RC_Key            // 119 tokens, all spelled RC_KEY_<NAME>: A..Z  0..9  F1..F24  UP/DOWN/LEFT/RIGHT
                  //   ENTER/ESCAPE/TAB/SPACE/BACKSPACE/DELETE/INSERT  HOME/END/PAGE_UP/PAGE_DOWN
                  //   punctuation COMMA/PERIOD/SEMICOLON/APOSTROPHE/GRAVE/MINUS/EQUAL/SLASH/BACKSLASH
                  //   LEFT_BRACKET/RIGHT_BRACKET  keypad RC_KEY_KP_*  locks CAPS_/NUM_/SCROLL_LOCK
                  //   modifiers LEFT_/RIGHT_ SHIFT/CTRL/ALT/SUPER (CTRL, not CONTROL)
                  //   MENU/PAUSE/PRINT_SCREEN
//   RC_KEY_NONE is the "no key" sentinel; that is how you disable RC_AppOptions.debugToggleKey or a zoom bind. RC_KEY_COUNT bounds the space.
RC_AppEvent       // RC_APP_EVENT_CREATED/SUSPENDED/RESUMED/CONTEXT_LOST/CONTEXT_RESTORED/CONTENT_SCALE_CHANGED/TERMINATING   (RESERVED: NO backend emits these in v1.0, desktop or web)
RC_Mod            // RC_MOD_PRIMARY  RC_MOD_SHIFT  RC_MOD_ALT  RC_MOD_CTRL  RC_MOD_SUPER   (query with rcModDown)
//   RC_MOD_PRIMARY is the accelerator key: Cmd on macOS, Ctrl everywhere else and on the web - and Ctrl+Alt does not count, so Windows AltGr never fires it
RC_PointerButton    // RC_POINTER_LEFT(0)  RC_POINTER_RIGHT(1)  RC_POINTER_MIDDLE(2)  → docs/api-notes.md ▸ RC_PointerButton
RC_LogLevel       // RC_LOG_INFO(0)  RC_LOG_WARNING(1)  RC_LOG_ERROR(2)   (severity handed to an rcSetLogSink sink; the numeric values are ABI-stable)
```

## titlebar control ids  (the contract underneath every titlebar, and the bundled default bar tags them for you)

```c
RC_ID_WINDOW_MINIMIZE   // "RC_Window_Minimize":  click this element to minimize
RC_ID_WINDOW_MAXIMIZE   // "RC_Window_Maximize":  click to maximize / restore
RC_ID_WINDOW_CLOSE      // "RC_Window_Close":     click to close the window
RC_ID_WINDOW_DRAG       // "RC_Window_Drag":      click-drag this region to move the window
RC_ID_WINDOW_NODRAG     // "RC_Window_NoDrag":    this subtree opts OUT of the drag: ordinary client area, so widgets inside it click normally
```

With `RC_AppOptions.titlebar.custom` set, tag the drag region yourself; for the buttons,
`rcWindowControls()` (or `rcWindowControlButton`, see the app module) draws and wires the
min/max/close cluster for you; these ids are the contract underneath. Without `.custom`, the
runner's bundled bar (see `rcTitlebar`) tags them for you. Titlebars are desktop-only: on
web/mobile all five ids are inert (no OS action, close included).

**A press anywhere in the drag band starts an OS window-move**, so an interactive widget placed
visually inside the band (a theme toggle, a "new" button, a search field) would lose its click to
the drag **on desktop** (and appear to work fine on web, where there is no window drag). Wrap the
widget cluster in an element tagged `RC_ID_WINDOW_NODRAG`: the drag hit-test then treats that
sub-region as ordinary client area, exactly as the built-in window-control buttons already are.
This is the desktop mirror of CSS `-webkit-app-region: no-drag`.

```c
rcRow(.id = RC_ID_WINDOW_DRAG, .w = "grow", .h = "56", .px = 14, .gap = 12, .align = "cl") {
    rcTextL("RayClay", .font = F_HEAD);                /* the whole band drags... */
    rcBox(.w = "grow") {}                              /* spacer: still draggable */
    rcRow(.id = RC_ID_WINDOW_NODRAG, .gap = 8, .align = "cl") {   /* ...except this cluster */
        rcTextC(dark ? "Dark" : "Light", .font = F_SMALL);
        rcToggle("tg_theme", &dark);                   /* clicks normally on desktop */
    }
    rcWindowControls();                                /* already exempt by contract */
}
```

The alternative, and what the bench apps use, is to **restructure** so the drag id only ever wraps
non-interactive content, with the widgets as siblings outside it. Both are correct; `RC_ID_WINDOW_NODRAG`
is the one to reach for when the widgets must sit *inside* a band you want draggable end to end.

## constants

```c
// Version (semantic versioning; MINOR bump requires explicit owner approval)
RC_VERSION_MAJOR   // integer major version
RC_VERSION_MINOR   // integer minor version
RC_VERSION_PATCH   // integer patch version
RC_VERSION         // version string, always "MAJOR.MINOR.PATCH"
// These four macros are the ONLY authority on the version; this page is a snapshot

// Custom-draw sentinel (set RC_CustomDrawData.magic to this before handing it to the layout engine)
RC_CUSTOM_DRAW_MAGIC   // 0x52434457u ('RCDW'), validated by rcRender to distinguish RC payloads from other CUSTOM data
```

## colors: Tailwind-style palette (`RC_NO_COLOR_PALETTE` to drop)

**22 of Tailwind's 26** named families × 11 shades (50–950), carrying **Tailwind v3's sRGB values**.

Two honest caveats, because a palette is the first thing an evaluator diffs:

- **Not the complete current palette.** Tailwind v4.2 added `taupe`, `mauve`, `mist` and `olive`; RayClay
  ships none of them.
- **Same name ≠ same value as current Tailwind.** v4 re-based the palette onto `oklch`, so `RC_SLATE_500`
  carries v3's sRGB value and will *not* byte-match today's `slate-500`, most visibly on a wide-gamut (P3)
  display. That is a pinned-provenance decision, not a bug: the constants are stable and will not shift
  under you when Tailwind ships a new version. If you need exact v4 values, define them yourself below.

Define your own brand shades with the pattern below.

```c
// Or write any colour as a CSS string: rcColor("#1e293b") / rcColor("rgb(30,41,59)") / rcColor("red")
// Named
RC_BLACK   RC_WHITE   RC_TRANSPARENT

// Scale:  RC_<FAMILY>_<shade>,  shade in { 50 100 200 300 400 500 600 700 800 900 950 }
// Families (all 22, in Tailwind's own order, with the five greys first):
RC_SLATE  RC_GRAY   RC_ZINC    RC_NEUTRAL RC_STONE   RC_RED     RC_ORANGE
RC_AMBER  RC_YELLOW RC_LIME    RC_GREEN   RC_EMERALD RC_TEAL    RC_CYAN
RC_SKY    RC_BLUE   RC_INDIGO  RC_VIOLET  RC_PURPLE  RC_FUCHSIA RC_PINK    RC_ROSE  // → docs/api-notes.md ▸ Colour builders are compound literals
```

## colors: your own palette (Tailwind `theme.extend.colors`, RayClay-style)

Define brand tokens the same way Tailwind's `theme.extend.colors` does: one name per shade, in plain C. No registry, no build step; the palette is just constants:

```c
// The clean public builders: no per-frame cost, ideal inside a layout callback.
// Name a file-scope token with #define, not `static const` - these expand to COMPOUND LITERALS
#define BRAND_500  rcRgb(99, 102, 241)
#define BRAND_700  rcRgb(67, 56, 202)
#define BRAND_A20  rcRgba(99, 102, 241, 51)    // with alpha, 0–255

// Inside a function the same expressions are ordinary initialisers:
RC_Color accent = BRAND_500;
// A raw brace literal IS a constant expression, so this file-scope form is portable:
static const RC_Color SLATE_800 = { 30, 41, 59, 255 };
#define ACCENT_400  rcHex(0x22d3ee)      // cheap inline hex → colour, no string parse
#define ACCENT_500  rcColor("#06b6d4")   // a CSS string: readable, but re-parsed each frame it runs

rcColumn(.bg = BRAND_700, .p = 16, .gap = 8) {
    rcBox(.bg = BRAND_500, .h = "40px", .borderRadius = "-md");
    rcBox(.bg = ACCENT_400, .h = "40px", .borderRadius = "-md");
}
```

Rule of thumb: a compile-time constant (`rcRgb` / `rcRgba`, a struct literal, or `rcHex`) costs nothing per frame;
`rcColor("#..")` parses the string every frame it is evaluated, so prefer the constant forms inside a layout
callback, and keep `rcColor()` for one-off literals.

## shipping on Windows: stop the console window appearing behind your app

A RayClay app built the ordinary way is a **CONSOLE** binary, so Windows opens a terminal alongside it.
Nothing in RayClay causes it; it is the linker's default subsystem.
→ **`docs/api-notes.md` ▸ Shipping on Windows: the console window**, the MSVC and MinGW flags, and why
`WIN32_EXECUTABLE` alone fails to link on MSVC.

## build-time configuration  (define before/at compile)

**WHERE you define a knob decides whether it does anything, and getting it wrong is SILENT.**

**ONE RULE COVERS EVERY KNOB BUT THREE: define it for EVERY translation unit**, in your build
system's compile definitions (`target_compile_definitions`, `CMAKE_C_FLAGS`, `-D` on the command
line), **not** as a `#define` above one `#include`. Most knobs are read only where the implementation
is compiled, and the few your own code also reads must agree with it.

**THE THREE EXCEPTIONS TRIM THE CONSUMER VIEW AND MUST NOT REACH THE IMPLEMENTATION**, which needs
the declarations in full: **`RC_NO_UI_HELPERS`**, **`RC_NO_STYLE`**, **`RC_NO_COLOR_PALETTE`**, tagged
**[CONSUMER-VIEW]** below. **This is the one class you cannot get wrong quietly**: the amalgamated
single header refuses the combination with an `#error` naming the knob.
In a multi-TU source build there is no `RAYCLAY_IMPLEMENTATION` at all, so that `#error` cannot fire
and the mistake is silent again. Follow the rule; do not rely on the diagnostic.

**[both]** marks a knob read on BOTH sides, so a value set on one side only makes your code and the
library disagree **with no diagnostic at all**. `RC_CHART_MAX_SERIES` is the case to know: size an
array against one cap while the library clamps at another and you get a silently truncated plot.

`RC_BUILD_SHARED` and `RC_USE_SHARED` are the one pair that is per-BUILD rather than per-TU: the
first belongs to the build that COMPILES RayClay, the second to the build that CONSUMES it.
**`cmake -DRC_SIZE_OPT=ON` is a CMake cache option, not a compile-time `#define`**: no source file
tests it. It selects an aggressive size posture (LTO + section GC + static stb_truetype).
→ **`docs/api-notes.md` ▸ RC_SIZE_OPT**

→ **`docs/api-notes.md` ▸ Build knobs: where to define them**, the single-header vs CMake sites, and
the stb instinct that silently does nothing.
→ **`docs/api-notes.md` ▸ Selecting the alternate renderer**: why `-DRC_GFX_PACKET=0` is
ignored on the drop, and how to read the arm back out of the built library.

```c
// -- DROPPING WHOLE LAYERS
RC_NO_UI_HELPERS      // Drop L2: the element DSL, the widgets, AND the whole icons module (rcIconEmit, the
                      //   7 rcIconDraw* helpers, RC_IconPoint/RC_ICON_MAX_PTS, the bundled titlebar glyphs).
                      //   It does NOT drop the palette or RC_Style; those are the two knobs below
                      //   [CONSUMER-VIEW]
RC_NO_STYLE           // Drop the RC_Style theme layer                        [CONSUMER-VIEW]
RC_NO_COLOR_PALETTE   // Drop the Tailwind palette (also drops the theme)     [CONSUMER-VIEW]
RC_NO_APP_RUNNER      // Drop L3: the app-loop runner (rcRunApp / rcRunFrame)  → docs/api-notes.md ▸ RC_NO_APP_RUNNER
RC_NO_IMAGE           // Compile out image loading (rcLoadImage* become stubs; drops stb_image)
RC_NO_SVG             // Compile out the SVG parser; ALL FIVE public SVG symbols stay, so you need no #ifdef
RC_NO_BUNDLED_FONT    // Drop the bundled Latin-1 Roboto subset (a fontless build renders no text)
RC_NO_DEFAULT_APP_ICON // Drop the bundled taskbar/dock icon (23,898 B of embedded image data)  → docs/api-notes.md ▸ RC_NO_DEFAULT_APP_ICON
RC_NO_LIVE_RESIZE     // Drop the hook that repaints DURING the OS modal resize loop  → docs/api-notes.md ▸ RC_NO_LIVE_RESIZE

// -- RENDERER
RC_GFX_PACKET         // 1 (default) = the packet renderer; 0 = the alternate renderer. On the
                      //   single-header DROP `cmake -DRC_GFX_PACKET=0` is an unread cache variable and
                      //   does NOTHING  → docs/api-notes.md ▸ Selecting the alternate renderer
RC_GFX_TEXT_CACHE     // 0 (default) | 1: cache text-run quads. Faster text, NOT pixel-exact
RC_GFX_TEXT_CACHE_KB  // Cache size, default 64 (range 4..4096); the default holds one screen of text  → docs/api-notes.md ▸ RC_GFX_TEXT_CACHE
RC_FLATNESS_TOL       // Max distance in PHYSICAL px a drawn curve may sit from the true curve (default 0.25)  → docs/api-notes.md ▸ RC_FLATNESS_TOL

// -- THE MEMORY DIALS. See docs/api-notes.md, Memory: how much of your app is actually RayClay?
RC_GFX_MSAA_SAMPLES   // Anti-aliasing sample count; supported values 1, 2 or 4 (default 4).
                      //   Nothing rejects another value at build time; RAYCLAY_MSAA does.
                      //   A REQUEST, not a setting: see the read-back note below.
                      //   → docs/api-notes.md ▸ RC_GFX_MSAA_SAMPLES
RC_DEFAULT_START_LAYOUT_ELEMENTS  // Compile-time default for RC_AppOptions.startLayoutElements (2048, ~1.4 MiB)
RC_DEFAULT_MAX_LAYOUT_ELEMENTS    // Compile-time default for RC_AppOptions.maxLayoutElements (65536)  → docs/api-notes.md ▸ RC_DEFAULT_START_LAYOUT_ELEMENTS / RC_DEFAULT_MAX_LAYOUT_ELEMENTS
RC_GFX_PACKET_MAX_VERTICES // THE DEFAULT RENDERER'S vertex arena: 98304 x 16 B = 1,572,864 B, held on the host AND requested as a GPU vertex buffer
RC_GFX_PACKET_MAX_INDICES  // Its index arena. DERIVED: RC_GFX_PACKET_MAX_VERTICES * 3 = 294912 x 2 B = 589,824 B, on the same two ledgers. 3:1 is the worst ratio the encoder emits, so leave it derived
RC_GFX_PACKET_MAX_SPANS    // Draw-state spans (pipeline/texture/scissor change) one frame may hold; 2048 x 24 B = 49,152 B, host only
//   Past ANY of those three the packet renderer refuses the WHOLE frame - nothing is drawn rather
//   than a clipped scene - and logs the reason ONCE per process. Not every refusal names a knob you
//   can raise: one primitive wanting more than 65,536 unique vertices is a portable-uint16 index
//   bound with no dial.
//   Together ~2,162,688 B requested - larger than the glyph atlas, and the line item to look at first.
RC_SGL_MAX_VERTICES  // ALTERNATE RENDERER ONLY. INERT on a stock build: `sgl_setup` sits in the
                     //   #else arm of `#if RC_GFX_PACKET` and the default is 1 (nm -u: 28 undefined
                     //   sgl_ symbols at 0, ZERO at 1). DO NOT TUNE THIS TO SAVE MEMORY
                     //   (default 256*1024)  → docs/api-notes.md ▸ RC_SGL_MAX_VERTICES
RC_SGL_MAX_COMMANDS  // ALTERNATE RENDERER ONLY. INERT on a stock build, same as above. Draw-state command pool (default 16*1024)  → docs/api-notes.md ▸ RC_SGL_MAX_COMMANDS
RC_ICON_POOL_CAPACITY // GROWTH STEP of the process-wide icon-payload pool (default 256), not a ceiling; the pool grows on demand. Leave it alone.  → docs/api-notes.md ▸ RC_ICON_POOL_CAPACITY
RC_SVG_CACHE_MAX      // Distinct paths rcSvg() will hold (default 64). NON-EVICTING: past it, warns once and draws nothing  → docs/api-notes.md ▸ RC_SVG_CACHE_MAX
RC_FONT_ATLAS_W / _H  // Glyph-atlas dimensions in px (default 1024; power-of-two, 256 … 32768)  → docs/api-notes.md ▸ RC_FONT_ATLAS_W
RC_MAX_FONTS          // The 16-slot font-ladder cap (one slot per family/weight/size)
RC_FONT_LAST_CODEPOINT // Highest codepoint the glyph TABLE covers (default 255; accepted 160 … 0x10FFFF,
                       //   outside REFUSES the build). IT ONLY GOES UP; cost is LINEAR IN THE CAP, not in
                       //   what you use; the bundled font is Latin-1 only, so raising it alone shows nothing
                       //   → docs/api-notes.md ▸ RC_FONT_LAST_CODEPOINT: widening the baked range
RC_CHART_MAX_SERIES   // Series per chart, default 16. Tunable 1..16; outside that range is a hard #error  [both]  → docs/api-notes.md ▸ RC_CHART_MAX_SERIES
RC_GRADIENT_MAX       // DISTINCT elements per frame that may carry a .gradient (64). NOT tunable - a
                      //   static .bss table. Past it the element draws its flat .bg instead, and the
                      //   library warns ONCE PER PROCESS, so frame 0 logs and every later frame is
                      //   silently wrong. READ IT TO DESIGN AGAINST IT  → docs/api-notes.md ▸ Styling caps
RC_SHADOW_MAX         // Same shape for .shadow (64): past the cap the shadow is simply not drawn

// -- INSTRUMENTS + LINKAGE
RC_EDIT_BLINK_TIMEOUT // Seconds a focused caret blinks before it SETTLES (default 10.0)  → docs/api-notes.md ▸ RC_EDIT_BLINK_TIMEOUT
RC_GFX_DIGEST         // 1 = log which frames actually drew: how to find an accidental continuous redraw
RC_PERF_COUNTERS      // 1 = unlock rcAppPerfFrame(app), the one public fn not compiled in by default   → docs/api-notes.md ▸ RC_PERF_COUNTERS
RC_PERF_PHASES        // 1 = time the 8 STARTUP phases (entry -> first present) and print ONE report at exit. Separate facility from RC_PERF_COUNTERS, not per-frame, and independent of it
RC_DEBUG_TOOLS        // The built-in layout inspector: OUT (0, THE DEFAULT) or IN (1)  [both]  → docs/api-notes.md ▸ RC_DEBUG_TOOLS
RC_BUILD_SHARED       // Define when COMPILING RayClay into a shared library (RC_API -> dllexport)
RC_USE_SHARED         // Define when CONSUMING RayClay as a shared library (RC_API -> dllimport)  → docs/api-notes.md ▸ RC_BUILD_SHARED / RC_USE_SHARED
```

---

*RayClay is released under the MIT License. Layout by [Clay](https://github.com/nicbarker/clay)
(zlib/libpng, © Nic Barker). The bundled default font is a Latin-1 subset of Roboto, © 2011 The Roboto
Project Authors, under the [SIL Open Font License 1.1](https://openfontlicense.org), so if you redistribute
a RayClay binary you are redistributing that font too, and the OFL asks that its notice travel with each
copy. Build with `RC_NO_BUNDLED_FONT` and supply your own face if you would rather not. This
quick-reference mirrors the style of the
[raylib cheatsheet](https://www.raylib.com/cheatsheet/cheatsheet.html).*
