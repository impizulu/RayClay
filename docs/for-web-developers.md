# Coming from the web: React / Tailwind → RayClay

RayClay is a C GUI library, but its authoring model is built for you: a developer who thinks in
components, props, state, and utility classes. This page maps what you already know onto RayClay and
flags the one place where C rules genuinely differ from JavaScript.

The one-sentence model: **RayClay is immediate-mode; your layout function is `UI = f(state)`, called
whenever the UI needs to redraw, with the reconciler removed.** There is no virtual DOM, no hooks, no
cascade. You mutate state directly and describe the whole UI in one pass; nothing diffs, everything
lays out.

**"Whenever it needs to redraw" is literal, and it is the one thing to internalise before you write
an app.** RayClay defaults to `RC_RENDER_ON_DEMAND`: it draws when something actually
happened and otherwise parks at ~0 CPU, exactly like a native toolkit, and unlike a `while (true)`
game loop. It already wakes itself for everything it can see (input, resize, focus, DPR changes). For
state it *cannot* see (a socket, a worker thread, a timer, an animation you drive yourself), you must
say so: `rcAppRequestFrame(app)` for "redraw now", `rcAppRequestFrameAfter(app, 0.1)` for "redraw in
100 ms". **This is the browser's `requestAnimationFrame` bargain: nothing animates for free.** A game
or simulation that genuinely must run every frame sets `.renderMode = RC_RENDER_CONTINUOUS` instead.

## What maps to what

