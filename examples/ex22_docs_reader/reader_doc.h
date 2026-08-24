/* ============================================================================
 *  reader_doc.h - the document ex22 renders, as typed blocks.
 * ============================================================================
 *
 *  WHY A BLOCK ARRAY AND NOT MARKDOWN. An example may not include a system
 *  header (test/check-examples-pure-rc.sh), so it cannot open a file, and a
 *  Markdown parser would be several hundred lines of string handling that
 *  teaches nothing about RayClay. The interesting part of a reader is the
 *  TYPOGRAPHY - measure, hierarchy, rhythm - so the content arrives already
 *  parsed and the app spends its code on the part worth showing.
 *
 *  This is also how a real app should hold its content: a typed model, not a
 *  string it re-parses every frame. Layout runs per frame; parsing must not.
 * ========================================================================= */
#ifndef RC_EX22_READER_DOC_H
#define RC_EX22_READER_DOC_H

typedef enum {
    DOC_H1,        /* document title - one per document                      */
    DOC_H2,        /* section heading - these become the table of contents   */
    DOC_LEAD,      /* standfirst: body prose in the BOLD weight of the ladder */
    DOC_PARA,      /* body prose, wraps to the measure                       */
    DOC_BULLET,    /* list item, hanging indent                              */
    DOC_CODE,      /* monospace block - must not wrap, it scrolls instead    */
    DOC_NOTE       /* an aside, set apart by colour rather than by a box     */
} DocKind;

/* The id lives in the data, not in a string built at draw time. A section
   needs a stable element id for its contents button, and interpolating an
   index would serve: snprintf is out, since an example may not include a
   system header, but rcFormat returns a NUL-terminated result, so
   rcFormat(rcAppArena(app), "sec%d", i).chars is a valid `.id`.
   It is still the wrong tool here: it rebuilds the same string every frame
   for something that never changes. The document is a compile-time constant,
   so its ids can be too: no builder, no allocation, and no way for two
   sections to collide. Blocks that need no id leave it NULL - an element
   without an explicit id gets one from the library. */
typedef struct {
    DocKind     kind;
    const char *id;    /* stable element id; only H2 needs one (the TOC) */
    const char *text;
} DocBlock;

/* Long enough that the measure, the scroll and the heading rhythm are all real
   problems rather than demonstrations of themselves. */
