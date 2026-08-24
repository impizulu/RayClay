/*
================================================================================
    main.c - RayClay 2000s example (Aqua / Winamp-style media player)
================================================================================

    A glossy turn-of-the-millennium media player: vertical gloss gradients, soft
    drop shadows, saturated aqua sky/blue chrome, and pill-rounded transport
    buttons - the XP Luna / Mac OS X Aqua / Web 2.0 look. Same source ->
    native desktop window AND web (cmake --preset web -> gui2000s.html).
    Zero-asset: bundled Latin-1 font baked at runtime; procedural icons only.

    Live: the seek bar advances by itself while "playing", proving the loop runs.

    Build target: rayclay_ex03_2000s_gui
================================================================================
*/

#include "rayclay.h"

#include "icons/rc_icons_rayclay_logo.h"

/* Font ladder baked from the bundled face at these sizes - zero-asset. */
typedef enum { F_SMALL = 0, F_BODY, F_TITLE, F_COUNT } AppFont;

#define TRACK_COUNT 8

/* Frames the seek scrub suppresses auto-advance for - long enough to outlast a
   drag's inter-frame gaps, short enough that playback resumes right on release. */
#define SEEK_SCRUB_FRAMES 10

typedef struct {
    int   track;      /* index of the current track                 */
    bool  playing;    /* transport state (Play toggles it)          */
    float pos;        /* seek position 0..100 (advances while live) */
    float vol;        /* volume 0..100                              */
    int   scrub;      /* seek-scrub guard: pauses auto-advance while >0 */
} AppState;

/* One playlist entry - static table, so no per-frame allocation. secs mirrors
   the printed duration so the seek readout tracks the selected track. */
typedef struct {
    const char *title;
    const char *dur;
    int         secs;
} Track;

static const Track g_tracks[TRACK_COUNT] = {
    { "Silica Dreams",        "3:45", 225 },
    { "Aqua Interlude",       "2:58", 178 },
    { "Bondi Blue",           "4:12", 252 },
    { "Brushed Metal",        "3:20", 200 },
    { "Glass Reflections",    "5:04", 304 },
    { "Pinstripe Sunrise",    "3:37", 217 },
    { "Candybar Skyline",     "2:44", 164 },
    { "Frutiger Aero",        "4:29", 269 },
};

/* Stable per-row ids for the playlist (rcClicked needs a unique id per row). */
static const char *const g_row_ids[TRACK_COUNT] = {
    "tk0", "tk1", "tk2", "tk3", "tk4", "tk5", "tk6", "tk7",
};

/* ---------------------------------------------------------------------------
   Aqua palette helpers - kept local so the gloss look reads in one place.
   --------------------------------------------------------------------------- */

/* A glossy caption/button gloss: light sky at the top, deep blue at the base. */
static RC_Gradient aqua_gloss(void) {
    return (RC_Gradient){ .from = RC_SKY_400, .to = RC_BLUE_700, .dir = "v" };
}

/* Soft drop shadow used under every raised aqua surface. */
static RC_Shadow soft_shadow(void) {
    return (RC_Shadow){ .color = rcAlpha(RC_BLACK, 110), .y = 3.0f, .blur = 8.0f };
}

/* ---------------------------------------------------------------------------
   Transport button: a glossy, shadowed, pill-rounded box with an ASCII glyph.
   Icons for play/pause/etc. don't exist, so we set them in text ("|<", ">", ...)
   rcClicked turns the styled box into a button; rcIsHovered brightens it.
   --------------------------------------------------------------------------- */
