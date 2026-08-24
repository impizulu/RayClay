/* ============================================================================
 *  ex22 - RayClay Reader
 * ============================================================================
 *
 *  An offline documentation reader. It ships its own typography, keeps the
 *  reading column inside a comfortable line length as the window resizes, and
 *  folds its table of contents away when there is no room for it.
 *
 *  WHY THIS APP EXISTS, beyond looking like a reader. Text is the hardest thing
 *  a GUI toolkit does, and three parts of RayClay have no worked example
 *  anywhere else:
 *
 *  1. CUSTOM FONTS, DONE PROPERLY. No other example ships a typeface - a few
 *     read an image or an SVG from examples/assets/, but every one of them
 *     draws in the bundled face. This one registers a real family ladder with
 *     rcRegisterFont and resolves it with rcFont - and it gives each WEIGHT its
 *     own file, which is the mistake that costs people an afternoon. The ladder
 *     is seven of the sixteen font slots, decided at startup and held for the
 *     run - the right shape for an app whose typography is fixed.
 *     Note: a slot can be given back (rcUnloadFont), which is what an
 *     app with an open-ended set of faces needs; this app deliberately does not
 *     use it, because recycling a ladder you never change would be ceremony.
 *
 *  2. rcMeasureText USED FOR SOMETHING. The status bar reports the measure -
 *     characters per line - and the reading column is capped so the measure
 *     stays near the 45-75 range typographers recommend. It is the same public
 *     function the layout engine uses for every wrap point.
 *
 *  3. A LAYOUT THAT RESPONDS FOR A REASON. The contents panel is not hidden on
 *     narrow windows because narrow is bad; it is hidden because keeping it
 *     would push the reading column below its minimum. The breakpoint is
 *     DERIVED from the measure, not picked from a table of device widths.
 *
 *  A CUSTOM APP ICON is set from RC_EX22_ICON. Note: on Wayland the compositor
 *  ignores per-window icons (it uses the .desktop file), so that half is a
 *  no-op there and works on X11, Windows and macOS - docs/getting-started.md.
 *
 *  ASSETS, AND WHAT HAPPENS WITHOUT THEM. CMake injects repo-relative paths, so
 *  the fonts resolve when the reader is launched from the repository root - the
 *  documented way to run it. Launched from anywhere else the registrations fail,
 *  RayClay keeps drawing in the bundled face, and the status bar SAYS SO.
 *  The fallback is the point: a missing font must degrade to readable, never
 *  to blank, and the app must not pretend it got what it asked for.
 *
 *  The faces are subset to the range RayClay bakes (U+0020-007E, U+00A0-00FF):
 *  Lato 73 KB -> 20 KB, Adwaita Mono 1,384 KB -> 19 KB. A full Unicode face is
 *  ~98% dead weight when the atlas stops at U+00FF. Both are SIL OFL 1.1 and
 *  their licences sit beside them in examples/assets/fonts/.
 * ========================================================================= */
#include "rayclay.h"

#include "reader_doc.h"

/* CMake injects these. The defaults are what a reader launched from the
   repository root would use anyway, which keeps the file readable on its own. */
#ifndef RC_EX22_FONT_DIR
    #define RC_EX22_FONT_DIR "examples/assets/fonts/"
#endif
#ifndef RC_EX22_ICON
    #define RC_EX22_ICON "examples/assets/logos/rayclay-logo-1024.png"
#endif

#define FONT_BODY_TTF  RC_EX22_FONT_DIR "Lato-Regular.ttf"
#define FONT_BOLD_TTF  RC_EX22_FONT_DIR "Lato-Bold.ttf"
#define FONT_MONO_TTF  RC_EX22_FONT_DIR "AdwaitaMono-Regular.ttf"

/* ── the type scale ───────────────────────────────────────────────────────
   Seven slots of sixteen; a slot is one (family, weight, size). Fixed here
   because this app's typography never changes.
   Note: a new size at runtime costs a slot. rcUnloadFont gives one
   back, which is what an app with a text-size control needs. */
#define SZ_H1    30
#define SZ_H2    21
#define SZ_BODY  16
#define SZ_SMALL 13
#define SZ_CODE  14

