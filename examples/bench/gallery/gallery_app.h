/*
================================================================================
    gallery_app.h - the gallery app CONTRACT (declarations only)
================================================================================

    The single header both drivers include:
      * the DEMO runner (main.c) - a real window, real clock, real input;
      * the BENCH harness (the test/ suite) - includes THIS header for the
        symbols, links gallery_app.c as a C object, drives it with a fixed dt + seed
        + synthetic input under the capture seam.

    It declares ONLY types + prototypes (no RayClay DSL, no libc), so it is clean to
    include from C AND C++ - the app is consumed as a linkable object.

    ONE SOURCE, TWO MODES: the pure core (gallery_seed/update/layout) runs identically
    in both modes; only the clock/seed/input SOURCE differs, and gallery_demo_chrome
    adds a demo-only overlay. The app is a photo gallery + viewer: a searchable,
    clipped-scroll thumbnail GRID and a detail pane with the selected image
    (aspect-fit), a large title, metadata, an EDITABLE multiline caption
    (rcTextArea), and a details modal. Its dominant cost (B9) is IMAGE DECODE/UPLOAD
    (12 procedurally-generated BMPs decoded once through rcLoadImageFromMemory) +
    a CLIPPED-SCROLL grid of textured quads - the suite's only asset cost centre,
    distinct from messenger (text runs), notes (wrapped body), trader (dense grids),
    and platformer (floating scene).

    Build target: rayclay_bench_gallery
================================================================================
*/
#ifndef GALLERY_APP_H
#define GALLERY_APP_H

#include "bench_app.h"          /* the shared AppCtx / AppInputSink / AppMode contract */
#include "gallery_backend.h"    /* GalStore + the procedural-BMP asset generator       */

/* The frozen bench scenario's version. Bump ONLY when the scripted path's rendered
   output changes. the bench harness emits it in the trend marker "SCENE gallery vN". */
#define GALLERY_BENCH_VERSION 2

/* Font ladder - baked from the bundled face. F_HERO = 56 is the detail-pane title:
   the >=48px crisp-text-PERSISTS heading (must stay crisp at 2x+ HiDPI / zoom and
   never vanish - owner priority; the atlas overflow HOLDS the last scale). */
typedef enum { F_SMALL = 0, F_BODY, F_MD, F_TITLE, F_HERO, F_COUNT } GalleryFont;

/* App state - a FLAT, memset-able POD, so gallery_seed can memset-then-set and
   the bench harness's run-twice determinism gate holds. The decoded RC_Image handles are a
   RayClay type, so they live HERE (not in the RayClay-free backend); they are copied
   from a process-global one-time-load cache (see gallery_app.c) so a re-seed never
   re-decodes (the capture image-handle pool is a 16-slot one-way static). */
typedef struct {
    GalStore  store;                       /* the backend model, BY VALUE (seed zeroes it) */
    RC_Image  images[GAL_IMG_COUNT];       /* decoded handles (copied from the process cache) */
    int       selected;                    /* selected image index, 0..GAL_IMG_COUNT-1     */
    char      search[32];                  /* grid search box (rcTextInput); filters titles */
    char      caption[GAL_CAPTION_CAP];    /* the editable multiline caption (rcTextArea)   */
    int       captionOf;                   /* image the caption buffer holds (-1 = unsynced)  */
    bool      modalInfo;                   /* the photo-details modal (closed by default)     */
    bool      darkMode;                    /* theme toggle                                    */
    bool      seeded;                      /* demo lazy-init guard                            */
} AppState;

/* The four-function app contract (+ the demo-only chrome). No RC_App / window handle;
   the input seam is the shared AppInputSink (bench_app.h). */
void gallery_seed  (AppState *st, unsigned seed);              /* = bench_seed: memset then build + load images */
void gallery_update(AppState *st, const AppCtx *ctx);         /* no per-frame sim (static content) => trivially a no-op */
void gallery_layout(AppState *st, const AppCtx *ctx);        /* the FROZEN core UI (both modes) */
void gallery_demo_chrome(AppState *st, const AppCtx *ctx);   /* demo-only overlay (never in bench) */
void gallery_bench_step(AppState *st, const AppInputSink *in, int frame); /* bench-only script */

#endif /* GALLERY_APP_H */