static bool transport_btn(const char *id, const char *glyph, bool primary) {
    RC_Gradient g = aqua_gloss();
    if (rcIsHovered(id)) {
        g.from = RC_SKY_300;   /* lift the gloss on hover */
        g.to   = RC_BLUE_600;
    }
    /* The primary (play) button reads a touch larger + brighter. RC_PX(expr) is
       the typed fast path and takes a value computed this frame; the string form
       (.h = "40px") is parsed, so it suits sizes you type by hand. */
    rcBox(.id = id, .wType = RC_PX(primary ? 58 : 46), .h = "40px",
           .align = "cc", .gradient = g, .shadow = soft_shadow(),
           .borderRadius = "all-full",
           .border = { .color = rcAlpha(RC_WHITE, 90), .width = "1px" }) {
        rcTextC(glyph, .font = primary ? F_TITLE : F_BODY, .color = RC_WHITE);
    }
    return rcClicked(id);
}

/* ---------------------------------------------------------------------------
   Caption band - the draggable glossy aqua title bar (desktop; web ignores it).
   --------------------------------------------------------------------------- */
/* One 12px Aqua "gel" traffic light, tagged with a window-control id so the
   runner performs the OS action. Hand-rolled: the bundled 38px cluster would
   overflow this 26px band - and left-side coloured circles ARE the era. */
static void aqua_light(const char *winId, unsigned rgb) {
    rcBox(.id = winId, .w = "12px", .h = "12px", .borderRadius = "all-full",
           .bg = rcHex(rgb),
           .border = { .color = rcAlpha(RC_BLACK, 70), .width = "1px" }) {}
}

static void titlebar(void) {
    /* Chrome, not content: RC_AppOptions.titlebarHeight freezes the OS drag
       strip in physical px, so a band that grew with the content zoom would
       stop matching the strip the OS lets you drag. Measured before this
       existed: at 2x zoom the drawn band was exactly twice the draggable one. */
    rcUnzoomed() {
        rcRow(.id = RC_ID_WINDOW_DRAG, .w = "grow", .h = "26px", .align = "cl",
               .px = 10, .gap = 8, .gradient = aqua_gloss()) {
            aqua_light(RC_ID_WINDOW_CLOSE,    0xff5f57);
            aqua_light(RC_ID_WINDOW_MINIMIZE, 0xfebc2e);
            aqua_light(RC_ID_WINDOW_MAXIMIZE, 0x28c840);
            rcIconRayClayLogo(16.0f);
            rcTextL("RayClay Player", .font = F_BODY, .color = RC_WHITE);
            rcBox(.w = "grow") {}
        }
    }
}

/* ---------------------------------------------------------------------------
   Now-playing panel - album placeholder + track title/artist.
   --------------------------------------------------------------------------- */
static void now_playing(const AppState *st) {
    const Track *t = &g_tracks[st->track];
    rcRow(.w = "grow", .gap = 14, .align = "cl") {
        /* Album art placeholder: a rounded gradient square with a gloss. */
        rcBox(.id = "album", .w = "72px", .h = "72px",
               .align = "cc", .borderRadius = "all-lg", .shadow = soft_shadow(),
               .gradient = { .from = RC_CYAN_400, .to = RC_INDIGO_700, .dir = "d" },
               .border = { .color = rcAlpha(RC_WHITE, 70), .width = "1px" }) {
            rcTextL("CD", .font = F_TITLE, .color = rcAlpha(RC_WHITE, 210));
        }
        rcColumn(.gap = 4) {
            rcTextC(t->title, .font = F_TITLE, .color = RC_WHITE);
            rcTextL("RayClay Sound System", .font = F_SMALL, .color = RC_SKY_200);
            rcRow(.gap = 6, .align = "cl") {
                rcBox(.bg = rcAlpha(RC_SKY_500, 90), .px = 8, .py = 2,
                       .borderRadius = "all-full") {
                    rcTextL("Now Playing", .font = F_SMALL, .color = RC_WHITE);
                }
            }
        }
    }
}

/* ---------------------------------------------------------------------------
   Playlist row - title + duration; the current track is highlighted.
   --------------------------------------------------------------------------- */
