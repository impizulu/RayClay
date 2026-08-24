/*
================================================================================
    main.c - RayClay 2020s GUI: a modern SaaS dashboard
================================================================================

    A realistic single-screen app: custom title bar, sidebar nav (4 live tabs),
    and a content area that shows a different page per tab. Same source ->
    native desktop window AND web (cmake --preset web -> index.html).
    Zero-asset: bundled font baked at runtime; procedural icons. The last stop in
    the decade walk (ex01..ex05): dark theme, soft shadows, rounded cards, violet accent.

    Tabs: Dashboard (metrics + feed) - Analytics (weekly chart) -
          Files (file browser) - Settings (project config form).

    Build target: rayclay_ex05_2020s_gui
================================================================================
*/

#include "rayclay.h"

#include "icons/rc_icons_rayclay_logo.h"
#include "icons/rc_icons_settings.h"
#include "icons/rc_icons_panel_left.h"
#include "icons/rc_icons_chart_column.h"
#include "icons/rc_icons_folder.h"

/* Font ladder baked from the bundled face at these sizes - zero-asset. */
typedef enum { F_SMALL = 0, F_BODY, F_HEAD, F_STAT, F_COUNT } AppFont;

typedef struct {
    long  frame;          /* proves the loop is live                    */
    bool  darkMode;       /* global theme toggle                        */
    int   page;           /* 0=Dashboard 1=Analytics 2=Files 3=Settings */
    bool  notifications;  /* Settings: toggle                           */
    char  project[48];    /* Settings: text field                       */
    float budget;         /* Settings: slider (0..1 -> $0..$5 000)      */
    int   plan;           /* Settings: dropdown                         */
} AppState;

/* ── helpers ─────────────────────────────────────────────────────────────── */

/* Sidebar nav row: a whole-row click target (icon + label). rcClicked turns
   any styled rcRow into a button, so the row itself is the control - no nested
   button - and rcIsHovered gives it hover feedback. */
static bool nav_btn(const char *id, const char *label, RC_IconCallback icon, bool active) {
    RC_Style s = rcGetStyle();
    rcRow(.id = id, .w = "grow", .h = "40px", .align = "cl", .px = 10, .gap = 8,
           .bg = active ? s.surfaceAlt : (rcIsHovered(id) ? s.surface : RC_TRANSPARENT),
           .borderRadius = "all-md") {
        icon(16.0f, active ? s.text : s.textMuted);
        rcTextC(label, .font = F_BODY, .color = active ? s.text : s.textMuted);
    }
    return rcClicked(id);
}

/* ── top bar ─────────────────────────────────────────────────────────────── */

static void topbar(RC_App *app, AppState *st) {
    RC_Style s = rcGetStyle();
    /* The whole band is the OS drag region; the theme-toggle cluster keeps its click
       by opting OUT with RC_ID_WINDOW_NODRAG - the desktop twin of CSS
       `-webkit-app-region: no-drag` (the window controls opt out the same way via
       their own ids). Inert on web, where the window has no drag. */
    /* Chrome, not content. RC_AppOptions.titlebarHeight freezes the OS drag
       strip in physical px, so a band that grew with the content zoom would
       stop matching the strip the OS lets you drag. Measured before this
       existed: at 2x zoom the drawn band was exactly twice the draggable one. */
    rcUnzoomed() {
        rcRow(.id = RC_ID_WINDOW_DRAG, .w = "grow", .h = "56px", .bg = s.chrome,
               .px = 14, .gap = 12, .align = "cl") {
            rcIconRayClayLogo(28.0f);
            rcTextL("RayClay", .font = F_HEAD, .color = s.text);
            rcBox(.bg = s.surfaceAlt, .px = 8, .py = 3, .borderRadius = "all-full") {
                rcTextL("Console", .font = F_SMALL, .color = s.textMuted);
            }
            rcBox(.w = "grow") {}
            RC_String fps = rcFormat(rcAppArena(app), "%.0f fps \xc2\xb7 frame %ld",
                                        rcAppFPS(app), st->frame);
            rcText(fps, .font = F_SMALL, .color = s.textMuted);
            rcRow(.id = RC_ID_WINDOW_NODRAG, .gap = 8, .align = "cl") {
                rcTextC(st->darkMode ? "Dark" : "Light", .font = F_SMALL, .color = s.textMuted);
                rcToggle("tg_theme", &st->darkMode);
            }
            rcWindowControls();
        }
    }
}

/* ── sidebar ─────────────────────────────────────────────────────────────── */