/* ── the measure ──────────────────────────────────────────────────────────
   The column width is derived from the type, not picked in pixels. The first
   version capped the column at a hardcoded 680 px, which sounded reasonable and
   produced a 106-character line - deep into the uncomfortable range the status
   bar was busy complaining about. A pixel cap is a guess about a font; asking
   rcMeasureText how wide the text actually is makes the layout correct for
   whatever face and size the ladder ended up with, including the bundled
   fallback. TARGET_CHARS sits in the upper half of 45-75 so the column uses the
   space it can justify. */
#define TARGET_CHARS    68.0f
#define TOC_MIN_PX      980.0f
#define TOC_W_PX        220.0f

typedef struct {
    uint16_t    h1, h2, body, bold, small, code;
    bool        custom;      /* false => every id above is the bundled face */
    const char *note;        /* what the status bar says about that         */
} ReaderFonts;

typedef struct {
    ReaderFonts   fonts;
    bool          ready;
    bool          sawFontError; /* set by the log sink during registration     */
    unsigned long framesDrawn;  /* the render witness - see main()             */
} AppState;

static AppState state;

/* Counts PRESENTED frames, which is what makes it a render witness rather than a
   liveness one: the runner does not call this for an idle frame it skipped. The
   count is read once, in main(), for the reason documented there. */
static void frame_end(RC_App *app, void *userData)
{
    AppState *st = (AppState *)userData;

    (void)app;
    st->framesDrawn++;
}

/* The level is the verdict, not the wording. Within a font call an ERROR
   means the load was REFUSED and you hold the default slot; a WARNING means the
   face loaded with something worth knowing. The sentence set is open-ended and
   spans three prefixes (rc_font:, rci_font_load:, rcRegisterFont:), so matching
   on text would eventually score a real failure as a success. */
static void reader_log(RC_LogLevel level, const char *msg, void *user)
{
    (void)msg;
    (void)user;
    if (level == RC_LOG_ERROR)
        state.sawFontError = true;
}

/* Register the ladder. One file per weight: bold is a different file, not a
   flag on the regular one. Point both weights at one file and both calls
   succeed, return different ids, and render identical outlines. */
static void reader_load_fonts(AppState *st)
{
    ReaderFonts *f = &st->fonts;

    st->sawFontError = false;
    f->body = rcRegisterFont("Lato", RC_WEIGHT_REGULAR, FONT_BODY_TTF, SZ_BODY);

    /* A 0 return cannot be the test - 0 is the default slot and every
       failure. The log sink is the oracle, which is what the docs tell a
       developer to do and what this app therefore does. */
    if (st->sawFontError) {
        f->custom = false;
        f->h1 = f->h2 = f->body = f->bold = f->small = f->code = 0;
        f->note = "bundled face - launch from the repository root to load Lato";
        return;
    }

    f->custom = true;

    /* REGISTER the ladder... */
    rcRegisterFont("Lato", RC_WEIGHT_BOLD,    FONT_BOLD_TTF, SZ_H1);
    rcRegisterFont("Lato", RC_WEIGHT_BOLD,    FONT_BOLD_TTF, SZ_H2);
    rcRegisterFont("Lato", RC_WEIGHT_BOLD,    FONT_BOLD_TTF, SZ_BODY);
    rcRegisterFont("Lato", RC_WEIGHT_REGULAR, FONT_BODY_TTF, SZ_SMALL);
    rcRegisterFont("Mono", RC_WEIGHT_REGULAR, FONT_MONO_TTF, SZ_CODE);

    /* ...then RESOLVE by name. This is the point of the named ladder: the
       registration knows about FILES, and everything after it asks for a
       (family, weight, size) instead of carrying a file path or an opaque id
       around. Resolving once here rather than per text run keeps the lookup
       off the per-frame path - layout runs every frame, this does not. */
    f->h1    = rcFont("Lato", RC_WEIGHT_BOLD,    SZ_H1);
    f->h2    = rcFont("Lato", RC_WEIGHT_BOLD,    SZ_H2);
    f->bold  = rcFont("Lato", RC_WEIGHT_BOLD,    SZ_BODY);
    f->body  = rcFont("Lato", RC_WEIGHT_REGULAR, SZ_BODY);
    f->small = rcFont("Lato", RC_WEIGHT_REGULAR, SZ_SMALL);
    f->code  = rcFont("Mono", RC_WEIGHT_REGULAR, SZ_CODE);
    f->note  = "Lato + Adwaita Mono - 7 of 16 font slots";
}

/* The average advance of one character in the body face, in pixels.
   A proportional face has no single character width, so this is an average -
   which is why the readout says "~" and why the sample mixes the commonest
   English letters rather than using the alphabet alone. Returns 0 if the text
   cannot be measured, and every caller treats 0 as "no opinion". */