static void playlist_row(int i, bool current) {
    const char *id = g_row_ids[i];
    RC_Color bg = current ? rcAlpha(RC_SKY_500, 150)
                            : (rcIsHovered(id) ? rcAlpha(RC_SKY_400, 70)
                                                : RC_TRANSPARENT);
    rcRow(.id = id, .w = "grow", .h = "30px", .align = "cl", .px = 10,
           .gap = 8, .bg = bg, .borderRadius = "all-md") {
        rcTextC(current ? ">" : " ", .font = F_SMALL, .color = RC_WHITE);
        rcBox(.w = "grow", .overflow = "hidden") {
            rcTextC(g_tracks[i].title, .font = F_BODY,
                     .color = current ? RC_WHITE : RC_SKY_100, .wrap = "n");
        }
        rcTextC(g_tracks[i].dur, .font = F_SMALL, .color = RC_SKY_200);
    }
}

/* ---------------------------------------------------------------------------
   Callbacks.
   --------------------------------------------------------------------------- */
static void update(RC_App *app, void *userData) {
    AppState *st = (AppState *)userData;
    if (st->scrub > 0)
        st->scrub--;
    /* Advance the seek bar slowly while playing; wrap at the end of the track.
       Hold off while the user is scrubbing so the drag isn't overwritten. */
    if (st->playing && st->scrub == 0) {
        st->pos += 0.25f;
        if (st->pos >= 100.0f) {
            st->pos = 0.0f;
            st->track = (st->track + 1) % TRACK_COUNT;   /* auto-advance */
        }
    }
    /* The transport moves with no input at all, and RayClay only
       draws when something asks it to. So while the track is playing (or the
       post-drag scrub countdown is still running) we ask for one more frame -
       the same contract as the browser's requestAnimationFrame. Paused, we ask
       for nothing and the window parks at ~0 CPU until the user clicks. */
    if (st->playing || st->scrub > 0)
        rcAppRequestFrame(app);
}