static void sidebar(AppState *st) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "220px", .h = "grow", .bg = s.surface, .p = 12, .gap = 4) {
        if (nav_btn("nav_dash", "Dashboard", rcIconPanelLeft,   st->page == 0)) st->page = 0;
        if (nav_btn("nav_ana",  "Analytics", rcIconChartColumn, st->page == 1)) st->page = 1;
        if (nav_btn("nav_fil",  "Files",     rcIconFolder,      st->page == 2)) st->page = 2;
        if (nav_btn("nav_set",  "Settings",  rcIconSettings,    st->page == 3)) st->page = 3;
        rcBox(.w = "grow", .h = "grow") {}
        rcRow(.id = "sb_badge", .w = "grow", .align = "cl", .gap = 10, .p = 10,
               .gradient = { .from = RC_VIOLET_600, .to = RC_INDIGO_600, .dir = "d" },
               .borderRadius = "all-lg",
               .shadow = { .color = rcAlpha(RC_VIOLET_900, 140), .y = 4, .blur = 14, .spread = -2 }) {
            rcIconRayClayLogo(24.0f);
            rcColumn(.gap = 2) {
                rcTextL("RayClay",           .font = F_SMALL, .color = RC_WHITE);
                /* RC_VERSION is a string literal, so it concatenates at compile time and the badge
                   can never drift from the library the way a hand-typed "v0.5" did. */
                rcTextL("v" RC_VERSION " \xc2\xb7 one source",
                         .font = F_SMALL, .color = rcAlpha(RC_WHITE, 200));
            }
        }
    }
}

/* ── shared widget ───────────────────────────────────────────────────────── */

/* Metric card: muted label, big number, coloured trend dot + text. */
static void stat_card(const char *id, const char *label, const char *value,
                      const char *trend, RC_Color accent) {
    RC_Style s = rcGetStyle();
    rcColumn(.id = id, .w = "grow", .bg = s.surface, .p = 16, .gap = 6,
              .borderRadius = "all-xl", .border = { .color = s.border, .width = "1px" },
              .shadow = { .color = rcAlpha(RC_BLACK, 90), .y = 6, .blur = 20, .spread = -2 }) {
        rcTextC(label, .font = F_SMALL, .color = s.textMuted);
        rcTextC(value, .font = F_STAT,  .color = s.text);
        rcRow(.gap = 6, .align = "cl") {
            rcBox(.w = "8px", .h = "8px", .bg = accent, .borderRadius = "all-full") {}
            rcTextC(trend, .font = F_SMALL, .color = accent);
        }
    }
}

/* ── page 0: Dashboard ───────────────────────────────────────────────────── */

static void feed_row(const char *id, const char *text,
                     const char *badge, RC_Color dot) {
    RC_Style s = rcGetStyle();
    rcRow(.id = id, .w = "grow", .h = "44px", .align = "cl", .px = 12, .gap = 10,
           .bg = rcIsHovered(id) ? s.surface : s.surfaceAlt, .borderRadius = "all-md") {
        rcBox(.w = "8px", .h = "8px", .bg = dot, .borderRadius = "all-full") {}
        rcBox(.w = "grow", .overflow = "hidden", .align = "cl") {
            rcTextC(text, .font = F_BODY, .color = s.text, .wrap = "n");
        }
        rcBox(.bg = s.surface, .px = 10, .py = 4, .borderRadius = "all-full",
               .border = { .color = s.border, .width = "1px" }) {
            rcTextC(badge, .font = F_SMALL, .color = s.textMuted);
        }
    }
}

static void page_dashboard(void) {
    RC_Style s = rcGetStyle();
    rcColumn(.gap = 2) {
        rcTextL("Dashboard",                         .font = F_HEAD,  .color = s.text);
        rcTextL("Live metrics - one C source.", .font = F_SMALL, .color = s.textMuted);
    }
    rcRow(.w = "grow", .gap = 14) {
        stat_card("sc_users",  "Active users", "1,284", "+12.4% this week", s.successHover);
        stat_card("sc_uptime", "Uptime",       "99.9%", "30-day average",   s.primaryHover);
        stat_card("sc_rev",    "Revenue",      "$12.4k","+3.1% vs last mo", s.warningHover);
    }
    rcColumn(.id = "panel_activity", .w = "grow", .bg = s.surface, .p = 16, .gap = 10,
              .borderRadius = "all-xl", .border = { .color = s.border, .width = "1px" },
              .shadow = { .color = rcAlpha(RC_BLACK, 70), .y = 8, .blur = 24, .spread = -4 }) {
        rcRow(.w = "grow", .align = "cl") {
            rcBox(.w = "grow") {
                rcTextL("Recent activity", .font = F_HEAD, .color = s.text);
            }
            rcTextL("Today", .font = F_SMALL, .color = s.textMuted);
        }
        feed_row("act1", "Deploy succeeded - web bundle 243 KB", "ci",      s.successHover);
        feed_row("act2", "New sign-up from the landing page",               "user",    s.primaryHover);
        feed_row("act3", "Invoice #1042 paid",                              "billing", s.warningHover);
        feed_row("act4", "Renderer benchmark within budget",                "perf",    s.successHover);
        feed_row("act5", "Login from a new device",                         "alert",   s.danger);
    }
}