static float reader_avg_char_px(uint16_t fontId)
{
    static const char    sample[] = "abcdefghijklmnopqrstuvwxyz etaoinshrdlu";
    RC_TextElementConfig cfg      = {0};
    RC_StringSlice       slice;
    RC_Dimensions        dim;
    float                avg;
    const int            n = (int)(sizeof sample - 1);   /* NUL not measured */

    cfg.fontId      = fontId;
    cfg.fontSize    = SZ_BODY;
    slice.chars     = sample;
    slice.baseChars = sample;
    slice.length    = n;

    dim = rcMeasureText(slice, &cfg, NULL);
    if (dim.width <= 0.0f)
        return 0.0f;
    avg = dim.width / (float)n;
    return avg > 0.0f ? avg : 0.0f;
}

static int reader_measure_chars(float columnPx, float avgCharPx)
{
    if (avgCharPx <= 0.0f)
        return 0;
    return (int)(columnPx / avgCharPx);
}

static RC_Color reader_measure_color(int chars)
{
    if (chars >= 45 && chars <= 75)
        return rcRgb(126, 200, 140);      /* comfortable                     */
    if (chars > 0)
        return rcRgb(220, 176, 100);      /* readable, but outside the range */
    return rcRgb(150, 150, 158);
}

/* Do not put an .id on a decorative element. These run in a loop, so a literal
   id would be a duplicate on every block after the first - RayClay names the
   collision in the log, and it is the one bug this file could most easily have
   shipped. An element with no explicit id gets one from the library. */
static void reader_block(const DocBlock *b, const ReaderFonts *f)
{
    switch (b->kind) {
    case DOC_H1:
        rcBox(.w = "grow", .h = "fit", .pt = 4, .pb = 12) {
            rcTextC(b->text, .font = f->h1, .size = SZ_H1,
                    .color = rcRgb(238, 240, 245));
        }
        break;

    case DOC_H2:
        rcBox(.id = b->id, .w = "grow", .h = "fit", .pt = 24, .pb = 6) {
            rcTextC(b->text, .font = f->h2, .size = SZ_H2,
                    .color = rcRgb(150, 196, 255));
        }
        break;

    /* The one block that draws the bold weight, and it is why the ladder is
       registered at all. Before this existed the app registered Lato Bold, spent
       one of the sixteen font slots on it, and never drew a glyph in it - an
       example teaching you to register faces you do not use. A slot is not
       recoverable in practice: rcUnloadFont returns the SLOT but stb_rect_pack
       cannot free the atlas rect, so the sheet room is gone for the process. */
    case DOC_LEAD:
        rcBox(.w = "grow", .h = "fit", .pb = 16) {
            rcTextC(b->text, .font = f->bold, .size = SZ_BODY,
                    .color = rcRgb(226, 230, 240), .lineHeight = 27);
        }
        break;

    case DOC_PARA:
        rcBox(.w = "grow", .h = "fit", .pb = 14) {
            rcTextC(b->text, .font = f->body, .size = SZ_BODY,
                    .color = rcRgb(206, 210, 220), .lineHeight = 26);
        }
        break;

    case DOC_BULLET:
        /* A hanging indent, not a bullet character in the text: the marker is
           its own fixed column so a wrapped second line aligns under the first
           WORD rather than under the dot. */
        rcRow(.w = "grow", .h = "fit", .pb = 9, .gap = 10) {
            rcBox(.w = "14", .h = "fit") {
                rcTextL("\xe2\x80\xa2", .font = f->body, .size = SZ_BODY,
                        .color = rcRgb(120, 170, 240));
            }
            rcBox(.w = "grow", .h = "fit") {
                rcTextC(b->text, .font = f->body, .size = SZ_BODY,
                        .color = rcRgb(206, 210, 220), .lineHeight = 25);
            }
        }
        break;

    case DOC_CODE:
        /* .wrap = "l" (newlines). A wrapped line of C is a lie about the
           program, so the block scrolls sideways instead of reflowing - but
           the newlines the listing itself contains are its content and must
           survive. "l" says exactly that: keep every break I typed, invent
           none. ("n" measures the same here; "l" is the one that says why.) */
        rcBox(.w = "grow", .h = "fit", .p = 14, .bg = rcRgb(22, 24, 30),
              .borderRadius = "all-md") {
            rcTextC(b->text, .font = f->code, .size = SZ_CODE, .wrap = "l",
                    .color = rcRgb(196, 214, 190), .lineHeight = 21);
        }
        break;

    case DOC_NOTE:
        rcRow(.w = "grow", .h = "fit", .pb = 14, .pt = 2, .gap = 12) {
            rcBox(.w = "3", .h = "grow", .bg = rcRgb(120, 170, 240),
                  .borderRadius = "all-sm") {}
            rcBox(.w = "grow", .h = "fit") {
                rcTextC(b->text, .font = f->body, .size = SZ_SMALL,
                        .color = rcRgb(168, 190, 220), .lineHeight = 21);
            }
        }
        break;
    }
}