| You know | RayClay | The difference that matters |
|---|---|---|
| JSX / HTML nesting | nested `rcBox` / `rcRow` / `rcColumn` braces | a C-preprocessor DSL, no DOM behind it |
| a component | an ordinary C function | no instance/object: just a function that emits elements |
| props | function arguments | C value/pointer lifetime rules apply (see below) |
| `useState` / app state | a long-lived `struct` you own | no hook storage; you hold the state |
| a controlled input | a pointer to a field in your state | `rcTextInput("name", state->name, sizeof state->name)` |
| conditional rendering | an ordinary `if` | re-evaluated on every redraw, like a re-render |
| `useEffect` firing a re-render | `rcAppRequestFrame(app)` | state RayClay cannot see must ask for the redraw |
| `.map()` over a list | an ordinary `for` | stable ids still required (see *Lists*) |
| React `key` | the element / widget **id** | the id also carries hover / scroll / focus state |
| `onClick` | `rcClicked(id)` | the same release edge the DOM has: press, slide off, release **cancels** |
| `onMouseDown` / `onPointerDown` | `rcPressed(id)` | eager, and like the DOM's it **cannot be cancelled**; one fire per press, no auto-repeat |
| inline `style={{...}}` | designated option fields `.bg`, `.p`, `.gap` | a documented subset, no cascade |
| Tailwind utility classes | those same fields, CSS-familiar values | Tailwind-*style* names, not class syntax |
| `theme.extend.colors` | your own `RC_Color` constants | one name per shade, plain C |
| a media query | a viewport check in C | no declarative breakpoints, and divide the window width by `rcAppZoom` first, see [Breakpoints](#breakpoints-measure-the-layout-width-not-the-window-width) |

Backend pointers (windows, GL, fonts) are fully hidden. The pointers you *do* see are deliberate: they
make **state ownership** explicit without allocation or hidden retained objects:

```c
rcCheckbox("enabled", "Enabled", &state->enabled);
rcSlider("volume", &state->volume, 0.0f, 1.0f);
rcTextInput("name", state->name, sizeof state->name);
```

That `&state->field` is RayClay's controlled-component model: you own the storage, RayClay reads and
writes it. No `useState`, no setter, no re-render bookkeeping.

## The one gotcha: string & pointer lifetimes

This is the single place where C bites a JS developer. **`rcText` and `rcTextC` do not copy the string;
the layout engine keeps your pointer until the frame is drawn.** The most natural JS move becomes a
use-after-scope bug in C:

```c
// BROKEN: buf dies when the function returns, before the frame draws.
static void row(int n) {
    char buf[32];
    snprintf(buf, sizeof buf, "%d items", n);
    rcTextC(buf);            // RayClay retains &buf[0] - dangling by draw time
}
```

Three safe patterns:

```c
rcTextL("Static label");                              // (1) a literal: always alive
rcTextC(state->name);                                 // (2) a char* buffer YOU own that outlives the frame
rcText(rcFormat(rcAppArena(app), "%d items", n));     // (3) the per-frame arena (needs scratchArenaBytes)
```

(`rcTextC` takes a `const char *`; `rcText` takes an `rcFormat`-style `RC_String`; `rcTextL` takes a
compile-time literal. None of the three copies; pattern (3)'s string lives until the arena resets next
frame.)

The full lifetime table, worth internalising once:

| What you pass | Who owns it | Rule |
|---|---|---|
| a string literal (`rcTextL`, ids) | the binary | always valid |
| a dynamic string (`rcText` / `rcTextC`) | **you** | must outlive the frame: *not* a function-local buffer |
| a scalar (`.p`, `.bg`, `&state->volume`) | you / by value | copied or read in place; no lifetime concern |
| an `rcFormat(...)` string | the frame arena | valid for this frame only; re-format next frame |
| a loaded font / image handle | RayClay | keep the id; the resource is retained for you |
| callback `userData` | you | the pointer you pass is handed back unchanged |

There is no runtime canary for the dangling-string case; treat the rule above as the contract.

### The same rule one step further: don't free your model *during* the layout

The table above is about a pointer that dies too early. The other half is a pointer you kill yourself,
and it is easy to reach because **a click handler runs inside your layout callback**:

```c
// BROKEN: the row is freed while the rest of THIS layout still borrows from it.
if (rcClicked("delete")) {
    note_store_remove(state, i);      // frees state->notes[i].title ...
}
rcTextC(state->notes[i].title);       // ... which a later element still reads
```

`rcClicked` and friends report an edge *while the layout is being built*, so anything you free or
reallocate there can still be read by elements declared after it, including strings the engine
retained earlier in the same frame. The failure is a use-after-free, not a blank label, and it will
not reproduce every run.

**Defer the mutation instead.** `updateCallback` runs before `layoutCallback` on each frame, so a flag
set during the layout is acted on next frame with nothing borrowed:

```c
static void update(RC_App *app, void *user) {
    App *s = user;
    if (s->pendingDelete >= 0) {      // acted on OUTSIDE any live layout
        note_store_remove(s, s->pendingDelete);
        s->pendingDelete = -1;
    }
}
// ... and in the layout, only record the intent:
if (rcClicked("delete")) s->pendingDelete = i;
```

This is the same discipline as an immediate-mode UI in any language (record intent while drawing,
apply it between frames), and it is why `RC_AppOptions` gives you an `updateCallback` at all.

## Alignment: the `.align` matrix

`.align` positions the *children* of a box in two characters: **vertical then horizontal.**

```
        left ("l")   center ("c")   right ("r")
top     "tl"         "tc"           "tr"
center  "cl"         "cc"           "cr"
bottom  "bl"         "bc"           "br"
```

So `.align = "cc"` centres both ways; `.align = "tr"` pins children to the top-right. A single axis is
fine (`.align = "c"` sets only the vertical), but the two-letter form is clearest. One caveat: `.align`
centres a *single line of text* only if the text element fills the box; otherwise a one-line label
shrink-wraps to its own width, so centre it via the **parent's** `.align`.

## Sizing: strings vs. typed

`.w` / `.h` accept CSS-like strings (`"grow"`, `"fit"`, `"200px"`, `"50%"`, `"50vw"`) that read like
Tailwind and cost exactly the same as the typed forms for static literals (measured 1:1). Reach for the
typed constants (`RC_PX(x)`, `RC_GROW`) only when the value is **computed every frame** in a hot loop.
One footnote: a decimal-percent string like `"12.5%"` parses ~1.8× slower than a whole number, still
tens of nanoseconds, so it only matters in a hot per-frame path. Rule of thumb: **strings for the values
you type by hand, typed constants for values you calculate.**

### `%` is parent-relative, and its basis is not CSS's

`"50%"` (and `RC_PCT(50)`) resolves against the **parent**, like CSS, but the basis differs in three
ways that each produce a correct-looking layout you did not ask for. Measured by rendering a 400 px row
and counting pixels, 2026-08-09:

| you write | CSS gives you | RayClay gives you |
|---|---|---|
| two `50%` siblings, `.gap = 10` | 200 + 200 → **overflows** by the gap | **195 + 10 + 195**: they *fit* |
| one `50%` child, no gap | 200 | 200 |
| a `50%` child of a **`"fit"`** parent | 200 (basis is the content box) | **0: the box vanishes** |

- **On the main axis, a percent is of what's left after the gaps are reserved.** Two 50 % siblings share
  the row instead of overflowing it. This is usually what you wanted; it is not what CSS does.
- **On the cross axis, gaps are not involved**: a clean `50%` of a 400 px parent is 200 px.
- **A percent inside a shrink-to-fit parent collapses to `0`, silently.** A `"fit"` parent sizes itself
  *from* its children, so a child asking for a percentage *of its parent* is circular and resolves to
  nothing. **Nothing is logged**; the element simply is not there, which looks exactly like a mistake in
  your own layout code. **If a box disappears, check whether its parent is `"fit"`** (that includes the
  default: `.w` unset **is** `fit`). Give the parent a definite width (`"grow"`, `"400px"`, a `%` of its
  own definite parent) or size the child in `px` / `vw`.

**`vw` / `vh` sidestep all of this**, because they resolve against the window rather than the parent,
so they work at any depth, including inside a `"fit"` box.

## Lists: stable ids are your `key`

Every interactive element needs an id, and, exactly like a React `key`, it must be **stable across
frames**, because the id is what carries hover / scroll / focus / selection state. Give each row an id
backed by memory that lives as long as the row: a `static` table of literals for a small fixed list, or
an id array you fill once for a long one (see the next section). A scratch buffer rebuilt every frame
still *hashes* correctly, so layout and clicks work, but the layout engine also keeps the id string for its debug
inspector, so scratch ids show up garbled there. Until an indexed-id shorthand lands (the equivalent of
`key={i}`), a small list is just a literal table:

```c
static const char *ROW_IDS[] = { "row0", "row1", "row2", "row3" };
for (int i = 0; i < n; i++)
    rcRow(.id = ROW_IDS[i]) { /* ... */ }
```

## Long lists: render only what's visible

Immediate mode rebuilds every element every frame, and layout charges per **declared** element, not per
*visible* one. So a 5,000-row list lays out 5,000 rows to show fifteen, and culling cannot rescue
you: an element has to be sized and positioned before anything knows it is offscreen, which is why
culling happens at draw time, too late to matter. One such list costs several times an entire
240-widget screen.

The fix is the one you already know from `react-window`: render only the visible slice and reserve the
rest of the scroll height. RayClay ships it as a loop macro. `rcVirtualList` works out the visible
window from last frame's scroll position, adds overscan rows so a fast fling has no gap, and emits the
two spacer boxes that hold the total content height constant, so the scrollbar and the scroll position
behave exactly as if every row were there.

```c
enum { ROW_COUNT = 5000, ROW_H = 28 };

typedef struct {
    const char *labels[ROW_COUNT];   /* your row data (must outlive the frame) */
    int         selected;
} ListState;

static void virtual_list(ListState *st) {
    rcColumn(.id = "list", .w = "grow", .h = "grow", .scroll = "v") {
        rcVirtualList(row, "list", ROW_COUNT, ROW_H) {
            char id[16];
            snprintf(id, sizeof id, "row%d", row.index);   /* key by the DATA index */
            rcRow(.id = id, .w = "grow", .hType = RC_PX(ROW_H), .p = 6,
                  .bg = (row.index == st->selected) ? rcGetStyle().primary
                                                    : rcGetStyle().surface) {
                rcTextC(st->labels[row.index]);
            }
            if (rcClicked(id)) st->selected = row.index;
        }
    }
    rcScrollbar("list");
}
```

That is the whole thing: no manual first/last arithmetic, no spacer bookkeeping, and no table of
pre-baked row ids (a local buffer is fine: the id is hashed as the element opens, so it never has to
outlive the frame). `snprintf` needs `<stdio.h>`; `rayclay.h` does not include it for you.

> **Why the id is keyed on the data index and not on the visible slot**, and the one ceiling worth
> knowing. Element ids are 32-bit hashes of the **string**, so formatted-index ids can collide once enough
> of them coexist in a single frame; how many is enough depends on the prefix you picked, which is not
> something you can reason about from your own code. Measured 2026-08-08: `"item%d"` is clean at 15,000
> siblings and collides at 20,000; `"Row %d"` and `"e%d"` stay clean through 65,536.
>
> **Virtualizing is what makes this a footnote rather than a hazard.** `rcVirtualList` declares only the
> visible window plus overscan, so a 1,000,000-row list still has about twenty live ids; the data index
> above is safe at any list size. The collision regime needs a *non*-virtualized list of 15,000+ siblings,
> and reaching it at all takes an explicit `RC_AppOptions.startLayoutElements` (the arena grows from 2,048).
> If you do hit it, it is not silent: the warning names the exact id.

> **Sizing note: a scrolling virtual list churns ids by design, so budget ~2× its element count.**
> the layout engine retires a generation of elements at the *end of the following* frame, so while you scroll, two
> generations are briefly resident: a 24-row list occupies about **50** slots (root + anchor + 2×24),
> not 25. If you set `RC_AppOptions.startLayoutElements` yourself, size it for roughly twice your
> per-frame element count. Undersizing is not a bug and never shows a half-drawn frame; the arena just
> doubles over a few background frames while you scroll.

> **The one shape to avoid: a scroll container inside another scroll container.** Nesting
> `overflow-y: auto` is unremarkable on the web, so this is the trap most likely to find you here. A
> scrolling parent is unbounded along its scroll axis, so the `"grow"` above resolves against its own
> content instead, and the spacers *are* that content, which is what the helper reads back as a viewport.
> Give the inner container a real height (`.h = "400px"`, or a parent that bounds it) whenever something
> above it scrolls. RayClay clamps the sampled viewport to the layout and logs one line naming
> the list, so the symptom is a stuck screenful plus a diagnostic rather than a hang. `"grow"` is correct
> everywhere else, including at the root exactly as written above.

Measured through the real DSL, the cost stops depending on how long the list is:

| rows | declared in full | `rcVirtualList` |
|---:|---:|---:|
| 100 | 446,057 Ir/frame | 86,757 (5.1×) |
| 1,000 | 4,375,713 | 86,689 (**50.5×**) |
| 5,000 | 22,040,911 | 86,689 (**254×**) |

Those are a **3-element row**. Layout charges roughly 1,470 instructions per element you declare, so
scale the left column by your own elements-per-row rather than quoting these: a richer row moves the
absolute cost and the ratio together. The right column barely moves at all, which is the point.

It is a memory lever too: one full-list frame permanently ratchets the element arena up (10.97 MiB at
5,000 rows), where the virtualized list holds the 1.43 MiB floor.

Three rules, and the third is the one that bites:

- **The id must name the enclosing scroll container**, and the macro sits directly inside it.
- **Every row must really be `rowHeight` tall.** The spacers are computed from that number, so a wrong
  pitch skews the scrollbar. Uniform rows only; a variable-height list still needs per-row
  measured offsets, which RayClay has no built-in for yet.
- **Key each row by its data index** (`row.index`), never by its position in the window. Keying by
  position retires and rebuilds the whole hashmap working set on every scroll step; it gives back most
  of the win *and* makes hover and selection jump between rows as you scroll.

**Do not `break` out of the `rcVirtualList` body.** The trailing spacer is emitted by the loop's *final* step, so
breaking skips it and the content ends up short by the rows you never declared; the scrollbar stops
matching the list. `continue` is fine. (This is a separate hazard from a `break` inside an `rcBox` body, which RayClay
closes for you; that one is safe.)

One more reason to virtualize: element ids are 32-bit hashes of the **string**, so formatted-index ids
like `"note%d"` can collide once enough of them coexist in one frame, and **how many is enough depends
on the prefix you happened to pick**, not on anything you can reason about. Measured: `"item%d"` is
clean at 15,000 siblings and collides at 20,000, while `"Row %d"` and `"e%d"` stay clean through
65,536. A virtualized list only ever declares about twenty ids, which is why it sidesteps the question
entirely: a non-virtualized list of 15,000+ siblings is the only way to reach it.

## What transfers from CSS / Tailwind, and what doesn't

RayClay is **CSS-familiar and Tailwind-inspired, not Tailwind-compatible.** The honest boundary:

- **Strong:** nested flex layout; fixed / parent-`%` / `vw`·`vh` sizing; hex / `rgb()` / `rgba()` /
  named colours; a Tailwind-*named* palette plus your own tokens; themes; built-in widgets; a
  desktop-first runner.
- **Partial / different semantics:** `%` over 100 and intrinsic sizing; positioning / overflow;
  gradients; per-side borders; single-line text alignment; the radius scale.
- **Absent by design or not yet:** class syntax; the cascade; selectors / specificity; `hover:` /
  `focus:` state variants; responsive breakpoints & container queries; grid; flex-wrap; margin; logical
  properties; full CSS Color / text shaping.

The per-field map is below.
When you reach for something in the "absent" column, the answer is usually an ordinary `if` in C: a
`hover:` is `if (rcIsHovered(id))`, and a breakpoint is a width test.

## CSS → RayClay: the per-field map


RayClay's DSL is CSS-*like*, not CSS: a familiar subset with a few deliberate deviations. This is the
one-glance map (each field's fine print is inline with its module below):

| CSS | RayClay | Deviations |
|---|---|---|
| flexbox layout | `rcRow`/`rcColumn`/`rcBox`, `.align "<Y><X>"` | a flexbox subset; no grid, no float |
| `justify-content` | the **main-axis** letter of `.align`: **X (2nd) in a row, Y (1st) in a column** | the two letters swap roles by direction; see "module: layout". **No `space-between`/`-around`/`-evenly`**: start/centre/end only |
| `align-items` | the **cross-axis** letter of `.align`: Y (1st) in a row, X (2nd) in a column | same swap; `"cc"` reads correctly either way, which is why the flip bites late |
| `width`/`height`, `%`, `vw`/`vh`, `auto` | `.w`/`.h` strings `fit`/`auto`/`grow`/`N`/`Npx`/`N%`/`Nvw`/`Nvh` | `%` must be 0–100 and is **not** clamped: `150%` warns once and falls back to the default sizing (FIT), exactly as a bad unit does |
| `padding`/`gap`/`margin` | `.p` · `.px`/`.py` · `.pt`/`.pb`/`.pl`/`.pr` · `.gap`; `rcMargin`/`rcSeparator` | px scalars only |
| `color`, hex, `rgb()`/`rgba()`, names | `rcColor("…")` · `rcRgb`/`rcRgba` · `rcHex` · `rcAlpha` | hex 3/4/6/8 + `rgb()/rgba()` + 20 names + `transparent`; **no `hsl()`**, not the full 148-name set |
| `border` width | `.border` (`RC_Border`) `"1px"` / `"all-2px"` | all-sides only **in the `rc` DSL**, but reachable the same way as min/max: set `border.width.left`/`.right`/`.top`/`.bottom` on the `defaults` declaration. That struct also carries `.betweenChildren`, which draws a line between each child along the container's direction and has no CSS equivalent. See the note under this table |
| `border-radius` | `.borderRadius` `"{side}-{size}"`, e.g. `"all-8"`, `"tl-6px"`, `"all-lg"`, `"-md"` | side ∈ `all` (or empty)/`t`/`b`/`l`/`r`/`tl`/`tr`/`bl`/`br`; size ∈ a number, `Npx`, **or** `xs`/`sm`/`md`/`lg`/`xl`/`2xl`/`full`. Empty side = all four (`"-md"` == `"all-md"`, the Tailwind shape). **A bare number = all four corners** (`"8"`/`"8px"` == `"all-8"`, like `.w`); a bare *keyword* still needs the dash (`"md"` warns → use `"-md"`) |
| `background-color` | `.bg`: an `RC_Color` field on `rcBox`/`rcRow`/`rcColumn` (and `rcSeparator`/`rcMargin`), e.g. `.bg = rcColor("#1e293b")` | a struct field, not a property, and it takes an `RC_Color` rather than a CSS string; the string form is `rcColor("…")`. Unset, or any colour with alpha 0, fills nothing, exactly like `transparent`. There is no `background` shorthand. A `.gradient` on the same element **replaces** the flat fill. The `background-image` slot is `.image` (a `const RC_Image *` from `rcLoadImage`): it draws behind the children like a CSS background, but it always **stretches to the element box**; there is no `repeat`/`position`/`size`. On such an element the flat `.bg` is still painted behind the picture *and* the same colour is multiplied into it as a **tint** (no `.bg` = untinted) |
| `background: linear-gradient(...)` | `.gradient` (`RC_Gradient`, 2-stop, dir `v`/`h`/`d`/`u`) | needs the element's `.id`; at most **`RC_GRADIENT_MAX`** (64) distinct elements per frame may carry one. Past the cap the element falls back to its flat `.bg` and the warning fires **once per process**, so a grid of 80 styled cards logs on frame 0 and then draws 16 of them wrong in silence. The constant is public so you can design against it; it is not tunable |
| `background` on `html` / `body` | `RC_AppOptions.clearColor` at create, `rcAppSetClearColor(app, c)` live | the **window surface behind your whole UI**: a different thing from `.bg`, and no element field reaches it. **Always opaque**: alpha 0 is an input *sentinel* meaning "the active style's background *at the moment of the call*", and every other alpha is discarded; RayClay never presents a see-through window. It is a **snapshot, not a live link**, so `rcSetStyle` does not move it and a dark/light toggle must call the setter too. It is also the only thing on screen for a frame RayClay holds back while it grows its layout arena; on that frame the clear colour *is* your app → [api-notes](api-notes.md) ▸ `rcAppClearColor` |
| `<title>` / `document.title` | `RC_AppOptions.title` | one field, both platforms: the OS window title on desktop AND the browser tab on web. You do not set it in your shell HTML. An EMPTY title is deliberately skipped rather than published, so a page that names itself in its own shell keeps that name instead of being overwritten |
| `box-shadow` | `.shadow` (`RC_Shadow`) | single-layer, linear falloff; needs `.id` + a fill; at most **`RC_SHADOW_MAX`** (64) per frame, same once-per-process warning; past the cap the shadow is simply not drawn |
| `opacity` (element/subtree) | `.overlay` (a subtree tint), else per-colour alpha | **no true opacity.** `.overlay = rcRgba(0,0,0,120)` tints a whole subtree (mixed over the children), which covers dim/scrim/wash, but it composites a colour ON TOP rather than making the subtree translucent, and it is square-cornered. For a single colour use 8-digit hex / `rcAlpha` |
| `transition` / `@keyframes` | - | **not in v1.0**: poll state and set values yourself. Your editor **will** offer you `.transition` on `RC_ElementDeclaration`; it is **visible but unsupported**. It freezes mid-curve under the default on-demand runner (a transition advances only on a frame that actually lays out, and nothing asks for the next one); the slot pool is a fixed **200 for the whole app**, which `maxLayoutElements` does not raise; and once your own declaration fills the layout arena to capacity, elements mid-*exit* stop animating for that frame (they resume when the scene fits again) |
| `:hover` / `:active` | poll `rcIsHovered` / `rcClicked` and branch | no declarative pseudo-states; the theme ships a `…Hover` token for each of the four accents (`primary`, `danger`, `success`, `warning`) for the ternary idiom |
| `font-family`/`weight`/`size` | `rcRegisterFont` + `rcFont` | exact-size match, no synthetic bold; text is ASCII + Latin-1 |
| `overflow` | `.overflow`: `"visible"` (default) · `"hidden"`/`"clip"` · `"scroll"`/`"auto"` | maps directly. Per-*axis* scroll is `.scroll` (`"v"`/`"h"`/`"b"`), and `.scroll` wins if you set both |
| `position: absolute` / `fixed` | `.floating` (`RC_Float`): `.to` + `.toId`, `.parent`/`.element` anchor points, `.offset` | anchor-based, not coordinate-based: you pin an anchor ON the target to an anchor ON yourself, then nudge. No `top`/`left` offsets from the viewport |
| `z-index` | `RC_Float.zIndex` (`int16_t`, higher draws above) | **only on a floating element.** In normal flow, order = declaration order; there is no `z-index` on an ordinary box |
| `white-space` / wrapping | `RC_TextOptions.wrap`: `""` words (default) · `"n"` none · `"l"` newlines-only | text only, and **this is the row where CSS habits mislead.** `"n"` and `"l"` suppress the break the *layout* would invent, never the break *you typed*; `white-space: nowrap` does both, collapsing a newline to a space. Measured both directions: in a box too narrow to hold it, `"alpha bravo charlie"` is 3 lines at `""` and **1 line at `"n"` and at `"l"`**; but `"alpha\nbravo\ncharlie"` is **3 lines under all three values**, `"n"` included. **If you want one line, put one line in the string.** `"n"` on a button label can push it past the layout cull edge and it VANISHES; keep labels short → [api-notes](api-notes.md) ▸ `RC_TextOptions` |
| `text-overflow: ellipsis` | - | **not in v1.0.** Clip with `.overflow = "hidden"`, or shorten the string yourself |
| `min-width`/`max-width` on an element | the `defaults` declaration you pass to `rcBeginComponent` | **not in the `rc` sizing DSL**: `.w`/`.h`/`.wType` have no min/max spelling, and `RC_AppOptions.minWidth`/`minHeight` are the **WINDOW** minimum. But it IS reachable: leave the axis unset and set `layout.sizing.<axis>.size.minMax` on the `defaults` declaration yourself. See the note under this table |
| `aspect-ratio` | the `defaults` declaration you pass to `rcBeginComponent` | **not in the `rc` DSL**, but reachable the same way: set `aspectRatio` on the `defaults` declaration and Clay derives the dependent axis. See the note under this table |
| `cursor` | `rcSetCursor(RC_CURSOR_*)`, last-writer-wins per frame | RayClay already auto-sets the expected shape per widget; `RC_AppOptions.autoCursorsDisabled` turns those defaults off |
| `letter-spacing` / Tailwind `tracking-*` | `RC_TextOptions.letterSpacing` | px |
| `text-align` | `RC_TextOptions.textAlign` (`l`/`c`/`r`) | Not to be confused with the container's `.align`, which is the two-letter `"<Y><X>"` form and means something else. Multi-line blocks only; centre a single line via the parent's `.align` |

### The escape hatch two rows above: fields `rc` never writes reach Clay untouched

`RC_ElementDeclaration` is a pass-through typedef of Clay's own `Clay_ElementDeclaration`, and
`rcBeginComponent(options, defaults)` forwards the whole struct. RayClay's option fold only
overwrites what you actually spelled: an axis you leave unset keeps whatever the `defaults`
declaration carried, and a field the `rc` DSL never touches at all (`aspectRatio` is one) simply
survives.

So the honest shape of the two rows above is **"not in the `rc` DSL"**, not "not in v1.0". Both
capabilities ship inside the single header today and a consumer can reach them:

```c
RC_ElementDeclaration d = {
    .layout      = { .layoutDirection = CLAY_TOP_TO_BOTTOM },   /* a column, the way rcBox stacks */
    .aspectRatio = { 16.0f / 9.0f },
};
d.layout.sizing.width.size.minMax = (Clay_SizingMinMax){ .min = 120.0f, .max = 480.0f };

rcBeginComponent((RC_ComponentOptions){ ._reserved = 0, .h = "grow" }, d);
    /* your children here */
rcEndComponent();
```

Every name in that declaration is one you are meant to write: `RC_` is RayClay's public surface, and
Clay's own `Clay_`/`CLAY_` types and constants are re-exported unchanged and appear in public
signatures. **A name beginning `rci_` or `RCI_` is not**; those are internal, a few are exported only
so the `rc` macros can reference them, and they can change in any release. You do not need one here.

`rcBeginComponent` takes the options and the defaults as **two separate arguments** on purpose:
folding both into one designated-initialiser list warns under `-Winitializer-overrides` at every
call site that overrides a default. `ex20` builds its whole `card(...)` macro on this pair.

**This is the raw Clay layer, deliberately.** It is not the `rc` string DSL, so it is not covered by
that DSL's validation, and a future RayClay spelling for either field may supersede it. Reach for it when you need the capability, not as a matter of course; `ex20` uses the
same `rcBeginComponent`/`rcEndComponent` pair for its custom element macro if you want a worked
example.

**The general rule, which is worth more than either row:** whether RayClay can express something
is a question about **what you can pass in**, not about which fields RayClay's own code happens to
set. A pass-through struct hands you every field of the type it wraps, including ones the library
never writes itself.

### Breakpoints: measure the *layout* width, not the window width

**`rcGetWindowDimensions()` is not your `100vw`.** It reports the real OS window size and the zoom
factor deliberately does **not** scale it, but the default zoom mode (`RC_ZOOM_LAYOUT`) reflows the UI
by laying it out into `window / zoom`. So on a 1400px window at 200% zoom your UI is really being laid
out into **700** logical px (tablet width, where a sidebar should collapse) while
`rcGetWindowDimensions().width` still says 1400 and your breakpoint never fires. Zoom is **on by
default**, so this is not an edge case; it is every user who has pressed `Ctrl` `-`.

Divide it out, and you have the equivalent of `100vw`:

```c
/* The width your layout is actually getting. RC_ZOOM_LAYOUT (the default) reflows
   into window/zoom; RC_ZOOM_OPTICAL magnifies a fixed surface instead, and only
   grows the logical space when zoomed OUT below 1.0. */
float vw = rcGetWindowDimensions().width;
float z  = rcAppZoom(app);

if (rcAppZoomMode(app) == RC_ZOOM_LAYOUT) {
    vw /= z;                                  /* reflow: the UI really is this wide */
} else if (z < 1.0f) {
    vw /= z;                                  /* optical zoomed out: more logical space than window */
}

if (vw < 768.0f) { /* narrow layout */ } else { /* wide layout */ }
```

If you only ever use the default mode (most apps), the whole thing is
`float vw = rcGetWindowDimensions().width / rcAppZoom(app);`.

**Sizing does not need this.** `"50vw"` / `"100%"` / `"grow"` are resolved inside the layout pass and
are already correct in both zoom modes; it is only *branching* in your own C that has to do the divide.

## Putting it together: a small app

Everything above in one ~50-line program: an `AppState` struct, a reusable component function,
controlled widgets, a conditional block, a list with stable ids, and your own colour tokens, driven by
`rcRunApp`. It compiles as-is on desktop and web, with no asset files:

```c
#include "rayclay.h"

/* project colour tokens: theme.extend.colors, RayClay-style.
   IN C, use #define rather than `static const RC_Color`: rcRgb expands to a
   compound literal, which is not a constant expression, so a file-scope
   static initialiser is rejected under -std=c99/c11 -pedantic-errors.
   (In C++ the same macro expands to a braced initialiser, which IS valid at
   file scope, so a C++ app may use either form.) */
#define BRAND   rcRgb(99, 102, 241)
#define SURFACE rcRgb(30, 41, 59)

typedef struct {
    bool  shuffle;
    float volume;                 /* 0..1 */
    int   selected;               /* index into TRACKS[] */
} AppState;

static const char *TRACKS[]    = { "Intro", "Nightfall", "Signals", "Afterglow" };
static const char *TRACK_IDS[] = { "t0", "t1", "t2", "t3" };
enum { TRACK_COUNT = 4 };

/* a reusable component: one list row, highlighted when selected */
static void track_row(AppState *st, int i) {
    RC_Style s = rcGetStyle();
    bool active = (st->selected == i);
    rcRow(.id = TRACK_IDS[i], .w = "grow", .p = 10,
          .bg = active ? BRAND : SURFACE, .borderRadius = "-md") {
        rcTextC(TRACKS[i], .color = active ? RC_WHITE : s.text);   // runtime string → rcTextC
    }
    if (rcClicked(TRACK_IDS[i]))
        st->selected = i;
}

static void layout(RC_App *app, void *user) {
    (void)app;
    AppState *st = (AppState *)user;
    RC_Style s = rcGetStyle();

    rcColumn(.w = "grow", .h = "grow", .bg = s.background, .p = 20, .gap = 12) {
        rcTextL("Now playing", .color = s.text, .size = 22);

        for (int i = 0; i < TRACK_COUNT; i++)          /* a list, stable ids */
            track_row(st, i);

        rcCheckbox("shuffle", "Shuffle", &st->shuffle);   /* controlled widgets */
        rcSlider("vol", &st->volume, 0.0f, 1.0f);

        if (st->shuffle)                               /* conditional block */
            rcTextL("Shuffle is on", .color = BRAND);
    }
}

int main(void) {
    AppState st = { .volume = 0.6f };
    RC_AppOptions opts = { .title = "Player", .layoutCallback = layout, .userData = &st };
    return rcRunApp(&opts);
}
```

Notice what is *not* here: no `malloc`, no destructor, no re-render bookkeeping. `st` lives on the stack
in `main`; every widget reads and writes it directly; the whole UI is rebuilt each frame from that one
struct. That is the entire RayClay model.

