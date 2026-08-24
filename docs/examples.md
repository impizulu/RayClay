# RayClay examples

Every example is **one C source** (`ex10`, `ex11`, `ex12`, `ex20`, `ex21` and `ex22` each add one
local header), carries **no asset files of its own** (the font is bundled and icons are procedural),
and builds unchanged for **desktop and web**.

The suite shares one `examples/assets/` folder for its icon headers, SVG sources, font faces and logo
art. Three demos read real files from it at run time, because reading real files is the thing they
demonstrate: `ex10` a PNG (`RC_Image`), `ex22` font faces (`rcRegisterFont`) and `ex24` SVG sources
(`rcLoadSvg`), and **each keeps working when the file is absent**, saying so in the UI rather than
failing. `ex11`, a desktop-only dev tool, reads a folder of SVGs *you* point it at.

They fall into three numbered ranges plus the benchmark suite:

- **`ex00`–`ex09`: learn RayClay.** `ex00` is the smallest possible app; `ex01`–`ex05` walk one
  GUI per decade, each a little richer than the last, so you can see both the *style* and the
  *complexity* of GUIs grow over time, and how RayClay expresses each.
- **`ex10`–`ex19`: RayClay dev tools.** `ex10` is a copy-paste gallery of every widget; `ex11` is
  a desktop app that converts SVG files into RayClay icon headers (a **tool demo, not a step you
  are expected to take**), and it swaps two of its own window-control glyphs, which is where the
  per-button `RC_TitlebarButtonIcons` override is shown; `ex12` is a live inspector / test harness
  that reads the runner's own metrics.
- **`ex20`–`ex29`: full demo apps.** Complete, realistic applications, single-source like the
  learning set rather than split like the bench suite. `ex20` is a system monitor; `ex21` is a data
  explorer; `ex22` is a documentation reader; `ex23` is an issue inbox; `ex24` puts the vector
  routes side by side.
- **`examples/bench/`: six full applications.** Realistic apps (a messenger, a notes app, a
  trading terminal, a photo gallery, a platformer, an operations dashboard) that double as the
  project's performance benchmarks. See [The benchmark suite](#the-benchmark-suite) below.

## The lineup