static void reader_toc(RC_App *app, AppState *st)
{
    int i;

    rcColumn(.id = "toc", .w = "220", .h = "grow", .p = 16, .gap = 2,
             .bg = rcRgb(19, 21, 26), .scroll = "v") {
        rcBox(.w = "grow", .h = "fit", .pb = 10) {
            rcTextL("CONTENTS", .font = st->fonts.small, .size = 11,
                    .color = rcRgb(130, 138, 152), .letterSpacing = 1);
        }
        for (i = 0; i < DOC_BLOCK_COUNT; i++) {
            const char *btnId;
            if (doc_blocks[i].kind != DOC_H2)
                continue;
            /* The button needs an id of its OWN. The document id belongs to the
               HEADING (reader_doc.h: "only H2 needs one"), and declaring it here
               too would be a duplicate id every frame: Clay keeps the first
               element under a hash and reports CLAY_ERROR_TYPE_DUPLICATE_ID, so
               rcGetElementBox below would return this button rather than the
               section it is supposed to scroll to. A document id is unique among
               SECTIONS, which is not the same as being unused elsewhere. */
            btnId = rcFormat(rcAppArena(app), "toc_%s", doc_blocks[i].id).chars;
            if (rcButton(btnId, doc_blocks[i].text, RC_BTN_GHOST)) {
                /* Scroll the heading to the top of the page container. Both
                   boxes are in the same CONTENT space, so their difference is
                   exactly how far below the container's top edge the section
                   currently sits, and rcScrollBy is positive-down like the DOM's
                   element.scrollBy. rcScrollbar("page") is called after this in
                   the same frame, which is the order rayclay.h requires. */
                RC_Box sec = rcGetElementBox(doc_blocks[i].id);
                RC_Box page = rcGetElementBox("page");
                if (sec.found && page.found && sec.height > 0.0f)
                    rcScrollBy("page", 0.0f, sec.y - page.y);
                else
                    rcScrollToTop("page");   /* boxes are not ready on frame one */
            }
        }
    }
    rcScrollbar("toc");
}

static void reader_status(RC_App *app, AppState *st, float columnPx, float avgCharPx)
{
    int chars = reader_measure_chars(columnPx, avgCharPx);

    rcRow(.id = "status", .w = "grow", .h = "30", .pl = 16, .pr = 16, .gap = 16,
          .bg = rcRgb(16, 18, 22), .align = "c") {
        rcBox(.w = "fit", .h = "fit") {
            rcTextC(st->fonts.note, .font = st->fonts.small, .size = 12,
                    .color = st->fonts.custom ? rcRgb(150, 158, 172)
                                              : rcRgb(220, 176, 100));
        }
        rcBox(.w = "grow", .h = "1") {}
        rcBox(.w = "fit", .h = "fit") {
            /* rcFormat allocates in the frame arena, which is reset every
               frame - exactly the lifetime a status line wants. */
            rcText(rcFormat(rcAppArena(app), "measure ~%d chars", chars),
                   .font = st->fonts.small, .size = 12,
                   .color = reader_measure_color(chars));
        }
        rcBox(.w = "fit", .h = "fit") {
            rcTextL("45-75 is comfortable", .font = st->fonts.small, .size = 12,
                    .color = rcRgb(110, 118, 130));
        }
    }
}

