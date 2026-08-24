# RayClay on the web (WebAssembly)

> **Scope: this page is about building RayClay's own bundled `examples/` into web pages.** A RayClay
> checkout registers each example with `rc_web_example()` and supplies the page shell they link
> against. **Building *your own* app for the browser needs none of it**: that is a dozen link flags,
> given in full in
> [getting-started.md](getting-started.md#4-build-for-the-web--the-same-source).

**Status:** working. `cmake --preset web` builds the `examples/` programs into runnable web pages you can
host and open in any browser: the six bench/showcase apps, the `gui1980s`–`gui2010s` decade walk, the ex10
widgets gallery, the ex05 dashboard (`dashboard.html`), the ex12 live inspector (`inspector.html`), the
ex20 system monitor (`sysmon.html`), the ex21 data explorer (`explorer.html`), the ex22 documentation
reader (`reader.html`), the ex23 issue inbox (`inbox.html`), the ex24 SVG showcase (`svg.html`), and
the two-line welcome canvas (`hello.html`). Same source as desktop; the
platform is selected at build time, never forked.

Every one sits behind a hub landing page, `examples/web/hub.html`, which the build copies to
`index.html`, so a local build and the deployed site (GitHub Pages) have the same front door. See §2.

This is the practical "how do I build, host, and test RayClay as a web app" guide. For the *why*, see
§6 "How it works" below.

---

## 1. Prerequisites: the Emscripten SDK

The web build needs [emscripten](https://emscripten.org) (the C/C++ → WebAssembly toolchain). One-time install:

```sh
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install 6.0.2 && ./emsdk activate 6.0.2
source ./emsdk_env.sh            # sets $EMSDK + puts emcc on PATH (re-run per shell)
emcc --version                   # expect 6.0.2
```

`source emsdk_env.sh` exports `$EMSDK`, which the `web` CMake preset uses to find the toolchain file: no
hard-coded paths.

> **Pin the toolchain: install `6.0.2`, not `latest`.** RayClay is tested against the **6.0.x** series and
> CI pins **6.0.2**. The version is load-bearing, and it has bitten in both directions:
> - **Too old:** on a HiDPI display the pointer is correct *only* because emscripten's GLFW shim overrides
>   `Browser.calculateMouseCoords` to report canvas-relative **CSS** pixels, the same space the layout engine lays out and
>   hit-tests in. RayClay feeds `glfwGetCursorPos` straight through with no DPR compensation, so on a shim that
>   reports device pixels every click lands in the wrong place at `devicePixelRatio ≠ 1`.
> - **From 6.0.2 onward:** emcc turned `GROWABLE_ARRAYBUFFERS` on by default, which Chrome's WebGL rejects, so a
>   page renders blank unless the build pins it off. This is not a reason to install something older:
>   the pin is `-sGROWABLE_ARRAYBUFFERS=0`, it is in every RayClay web target, and `getting-started.md` §4
>   gives it for your own app.
>
> Nothing in the build enforces a version yet, so a drifting emsdk fails **silently**: it still compiles.

## 2. Build the `examples/` into web apps

*(Any checkout that carries `examples/` - see the scope note at the top of this page.)*

```sh
cmake --preset web               # configures build-web/ with the emscripten toolchain
cmake --build --preset web       # compiles the library + every web-registered example to WebAssembly
```

> **Build via the preset, never `cmake --build build-web -j`.** The presets carry an explicit job count.
> A bare `-j` (or a bare `--parallel`) is an *unbounded* `make -j`: GNU make then spawns every job the
> dependency graph allows (not one per core), which is enough concurrent compilers to exhaust RAM and
> take the machine down with it. Pass a number if you drive the directory directly: `--parallel 4`.

Output lands in `build-web/`: one `<name>.{html,js,wasm}` set per example (and a `.data` only where an
example preloads assets), plus `index.html`, the hub. **One page per web-eligible target: every
`examples/ex*/` app except `ex11`** (the SVG→icon converter, a desktop-only dev tool that does batch
filesystem I/O the Emscripten VFS does not model), **plus the `examples/bench/` apps.**

**Do not trust a page COUNT written here; count the build:** `ls build-web/*.html | wc -l`. The set
grows whenever an example is added, and a number in prose cannot follow it. The bijection between
`examples/ex*/` and the web targets is enforced by a check in the development repository, which
also refuses an exclusion that gives no reason.

| File | What it is |
|---|---|
| `dashboard.html` | the ex05 2020s dashboard, the showcase app (hosted in RayClay's shell page) |
| `hello.html`   | the ex00 two-line welcome canvas (no assets) |
| `gui1980s.html` … `gui2010s.html` | the ex01–ex04 decade GUIs (no assets) |
| `gallery.html` | the ex10 widgets gallery (`gallery.data` holds its one preloaded PNG at `/assets`) |
| `inspector.html` | the ex12 live inspector / test harness: split pane, live fps + frame-time charts, a table log (no assets) |
| `sysmon.html` | the ex20 system monitor: sortable virtualized process table, live charts, and a custom titlebar. **The band is drawn but inert here:** `rcWindowControlButton` and the `RC_ID_WINDOW_*` ids emit nothing on web, because the browser tab is the chrome |
| `explorer.html` | the ex21 data explorer: a 12,000-row catalogue behind filters, three linked charts and a sortable virtualized table (no assets) |
| `reader.html` | the ex22 documentation reader, the typography example: `reader.data` carries its three font faces plus the demo logo PNG at `/assets` |
| `inbox.html` | the ex23 issue inbox: 100,000 rows behind a live filter, with a rail and a detail pane (no assets). Its titlebar band is drawn but inert here, for the same reason as `sysmon.html` |
| `svg.html` | the ex24 SVG showcase: the vector routes side by side; `svg.data` carries the eleven `.svg` sources it reads at run time at `/assets/icons` |
| `messenger.html` `notes.html` `trader.html` `platformer.html` `photos.html` `opsdash.html` | the six bench/showcase apps, the same sources that serve as the perf benchmark. The bench image viewer ships as **`photos.html`** so it does not collide with ex10's `gallery.html` |
| `index.html`   | the hub, staged from `examples/web/hub.html`. The build FAILS if a hub link has no page |
| `<name>.js`    | the runtime/loader glue for that page (instantiates the wasm) |
| `<name>.wasm`  | the compiled code (the library + that example) |

A wasm is **not** standalone: it needs its `<name>.js` (and `<name>.data` when present) beside it, so
always ship/serve the whole set. **Three pages ship a `.data`:** `gallery.html` (ex10's one preloaded
PNG), `reader.html` (ex22's three font faces plus the demo logo PNG) and `svg.html` (ex24's eleven
`.svg` sources). Everything else is fully procedural:
bundled font plus procedural icons, and the bench image viewer generates its images at runtime rather
than shipping any.

**Wasm size is a property of the BUILD TYPE, so measure yours rather than quoting a range:**
`ls -l build-web/*.wasm`. A `Release` build and a `MinSizeRel` build of the same page differ enough that
a number copied from the wrong one will mislead you about your own download.

> Driving the directory yourself needs the preset's cache variables spelled out; they are not cosmetic:
>
> ```sh
> emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=MinSizeRel -DRC_BUILD_TESTS=OFF \
>              -DRC_BUILD_EXAMPLES=ON
> cmake --build build-web --parallel 8
> ```
>
> **`RC_BUILD_TESTS=OFF` is load-bearing.** The test suite links the vendored GLFW, which an emscripten build
> never builds (the browser target uses emscripten's own GLFW shim), so leaving tests on fails configuration
> outright with `Target "glfw" not found`. Nothing is wrong with your toolchain when that happens.

### The hub (the deployed landing page)

`examples/web/hub.html` is a plain, self-contained page (no external fonts, scripts or stylesheets) that
links every built demo. **The build copies it to `build-web/index.html`**, so the hub is the landing page both
locally and on the deployed site; local and deployed are the same tree, and a broken hub link fails where
you can see it rather than after a deploy.

**Page names are set at BUILD time** (`rc_web_example()` in `CMakeLists.txt`), never by renaming a file
afterwards. emcc bakes the wasm filename into the generated `.js`, so a copy-time rename leaves e.g.
`dashboard.js` still fetching `index.wasm` and the page dies with a blank canvas.

## 3. Host it locally (you can do this right now)

A wasm app **must be served over HTTP**: browsers block `wasm`/`fetch` on `file://`. RayClay is
single-threaded, so **no special COOP/COEP headers are needed**; any static server works:

```sh
# Option A: Python (the universal fallback, no extra deps)
cd build-web && python3 -m http.server 8080
#   then open http://localhost:8080/            (index.html is the hub; every app is linked from it)

# Option B: emrun (ships with emsdk; also used for headless CI smoke, §5)
emrun --no_browser --port 8080 build-web/dashboard.html
```

Open the URL and the dashboard renders in the canvas, redrawing **on demand**: input, resize and
device-pixel-ratio changes wake it, and an idle page draws nothing (see `RC_RenderMode`; the animation
frame stays registered, but a tick nothing asked for returns without laying out or rendering). Don't
benchmark the page or write a test wait expecting a steady 60 fps. Otherwise: the same widgets, fonts, gradients,
shadows and icons as the desktop build. **A local build emits `index.html` too**: the demo hub is copied
into `build-web/` at build time, so serving the directory and opening `/` lands on the same hub page the
public site does. Local and deployed are the same tree; there is no deploy-only step to get wrong.

## 4. Responsive layout: desktop / tablet / phone

RayClay draws the whole UI into **one WebGL canvas**, not a DOM tree, so the canvas fills the viewport and
the C side re-lays-out each frame. On any window/device resize, a C-side callback
(`emscripten_set_resize_callback`, inside RayClay's window backend) pushes the new CSS size into GLFW, so the **layout
flexbox re-flows** to the new viewport. (GLFW's logical window size stays CSS px; the layout engine lays out and
hit-tests there, while the WebGL backing store is sized to `cssPx × devicePixelRatio`, so vector
content rasterises at native device resolution; see §7.)

To test the responsive story:
- **In a browser:** open DevTools → device toolbar (Ctrl+Shift+M) → pick iPhone / iPad / a desktop size, or
  just drag the window; the layout adapts live.
- **Headless (Playwright):** `page.setViewportSize({width, height})` (or `devices['iPhone 13']`) +
  `toHaveScreenshot()` per viewport. Verified manually at 1366×768 (desktop two-column) and 390×844 (phone
  single-column): the top-level layout re-flows correctly.

> **Note, what does NOT apply:** because the UI is a single canvas (no DOM reflow), the web-vitals **CLS**
> (Cumulative Layout Shift) metric is N/A here. Don't run a CLS audit, it measures the wrong thing.

## 5. Testing the WASM build

The standard pyramid for a C/wasm canvas app, cheapest → richest. RayClay already owns the first two:

| Layer | Tool | Asserts | Cost to you |
|---|---|---|---|
| 1. Artifact validation | `wasm-validate` (WABT) | the `.wasm` is a valid module | one command against the file you just built - below |
| 2. Headless logic smoke | Node runs a GL-free build of your logic; exit code = pass/fail | your layout and widget code runs correctly on wasm32 | a second link target, no browser |
| 3. Browser smoke | a real browser drives the page | the *rendered* page lives: canvas sized, WebGL2 ok, no `abort()` | a headless browser in CI |
| 4. Pixel/visual diff | Playwright `toHaveScreenshot` per viewport | screenshot vs baseline | advisory (GPU/AA variance) |

**Layer 3 is the one that catches what the others cannot.** An exit code proves a page did not `abort()`; it
does **not** prove the page is not blank, and it does not prove the page is quiet. A build that compiles, exits
0, and paints an empty canvas passes layers 1 and 2, so the render check runs *before* the deploy, not after.
Two assertions are enough:

```js
canvas.width > 0 && canvas.getContext('webgl2') !== null   // it actually has a live surface
consoleErrors.length === 0                                 // and it is not screaming
```

Layer 1 is a single command against the artifact your build just produced, and it is the cheapest
thing in this table to wire into CI:

```sh
wasm-validate build-web/myapp.wasm    # silent, exit 0 when the module is valid
                                      # names the byte offset and exits 1 when it is not
```

Layers 1–2 prove the **logic** is byte-correct on wasm32 (no GPU). Layer 3 is the first tier that proves the
**rendered** app, and it is the one worth building next: it is the only layer that can tell a live page
from a blank one.

### Seeing RayClay's diagnostics in the browser

RayClay reports mistakes it can detect at runtime (a bad sizing unit, an unparsable `.align`, a `break`
out of an element body) by writing to `stderr`. On the web that is **not** a dead end: Emscripten routes
`stderr` through `Module.printErr`, so the warnings reach the browser console with the same text they
have on the desktop. **Open devtools and they are there.**

THAT ROUTE IS YOURS TO KEEP OPEN. `printErr` belongs to the page shell, not to RayClay, so whether a
diagnostic is visible depends on the shell YOU ship. One line in your `Module` guarantees it:

```js
printErr: function (t) { console.error(t); }   // put this in your shell's Module
```

Verified end-to-end with that wiring in place: a `fprintf(stderr, ...)` from wasm arrives as a
`console.error` entry in the page. The practical failure mode is not invisibility; it is simply
**not having devtools open**, which is worth ruling out before you conclude a warning "doesn't fire on
web".

`rcSetLogSink(fn, user)` is for sending diagnostics *somewhere else* (an in-page log panel, a toast,
telemetry), not for making them visible in the first place. While a sink is installed, `stderr` is not
written, so a sink **replaces** the console output rather than adding to it; call `rcSetLogSink(NULL, NULL)`
to restore it.

## 6. How it works (the three things that make web possible)

1. **Loop inversion.** A browser tab can't run a blocking `while` loop. `rcRunApp` is built from
   `rcAppCreate` / `rcRunFrame` / `rcAppDestroy`; on `__EMSCRIPTEN__` it hands `rcRunFrame` to
   `emscripten_set_main_loop` (requestAnimationFrame) instead of looping. The same `main.c` is unchanged.
   **`rcRunApp` therefore does NOT return on web**: `emscripten_set_main_loop` unwinds the C
   stack and the browser drives the frames. Code you write after `rcRunApp` in `main()` runs on
   desktop and never in a browser; put teardown in `RC_AppOptions.frameEndCallback`, or own the loop.
2. **Build-time platform select.** The root CMake skips desktop OpenGL / the vendored GLFW / X11+nobar under
   `if(EMSCRIPTEN)`; sokol uses its **GLES3 → WebGL2** backend, and the example links with `-sUSE_GLFW=3`
   (emscripten's GLFW shim), `-sFULL_ES3`, `-sSTACK_SIZE=8MB` (the layout recurses), asset preload, and our
   own shell page.
3. **One window backend, two targets.** The GLFW backend doubles as the web backend through emscripten's
   GLFW shim; the few desktop-only calls (window centring, resize cursors) are gated out under `__EMSCRIPTEN__`.

## 7. Known limitations (web, this iteration)

- **No native window chrome.** Titlebars are desktop-only: nobar compiles out, the bundled titlebar is
  not drawn on web, and `rcTitlebar`/`rcWindowControls`/`rcWindowControlButton` emit nothing there.
  An example's hand-rolled bar still renders as ordinary layout, and every `RC_ID_WINDOW_*` verb,
  close included, is inert (the page chrome is the browser's). This is correct: `nativeFrame` has no
  meaning in a tab.
- **The clipboard works on the web out of the box: copy and paste both, with no setup.** RayClay's web
  backend is the browser's own clipboard: a copy calls `navigator.clipboard.writeText`, and a read calls
  `readText()` and resolves that promise back through the token protocol. It deliberately does NOT
  go through emscripten's GLFW shim, which stubs `glfwGetClipboardString`/`glfwSetClipboardString` to
  return 0; that seam is dead. You read with `rcClipboardRequest` +
  `rcClipboardPoll`, or let the bundled text field do it.
  **`rcClipboardGet` returns `NULL` on the web, and that is not a failure signal.** It can only answer
  under a *synchronous* backend, and a browser read is a promise. **Do not feature-detect the clipboard with
  it**: you will report "no clipboard" on a platform where the clipboard works perfectly.
  **Two browser rules travel with this, and neither is RayClay's to relax.** The Clipboard API exists only
  in a **secure context** (https, or `localhost`), so a page served over plain http to a LAN address has
  none at all. And a **read** additionally needs a user gesture or a granted permission, so issue it from
  inside a click handler rather than at startup. When the API is absent or the read is refused, RayClay
  **delivers a denial** instead of throwing: the paste resolves as "no text" and a UI waiting on it stops
  waiting. Through the public API an empty clipboard and a refused read are indistinguishable.
  You can still install your own `RC_ClipboardImpl` via `rcSetClipboardImpl()` to **override** the default:
  the struct is copied **by value**, so a stack local may go out of scope; only `user` stays borrowed. Its
  `request(user, token)` starts your read and calls `rcClipboardDeliver(token, text)` when it resolves,
  echoing the same token back. Cursors are also weaker under the shim.
  **You do not have to wake the app yourself, with the default backend or your own.** Under
  on-demand rendering an app can be parked when the promise resolves, which would leave a correct delivery
  invisible until the user happened to move the mouse. `rcClipboardDeliver` therefore requests a frame as
  part of delivering, including on the denial path, so a UI waiting on the answer is woken to observe a
  refusal too. A custom `request` callback only has to call `rcClipboardDeliver(token, text)`; echo the
  token back and RayClay does the rest. (The hook lives from `rcAppCreate` to `rcAppDestroy`, so it covers a hand-rolled `rcRunFrame`
  loop as well as `rcRunApp`.)
- **A lost WebGL context needs a page reload.** If the browser drops the GL context (a GPU
  reset, or the tab backgrounded/starved too long), RayClay cannot repaint: the textures it uploaded
  are gone from the GPU and the CPU-side image bytes were freed after upload. The page shell shows an
  honest "reload to continue" card; automatic context restoration is planned for a later release.
  Desktop has no equivalent: the GL context lives with the window.
- **Antialiasing is a request on desktop and a *hint* on web, and the browser may simply decline.**
  Desktop GL negotiates a sample **count**, so RayClay asks for `RC_GFX_MSAA_SAMPLES` and compares what
  it got. Emscripten's GLFW shim has no count to negotiate: it maps `GLFW_SAMPLES` onto the WebGL
  `antialias` context attribute, a **boolean**, and the WebGL spec explicitly lets an implementation
  ignore it. **An outright refusal is reported**: one warning, in the console, alongside every other
  RayClay diagnostic (see *Seeing RayClay's diagnostics in the browser* above).
  **Silence on web means "some antialiasing", not "four samples".** A browser that gives you 2 where
  you asked for 4 stays quiet on purpose: the count there is the browser's choice, so treating it as a
  disagreement would fire for your users and never for you.
  **What it means for your UI:** the fallback is legible, not broken; rounded corners, chart lines
  and icon strokes are harder-edged. **Text is unaffected on every platform** (it is sampled from an
  already-antialiased atlas). Design so 4× edges are a polish layer rather than the thing that makes
  the screen readable, and the web fallback costs you nothing you cannot afford.
- **Zoom is the browser's.** The desktop zoom *gestures* (Ctrl +/-/0/wheel) are inert on web: the
  browser owns page zoom in a tab, and Ctrl+wheel over the canvas is deliberately passed through to it
  while `rcAppSetZoom()` still applies programmatically, clamped to the app's configured
  `RC_AppOptions.zoom` `[minZoom, maxZoom]` range and honouring `.mode` (a reflow in the default
  layout mode), and `rcGetContentScale()` returns the display's true device-pixel-ratio there
  (e.g. `2.0` on a 2× display).
- **But `.zoom.pan` *is* live on web, and the asymmetry is deliberate.** Drag-to-pan (hold Space, drag
  with the left button) behaves exactly as it does on desktop. The zoom *keys* are excluded here because
  the browser already owns `Ctrl` `+`/`-`/wheel and intercepting them would double-zoom; **no browser owns
  a space-drag**, and `rcAppSetZoom()` + `RC_ZOOM_OPTICAL` do work on web, so excluding the pan would
  hand a web build a magnified view with no way to reach its own edges, which is the one-source promise
  breaking on the platform least able to work around it.
- **HiDPI renders at native device resolution.** The WebGL backing store is sized to `cssPx ×
  devicePixelRatio` (GLFW's logical size stays CSS px, where the layout engine lays out and hit-tests), so all vector
  content (borders, icons, rects, components) rasterises at the display's true device resolution and
  **re-crisps on a browser page-zoom** instead of being bilinearly upscaled from one 1× bitmap. Glyph text
  follows it: `contentScale` carries the browser's device-pixel-ratio *and* its page zoom, so once that
  settles the runner re-bakes the glyph atlas at the new density and settled text is native-resolution here
  exactly as on the desktop, bounded by the atlas's capacity, where the re-bake settles on the highest
  density the sheet can hold (the same one however fast you drove the zoom) and text goes slightly soft
  rather than missing. Only the few frames before the re-bake lands show the
  previous bake magnified; `RC_AppOptions.fontOversample` smooths that transient, and also replaces the
  automatic oversampling policy, at a real cost in zoom ceiling (see
  [getting-started](getting-started.md)). The GLFW shim's own HiDPI path (`GLFW_SCALE_TO_MONITOR`) is left
  off because it conflicts with CSS-driven canvas sizing (it mis-sizes the first frame); RayClay drives the
  device-px backing store directly instead.
- The GLFW-shim path already drives resize **and** the device-px backing store from C, reusing the desktop
  window/input code. A sokol-`sapp` web backend that owned the resize/DPI handler natively is a possible
  future refinement, but it is **not required** for native-resolution HiDPI: the shim path already
  delivers that.
