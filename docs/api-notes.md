# RayClay API notes: the measurements behind the cheatsheet

`docs/cheatsheet.md` is a **quick reference**, in the shape of the
[raylib cheatsheet](https://www.raylib.com/cheatsheet/cheatsheet.html) that inspired it: one line per
entry, a short trailing comment, and nothing you have to read twice. This page is where the long-form
material lives (the measurements, the traps, the "why is this number what it is"), so the card can stay
a card.

**Everything here was measured, not reasoned.** Where a figure is quoted, the box, the counter, the build
type and the date are named with it, because a magnitude that cannot say where it came from is not
transferable. Directions transfer between machines; magnitudes never do.

**This page does not define the API.** Every public function, type, field and constant is named on the
cheatsheet, and that is checked automatically. If a symbol appears only here, that is a bug in the card.

---

## Memory: how much of your app is actually RayClay?

*Referenced from the cheatsheet's build-knob section.*

▸▸ **Before you touch any dial below: most of your app's memory is not RayClay, and you cannot tell
without a control.** Run a bare GLFW+GL window with *your* creation parameters and subtract it. The
absolute floor is a property of the GL driver, the compositor and the swapchain far more than of
any toolkit, measured on two machines with two different counters:
  macOS M3 Pro, phys_footprint (unified memory, so GPU is *inside* the number),
  800x600, drawing nothing, three runs of medians-of-3 (bands are the observed spread):
      GLFW window, no GL context                     ~20 MiB
      + GL 4.1 core, MSAA 0, depth/stencil 0        55-63 MiB
      + MSAA 4 (RayClay's default)                 116-124 MiB   <- +53 to 61 MiB
      + RayClay itself                             126-130 MiB   <- +5 to 14 MiB
  Fedora + discrete NVIDIA, cgroup v2 non-reclaimable, VRAM read separately:
      bare GL 12.24 MiB host / 16 MiB VRAM;  RayClay 14.51 / 19.
      bare GL is 84% of RayClay on both sides of the memory boundary.
**The direction transfers, the magnitude never does:** ~90% and 84% are the same finding in two
  memory architectures, not a disagreement. Quote neither as "RayClay's footprint"; quote the
  *delta* you measured on the box you measured it on.
**The biggest line item on macOS is the 4x MSAA framebuffer, and it is a knob, not a cost of using
  RayClay.** `RC_GFX_MSAA_SAMPLES` defaults to 4; on this box that framebuffer is worth more than the
  GL context and roughly five times everything RayClay itself allocates. If memory matters more than
  edge quality on your target, that is the first dial to turn, and it is the same dial on every
  platform, so measure it before you go looking for allocations.
**Four rows instead of two, because two subtractions can go wrong in opposite directions.** Run the
  control *without* MSAA against a RayClay build that defaults to 4x and the library is charged for
  the framebuffer: "RayClay's marginal cost is 56-65 MiB", almost exactly the MSAA framebuffer. Set
  MSAA 4 but leave GLFW's default 24-bit depth + 8-bit stencil in, which RayClay explicitly reclaims
  (it never samples either), and the control comes out *higher* than the subject, making the library
  look like it *saved* 32 MiB. Both numbers are the control's fault.
  **So: a control is not "the same kind of program".** It is the same *creation parameters*, all of
  them: sample count, depth bits, stencil bits, profile, scale-to-monitor. Copy them from the
  source rather than choosing them, and if a subtraction comes out negative or suspiciously large,
  suspect the control before you believe the finding.
**Do the arithmetic on that Fedora line, and do it twice, because it answers two different
  questions and mixing them is a 6x error:**
    "what does the process hold?"    19 MiB device + 14.51 MiB host. More than half of it is
                                     invisible to the host counter most people quote, so on a
                                     discrete GPU you need two readings or you have none.
    "what does RayClay cost?"        19 − 16 = ~3 MiB marginal device, because a bare GL window
                                     holds 16 MiB before any toolkit exists.
  **"RayClay uses 19 MiB of VRAM" is the wrong sentence**: it is the total, and ~84% of it is
  the GL context. It is the same "quote the *delta*" rule as the line above, on the device side.
  *(An external benchmarking team independently measured the total on the same class of box on
  2026-08-10 (19.004 MiB device vs 14.653 MiB host) and led with it as a RayClay weakness. Both
  their number and the 3 MiB are correct; only the second one is about RayClay. Quote the total and
  the control together, or the total will be read as RayClay's device cost.)*
  **And the ~3 MiB is now decomposed,** by counting what RayClay *asks* the GPU for at link time
  (`--wrap` on sg_make_image/sg_make_buffer, which is GNU-ld only, so quote it as "GPU bytes requested",
  never as VRAM: it is demand, not residency, and it does not exist on macOS or Windows):
      images   1,048,580 B   the 1024x1024 R8 glyph atlas + a 1x1 white RGBA8
      buffers  2,162,688 B   packet vertex + index buffers
      TOTAL    3,211,268 B = 3.063 MiB
  **Two instruments sharing no mechanism agree:** 19.004 total minus the ~16 a bare GL window holds
  gives ~3, and the link-time count that never touches the device gives 3.063. Believe it more than
  you would believe either alone.
  **So "the atlas is RayClay's GPU cost" is measurably wrong**: the packet *buffers* are 2.06 of the
  3.06 MiB, larger than the atlas. ⇒ ~84% of the 19 MiB is driver context, swapchain, shader
  compiler and allocator rounding: real, but not RayClay's to shrink.
  **The sharp edge nothing host-side guards:** a pixel-format change R8 -> RGBA8 would 4x the
  atlas's demand with *zero* host-heap movement, so a measurement of peak *heap* alone stays flat
  through it, and so does a count of live pool entries, because *alive-counts* do not move when
  bytes do. Only a count of requested bytes sees it.
**And the spread is an architecture property too.** The same experiment moved 21.5 MiB run-to-run
  under ri_phys_footprint and 0.02 MiB under the Linux cgroup counter: ~400x. Report your spread
  beside your number, or you cannot tell an effect from noise: a 341 KB binary saving is invisible
  inside a 21 MiB spread, and "no effect" is then a statement about your instrument, not the code.
**Reserved is not resident, and the gap is easy to miss.** A bare GLFW+GL
program sits at 20 MiB after glBufferData(16 MiB, NULL), and jumps to 36 MiB the moment it writes
48 bytes into that buffer. Allocation is free; **the first touch commits the whole thing**. So a pool's
declared capacity is an honest *upper bound* on what it costs you, never a reading of what it costs.
**First-ever launch is not steady state.** On a cold GL shader cache the driver compiles the shaders at
startup and the cost lands in your process: measured on NVIDIA by an external benchmarking team,
+12.7 MiB for a window that only calls glClear and +50.0 MiB for RayClay, both vanishing once the
cache is warm. If your harness hands each run a fresh HOME/XDG_CACHE_HOME for isolation, you are
measuring shader compilation, not your app. Driver-specific: verify before porting the numbers.

## rcRegisterFont / rcFont

`rcRegisterFont` bakes a font and indexes it by (family, weight, size); size rounds to the nearest
pixel, and re-registering the same path is idempotent (it reuses the baked face, no new slot).
`rcFont` resolves a registered (family, weight, size) back to its fontId, rounding size the same way.

**One file per weight. The `weight` argument labels the file you give it; it does not select a
weight inside it.** Point two weights at one file and both bake that file's outlines:

```c
rcRegisterFont("Roboto", RC_WEIGHT_REGULAR, "Roboto-VariableFont_wght.ttf", 28);
rcRegisterFont("Roboto", RC_WEIGHT_BOLD,    "Roboto-VariableFont_wght.ttf", 28);  // NOT bold
```

Both calls succeed and return *different* ids, `rcFont`(..., `RC_WEIGHT_BOLD`, ...) resolves the second,
and your bold text is Regular.
**This is not a variable-font problem**: handing a *static* Regular file to `RC_WEIGHT_BOLD` does exactly
the same thing. The rule is one *file* per weight, whatever its flavour.

**It tells you:** registering one path under a second weight logs a warning naming both
weights ("`rcRegisterFont`: '<path>' is already registered at weight 400 and is now also registered at
weight 700 …"). It fires **once per weight**, not per call (a 2-weight x 5-size ladder logs it once),
and the face still loads, so this is a warning, not a refusal.
Measured 2026-08-09 on Roboto, Inter, Open Sans and Nunito: the two variable rows are identical to
the ink pixel, while the same test on the *static* files separates cleanly (Roboto 1481 px regular vs
2118 bold).
**The fix is the file, not the call:** use the per-weight statics. A Google Fonts download puts them
in the `static/` subfolder while the variable file sits at the top level, so the file you reach for
first is the one that cannot work.

## The font ladder (RC_AppOptions.fontPath / fontSizes / fontCount)

Declarative setup is the usual way in, and what every bundled example does: declare the ladder on
`RC_AppOptions` and the runner bakes it before the first frame.

```
.fontPath + .fontSizes + .fontCount     // bake ONE face at each listed pixel size
```

**The i-th size becomes fontId i: the load-order index, not the size value.** Name the slots and pass
the name:

```c
enum { F_SMALL, F_BODY, F_H1, F_COUNT };  static const float sizes[F_COUNT] = { 13, 16, 28 };
.fontSizes = sizes, .fontCount = F_COUNT   =>   rcTextL("Title", .font = F_H1)   // NOT .font = 28
```

.fontPath = NULL *with* .fontSizes bakes the bundled face at each of those sizes: a crisp ladder
with zero asset files.
.fontPath without .fontSizes/.fontCount is ignored (warns once) and you get the bundled face:
your font silently never loads.

Neither .fontPath nor .fontSizes set gives you the bundled Latin-1 Roboto subset at one default size
as fontId 0, so text always renders out of the box.
A bake that fails mid-ladder does not consume its slot, which **shifts every later id**; the runner
warns loudly naming the file.

Up to 16 baked fonts (`RC_MAX_FONTS`), one slot per (family, weight, size), so a 4-weight x 5-size
ladder exhausts them. Over-cap registration is diagnosed (ERROR/WARN) at load. Plan your weight x
size matrix.

## rcSetLogSink

Routes the layout engine's own diagnostics to a log panel, a file or telemetry instead of stderr.
NULL restores stderr. A sink *replaces* stderr rather than adding to it.

You do *not* need this to see diagnostics on the web: they go to stderr, and the bundled shell.html
routes that to console.error (verified end to end). A sink is for *capturing* them.

Called synchronously. Keep it cheap and do not re-enter RayClay. `msg` is the bare body: no
"RAYCLAY[..]:" prefix, no trailing newline.

## rcProcessMemoryBytes / rcProcessCpuPercent

`rcProcessMemoryBytes` returns this process's resident memory: RSS (Linux), working set (Windows),
Mach resident size (macOS), WASM heap (web). 0 means the platform offers no reading. Coarse and
cheap: a dashboard sample, *not* an allocator-grade profile.

`rcProcessCpuPercent` returns CPU since the *previous* call, as a % of one core (like `top`, so >100
means multiple cores busy).
The first call returns 0 (no interval yet); web returns -1, because a browser tab has no OS
process view; `rcProcessMemoryBytes` still works there.
**A third reading exists and it is easy to misdiagnose: -1 on the desktop, from the very first call.**
That means the implementation was compiled in strict-ANSI mode, so `CLOCK_MONOTONIC` was never
declared and the monotonic clock fell through to its unavailable branch. The usual cause is a
directory-scope `set(CMAKE_C_EXTENSIONS OFF)` in your own `CMakeLists.txt`, which is inherited by
RayClay's target through `add_subdirectory`/`FetchContent`. Put your standard settings on your own
target instead; see [getting-started.md](getting-started.md) ▸ Build. `rcProcessMemoryBytes` is
unaffected either way.
Sample at a steady ~1 Hz: OS accounting is 1-10 ms granular, so per-frame calls read noise and
back-to-back calls (<1 ms) re-return the previous reading.

## rcGetContentScale / rcGetWindowDimensions

`rcGetContentScale` is the display device-pixel-ratio (physical/logical px): read-only, independent of
zoom, true DPR on macOS, Wayland and web. It reads 1.0 on a 1x display and before the window exists.
**On Win32 and X11 it is 1.0 at every DPI, and that is not a gap; it is where the scale went.** GLFW
  spends the HiDPI factor on the *window size* there; macOS, Wayland and web spend it on the
  *framebuffer*, which is why the ratio only carries information on those.
**Never multiply either size by a display scale to get device pixels.** The device-pixel count is the
  framebuffer size on every platform, full stop. Multiplying applies the factor a second time.

`rcGetWindowDimensions` is the window size in real logical px. Zoom never scales it, so it matches the
pointer only at zoom 1; under zoom, layout and the pointer use a zoom-divided space.

## rcUnloadFont

**The slot table is not one-way.** Without this, a font browser (or any app loading faces on
demand) would hit `table full` at 16 and every later load would silently render in the default
face. `rcUnloadFont` gives the slot back.
Returns false, changing *nothing*, for: id 0, an id never loaded, one already unloaded, and a call
made while the glyph sheet is *frozen*. id 0 is refused deliberately: it is both the default face
and `rcLoadFont`'s failure return, so destroying it is never what you meant.
**The frozen window is wider than "during a draw pass"**, which is the trap: the sheet freezes on a
frame's *first* text draw and thaws only when the *next* frame begins. So a `frameEndCallback`, and
anything after your last frame, is refused too. **Unload between frames, and branch on the return
rather than assuming it took.**
**It buys back a slot, not memory.** The glyphs stay in the atlas until something re-bakes it, so do
not expect atlas usage to drop; measure slots, not bytes.
**Consequence**, and it bites exactly the app this feature exists for: unloading clears `table full`
but *not* `atlas full packing`. A browser that cycles through faces frees slots fine, yet a later load
can still be refused because the sheet has no room left for its glyphs: a different refusal, with a
different log line. Branch on the message, not on the 0. The two sentences, as a run prints them:

```
rc_font: table full (16), ignoring font       <- CURED by rcUnloadFont
rc_font: atlas full packing @ 400px; font dropped   <- NOT cured; the sheet is the limit
```

Both are `RC_LOG_ERROR`, so the *level* still means "refused" and only the *message* says which wall you
hit. (Measured 2026-08-10: 16 loads exhausted the table, one unload freed a slot, a reload took it,
and a 400 px face was still refused with that slot standing free.)
**The freed slot comes back with the same id number**, and that is the sharp edge. In the run above
the victim was id 1 and the reload returned id 1: a *different* face wearing the number you already
hold. Nothing errors, nothing warns; your old handle simply draws something else. ⇒ Treat an id as
dead the moment you unload it, exactly as you would a freed pointer.
It also drops any `rcRegisterFont` rows that resolved to that id, so `rcFont` stops finding them. That
is the point: without it, `rcFont` would hand back an id whose slot the *next* load has reused, and you
would silently draw a different face than you asked for.
**A 0 return is not a failure signal**, so `if (!id)` is the wrong check.
**And 0 is a common *success* return, which is the half people miss:** the id is an *ordinal*, not a
size, so the *first* sized load after init legitimately returns 0. Measured: 12 px first returns 0;
16 px first returns 0 and 12 px then returns 1. A check reading `id == 0` as "the library refused
this size" accuses it of a defect it does not have. 0 is the *default* slot: it is
what every failure degrades to (missing file, rejected outline, NaN/0/negative/inf size) and what you
legitimately hold whenever the default face is in use. Every failure also logs, so **detect a failed
load from the log** (`rcSetLogSink`), never from the return value.
**Trusted fonts only**: RayClay bounds the offset table, table directory, cmap, the glyf outlines and
the `CFF ` container that stb_truetype walks; the Type-2 charstring *program* a CFF font executes is
still unbounded. Bundle the fonts you ship; never hand this a file a user supplied.
**The level is the verdict; the sentence is only the cause.** Do not match on the wording: that list
is open-ended. Across the whole font path (`rcLoadFont` and `rcRegisterFont`):
an ERROR means *refused* (you were handed the default slot), and a WARNING means the face *loaded* and
something about it is worth knowing. That rule is what your log sink should branch on.
**And the prefix is not one fixed string.** The font path logs under a *family* of prefixes that all
begin `rc_font`, `rcFont` or `rcRegisterFont`, and the set is not closed; it grows whenever a new
load path is added. **So match on the family and branch on the level above; never string-equal a
remembered list.** RayClay ships six of them, so a sink that hard-coded the three most obvious
ones would drop half of them.
**And a file your OS calls a valid font can still be refused.** The four sentences below are the ones
about the *font itself* (two refusals, two warnings on a face that *did* load), and only the first is
your file's fault:

```
rc_font: bad font data                  REFUSED. The bytes are damaged, truncated, or not a font.
                                        This one really is a broken file; re-download it.
rc_font: unsupported font (...)         REFUSED. The structure is valid and this build's
                                        rasteriser cannot open it. Nothing is wrong with the
                                        file. THREE known causes, all named in the message:
                                        a font COLLECTION (.ttc/.otc), CFF2 outlines, or a
                                        symbol-only cmap. A collection is the one you are
                                        most likely to hit: the stock CJK faces on most
                                        distributions ship as .ttc. Supply a single-face
                                        .ttf or .otf instead.
rc_font: N glyph(s) skipped ...         LOADED, minus N glyphs judged unsafe to rasterise. The
                                        slot is valid and the rest of the face draws normally;
                                        skipped glyphs bake blank at their true advance, so
                                        your layout does not shift.
rc_font: the NxN glyph atlas could      LOADED, but SHARPENED LOWER than you asked for. The
  not hold this face ... sharpened       atlas could not hold the face at that on-screen size
  at NxN instead                         with the oversampling requested, so it was baked at a
                                        lower one. Text is softer, not missing. Load fewer or
                                        smaller faces, lower RC_AppOptions.fontOversample, or
                                        build with a larger RC_FONT_ATLAS_W/H.
rc_font: symbol-encoded face (...)      LOADED, but its ASCII lives in the private use area, so
                                        letters come out as symbols. Expected for a dingbat or
                                        icon face; a surprise for anything you meant to read.
```

⇒ **Do not read every refusal as a damaged file**, and do not read a WARNING as a failure; a face
that warns is loaded and usable.
**The other refusals are not about the font at all**, and they are the ones you will actually hit
first. Each is an ERROR and each hands back the default slot, exactly like a rejected face:

```
rc_font_load: cannot read '<path>'       The path did not resolve. NOTE THE DIFFERENT PREFIX:
                                        a sink matching only "rc_font:" misses this entirely.
rc_font: table full (16), ignoring font You are out of slots. RC_MAX_FONTS is 16 INCLUDING the
                                        bundled default, and rcLoadFont does NOT de-duplicate by
                                        path; calling it in a loop or per frame exhausts the
                                        table. Load once at startup; rcRegisterFont IS idempotent.
                                        RECOVERABLE: rcUnloadFont frees a slot and the next
                                        load reuses it.
rc_font: atlas full packing @ Npx       REFUSED, and THE FACE IS DROPPED, not degraded. The
                                        glyph sheet could not hold it at that size even after
                                        the internal retry at reduced oversampling. Ask for a
                                        smaller size, or a bigger atlas.
```

Measured 2026-08-09 with a valid face: the 16th load returns 0 and logs `table full`;
one load at 400 px logs `atlas full packing`. **This is why you branch on the level**: a sink built
from the four font sentences above scores all three of these as success.
A refusal of the *face*, meanwhile, is the exception. Every system face on three stock boxes, all
measured against the same build. These are properties of each *platform's* font collection, not of
RayClay, so treat them as dated and sweep your own target if it matters:

```
box                      faces  loaded  of those: glyphs skipped / symbol-encoded   refused
Fedora 43    x86_64        238     234                    0 / 0                        4
macOS 15.7.5 arm64         371     365                    2 / 5                        6
Windows 11   AMD64         143     143                   10 / 5                        0
```

⇒ 742 of 752 faces loaded (98.7%). **Every outcome is platform-skewed:** Windows refused nothing but
produced two thirds of all the partial loads, and Fedora produced *no* warning of either kind, so a
font path tested only on Linux has never exercised the two warning branches at all.
**The refusal set is a property of the platform's fonts, not of RayClay**: it differs per OS and it
moves when the rasteriser does, so treat any face list you see quoted as dated. Sweep your own
target if it matters to you: load each face and read the log, which is all this measurement is.
**And the font layer has moved since**, so this table is dated *in fact*, not merely in principle:
the handling of symbol-encoded (3,0) faces, one of the columns above, is among the things repaired
since these figures were taken. The *shape* of the result (near-total load, platform-skewed
warnings) is what to carry forward; the per-box digits are a snapshot of a build that has since
changed.
**"`CFF2`" is not the same as "variable", and the difference decides whether your font works.** A
variable font built on `glyf` loads and draws normally: all 310 faces of a 10-family Google Fonts
download loaded and rendered, the 14 variable ones included (measured 2026-08-09). It is only the
`CFF2` flavour that cannot be read at all. ⇒ Check the *table*, never the filename.

## rcUnloadImage

**The one lifecycle rule**, and skipping it *breaks the app* rather than merely fattening it:
`rcLoadImage` **decodes and uploads on every call**; it does not cache by path. Assign a second `RC_Image`
over a live one and the first texture is unreachable forever. **Free, then load.**
**There is a ceiling of 128 live images.** Measured: reloading without freeing succeeds 126 times,
then *every* further `rcLoadImage` fails for the life of the process and .handle comes back NULL. The
same 200-frame run that frees first: 0 failures. Exhaustion **logs once** naming the cause
and the remedy, so you get a sentence rather than a mystery NULL. Host memory leaks alongside it
(~2.3 MB/load), but the pool is what actually ends the app.
**Do not plan around 128 growing.** A slot costs 84 bytes of host bookkeeping and no GPU memory, so
a larger pool would be cheap, and it would not help, because the *textures* are what accumulate: a
bigger pool buys a leaking app a later wall, not a working one. 128 is generous for a fixed art set,
and any app that *streams* images (a folder browser, a feed, a map tiler) must free as it goes no
matter what the cap is.
Where to free: any time your own code decides a picture is finished with: leaving a screen,
evicting a cache, swapping art. ex10's IMAGE section drives this live.
**At shutdown, do nothing**: `rcRunApp`'s teardown releases every GPU resource for you, so there is no
cleanup to write and no shutdown hook to want. Calling `rcUnloadImage` after `rcRunApp` returns is a
deliberate, safe no-op (it detects the backend is down, frees the CPU-side wrapper, and cannot
double-free). Freeing *during* the run is about the ceiling, never about exit.

## rcAppFrameCounts

.declared is the elements your layoutCallback declared this frame, before culling; .drawCommands is how
many of them survived culling and reached the renderer.

**The first number to reach for** when a frame feels expensive, and unlike `rcAppPerfFrame` it needs no
build knob; it is always compiled in. A large ratio is the signal, and the cure is to declare less
(`rcVirtualList`), not to draw faster. Measured: 400 filled 4px boxes stacked in a column at 800x600
read 402 declared -> 151 drawn, so only what fits the viewport survives; the ratio climbs with the
list length while the visible cost stays flat.

**The drawn count depends on your geometry**, so compare it against your own earlier runs rather than
against any number quoted here. .declared is the stable half: it counts what your layoutCallback asked for
and does not move when the window does.

**Culling happens in two places, and the second one surprises people.** The first is per *element*: a box
whose bounding box is entirely outside the viewport emits nothing. The second is *inside* a wrapped-text
element, at *line* granularity: the emission loop stops at the first line whose running y has passed the
viewport bottom, and every later line of that same paragraph is dropped with it.
  ⇒ **One long paragraph can contribute fewer draw commands than it has lines**, and how many depends on
  the window height. It is not clipped-at-draw-time; the commands are never generated.
  **It keys on the viewport, not on the containing box**, so a tall paragraph inside a scroll container
  is unaffected once you scroll it into view; the test is where the line lands *on screen*.
  The line that crosses the edge *is* emitted; the stop applies to the ones after it. Both sites are
  governed by the same culling switch, so both go away together if it is turned off.
  **This is why a drawn count is not a line count.** If you are using .drawCommands to sanity-check that
  a body of text rendered, it will under-report by design, and resizing the window will change it.

**An element with nothing to fill emits no draw command.** The same 400 boxes with no .bg read 402
declared -> 0 drawn. A low drawn count is not evidence of a problem: a layout scaffold of bare
`rcRow`/`rcColumn` is *supposed* to cost nothing.

Both fields are 0 before the first drawn frame, and for a NULL app. What each ratio means, with a
table, is in getting-started ▸ "The first number to look at".

**Read it between frames**: in `RC_AppOptions.frameEndCallback`, or anywhere outside the layout pass.
It is snapshotted where the render walk begins, so a read inside layoutCallback/updateCallback answers for the
*previous* drawn frame. That is deliberate: correct numbers for an earlier frame rather than torn
numbers for this one.

## rcAppArena

Needs `RC_AppOptions.scratchArenaBytes` > 0, and the two failures look *different* on screen. Left at
the 0 default (or handed a zeroed `RC_Arena`) `rcFormat` returns the visible marker
`<set scratchArenaBytes>`; if that string turns up in your UI, this is why. An arena that is merely
*full*, or a NULL arena or format, returns `""` instead, and that is the usual cause of a label that
renders empty. Either way it warns once.

## rcAppDestroy

  **One live `RC_App` per process.** A second create while one is live is rejected (NULL).
    Sequential windows *do* work: create -> `rcAppDestroy` -> create.
    The limit is RayClay's own; the layout engine underneath does support simultaneous contexts.
    Lifting it means moving a hundred-odd pieces of per-window state behind an explicit context, so
    treat one live `RC_App` as a standing constraint rather than a check waiting to be lifted:
    `rcAppCreate` refuses outright rather than half-working.
    **Want a second view today?** Dock it in the same window: `rcBeginSplitPane` (a resizable
    sidebar/detail split) or a non-modal `rcBeginModalEx` panel (a floating inspector that leaves
    the app live behind it). Both ship, and are demoed in ex10/ex12.
  **`rcRunFrame` is non-blocking by contract**: it never sleeps, so the on-demand idle does not
    apply to a hand-rolled `while (rcRunFrame(app)) {}`. That spins flat out: measured 115.45 vs
    0.20 CPU-s over the same idle window: 577x. The park lives in `rcRunApp`'s loop, not in the
    frame call. Prefer `rcRunApp` unless you must own the loop; if you must, block or pace it
    yourself. **Pacing is yours; the two bounds are not.** `maxFrames`/`RAYCLAY_MAX_FRAMES` and
    `maxSeconds`/`RAYCLAY_MAX_SECONDS` are both honoured here: `rcRunFrame` tests the deadline on
    entry and returns false, the same edge `rcRunApp`'s loop tests it on.

## rcCloseWindow, and how an app actually quits

`rcCloseWindow` is the split-loop host's teardown: the symmetric partner of `rcInitWindow` for a
program that drives its own loop. It shuts down fonts, text, clipboard, the icon pool, widgets,
intents and input, tears down the render backend, then destroys the window.

**It is not how an `rcRunApp` application quits itself.** A Quit button, a menu item, a close
chip: all of them call `rcAppRequestClose(app)`, which sets a flag the runner reads at the top of
its next iteration, so the current frame finishes drawing and the loop then exits normally.

```c
if (rcButton("quit", "Quit", RC_BTN_GHOST))
    rcAppRequestClose(app);          /* the quit verb */
```

**Calling `rcCloseWindow` from a callback is safe, but its timing is not yours to predict.** Two
different things can happen and your app cannot tell which it is getting:

| the frame you are in | what `rcCloseWindow` does |
|---|---|
| an ordinary main-loop frame | tears down immediately, mid-frame, while your callback is still running |
| a frame driven by the refresh hook: any X11 `Expose`, and the whole OS modal-resize loop | defers to the end of the frame; the window stays alive for the rest of it |

Nothing in the public API reports which kind of frame is on the stack, and the refresh-driven one
arrives unbidden: the OS decides when to send it. So a teardown that looks synchronous every time
you test it can defer on a user's machine the moment they grab the window edge.
⇒ **Reach for `rcAppRequestClose` and this whole question disappears.**

`rcCloseWindow` is idempotent, so a split-loop host that calls it explicitly *and* lets the runner
reach its own teardown is doing the ordinary thing, not double-freeing.

## Redraw scheduling: the contract

**The contract:** RayClay redraws when something happens. Input, resize, focus and your own requests
admit a frame; otherwise `rcRunApp` sleeps in the OS event loop at ~0 CPU (measured 1.08 -> 0.00
CPU-s/min idle). This is the browser's requestAnimationFrame bargain: nothing animates for free.

If your app changes state RayClay *cannot see* (a timer, a socket, a worker result, a physics step),
you must ask for the frame.

## Colour builders are compound literals

`rcRgb` / `rcRgba` and the built-in RC_* palette expand to a compound literal, which is not a constant
expression. `rcHex` and `rcColor` are *functions*, also not constant expressions, so the same rule
below applies to all four spellings, for a different reason.

**Name a file-scope token with #define, not `static const`.** A file-scope
`static const RC_Color X = rcRgb(...)` (or `= RC_SLATE_800`) is rejected under
-std=c99/c11 -pedantic-errors. It compiles as a GNU/clang extension; do not rely on it.

```c
#define BRAND_500  rcRgb(99, 102, 241)
```

## rcAppSchedStats: reading the idle scheduler while it runs

On-demand rendering is RayClay's headline claim, and a claim needs an instrument. `rcAppSchedStats`
returns a snapshot of the runner's frame-admission counters, so an app can plot what that claim is
about: how often it woke, and how often that wake did nothing.

```c
RC_SchedStats s = rcAppSchedStats(app);
float wasted = s.waits ? 100.0f * (float)s.spurious / (float)s.waits : 0.0f;
```

**The counters are cumulative.** Chart the *delta* between samples, not the value: the raw figure only
  ramps, and a wake loop is a *rate* that spikes, not a total that grows.

**The denominator is `waits`, never `admitted`.** spurious/admitted compares two different
  populations and reads alarmingly high on a perfectly healthy idle app: one that parks a great
  deal and draws rarely is doing exactly what it is supposed to.

**A per-second rate needs a clock, and the clock is `rcAppTime`**, *not* an accumulation of
  `rcAppFrameTime`, which is exponentially smoothed and discards any interval of 1 s or more, so on
  demand it does not lose precision, it stops tracking. Measured over 6 s: summing it while woken
  about once a second gives 0.610 s against 6.006 s of wall clock; the same measurement under
  continuous rendering reads 1.000. → **rcAppTime** below. Or prefer the *ratio* above, which is
  dimensionless and needs no clock at all. `examples/ex12_rayclay_inspector` labels its two series
  `spur/frame` and `adm/frame`, so the unit is on screen and not merely in its source.

### The three zeros that lie

**`RC_FRAME_EXPOSE` reads 0 because nothing raises it.** That is a statement about the
  library, not about your app: repaints during an OS modal resize are serviced by a synchronous
  path that draws directly, so no admission is ever attributed to it. "Nothing can report this" and
  "this never happened" print the same digit and are different claims. Check the counter rather
  than relying on this note: if a producer is wired in a later release, the value will move.

**Every counter is 0 under a manual `rcRunFrame` loop.** The scheduler belongs to `rcRunApp`; a host
  driving its own loop has nothing to admit and nothing to park. That zero is *not* "my app never
  sleeps"; it is "this app has no scheduler".

**`waits` is 0 under `RC_RENDER_CONTINUOUS`**, because the loop never parks. So a scheduler panel in a
  continuous app is a structural "not applicable", not a clean bill of health. Measured on ex23 idle
  for 3 s, Linux/Xvfb/llvmpipe: continuous 0 waits and 0 spurious, on demand 1 wait.
  Frame counts from that run are a property of one GPU and one 3-second window and would not
  reproduce elsewhere; the zeros are the finding.

### Reading `spurious` and `refreshRepaints`

`spurious` counts parks that ended with **no reason pending and no deadline due**, so it reads
directly as "parks that woke for nothing", which is the presentation-echo signature you are looking
for. A park armed with a timeout that then fires *on* that timeout is real work you asked for, and it
is **not** charged here: a deadline is a *time* rather than a reason bit, and the scheduler asks
whether one is due before booking the wake as waste.

`refreshRepaints` counts **window-refresh callback ENTRIES**: how often the OS *asked* for a
synchronous repaint outside the admit path, not frames drawn. The counter is incremented before the
frame is attempted, and a callback that arrives while a frame is already in flight is dropped by the
re-entrancy guard without painting; nothing decrements. Measured on macOS: **+40 across a single
window zoom, against about two frames actually completed**. Never read it as a frame count.

Those entries are a different population from the parks the other fields describe, so treat it as
context beside `spurious` rather than something to subtract from it.

**`RC_FRAME_REASON_COUNT` is an array length, not a reason.** Bound `byReason[]` with `<`, never
  `<=`, and read the length from the enum rather than writing the number down; that is the whole
  point of it having a name. `rcFrameReasonName` turns an index into a label, so a loop over the
  array needs no table of your own and picks up any reason added later.

## rcAppRequestFrame / rcAppRequestFrameAfter

  `rcAppRequestFrame`:       call it whenever you mutate something visible outside an input handler.
  `rcAppRequestFrameAfter`:   for a clock, a poll, a 1 Hz feed. ~60x cheaper than continuous.
    **One outstanding timer, earliest-wins:** two independent schedules coalesce to the sooner one,
      so re-arm the later one when you are woken.
  `rcAppSetContinuousRendering`: turn on around a self-driven animation, off when it ends.

  **A live FPS or frame counter in your UI *is* an animation.** The text changes every frame, so the
    window can never park and one label costs you the whole idle win. Drop it, or accept
    `RC_RENDER_CONTINUOUS` knowingly.
  Diagnostics: RAYCLAY_IDLE_STATS=1 prints admitted/waits/deadline/spurious at exit, and then one
    line per admission reason: initial · input · window · expose · resource · app · deadline ·
    internal. **That breakdown is the tool for "why will my window not park":** it names which
    source is waking you instead of leaving you to guess. Your own `rcAppRequestFrame` lands in
    "app"; `rcAppRequestFrameAfter` and RayClay's own bounded retries land in "deadline".
    Measured on ex00, idle 4 s on-demand: 2 admitted (1 initial, 1 input), 0.15 s user CPU.
    RAYCLAY_MAX_SECONDS=n bounds a run by wall clock (a frame budget cannot bound an event-driven
    run) · compile the library with RC_GFX_DIGEST=1 (target_compile_definitions on the rayclay
    target, not a cache -D) to be warned once if your app draws a moving picture but never
    requests a frame.

## Webapp-style zoom (the accelerators)

Webapp-style zoom  (on by default; configure via `RC_AppOptions.zoom`)
  Ctrl +/- walk the zoom ladder (Chrome's stops: … 90 100 110 125 150 175 200 …), Ctrl+0 resets to
  100%, Ctrl+wheel zooms continuously. On macOS the accelerator is Cmd.
  `rcAppZoomLadder`(app, &n) hands back the stops the keys *actually* walk (see App control below).
  `RC_ZOOM_LAYOUT` (default)  reflows like a browser page.
  `RC_ZOOM_OPTICAL`           magnifies the surface about the cursor and never overscrolls; zoomed
                            out it lays out into the larger visible rect, so a grow root still
                            fills the window. Set .zoom.pan = true to hold Space and drag the
                            magnified view around; without it the view is anchored and a
                            magnified surface has no way to reach its own edges.
  Settled text re-bakes at the density the renderer draws at; an atlas too full to bake holds the
  last fitting density (soft, never missing). **The accelerators are desktop-only**: inert on web, where the browser
  owns page zoom.

## rcAppZoomLadder

  The zoom stops the *keys* actually walk, already resolved: your `RC_ZoomOptions.ladder` if you supplied a
  valid one, RayClay's bundled Chrome table if you did not (including after an invalid one was rejected).
  Writes the entry count to *count when count is non-NULL. Build a preset row or a "%" dropdown from this
  rather than from your own copy: a duplicate silently diverges the day either side changes, which is the
  whole reason .ladder is a pointer you hand over instead of a value the library mirrors.
  Returns NULL with *count = 0 when there is no ladder to show: you set .step and asked for continuous
    keyboard zoom (also for a NULL app). That is the honest answer, not an empty table: draw no preset
    chips, because a hard-coded fallback list would show stops the keys never visit.
  Pointer is borrowed (valid for the app's lifetime) and the contents are read-only: the bundled table is
  shared process-wide. The wheel is always continuous and never consults this.

## Leaving a container body early

**Leaving a container body early.** The brace body is a macro-generated loop, not a plain block, so
   C's jump keywords do not mean what they look like. Measured on gcc and clang, 0 warnings, on
   both public runners (`rcRunApp` and the `rcRunFrame` split loop, which share one frame path):
     continue   safe      ends the element body cleanly. Always was. No diagnostic.
     break      safe, but not what you mean: it ends the *element* body, not your enclosing loop,
                and the loop keeps going. Measured: `break` at i==2 of 5 still ran i=3 and i=4.
                The element is closed for you, and RayClay logs one warning that names the fix.
     goto out   unsupported. Do not write them. Both leave the whole `for` without running its
     return out increment, so the close is skipped and the element stack is left unbalanced.
                The cost, measured: the layout engine drains what you left open, so you get a completed frame
                that is the *wrong* frame: every element after the jump is missing, every frame (a
                5-iteration loop declared 3), and a sibling declared after the escape point can
                vanish entirely. "Unsupported" is the contract, not a promise about what happens
                next: do not rely on whatever you happen to observe.
   **The diagnostic fires once per app, not per frame**: RayClay latches each error type per `RC_App`,
     so it does not flood your log; a second `rcAppCreate` re-warns. It scrolls away while the broken
     UI stays, so never go looking for a repeating line to confirm this; there isn't one. It usually
     names the culprit: "Innermost element left open: <your-element-id>", but only when that element
     has a string .id and the message fits the frame's diagnostic buffer; otherwise you get the bare
     sentence by design, so absence is not a second bug. Give looped containers real string ids.
     Fail CI on any RAYCLAY[WARNING] across the whole run, never on a tail.
   **The fix** for all three: put the loop outside the element, or set a flag inside the body and
      test it after. Never jump out of the braces.
   On the web the warning *does* reach you: RayClay logs to stderr and the bundled shell routes it to
     console.error, so it shows up in devtools. You just have to have devtools open. See web-build.md.

## .align: the "<Y><X>" code

.align: "<Y><X>", e.g. "tc" = top + centre.  Grammar: Y is one of t|c|b, X is one of l|c|r.
  **A half-wrong code half-works, loudly:** "lc" puts 'l' in the Y slot, so the parser warns
    (WARNING: ignoring unparsable align token "lc") and **then lays out anyway** using the valid half.

**The trap: which letter is justify-content depends on the container's direction.**
  These letters are not bound to fixed CSS properties. justify-content is always the *main* axis, and
  the main axis is X in a row but Y in a column, so the two letters *swap roles* between them:
                     justify-content (distributes the children as a group)   align-items (each child alone)
    `rcRow`                    X, the 2nd letter                                  Y, the 1st letter
    `rcColumn` / `rcBox`         Y, the 1st letter                                  X, the 2nd letter
       (`rcBox` is top-to-bottom by default, so it behaves as a column here)
  Measured, 1280-wide container holding two 100px-wide children:
    `rcRow`    .align="tc"  -> first child x = 540 = (1280-200)/2   the pair is centred as a group
    `rcColumn` .align="tc"  -> first child x = 590 = (1280-100)/2   each child is centred on its own
  It reads correctly for "cc" whichever way you assume, which is exactly why this bites late: a
    developer writing .align="tc" on an `rcColumn` expecting a spread row gets individually-centred
    children and no warning. If you want a group distributed along a row, you want `rcRow`.

  There is no space-between / space-around / space-evenly. Main-axis distribution is start | centre
    | end only. (The usual substitute: an `rcSeparator` between the items, which grows to fill.)

## Sizing strings (.w / .h)

.w / .h is the CSS-like sizing string, the primary inline-CSS form used throughout examples/:
  ""/NULL/"fit"/"auto" => FIT (shrink to content)   "grow" => GROW (fill the parent)
  "100" or "100px" => fixed px   "50%" => % of parent   "50vw" / "50vh" => % of the viewport w/h
  **Units are case-sensitive** here while CSS is not: "50vw" parses, "50VW" is rejected (and warns).
  Decimals and "1e2" are fine; a hex / negative / over-100% / unknown-unit token warns once and falls
  back to FIT, and a childless FIT box is 0-wide, so a bad unit makes the element vanish (watch the
  log). "%" is parent-relative; a "%" inside a FIT parent has no definite basis and likewise collapses
  to 0. Its exact per-axis basis vs CSS (the main axis reserves sibling gaps, the cross axis does not,
  and a "fit" parent collapses the child) is measured out in for-web-developers.md ▸ "% is parent-relative".

## rcTextC

  e.g.  `rcTextL("Title", .font = rcFont("Roboto", RC_WEIGHT_BOLD, 22), .color = s.text);`
  Lifetime: `rcText`/`rcTextC` do **not** copy; the layout engine keeps your pointer until the frame is drawn, so a loop-scoped
  stack buffer is a use-after-scope; use `rcFormat` / a longer-lived buffer (`rcTextL` literals are static).
  Text covers ASCII (32–126) + Latin-1 (160–255) by default, an *engine* codepoint window, not the loaded font's
  own coverage: a codepoint outside it (smart quotes, €, CJK) renders '?' even from a custom font that contains
  it; control/DEL chars render nothing.
  The window is a build-time default, not a hard limit: `-DRC_FONT_LAST_CODEPOINT=N` widens it, and the
  three conditions that must travel with it (bundled font has no glyphs past Latin-1 · cost is linear in the
  *cap* not in use · atlas area is a separate ceiling) are spelled out at `rcRegisterFont`. Not a CJK switch.

## rcTextInput: the caret settle

**The caret settles:** it blinks for 10 s after the last edit, then holds steady (on, never off), so
the insertion point stays visible. A browser blinks forever; this is RayClay's one deliberate
divergence from browser behaviour, because a blinking caret is the only thing that draws with no
input at all, at 0.0600 CPU-s/min forever.
Restore forever-blink with -DRC_EDIT_BLINK_TIMEOUT=1e9.

## rcScrollBy: the sign convention

POSITIVE-DOWN and POSITIVE-RIGHT, like the DOM's element.scrollBy, and like `RC_ScrollInfo`'s
.offsetY/.offsetX readback, so the two *compose*: `rcScrollBy`(id, 0, 160) raises .offsetY by 160.
Clamped at both ends.
**Both wheel readers are exceptions, and in different ways.** They report the raw wheel in notches
rather than pixels, so scale them. `rcScrollDeltaY` is positive-UP, the opposite of `rcScrollBy`'s
positive-DOWN, so negate it. `rcScrollDeltaX` does **not** follow `rcScrollBy`'s positive-RIGHT
either; the value is normalised to read the same on every platform, so decide its direction once by
trying it and it will hold everywhere. Do not assume the two readers share `rcScrollBy`'s signs.

## Element ids are 32-bit hashes

Ids are 32-bit hashes of the *string*, so a formatted-index id can collide once ~15k of them coexist
in one frame, and the threshold depends on the *prefix*: "item%d" collides at 20,000 while "Row %d" is
clean through 65,536 (measured 2026-08-08).

Virtualizing is what keeps this from mattering in practice: only the visible window is declared, so
a 1,000,000-row list has ~20 live ids and the *data* index is safe at any size. And it is not silent
if you do hit it: the warning names the id.

## rcScrollbar

**Call it inside your layout callback**: a requirement, not a style note. The bar is a floating
  element declared where you call it, so the layout must still be open
**Layering is automatic**: the bar sits just above the content it scrolls and *below* a modal scrim,
  so an open dialog covers and dims it along with the rest of the background. Declare it in the
  *same scope* as its container: called inside a modal/popup scope it lifts above *that* panel instead
Drive the scroll *before* you declare the bar: `rcScrollbar` samples the offset as it declares
  itself, so an `rcScrollToBottom` placed after it leaves the thumb a frame stale
The thumb is placed from the container's *previous* frame, so it lags one frame on a content-size
  change (the ordinary immediate-mode trade). Auto-hides while the content fits

## rcIsScrolledToBottom

Long lists: declare only what's visible
  **Why:** layout charges per *declared* element, not per visible one (roughly 1,470 instructions each,
  every frame), and culling cannot help, because an element must be sized and positioned before
  anyone knows it is offscreen. So one row costs (elements in it) x ~1,470, and a 1,000-row list
  pays that a thousand times to show fifteen rows.
  **Measured across five of the bench apps:** halving the viewport drops render commands 35-58% and
  moves declared elements by exactly zero in all five of them. Culling is working perfectly and it is not
  the cost. Showing less of a list never makes a list cheaper; declaring less of it does. A scroll
  container is the shape where this hides: it looks bounded on screen and is unbounded in the data
  behind it, so the cost is linear in *your* dataset with no visible symptom until it is large.
  `rcVirtualList` declares only the visible window plus overscan, padded by two spacers, so cost goes
  *flat* in row count. On the 3-element row rayclay.h measures: 1,000 rows 4,375,713 -> 86,689
  Ir/frame (50x), 5,000 rows 254x. Scale that by *your* elements-per-row rather than quoting it.
  It is also a RAM lever: the element arena never shrinks, so 5,000 declared rows pin it at
  10.97 MiB where the virtualized list stays on the 1.43 MiB floor.

## rcVirtualList: the three rules

**Three rules:**
  1. The id must name the *enclosing scroll container*.
     **And give that container a height if it is itself inside another scroll container.** A scrolling
       parent is unbounded along its scroll axis (that is what scrolling means), so a "grow" child of
       one resolves against its own content instead, and the spacers *are* that content: the viewport
       this helper samples becomes the window it declared last frame, and the two feed each other.
       The sampled viewport is clamped to the layout and one line naming the list is
       logged, so the symptom is a stuck screenful of rows, not a hang. "grow" is otherwise the
       normal, correct idiom, including at the root, exactly as above. Measured at 1,000,000 rows:
       grow-at-root, grow-in-fit-parent, grow-in-fixed-parent and fixed px all declare a bounded
       window every frame; scroll-inside-scroll is the *one* shape that runs away.
  2. Every row must really be rowHeight tall: the spacers are computed from it. Uniform rows only
     ; a wrong pitch skews the scrollbar.
  3. Key each row by its *data* index, never by its position in the window. Keying by position
     rebuilds the hashmap working set on every scroll step: it gives back most of the win and makes
     hover/click jump between rows.
  Never `break` out of the *loop* (see above); `continue` is fine.
  The 32-bit id hash can collide once enough sequential ids coexist, and how many depends on the
  *prefix* ("item%d" collides at 20,000, "Row %d" and "e%d" stay clean through 65,536; see "Element
  ids are 32-bit hashes" above). A virtualised list declares only ~20 ids at a time, which is why it
  is the only shape that works at that scale at all.

## rcIsModalOpen / RC_ModalOptions

Non-modal popups read false by design: a combo, a menu, or an `rcBeginModalEx` with
`RC_MODALITY_NON_MODAL` deliberately leaves the app behind it live, so nothing should pause for one.

**Use it for what only your app knows is running:** pause a video, suspend a poll, hold a toast, stop
a simulation tick while the user is in a dialog.
**Not** for working around library layering. A widget that paints or reacts wrongly under a modal is a
*library* bug and gets fixed there; report it instead of guarding your own call sites with this.

`RC_ModalOptions` has two independent axes, modality (is the app behind usable?) and dismissal (does
an outside click close it?):

```c
.modality          = RC_MODALITY_NON_MODAL   // no scrim; the app behind stays LIVE
.noBackdropDismiss = true                    // an outside click is ignored (Esc still closes)
```

`RC_MODALITY_NON_MODAL` *alone* is not "leave it open and keep working": with no scrim, "outside"
means anywhere in the app, so a click into the app both does what the user wanted and closes the
panel. A panel that stays open while you keep working is the *pair* of both fields.

## rcSparkline

  `rcChart`      multi-series: mixed kinds, per-series colour, optional 2nd y axis.
  `rcSparkline`  no axes/ticks/legend/padding: the whole box *is* the plot (resource strips, cells).
  y/x arrays and label strings are borrowed: keep them alive until `rcRender`() has run.
  Both fill their parent; size them by wrapping in a sized `rcBox`.
  Non-finite (NaN/inf) breaks a line into segments rather than plotting.
  Over 16 series: warns once, draws the first 16, and adds a "+N more" chip in the chart, shown
    whether or not you enable the legend, because the legend defaults to off and a quietly
    truncated plot reads as "those series are flat", i.e. wrong data rather than less.
    -DRC_CHART_MAX_SERIES=N retunes that cap within 1..16; anything outside that range is refused
    by `rayclay.h`'s own #error naming the knob and its range. 16 is the ceiling because the
    categorical palette has exactly 16 entries and a higher cap would wrap it and make two series
    share a colour.  → RC_CHART_MAX_SERIES below
    It is a *readability* limit, not a memory one: past ~12 categorical hues nobody can tell two
    lines apart, so set .color explicitly beyond that. Each series costs 1408 B of .bss.
**Two different 16s**. Do not confuse them:
    16 series per chart   (tunable *down* with -DRC_CHART_MAX_SERIES=N; over it you get the "+N more" chip above)
    16 charts per frame   (not tunable, an internal pool). A 20-chart dashboard hits this one.
  Past the per-frame cap a chart draws "chart limit reached" and keeps its box, so the surrounding layout
  does not reflow; a sparkline has no room for a message and draws nothing. It also warns once per process.
  It counts `rcChart` *calls* in a frame, not charts on screen: the slot is taken before any culling, so a
  chart scrolled out of view still consumes one. Build them conditionally if you have more than 16.

## Styling caps: RC_GRADIENT_MAX and RC_SHADOW_MAX

Both are **64**, both are public constants in `rayclay.h`, and **neither is tunable**: they size
two side-channel tables that live in `.bss`, so raising them is a footprint decision rather than a
caller's. They bound how many **distinct elements per frame** may carry a `.gradient` or a `.shadow`.

**Past the cap, the failure is silent.** The frame is otherwise correct:
a gradient falls back to the element's flat `.bg`, and a shadow is simply not drawn. The library
warns, but **once per process, not per frame**, deliberately, because a 60 Hz log line is a worse
defect than a missed one. ⇒ **a grid of 80 styled cards logs two lines on frame 0 and then draws 16
of them wrong, silently, for the rest of the run.** On the web that single line is invisible unless
you installed `rcSetLogSink`.

**They are public so you can design against them**, the same reason `RC_CHART_MAX_SERIES` is:

```c
if (cardCount > RC_GRADIENT_MAX) {
    /* page the grid, or style only the visible window - a virtualized list
       declares only what is on screen, which keeps you under the cap for free */
}
```

**The cap counts distinct ids in a frame, not elements in your model.** Both features are keyed by
the element's `.id`, and an id-less element cannot carry either at all.

## RC_CHART_MAX_SERIES

`-DRC_CHART_MAX_SERIES=N`, default 16, legal range **1..16**.

**It is a public, readable constant**: it lives in `rayclay.h`, so you can design against it
rather than only set it. That is what a public cap is for:

```c
RC_Series series[RC_CHART_MAX_SERIES];      /* size an array against it */
#if RC_CHART_MAX_SERIES < 4
#  error "this dashboard needs four series"  /* or refuse at compile time */
#endif
```

**Define it for every translation unit.** This is the `[both]` class: your code and the library
each read it, and setting it on one side only gives you an array sized to one cap while the library
clamps at another: **a silently truncated plot, not a build error.** Put it in your build's compile
definitions.

**Both ends of the range are refused, and the floor is the one that matters.** Above 16 the
auto-colour palette wraps and two series get the same rgba, which no reader can attribute. At 0 the
payload's series array becomes zero-length while the library still writes a whole series through
`&series[0]`, and gcc and clang accept that as an extension and diagnose **nothing**
(measured: zero warnings at `-Wall -Wextra -Warray-bounds=2`). The `#error` is what makes the illegal
value impossible rather than merely discouraged.

Cost is `.bss` only and linear: **1408 B per series**, so 16 costs 11,264 B more than 1. Tune it for
*readability*, not size: past ~12 categorical hues nobody can tell two lines apart, and a caller
with more than that should be setting `.color` explicitly.
`-DRC_NO_UI_HELPERS` removes this knob along with `rcChart` itself: it configures the chart API and
has no meaning without it.

## Chart hover: .tooltip, .hoverGuide, .hoverMarkers

.tooltip = `RC_CHART_TOOLTIP_NEAREST` opts a chart into a hover readout: the x value plus *every*
series' value at the hovered datum. Zero-init is NONE, so existing charts are unchanged. Prefer it
over hand-rolling: the chart owns its plot transform.

**On a multi-series chart, setting .tooltip alone is rarely what you want.** The panel lists the
numbers but cannot say which series each belongs to. The three-field recipe is what you actually
want, and .tooltip deliberately does not switch the others on for you:

```c
.tooltip = RC_CHART_TOOLTIP_NEAREST, .hoverMarkers = true, .hoverGuide = true
```

ex10's chart panel ships exactly that, with a checkbox for each boolean so you can turn them back
off and see what each was contributing; the comparison is the point.

**Which datum is hovered is decided by mark geometry**, not by an option:

```
LINE / AREA    a curve is continuous, so every x has a reading => the datum nearest the
               pointer's x. UNCHANGED.
BAR / SCATTER  a discrete mark has a real rect or disc and there is nothing to read between
               marks => THE POINTER MUST BE OVER THE MARK. No mark under the pointer means NO
               readout.
```

A chart *mixing* the two stays continuous: the curve is readable at any x, even where no bar is.
Marks get a few px of grace at *both* vertical ends, so a near-zero bar (drawn 1px tall) and a
*negative* bar stay reachable.
A bar chart does *not* fire a readout anywhere inside the plot: not above a short bar, not in
the gap between two. So you do not need a hit test of your own around it.
This is not configurable: a readout for a datum the user did not point at would be a bug rather
than a style choice.

.tooltipPlace decides *where* that readout sits, a *different* field from .tooltip (the trigger):

```
RC_TOOLTIP_PLACE_CURSOR (0, default)  follows the pointer, flipping leftward past the plot's
                                      horizontal middle so the panel never leaves the plot
RC_TOOLTIP_PLACE_CORNER               parks it in the TOP corner opposite the pointer's half:
                                      never covers the data, the better default for dense plots
RC_TOOLTIP_PLACE_FIXED                pins it at .tooltipAnchor (any of the nine RC_Anchor
                                      points; 0 => TOP_LEFT)
```

.tooltipOffset is the gap from the anchor, {0} => 12x12, a *distance*, not a direction: the sign is
applied for you.

.hoverGuide / .hoverMarkers (bool) draw the hover *in the plot* instead of only in the
floating panel. **Both** are *independent* of .tooltip: a guide with .tooltip = NONE is a legitimate
choice (crosshair, no panel).

```
.hoverGuide    a vertical rule at the hovered datum's x, drawn UNDER the series so it aids
               reading rather than obscuring it. Vertical ONLY: with a second y axis a
               horizontal rule would have to pick one and be wrong for the other.
.hoverMarkers  one dot per LINE/AREA/SCATTER series at the hovered x, in THAT SERIES' OWN
               COLOUR, with a halo so it stays visible where a same-colour line runs through
               it. BARS ARE SKIPPED: a hovered bar already shows which datum it is.
```

This is what makes a *multi-series* readout legible: the panel lists the numbers but cannot say
which series each belongs to; a colour-matched dot can.

**Cost:** with the pointer parked off the plot the frame is byte-identical whether both are off or
both are on, so an unhovered chart pays nothing for either; this is verified in the test suite.
While hovering, the guide is +1 line and the markers +2 circles per series (the dot and its halo).

## The bundled titlebar (Flat Slab)

Under nativeFrame the runner draws the *bundled* bar for you (title + min/max/close); set
`RC_AppOptions.titlebar.custom` to own the band instead.

A Flat Slab is a square-cornered, full-band-height rectangle carrying your glyph, invisible at rest
and filling with its accent on hover: close reaches a near-solid red, minimize and maximize a faint
wash. No radius, no border, no glow, no press bounce.
**Desktop-only**: on web and mobile these draw nothing and the ids are inert.

**Fixed chrome:** content zoom never resizes or moves the bundled band. It stays pinned to the
window's top edge at a constant on-screen size while the content magnifies or reflows beneath,
exactly like a browser's own titlebar. The band is sized by the *visible window*, never by your
content, so the controls stay on-screen at any zoom and any window size.
Opt the band into zooming with `RC_TitlebarOptions.zoomWithContent`.
**That is the bundled band only.** A `titlebar.custom` band is ordinary content you drew, so it zooms
unless you say otherwise; wrap it in `rcUnzoomed()`, below. The two knobs are inverses: the bundled
band is out by default and `zoomWithContent` opts it *in*; a custom band is in by default and
`rcUnzoomed()` opts it *out*.

## rcAppTime: the clock to animate from

`double rcAppTime(const RC_App *app)`: monotonic seconds since RayClay brought the windowing
system up. It is the only wall clock the API exposes, and it is what a fade, a spinner, a
countdown or a debounce should be driven from.

```c
static void layout(RC_App *app, void *userData) {
    float t = (float)rcAppTime(app);
    rcBox(.id = "pulse", .bg = rcAlpha(RC_SKY_400, (int)(128 + 127 * sinf(t * 3.0f)))) {}
    rcAppRequestFrameAfter(app, 1.0f / 60.0f);   /* on demand, ask for the next frame */
}
```

**Only differences are meaningful.** The origin is windowing start-up, not a date, not
process start, and there is nothing you may persist or transmit. Take two readings and subtract.

**And the origin is not even uniform across two apps in one process.** After
`rcAppDestroy`, a second `rcAppCreate` restarts the clock near zero on Windows, macOS, X11 and
the web, but *continues* from the first app's clock on Wayland, where RayClay must skip GLFW's
global teardown. ⇒ **Never compare a reading from one app against a reading from another.**

**Do not build elapsed time by summing `rcAppFrameTime`: it is not a shortfall, it is a
stop.** That value is an exponential moving average and it *discards* any interval of 1 s or more
outright so one modal-resize stall cannot tank the readout. Under the default on-demand mode an
app woken about once a second therefore never updates the average at all. Measured, 6 s of wall
clock, in two render modes:

```
on-demand, woken ~1/s      9 frames    summed 0.610s   real 6.006s   ratio 0.102
continuous  (CONTROL)   8069 frames    summed 5.999s   real 6.000s   ratio 1.000
```

The control tracks wall clock exactly, so the shortfall is the render mode and not the
arithmetic. ⇒ **On demand, summing frame times under-reports by about ten times.**

**Under `RAYCLAY_FIXED_DT` this clock is frame-indexed, which is the point.** It advances by
exactly the pinned delta per frame rather than reading the real clock, so a benchmark replays
identically. That is also precisely why pairing `RAYCLAY_FIXED_DT` with
`RAYCLAY_RENDER_MODE=ondemand` is refused: time would advance only when a frame is drawn, an
idle park would produce no frames, and a deadline expressed in that time could never come due.

**Why it takes the app handle** when today there is one window per process: the handle is the
proof that the windowing system is up. A `(void)` form would be callable before that, and the
underlying clock answers differently on desktop and on the web at that moment, which is exactly
the divergence RayClay exists to not have.

## rcAppClearColor / rcAppSetClearColor

```c
RC_Color rcAppClearColor(const RC_App *app);            /* ALWAYS opaque; opaque black for a NULL app */
void     rcAppSetClearColor(RC_App *app, RC_Color color);
```

**The clear colour is the window behind your UI.** It is painted every frame before anything you
draw, and it is the only thing visible on a frame RayClay holds back while it grows its layout
arena. The initial value is `RC_AppOptions.clearColor`.

**Why the setter exists, and it is not symmetry.** `rcSetStyle` changes every colour your UI draws
with, but it cannot reach the window behind it: the clear colour was resolved once, at creation. A
dark/light toggle that does not also call this leaves the *old* theme's background showing wherever
your layout does not cover the window:

```c
rcSetStyle(dark ? rcStyleDark() : rcStyleLight());
rcAppSetClearColor(app, rcGetStyle().background);   /* without this the window stays on the old theme */
```

**The alpha is an input sentinel, never an output.** Alpha 0 (to the option or to the setter) means
"use the active style's background". Every other alpha is *discarded*: the window's framebuffer is
opaque and RayClay never presents a see-through window. So `rcAppClearColor` always reports an opaque
colour, and the colour it reports is exactly the colour painted.

⇒ Round-tripping the getter's value through the setter is a no-op; zeroing its alpha first is a
*reset* to the theme, not a no-op.

**The sentinel resolves once and then freezes.** It reads the theme installed *at the moment of the
call*, a snapshot, not a live link. A later `rcSetStyle` does not move an already-resolved colour,
which is exactly why the setter exists.

**Calling it every frame is safe.** Setting the colour already in force returns immediately, so a
layout callback can set it unconditionally without tracking whether it changed, and an on-demand
app still parks.

**It requests its own frame** when the colour actually changes. That matters for one caller in
particular: a `frameEndCallback` runs *after* the clear and after the buffer swap, so a colour set
there reaches no frame at all. From an update or layout callback the change lands in the frame
already in flight, because the clear is issued after your layout runs.

The getter returns *opaque black* for a NULL app rather than a zeroed colour, whose alpha 0 would
read back as the sentinel and make the documented round-trip lie.

## rcAppTitlebarHeight / rcAppSetTitlebarHeight

```c
int  rcAppTitlebarHeight(const RC_App *app);      /* PHYSICAL px; 0 for a NULL app */
void rcAppSetTitlebarHeight(RC_App *app, int height);
```

**The caption height and the bar you draw are two different things, and on Windows that gap is a
defect the user can feel.** The height is a number you hand the OS; the bar is pixels you draw. They
are set from one number at window creation and then drift apart the moment your bar changes height:
on Windows a bar that folds leaves a vacated strip still swallowing clicks that should reach your UI,
and a bar that grows gets extra pixels the user cannot drag by. (macOS, X11 and Wayland re-read your
drag box every frame, so they follow a folding bar on their own; see below.)

Call the setter whenever your bar's height changes and the two stay in step:

```c
if (app) rcAppSetTitlebarHeight(app, (int)state->barHeight);
```

**Calling it every frame is the intended use.** A value equal to the current one returns immediately,
so a layout callback can set it unconditionally without tracking whether it changed.

**It requests its own frame.** An on-demand app is parked until something asks for a redraw, and a
bar that just changed height has to be redrawn to match the strip that already moved. The setter
calls `rcAppRequestFrame` for you; you do not add one, and the OS region and the pixels cannot
disagree while waiting for unrelated input.

**On the bundled bar it moves the drawn band too.** Under `nativeFrame` with the bundled titlebar
(`titlebar.custom` unset), the setter also updates the band's drawn height, because the runner owns
those pixels and drawing one height while the OS hit-tests another is the same defect inverted. With
`titlebar.custom` the band is your own layout and the setter leaves it alone; you are already
drawing whatever you drew.

**On Windows, a very short strip is not draggable at all, and that is the OS, not RayClay.**
Measured 2026-08-21 against the window manager itself (`WM_NCHITTEST`). Folding `ex20`'s bar from
48 px to 5 px and back:

```text
open      y0-y7 HTTOP    y8-y47 HTCAPTION    y48+ HTCLIENT
folded    y0-y7 HTTOP    y8+    HTCLIENT
reopened  y0-y7 HTTOP    y8-y47 HTCAPTION    y48+ HTCLIENT
```

The caption ends at exactly the value you set, vanishes when you set 5, and comes back. So the
setter does reach the OS. But the top `SM_CYSIZEFRAME + SM_CXPADDEDBORDER` pixels of a resizable
window are the **resize border**, and `HTTOP` outranks `HTCAPTION`. That sum was 4 + 4 = 8 on the
measured machine at 96 DPI; both metrics scale with DPI, so it is larger on a high-DPI display.

So a strip at or below that sum keeps zero draggable pixels on Windows, and because the sum is a
function of DPI and theme there is no fixed pixel value that is safe everywhere. If your folded
state still needs to be draggable, keep a strip comfortably taller than a resize border rather
than a hairline, and treat "a few pixels are still draggable" as something to check on the target
machine rather than assume. Other platforms do not reserve the same band, so this is one of the
few places where an identical `titlebarHeight` gives you genuinely different behaviour.

`height <= 0` means "no draggable strip". The getter reports the resolved caption height in
*physical* px: the units the OS strip is in, not zoom-scaled layout px.

**It does not move the window's minimum height.** That is resolved once at creation from the *open*
bar, deliberately: a folded bar must not license the window to shrink below what the open bar needs.

**Desktop only**, and only Win32 actually reads the value. macOS, X11 and Wayland drive the drag from
the box you draw with `RC_ID_WINDOW_DRAG` and re-read it from your layout every frame, so they follow
a folding bar already. Calling it there is harmless and keeps one source portable: write the call
once and every desktop behaves.

## rcUnzoomed

`rcUnzoomed() { ... }` holds a subtree at a constant on-screen size whatever the content zoom is:
the desktop answer to "this part is chrome, not content". Ctrl +/-/0 and Ctrl+wheel zoom your content
like a browser page; a browser's own toolbar does not grow with it, and neither should a custom
titlebar, a HUD or a status strip.

```c
rcUnzoomed() {
    rcRow(.id = RC_ID_WINDOW_DRAG, .h = "46px", ...) { ... }
}
```

**A custom titlebar needs this, and it is why the scope exists.** `RC_AppOptions.titlebarHeight`
freezes the strip the OS lets you drag in *physical* px at window creation. A band that grows with
the zoom desyncs from it, so the bar you *see* stops matching the bar you can *grab*. Measured on a
46px band with `.titlebarHeight = 46`, reading
`rcGetElementBox(RC_ID_WINDOW_DRAG).height × rcAppZoom(app)`, the band's physical height, which
must stay 46:

| content zoom | 0.50 | 0.75 | 1.00 | 1.25 | 1.50 | 2.00 |
|---|---|---|---|---|---|---|
| plain band | 23.0 | 34.5 | 46.0 | 57.5 | 69.0 | 92.0 |
| inside `rcUnzoomed()` | 46.0 | 46.0 | 46.0 | 46.0 | 46.0 | 46.0 |

At 2× the drawn band is exactly twice the configured height; at 0.5×, half. Measured on Linux/Xvfb with
both sizing spellings (`.h = "46px"` and `.hType = RC_PX(46)`); they are different parse paths and
behave identically.

**What is scaled:** fixed sizes (`.w`/`.h` `"48px"`, `RC_PX`), padding, gaps, border widths, corner
radii, floating offsets, text size / lineHeight / letterSpacing, and icon + `rcSvg` boxes.
**What is not, because it is already right:** `"grow"`/`"fit"` (parent- and content-driven), `"%"`
(parent-relative), and `vw`/`vh` (viewport-relative, and the layout viewport is itself window ÷ zoom).
Scaling any of them would double-apply.
⇒ **You do not multiply anything yourself.** Write the px you want on screen.

**The band's height must be the same number you passed to `.titlebarHeight`.** Get it wrong and the
desync is a small *constant* at every zoom rather than a proportional one (2 px for a `"48px"` band
against `.titlebarHeight = 46`), which is much harder to spot.
**On Windows this matters most**, because there the configured height *is* the drag region, so the bar
you see stops being the region that drags; on macOS, X11 and Wayland the drag follows the box you draw
and stays in step by itself.
**A debug build checks it for you** and warns once, naming both numbers. It waits for the zoom to
settle, so a transient during a zoom step will not cry wolf, and it is compiled out under `NDEBUG`:
a Release artifact carries none of it.

Scopes nest, and nesting does not compound: an inner scope is still 1÷zoom, not 1÷zoom². `break`
inside the block still closes the scope. `rcUnzoomedScale()` returns exactly 1.0 outside any scope,
so it is always safe to multiply by; it exists for pixel maths RayClay does not own, such as your own
`RC_CustomDrawCallback`. Everything RayClay lays out inside the scope is already scaled; multiplying
again double-applies.

## rcChart

**Drag-to-zoom / brush:** map the pointer through the *plot* rect, then re-pin .min/.max next frame.
  `rcChartPlotRect`, not `rcGetElementBox`(chartId), because the plot sits inside the box you gave the chart:

```c
RC_Box plot = rcChartPlotRect("res");   RC_Vec2 p = rcPointer();
if (plot.found && plot.width > 0.0f) {
    float dataX = lo + (p.x - plot.x) / plot.width * (hi - lo);
    ... latch dataX on rcPointerPressed, track on rcPointerDown, commit on rcPointerReleased ...
}
```

**The width check is the readiness test, not `.found`**. See `rcGetElementBox`.

Full worked version, incl. clamping and the degenerate-drag case: widgets.md ▸ "Zoom, pan and brush".

## rcEndTable

Columns declared per call: `RC_TableColumn`{ .header (NULL/"" => none; *all* none => no header row), .w (CSS
  width string "48px"/"25%"/"grow"; "fit" warns + falls back to GROW) or typed .wType
  (`RC_PX`/`RC_PCT`/`RC_GROW`/`RC_VW`; zero-init = GROW; .wType wins if both set, and the runtime warns "a column
  sets both .wType and .w; .wType wins"), .align ("<Y><X>", e.g. "cr" =
  right-aligned numerics) }.
Cells are *your* layout (text, a sparkline, chips); rows are *fit*-height (tallest cell wins), cells stretch to
  the row. Zero-init `RC_TableOptions` => 6px cell padding. No sort/filter/reorder/resize: sort your own
  array. Virtualising *is* supported; see the big-table form below.

## Big tables: virtualize the row loop

Big tables (thousands of rows): wrap the row loop in `rcVirtualList`; only the visible rows are declared, so
  the frame cost goes flat in row count (cost model + measured figures under "Long lists" above). Layout
  charges per *declared* element, not per visible one, so this is the only thing that makes a huge table
  cheap.
  **In a table the pitch is not the row height.** Every cell carries the table's cellPadding top *and*
    bottom, so the distance from one row's top to the next is (row height + 2 x cellPadding). Pass the
    row height alone and the spacers under-report the content by 2 x padding *per row* (a third of the
    list at these sizes), and the scrollbar stops agreeing with the rows. Nothing warns.
  So *declare* the padding with `RC_VAL` and derive the pitch from it, as below. Inherit the default
    instead and the pitch depends on a number your call site never names, which is the real hazard.
    (Zero-init still means the house 6 px; `RC_VAL`(0) is genuinely flush. See `RC_OptFloat` in the types list.)

## rcComponent

`rcBox`, `rcRow`, `rcColumn`, `rcSeparator` and `rcMargin` are each one line:

```c
#define rcBox(...) rcComponent(rci_core_box_defaults, __VA_ARGS__)
```

so `rcComponent`(defaults, ...) is the seam for a *sixth* one of your own. It takes the same flat
`RC_ComponentOptions` designated initialisers every element takes and layers them over a defaults
record, so a call site says only what is different about that element. ex20_system_monitor's
`card` macro is the worked example, including why the defaults belong in the second argument
rather than pasted in front of __VA_ARGS__.

**It pairs Begin and End for you**, which is the reason to prefer it over calling
  `rcBeginComponent`/`rcEndComponent` yourself. The expansion is a double `for` whose second clause
  closes the element, and that is what makes `break` out of a body safe where `goto` and `return`
  are not: those skip the close.

rci_core_component_escaped() has external linkage and appears in the expansion. A macro defined
  in a public header must call something the consumer's translation unit can link, so this
  reporting hook is exported; the rci_ prefix is the signal that you never write it yourself. It
  is how a body left early earns its one-time warning.

## RC_IconPoint

The geometry a generated icon header is built from: one point in the SVG's viewBox space, which
the library scales to the element's bounds at draw time. `RC_ICON_MAX_PTS` (128) caps how many
points a single generated polyline or polygon may carry.

You name these only if you hand-write an icon instead of generating one with
ex11_rayclay_icon_converter. `rcIconEmit` plus the rcIconDraw* family in
examples/assets/icons/rc_icons_common.h are what a generated header actually calls.

**Regenerate a generated icon header from its SVG** rather than hand-editing it: the generator and
  the library are versioned together, so an edited header is a fix with a timer on it.

## Loading an SVG at runtime

`rcSvg` draws an .svg *by path*, straight in your layout. There is no generator step, no compiled
icon header and no build dependency:

```c
rcSvg("assets/logo.svg", 48.0f, s.text);            /* that is the whole thing */
```

Nothing in your app struct, nothing to load, nothing to free. The first frame that names a path
parses it, every later frame reuses that parse, and the library frees it at `rcCloseWindow`.

`rcLoadSvg` / `rcLoadSvgFromMemory` parse into an opaque `RC_Svg` handle that *you* own, and
`rcSvgHandle` draws from one. That is the route to reach for when the markup has no file behind
it, or when you want to free on your own schedule:

```c
RC_Svg *logo = rcLoadSvgFromMemory(LOGO_SVG, (int)(sizeof LOGO_SVG - 1));
...
rcSvgHandle(logo, 48.0f, s.text);                   /* per frame          */
...
rcUnloadSvg(&logo);                                 /* NULLs your pointer */
```

**Colour is a per-call argument, not a property of the artwork.** The same SVG can be drawn at two
  sizes in two tints in one frame, and re-tinted every frame, with no reload. A *zero-alpha* colour
  means "unset" and resolves to the active style's text colour, so a mono icon stays visible on
  either theme rather than becoming transparent. Artwork with its own baked colours ignores the
  tint: the colour is then part of the drawing, exactly as it is for a generated icon.

**Both draw calls end in `rcIconEmit`'s (size, color)**: `rcSvg`(path, size, color) and
  `rcSvgHandle`(svg, size, color), precisely as a generated icon header does. Moving between any
  two routes is a one-line change, which is what makes the choice below a question about your
  *build* rather than a rewrite.

**Which route**: four, and the first is the one to reach for. All four end in the same icon ops and
the same drawing code. examples/ex24_svg_live draws three of them at once on one artwork.

  1. `rcSvg`("path.svg", size, color): **the default**. No app state, no load, no unload.

  2. `rcLoadSvgFromMemory` + `rcSvgHandle`: the markup is a static const char[] in your source, or
     a string your program built this frame. One executable, no converter, no asset files.
  3. `rcLoadSvg` + `rcSvgHandle`. Own the lifetime: free on your own schedule, or swap which handle
     is drawn from frame to frame.
  4. A generated icon header (ex11_rayclay_icon_converter). The parser stays out of the
     binary entirely, there is no parse at startup, and the icon *cannot* fail to load.

**Any route that names a path**, `rcSvg`("...") or `rcLoadSvg`("..."), **costs you the
  single-executable property, and nothing warns you:** the artwork becomes a file on the end user's
  disk that has to ship, install and stay put. That is the trade route 1 makes for its
  convenience, and for most apps it is the right one. When it is not (a single binary you can
  email), route 2 buys the property back for the price of a string literal, with no converter and
  no build step. The two are one line apart, so this is a decision you can defer.

**What route 4 still buys over route 2**, all small but real: 86,080 B of parser stays out (measured
on ex24, Release + -DRC_SIZE_OPT=ON, stripped; ex24 *calls* `rcLoadSvg` directly, and the saving for
an app that reaches the parser only through `rcSvg`'s cache may differ); the artwork is
compact point arrays rather than XML text; there is no 21-41 µs per-icon parse at startup; and a
baked icon cannot fail to load.

### How rcSvg's cache behaves

**The key is the path's bytes**, not the pointer, and the cache keeps its own copy. So building a
  path into a scratch buffer every frame works exactly as well as a string literal, and two call
  sites naming the same file share *one* parse; they do not each get their own.

**A failure is cached too**, and that half is what makes the shape safe at all. A missing or
  unparseable path is opened *once* and logged *once*; every later frame draws nothing, in silence.
  Without it a typo'd path would re-open a missing file sixty times a second and bury the very
  log line it was writing. Measured on ex24 with a deliberately wrong working directory: 60
  frames, *two* call sites naming the same missing file, *one* "cannot open" line.
  **The message names the function you called** (`rcSvg`, `rcLoadSvg` or `rcLoadSvgFromMemory`), even
  though all three reach one loader, so a log line always points at code you actually wrote.

**A path that fails is remembered as failed for the process.** Fixing the file on disk while the
  app is running will not bring the artwork back; the miss is cached exactly as a hit is. Restart,
  or take route 3 and control the loads yourself.

## RC_SVG_CACHE_MAX

How many *distinct* paths `rcSvg` will hold at once. Default 64, and it is **non-evicting**: past the
limit the call warns once and draws nothing.

**Not evicting is the deliberate choice.** An LRU would keep working and silently re-parse on every
  frame, which surfaces as an unexplained frame-time collapse rather than as a limit you have hit.
  Warning once and drawing nothing is louder and cheaper to diagnose.

⇒ **An app that cycles through hundreds of SVG files wants route 3**, not a bigger number. `rcLoadSvg`
  plus your own lifetime policy is the shape for a file browser or an icon picker over an
  arbitrary directory; the cache is for the fixed set of artwork an app ships with. Raise the knob
  when your app has, say, 90 fixed icons; reach for handles when it has an unbounded set.

**RayClay does not rasterise SVG**: shapes become icon ops. path, line, polyline, polygon, rect,
  circle and ellipse are supported. Gradients, <text>/<tspan>, filters, <image> and <use> are
  skipped with a warning that names the family and the way out. A partial result is normal, not a
  failure. For artwork that needs any of them, export a PNG and use `rcLoadImage`; that is a
  picture rather than an icon, so it will not re-tint.
**`<defs>`, `<mask>`, `<clipPath>`, `<pattern>` and `<symbol>` are a different case, and the one
  most likely to reach you from a real export.** The element itself contributes no geometry, but
  RayClay walks the tree, so shapes *nested inside* one may still be collected: a `<clipPath>`
  holding a full-bleed `<rect>` can put that rectangle on your icon. `<defs>` does not even warn.
  ⇒ **Delete or flatten those subtrees in your editor before exporting.** Illustrator and Figma
  emit them routinely (`<defs><clipPath id="SVGID_1_"><rect .../></clipPath></defs>`), and an
  editor round-trip is the reliable fix rather than anything you can pass to `rcSvg`.

### Path data: what parses, what warns, and how much of it fits

**Both arc-flag spellings parse.** The SVG grammar defines a flag as a single
character, so a minifier may glue it to what follows. `A40 40 0 0 1 45 30` and the compact
`A40 40 0 0145 30` are the same arc, and RayClay reads both. A flag that is neither `0` nor `1` is
refused rather than coerced. ⇒ **svgo, Figma and Illustrator minified output is safe to feed to
`rcSvg`.**

**A path command RayClay cannot read logs `malformed path; skipped incomplete tail`**: one warning
per document, through your `rcSetLogSink`. It is worth knowing what that does *not* mean: the parser
skips forward to the next command letter and *carries on*, and every point accumulated before the bad
token is kept. You get a *partial* path, not a dropped file. The two ways to reach it are an arc flag
that is not `0` or `1`, and a coordinate list that runs out of numbers.

**`RC_ICON_MAX_PTS` (128) is a vertex count, and the conversion from your artwork is fixed**, so you
can predict truncation instead of discovering it. Each cubic or quadratic segment flattens to
**16 points**; an arc flattens at **7.5 degrees per step, minimum 4**, so a 90-degree arc costs 12
points and a near-full circle 47. ⇒ **more than 8 curve segments in one subpath will be truncated**,
which is a thing you can count in your editor in a way that "128 points" is not.

**During the parse two further pools bind before those:** 512 shapes and **4,096 flattened points**,
which is *smaller* than the 8,192 of the built icon. Exhausting it logs `shape-point budget
exhausted; geometry truncated`. Nesting is bounded at 64 levels.

### Attributes the parser ignores: the ones that draw your art *wrong* rather than not at all

Read this section if an icon appears but looks wrong: off-centre, the wrong size, a layer you
hid, a solid blob where you drew a ring, or a solid line where you drew dashes. Every one of
those has the same cause, and it is not your layout.

**The class, and why it is harder to spot than a missing element.** An unsupported *element*
  disappears, which you notice immediately and go looking for. An unsupported *attribute* leaves
  the element on screen, *drawn wrong*, so the app looks like it has a layout bug, and you debug
  your own code instead. A single <g transform> can cost three rounds of debugging before the
  attribute is suspected at all, which is why it warns.

Each of these logs one `RC_LOG_WARNING` naming the attribute and the way out:

```
attribute                        what you drew          what RayClay draws
---------------------------------------------------------------------------------------
transform                        art placed and scaled  art at its RAW viewBox coordinates
display="none" / visibility      a hidden layer         THE HIDDEN LAYER, DRAWN
opacity / fill- / stroke-        a blend                fully opaque
fill-rule / clip-rule            an evenodd hole        the hole FILLED SOLID
stroke-dasharray                 dashes                 one solid stroke
```

**One warning per family per document**, not per element. Five transforms in one file produce a
  single line, because the ring holds 16 entries and de-dups on the message itself: a
  per-element message would push every other diagnostic out of the ring.

**On the `rcSvg` route you get it once per distinct path spelling**, for the life of the process. The
  cache parses a path the first frame that names it and never re-reads, so the warning does not
  repeat, and editing the file while the app runs will not produce a second one. Restart, or take
  route 3. **The key is the path's bytes**, so `"a.svg"` and `"./a.svg"` are two entries and *one* file
  referenced both ways warns *twice*. That is the same trade documented under "How `rcSvg`'s cache
  behaves", where it also means two spellings cost two parses.

**The fix for a transform is in the asset**, and it is the one RayClay cannot do for you. Flatten
  the transform in your editor, then reframe the viewBox around the art *as drawn*: Inkscape's
  "Resize page to drawing" then square the page; in Figma, frame the art in a square frame and
  export the frame. **The rule underneath: RayClay maps your viewBox onto the box.** It does not
  compute the drawing's bounding box, so art that sits high inside its viewBox draws high inside
  your element, and .align cannot correct it; .align is placing the box, and the box is already
  where you asked. A worked example: art occupying y 157–585 of a 1024 viewBox renders 9 px high
  of centre in a 64 px slot, and reframing to viewBox="193 52 638 638" brings it to a 10/11 px gap.

**stroke-linecap and stroke-linejoin are ignored too, and deliberately do not warn.** In a set of
  18 art SVGs they appear on 17 and 14 respectively, so warning on them would fire on almost every
  healthy icon you feed the parser, and a diagnostic that fires on healthy input teaches you to
  ignore the channel, including the lines that matter. The bar for warning is "does ignoring it change *what
  is there*", not "is it unimplemented": a mitred corner where you asked for a round one is a
  different order of wrong from a hidden layer that draws.

**The complete list of attributes whose value the parser reads** is 21, and it is short enough to state rather
  than describe. Anything not here is ignored, whether or not it warns:

```
cx  cy  d  fill  height  points  r  rx  ry  stroke  stroke-width  style
viewbox  viewBox  width  x  x1  x2  y  y1  y2
```

  (transform is read only to warn; the other eight warn-only names in the table above are matched
  by name alone and are not in this list, which is "attributes whose *value* RayClay reads".)

**`fill`, `stroke` and `stroke-width` are each read from an inline `style=""` first, and only
  then from the matching presentation attribute**: an inline style wins, exactly as CSS says it
  should.
**A `<style>` stylesheet is not resolved, and `class` is not a name the parser knows.** There is
  no selector engine, and `class` is not in the warn table either, so it fails *silently*. This is
  the single most likely way a real export loses all of its colour:

```
<style>.st0{fill:#231F20;}</style><path class="st0" d="..."/>     <- fill is lost, no warning
<path fill="#231F20" d="..."/>                                    <- what to export instead
```

  Illustrator's "Style Elements" CSS setting produces the first form. Export with **presentation
  attributes or inline `style=""`**, not CSS classes. In a document that has any baked colour
  elsewhere, a shape left with neither fill nor stroke is dropped entirely rather than drawn
  untinted, so the symptom is a *missing* shape rather than a grey one.

**A failed load returns NULL and is not fatal.** The reason is logged, and `rcSvgHandle`(NULL, ...)
  draws nothing rather than crashing, so a missing asset degrades to a gap in the UI. In
  ex24: run it from the wrong directory and it logs `<the function you called>: <path>: cannot
  open` (`rcSvg:` for its by-path panel, `rcLoadSvg:` for the handle it owns), renders
  every other element and still exits 0. ⇒ Check the handle if the art is load-bearing.
  On the `rcSvg` side there is no handle to check; the failure is cached and logged once instead,
  which is the trade that route makes: no error to handle, and no way to ask after the fact.

**A very complex path is truncated, and it tells you.** `RC_ICON_MAX_PTS` (128) caps the vertices in
  one flattened path; a path over it keeps its first 128 and drops the tail when drawn. The parser
  warns at load naming the path and the remedy (simplify the SVG) rather than truncating quietly:
  splitting is not offered on purpose, because splitting a fill seams its triangulation and
  splitting a stroke gaps it.

**Trusted input only**, the same posture `rcLoadFont` documents. The pools are bounded (512 ops,
  8,192 points, 64 nesting levels) and overflow warns rather than grows, but the *container* is
  bounded, the document inside it is not. Two early fuzz findings (a NaN geometry packed
  into a returned handle, and a 47-byte input that hung the parser forever) are fixed, and the
  fuzzer that found them still runs against every change.

**The handle is one right-sized allocation**: sizeof(`RC_Svg`) plus exactly the ops and points the
  document produced, not a fixed worst case. So holding twenty small icons costs about what
  twenty small icons contain, and freeing one is a single free.

`RC_NO_SVG` compiles the parser out and **all five public SVG symbols stay**, so a consumer needs no
`#ifdef` of their own. They are not uniformly silent, and the difference is deliberate: `rcLoadSvg`,
`rcLoadSvgFromMemory` and `rcSvg` warn by name (a call that can never work should say so), while
`rcSvgHandle` draws nothing and `rcUnloadSvg` still NULLs your pointer.
With section-GC on, an app that names none of the five already dead-strips the parser, so the
knob only shows a saving on an app that actually draws an SVG. In particular, calling only
`rcSvg`(path, ...) is enough to keep the parser in: the cache reaches it through the same loader
`rcLoadSvg` uses.
  → docs/cheatsheet.md ▸ build-time configuration

## RC_ICON_POOL_CAPACITY

The pool holds every icon payload kept live in one frame, from the layout pass until `rcRender`
consumes it. It is one process-wide, chunked, grow-on-demand allocator owned by the
library: blocks are added as a frame needs them and are never moved or freed mid-run.

**So there is no per-frame icon ceiling to budget against.** An icon-dense frame grows the pool
instead of failing, and `RC_ICON_POOL_CAPACITY` (256) is the *growth step* (the block size), not a
hard limit. Raising it trades a little idle memory for fewer allocations on a very icon-heavy
frame; leaving it alone is the right default.

**The invariant that matters**, if you are wondering why it is a block list rather than one array:
the layout engine holds these pointers from the layout pass until `rcRender` runs, so a realloc'd
flat array would dangle every pointer already handed out *this* frame. A block list cannot.

**The count is per process, not per source file.** A #define in your own file
cannot raise the pool that `rcSvg` emits from, because that is the library's TU. One name, one
value, one meaning, in both the split build and the amalgamated header.

## clip slots

**There are only 100 clip slots per frame, and a cell that never scrolls still takes one.**
  Every element declaring .clip / .overflow / .scroll consumes a slot whether or not it is
  scrollable, so a *grid* of clipped cells hits the ceiling long before anything scrolls: 8 clipped
  cells per row is ~12 rows. Raising maxLayoutElements does *not* raise this; the slot array is a
  fixed 100.
Reported by a team shipping on RayClay: their table clipped one box per cell and simply
  stopped working past ~12 rows. Past the limit an element *still lays out and clips* correctly;
  what it loses is the scroll offset *outright*, not merely across frames: it is discarded in the
  very frame it is set. Measured with two cells declaring the *same* offset: cell 99 (slotted)
  moved -10.00, cell 100 (past the ceiling) moved 0.00. The two failures compound, so you cannot
  route around one with the other: rcScrollTo* is ignored *and* the offset you set by hand never
  applies.
**The symptom you will actually see**, measured with 120 clipped cells against a baseline of 80:
  the 120th element has a real box (`rcGetElementBox` reports found, 60x12, it renders), but
  `rcGetScrollInfo` reports .found = false for it. So an element that is plainly on screen reads as
  "not a scroll container", and `rcScrollBy` / `rcScrollToBottom` against its id do nothing.
You get told: RayClay logs one warning naming clipping and the remedy (once per `RC_App`, not
  per frame).
**The fix:** clip the *container*, not each cell. A row of plain cells inside one clipped scroll body
  costs one slot.
**For the per-cell overflow itself, there is no ellipsis:** `RC_TextOptions` carries no
  `text-overflow` field, so a cell cannot be truncated with a trailing "…" for you. What you have is
  `.wrap` (`""` lets a long cell wrap to more lines instead of overflowing) or shortening the string
  yourself before you draw it.

## RC_String.isStaticallyAllocated

**A public, writable field that silently renders stale text.** `RC_String` is a transparent alias for
  the layout engine's own string type, so this field is reachable from your code and is part of the
  public surface. Compiled against the public header at -std=c99 -pedantic-errors -Wall -Wextra,
  this is clean C, and wrong:

```c
RC_String s = rcStringFromCStr(p);
s.isStaticallyAllocated = true;      /* renders stale text forever */
```

**The mechanism.** With the flag *set*, the measure cache keys on the *address*
  (`hash += (uintptr_t)text->chars`) plus the length; with it clear, it keys on the *contents*.
  Separately, the scratch arena is a bump allocator (`rcArenaAlloc` advances currOffset) that the
  runner resets once per frame (`rcArenaReset(&app->scratchArena)`).
  ⇒ The Nth `rcFormat` of frame N+1 lands at the *exact* address of the Nth of frame N. Same address,
  same length, flag set ⇒ cache *hit* ⇒ the previous frame's measurement, forever.

```
static=0  f1("AAA")=24.0  f2("WWW")=60.0   correct
static=1  f1("AAA")=24.0  f2("WWW")=24.0   STALE - and zero errors logged
static=1  f1("AAA")=24.0  f2("WWWW")=80.0  correct (a LENGTH change breaks the key)
```

**It bites hardest where it looks safest:** every changing number of *constant width*. "45%"->"92%",
  "2019"->"2020", "1.23"->"9.87" all keep their length, so the key never changes and the old width
  is served with no warning and no error.
**Storage duration is not content stability**, and this is the part that catches people. A
  `static char[]` that you *rewrite* does *not* qualify, even though `static` is right there in the
  declaration. A `static char[N][M]` ring rewritten every frame is the shape that fools people: the
  storage lives forever, the *content* does not. It is
  also why no automatic "is this address stable?" check is possible: such a check passes a stack
  buffer, a refilled malloc, a caller ring and that static char[] alike.
**The rule:** set it only when the **bytes at that address never change** for the life of the program:
  a string literal, or a table you fill once and never touch. Leave it clear for anything from
  `rcFormat`, any buffer you rewrite, and anything you are unsure about. Clear is always correct;
  it costs a content hash.
`rcTextL` literals set it for you, correctly. You never need to set it by hand.

## RC_String.length

**Bytes** (not codepoints, not characters) and **signed** (`int32_t`). RayClay's text is UTF-8, so
a `.length` you compute yourself can cut in the middle of a character: dropping N from it drops N
*bytes*, which outside ASCII is not N characters. Both producers fill it in correctly for you:
`rcStringFromCStr` stores the `strlen` of what you passed, `rcFormat` the formatted byte count.

**It is the whole bound, and the only one.** RayClay does not copy your bytes and is never told
your buffer's size (the three-field `RC_String` is the entire contract), so a `.length` *longer* than
the bytes you own is read past them. An over-long *positive* length is undecidable here, and it stays
your bug.

**A negative length is treated as length 0.** It is clamped as the string enters the layout engine,
so none of your bytes are read and no text is drawn: a safety net for a subtraction that came out
backwards, not an idiom for hiding text.

**"The text disappears" is the whole promise**, not "the element does". Exactly as for `""`, the
element is still declared and still consumes an element slot.

You only reach this field by writing it yourself. `rcTextL`, `rcTextC` and every `rcFormat` result
already carry a correct length; `rcText` forwards the `RC_String` you built verbatim.

## The RC_ value types are transparent aliases

`RC_Color`, `RC_String`, `RC_Dimensions`, `RC_Vec2` and `RC_BoundingBox` are each a *transparent* alias for the
layout engine's own type, so the pair is genuinely *one* type and not a convertible copy.

You should never need the underlying spelling: the RC_ name is the API, and the whole public surface
reads that way: declarations, the `RC_CustomDrawCallback` / `RC_IconCallback` typedefs, the struct
fields (.bg, .overlay, .clearColor …) and the generated icon headers.

`RC_BoundingBox` is **not** `RC_Box`. `RC_Box` is a *query result* (what `rcGetElementBox` and `rcChartPlotRect`
hand back); `RC_BoundingBox` is the geometry the layout engine hands your custom drawer.

## RC_Vec2

**Content** (logical) space: zoom and pan are already undone. Same space as every layout geometry,
  `rcIsHovered` and every chart/table/widget box live in, so comparing it against a layout
  bounding box stays correct at any zoom. At zoom 2.0 a pointer 100 physical px from the
  window's left edge reports x = 50.
**Never** compare it against a raw OS/window coordinate; that desyncs by the zoom factor, and
  preventing exactly that is why the accessor is public.
Latched once per frame (x and y can never straddle a move); reads (0,0) before the first
pointer event.

## The gesture vocabulary: rcPointerDown / rcPointerPressed / rcPointerReleased

`rcClicked` and `rcPressed` both answer "was *this element* activated?", and neither can describe a
gesture still in flight. A drag, a chart brush or a range-select needs the raw *pointer* press edge,
then motion while the button is *held*, then the release, so these four are one unit and you will
use them together.
These are the *pointer*-level reads and they are not element predicates: `rcPointerPressed`(button)
  asks about the mouse, `rcPressed`(id) asks about one element. The names are one word apart.

```
rcPointerDown      the LEVEL you track a drag with
rcPointerPressed   the edge that STARTS a gesture
rcPointerReleased  the edge that COMMITS it
rcPointer          the position, in CONTENT space
```

## Element geometry converts a pointer position into a value

`rcGetElementBox` and `rcChartPlotRect` report where a box ended up, in the same content space `rcPointer`
reports. That pairing is what turns a pointer position into a value: a brush, a drag-pan, a
drag-select, a custom slider, a popover pinned to an anchor, a drop target.

## The two activation edges: rcClicked and rcPressed

`rcClicked` fires on the release edge: a press that started on the element and a release still over
it. Press, slide off, release and **nothing** fires. That cancel gesture is not a nicety: it is the
only way a user can change their mind after committing a press, and every desktop toolkit and every
browser has it. It is why the release edge is the default, and why no bundled widget offers an eager
activation or a flag to ask for one.
Grabbing is not activating: `rcSlider`'s handle and `rcScrollbar`'s thumb take the press edge, because
  a gesture you must release to begin is not a drag. Menu and combo dismissal is press-edge too.

`rcPressed` fires on the press edge, for the control where the wait is the problem: a nudge, a step,
a jump in a game. Swapping one predicate for the other is the whole change; nothing else moves.

**An eager activation cannot be cancelled**, because there is no release left to withhold. That is
  what eager *means*, and it is why it is not the default. Think hard before putting a destructive
  action on it: a mis-aimed press-edge Delete has no undo gesture.

`rcPressed` **is one fire per physical press**. It does not auto-repeat, and the difference matters
  because "eager" and "repeating" sound like the same idea. A hold-to-repeat control (a scrollbar
  arrow that keeps stepping while held) is still `rcPointerDown` + `rcIsHovered` + your own timer.

**Inside a titlebar drag band, `rcPressed` is strictly worse than `rcClicked`.** A press anywhere in
  `RC_ID_WINDOW_DRAG` starts an OS window-move, so a press-edge read there collides with the drag on
  the very same edge and there is no release for your widget to win on. Tag interactive children
  `RC_ID_WINDOW_NODRAG`, and prefer the release edge in that band.

## rcClicked and the pointer cursor

Polling `rcClicked` (or `rcPressed`) also gives that element the web's clickable-hand: while hovered,
the frame's cursor defaults to `RC_CURSOR_POINTER`. Both predicates share one hit-scan, so the hint
costs nothing extra and appears whichever edge you chose.

Opt out app-wide with `RC_AppOptions.autoCursorsDisabled`, or per-element by calling
`rcSetCursor`(`RC_CURSOR_DEFAULT`) after the poll, e.g. a click-to-dismiss backdrop.
RayClay auto-sets the expected shape per component; autoCursorsDisabled turns those defaults off.

## rcModDown and RC_MOD_PRIMARY

Use `RC_MOD_PRIMARY` for app shortcuts (Cmd on a macOS desktop build, Ctrl everywhere else), so they
land on the right key with no per-platform branch.

The pick is made at compile time (__APPLE__), which emscripten does not define. So on the web it
is Ctrl even in a browser on a Mac.

## rcPointerReleased

The drag shape (latch on the press, track while down, commit on release):

```c
static bool dragging;  static float x0, x1;
RC_Vec2 p = rcPointer();
if (rcPointerPressed(RC_POINTER_LEFT) && rcIsHovered("plot")) { dragging = true;  x0 = x1 = p.x; }
else if (dragging && rcPointerDown(RC_POINTER_LEFT))          { x1 = p.x; }         // live edge
else if (dragging && rcPointerReleased(RC_POINTER_LEFT))      { dragging = false; } // commit
```

**A position is only half a mapping.** To turn a pointer x into a value you also need the rect you are
  mapping *into*; that is `rcGetElementBox` / `rcChartPlotRect`, immediately below. A gesture that is purely
  *relative* (a drag-scrub, dragging a card, a custom divider) needs only the delta between two `rcPointer`()
  reads and no rect at all.
On-demand builds: a gesture is pointer motion, so frames come for free while the mouse moves. But if you
  *animate* the commit (an eased zoom), that is your own state changing: call `rcAppRequestFrame`(app) per
  step or it will not draw.

## RC_Box

  `RC_Box` is { float x, y, width, height; bool found; }, in content space, so it compares directly
  against `rcPointer`().
  **One frame behind by construction**: layout runs *after* your callback, so this reports the last
    completed layout: exact while the scene is static (the normal case for a gesture, which spans
    many frames anyway) and one frame stale through a resize.
  **Guard on the extent, not on .found:**   if (b.found && b.width > 0.0f)
    `.found` answers "does this id exist?", **not** "is the rect ready". On an element's *first* frame
    you get found = TRUE with an all-zero rect. The layout engine registers an element when it *opens* and fills
    its box only when layout *ends*, so `if (b.found)` alone divides by zero.
    Use `.found` to catch a *typo'd id*; use the extent to catch the *first frame*.
  `rcChartPlotRect` returns the axes' inner region, excluding the tick-label gutters, the x-axis
    strip and the legend/title row. That is deliberately **not** `rcGetElementBox`(chartId): the chart
    sizes its plot inside the box you gave it (the y gutter grows with the widest tick label, a
    legend takes a header row), so mapping against the *outer* box is wrong by however much chrome
    the chart chose. The plot's own id is internal and hash-falls-back for long ids, so you cannot
    ask for it by name. Same one-frame settle, same .found contract.

## rcSetClipboardImpl

Any undelivered read is dropped
**Desktop is out of the box**: nothing to install, and Ctrl+C/Ctrl+V already work inside `rcTextInput` /
  `rcTextArea` with no app code at all. Verified by round-tripping against a *separate* process on Linux/X11
  and against pbcopy/pbpaste (real system apps) on macOS: both directions, byte-exact, on both platforms
**A read is mirrored into a fixed buffer:** text longer than 4095 bytes arrives truncated, with one warning
  naming both sizes ("text truncated from 5000 to 4095 bytes"). Measured identical on Linux and macOS.
  Writing is not capped; `rcClipboardSet` passes straight through to the platform clipboard. The bundled
  text field's own Ctrl+C copies at most 1024 bytes (it warns once too)
`rcClipboardPoll` returns NULL for "still pending", "denied" and "already collected" alike, so a paste
  needs its own give-up bound rather than an open-ended wait; the bundled text field keeps a frame
  countdown for exactly this. See ex10 ▸ CLIPBOARD for the whole pattern
**Web is out of the box too**: the default web backend *is* the browser's clipboard (navigator.clipboard:
  writeText to copy, readText resolved back through the token protocol). No `RC_ClipboardImpl` needed;
  install one only to *override*. It deliberately does **not** go through the emscripten GLFW shim, which
  stubs glfwGet/SetClipboardString to no-ops; that path leads nowhere.
**Do not feature-detect with `rcClipboardGet`.** It answers only under a synchronous backend, so on the web
  it returns NULL even though the clipboard works perfectly; a set-then-get probe reports "no clipboard"
  on a platform that has one. There is no capability query in the API; the honest test is a real
  Request+Poll from a user action, which is what ex10 ▸ CLIPBOARD does.
**Two browser rules travel with it**, neither RayClay's to relax: the Clipboard API exists only in a secure
  context (https or localhost; a plain-http LAN address has none), and a read also needs a user gesture
  or granted permission, so request from a click handler, not at startup. An absent API or a refused read
  delivers a denial (resolves as "no text") rather than throwing, so a waiting UI stops waiting

## RC_RenderMode

`RC_RENDER_ON_DEMAND`(0, the default: draw on events/deadlines, else sleep at ~0 CPU)

```
RC_RENDER_CONTINUOUS (draw every vsync: games, simulations, benchmarks)   (RC_AppOptions.renderMode;
```

flip live via `rcAppSetContinuousRendering`)

  **Three things override your choice and force continuous**, and none of them announces it:
    `RC_AppOptions.maxFrames` > 0 · RAYCLAY_MAX_FRAMES · RAYCLAY_FIXED_DT. Each one counts frames, and a
    budget is meaningless if the runner may park instead of drawing (an app that parks draws 1 frame,
    not the 88,000 the budget asked for).
    ⇒ **Any bounded run is a continuous run**, so a `RAYCLAY_MAX_FRAMES` smoke test cannot observe on-demand
    behaviour (idle CPU, parking, an unrequested frame), and a bug that only lives there passes it.
    **To bound a run and stay on-demand, use a duration:** `RC_AppOptions.maxSeconds` in code, or
    RAYCLAY_MAX_SECONDS=n from the environment (which overrides the field, so one binary samples several
    durations without a rebuild). Neither is a forcing knob, so on-demand survives both. Expect very few
    frames: an idle on-demand app draws once and parks, so a count near 1 is the *success* signal.
    A frame budget wins over RAYCLAY_RENDER_MODE=ondemand, and says so. A budget only means something
    if the frames actually happen, so maxFrames / RAYCLAY_MAX_FRAMES and RAYCLAY_FIXED_DT each force
    continuous; asking for ondemand on top is ignored with a warning naming which knob won. The run
    still terminates. ⇒ To bound an on-demand run *without* leaving on-demand, use a duration
    (RAYCLAY_MAX_SECONDS): wall clock needs no frames, so it is the one bound that leaves the mode intact.

## RC_PointerButton

Query targets for `rcPointerDown`/Pressed/Released. RayClay's own button space, never a backend's
numbering, so binding a button needs no third-party header.
**"Pointer", not "mouse", because a touch tap arrives as `RC_POINTER_LEFT`.** Mobile is a v1.0 target, so
the same handler serves a click and a tap, which is why these read as pointer state, not mouse state.
Right also opens context menus, so an app polling it sees the same press the menu does.
  Middle is delivered but unused by the library.
Dense from 0, so a compact lookup table may be indexed by the value, but, as with `RC_Key`, the
integer identifies a button only *within* a build: never persist or transmit it.

## Build knobs: where to define them

*Where* you define a knob decides whether it does anything, and getting it wrong is **silent**.

**The rule: define a knob for every translation unit**, in your build system's compile definitions
rather than as a `#define` above one `#include`. Most knobs are read only in the TU the implementation
is compiled in, a few are read in yours as well, and two are read in *both*; defining it everywhere is
correct for all of them and needs no per-knob lookup.
**Three knobs are the exception** and the cheatsheet tags them `[CONSUMER-VIEW]`:
`RC_NO_UI_HELPERS`, `RC_NO_STYLE`, `RC_NO_COLOR_PALETTE`. They trim the *declarations* and must **not**
reach the implementation, which needs the header in full.
**[both]** marks the knobs read on both sides: `RC_CHART_MAX_SERIES` and `RC_DEBUG_TOOLS`. Setting
one of those on a single side is the worst case in this whole section: no `#error`, no link error,
just your code and the library working from different numbers.

```
single header (rayclay.h alone)     the SAME TU that defines RAYCLAY_IMPLEMENTATION, above the #include
CMake (add_subdirectory/FetchContent)   -D on the RAYCLAY target, not on yours
```

**The instinct to avoid** is the stb one: `#define RC_FONT_ATLAS_W 2048` at the top of your own main.c
and then `#include "rayclay.h"`. That TU gets **declarations only**, so the define is read by nothing.
Run-verified both ways on the shipped single header: with RAYCLAY_IMPLEMENTATION a bad value stops the
build with an #error; without it, the same bad value compiles clean and the atlas stays 1024.

**Only three knobs are refused at build time, and the refusal is narrower than it looks.** The usual
advice ("a build knob is silent unless you define it where the implementation is compiled") is true
for most and fatal for three. The three are exactly `RC_NO_COLOR_PALETTE`, `RC_NO_STYLE` and
`RC_NO_UI_HELPERS`.

```
CONSUMER-VIEW    must NOT reach the implementation: these trim the DECLARATIONS, and the
                 implementation needs them in full. The AMALGAMATED header refuses it with #error.
both             read on BOTH sides; one-sided values diverge SILENTLY (RC_CHART_MAX_SERIES,
                 RC_DEBUG_TOOLS)
untagged         read where the implementation is compiled; a consumer-only define does nothing
```

**The `#error` is single-header only.** The single-header build emits those three arms inside
`#if defined(RAYCLAY_IMPLEMENTATION)`, and a multi-TU CMake build from the RayClay source tree never
defines that macro, so in a source build the same mistake is silent again. **Follow the rule; do not
rely on the diagnostic to catch you.**

So a project-wide -DRC_NO_COLOR_PALETTE (via CMAKE_C_FLAGS, or target_compile_definitions on the
library target) **fails the build** with "cannot be combined with RAYCLAY_IMPLEMENTATION in one translation
unit". Define a consumer-view knob on **your own** target only.

And do not reach for the consumer-view three to save space: they trim your view of the header, not
the library. With section-GC on (which the shipped CMakeLists.txt enables unconditionally), code you
never call is already collected, so the size win is ~0. The size levers are all in the implementation
class. (Classified by compiling both kinds of translation unit against the shipped header,
2026-08-04.)

**Test a knob by value, not by existence; then ask which TU is reading it.** Measured on the shipped
header, 2026-08-08, compiling one consumer TU and one RAYCLAY_IMPLEMENTATION TU:

```
knob              your own TU sees    the implementation TU sees
RC_DEBUG_TOOLS    #define'd to 0      0
RC_GFX_DIGEST     undefined           0
RC_GFX_PACKET     undefined           1   <- the packet renderer is the DEFAULT
```

⇒ `#ifdef RC_DEBUG_TOOLS` is true in a stock build, so a guard written that way reports the inspector
*on* when it is off. Write `#if RC_DEBUG_TOOLS`. The other two are defaulted where the library itself
is compiled, so they answer only in that TU.
The RC_NO_* family is the opposite again: never defined by the library, so `#ifdef` is the correct
test there.

## Selecting the alternate renderer

**The spelling depends on which RayClay you build, and one of the two is silently ignored.** If you use
the single-header drop (the normal case, and the only one if you vendored rayclay.h plus its
CMakeLists.txt), `cmake -DRC_GFX_PACKET=0` does **nothing**. It is an unread cache variable there: the
`if(DEFINED RC_GFX_PACKET)` block lives in the development repository's root CMakeLists.txt, which is
not what the drop ships.

```
# works everywhere, including the single-header drop
cmake -S . -B build -DCMAKE_C_FLAGS="-DRC_GFX_PACKET=0"

what you build        -DRC_GFX_PACKET=0    -DRC_GFX_PACKET=2        -DCMAKE_C_FLAGS="-D..=0"
the drop              ignored, packet      ignored, packet          alternate
the development repo  alternate            configure FATAL_ERROR    alternate
```

(Measured 2026-08-09 against the single-header drop, entered the supported way with add_subdirectory, and
against the development tree.)

**The typo hazard is still live for a drop consumer:** `#if 2` is true, so -DRC_GFX_PACKET=2 in
CMAKE_C_FLAGS compiles the packet arm (the very renderer you were escaping), and no configure step
objects. Do not trust the flag you passed; read the arm back out of the built library:

```sh
nm librayclay.a | grep -c rci_gfx_sokol_pkt_      # 0 = alternate, NON-ZERO = packet
```

**Take the zero-vs-non-zero, never the magnitude.** The same property counted 4 on a drop build and 2
on a modular development build, and a third nm scope once read 15. A counter's name is not its unit;
only its being zero means anything here. This has already caught a real consumer: a guard asserting
that the experimental renderer was **not** enabled fired, because the flag they passed had been
silently ignored.

## Shipping on Windows: the console window

A RayClay app built the ordinary way is a **console** binary, so Windows opens a terminal alongside it.
That is fine while you are developing (it is where your `RAYCLAY[WARNING]` lines go), but it is not
what you ship. Nothing in RayClay causes it: it is the linker's default subsystem.

| toolchain | flag | keeps your `int main(void)`? |
|---|---|---|
| **MSVC** | `/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup` | yes: that is what `/ENTRY` is for |
| **MinGW / clang** | `-mwindows` | yes |

```cmake
# CMake, either toolchain. WIN32_EXECUTABLE alone is NOT enough on MSVC:
# it asks for /SUBSYSTEM:WINDOWS, whose default entry point is WinMain, and a
# plain int main() then fails to LINK. /ENTRY:mainCRTStartup is the other half.
set_target_properties(my_app PROPERTIES WIN32_EXECUTABLE ON)
if(MSVC)
    target_link_options(my_app PRIVATE /ENTRY:mainCRTStartup)
endif()
```

**You lose stdout with it**: that is the same switch, not a separate one. Anything you were reading
off the console (RayClay's log included) needs somewhere else to go: install `rcSetLogSink` and write to
a file, or keep a console build for development and flip the subsystem only for the shipped artifact.
**Verified both ways, both toolchains, by reading the PE subsystem byte rather than by looking at the
screen** (2026-08-10): the default build reports subsystem **3 = CONSOLE**, and each recipe above
reports **2 = WINDOWS**, linking with an unchanged `int main(void)`.

## RC_NO_APP_RUNNER

**Supported**, verified on the shipped single header: the implementation TU compiles *and* links
with 0 errors, and a split-loop `main` runs.
**But read this before you reach for it:** dropping the runner means driving `rcRunFrame` yourself,
and the idle win lives in `rcRunApp`'s *loop*, not in `rcRunFrame`. A hand-rolled
`while (rcRunFrame(app)) {}` spins at vsync forever: measured 0.20 vs 115.45 CPU-seconds on the
same scene. Drop L3 to embed RayClay in a host that *already* owns a frame loop, never simply to gain
more control over a loop RayClay would otherwise run for you.

## RC_NO_DEFAULT_APP_ICON

it the 256px size. In the single-header drop that same icon is also ~154 KB of hex source
text you compile: a different unit, and neither number is a proxy for the other. Your own
`RC_AppOptions.iconBytes`/iconPath still works; this removes only the fallback used when you set none.
Free bytes for a build that *always* sets its own icon, and for a Wayland-only or web target, where
an icon set through the API can never do anything at all.
**It does not free the PNG decoder, and that is the bigger number.** Because .iconBytes/.iconPath
keep working, `rcInitWindow` still calls the decoder, so stb_image stays linked whatever you do here.
Measured on ex00_hello (Release, section-GC on, stripped): 779,120 B default -> 754,544 B with this
knob = 24,576 B, the image payload and nothing else. -DRC_NO_IMAGE instead gives 688,872 B
(-90,248 B, and stb_image's symbol count goes 38 -> 0). ⇒ If your app never calls rcLoadImage*,
`RC_NO_IMAGE` is the knob you actually want; this one recovers about a quarter as much.
**Both figures require section-GC**, so check which build you are measuring: the shipped CMakeLists
enables it unconditionally (a vendoring consumer has it); building from the RayClay source tree
instead puts section-GC behind -DRC_SIZE_OPT=ON, which is OFF by default. Without it the same A/B
shows the 24,576 B and none of the 90,248 B.

## RC_NO_LIVE_RESIZE

Keep it (the default) and content redraws live while a window edge is dragged, like a browser;
define it and the window holds its last frame until the drag ends. Desktop-only, and the loop
it works around is Windows/macOS; see getting-started ▸ "resize".
**Not covered by CI:** no build preset or test defines this, so unlike the other knobs here
  nothing defends it against a future edit. Build it once yourself before you depend on it, and
  that is not hypothetical: a guard-only mistake here (a helper defined inside the guard
  and used outside it, so the implementation TU would not compile) builds cleanly in every
  other configuration, and an uncovered knob is exactly where that survives.

## RC_GFX_MSAA_SAMPLES

**The largest safe memory dial** in the library: the multisampled framebuffer is allocated at full
window size for *every* sample.

**You do not have to rebuild to compare arms.** `RAYCLAY_MSAA=1|2|4` overrides the compiled default
for *one* process, so an A/B holds the binary constant and the arm becomes a property of the *run*:

```
RAYCLAY_MSAA=1 RAYCLAY_GFX_STATS=1 ./myapp     # 1 is how "off" is spelled
RAYCLAY_MSAA=4 RAYCLAY_GFX_STATS=1 ./myapp
```

**It takes 1, 2 and 4 and refuses everything else, warning and falling back to the build default**,
including 8, which the macro will accept. 8 was measured coming back as 4 on an M3, and a knob whose
value the driver silently replaces is how a measurement lies.
`RAYCLAY_GFX_STATS=1` prints one line per window (the resolved *request*, what the framebuffer
actually reported, and the backing-store size), so a figure can be attributed to an arm instead of
guessed at:

```
rc_gfx stats: msaa_requested=4 msaa_framebuffer=4 fb=1600x1200
```

`fb` **is device pixels**, deliberately with no DPR column: the logical viewport here is zoom-divided,
so fb/logical is contentScale x zoom and equals the DPR only at zoom 1.0. The count reads `n/a`,
never 0, when it could not be read: 0 is the dummy/web backend's null, and a null printed as a
number is how a blank column becomes a false claim. Both knobs are opt-in and silent otherwise: a
shipped app must not print diagnostics at its user.
Measured on macOS, where these regions are actually accounted:

```
ex00_hello      4x = 380.1 MB   vs  1x = 327.0 MB   physical footprint
bare GL window  4x = 192.8 MB   vs  0x =  90.5 MB
```

**Whether your tooling can see this depends on where the buffer lives**. There are three regimes,
  and "Linux under-reports it" is true of only one of them:

```
discrete GPU:   the buffer is in VRAM, so process RSS never sees it. A flat RSS here means
                your measurement cannot see the cost, NOT that the cost is absent.
unified memory (macOS): accounted to the process; the figures above.
software raster (llvmpipe / Xvfb / most headless CI): the framebuffer is ordinary system
                RAM and shows in RSS in full. MEASURED: a blank 800x600 window,
                4x = 117.8 MiB vs 1x = 87.3 MiB peak RSS, -30.45 MiB, 3 reps, spread <=24 kB.
```

  ⇒ If you gate footprint in CI you will *see* this dial move; if you profile on a workstation GPU
    you may not. Say which you measured on.

Unlike the vertex pool below it trades *quality*, not correctness: at 1x nothing is dropped,
curves and diagonals are simply harder-edged.
**How to decide whether you want it:** MSAA does nothing for axis-aligned rectangles or for text
  (text is sampled from an already-antialiased atlas). That is most of a typical UI. It earns
  its cost on *rounded corners*, chart lines, scatter and pie. Two measured end points:

```
a window that draws no geometry  -> 4x vs 1x is ZERO differing pixels. You pay the full
                                    buffer for nothing; turn it down.
a line chart + rounded cards     -> a real difference (see the measuring note below).
```

**Measuring the visual side:** on an *animating* app a pixel diff cannot answer the question; the
  frame-to-frame churn swamps it (control, same config twice: 37,655 differing px; treatment
  4x vs 1x: 44,537, an 18% separation, i.e. noise). **Count distinct colours** instead: antialiasing
  works by adding intermediate blend colours, and that quantity barely moves as the animation
  advances (same control: spread of 15 colours; 4x vs 1x: a gap of 391, 26x the control).
**The count you ask for is not the count you necessarily get.** GL/EGL read a sample count as a
  *minimum*, so a request of 1 is commonly satisfied with 2 or 4 and the memory you meant to save is
  still spent. RayClay reads the count back off the default framebuffer and **warns once** when
  it disagrees with the request, so *silence* is the confirmation, and a footprint A/B only means
  something if *neither* arm warned. Check the log (`rcSetLogSink`) before trusting either number.
A build being md5-distinct proves the *define* took, never that the driver *agreed*.

**All of that is the desktop story. Web asks a weaker question and gets a weaker answer.**
  Emscripten's GLFW shim maps `GLFW_SAMPLES` onto the WebGL `antialias` context attribute, a
  *boolean*, which the spec lets a GPU ignore, so on web there is no negotiated count to compare
  against, and the desktop equality test above is not compiled into a wasm build at all.
  RayClay asks the one question that *is* answerable there ("did we get any?") and warns
  once if the browser refused outright.

```
desktop  "requested 4x MSAA, the framebuffer reports 1x"   <- a COUNT disagreed
web      "this browser refused the antialias request"      <- there was NO antialiasing at all
```

  ⇒ **On web, silence means "some antialiasing happened", not "you got four samples."** A browser that
    hands you 2 where you asked for 4 is silent, correctly: the count on web is the browser's choice
    and RayClay reporting it as a disagreement would fire for your users and stay quiet for you.
  **What to do with that:** on web, treat the sample count as a hint you cannot hold the platform to.
    Do not build a design whose readability depends on 4x edges; the fallback is single-sampled,
    which is legible but harder-edged on rounded corners, chart lines and icon strokes. Text is
    unaffected on every platform (it is sampled from an already-antialiased atlas).
  **How often a browser actually refuses is unmeasured.** The warning is proven to fire and proven to
    stay quiet, but the population that would trigger it in the field (low-end and mobile GPUs,
    software rasterisers, drivers that drop MSAA on a large canvas) has not been surveyed.
  The two wordings are exclusive per target: a Linux Release `librayclay.a` carries only the count
    sentence, a wasm `librayclay.a` only the refusal sentence, and each is absent from the other.

## RC_DEFAULT_START_LAYOUT_ELEMENTS / RC_DEFAULT_MAX_LAYOUT_ELEMENTS

Compile-time defaults for `RC_AppOptions.startLayoutElements` (2048, ~1.4 MiB) and maxLayoutElements
(65536). The -D exists for the case the runtime field cannot reach: an embedded or MCU target that
must cap the arena low without editing source or touching every call site.

**Raising the ceiling is a memory decision, not a speed one.** The arena only doubles on demand, and
per-frame element retirement is bounded by the elements you have actually *used* (the high-water
count), *not* by the ceiling, so headroom you never reach costs you nothing per frame.

## RC_EDIT_BLINK_TIMEOUT

Seconds a focused caret blinks before it *settles* (default 10.0). It settles *on*, never off, so the
field still shows the insertion point; it just stops costing frames.

This is RayClay's one deliberate divergence from browser behaviour (a browser blinks forever) and it
is what takes an idle app the last step to zero: a blinking caret is the only thing RayClay draws
with *no* input at all, measured at 0.0600 CPU-s/min forever.

Set it huge (e.g. 1e9) to restore forever-blink; the knob exists so that A/B is one -D apart in the
*same* source rather than a comparison across two revisions.

## RC_PERF_COUNTERS

-DRC_PERF_COUNTERS=1 unlocks the *one* public function that is not compiled in by default:
`rcAppPerfFrame`(app), returning this frame's work counts (declaredElements, renderCommands, rects,
borders, shadows, texts, images, vertices, clipPushes).
Set it on the **library** target, not just your app. The two mistakes are a link error and a compile
error respectively, so a wrong build never reaches you as silent zeroes.
Off by default and byte-identical when off, so it costs a release build nothing.

## RC_BUILD_SHARED / RC_USE_SHARED

**You** define these by hand; RayClay's CMake does *not*. Define `RC_BUILD_SHARED` when
*compiling* RayClay into a shared library (`RC_API` becomes __declspec(dllexport) on Windows,
visibility("default") elsewhere), and `RC_USE_SHARED` when *consuming* it as one (`RC_API` becomes
dllimport).

**There is no shared build today:** the shipped CMake has no SHARED target and never defines either
macro, so a normal build is static however you configure it. These exist so a consumer who wires up
their *own* DLL build has the export/import attributes already correct: a hook, not a feature.

## RC_SGL_MAX_VERTICES

**Alternate renderer only.** `sgl_setup` sits in the `#else` arm of `#if RC_GFX_PACKET`, and that knob
defaults to **1**, so on a stock build this pool is never created and every figure below applies only
to a `-DRC_GFX_PACKET=0` build. (Measured with the linker, not by reading conditionals: `nm -u` on the compiled renderer
reports **28 undefined `sgl_` symbols at `RC_GFX_PACKET=0` and zero at 1**.) The
*default* renderer's pools are `RC_GFX_PACKET_MAX_VERTICES` / `_INDICES` / `_SPANS`.

It is the one knob here that trades *correctness* for bytes, and it fails **silently**.
It is the vertex pool, and it *is* RayClay's largest GPU cost: the driver commits the whole
declared size on first touch, so a drawing app sits at ~36 MiB where a parked one sits at 22,
and the cost scales with the *cap*, not with what you draw (16K -> 24 MiB, 64K -> 25, 256K -> 36).
That makes lowering it look like an easy ~11 MiB win. It is not: past the cap sokol_gl *discards*
the excess geometry and simply draws less of your UI.
Measured: a -DRC_SGL_MAX_VERTICES=24576 build of ex10 (the shipped widgets gallery, not
some pathological scene) logs "sokol_gl buffer overflow - some geometry was dropped" and *still*
exits 0 reporting every frame rendered. The warning fires *once* per process, so the realistic
outcome is a silently clipped UI you cannot see the cause of.
**The headroom is not slack to reclaim.** ex10's peak sits between 24,576 and 28,672 verts on a
1x display - about 9x under the default. Corners tessellate to a quarter-pixel budget rather than a
flat segment count, which is what keeps the peak low - and it also means the peak scales with *physical*
pixels instead of with your UI alone: it rises with radius x DPR x zoom, so a 2x panel or a
zoomed-in window asks for more than any 1x figure quoted here. The headroom absorbs that.
Leave it alone; if GPU memory matters to you, say so upstream rather than shrinking this.

## RC_SGL_MAX_COMMANDS

**Alternate renderer only, exactly as for the vertex pool above:** at the default `RC_GFX_PACKET=1` this
pool is never created, so the figure below is not in a stock build's ledger at all.

One entry per pipeline/texture/scissor span. Same overflow path and the same one-shot warning as
the vertex pool, so the *correctness* advice is identical: do not shrink it.
But the *memory* story is the **opposite** of the vertex pool's, and the difference is the whole
point. This pool costs 160 B per unit: a 32-byte command *plus* a 128-byte uniform slot that
sokol_gl sizes from the *same* knob, so 16K reserves ~2.5 MiB. That is **reserved, not resident**:
it is allocated uncleared, so untouched entries never fault in, and a pathological peak of
~2,163 commands touches ~346 KB of it. Cutting 16K -> 4K frees 1.875 MiB of *address space* and
almost no RSS, while cutting headroom from 7.6x to 1.9x. The vertex pool above is the reverse
The driver commits it on first touch, so that one genuinely is resident.
So if you are hunting RSS, this knob is not where it is. Measure before you tune either.

## RC_FLATNESS_TOL: how finely curves tessellate

-DRC_FLATNESS_TOL=f, default 0.25f. The largest distance, in *physical* pixels, that a drawn curve is
allowed to sit from the true circle it approximates. Rounded corners, circles and arcs are subdivided
until they are inside it, so this knob sets how many vertices your curves cost.

**It is the other half of the vertex-pool story above.** Because the budget is in *physical* pixels, the
segment count rises with radius x DPR x zoom, the same reason a 2x panel or a zoomed-in window asks
for more vertices than any 1x figure. Raising the tolerance buys vertices back by drawing visibly
flatter curves; lowering it spends them for smoothness you may not be able to see.
**Measure first.** The default is already a quarter of a physical pixel, which is below what a display
resolves; there is far more headroom in the pool than there is quality to gain here.

## RC_FONT_LAST_CODEPOINT: widening the baked range

-DRC_FONT_LAST_CODEPOINT=N, default 255 (ASCII + Latin-1, the field norm). 0x17F reaches Latin
Extended-A (Polish, Czech, Turkish).

**Three things travel with it**, and without all three the knob is a false promise:
  1. **The bundled font does not have those glyphs**: it is a Latin-1 subset (191 glyphs in its
     cmap), so raising the cap *alone* changes nothing visible. Supply a face with the coverage
     via .fontPath.
  2. **Cost is linear in the cap, not in use.** The glyph table is direct-indexed, so every codepoint
     from 32 to the cap reserves a slot in *every* loaded face. Measured: 0x17F costs +58,368 B
     of .bss (1.49x).
  3. **Atlas area** is a separate, shared ceiling that *degrades* (oversample halving, then '?')
     rather than failing loudly; see `RC_FONT_ATLAS_W`.

This is *not* a CJK switch and cannot be used as one.

## RC_FONT_ATLAS_W

**Both ends are hard `#error`s**, not clamps; the build *stops*, it does not quietly fix you up:
  • below 256, or not a power of two →  "must be powers of two >= 256"

```
• above 65535                      →  "a uint16 uv texel coord cannot address a wider atlas"
```

The ceiling is 65535 because that is the real constraint, so the largest *legal* power of two is
32768. It is not an arbitrary limit: the renderer addresses a texel with a 16-bit uv, and a
sheet wider than that has coordinates it cannot name.
The retained cost is *quadratic* in the dimension and you pay it **twice**: a W*H-byte CPU sheet
(8-bit coverage) plus a GPU texture of the same dimensions. **On the default path that texture is
also one byte per texel:** the atlas uploads as R8 coverage, not expanded RGBA8.
So the 1024 default retains ~1 MB CPU + ~1 MB GPU; 2048 quadruples both (~4 MB + ~4 MB);
256 suits an MCU (~64 KB CPU).
**RGBA8 is the fallback, not the norm**, and it costs 4x on both sides (+3 MiB GPU, +4 MiB host
at 1024; the host side is an expansion buffer that the coverage path never allocates at all).
It is taken only when the backend has no glyph-coverage shader, or when the driver cannot
sample SG_PIXELFORMAT_R8. *Both* cases emit `RC_LOG_WARNING`, so *silence* is your confirmation that
you are on the cheap path; check the log (`rcSetLogSink`) before costing an atlas change.
The *bundled font* is not the lever here: the Latin-1 Roboto subset is 17,488 bytes, ~1.6% of
a built binary, against a retained atlas measured in megabytes. Swapping or dropping it
(`RC_NO_BUNDLED_FONT`) saves kilobytes, not megabytes.
Raise it only for a large glyph range (e.g. CJK).

## RC_SIZE_OPT

(CMake build option, not a header define; it configures the build, so it is invisible if you vendor
rayclay.h and compile it yourself) aggressive size posture: LTO + section GC + static stb_truetype;
it also sets `RC_DEBUG_TOOLS`=0, which is already the default, so the inspector is not part of
what this option buys you
**The biggest size lever is not a RayClay knob at all: it is your own compiler flags.**
  -ffunction-sections -fdata-sections   (compile)      MSVC: /Gy /Gw
  -Wl,--gc-sections                     (link)         MSVC: /OPT:REF
  Measured on a minimal single-header consumer app, linux/gcc -O2 -DNDEBUG, stripped:
    ordinary flags            923,968 B
    + the three flags above   669,784 B     = 254,184 B saved (~27%)
    + -DRC_DEBUG_TOOLS=0      620,472 B     = 303,496 B saved (~33%), zero code changed
  **Read the third row as your starting point, not as a saving to claim:** `RC_DEBUG_TOOLS` defaults
    to 0, so you are handed it. The gc-sections row is the one still yours to pull, and it is the
    biggest single lever.
  Reproduce the *saving*, not the absolute: your total depends on what else you link. Two independent
    trees measured the gc-sections saving 32 bytes apart (254,184 vs 254,216) and the `RC_DEBUG_TOOLS` delta
    at exactly 49,312 B on both, so the deltas are the trustworthy half, and the deltas *survive* the
    default flip, because a delta does not care which side of it you start on.
  **Why the single-header shape creates this**, and why it is easy to lose by accident: the amalgam is *one*
    translation unit compiled with *your* flags. Verified on the shipped rayclay.h: without those flags the
    whole implementation lands in 3 .text sections; with them, 1,136. The linker can only discard whole
    sections, so at 3 it cannot drop a single function you never call.
  RayClay's own CMake sets all three, so if you build RayClay through it, you already have them.
    A consumer who just drops rayclay.h into their own build gets *none* of it, which
    is exactly the case that never sees RayClay's build system.

## RC_DEBUG_TOOLS

**Default 0: you opt in** with -DRC_DEBUG_TOOLS=1. An end user never opens a layout inspector, so
shipping it by default would charge ~50 KB to every app for a developer feature, and the person who
wants the inspector is the person holding a compiler.
**It is a behaviour default, not just a size one.** At 0, `RC_AppOptions.debugToggleKey` and
`rcAppIsDebugEnabled` stop doing anything: the toggle key warns once naming the knob rather than silently
ignoring you, and `rcAppIsDebugEnabled` keeps telling the truth (it just always answers false).
⇒ If you relied on the inspector, put -DRC_DEBUG_TOOLS=1 in your *development* build.
Cost of opting in, measured on the translation unit that compiles the layout core, which is the only one
the inspector lands in; linux/gcc -O2 -DNDEBUG -ffunction-sections: .text 83,446 -> 133,579 B = 50,133 B.
rayclay.h's own header comment reports the whole-binary delta across four toolchains: ~45.6 KB
(linux/clang) to ~74.4 KB (web/emcc). Reproduce the *delta*, never the absolute.
**No linker can do this for you**, and the reason generalises past this knob: the inspector hangs off a
*runtime* flag, so the end-of-layout pass reaches it through a live `if (debugModeEnabled)` branch and it
survives --gc-sections. A runtime flag keeps code alive at link time; only a compile-time macro removes it.
**`RC_DEBUG_TOOLS` is the only knob, and that is enforced.** Clay's CLAY_DISABLE_DEBUG_TOOLS is
derived from it, *one way* and never the reverse, and defining Clay's macro yourself is a hard #error
naming the fix.
`RC_SIZE_OPT` sets `RC_DEBUG_TOOLS`=0 exactly like any other consumer would: no separate mechanism.

## RC_ZoomOptions

Zero-init = zoom on, 25%–500%, the Chrome ladder on the keys, a 10% continuous wheel, layout
reflow, and pan off.

**The keys walk a ladder of stops, not a fixed multiplier.** The bundled table is Chrome's:

```
25 33 50 67 75 80 90 100 110 125 150 175 200 250 300 400 500  (%)
```

so Ctrl+'+' from 100% gives 110, 125, 150, 175, 200 (round numbers a user recognises).

.ladder + .ladderCount supply your *own* stops (ascending, positive, finite, >= 2 entries):

```c
.zoom = { .ladder = (float[]){ 0.5f, 1.0f, 2.0f, 4.0f }, .ladderCount = 4 }
```

**Not copied**: it must outlive the app; a static or literal array is the intended shape. An invalid
table warns once and falls back to the bundled ladder rather than zooming unpredictably.
minZoom/maxZoom always win: stops outside the range are unreachable.

.step **is the opt-out**, not "the keyboard step". Set it
(e.g. 1.05f for a drawing tool) and the keyboard goes *continuous* with the ladder off for that app.
Unset or <= 1 means use the ladder. .wheelStep is unchanged, and **the wheel is always continuous**:
it never consults the ladder. The two compose: a ladder keypress from a wheel-zoomed 137% goes to
the next stop beyond it.

.pan = true turns on drag-to-pan: hold .bindPan (`RC_KEY_NONE` = Space) and drag with the left button
to move a magnified view, Figma-style. **Optical only**: `RC_ZOOM_LAYOUT` reflows into the window, so
there is nothing outside it to reach and the flag does nothing there.
**Off by default on purpose:** a browser has no pan, and out of the box a RayClay app is a browser.
Turn it on for canvas-shaped apps (maps, diagrams, image work), not for ordinary UI.
Space is a *content* key. Panning is suppressed while an `rcTextInput` holds focus, so a space typed
into a field can never drag the view. Rebind if your app holds Space for something else.

**Four independent per-gesture kills**, so you can drop one route and keep the rest:
.inDisabled · .outDisabled · .resetDisabled · .wheelDisabled; e.g. wheelDisabled alone leaves
Ctrl+wheel to your handler while the keyboard steps still zoom.

bindZoom* and `RC_AppOptions.debugToggleKey` take `RC_Key` values (.bindZoomIn = `RC_KEY_I`,
.debugToggleKey = `RC_KEY_F12`). For bindZoom*, `RC_KEY_NONE` (0) means the built-in default (=/-/0)
and the *keypad twin* is always also accepted. For debugToggleKey, `RC_KEY_NONE` (0) means the overlay
toggle is *disabled*: there is no default debug key.
**The overlay is compiled out by default** (`RC_DEBUG_TOOLS`=0), so binding a key is not
enough on its own: build with -DRC_DEBUG_TOOLS=1 or the key warns once and does nothing.
`RC_Key` is an enum (an int in C), so a raw backend keycode like GLFW_KEY_F12 still compiles and
silently binds the *wrong* key. Always name the RC_KEY_* constant.

### RAYCLAY_ZOOM: start at a zoom without rebuilding

`RAYCLAY_ZOOM=<float>` replaces the starting zoom for one run, so a single binary can be sampled at
several zooms. It is read *before* the first layout, so frame 1 is already at the requested zoom: a
zoom that landed on frame 2 would make every first-frame reading a lie.

```
RAYCLAY_ZOOM=1.5 ./myapp      # opens at 150%
```

Precedence and behaviour match RAYCLAY_RENDER_MODE: the environment overrides the app's seed, but
never overrides a *refusal*: an app with .disabled set is not forced into zoom.
**Clamped to .minZoom/.maxZoom and it says so**, naming the requested value and the applied one.
A value that does not parse cleanly, or is not positive, is *ignored* rather than guessed at, with one
warning, so a typo turns into a sentence in the log instead of a degenerate layout that reads as a
RayClay bug.

## RC_TitlebarOptions

Zero-init = the default bar, controls right, a chrome-coloured band, and *fixed* chrome that never
zooms. .title is copied at `rcAppCreate`, so a temporary buffer is fine. .background with 0 alpha
means the theme's chrome colour.

**`.height` at or below 0 means UNSET, not folded.** The band falls back to
`RC_AppOptions.titlebarHeight`, and to the 38 px default only if that is unset too, so zero-initialising
this struct gives you the normal bar with all three controls, not a chromeless window. A negative value
behaves exactly like 0; the test is `> 0`.
Folding at *runtime* is a different mechanism with a different contract: `rcAppSetTitlebarHeight(app, 0)`
does fold the bundled band, and because that band is what draws minimize, maximize and close, under
`.nativeFrame` (where there is no OS chrome to fall back on), it leaves **no close button and no drag
region**, only the resize edges. That is a legitimate way to build a chromeless window and a nasty
surprise if you only meant to shrink the bar. **Fold to a small positive height, or draw your own
`RC_ID_WINDOW_*` controls.**
Only the *bundled* bar behaves this way. With `.titlebar.custom` the band is your own layout, so
`rcAppSetTitlebarHeight(app, 0)` moves the OS strip and nothing of yours disappears, which is why
`ex20` can fold to a rail safely.
And a *positive* height that is very small is a separate trap on Windows: the resize border outranks
the caption, so a strip thinner than `SM_CYSIZEFRAME + SM_CXPADDEDBORDER` keeps no draggable pixel.
See `rcAppTitlebarHeight / rcAppSetTitlebarHeight` above.

**Per-button glyphs.** `.minimize`, `.maximize` and `.close` each take an `RC_TitlebarButtonIcons`:
`normal`, `hover` and `press`, all of them `RC_IconCallback`: `void (*)(float size, RC_Color color)`.
Three rules, and they compose: a NULL state falls back to `normal`; an **all-NULL set keeps the
bundled Flat Slab glyph**, so you override only the buttons you mean to; and the bar chooses the
colour it hands you (the style's text colour, lifted toward white as the hover fill strengthens), so
**a glyph must draw with the `color` argument and never pick its own**, or it goes invisible against
the filled slab.

That signature is exactly what a generated icon header exposes, so an icon you converted drops onto a
window control with no adapter. `examples/ex11_rayclay_icon_converter` does it to its own window:
minimize sets `normal` + `hover` (a chevron that gains a rail under the pointer), maximize sets
`normal` alone (so its hover and press reuse that one glyph), and **close is deliberately left
bundled.** Minimize and maximize are safe to restyle; the control a user reaches for when an app
misbehaves is the one to leave conventional.

.allowControlsClip deliberately accepts a window that can be sized smaller than its own controls.
While the library is drawing those controls it floors `RC_AppOptions.minWidth` so they cannot be
clipped, and warns once if it had to raise your value. It does not apply to `.custom`, which draws no
controls of its own. See `RC_AppOptions` in depth ▸ minWidth / minHeight.

## RC_ComponentOptions

.overlay is an `RC_Color` tint applied to this element **and its whole subtree**:
mix(content, overlay.rgb, overlay.a). White lightens, black darkens (a scrim), a hue washes.
Transparent (the default) means none, and no .id is needed: the engine brackets the subtree.
It is the closest thing RayClay has to CSS `opacity` on a group: one flat quad drawn on top, so it
costs nothing and honours the surrounding clip.
**Square-cornered**: over a rounded element it overhangs the corners by a few px. Prefer it on
square subtrees, or accept the bleed. Use it for a disabled/dimmed section, a modal scrim you own,
or a "selected" wash.

.w / .h and .borderRadius take CSS *strings*, not scalars. A scalar like .borderRadius = 6 compiles
(the field is a char array) and silently drops at runtime.

## RC_TextOptions

.wrap chooses how a run breaks: `""` words (the default) · `"n"` none · `"l"` newlines-only.
**It governs automatic wrapping only.** An explicit `\n` inside your string starts a new line under
**all three values**, `"n"` included. `"n"` and `"l"` both switch off the width-driven breaking that
`""` does; neither of them suppresses a newline you typed.

**If you arrive from CSS, that is the opposite of what you expect.** `white-space: nowrap` collapses
  a newline into a space, so a string you meant to draw on one line comes out N lines tall, a *third*
  of the width the same text takes on one line, and it pushes whatever sits below it down the column.
  Nothing warns.

Measured on Linux, Release, with one text element in an 800x600 window. The top group sizes
a `fit` box, so only a newline can break it; the bottom group forces a 60 px box that the text
cannot fit, so only *width* can break it:

```
--- a FIT box: nothing forces a width break, so any break here is the NEWLINE ---
"alphabravocharlie"      .wrap="n"    box 108x16    1 draw command    <- control: one line
"alpha\nbravo\ncharlie"  .wrap=""     box  41x48    3 draw commands
"alpha\nbravo\ncharlie"  .wrap="l"    box  41x48    3 draw commands
"alpha\nbravo\ncharlie"  .wrap="n"    box  41x48    3 draw commands   <- "n" did NOT stop it

--- a 60px box, spaces not newlines: so any break here is the WIDTH ---
"alpha bravo charlie"    .wrap=""     box  60x48    3 draw commands   <- control: it wraps
"alpha bravo charlie"    .wrap="l"    box  60x16    1 draw command    <- "l" DID stop it
"alpha bravo charlie"    .wrap="n"    box  60x16    1 draw command    <- "n" DID stop it
```

  ⇒ Both directions are measured, and they are the whole contract: `"n"` suppresses the break the
  *layout* would invent, and never the break *you* typed.
  Each row reads two values, and they agree: `rcGetElementBox` for the box the layout gave it, and
  `rcAppFrameCounts().drawCommands` for the lines that actually reached the renderer. (They *can*
  disagree: a line past the viewport bottom keeps its box and emits no command, so a run where they
  diverge means the window was too small, not that wrapping behaved differently.)
  Note what the bottom group costs you: at `"n"` the *box* stays 60 px while the run needs 108, so
  the text is wider than the element holding it. What that looks like is then the ancestors'
  `.overflow`: visible by default, cut inside a `"hidden"`/`"scroll"` container, and gone entirely
  past the layout cull edge, which is the button-label case below. Budget the width or clip on
  purpose; do not leave it to chance.

The rule this leaves you with is simple: **if you want one line, put one line in the string.**
  `.wrap` cannot rescue a string that already contains a break. Where the text comes from your data
  rather than a literal, strip or replace the newlines before you draw it.
`"n"` on a button label can push it past the layout cull edge, where it *vanishes* rather than
  overflowing. Keep labels short.
**For text whose line structure you typed** (a block of code, a log line), reach for `"l"`. A break
  invented by the layout would be a lie about that text, while a break you typed is the content, and
  `"l"` (newlines) is the value that says exactly that. The table above shows `"n"` measuring the
  same here; `"l"` is the one whose *name* matches the intent, so it is the one to write down.

.lineHeight sets the *line box* in px. It is CSS `line-height`, **not** CSS `margin`: it *replaces* the
line's default height instead of adding to it, and the glyph run is centred in the box that results.
0 does **not** mean a typographic metric: the default is the text's *pixel size* (`.size`, or the font
slot's loaded size when `.size` is 0). 16 px text with `.lineHeight` unset gives a 16 px line box, not
the ~19 px a browser's `normal` would give you. Set `.lineHeight` explicitly for body copy.

Measured with 20 px text, so the default line box is 20:

|lines|`.lineHeight`|total height|
|---|---|---|
|1|0|20|
|1|40|**40**|
|3|0|60|
|3|40|120|

**The single-line row shows this directly.** One line has nothing to be spaced *from*, so a field that
*added* space would have to measure 20 there; it measures 40. And three lines at 40 give 3 x 40 = 120, not
60 + 2 x 40.
**A value below the natural height tightens the lines** and will eventually crowd them, because it
replaces rather than pads. Leading is `.lineHeight` minus the pixel size, and it may be negative.
⇒ Pick it as you would pick CSS `line-height`: the px line box you want, comfortably above the font
size for body copy. The examples run 16 px text at 25-27. Because the extra space is split above and
below, that centring is what you align against when a text block sits beside an icon or a control.

.letterSpacing adds N px between characters. Measurement and the renderer agree: both the element
box and the drawn glyphs grow by (glyphs-1) x spacing, so centred and right-aligned text stays put.
0 means the font's natural advance.

.textAlign ("l"/"c"/"r") aligns lines *within* the block width, and is effective only for **multi-line**
(wrapped) text. A single line shrink-wraps to its own width, so align is a no-op there; centre one
line via the parent's .align.

.textAlign aligns **lines inside** the text block; the *container*'s .align is a two-letter "<Y><X>"
code positioning **children**. Different axis, different field. Do not reach for one meaning the other.

## RC_OptFloat / RC_VAL

"Unset" versus "zero", for option fields where 0 is a value a caller can legitimately want.
Zero-init means use the widget's default; `RC_VAL`(x) means exactly x, including `RC_VAL`(0). Same
shape as `RC_Size`: zero-init = *unset*, and a macro to mean it exactly.
A plain float field whose 0 means "default" makes that default *unreachable*, which is why
`RC_TableOptions.cellPadding` uses this form.

## RC_Series / RC_Axis

`RC_Series` requires y + count. Optional: x (NULL => 0..count-1), kind, color ({0} => categorical
palette by index), label (NULL => no legend entry), thickness (LINE width 0 => 2; SCATTER dot
radius 0 => 3), axis (0 = left, 1 = y2), points (LINE only).
y, x and label are *borrowed* until `rcRender`(); the `RC_Series` descriptors themselves are copied.

`RC_Axis` zero-init gives auto-fit with nice-rounded ticks. min/max (both 0 => auto, else the exact
pinned range), label, ticks (0 => ~5, clamped to 16; labels cap at 3 decimals), grid, hide.

## RC_VirtualRow

The `rcVirtualList` loop variable. .index is the *data* row to draw this iteration, the field you
actually use. .first / .last are the declared window, inclusive (.last < .first means nothing to
draw). .count, .rowHeight and .containerId are as passed in. .found is false until the container
has been laid out once.

On that first frame, or with a misspelled id, the window is computed against the whole viewport
at scroll 0, which is correct at the top of a list and bounded everywhere else. A miss that *repeats*
across frames is a typo, and logs one line naming the id, once *per* id, so several broken lists
each report, and ids you snprintf per frame are diagnosed too.

**A second and different diagnostic**, also once per id: "reports a viewport taller than the
whole layout". That is **not** a typo; it is the nested-scroll-container shape. The window is clamped
to the layout, so the list keeps rendering a screenful; give the inner container a height to get
the rest back.

## RC_Float

Floating placement: .to (the attach target, `RC_AttachTo`), .toId (the target element's id, required
when .to = `RC_ATTACH_ELEMENT`), parent/element anchors, offset, zIndex, capture.

**A floating element does not inherit its target's clipping, and there is no opt-out.** A dropdown or
tooltip opened inside a scroll panel draws *over* whatever lies outside that panel, and keeps drawing
there as the panel scrolls. This is deliberate: escaping the container is the whole point of a
menu, and every floating widget here relies on it, but there is no per-element switch to turn it
off. Stop looking for the field: it is not there. If you need it clipped, do not float it.

**When `.toId` names an element not declared this frame, the anchor becomes a zero-size box at the
window origin**, never its last position, and not for one frame only: it stays that way for as long
as the target is missing. Dismissing the row a tooltip or dropdown is pinned to is exactly this shape.

**"The origin" is not where it lands.** Your `.element` anchor still subtracts the float's own
extent and `.offset` is still added, so the result is usually *negative*: off-screen, culled, drawing
nothing. Worked example, the bundled `rcScrollbar`: 8 px wide, `.parent` and `.element` both
`RC_ANCHOR_TOP_RIGHT`, `.offset = {-3, +3}` lands at **(-11, +3)**: x = 0 − 8 − 3, y = 0 + 3, because
TOP_RIGHT subtracts the width in x and nothing in y. **The symptom is a disappearance**, not a widget
stranded in the corner. (The bar itself self-heals: once the scroll container is gone
`rcGetScrollInfo` stops finding it too, so the next frame declares no bar at all.)

**The warning this logs names a field you do not have.** It reports a `.parentId` (the layout
engine's name for what you spell `.toId`) and says the element "is laid out at the origin and is not
clipped", which is the imprecision above. It is logged **once per app**, so later recurrences are
silent: do not read one line as one frame. Declaring the anchor *after* the float in the same frame is
fine and is *not* this error; the message says so itself.

Widgets that declare the anchor and the float in one call (`rcBeginMenu`) cannot reach this. Only a
`.toId` naming an element declared elsewhere in your tree can.

## RC_ScrollInfo

What `rcGetScrollInfo` returns: offsetX/offsetY (distance scrolled from the origin, >= 0),
maxOffsetX/maxOffsetY (content minus viewport, clamped >= 0), and found (false with all zeros when
the id named no scroll container in the last laid-out frame).
DOM convention: positive-DOWN, so offsetY reads like element.scrollTop.

**`.found == false` does not mean you mistyped the id.** An element that declares `.scroll` /
`.overflow` past the **100-clip-slot ceiling** is never registered as a scroll container, so it lays
out, clips and renders (`rcGetElementBox` finds it) while `rcGetScrollInfo` reports `.found = false`
for it. Count your clipped elements before you go hunting for a typo; see "clip slots" above.

## RC_Style

Read by value with `rcGetStyle`(). Idiom: `RC_Style s = rcGetStyle();` then `.bg = s.surface`,
`.color = s.textMuted`.

The four accent bases are the _600 shades and the *Hover ones are _500, one step lighter. So
s.success is **not** `RC_EMERALD_500`; that constant is s.successHover.

**Metrics (radius, padding, gap) are advisory tokens for *your* layout, not widget settings.**
Nothing in the library reads them, so raising s.radius does **not** restyle `rcButton`; its geometry is
fixed. Use them the way you would a CSS variable:

```c
rcRow(.p = s.padding, .gap = s.gap)   // your own containers re-theme in one line
```

The *colours*, by contrast, **are** consumed by the built-in widgets.

## RC_AppOptions in depth

frameEndCallback fires only for a frame that actually *drew*: under the default on-demand
  scheduling an idle frame does not call it, so "per frame" there means per *drawn* frame. It is the
  supported read point for `rcAppPerfFrame`(); see getting-started ▸ "Measuring what a frame costs"
It fires *more* often than maxFrames, so it is not a frame counter: live-resize repaints draw
  outside the main loop and call it too (measured, budget 90 -> 91 calls at rest, more while a
  window is dragged, none under -DRC_NO_LIVE_RESIZE). For "how many frames did this run render",
  read the runner's "rendered N of N budgeted frames" teardown line instead
### maxFrames / maxSeconds

Two bounded-run knobs that are **not** interchangeable. maxFrames forces *continuous* rendering;
maxSeconds is the **only** bound that leaves on-demand scheduling intact. See `RC_RenderMode`.

### iconBytes / iconLength / iconPath

The icon a taskbar, dock or window list shows. Leave all three zeroed and you get RayClay's bundled
icon, so an app that configures nothing still looks like an app. Bytes (an encoded PNG/JPG/BMP you
embedded) beat iconPath, and are the robust form: an embedded icon cannot go missing when the
binary is moved.

**This is the one option whose effect is not the same on every target.** Five behaviours, not one:

```
Windows        the taskbar and title-bar icon
Linux/X11      the window icon (_NET_WM_ICON)
macOS          the DOCK icon: a macOS window has no icon of its own, so the dock is what a user sees
Linux/Wayland  NOTHING. There is no protocol to set an icon at runtime; the compositor matches your
               app_id to an INSTALLED .desktop file, so on Wayland the icon is a PACKAGING job.
               Setting the field anyway is harmless and stays right on every other desktop.
Web            NOTHING. The page owns the favicon; it is HTML, not something a canvas can claim.
```

⇒ Ship the .desktop file if Wayland matters to you. Nothing in this API can substitute for it.

Under `RC_NO_IMAGE` the icon is dropped on **Windows and X11 only**: macOS hands the bytes to the OS,
which decodes them itself, so that leg keeps working. A partial outcome, not an all-or-nothing one.

**Footprint:** the bundled default is 23,898 bytes of embedded image data, 80% of it the 256px size the
macOS dock wants at Retina. That is the image payload, **not** the whole cost: as hex text in the
single-header drop the same icon is ~154 KB of source you compile, and header text, download size,
compile time, binary size and RSS are not proxies for one another. -DRC_NO_DEFAULT_APP_ICON drops
it, which is worth doing if you always set your own, and for a Wayland-only or web build, where those bytes
can never do anything.
It does **not** drop the PNG decoder with it: .iconBytes/.iconPath still need one, so stb_image stays
linked. If your app never calls rcLoadImage*, `RC_NO_IMAGE` saves ~3.7x more; measured A/B and the
section-GC caveat that both numbers depend on are under "Build knobs" ▸ `RC_NO_DEFAULT_APP_ICON`.

### fontPath / fontSizes / fontCount

The font ladder, baked before the first frame. See getting-started ▸ "Fonts" for the
fontId-is-the-load-order-index rule and the fontPath-without-fontSizes footgun.

### fontOversample  (1-8)

*Replaces* the automatic policy on *both* axes, so the 36px oversampling cliff never fires: 2 roughly
halves text error; it costs ~2x the sheet per glyph at ordinary sizes (auto *already* oversamples 2x
horizontally, so you are only adding the vertical axis) but the full 4x out at the ceiling, where
auto has dropped oversampling entirely, so it *halves* your zoom ceiling. Measure your own font
ladder first. Too high is retried at a lower oversample; only a genuinely full atlas drops to the
default font.

### startLayoutElements / maxLayoutElements

The layout arena, counted in *elements*. start (0 => 2048, ~1.4 MiB) is the initial capacity; it
doubles on demand and *never* shrinks again, so max (0 => 65536) is the safety ceiling: at the cap the
runner drops that frame's overflow and logs one error instead of doubling until OOM. Lower start
(e.g. 512) for a memory-tight or embedded build; raise it to skip warm-up growth for a large UI.
Exceeding start costs a few warm-up frames of clean background, never a half-drawn frame.

**Size it for ~2x your per-frame element count if your ids churn**, and a scrolling `rcVirtualList`
churns by design. The layout engine retires a generation at the *end* of the following frame, so two
generations are briefly resident: a 24-row churning list occupies ~50 slots (root + anchor + 2x24),
not 25. Undersizing is not a bug; the arena just grows over a few background frames.

### titlebarHeight

The caption height for a nativeFrame window. **On Windows it is the drag region**; on macOS, X11 and
Wayland it sets the window's minimum height instead, and the drag follows the box you draw. It **must**
match the height you actually draw, or on Windows the region that drags is not the bar you see. The
bundled bar keeps the two in step for you; set this only when titlebar.custom draws a band at a
non-default height. 0 = 38, what `rcTitlebar` draws. Ignored unless nativeFrame.

**It is frozen in physical px at window creation, so a custom band must not zoom.** Matching the
height is necessary and not sufficient: a band you draw yourself is ordinary content and grows with
the content zoom, so the two agree at 100% and nowhere else. Wrap it in `rcUnzoomed()`: measured
above, at 2× an unwrapped 46px band draws 92 physical px against a configured 46.
The element must also be tagged `RC_ID_WINDOW_DRAG`. RayClay moves the window from that element, so
without it RayClay contributes no drag at all, and **what you then see depends on the platform**: on
Windows, X11 and Wayland the window is undecorated and cannot be moved by its title bar, while macOS
keeps its own native title bar and still drags. One source, different behaviour per platform, which is
exactly why the debug build says so. If an app deliberately has no drag region, call
`rcAppSetTitlebarHeight(app, 0)` to say so.

### minWidth / minHeight

The window cannot be resized below this, and a smaller requested size is clamped up at startup.
0 = **derived from the title bar**: a window may shrink to its own chrome and no further (the control
cluster's width by the caption band's height), and this applies *whether or not* nativeFrame is set.

It tracks what the band *actually* draws: 150x38 with the default 3 slabs, 58x38 with minimize+maximize
hidden, and the height follows titlebar.height (80 => x80). Those are *derived*, not constants: hide
a chip or set titlebar.height and the floor moves, so never hard-code them. Ignored on web.

Setting either field *replaces* the library default on **that axis** only: minWidth=900 with minHeight=0
still gets the derived height floor.

Your minWidth is itself *floored* when RayClay draws the window controls, nativeFrame with the
bundled bar: a value small enough to clip them off the band is raised, with one warning naming the
override. Opt out deliberately with titlebar.allowControlsClip. Two other modes honour a tiny
minWidth exactly as written and say nothing: an OS-decorated window, where the chrome is the OS's,
and titlebar.custom, where the band you draw owns its own controls. (In that floored mode the library warns
once per axis (twice for a too-small width *and* height) and names allowControlsClip as the way
out; the opt-out and an already-clearing value are both silent.
The floor it raises *to* is derived from the band, not a constant: 12 + 46*nbtn wide by the band
height, so 150x38 at the default three slabs. Do not hard-code it; change the band and it moves.)

### renderWhileMinimized

false (default) skips layout+render while the desktop window is minimized (iconified): the loop
parks on events at ~0 CPU (no swap = no vsync spin) and redraws the instant it is restored. Set true
for a game/sim/dashboard that must keep updating while hidden. No effect on web (hidden tabs are
browser-throttled).

## RC_GFX_TEXT_CACHE

Off by default. `1` caches text-run quads so a run that has not changed replays instead of re-shaping.
`RC_GFX_TEXT_CACHE_KB` sizes it (default 64, range 4–4096). The default holds one screen of text with
headroom and deliberately refuses to hold two; that is the sizing rule, and it is the one to reason
with. The quad *count* is derived from an internal struct's size and moves when that struct does, so it
is not a number to design against. Both knobs are `#error`-checked, so a typo stops the build rather
than compiling something else.

**It is not pixel-exact against the uncached renderer, and that is a property, not a bug.** The key
excludes position (which is exactly what makes a *moved* run a hit, and the whole prize), so a hit
replays `(abs − origin) + origin`. Measured over 15,680 coordinates (6 strings × 4 sizes × 7 origins):

```
(0,0) (20,60) (3840,2160) (-20000,-20000) (±1e6,±1e6)   ->  0 coordinates differ
(-512,-33)                                              ->  205 differ, worst 6.104e-05 logical px
```

The mechanism is arithmetic rather than statistical. Recording and replaying at the **same** origin is
exact at every origin measured, including a 4K deep scroll: the subtraction is exact whenever origin and
coordinate are within a factor of two (Sterbenz). The divergence needs the origin to have **moved**, which
is precisely the case the key exists to create. It is then bounded by **one float ULP of the origin's
magnitude** (`ULP(x) = 2^(floor(log2 x) − 23)` for float32), so it is **not a fixed figure: it doubles at
every power of two**, and it is constant *within* a binade:

```
|origin|      64      128      256      512     1024     2048     4096     8192    16384
bound   7.63e-06 1.53e-05 3.05e-05 6.10e-05 1.22e-04 2.44e-04 4.88e-04 9.77e-04 1.95e-03
```

Sub-pixel by a factor of ≥1000 at any realistic scroll depth, and that, not the number being small and
fixed, is what makes the divergence acceptable. Quote the bound with the origin it belongs to, or quote
the rule.

**The bound is tight, not conservative**: it is an attained maximum, not a safety margin. At 64 px,
128 px and 512 px the ratio of worst observed error to ULP is **1.000**: the worst case reaches a full
ULP rather than approaching it. Reading an observed error as if it were the bound is easy and wrong in
a specific way: **half an ULP at one origin has the same digits as a full ULP one binade below**
(`½·ULP(2048) == ULP(1024)` exactly), so a figure quoted without saying *which quantity it is* can be
read two ways that both look right.

**Spell the [512,1024) figure `6.104e-05`, never `6.1e-05`.** The attainable maximum there is exactly
`0x1p-14 = 6.103515625e-05`, and the rounded-down spelling sits 3.5e-08 *below* it: an assertion written
to the short form rejects correct output.

**No key can fix it.** Putting position back into the key destroys the hit rate; generating stored
quads at a zero pen makes hit and miss agree with each other while both still differ from the uncached
renderer.

**A pixel comparison that draws each scene once cannot see it.** The hit path runs only when a run is
drawn a second time, so a single-draw comparison exercises the *miss* path alone: passing with this knob
on says nothing about hits. The figures above come from measuring the record-and-replay round trip
directly rather than inferring it from a passing comparison.

⇒ **When to turn it on:** text-heavy scenes that redraw often. **When not to:** anything doing pixel
comparison against an uncached build, and anything where content routinely sits just off the top-left.
