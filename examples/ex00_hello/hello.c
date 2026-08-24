/*
================================================================================
    hello.c - the smallest RayClay program (start here)
================================================================================

    Two lines open a cross-platform window showing the built-in welcome canvas.
    The SAME source builds and runs unchanged on every desktop OS and compiles
    to the web (cmake --preset web).

    rcRunApp IS THE ENTRY POINT EVERY APP USES, and this is it with nothing
    configured yet. Passing NULL asks for every default; you make it yours by
    passing options instead, never by calling something else:

        static void layout(RC_App *app, void *userData) { ... }

        RC_AppOptions opts = {
            .width = 900, .height = 600, .title = "My App",
            .layoutCallback    = layout,
            .scratchArenaBytes = 4096,   // needed by rcFormat
        };
        return rcRunApp(&opts);

    Because this program contains none of YOUR code, it is also the fastest way
    to tell a broken toolchain from a broken layout: if this window does not
    appear, the problem is the compiler, linker, GPU driver or emsdk.

    From here, ex01..ex05 walk one GUI per decade (1980s -> 2020s), each a little
    richer than the last; ex10 is the full widgets gallery.

    Build target: rayclay_ex00_hello
================================================================================
*/
#include "rayclay.h"

int main(void) { return rcRunApp(NULL); }