static const DocBlock doc_blocks[] = {
{ DOC_H1,      NULL, "RayClay in ten minutes" },
{ DOC_LEAD,    NULL, "A standfirst, set in the BOLD weight of the same family - which is the whole "
              "point of registering a ladder rather than one face." },
{ DOC_PARA,    NULL, "RayClay is an immediate-mode GUI library for C. You describe the whole window every "
              "frame, and the library works out what changed. There is no widget tree to keep in "
              "sync, no callback spaghetti, and no separate layout language: the layout IS the C "
              "code you just wrote." },
{ DOC_NOTE,    NULL, "This document is rendered by the app you are reading it in. Every heading, every "
              "paragraph and the code blocks below are ordinary RayClay elements." },

{ DOC_H2,     "sec1", "The smallest app" },
{ DOC_PARA,    NULL, "One include, one layout function, one call. The layout function runs every frame "
              "and describes the window as it should look right now." },
{ DOC_CODE,    NULL, "#include \"rayclay.h\"\n"
              "\n"
              "static void layout(RC_App *app, void *user)\n"
              "{\n"
              "    rcColumn(.id = \"root\", .w = \"grow\", .h = \"grow\", .p = 24) {\n"
              "        rcTextL(\"Hello, world\", .size = 24);\n"
              "    }\n"
              "}\n"
              "\n"
              "int main(void)\n"
              "{\n"
              "    RC_AppOptions opts = {0};\n"
              "    opts.width = 800; opts.height = 600;\n"
              "    opts.title = \"Hello\"; opts.layoutCallback = layout;\n"
              "    return rcRunApp(&opts);\n"
              "}" },
{ DOC_PARA,    NULL, "That is a complete program. It opens a window, renders on demand, and exits cleanly "
              "when you close it." },

{ DOC_H2,     "sec2", "Sizing, and how it differs from CSS" },
{ DOC_PARA,    NULL, "Sizes are strings because that is how a designer says them: \"grow\" takes the space "
              "left over, \"fit\" shrinks to the content, \"320\" is pixels. The typed forms are "
              "there when you want arithmetic instead of a literal." },
{ DOC_BULLET,  NULL, "\"grow\" - take the remaining space along the parent's axis." },
{ DOC_BULLET,  NULL, "\"fit\" - shrink to wrap the children. This is the DEFAULT when you set nothing." },
{ DOC_BULLET,  NULL, "RC_PCT(50) - half the parent, but read the next paragraph before you use it." },
{ DOC_PARA,    NULL, "A percentage inside a \"fit\" parent resolves to zero, and nothing is logged. The "
              "parent is sizing itself from its children while a child is asking for a share of the "
              "parent, so the question is circular and the box simply is not drawn. If an element "
              "disappears, check whether its parent is \"fit\" - including by default." },

{ DOC_H2,     "sec3", "Text is measured, not guessed" },
{ DOC_PARA,    NULL, "Every wrap point in this paragraph came from rcMeasureText, the same public function "
              "the status bar below uses to report how many characters fit on a line. Typographers "
              "put the comfortable range at roughly 45 to 75 characters; wider than that and the eye "
              "loses its place returning to the left margin." },
{ DOC_PARA,    NULL, "Resize this window and watch the figure change. The reading column stops growing at "
              "its maximum measure rather than filling the screen, which is why the text stays "
              "readable on a wide monitor instead of stretching into a single long line." },

{ DOC_H2,     "sec4", "Fonts: one file per weight" },
{ DOC_PARA,    NULL, "This app ships three faces and registers them by family, weight and size. The single "
              "most common mistake is pointing two weights at one file: both calls succeed, both "
              "return ids, and your bold text renders as regular." },
{ DOC_CODE,    NULL, "/* right: a separate file per weight */\n"
              "rcRegisterFont(\"Lato\", RC_WEIGHT_REGULAR, \"Lato-Regular.ttf\", 16);\n"
              "rcRegisterFont(\"Lato\", RC_WEIGHT_BOLD,    \"Lato-Bold.ttf\",    16);\n"
              "\n"
              "/* wrong: one file under two weights - both bake the same outlines */\n"
              "rcRegisterFont(\"Lato\", RC_WEIGHT_REGULAR, \"Lato-Variable.ttf\", 16);\n"
              "rcRegisterFont(\"Lato\", RC_WEIGHT_BOLD,    \"Lato-Variable.ttf\", 16);" },
{ DOC_PARA,    NULL, "The second case logs a warning naming both weights. It is a warning and "
              "not a refusal: the face still loads, it is simply not the weight you asked for." },
{ DOC_NOTE,    NULL, "There are sixteen font slots in total, counting the bundled default, and a slot is "
              "one family-weight-size triple. This app registers six at startup and keeps them, "
              "which is the right shape for an app with a fixed ladder." },
{ DOC_PARA,    NULL, "The table is not one-way: rcUnloadFont gives a slot back, so an "
              "app that loads faces on demand - a browser, a theme picker - can recycle them instead "
              "of exhausting the table. It returns true only when a live font was released, and "
              "refuses id 0 on purpose, because zero is both the default face and the value a failed "
              "load returns." },
{ DOC_NOTE,    NULL, "Two things about unloading are worth knowing before you build on it. It buys back a "
              "SLOT, not memory: the glyphs stay in the atlas until something re-bakes it, so a load "
              "can still be refused for sheet room with a slot standing free - a different refusal "
              "with a different log line. And the freed slot is handed to the next load unchanged, "
              "id and all, so an id you released and reloaded is a DIFFERENT face wearing the same "
              "number. Drop the old handle." },

{ DOC_H2,     "sec5", "Detecting a font that did not load" },
{ DOC_PARA,    NULL, "rcLoadFont returns zero both for the default slot and for a failed load, so the "
              "return value cannot tell you what happened. Install a log sink and read the LEVEL: "
              "an error means the load was refused and you hold the default face, a warning means "
              "the face loaded and something about it is worth knowing." },
{ DOC_PARA,    NULL, "Match on the level rather than on the wording. The set of messages is open-ended and "
              "spans three prefixes, so a sink that pattern-matches sentences will one day score a "
              "real failure as a success." },

{ DOC_H2,     "sec6", "Rendering on demand" },
{ DOC_PARA,    NULL, "A RayClay window parks when nothing changes, which is why an idle app costs almost "
              "nothing. If your app animates on its own - a clock, a progress bar, a simulation - "
              "you must ask for the next frame, or it will stop the moment the window goes quiet." },
{ DOC_CODE,    NULL, "rcAppRequestFrame(app);            /* draw one more, now      */\n"
              "rcAppRequestFrameAfter(app, 1.0f); /* draw again in a second  */" },

{ DOC_H2,     "sec7", "One source, three platforms" },
{ DOC_PARA,    NULL, "The same file builds for Linux, macOS, Windows and the web with no conditional "
              "compilation. The layout inputs and the layout shape are identical across them: the "
              "text measurements and element counts this app produces are bit-identical on x86_64 "
              "and arm64, under gcc, Apple clang and MSVC." },
{ DOC_PARA,    NULL, "What that does not promise is identical pixels. Anti-aliasing and rasterisation "
              "belong to the platform's GPU driver, so run your own interface on each target and "
              "look at it before you believe it." },

{ DOC_H2,     "sec8", "Where to go next" },
{ DOC_BULLET,  NULL, "docs/cheatsheet.md - every public function, type and constant on one page." },
{ DOC_BULLET,  NULL, "docs/getting-started.md - building, linking and shipping." },
{ DOC_BULLET,  NULL, "docs/for-web-developers.md - the CSS mental model, and where it stops applying." },
{ DOC_BULLET,  NULL, "examples/ - the other programs in this tree. The ones that need a file at run "
              "time, this reader among them, read it from examples/assets/." },
};

#define DOC_BLOCK_COUNT ((int)(sizeof(doc_blocks) / sizeof(doc_blocks[0])))

#endif /* RC_EX22_READER_DOC_H */