| Example | Era | What it is | What it teaches |
|---------|-----|-----------|-----------------|
| [`ex00_hello`](https://github.com/impizulu/rayclay/blob/examples/examples/ex00_hello/hello.c) | - | Two lines → a welcome window | `rcRunApp(NULL)`; the one-source promise, and the one startup warning that tells you the canvas is a fallback, not your app |
| [`ex01_1980s_gui`](https://github.com/impizulu/rayclay/blob/examples/examples/ex01_1980s_gui/main.c) | 1980s | A 1-bit desktop calculator | Styling raw boxes into buttons (`rcClicked`/`rcIsHovered`), a menu bar, a custom monochrome theme |
| [`ex02_1990s_gui`](https://github.com/impizulu/rayclay/blob/examples/examples/ex02_1990s_gui/main.c) | 1990s | A Windows 95 settings dialog | Tabs, checkboxes, radios, combos, sliders; building a two-tone 3D bevel by nesting boxes |
| [`ex03_2000s_gui`](https://github.com/impizulu/rayclay/blob/examples/examples/ex03_2000s_gui/main.c) | 2000s | An Aqua / Winamp media player | Gradients (`RC_Gradient`), drop shadows (`RC_Shadow`), sliders, a progress bar, a scrolling list |
| [`ex04_2010s_gui`](https://github.com/impizulu/rayclay/blob/examples/examples/ex04_2010s_gui/main.c) | 2010s | A flat Material tasks app | Cards, a floating action button (`RC_Float`), a live list with add + deferred delete |
| [`ex05_2020s_gui`](https://github.com/impizulu/rayclay/blob/examples/examples/ex05_2020s_gui/main.c) | 2020s | A modern dark SaaS dashboard | Theming, a sidebar + tabbed content, stat cards, soft shadows, a gradient accent |
| [`ex10_rayclay_widgets_gallery`](https://github.com/impizulu/rayclay/blob/examples/examples/ex10_rayclay_widgets_gallery/main.c) | - | Every widget in one screen | A reference: buttons, inputs, menus, a context menu, a modal dialog **and a non-modal inspector panel**, an image, scrolling, charts (including **16 series in one plot**, the cap), and **a 5,000-row virtualized table** (`rcVirtualList`) showing how a large dataset stays cheap; runs under the bundled default titlebar |
| [`ex11_rayclay_icon_converter`](https://github.com/impizulu/rayclay/blob/examples/examples/ex11_rayclay_icon_converter/main.c) | - | Convert SVGs → RayClay icon headers (desktop-only) | A real app built *with* RayClay: folder scan, live preview, batch export. **A tool demo, not a step in a normal workflow**: `rcSvg("art.svg", size, color)` draws the `.svg` directly, with no conversion step |
| [`ex12_rayclay_inspector`](https://github.com/impizulu/rayclay/blob/examples/examples/ex12_rayclay_inspector/main.c) | - | A live inspector / test harness | `rcBeginSplitPane`, `rcChart` + `rcSparkline` (live fps/frame-time), an `rcBeginTable` grid with a follow-the-tail log, `rcSetLogSink`, and buttons that provoke real library diagnostics. **Filter the log by level and source**, **export it, or both metric histories as CSV, to the clipboard** (`rcClipboardSet`, with a byte count and a preview), and open a **non-modal live panel** reading window size, zoom and mode, frame cost, arena occupancy and log depth straight from the public getters. The worked example of a tool where **every control changes what the app shows or produces**; widget-for-its-own-sake belongs in ex10. Its titlebar **folds away on the primary modifier + T**. It also carries a **SCHEDULER** panel reading `rcAppSchedStats`: `spurious / waits` (the wake-loop metric), the per-reason breakdown via `rcFrameReasonName`, and a **render-mode toggle**, because the counters are all a structural zero while an app draws every frame: the contrast is the lesson. The panel detects the mode from a **delta**, not a total, since the counters only ever climb. |
| [`ex20_system_monitor`](https://github.com/impizulu/rayclay/blob/examples/examples/ex20_system_monitor/main.c) | - | A live system monitor | The **branded-window** example: a custom animated titlebar (`rcWindowControlButton`, `RC_ID_WINDOW_DRAG` / `RC_ID_WINDOW_NODRAG`) that folds to a drag rail on the primary modifier + T. Also the worked **custom element macro** (`rcBeginComponent` / `rcParseComponentOptions` / `rcEndComponent`), a **sortable** virtualized table, and the polled-deadline **on-demand sampling** pattern. Real readings for its own process; a simulated host behind one swappable file |
| [`ex21_data_explorer`](https://github.com/impizulu/rayclay/blob/examples/examples/ex21_data_explorer/main.c) | - | An analytics workbench over a 12,000-row catalogue | The **one dataset, many views** shape every data app has. Filters and sorts permute an **index array** (never the records), and a table, three charts and a summary all read that one selection. Also: **caching derived data on a change rather than per frame** (the app prints its own rebuild count, so you can watch it not move), **sampling a scatter** instead of asking the renderer for 12,000 discs, and **selectable virtualized rows** built from `rcRow` because `rcTableRow()` takes no id |
| [`ex22_docs_reader`](https://github.com/impizulu/rayclay/blob/examples/examples/ex22_docs_reader/main.c) | - | An offline documentation reader | The **typography** example. Registers a real family ladder with `rcRegisterFont` (**one file per weight**, the mistake that costs an afternoon) and resolves it with `rcFont`. Derives its reading column from `rcMeasureText` × a target character count rather than a hardcoded width, and folds the table of contents away when the window cannot hold it. **Degrades to the bundled face and still exits 0 when its fonts are absent**, so the exit code cannot tell you they loaded; the on-screen status line can |
| [`ex23_issue_inbox`](https://github.com/impizulu/rayclay/blob/examples/examples/ex23_issue_inbox/main.c) | - | A triage inbox over 100,000 issues | The **rail / list / detail** shape every desktop app is built on, at a size where the naive version stops working. A **100,000-row** `rcVirtualList` declares ~30 elements, so drawing costs what the *window* costs, not what the dataset does; filtering permutes an **index array** and never touches a record; the scan runs on a **change, not a frame** (the rail prints its own rebuild counter, so you can watch it not move while you drag the window); and a row carries **no strings at all**: 20 bytes of indices into shared word tables, with the title composed where it is drawn. Also the worked **arrow-key list**: `rcGetScrollInfo` + `rcScrollBy` keep the keyboard selection on screen, and the selection survives a refilter **by identity** rather than by row |
| [`ex24_svg_live`](https://github.com/impizulu/rayclay/blob/examples/examples/ex24_svg_live/main.c) | - | The three vector routes, side by side | The choice a developer actually has to make, made visible: the **same artwork** drawn three ways at once, **by path** (`rcSvg("…/settings.svg", size, color)`, the default: no app state, no load, no unload), **from a handle you own** (`rcLoadSvg` / `rcLoadSvgFromMemory` → `rcSvgHandle`), and from a **generated header** (compiled in, nothing to ship, cannot fail at run time). All three land in the same draw path, so the three panels are indistinguishable, which is the point: the choice is about your *build*, not about how it looks. The **Unload** button is the lesson in one click: it frees the handle you own while the path panel keeps drawing, because the library owns that one. Also live re-tint (`colour is a per-call argument`), a size slider, and one rail entry whose markup lives **only in the C source**: nothing for `rcSvg` to point at, which is exactly when you reach for a handle |

## The decade walk

The point of `ex01`–`ex05` is that a good-looking GUI is a moving target. As hardware and taste
changed, so did what "modern" meant, and the feature set of a typical app grew with it. The set
reads as one story:

1. **1980s: 1-bit.** Black on white, hard rectangular buttons, no rounding, no gradients, no
   shadows. A menu bar and a number pad. This is the whole visual vocabulary of the era, and it maps
   to a handful of `rcBox`es with 1px borders.
2. **1990s: beveled gray.** The battleship-silver 3D look: raised buttons, sunken wells, a solid
   blue caption. More widgets appear (tabs, radio groups, combos). RayClay has a single-colour
   border, so the two-tone bevel is built by nesting a light edge over a dark one: a good lesson in
   composing effects the primitives don't give you directly.
3. **2000s: glossy.** Skeuomorphism arrives: vertical gradients on every button, subtle rounding,
   soft drop shadows, saturated aqua blues. `RC_Gradient` and `RC_Shadow` (each needs an `.id`) do
   the heavy lifting; sliders and a progress bar drive a working media player.
4. **2010s: flat.** The reaction: bold flat colour, generous whitespace, cards, and Material's one
   concession to depth (a soft shadow and a circular floating action button). `RC_Float` pins the
   FAB to a corner; a card list adds and deletes tasks.
5. **2020s: modern.** Dark mode, muted palette with an accent, rounded-xl cards, soft large
   shadows, a sidebar-and-content shell with live tabs. This is the current house style, and the
   richest example.

## The icon converter (ex11)

> **You almost certainly do not need this.** Using your own artwork is `rcSvg("art.svg", 24.0f,
> s.text)` in your layout: one line, no handle, no conversion step in a normal workflow.
> ex11 is here as a **real app built with RayClay**, and for the advanced case where you want the
> parser out of your binary entirely (see `ex24_svg_live` for the comparison). Read it as a tool
> demo, not a rite of passage.

`ex11_rayclay_icon_converter` is a dev tool built *with* RayClay (dogfooding): point it at a folder
of SVGs, preview any icon live (rendered through the exact same path a generated icon uses, so it is
WYSIWYG) and export RayClay icon headers (`rc_icons_<name>.h`), one at a time or the whole folder at
once. It bundles `rc_svg2icon.h`, a self-contained C SVG parser (paths, arcs, béziers,
rects/circles/ellipses/polylines, stroke *and* multi-colour), so the whole conversion workflow is a
GUI. **It vendors that parser because it needs the EMITTER** (writing a
`.h`), which the public API does not expose. If you only want to *draw* an SVG, you want `rcSvg`
and `ex24_svg_live`, not this. It is **desktop-only** (it reads and writes the filesystem and
batch-converts) and, by design, the one example that steps outside the pure-`RC_` rule: a
deliberate, documented exception for a real-world dev tool.

```bash
cmake --build build-desktop --target rayclay_ex11_rayclay_icon_converter
./build-desktop/rayclay_ex11_rayclay_icon_converter path/to/svgs   # or use the in-app path field
```

## The benchmark suite

`examples/bench/` holds six applications that are deliberately bigger than the teaching examples.
Each is a plausible real app rather than a widget demo, and most double as a **performance
benchmark**: the same source is both the public showcase and the frame the test suite measures.

| App | Target / page | What it exercises |
|-----|---------------|-------------------|
| `messenger` | `rayclay_bench_messenger` · `messenger.html` | A chat client: conversation list with live search, a collapsible sidebar, content-sized message bubbles, a profile drawer |
| `notes` | `rayclay_bench_notes` · `notes.html` | A notes app: a note list, an editable body, tag chips |
| `trader` | `rayclay_bench_trader` · `trader.html` | A trading terminal: a live chart, an order book, a portfolio tab, symbol search (the densest layout in the suite) |
| `gallery` | `rayclay_bench_gallery` · `photos.html` | A photo gallery: an aspect-fit thumbnail grid and a detail view, across 12 procedurally-generated bitmaps decoded once through the real `rcLoadImageFromMemory` path, so it exercises decode + GPU upload while staying zero-asset |
| `platformer` | `rayclay_bench_platformer` · `platformer.html` | A game loop: per-frame animation and input, rather than a document-shaped UI |
| `opsdash` | `rayclay_bench_opsdash` · `opsdash.html` | An operations dashboard: a 48-service inventory that never changes under a telemetry band that changes every frame (the mostly-static screen most real tools are) |

Two consequences worth knowing before you edit one:

- **They are structured differently from the single-source examples.** Each is a small set of files
  * `main.c` (the entry point), `<app>_app.c` (the GUI), `<app>_app.h` (the contract),
  `<app>_backend.h` (the data), so the UI can be driven by a deterministic fake backend under test
  and by live data in the demo.
- **Their frames are frozen.** The benchmark compares against a recorded reference frame, so a
  change that moves the layout requires re-recording the baseline. Each app carries an
  `<APP>_BENCH_VERSION` that is bumped when that happens.

If you are looking for a starting point to copy, prefer `ex05` or `ex10`: same API, none of the
benchmark machinery.

## Build & run any example

Each example is a CMake target named `rayclay_<dir>` (e.g. `rayclay_ex03_2000s_gui`).

```bash
# Desktop: in any checkout that carries examples/
cmake -B build-desktop
cmake --build build-desktop --target rayclay_ex03_2000s_gui
./build-desktop/rayclay_ex03_2000s_gui
```

> **The public distribution splits the library from the demos, so check which one you cloned.**
> `main` carries the single header alone; the example sources live on the **`examples` branch**:
>
> ```bash
> git clone --branch examples <repo>
> ```
>
> A configure on a branch without them prints `RayClay: no examples on this branch - building the
> library only`, so the build tells you rather than leaving you to wonder why `cmake --build`
> succeeded and produced nothing to run.

**Web pages for the bundled examples build from any checkout that carries `examples/`**, which
registers each one with its `rc_web_example()` helper and ships the page shell they link against:

```bash
cmake --preset web                                   # the emscripten toolchain
cmake --build build-web --target rayclay_ex03_2000s_gui
python3 -m http.server 8080 --directory build-web    # open the page below
```

> **This builds RayClay's *bundled demos* into pages.** Building *your own* app for the browser is a
> different and much shorter recipe: a dozen link flags, given in full in
> [getting-started.md](getting-started.md#4-build-for-the-web--the-same-source).

Web pages are named for what they show, not for their target, so **`index.html` is the demo hub**:
a landing page linking to every demo. The full mapping:

| Target | Page | | Target | Page |
|--------|------|-|--------|------|
| `ex00_hello` | `hello.html` | | `ex12_rayclay_inspector` | `inspector.html` |
| `ex01_1980s_gui` | `gui1980s.html` | | `bench_messenger` | `messenger.html` |
| `ex02_1990s_gui` | `gui1990s.html` | | `bench_notes` | `notes.html` |
| `ex03_2000s_gui` | `gui2000s.html` | | `bench_trader` | `trader.html` |
| `ex04_2010s_gui` | `gui2010s.html` | | `bench_gallery` | `photos.html` |
| `ex05_2020s_gui` | `dashboard.html` | | `bench_platformer` | `platformer.html` |
| `ex10_rayclay_widgets_gallery` | `gallery.html` | | `ex20_system_monitor` | `sysmon.html` |
| `ex21_data_explorer` | `explorer.html` | | `bench_opsdash` | `opsdash.html` |
| `ex22_docs_reader` | `reader.html` | | `ex23_issue_inbox` | `inbox.html` |
| `ex24_svg_live` | `svg.html` | | | |
| *(the hub)* | `index.html` | | | |

`ex11` has no web page: it is desktop-only.

## Headless / CI

Any example bounds itself to N frames and exits 0 when `RAYCLAY_MAX_FRAMES` is set, a one-line
smoke test with no code change:

```bash
RAYCLAY_MAX_FRAMES=3 ./build-desktop/rayclay_ex01_1980s_gui   # opens, draws 3 frames, exits 0
```

**A frame budget forces `RC_RENDER_CONTINUOUS`**, so this smoke-tests a different render mode than the
example ships with. That is correct (a budget only means something if the frames actually happen), but it
means on-demand behaviour (idle CPU, parking) cannot be observed this way. To bound a run and stay on the
default path, bound by time instead: `RAYCLAY_MAX_SECONDS=3 RAYCLAY_RENDER_MODE=ondemand ./…`.
Full explanation → [getting-started.md](getting-started.md) ▸ Headless / CI.

## House rules (if you copy an example as a starting point)

- **Pure `RC_` API.** Examples call only the public `RC_` API and name only `RC_` types
  (`RC_Color`, `RC_String`, `RC_Dimensions`, `RC_Vec2`, `RC_BoundingBox`); no layout-engine calls, no
  libc includes needed. That includes a custom element's draw callback: `RC_CustomDrawCallback` takes an
  `RC_BoundingBox`. This is gated rather than aspirational: a check in RayClay's own CI fails the
  build if an example names a non-RayClay type or reaches for a system include.
- **Zero-asset.** The font is baked from the bundled face via `RC_AppOptions.fontSizes[]`; icons are
  procedural, compiled in from the shared `examples/assets/icons/` headers, so for most of the suite
  there is nothing to ship alongside the binary. The three that deliberately read real files
  (`ex10` a PNG, `ex22` font faces, `ex24` SVG sources) each degrade to working without them.
- **One source, both targets.** No `#ifdef` in your code: `.nativeFrame` (borderless + the bundled
  titlebar, drawn by the runner) is simply ignored on the web, where the browser owns the frame.
  The decade GUIs set `.titlebar.custom = true` and hand-roll their period bars instead; `ex10`
  and `ex11` run under the default bar. On the web the period bars still render, deliberately, as
  inert period art (titlebars are desktop-only, so every `RC_ID_WINDOW_*` verb is a no-op there).