/* ── page 1: Analytics ───────────────────────────────────────────────────── */

/* Requests per hour across a day: a native rcChart trace (file-scope + const,
   so the borrowed-until-render array can never dangle). The x axis is the hour
   0..23 - meaningful as a number, so no categorical labels are needed. */
static const float req_24h[] = {
    12, 8, 6, 5, 4, 6, 14, 32, 58, 71, 78, 82,
    88, 79, 74, 80, 91, 97, 85, 66, 48, 34, 22, 15,
};

static void page_analytics(void) {
    RC_Style s = rcGetStyle();
    static const struct {
        const char *day; float val; const char *pct;
    } bars[] = {
        {"Mon", 0.62f, "62%"}, {"Tue", 0.84f, "84%"}, {"Wed", 0.71f, "71%"},
        {"Thu", 0.93f, "93%"}, {"Fri", 0.88f, "88%"}, {"Sat", 0.45f, "45%"},
        {"Sun", 0.38f, "38%"},
    };
    static const char *const pb_ids[] = {
        "pb_mon", "pb_tue", "pb_wed", "pb_thu", "pb_fri", "pb_sat", "pb_sun",
    };

    rcColumn(.gap = 2) {
        rcTextL("Analytics",                          .font = F_HEAD,  .color = s.text);
        rcTextL("Request volume - today and this week.", .font = F_SMALL, .color = s.textMuted);
    }
    /* A native rcChart hero: today's hourly request volume as a filled area with
       a y grid. rcChart GROWs, so it is wrapped in a sized box. */
    rcColumn(.id = "panel_hourly", .w = "grow", .bg = s.surface, .p = 16, .gap = 10,
              .borderRadius = "all-xl", .border = { .color = s.border, .width = "1px" },
              .shadow = { .color = rcAlpha(RC_BLACK, 70), .y = 8, .blur = 24, .spread = -4 }) {
        rcTextL("Hourly requests (today)", .font = F_HEAD, .color = s.text);
        rcBox(.w = "grow", .h = "150px") {
            RC_Series ser = { .y = req_24h, .count = 24, .kind = RC_SERIES_AREA,
                              .color = s.primary, .label = "req/hour" };
            rcChart("an_hourly", &ser, 1,
                     (RC_ChartOptions){ .y = { .grid = true }, .x = { .ticks = 6 },
                                        .tooltip = RC_CHART_TOOLTIP_NEAREST });
        }
    }
    rcColumn(.id = "panel_weekly", .w = "grow", .bg = s.surface, .p = 16, .gap = 10,
              .borderRadius = "all-xl", .border = { .color = s.border, .width = "1px" },
              .shadow = { .color = rcAlpha(RC_BLACK, 70), .y = 8, .blur = 24, .spread = -4 }) {
        rcTextL("Weekly traffic", .font = F_HEAD, .color = s.text);
        for (int i = 0; i < 7; i++) {
            rcRow(.w = "grow", .gap = 10, .align = "cl") {
                rcBox(.w = "36px") {
                    rcTextC(bars[i].day, .font = F_SMALL, .color = s.textMuted);
                }
                rcBox(.w = "grow") { rcProgress(pb_ids[i], bars[i].val); }
                rcBox(.w = "36px") {
                    rcTextC(bars[i].pct, .font = F_SMALL, .color = s.textMuted);
                }
            }
        }
    }
    rcRow(.w = "grow", .gap = 14) {
        stat_card("sc_peak", "Peak day",    "Thu 93%", "highest this week", s.primaryHover);
        stat_card("sc_avg",  "Avg daily",   "74%",     "vs 68% last week",  s.successHover);
        stat_card("sc_dip",  "Weekend dip", "-46%",    "vs weekday avg",    s.warningHover);
    }
}

/* ── page 2: Files ───────────────────────────────────────────────────────── */

