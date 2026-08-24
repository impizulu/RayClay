/*
================================================================================
    notes_app.h - the notes app CONTRACT (declarations only)
================================================================================

    The single header both drivers include:
      * the DEMO runner (main.c) - opens a real window, real clock, real input;
      * the BENCH harness (the test/ suite) - includes THIS header for the
        symbols, links notes_app.c compiled as a C object, and drives it with a
        fixed dt + seed + synthetic input under the capture seam.

    It declares ONLY types + prototypes (no RayClay DSL, no libc), so it is clean
    to include from C AND C++ - the app is consumed as a linkable object, keeping
    the DSL-heavy notes_app.c always compiled as C.

    ONE SOURCE, TWO MODES (the benchmark/showcase duality): the pure core
    (notes_seed/update/layout) runs identically in both modes; only the
    clock/seed/input SOURCE differs, and notes_demo_chrome adds a demo-only overlay.
    The app is a note-taking + blog editor: a sidebar of notes, a Write/Preview
    split, a publish + a settings dialog. Its dominant cost (B9) is ONE large
    word-wrapped article body re-measured every frame - the glyph/measure path,
    categorically distinct from the messenger's many-small-runs cost.

    Build target: rayclay_bench_notes
================================================================================
*/
#ifndef NOTES_APP_H
#define NOTES_APP_H

#include "bench_app.h"       /* the shared AppCtx / AppInputSink / AppMode contract */
#include "notes_backend.h"   /* NoteStore, embedded by value in AppState            */

/* The frozen bench scenario's version. Bump ONLY when the scripted path's rendered
   output changes (seed/fixtures/script/frozen layout) - never for demo-only chrome.
   the bench harness emits it in the trend marker "SCENE notes vN". */
#define NOTES_BENCH_VERSION 3

/* Font ladder - baked from the bundled face. A text app exercises a RICH atlas, so
   notes ladders FIVE sizes (vs the messenger's four); the index set is shared by the
   GUI (notes_app.c) and the demo runner (main.c), so they never desync. */
typedef enum { F_SMALL = 0, F_BODY, F_MD, F_HEAD, F_TITLE, F_COUNT } NoteFont;

/* App state - a FLAT, memset-able POD blob (no pointers into transient memory), so
   notes_seed can memset-then-set and the bench harness's run-twice determinism gate holds.
   If a field ever becomes a transient pointer, that invariant breaks. */
typedef struct {
    NoteStore store;         /* the backend, BY VALUE (the seed's memset zeroes it)   */
    int   openNote;          /* the open note index                                   */
    int   tab;               /* main pane: 0 = Write, 1 = Preview                      */
    int   folderFilter;      /* sidebar pills: 0 = All, 1 = Drafts, 2 = Published      */
    int   category;          /* publish/settings combo index into the category list    */
    char  search[64];        /* the sidebar search buffer (present; non-filtering)     */
    char  title[NOTE_TITLE_CAP]; /* the title input; sized to the store slot so the editor */
                             /* never holds more than note_set_title persists (no divergence) */
    char  body[4096];        /* the Write-tab multi-line body editor (seeded from the  */
                             /* open note's corpus; edit-scope, not persisted)         */
    char  tags[96];          /* the tags rcTextInput (display-scope, not persisted)   */
    char  slug[64];          /* the publish-modal slug input                           */
    char  summary[192];      /* the publish-modal summary input                        */
    bool  modalPublish;      /* the publish dialog (rcBeginModal open)                */
    bool  modalSettings;     /* the settings dialog (mutually exclusive with publish)  */
    bool  isPublic;          /* publish-modal visibility toggle (showcase)             */
    bool  spellcheck;        /* settings toggle (showcase)                             */
    bool  autoSave;          /* settings toggle (showcase)                             */
    bool  darkMode;          /* theme toggle (showcase chrome)                         */
    float editorFontPx;      /* settings slider; steps the Write-tab body font (12-24) */
    bool  seeded;            /* demo lazy-init guard (seed once the renderer is up)    */
} AppState;

/* The four-function app contract (+ the demo-only chrome). No RC_App / window handle;
   the input seam is the shared AppInputSink (bench_app.h). */
void notes_seed  (AppState *st, unsigned seed);              /* = bench_seed: memset then build */
void notes_update(AppState *st, const AppCtx *ctx);         /* advance backend by ctx->dt */
void notes_layout(AppState *st, const AppCtx *ctx);        /* the FROZEN core UI (both modes) */
void notes_demo_chrome(AppState *st, const AppCtx *ctx);   /* demo-only overlay (never in bench) */
void notes_bench_step(AppState *st, const AppInputSink *in, int frame); /* bench-only script */

#endif /* NOTES_APP_H */