static void layout(RC_App *app, void *userData)
{
    AppState       *st = (AppState *)userData;
    RC_Dimensions win;
    bool          showToc;
    float         columnPx, avgCharPx, idealPx;
    int           i;

    /* Fonts need the atlas, which exists once the app is running - so the
       ladder is registered on the first frame, not in main(). */
    if (!st->ready) {
        st->ready = true;
        reader_load_fonts(st);
    }

    win = rcGetWindowDimensions();

    /* Both the column and the breakpoint are derived from the type. The
       column is as wide as TARGET_CHARS characters of the body face actually
       measure, capped by the space available; the contents panel folds away
       when keeping it would take the column below that. Neither number is a
       device width, and both follow the font if the ladder changes. */
    avgCharPx = reader_avg_char_px(st->fonts.body);
    idealPx   = avgCharPx > 0.0f ? avgCharPx * TARGET_CHARS : 640.0f;

    showToc  = win.width >= TOC_MIN_PX;
    columnPx = win.width - (showToc ? TOC_W_PX : 0.0f) - 48.0f;
    if (columnPx > idealPx)
        columnPx = idealPx;
    if (columnPx < 220.0f)
        columnPx = 220.0f;

    rcColumn(.id = "root", .w = "grow", .h = "grow", .bg = rcRgb(13, 14, 18)) {
        rcRow(.id = "body", .w = "grow", .h = "grow") {
            if (showToc)
                reader_toc(app, st);

            /* Centred by two growing spacers rather than by a margin, so the
               column stays put as the window changes width. */
            rcRow(.id = "page", .w = "grow", .h = "grow", .scroll = "v") {
                rcBox(.w = "grow", .h = "fit") {}
                rcColumn(.id = "col", .h = "fit", .pt = 28, .pb = 64,
                         .wType = RC_PX(columnPx)) {
                    for (i = 0; i < DOC_BLOCK_COUNT; i++)
                        reader_block(&doc_blocks[i], &st->fonts);
                }
                rcBox(.w = "grow", .h = "fit") {}
            }
            rcScrollbar("page");
        }
        reader_status(app, st, columnPx, avgCharPx);
    }
}

int main(void)
{
    RC_AppOptions opts = {0};
    int rc;

    opts.width    = 1180;
    opts.height   = 820;
    opts.title    = "RayClay Reader";
    opts.iconPath = RC_EX22_ICON;
    opts.layoutCallback = layout;
    opts.userData = &state;
    opts.frameEndCallback = frame_end;

    /* Backs rcFormat, which the status bar uses for the measure readout.
       Warning: this is the one option where 0 means "off" rather than "a
       sensible default". Leaving it unset does not fail quietly: rcFormat
       returns the visible "<set scratchArenaBytes>" placeholder, which is how
       this very omission was caught here - it was legible in a screenshot
       while the exit code stayed 0. A misconfiguration that shows up on
       screen beats one that only reaches a log nobody reads on the web. */
    opts.scratchArenaBytes = 4096;

    /* Installed BEFORE the app starts: the font registrations happen on the
       first frame and their diagnostics are the only failure signal there is. */
    rcSetLogSink(reader_log, NULL);

    rc = rcRunApp(&opts);

    /* THE RENDER WITNESS, and it is an EXIT CODE because this example has no
       other honest channel - the same reason, and the same shape, as ex12.
       Every sibling is proved to have drawn by grepping stdout for the runner's
       "rendered N of N budgeted frames". This one installs rcSetLogSink, and the
       sink DISCARDS that message (it only latches the ERROR level). The example
       cannot print its way out either: it is pure RC_ with no libc include,
       which test/check-examples-pure-rc.sh enforces, and RayClay exposes a way
       to RECEIVE diagnostics, not to emit one. So the claim goes in the exit
       code.

       NOTE which route the budget arrives by decides who owns that line. Under
       RAYCLAY_MAX_FRAMES the library emits it PAST the sink, on the grounds that
       a sink the APP installed must not swallow the reply to a question the app
       did not ask. Under RC_AppOptions.maxFrames the sink owns it.
       => The exit code is what covers the OPTIONS route and the unbounded run,
       which is why it stays. Exit 0 means "presented at least one frame".

       Do not tighten this to `!= budget`. frameEndCallback counts PRESENTED
       frames while RC_AppOptions.maxFrames counts MAIN-LOOP frames, and the
       window system's first-map refresh is a presented frame the loop never
       counted - rayclay.h records 30 -> 31 and 90 -> 91. "> 0" is the only
       assertion that is portable here.

       This example was absent from test/CMakeLists.txt RC_EXAMPLE_TARGETS
       entirely, so nothing booted it; the anti-vacuity check there is a FLOOR,
       which by construction cannot notice an example that was never added. */
    if (rc == 0 && state.framesDrawn == 0)
        rc = 3;

    return rc;
}