static void page_files(void) {
    RC_Style s = rcGetStyle();
    static const struct {
        const char *name; const char *type; const char *size; const char *date;
    } files[] = {
        {"rayclay.h",         "header", "142 KB", "Jun 26"},
        {"rc_widgets.c",      "source", "38 KB",  "Jun 25"},
        {"rc_app.c",          "source", "29 KB",  "Jun 28"},
        {"roboto-latin1.ttf", "font",   "14 KB",  "Jun 19"},
        {"CMakeLists.txt",    "config", "8 KB",   "Jun 22"},
        {"gallery.html",      "web",    "623 B",  "Jun 26"},
    };
    static const char *const row_ids[] = {
        "file0", "file1", "file2", "file3", "file4", "file5",
    };

    rcColumn(.gap = 2) {
        rcTextL("Files",                                .font = F_HEAD,  .color = s.text);
        rcTextL("RayClay source tree - key artifacts.", .font = F_SMALL, .color = s.textMuted);
    }
    rcColumn(.id = "panel_files", .w = "grow", .bg = s.surface, .p = 16, .gap = 2,
              .borderRadius = "all-xl", .border = { .color = s.border, .width = "1px" },
              .shadow = { .color = rcAlpha(RC_BLACK, 70), .y = 8, .blur = 24, .spread = -4 }) {
        /* Header row */
        rcRow(.w = "grow", .h = "36px", .px = 12, .align = "cl") {
            rcBox(.w = "grow") { rcTextL("Name",  .font = F_SMALL, .color = s.textMuted); }
            rcBox(.w = "72px") { rcTextL("Type", .font = F_SMALL, .color = s.textMuted); }
            rcBox(.w = "72px") { rcTextL("Size", .font = F_SMALL, .color = s.textMuted); }
            rcBox(.w = "60px") { rcTextL("Date", .font = F_SMALL, .color = s.textMuted); }
        }
        for (int i = 0; i < 6; i++) {
            rcRow(.id = row_ids[i], .w = "grow", .h = "44px", .px = 12, .align = "cl",
                   .bg = rcIsHovered(row_ids[i]) ? s.surfaceAlt : RC_TRANSPARENT,
                   .borderRadius = "all-md") {
                rcBox(.w = "grow") {
                    rcTextC(files[i].name, .font = F_BODY, .color = s.text);
                }
                rcBox(.w = "72px") {
                    rcBox(.bg = s.surfaceAlt, .px = 8, .py = 3, .borderRadius = "all-full") {
                        rcTextC(files[i].type, .font = F_SMALL, .color = s.textMuted);
                    }
                }
                rcBox(.w = "72px") {
                    rcTextC(files[i].size, .font = F_SMALL, .color = s.textMuted);
                }
                rcBox(.w = "60px") {
                    rcTextC(files[i].date, .font = F_SMALL, .color = s.textMuted);
                }
            }
        }
    }
}

/* ── page 3: Settings ────────────────────────────────────────────────────── */

static void page_settings(RC_App *app, AppState *st) {
    RC_Style s = rcGetStyle();
    rcColumn(.gap = 2) {
        rcTextL("Settings",             .font = F_HEAD,  .color = s.text);
        rcTextL("Project configuration.",.font = F_SMALL, .color = s.textMuted);
    }
    rcColumn(.id = "panel_settings", .w = "grow", .bg = s.surface, .p = 16, .gap = 14,
              .borderRadius = "all-xl", .border = { .color = s.border, .width = "1px" },
              .shadow = { .color = rcAlpha(RC_BLACK, 70), .y = 8, .blur = 24, .spread = -4 }) {
        rcRow(.w = "grow", .gap = 12, .align = "cl") {
            rcBox(.w = "120px") {
                rcTextL("Project name", .font = F_SMALL, .color = s.textMuted);
            }
            rcBox(.w = "grow") {
                rcTextInput("in_project", st->project, sizeof st->project,
                             .placeholder = "my-rayclay-app");
            }
        }
        rcRow(.w = "grow", .gap = 12, .align = "cl") {
            rcBox(.w = "120px") {
                rcTextL("Notifications", .font = F_SMALL, .color = s.textMuted);
            }
            rcToggle("tg_notify", &st->notifications);
            rcTextC(st->notifications ? "On" : "Off",
                     .font = F_SMALL, .color = s.textMuted);
        }
        rcRow(.w = "grow", .gap = 12, .align = "cl") {
            rcBox(.w = "120px") {
                rcTextL("Monthly budget", .font = F_SMALL, .color = s.textMuted);
            }
            rcBox(.w = "grow") { rcSlider("sl_budget", &st->budget, 0.0f, 1.0f); }
            RC_String amt = rcFormat(rcAppArena(app), "$%d",
                                        (int)(st->budget * 5000.0f + 0.5f));
            rcText(amt, .font = F_SMALL, .color = s.textMuted);
        }
        rcRow(.w = "grow", .gap = 12, .align = "cl") {
            rcBox(.w = "120px") {
                rcTextL("Plan", .font = F_SMALL, .color = s.textMuted);
            }
            static const char *const plans[] = { "Free", "Pro", "Team", "Enterprise" };
            rcBox(.w = "220px") { rcCombo("cb_plan", &st->plan, plans, 4); }
        }
        /* Settings apply live (immediate mode: every widget writes AppState the
           frame it changes), so there is no dead "Save" - just a working reset. */
        rcRow(.gap = 10, .align = "cl") {
            if (rcButton("btn_reset", "Reset to defaults", RC_BTN_DEFAULT)) {
                st->project[0]    = '\0';
                st->notifications = true;
                st->budget        = 0.6f;
                st->plan          = 1;
            }
            rcTextL("Changes apply instantly.", .font = F_SMALL, .color = s.textMuted);
        }
    }
}

