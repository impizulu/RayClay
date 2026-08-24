# Interactive widgets: patterns and recipes

> Companion to the [cheatsheet](cheatsheet.md). Covers the stateful widgets:
> text fields and text areas, menus, context menus, split panes, and modal
> dialogs, that need more explanation than a one-line signature provides.
>
> Every widget here is **input-driven**, so it works under RayClay's default
> on-demand rendering with no extra code. If a widget's data changes on its own
> (a chart fed by a socket, a log appended by a worker thread), you must ask for the
> redraw yourself; see
> [Idle CPU](getting-started.md#idle-cpu-rayclay-draws-only-when-something-happens).

---

## Cursors: you almost never set them

RayClay picks the expected cursor per component out of the box: a button gets the pointer, a draggable
gets grab/grabbing, a text field gets the I-beam. **`rcClicked(id)` (or `rcPressed(id)`) extends that
to your own elements:**
polling it gives the element the web's clickable-hand, so while the pointer is over it the frame's cursor
defaults to `RC_CURSOR_POINTER`. Any styled `rcBox` you turn into a button therefore behaves like one
without a line of cursor code:

```c
rcBox(.id = "card", .bg = s.surface, .p = 12) { rcTextL("Open"); }
if (rcClicked("card")) open_thing();          /* hovering it already shows the hand */
```

Two escape hatches, for the cases where the default is wrong:

```c
/* 1. Per element: a clickable surface that should NOT look like a button:
      a click-to-dismiss backdrop, a whole-row hit target. An explicit set always wins. */
if (rcClicked("backdrop")) close_popup();
rcSetCursor(RC_CURSOR_DEFAULT);               /* AFTER the poll */

/* 2. App-wide: take over cursors entirely. rcSetCursor still works. */
RC_AppOptions opts = { .autoCursorsDisabled = true, /* ... */ };
```

`rcSetCursor` is last-writer-wins for the frame, which is why the override goes *after* the poll.
Cursor state never reaches the draw stream, so adding or removing these calls cannot change a rendered
frame, only what the pointer looks like.

---

## Buttons: which edge fires, and how to opt out

A click in RayClay is **press-then-release over the same element**, exactly as it is in a browser and
in every desktop toolkit. That is not a detail; it is the escape hatch users rely on without ever
being taught it:

- **`rcClicked(id)`**: the **release** edge. Press it, keep the button held, slide the pointer *off*,
  release: **nothing fires.** This is the default, and it is almost always what you want. `rcButton`,
  and every other bundled widget that activates, is on this edge too.
- **`rcPressed(id)`**: the **press** edge, for the rare control where the wait is the problem: a
  nudge, a step, a jump. It fires the instant the button goes down.
  > **An eager activation cannot be cancelled**: there is no release left to withhold. Think hard
  > before putting a *destructive* action on it: a mis-aimed press-edge **Delete** has no undo gesture.
- **`rcPressed` fires once per physical press; it does not auto-repeat.** A hold-to-repeat control
  (a scrollbar arrow that keeps stepping while held) is still `rcPointerDown` + `rcIsHovered` + your
  own timer. The two are not interchangeable.

Both predicates give the element the clickable-hand cursor while it is hovered, and both take the same
escape hatches as the section above.

No bundled widget offers an eager **activation**, and none takes a flag to ask for one. If you want a
button that fires on press, the route is a styled `rcBox` polled with `rcPressed`, the same way you
would build any other custom button.

> **A drag is a different thing, and it does start on press**: `rcSlider`'s handle and
> `rcScrollbar`'s thumb both grab on the press edge, because a gesture you have to *release* to begin
> would not be a drag at all. Dismissing a menu or a combo works on press for the same reason. That is
> about **grabbing**, not about activating, and it is why the release-edge rule above is stated for
> things that *activate*.

---

## Keyboard: shortcuts and held keys

Poll key and modifier state directly; there is no event handler to register. Every query reads
*this frame*, so call them from your layout or update callback:

- **`rcKeyDown(key)`**: held right now (a level, independent of OS auto-repeat). Read it every
  frame for *continuous* input: hold-to-move, a camera pan, a charging action.
- **`rcKeyPressed(key)`**: the frame the key *goes down* (a fresh edge; auto-repeat does **not**
  re-fire it). Use it for *one-shot* actions: jump, toggle, "next".
- **`rcKeyReleased(key)`**: the frame it goes up.
- **`rcModDown(mod)`**: a modifier level. For app shortcuts always reach for `RC_MOD_PRIMARY`:
  it is **Cmd on a macOS desktop build and Ctrl everywhere else**, so one line lands on the right key
  without a per-platform branch (and Windows AltGr, which presents as Ctrl+Alt, can never fire it). Use
  `RC_MOD_SHIFT` / `RC_MOD_ALT` / `RC_MOD_CTRL` / `RC_MOD_SUPER` when you need a specific physical modifier.
  > **On the web, `RC_MOD_PRIMARY` is Ctrl, even in a browser on a Mac.** The choice is made at *compile*
  > time (`__APPLE__`), and the web build is compiled by emscripten, which does not define it. So a Mac user
  > running your **desktop** build presses Cmd+S, while the same source served as a **web page** wants Ctrl+S.
  > If that matters to your app, say so in your UI hint rather than assuming Cmd; a runtime user-agent split
  > is not in v1.0.

Keys are named with the backend-neutral `RC_Key` enum (`RC_KEY_W`, `RC_KEY_SPACE`, `RC_KEY_LEFT`, …);
never hardcode an integer: the value is only meaningful within a single build.

### A letter key means the letter the user *sees*

`RC_KEY_Z` is **the key labelled Z on the user's keyboard**, not a fixed position on the
board. So `RC_MOD_PRIMARY + RC_KEY_Z` is undo for everybody, including French AZERTY and
German QWERTZ, where that letter sits somewhere else entirely. Non-letter keys
(punctuation, arrows, function keys, space) are **positional**, and a non-Latin layout
(Cyrillic, Greek) falls back to positional too, since no key is labelled with a Latin
letter to match. This is the same hybrid rule browsers and Qt use, and it is what makes
one source agree about `RC_KEY_Z` on desktop *and* web.

**The corollary, if you are writing a game:** that rule is right for *shortcuts* and
wrong for *movement*. WASD is a **shape**: players pick those keys because of where they
sit under the left hand, not because of their letters. An AZERTY player's movement cluster
is **ZQSD**: the very same physical keys. Ask for `RC_KEY_W` and they will get the key
*labelled* W, which on their board is down in the bottom-left corner.

**RayClay has no positional-key query today**, so until it does, prefer the keys that do not
move (**arrows and space**) for anything ergonomic, and offer letter bindings as a
*rebindable* extra rather than the only way to play:

```c
static void update(RC_App *app, void *user) {
    Player *p = user; (void)app;
    /* Arrows are positional, so they are the same keys on every layout. */
    if (rcKeyDown(RC_KEY_LEFT))  p->x -= SPEED;
    if (rcKeyDown(RC_KEY_RIGHT)) p->x += SPEED;
    if (rcKeyPressed(RC_KEY_SPACE)) player_jump(p);      /* one jump per press */
}
```

**A cross-platform save shortcut**: Cmd+S on a macOS desktop build, Ctrl+S everywhere else (including a
browser on a Mac; see the note above), in one line:

```c
if (rcModDown(RC_MOD_PRIMARY) && rcKeyPressed(RC_KEY_S))
    save_document();
```

**Mind focused text fields.** A focused `rcTextInput` / `rcTextArea` edits with the keyboard, so a
bare single-key shortcut (a lone `RC_KEY_S`, arrow-key movement) can clash with typing. Gate those on
`!rcIsFocused("your_field")`; a shortcut that also holds `RC_MOD_PRIMARY` is normally safe to leave
ungated.

---

## Text fields and text areas

A text field edits a **caller-owned `char` buffer** in place through the vendored
stb_textedit state machine (cursor, selection, word navigation, undo, clipboard);
there is no separate widget object. `rcTextInput` is single-line; `rcTextArea` is
the multiline variant. Both return `true` on a frame the buffer changed.

```c
static char name[64];
static char bio[1024];

rcTextInput("name", name, sizeof name, .placeholder = "Your name");
rcTextArea ("bio",  bio,  sizeof bio,  .rows = 6);
```

**The buffer contract:**
- You own `buf`; `capacity` is its full size **including the NUL slot** (pass
  `sizeof buf`). Typing is clamped to `capacity - 1` characters.
- **Typed input is ASCII in v1.** Typing and pasting are filtered to ASCII (plus `\n`
  in a text area). **Text *you* supply is not**: a `.placeholder`, or a value you
  pre-fill into `buf`, renders the full Latin-1 window, so `.placeholder = "Prénom"`
  displays correctly.
- **Editing is byte-indexed**, so multibyte UTF-8 you pre-fill into `buf` is
  memory-safe but a backspace can split a character. That, not the rendering, is the
  reason to keep an *editable* buffer plain ASCII.
- Click to focus; click elsewhere or press **Esc** to blur. Only the one focused
  field holds edit state. `rcIsFocused(id)` / `rcSetFocus(id)` read and move focus
  (call `rcSetFocus` on an event, not every frame; a NULL/empty id clears focus).
- The caret blinks while focused and **settles to a steady on after 10 s without
  interaction**, resuming on the next keystroke or click. This is GTK's long-standing
  `gtk-cursor-blink-timeout` rule at the same default, and it is what lets an app with a
  focused field park at ~0 CPU instead of paying two frames a second forever. It settles
  *on*, so it never stops marking the insertion point.

**Text areas (`rcTextArea`) specifically:**
- **Enter** (and numpad Enter) inserts a newline; long lines **soft-wrap** at the box
  width; **Up/Down** move the caret by row; the view scrolls vertically to keep the
  caret visible. `.rows` sets the visible height (default 4).
- v1 limits: a **paste is capped at 1024 bytes** (the truncation warns once); **tabs
  are unsupported** (Tab is inert, pasted tabs are dropped); there is **no mouse-wheel
  scroll yet** (the view follows the caret only); an **unfocused text area shows from
  its top row**. App-supplied **CRLF / lone-CR line endings normalize to `\n`** in
  your buffer (the web-`<textarea>` value convention); copying *out* on Windows re-emits
  `\n` as CRLF at the OS clipboard boundary, while your buffer stays LF.
  - **That normalisation counts as a change.** Pre-fill a buffer with CRLF/CR content
    and the first frame rewrites it to LF, so `changed` returns `true` once before the
    user types anything. If you drive an "unsaved changes" flag off `changed`, either
    normalise your seed text to `\n` yourself before the first frame, or ignore the
    first-frame edge; the buffer is settled from frame two on.

**C++ callers:** the `...` designated-initialiser form is C99; from C++ call
`rcTextInputEx(id, buf, cap, opts)` with the option fields in declaration order.

---

## Menus

A menu is a button that opens a floating list of items below it. Only one
menu is open at a time. Clicking an item, pressing Esc, or clicking anywhere
outside closes it automatically.

```c
if (rcBeginMenu("file_menu", "File")) {
    if (rcMenuItem("New"))    new_document();
    if (rcMenuItem("Open…"))  open_document();
    if (rcMenuItem("Save"))   save_document();
    rcEndMenu();
}
```

**Rules:**
- Call `rcEndMenu()` only on the frame `rcBeginMenu` returned `true`
  (i.e. inside the `if` body, same as `rcBeginModal`).
- The `id` must be unique across every menu in the layout. The `label` is
  the button text shown in the toolbar.
- String literals work for both; they must outlive the frame.
- **A menu is floating, and a floating element does not inherit its target's clipping.** Open one inside a
  scroll panel and it draws *over* whatever lies outside that panel, and keeps drawing there as the panel
  scrolls. That is deliberate (escaping the container is the whole point of a menu), but there is no
  per-element opt-out, so if you need something clipped, do not float it.

### Menu bar pattern

Wrap several `rcBeginMenu` calls in a `rcRow` for a classic menu bar:

```c
rcRow(.gap = 4) {
    if (rcBeginMenu("m_file", "File")) {
        if (rcMenuItem("New"))  { /* ... */ } rcEndMenu();
    }
    if (rcBeginMenu("m_edit", "Edit")) {
        if (rcMenuItem("Undo")) { /* ... */ }
        if (rcMenuItem("Redo")) { /* ... */ }
        rcEndMenu();
    }
}
```

---

## Context menus

A context menu appears at the pointer position when the user right-clicks
a specific element (long-press on touch is planned, not yet available). The
target element must have a matching `.id`.

```c
/* 1. Tag the element you want right-clickable. */
rcRow(.id = "item_0", .w = "grow", .h = "44", ...) {
    rcTextL("My item");
}

/* 2. Declare the context menu bound to that element's id. */
if (rcBeginContextMenu("ctx_item_0", "item_0")) {
    if (rcMenuItem("Edit"))   edit_item(0);
    if (rcMenuItem("Delete")) delete_item(0);
    rcEndContextMenu();
}
```

**Rules:**
- The second argument `targetId` must exactly match the `.id` field of the
  element to watch.
- Place the `rcBeginContextMenu` call at the same level as (or after) the
  target element in the layout, never nested inside it.
- With dynamic lists, generate stable unique IDs per item. A per-frame arena
  string like `rcFormat(mem, "ctx_%d", i).chars` is the simplest approach.
  The IDs must be stable across frames for the menu's open state to persist.
  Set `.scratchArenaBytes` in your `RC_AppOptions` (4096 covers a screenful of
  short IDs); with the default `0` there is no scratch arena and `rcFormat`
  returns the visible placeholder `"<set scratchArenaBytes>"` (warning once), so
  every ID collapses to that one string and the whole list shares an id. It is
  deliberately visible: that state only exists while an app is misconfigured, and
  a marker on screen is seen where a log line is not.

```c
RC_Arena *mem = rcAppArena(app);          /* the runner's per-frame scratch arena */

for (int i = 0; i < count; i++) {
    const char *row_id = rcFormat(mem, "row_%d", i).chars;
    const char *ctx_id = rcFormat(mem, "ctx_%d", i).chars;

    rcRow(.id = row_id, ...) { /* ... */ }

    if (rcBeginContextMenu(ctx_id, row_id)) {
        if (rcMenuItem("Delete")) del = i;   /* defer; see below */
        rcEndContextMenu();
    }
}
/* Apply deferred mutation AFTER the loop; never modify the list you are
   iterating while element emission is still in flight. */
if (del >= 0) delete_item(&state, del);
```

---

## Modal dialogs and non-modal panels

A modal draws a full-viewport scrim that makes the rest of the UI inert, then
centers a floating panel inside it. It is controlled by a caller-owned `bool`:
set it to `true` to open, and the modal sets it to `false` on Esc, on a
scrim click (default), or when your code explicitly sets it.

```c
static bool open = false;

/* Somewhere in the layout: the trigger. */
if (rcButton("show_dlg", "Delete item", RC_BTN_DANGER))
    open = true;

/* The modal itself: emit the body only while open. */
if (rcBeginModal("dlg_confirm", &open)) {
    rcColumn(.bg = s.surface, .p = 24, .gap = 16, .borderRadius = "all-xl") {
        rcTextL("Delete this item?", .font = FONT_HEAD, .color = s.text);
        rcTextL("This cannot be undone.", .color = s.textMuted);
        rcRow(.gap = 8) {
            if (rcButton("dlg_ok",     "Delete", RC_BTN_DANGER))  { do_delete(); open = false; }
            if (rcButton("dlg_cancel", "Cancel", RC_BTN_DEFAULT)) open = false;
        }
    }
    rcEndModal();
}
```

**Rules:**
- Call `rcEndModal()` only when `rcBeginModal` returned `true`.
- Build all modal content **between** `rcBeginModal` and `rcEndModal`; it
  sits inside the floating panel.
- Place the `rcBeginModal` call **outside** the root column if you want the
  scrim to cover the entire window (the usual case). Placing it inside a
  container restricts the scrim to that container's bounds.
- Modals **stack, up to 8 deep**: a modal opened from inside another sits above
  it. Give each its own `bool` flag and a distinct id.

### Options: modality and dismissal are two *independent* axes

`rcBeginModalEx` takes an `RC_ModalOptions` with two flags, and they control
completely different things. Mixing them up is the single most common way to get a
popup that misbehaves, so it is worth being precise:

| field | controls | default |
|---|---|---|
| `modality` | **whether the app behind stays usable** | `RC_MODALITY_MODAL` (scrim; app behind inert) |
| `noBackdropDismiss` | **whether a click outside closes the popup** | `0` = an outside click closes it |

All four combinations are legal, and each is the right answer to a different question:

| `modality` | `noBackdropDismiss` | what you get |
|---|---|---|
| `RC_MODALITY_MODAL` | `0` | **Modal dialog** (the `rcBeginModal` default). Scrim; the app behind is inert; a click outside or Esc closes it. |
| `RC_MODALITY_MODAL` | `1` | **Blocking dialog.** Scrim; a click outside is ignored. The user must choose. Use for destructive confirmations. |
| `RC_MODALITY_NON_MODAL` | `0` | **Almost never what you want**: see the trap below. |
| `RC_MODALITY_NON_MODAL` | `1` | **A detached panel.** No scrim, the app stays live, and it stays open until closed explicitly. **This is "leave it open and keep working".** |

### The trap: `RC_MODALITY_NON_MODAL` alone is not "leave it open"

The obvious reading of `RC_MODALITY_NON_MODAL` is "the popup that lets me keep working". It is
not (**not on its own**), and the failure is quiet enough to ship.

`RC_MODALITY_NON_MODAL` removes the scrim, so the app behind *is* usable. But it does not touch
the *dismissal* axis, and the dismissal default is unchanged: **an outside click still
closes the popup.** With the scrim gone, "outside" means *anywhere in the app*. So
the very first click the user makes into the thing they wanted to keep using is also
the click that destroys the panel they wanted to keep open. The click does both: it
moves the slider *and* closes the panel.

**"Stays open" is the pair:**

```c
/* A detached inspector: the app behind stays live, AND the panel survives clicks. */
if (rcBeginModalEx("inspector", &open,
        (RC_ModalOptions){ .modality = RC_MODALITY_NON_MODAL, .noBackdropDismiss = true })) {
    rcTextL("Inspector");
    if (rcButton("insp_close", "Close", RC_BTN_DEFAULT)) open = false;
    rcEndModal();
}
```


**Esc closes it either way.** `noBackdropDismiss` suppresses the *outside-click*
dismissal only; it does **not** make a popup undismissable. Esc always closes the
topmost popup, modal or not. If a panel must survive Esc, own the `bool` yourself and
do not let the library clear it.

Two more things worth knowing before you reach for a non-modal panel:

- **It is still centered.** `rcBeginModalEx` always centers its panel; there is no
  offset or docking control. A real inspector usually wants to sit against an edge;
  that is a plain `.floating` `rcBox` (see the `.floating` entry in
  [`cheatsheet.md`](cheatsheet.md)), not this call.
- **A non-modal popup claims only the presses that land on itself**, so it cannot
  swallow input meant for the rest of your UI. (A modal's scrim, by design, claims
  every press on the window.)

`ex10` ships this as a working demo: **Open inspector** opens a non-modal panel with a
live readout, and a **"Stay open when I click away"** checkbox that flips
`noBackdropDismiss` at runtime: uncheck it, click the gallery, and watch the panel
vanish. That is the trap, made visible.

---

## Charts: give every chart a tooltip

A chart without a hover readout makes the reader estimate values off the gridlines.
Chart.js and Recharts both make a tooltip the default; RayClay makes it one field,
because a tooltip that draws over the plot is a cost you should opt into per chart:

```c
RC_Series series[2] = {
    { .y = requests, .count = 24, .label = "requests" },
    { .y = errors,   .count = 24, .label = "errors"   },
};
rcChart("hourly", series, 2, (RC_ChartOptions){
    .legend  = true,
    .y       = { .grid = true },
    .tooltip = RC_CHART_TOOLTIP_NEAREST,   /* hover => "x 10 / requests 78 / errors 3" */
});
```

`RC_CHART_TOOLTIP_NEAREST` floats a readout naming the hovered **x** plus **every series'
value there**, so a two-axis chart reads out both series from one hover, and a gap in the
data (a non-finite y) reads as a gap rather than a snapped-to-neighbour lie. *Which* datum
counts as hovered depends on what the chart draws; that is the next section.

**Do not hand-roll this.** It looks like a small job (track the pointer, divide by the
plot width, index the array), and it is wrong at exactly the places people hover most.
The chart owns its plot transform: the axis auto-fit, the tick rounding, the label
gutter, and the legend row all move the plot rect inside the box you gave it. Code
outside the chart does not know where the plot actually starts, so a hand-rolled
version drifts near both edges and breaks outright the first time a label gets wider.

Zero-init is `RC_CHART_TOOLTIP_NONE`, so adding the field changes nothing until you ask
for it. `rcSparkline` has no tooltip: it is deliberately axis-less and padding-less, and
a floating readout would cover the strip it is drawn on.

### Which datum is hovered: decided by mark geometry, not by an option

There is no field for this, because the right answer is not a preference; it follows from
what the chart actually **draws**:

| series kind | how the hovered datum is chosen |
|---|---|
| `RC_SERIES_LINE`, `RC_SERIES_AREA` | **Nearest x.** A curve is continuous, so every x has a reading, and the datum nearest the pointer's x is the one being asked about. |
| `RC_SERIES_BAR`, `RC_SERIES_SCATTER` | **The pointer must be on a mark.** A bar has a real rect and a scatter dot a real disc, and there is nothing to read *between* marks. No mark under the pointer means no readout at all. |

A chart that **mixes** the two stays continuous: the curve is readable at any x, even
where no bar stands. Marks get a few pixels of grace at **both** vertical ends, so a
near-zero bar (drawn 1px tall) and a negative bar, whose short end is the *bottom* one,
stay comfortably reachable.

> **There is no opt-out, by design.** Reporting a datum the user never pointed at is a
> defect, not a mode, so a bar chart will not hand you a readout for a bar the pointer is
> not on, and you never need a hit test of your own to suppress one.

### Where the readout sits: `.tooltipPlace`

`.tooltip` is the **trigger**; `.tooltipPlace` is the **position**. They are separate
fields, so the readout is still off until you set `.tooltip`, and once it is on, three
placements are available:

```c
rcChart("dense", series, 4, (RC_ChartOptions){
    .tooltip      = RC_CHART_TOOLTIP_NEAREST,
    .tooltipPlace = RC_TOOLTIP_PLACE_CORNER,   /* keep the panel off a busy plot */
});
```

- **`RC_TOOLTIP_PLACE_CURSOR`** is the default, and what a chart on the web does: the panel
  tracks the pointer, so the reading sits next to the thing being read instead of parked
  across the plot. It flips by quadrant (leftward once the pointer passes the plot's
  horizontal middle, upward past the vertical middle), so it always grows *away* from the
  edge it would otherwise overrun.
- **`RC_TOOLTIP_PLACE_CORNER`** parks the panel in the **top** corner opposite the
  pointer's half (pointer on the left ⇒ panel top-right, and the reverse). It stays
  entirely clear of the data, which makes it the better choice on a dense plot. **Name this
  mode explicitly whenever the panel must stay off the plot.**
- **`RC_TOOLTIP_PLACE_FIXED`** pins the panel at `.tooltipAnchor` (any of the nine
  `RC_Anchor` points; `0` is `RC_ANCHOR_TOP_LEFT`) and leaves it there whatever the
  pointer does. A dashboard that wants the readout to hold still wants this.

`.tooltipOffset` is the gap from the anchor point. **It is a distance, not a direction**:
the sign is applied for you, away from whichever edge the panel is leaning off, so one
value reads the same in all four quadrants. Each component falls back independently when
zero, so `{0}` means 12x12 and `{24, 0}` means 24x12: a zero component asks for the
*default* gap, not a flush one.

Placement is quadrant-based rather than measured, and that is deliberate. A floating
panel's own size is not known until the frame after it is laid out, so positioning from
the previous frame's size would make the panel jitter while the pointer moves. Flipping by
quadrant costs nothing and never lags.

### Hover *in* the plot: `.hoverGuide` and `.hoverMarkers`

The panel tells you the numbers. On a multi-series chart it cannot tell you **which line
each number belongs to**, and that is the reading you actually want. Two independent
booleans draw the hover into the plot itself:

```c
rcChart("hourly", series, 2, (RC_ChartOptions){
    .legend       = true,
    .tooltip      = RC_CHART_TOOLTIP_NEAREST,
    .hoverGuide   = true,   /* vertical rule at the hovered x */
    .hoverMarkers = true,   /* one dot per series, in that series' colour */
});
```

- **`.hoverGuide`**: a vertical rule at the hovered datum's x, drawn **under** the series
  so it aids reading the data instead of crossing it. Vertical only: with a second y axis a
  horizontal rule would have to pick one of the two and be wrong for the other.
- **`.hoverMarkers`**: one dot on each line, area and scatter series at the hovered x, in
  **that series' own colour**, with a halo so the dot still reads where a same-colour line
  runs through it. **Bars are skipped**: a hovered bar already shows which datum is
  selected, so a dot on it would be noise.

Both **default off**, so no existing chart changes, and both are **independent of
`.tooltip`**: a guide with `.tooltip = RC_CHART_TOOLTIP_NONE` is a legitimate choice if you
want the crosshair without the panel.

**They cost an idle app nothing, and that is gated rather than asserted.** With the pointer
parked away from the plot, the frame is byte-identical whether both fields are off or both
are on: the work happens only while a pointer is actually over the plot. Hovering, the
guide is one extra line and the markers two extra circles per series (the dot and its halo).

That check runs on the render **digest**, which folds every primitive's arguments, rather
than on a count of commands: a count cannot tell you a frame is unchanged, only that the
same *number* of things were drawn. It is paired with a control arm (the same comparison
with the pointer *on* the plot, which must differ), because an equality that holds because
the instrument went blind looks exactly like an equality that holds because the code is
right.

### Zoom, pan and brush: what you can build today

There is no built-in zoom, pan or brush widget, and for **zoom** you do not need one. The
view is two floats you own, so pinning them and re-declaring is the whole feature:

```c
static float lo = 0.0f, hi = 500.0f;                      /* YOUR window over the data */
if (rcIsHovered("plot")) {
    float w = rcScrollDeltaY();                           /* 0 when Ctrl+wheel zoom took it */
    if (w != 0.0f) {
        float mid = (lo + hi) * 0.5f;
        float half = (hi - lo) * 0.5f * (w > 0.0f ? 0.9f : 1.1f);
        lo = mid - half; hi = mid + half;
    }
}
rcChart("plot", s, 2, (RC_ChartOptions){ .x = { .min = lo, .max = hi } });
```

This works because the wheel hands you a **scalar**: how much to zoom, not where. It
needs no knowledge of the plot's geometry, and immediate mode makes re-plotting at the
new range free.

**A drag-brush needs one more thing: the rect you are mapping into.** A pointer position
is only half a mapping. `rcChartPlotRect(chartId)` is the other half, and note it is
**not** `rcGetElementBox(chartId)`. The chart sizes its plot *inside* the box you gave it
(the y-axis gutter grows with the widest tick label; a legend or axis title takes a header
row), so mapping against the outer box is wrong by however much chrome the chart chose.
That is the same fact the tooltip section above states (the plot rect is private), except
now the chart will tell you what it decided.

```c
static bool  brushing;
static float b0, b1;                                  /* brush edges, in DATA units */

RC_Box       plot = rcChartPlotRect("plot");
RC_Vec2 p    = rcPointer();                      /* already CONTENT space */

if (plot.found && plot.width > 0.0f) {                /* the WIDTH check is the readiness test */
    float t     = (p.x - plot.x) / plot.width;        /* 0..1 across the plot */
    float dataX = lo + t * (hi - lo);

    if (rcPointerPressed(RC_POINTER_LEFT) && rcIsHovered("plot")) {
        brushing = true;  b0 = b1 = dataX;
    } else if (brushing && rcPointerDown(RC_POINTER_LEFT)) {
        b1 = dataX;                                   /* live edge; draw it if you like */
    } else if (brushing && rcPointerReleased(RC_POINTER_LEFT)) {
        brushing = false;
        if (b1 < b0) { float t2 = b0; b0 = b1; b1 = t2; }   /* dragged right-to-left */
        if (b1 - b0 > 0.0f) { lo = b0; hi = b1; }           /* ignore a click-with-no-drag */
    }
}
rcChart("plot", s, 2, (RC_ChartOptions){ .x = { .min = lo, .max = hi } });
```

Three things that are easy to get wrong, all of them cheap to handle:

- **Guard on the extent, not on `.found`.** This one is a genuine trap, because the
  obvious-looking `if (plot.found)` is the wrong test. `.found` answers *"does an element
  with this id exist?"*, not *"is its rectangle ready?"* the layout engine registers an element in its
  hashmap the moment it **opens**, but only fills in the bounding box when layout **ends**,
  so on an element's very first frame you get **`found = true` with an all-zero rect**.
  `if (plot.found)` alone therefore computes `(p.x - 0) / 0` on frame one. Use `.found` to
  catch a misspelled id and the **width** to catch the first frame, which is exactly what
  `plot.found && plot.width > 0.0f` above does. For the same reason the rect is one frame
  stale through a resize and exact while the scene is static, which is the normal case for a
  gesture since a drag spans many frames anyway.
- **Normalise the drag.** A user dragging right-to-left gives you `b1 < b0`; swap them
  before pinning or the chart gets an inverted range.
- **Reject the zero-width drag**, or a plain click on the plot collapses your range to a
  single value and there is no way back except a reset.

Wheel-zoom needs none of this, because a wheel hands you a **scalar** rather than a position, so it
needs nothing from the pointer or the box. Keep both: the wheel for quick in/out, the drag for
"show me exactly this window".

**Tooltips are not a chart feature.** Any ordinary element gets one the same cheap way:
`.tooltip = "Refresh"` on an `rcBox` / `rcRow` / `rcColumn`. Two conditions: the element
**must carry an `.id`** (the tooltip is keyed and hit-tested by it), and the string must
**outlive the frame**: a literal is ideal, a stack buffer is not. (Unlike an element `.id`,
which RayClay hashes on the spot, the tooltip string is retained.) `NULL`/empty means none.

```c
rcBox(.id = "refresh", .tooltip = "Refresh (Ctrl+R)", .p = 8) { … }
```

---

## Scrolling: follow the tail without hijacking the reader

A log, a chat transcript, or a live table wants to stay pinned to the newest line, but
only while the reader has not scrolled up to read something. Auto-scrolling unconditionally
is the single most irritating bug in this class of UI: the user scrolls back, and the next
arriving line yanks them to the bottom.

`rcIsScrolledToBottom` makes the correct behaviour a one-liner:

```c
if (st->log.total != st->lastTotal) {                   /* a NEW line arrived */
    if (rcIsScrolledToBottom("LogScroll"))             /* ...and they were already at the bottom */
        rcScrollToBottom("LogScroll");
    st->lastTotal = st->log.total;
}
```

Two details that are easy to get wrong:

- **Detect "new line" from a monotonic counter**, not from the number of lines currently
  held. A ring buffer's count saturates at capacity, so `count != lastCount` stops firing
  the moment the ring fills: the log follows the tail for the first N lines and then
  silently stops. Keep a total that only ever increases.
- **`rcIsScrolledToBottom` is also true when the content fits**, which is what you want:
  a log that has not yet overflowed its viewport stays pinned, and starts following
  properly the moment it does overflow. It is `false` for an id that named no scroll
  container in the last laid-out frame. **And a typo is only one way to be in that
  state.** Two more bite with a correctly spelled id, on an element the reader can
  plainly see. First, an id is a scroll container only if that element actually asked
  to clip: `.scroll = "v"` / `"h"` / `"b"`, or `.overflow` set to `"hidden"` / `"clip"`
  / `"scroll"` / `"auto"`. `.overflow = "visible"` is the default and clips nothing, so
  the panel you think of as "the scrolling one" is not registered until it clips.
  Second, there are only **100 clip slots per frame**, and every clipped element spends
  one whether or not it scrolls; past that ceiling the container still lays out and
  still clips, but reads here as not a scroll container at all. Both look identical
  from the outside: the tail silently stops following a list that is plainly on screen.
  Read that symptom as "my container is not registered this frame" before you read it
  as "the user scrolled up". `false` is also the honest answer in the transients:
  before the first layout completes, after the run loop exits, and once you stop
  declaring a container: a collapsed panel, a tab you switched away from.

For anything finer (a "jump to bottom" button that should appear only when the reader is
away from the tail, or a progress indicator tied to scroll depth), read the numbers
directly with `rcGetScrollInfo`. It reports in the DOM's positive-down convention
(`.offsetY` reads like `element.scrollTop`, `.maxOffsetY` like `scrollHeight - clientHeight`),
both clamped `>= 0`. **`.found` does not tell you whether the id exists**; it tells you whether
that id was a **live scroll container** in the last laid-out frame. Those are different questions
with different answers: `rcGetElementBox` can report a real box for the very element
`rcGetScrollInfo` reports `.found = false` for. **Test it first**, because every other field is
zeroed on the false path, and zeroed means `.offsetY` and `.maxOffsetY` are *both* `0`, so a
hand-rolled `offsetY >= maxOffsetY` reads **true, "at the bottom"** and auto-scrolls a container
that was never found. `rcIsScrolledToBottom` is safe here only because it checks `.found` first.

`ex12` ships this: its log panel is an `rcBeginTable` grid that follows the tail while you leave
it alone and holds still the moment you scroll up.

**Under the default `RC_RENDER_ON_DEMAND` this block only runs on a frame that actually
happened, and `rcScrollToBottom` does not itself request one.** If the new line arrives from
somewhere RayClay cannot see (a socket, a worker thread, a timer), call `rcAppRequestFrame(app)`
where you append it, or `rcAppRequestFrameAfter(app, 0.1)` for a polled feed. Otherwise the window
stays parked, the callback never runs, and the tail never follows. `ex12` sidesteps this by setting
`.renderMode = RC_RENDER_CONTINUOUS`, because it measures the frame loop itself.

**Past a few hundred rows, declare only what is visible.** Layout charges for every element you
*declare*, not every element drawn, so a long log pays for scrollback nobody is looking at. Wrap
the row loop in `rcVirtualList(row, "LogScroll", total, 28) { … }`: the content height, scrollbar
travel and scroll position stay identical while the per-frame cost stops depending on the row
count. It composes with `rcBeginTable`: name the table's own id as the container.

> **Inside a table, that last argument is the row PITCH, and the pitch is not the row height.**
> Every cell carries `RC_TableOptions.cellPadding` above and below its content, so the distance from
> one row's top to the next is `rowHeight + 2 * cellPadding`. Pass the row height alone and the
> spacers under-report the content by twice the padding on every row; the list runs short and the
> scrollbar stops agreeing with it. Nothing warns; you find it by looking at the window.
> **So declare the padding rather than inheriting it**: `RC_VAL(4)`, then derive the pitch from that
> same constant. `ex10` and `ex20` both do. Inherit the default and your pitch silently depends on a
> number the call site never names, which is the actual hazard; the default itself is fine.

### `cellPadding` is an `RC_OptFloat`: unset and zero are different things

`RC_TableOptions.cellPadding` is not a plain float. Zero-initialise the struct and you get RayClay's
house 6 px, which is the right default and is meant to be inherited when you have no opinion:

```c
rcBeginTable("t", cols, 3, (RC_TableOptions){0});                        // 6 px: the house default
rcBeginTable("t", cols, 3, (RC_TableOptions){ .cellPadding = RC_VAL(0) });  // flush, no padding at all
rcBeginTable("t", cols, 3, (RC_TableOptions){ .cellPadding = RC_VAL(10) }); // exactly 10
```

**`.cellPadding` is an `RC_OptFloat`, not a bare float**, so `RC_VAL(0)` really is zero, and an unset
field takes the 6 px default. Pass a bare number and the compiler will tell you: wrap it in `RC_VAL`.

> **The rule this encodes, worth applying to your own option structs:** a zero-means-default is
> only legitimate where zero has no valid meaning. A font size of `0` describes nothing, so
> `0 => the slot's size` is fine. "No padding" describes something a caller can genuinely want, so
> that field must be able to carry it.

---

## Titlebars: keeping a widget clickable inside the drag band

This one is a genuine trap, because **it works perfectly on web and fails on desktop**.

A press anywhere inside an element tagged `RC_ID_WINDOW_DRAG` starts an OS window-move.
So a theme toggle, a "new" button, or a search field placed visually inside your custom
titlebar loses its click to the drag; the window moves a pixel instead of the toggle
flipping. On web there is no window drag, so the same code behaves exactly as intended,
and the bug is invisible until someone runs the desktop build.

Two correct fixes, and they suit different layouts:

**1. Tag the widget cluster `RC_ID_WINDOW_NODRAG`**: the whole band stays draggable, and
the hit-test treats that subtree as ordinary client area (which is how the built-in window
controls have always been exempt). This is the desktop mirror of CSS
`-webkit-app-region: no-drag`:

```c
rcUnzoomed() {                                                     /* chrome, not content; see below */
    rcRow(.id = RC_ID_WINDOW_DRAG, .w = "grow", .h = "56", .px = 14, .gap = 12, .align = "cl") {
        rcTextL("RayClay", .font = F_HEAD);
        rcBox(.w = "grow") {}                                          /* spacer: still drags */
        rcRow(.id = RC_ID_WINDOW_NODRAG, .gap = 8, .align = "cl") {    /* this cluster does not */
            rcTextC(st->darkMode ? "Dark" : "Light", .font = F_SMALL);
            rcToggle("tg_theme", &st->darkMode);
        }
        rcWindowControls();
    }
}
```

**2. Restructure so the drag id only ever wraps non-interactive content**: the drag element
holds the logo, title and spacer, and the widgets are siblings beside it rather than children
of it. No new id, and it is the shape to prefer when the band's interactive parts are already
at one end.

Either way, the rule to carry: **anything that reads `rcClicked` or `rcPressed` must not be a descendant of
`RC_ID_WINDOW_DRAG`** unless it is inside an `RC_ID_WINDOW_NODRAG` subtree. `ex05` demonstrates
the no-drag form; the bench apps under `examples/bench/` demonstrate the restructured form.

### The second trap in the same band: your titlebar must not zoom

Wrap the band in [`rcUnzoomed()`](api-notes.md#rcunzoomed), as above. The reason is that
`RC_AppOptions.titlebarHeight` freezes the strip the OS lets you drag in **physical** pixels when the
window is created, while a band you draw yourself is ordinary content and grows with the browser-like
content zoom. The bar you *see* stops matching the bar you can *grab*.

Measured on a 46px band with `.titlebarHeight = 46`, reading
`rcGetElementBox(RC_ID_WINDOW_DRAG).height × rcAppZoom(app)`, which is the band's physical height, and
must equal 46 at every step:

| content zoom | 0.50 | 0.75 | 1.00 | 1.25 | 1.50 | 2.00 |
|---|---|---|---|---|---|---|
| plain band, physical px | 23.0 | 34.5 | 46.0 | 57.5 | 69.0 | 92.0 |
| inside `rcUnzoomed()` | 46.0 | 46.0 | 46.0 | 46.0 | 46.0 | 46.0 |

At 2× the drawn band is exactly **twice** the configured height. The bundled `rcTitlebar`
counter-scales itself, so this trap is specific to `titlebar.custom`, and it is the inverse of
`RC_TitlebarOptions.zoomWithContent`, which opts the *bundled* band back **in** to the zoom.

**One coupling `rcUnzoomed()` cannot check for you:** the band's height must be the same number you
passed to `.titlebarHeight`. Get that wrong and the desync is a small *constant* at every zoom rather
than a proportional one, and harder to see. A **debug desktop build warns once**, naming both numbers
(the band's physical height and the OS strip); a Release build and web do not, so develop with the
warning available rather than relying on spotting it by eye.

**Do not multiply anything by the zoom yourself.** Everything RayClay lays out inside the scope is
already counter-scaled; `rcUnzoomedScale()` exists only for pixel maths RayClay does not own, such as
your own `RC_CustomDrawCallback`.

---

## See also

- [`examples/ex10_rayclay_widgets_gallery/main.c`](https://github.com/impizulu/rayclay/blob/examples/examples/ex10_rayclay_widgets_gallery/main.c):
  the full widgets gallery: a menu bar, a right-click context menu, a modal dialog and
  a **non-modal inspector panel** side by side (alongside every other widget) in one
  working source.
- [`cheatsheet.md`](cheatsheet.md): every public RC_ symbol, one line each.