static void layout(RC_App *app, void *userData) {
    AppState *st = (AppState *)userData;

    /* Dark player body so the aqua gloss and white highlights pop. */
    RC_Color body   = rcColor("#0b1a2e");
    RC_Color panel  = rcAlpha(RC_SLATE_900, 235);
    RC_Color glass  = rcAlpha(RC_WHITE, 22);

    rcColumn(.id = "Root", .w = "grow", .h = "grow", .bg = body) {
        titlebar();

        rcColumn(.w = "grow", .h = "grow", .p = 16, .gap = 14) {
            now_playing(st);

            /* Seek: slider + a "1:23 / 3:45"-style elapsed/total readout. */
            rcColumn(.id = "seekpanel", .w = "grow", .gap = 6, .p = 10,
                      .bg = glass, .borderRadius = "all-lg",
                      .shadow = soft_shadow(),
                      .border = { .color = rcAlpha(RC_WHITE, 40), .width = "1px" }) {
                if (rcSlider("seek", &st->pos, 0.0f, 100.0f))
                    st->scrub = SEEK_SCRUB_FRAMES;   /* user is dragging - back off */
                rcRow(.w = "grow", .align = "cl") {
                    int total = g_tracks[st->track].secs;
                    int cur   = (int)(st->pos * (float)total / 100.0f);
                    RC_String time = rcFormat(rcAppArena(app),
                                                 "%d:%02d / %d:%02d",
                                                 cur / 60, cur % 60,
                                                 total / 60, total % 60);
                    rcText(time, .font = F_SMALL, .color = RC_SKY_100);
                    rcBox(.w = "grow") {}
                    rcTextC(st->playing ? "Playing" : "Paused",
                             .font = F_SMALL, .color = RC_SKY_200);
                }
            }

            /* Transport cluster of glossy round-ish buttons. */
            rcRow(.w = "grow", .gap = 10, .align = "cc") {
                /* Load-bearing: a track change resets the seek position. st->pos is
                   a percentage through the current track, so carrying it across a
                   skip lands you 40% into a song you just started - and the elapsed
                   readout computes against the new track's duration, so it shows a
                   time that never elapsed. The other three track-change paths (stop,
                   auto-advance, playlist click) all zero it; these two did not. */
                if (transport_btn("t_prev", "|<", false)) {
                    st->track = (st->track + TRACK_COUNT - 1) % TRACK_COUNT;
                    st->pos   = 0.0f;
                }
                if (transport_btn("t_play", st->playing ? "||" : ">", true))
                    st->playing = !st->playing;
                if (transport_btn("t_stop", "[]", false)) {
                    st->playing = false;
                    st->pos = 0.0f;
                }
                if (transport_btn("t_next", ">|", false)) {
                    st->track = (st->track + 1) % TRACK_COUNT;
                    st->pos   = 0.0f;
                }
            }

            /* Volume slider + buffering progress. */
            rcRow(.w = "grow", .gap = 10, .align = "cl") {
                rcTextL("Vol", .font = F_SMALL, .color = RC_SKY_100);
                rcBox(.w = "grow") { rcSlider("vol", &st->vol, 0.0f, 100.0f); }
                RC_String vpct = rcFormat(rcAppArena(app), "%.0f%%", st->vol);
                rcText(vpct, .font = F_SMALL, .color = RC_SKY_200);
            }
            rcRow(.w = "grow", .gap = 10, .align = "cl") {
                rcTextL("Buf", .font = F_SMALL, .color = RC_SKY_100);
                rcBox(.w = "grow") {
                    /* Buffer sits a little ahead of the play head. */
                    float buf = (st->pos + 18.0f) / 100.0f;
                    rcProgress("buf", buf > 1.0f ? 1.0f : buf);
                }
            }

            /* Scrollable playlist. */
            rcColumn(.id = "tracks", .w = "grow", .h = "grow", .scroll = "v",
                      .p = 8, .gap = 2, .bg = panel, .borderRadius = "all-lg",
                      .shadow = soft_shadow(),
                      .border = { .color = rcAlpha(RC_WHITE, 30), .width = "1px" }) {
                rcRow(.w = "grow", .px = 10, .pb = 4, .align = "cl") {
                    rcBox(.w = "grow") {
                        rcTextL("Playlist", .font = F_BODY, .color = RC_WHITE);
                    }
                    RC_String n = rcFormat(rcAppArena(app), "%d tracks",
                                              TRACK_COUNT);
                    rcText(n, .font = F_SMALL, .color = RC_SKY_200);
                }
                for (int i = 0; i < TRACK_COUNT; i++)
                    playlist_row(i, i == st->track);
            }
        }
    }

    /* Playlist row clicks: applied AFTER the column closes (no in-flight edits). */
    for (int i = 0; i < TRACK_COUNT; i++) {
        if (rcClicked(g_row_ids[i])) {
            st->track = i;
            st->pos = 0.0f;
        }
    }

    rcScrollbar("tracks");   /* floating, declared in-layout; layers itself above "tracks" */
}

/* ---------------------------------------------------------------------------
   Entry point.
   --------------------------------------------------------------------------- */
int main(void) {
    AppState state = {
        .track   = 0,
        .playing = true,
        .pos     = 24.0f,
        .vol     = 70.0f,
        .scrub   = 0,
    };

    static const float fontSizes[F_COUNT] = {
        [F_SMALL] = 12.0f,
        [F_BODY]  = 15.0f,
        [F_TITLE] = 18.0f,
    };

    RC_AppOptions opts = {
        .width          = 420,
        .height         = 620,
        .title          = "RayClay Player",
        .clearColor     = rcColor("#0b1a2e"),
        .fontSizes      = fontSizes,
        .fontCount      = F_COUNT,
        .scratchArenaBytes = 4096,   /* backs rcFormat (time / volume / count) */
        .nativeFrame    = true,
        .titlebarHeight = 26,
        .titlebar       = { .custom = true },   /* we draw the Aqua bar ourselves */
        .updateCallback       = update,
        .layoutCallback       = layout,
        .userData       = &state,
    };

    return rcRunApp(&opts);
}