/* ── app callbacks ───────────────────────────────────────────────────────── */

static void update(RC_App *app, void *userData) {
    (void)app;
    ((AppState *)userData)->frame++;
}

static void layout(RC_App *app, void *userData) {
    AppState *st = (AppState *)userData;
    rcSetStyle(st->darkMode ? rcStyleDark() : rcStyleLight());
    RC_Style   s  = rcGetStyle();
    /* Warning: a runtime theme switch has to move the window too. rcSetStyle changes
       every colour the UI draws with, but it cannot reach the window behind the
       layout: the clear colour is resolved once at creation and rc_theme.h has no
       RC_App to reach. Without this line the old theme's background stays wherever
       your layout does not cover the window - and it is all you see on a frame
       RayClay holds back while it grows the layout arena. Safe to call every frame:
       the setter is change-gated, so setting the colour already in force returns
       immediately. */
    rcAppSetClearColor(app, s.background);

    rcColumn(.id = "Root", .w = "grow", .h = "grow", .bg = s.background) {
        topbar(app, st);
        rcRow(.id = "Body", .w = "grow", .h = "grow") {
            sidebar(st);
            rcColumn(.id = "Content", .w = "grow", .h = "grow", .scroll = "v",
                      .p = 20, .gap = 16, .bg = s.background) {
                switch (st->page) {
                case 0: page_dashboard();       break;
                case 1: page_analytics();       break;
                case 2: page_files();            break;
                case 3: page_settings(app, st); break;
                default: break;
                }
            }
        }
    }
    rcScrollbar("Content");
}

/* ── entry point ─────────────────────────────────────────────────────────── */

int main(void) {
    AppState state = {
        .darkMode      = true,
        .notifications = true,
        .budget        = 0.6f,
        .plan          = 1,
    };

    static const float fontSizes[F_COUNT] = {
        [F_SMALL] = 13.0f,
        [F_BODY]  = 15.0f,
        [F_HEAD]  = 20.0f,
        [F_STAT]  = 30.0f,
    };

    rcSetStyle(rcStyleDark());

    RC_AppOptions opts = {
        .width          = 1180,
        .height         = 760,
        .title          = "RayClay Console",
        .clearColor     = rcGetStyle().background,
        .fontSizes      = fontSizes,
        .fontCount      = F_COUNT,
        .scratchArenaBytes = 4096,   /* backs rcFormat (FPS + budget labels) */
        .nativeFrame    = true,
        .titlebarHeight = 56,
        .titlebar       = { .custom = true },   /* the dashboard topbar IS the titlebar */
        .updateCallback       = update,
        .layoutCallback       = layout,
        .userData       = &state,
        /* This dashboard shows a live "N fps - frame N" readout in its topbar,
           and a per-frame readout IS an animation: the text changes every frame,
           so the picture never settles and the window can never park. That is a
           deliberate trade for a console-styled demo - we keep the telemetry and
           pay for it. The default is RC_RENDER_ON_DEMAND, and for an
           ordinary app the better move is to drop the counter and park at ~0 CPU
           (ex04 does exactly that). See ex03/ex10 for asking for frames instead. */
        .renderMode     = RC_RENDER_CONTINUOUS,
    };

    return rcRunApp(&opts);
}
