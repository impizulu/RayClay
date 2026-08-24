/*
================================================================================
    messenger_app.c - the messenger app's pure-RC_ GUI + bench hooks
================================================================================

    The single implementation TU. It defines the four-function app contract
    (seed / update / layout / bench_step) + a demo-only chrome overlay, and it
    carries the header-only backend implementation (MESSENGER_BACKEND_IMPLEMENTATION).

    PURE RC_ API - RayClay types only, no layout-engine call, no <system>
    include (any raw-memory need is routed through messenger_backend.h), so this
    file passes examples/check-examples-pure-rc.sh unchanged. The FROZEN core
    (seed/update/layout) calls ZERO rcFormat: every dynamic string (timestamps,
    unread badges) is precomputed at seed into fixed-width buffers, so a formatted
    string's byte length is machine-invariant and the bench's two measured frames
    do byte-identical work. rcFormat is used ONLY in the demo-only chrome.

    Build target: rayclay_bench_messenger
================================================================================
*/
#define MESSENGER_BACKEND_IMPLEMENTATION
#include "messenger_app.h"

/* The nav-rail sidebar-toggle glyph from the shared example icon set. */
#include "icons/rc_icons_panel_left.h"

/* The frozen bench scenario length: warmup frames, then HOLD. the bench harness tunes
   this + the click coordinates to its measurement budget when it wires the app. */
#define MSG_BENCH_WARMUP 64

/* Unique element ids for the sidebar conversation rows (the layout engine needs a stable id
   per interactive element; kept as a static table so the core stays rcFormat-free). */
static const char *const CONV_IDS[MSG_MAX_CONVERSATIONS] = {
    "conv00", "conv01", "conv02", "conv03", "conv04", "conv05", "conv06", "conv07",
    "conv08", "conv09", "conv10", "conv11", "conv12", "conv13", "conv14", "conv15",
};

/* Solid tile tints for the emoji picker + shared-media grid (procedural, zero-asset,
   Latin-1-safe - real emoji glyphs are outside the bundled Latin-1 face). */
static const uint32_t TILE_TINTS[] = {
    0x6366f1, 0x10b981, 0xf59e0b, 0xec4899, 0x8b5cf6, 0x06b6d4,
    0xef4444, 0x84cc16, 0x3b82f6, 0xf97316, 0x14b8a6, 0xa855f7,
};

/* ── small components ────────────────────────────────────────────────────── */

/* A round, procedural avatar: a tinted circle with the contact's initials. The
   image path deliberately isn't used here - avatars are the gallery app's cost
   centre; a messenger's is text + scroll. */
static void avatar_circle(const char *initials, uint32_t accent, float size) {
    rcBox(.wType = RC_PX(size), .hType = RC_PX(size), .align = "cc",
           .bg = rcHex(accent), .borderRadius = "all-full") {
        rcTextC(initials, .font = F_SMALL, .color = RC_WHITE);
    }
}

/* A nav-rail icon button. The whole box is the click target. */
static bool nav_button(const char *id, RC_IconCallback icon, bool active) {
    RC_Style s = rcGetStyle();
    rcBox(.id = id, .w = "44px", .h = "44px", .align = "cc",
           .bg = active ? s.surfaceAlt : (rcIsHovered(id) ? s.surface : RC_TRANSPARENT),
           .borderRadius = "all-lg") {
        icon(20.0f, active ? s.primary : s.textMuted);
    }
    return rcClicked(id);
}

/* A sidebar conversation row: avatar + name/time + preview/unread-badge. The
   whole row is a click target (rcClicked turns the styled row into a button). */
static bool conv_row(const char *id, const MsgConversation *c, bool active) {
    RC_Style s = rcGetStyle();
    if (!c)                               /* out-of-range query returns NULL; row is inert */
        return false;
    rcRow(.id = id, .w = "grow", .h = "62px", .align = "cl", .px = 8, .gap = 10,
           .bg = active ? s.surfaceAlt : (rcIsHovered(id) ? s.surface : RC_TRANSPARENT),
           .borderRadius = "all-md") {
        avatar_circle(c->initials, c->accent, 42.0f);
        rcColumn(.w = "grow", .gap = 3) {
            rcRow(.w = "grow", .align = "cl", .gap = 6) {
                rcBox(.w = "grow", .overflow = "hidden") {
                    rcTextC(c->name, .font = F_BODY, .color = s.text, .wrap = "n");
                }
                rcTextC(c->lastTs, .font = F_SMALL, .color = s.textMuted);
            }
            rcRow(.w = "grow", .align = "cl", .gap = 6) {
                rcBox(.w = "grow", .overflow = "hidden") {
                    rcTextC(c->preview, .font = F_SMALL, .color = s.textMuted, .wrap = "n");
                }
                if (c->badge[0]) {
                    rcBox(.bg = s.primary, .px = 7, .py = 1, .align = "cc",
                           .borderRadius = "all-full") {
                        rcTextC(c->badge, .font = F_SMALL, .color = RC_WHITE);
                    }
                }
            }
        }
    }
    return rcClicked(id);
}

/* One message bubble: outgoing right + accent, incoming left + surface. A grow
   spacer on the free side pushes the fixed-width (62%) bubble to the correct edge;
   the fixed width makes the wrap deterministic (machine-invariant). */
static void bubble(const MsgMessage *m) {
    if (!m)                               /* out-of-range query returns NULL; bubble is inert */
        return;
    RC_Style s = rcGetStyle();
    bool out = (m->dir == MSG_DIR_OUTGOING);
    /* A content-sized bubble hugs its text, with the time inline - so a one-word
       reply is a small pill, not a half-width block; a grow spacer on the free side
       floats it right (outgoing) or left (incoming). */
    rcRow(.w = "grow", .gap = 0, .align = "cl") {
        if (out)
            rcBox(.w = "grow") {}
        rcRow(.bg = out ? s.primary : s.surfaceAlt, .px = 12, .py = 7, .gap = 8,
               .align = "cr", .borderRadius = "all-lg") {
            rcTextC(m->text, .font = F_BODY, .color = out ? RC_WHITE : s.text);
            rcTextC(m->ts, .font = F_SMALL,
                     .color = out ? rcAlpha(RC_WHITE, 190) : s.textMuted);
        }
        if (!out)
            rcBox(.w = "grow") {}
    }
}

/* An animated "typing..." bubble - the one app-level timer, driven by the injected
   frame tick (never a wall clock, B4). At the bench freeze the tick is pinned, so
   the dots are static and the measured frame is byte-identical. */
static void typing_indicator(uint64_t tick) {
    RC_Style s = rcGetStyle();
    static const char *const DOTS[4] = { "", ".", "..", "..." };
    rcRow(.w = "grow", .gap = 8, .align = "tl") {
        rcRow(.bg = s.surfaceAlt, .px = 12, .py = 8, .gap = 1, .align = "cl",
               .borderRadius = "all-lg") {
            rcTextL("typing", .font = F_SMALL, .color = s.textMuted);
            rcTextC(DOTS[(tick / 20) % 4], .font = F_SMALL, .color = s.textMuted);
        }
        rcBox(.w = "grow") {}
    }
}

/* ── regions ─────────────────────────────────────────────────────────────── */

/* The custom titlebar (nativeFrame + .titlebar.custom): brand + theme toggle +
   the bundled window controls. RC_ID_WINDOW_DRAG makes the band draggable. */
static void msg_topbar(AppState *st) {
    RC_Style s = rcGetStyle();
    rcRow(.w = "grow", .h = "52px", .bg = s.chrome, .px = 14, .gap = 10, .align = "cl") {
        /* Only the non-interactive brand + empty stretch is the OS drag handle: an
           interactive widget inside RC_ID_WINDOW_DRAG loses its click to the window
           move on desktop, so the theme toggle + window controls sit outside it. */
        rcRow(.id = RC_ID_WINDOW_DRAG, .w = "grow", .h = "grow", .align = "cl", .gap = 10) {
            rcBox(.w = "26px", .h = "26px", .align = "cc",
                   .bg = s.primary, .borderRadius = "all-md") {
                rcTextL("R", .font = F_BODY, .color = RC_WHITE);
            }
            rcTextL("RayClay Messenger", .font = F_HEAD, .color = s.text);
        }
        rcRow(.gap = 8, .align = "cl") {
            rcTextC(st->darkMode ? "Dark" : "Light", .font = F_SMALL, .color = s.textMuted);
            rcToggle("tg_theme", &st->darkMode);
        }
        rcWindowControls();
    }
}

static void msg_navrail(AppState *st) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "64px", .h = "grow", .bg = s.surface,
              .py = 12, .gap = 6, .align = "tc") {
        /* Collapse / expand the conversation sidebar; the button reads "active"
           while the sidebar is shown. */
        if (nav_button("nav_toggle", rcIconPanelLeft, !st->sidebarCollapsed))
            st->sidebarCollapsed = !st->sidebarCollapsed;
        rcBox(.w = "grow", .h = "grow") {}
        /* The "you" avatar opens settings - the account entry point, so the rail
           carries no dead decorative element. */
        rcBox(.id = "nav_me", .align = "cc", .tooltip = "You - settings") {
            avatar_circle("ME", 0x6366f1, 40.0f);
        }
        if (rcClicked("nav_me")) {
            st->modalSettings = true;
            st->modalAttach   = false;
        }
    }
}

static void msg_sidebar(AppState *st) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "300px", .h = "grow", .bg = s.surface, .p = 10, .gap = 8) {
        rcTextL("Chats", .font = F_TITLE, .color = s.text);
        rcTextInput("search", st->search, sizeof st->search, .placeholder = "Search conversations");
        rcColumn(.id = "SideScroll", .w = "grow", .h = "grow", .scroll = "v", .gap = 2, .pr = 12) {
            int n = msg_conversation_count(&st->store);
            int shown = 0;
            for (int i = 0; i < n; i++) {
                const MsgConversation *c = msg_conversation_at(&st->store, i);
                if (c && !msg_name_matches(c->name, st->search))
                    continue;                 /* the search box filters the list live */
                shown++;
                if (conv_row(CONV_IDS[i], c, i == st->openConv)) {
                    st->openConv = i;
                    msg_mark_read(&st->store, i);
                }
            }
            if (!shown)
                rcTextC("No conversations match.", .font = F_SMALL, .color = s.textMuted);
        }
    }
}

static void msg_thread(AppState *st) {
    RC_Style s = rcGetStyle();
    const MsgConversation *c = msg_conversation_at(&st->store, st->openConv);
    if (!c)
        return;                     /* no open conversation: render nothing (defensive) */

    rcColumn(.w = "grow", .h = "grow", .bg = s.background) {
        /* header - click the avatar/name to open this contact's profile (the info
           drawer). The avatar IS the affordance; no separate expand button. */
        rcRow(.w = "grow", .h = "60px", .bg = s.chrome, .px = 14, .align = "cl") {
            rcRow(.id = "hdr_profile", .w = "grow", .gap = 10, .align = "cl", .px = 6, .py = 5,
                   .borderRadius = "all-md", .tooltip = "View profile",
                   .bg = st->infoOpen ? s.surfaceAlt
                       : (rcIsHovered("hdr_profile") ? s.surface : RC_TRANSPARENT)) {
                avatar_circle(c->initials, c->accent, 40.0f);
                rcColumn(.gap = 1) {
                    rcTextC(c->name, .font = F_HEAD, .color = s.text, .wrap = "n");
                    rcRow(.gap = 5, .align = "cl") {
                        rcBox(.w = "8px", .h = "8px", .bg = s.successHover, .borderRadius = "all-full") {}
                        rcTextL("online", .font = F_SMALL, .color = s.textMuted);
                    }
                }
            }
            if (rcClicked("hdr_profile"))
                st->infoOpen = !st->infoOpen;
        }
        /* message thread (the dense many-small-runs + nested-scissor cost path) */
        rcColumn(.id = "ThreadScroll", .w = "grow", .h = "grow", .scroll = "v",
                  .p = 16, .gap = 8) {
            int n = msg_thread_count(&st->store, st->openConv);
            for (int i = 0; i < n; i++)
                bubble(msg_thread_at(&st->store, st->openConv, i));
            /* Only the active chat shows a live "typing" bubble, so the app does not
               claim every contact is perpetually typing. */
            if (st->openConv == 0)
                typing_indicator(st->tick);
        }
        /* composer */
        rcRow(.w = "grow", .bg = s.surface, .p = 10, .gap = 8, .align = "cl") {
            rcBox(.id = "btn_attach", .w = "40px", .h = "40px", .align = "cc",
                   .bg = rcIsHovered("btn_attach") ? s.surfaceAlt : RC_TRANSPARENT,
                   .borderRadius = "all-md", .tooltip = "Attach a photo") {
                rcTextL("+", .font = F_TITLE, .color = s.textMuted);
            }
            if (rcClicked("btn_attach")) {
                st->modalAttach   = true;
                st->modalSettings = false;
            }
            rcBox(.w = "grow") {
                rcTextInput("composer", st->composer, sizeof st->composer,
                             .placeholder = "Message");
            }
            if (rcButton("btn_send", "Send", RC_BTN_PRIMARY) && st->composer[0]) {
                int len = 0;
                while (st->composer[len])
                    len++;
                msg_send_text(&st->store, st->openConv, st->composer, len);
                st->composer[0] = '\0';
            }
        }
    }
}

/* The right-hand info drawer (toggled): conversation details + a shared-media
   grid (the section-3 grid slot) + a per-chat toggle. */
static void msg_info_drawer(AppState *st) {
    RC_Style s = rcGetStyle();
    const MsgConversation *c = msg_conversation_at(&st->store, st->openConv);
    if (!c)
        return;                     /* no open conversation: render nothing (defensive) */
    rcColumn(.w = "260px", .h = "grow", .bg = s.surface, .p = 16, .gap = 12,
              .align = "tc") {
        avatar_circle(c->initials, c->accent, 72.0f);
        rcTextC(c->name, .font = F_HEAD, .color = s.text);
        rcTextL("online", .font = F_SMALL, .color = s.textMuted);
        rcBox(.w = "grow", .h = "1px", .bg = s.border) {}
        rcRow(.w = "grow", .align = "cl") {
            rcTextL("Shared media", .font = F_SMALL, .color = s.textMuted);
        }
        rcColumn(.w = "grow", .gap = 6) {
            for (int r = 0; r < 3; r++) {
                rcRow(.w = "grow", .gap = 6) {
                    for (int col = 0; col < 3; col++) {
                        rcBox(.w = "grow", .h = "52px", .borderRadius = "all-md",
                               .bg = rcHex(TILE_TINTS[(r * 3 + col) % 12])) {}
                    }
                }
            }
        }
        rcRow(.w = "grow", .align = "cl", .gap = 8) {
            rcTextL("Read receipts", .font = F_SMALL, .color = s.text);
            rcBox(.w = "grow") {}
            rcToggle("tg_receipts_info", &st->readReceipts);
        }
    }
}

/* The modals - placed OUTSIDE the root so their scrim covers the whole window. */
static void msg_modals(AppState *st) {
    RC_Style s = rcGetStyle();

    if (rcBeginModal("modal_attach", &st->modalAttach)) {
        rcColumn(.w = "380px", .bg = s.surface, .p = 18, .gap = 12,
                  .borderRadius = "all-xl") {
            rcTextL("Attach", .font = F_TITLE, .color = s.text);
            rcTextL("Pick a photo to send.", .font = F_SMALL, .color = s.textMuted);
            rcColumn(.w = "grow", .gap = 6) {
                for (int r = 0; r < 3; r++) {
                    rcRow(.w = "grow", .gap = 6) {
                        for (int col = 0; col < 6; col++) {
                            rcBox(.w = "grow", .h = "40px", .borderRadius = "all-md",
                                   .bg = rcHex(TILE_TINTS[(r * 6 + col) % 12])) {}
                        }
                    }
                }
            }
            rcCheckbox("cb_origq", "Send at original quality", &st->attachOriginal);
            rcRow(.gap = 8) {
                if (rcButton("btn_attach_send", "Send", RC_BTN_PRIMARY))
                    st->modalAttach = false;
                if (rcButton("btn_attach_cancel", "Cancel", RC_BTN_DEFAULT))
                    st->modalAttach = false;
            }
        }
        rcEndModal();
    }

    if (rcBeginModal("modal_settings", &st->modalSettings)) {
        static const char *const statuses[] = { "Available", "Busy", "Away" };
        rcColumn(.w = "400px", .bg = s.surface, .p = 18, .gap = 14,
                  .borderRadius = "all-xl") {
            rcTextL("Settings", .font = F_TITLE, .color = s.text);
            rcRow(.w = "grow", .align = "cl") {
                rcTextL("Read receipts", .font = F_BODY, .color = s.text);
                rcBox(.w = "grow") {}
                rcToggle("tg_receipts", &st->readReceipts);
            }
            rcRow(.w = "grow", .align = "cl", .gap = 12) {
                rcBox(.w = "150px") {
                    rcTextL("Notification volume", .font = F_BODY, .color = s.text);
                }
                rcBox(.w = "grow") { rcSlider("sl_vol", &st->notifVolume, 0.0f, 1.0f); }
            }
            rcRow(.w = "grow", .align = "cl", .gap = 12) {
                rcBox(.w = "150px") {
                    rcTextL("Status", .font = F_BODY, .color = s.text);
                }
                rcBox(.w = "grow") { rcCombo("cb_status", &st->statusCombo, statuses, 3); }
            }
            rcRow(.gap = 8) {
                if (rcButton("btn_settings_done", "Done", RC_BTN_PRIMARY))
                    st->modalSettings = false;
            }
        }
        rcEndModal();
    }
}

/* ── the app contract ────────────────────────────────────────────────────── */

void messenger_seed(AppState *st, unsigned seed) {
    msg_memzero(st, sizeof *st);        /* B2: zero all (incl. padding) THEN set fields */
    msg_store_seed(&st->store, seed);
    st->openConv       = 0;
    st->darkMode       = true;
    st->readReceipts   = true;
    st->attachOriginal = true;
    st->notifVolume    = 0.7f;
    st->statusCombo    = 0;
    st->seeded         = true;
    msg_mark_read(&st->store, st->openConv);
}

void messenger_update(AppState *st, const AppCtx *ctx) {
    msg_store_step(&st->store, ctx->dt);   /* dt <= 0 (freeze) => a no-op */
    if (ctx->dt > 0.0f)
        st->tick++;                        /* the app clock; pinned at freeze */
}

void messenger_layout(AppState *st, const AppCtx *ctx) {
    (void)ctx;
    rcSetStyle(st->darkMode ? rcStyleDark() : rcStyleLight());
    RC_Style s = rcGetStyle();

    rcColumn(.id = "Root", .w = "grow", .h = "grow", .bg = s.background) {
        msg_topbar(st);
        rcRow(.id = "Body", .w = "grow", .h = "grow") {
            msg_navrail(st);
            if (!st->sidebarCollapsed)
                msg_sidebar(st);
            msg_thread(st);
            if (st->infoOpen)
                msg_info_drawer(st);
        }
    }
    msg_modals(st);                 /* modals sit outside Root (full-window scrim) */

    rcScrollbar("SideScroll");
    rcScrollbar("ThreadScroll");
}

void messenger_demo_chrome(AppState *st, const AppCtx *ctx) {
    if (ctx->mode != APP_DEMO || !ctx->arena)
        return;
    /* A floating perf/status HUD - demo-only, floats over the UI so it can never
       reflow the measured core. It is a PASSIVE readout: RC_CAPTURE_PASSTHROUGH lets
       clicks fall through (so it never blocks the titlebar window controls or the
       composer), and it sits above the composer clear of the titlebar. rcFormat is
       fine here (never in the bench path). */
    RC_String hud = rcFormat(ctx->arena, "%.0f fps \xc2\xb7 %d chats \xc2\xb7 %d unread",
                                ctx->dt > 0.0f ? 1.0f / ctx->dt : 0.0f,
                                msg_conversation_count(&st->store),
                                msg_unread_total(&st->store));
    rcBox(.id = "demo_hud", .bg = rcAlpha(RC_BLACK, 150), .px = 10, .py = 5,
           .borderRadius = "all-full",
           .floating = { .to = RC_ATTACH_ROOT, .parent = RC_ANCHOR_BOTTOM_RIGHT,
                         .element = RC_ANCHOR_BOTTOM_RIGHT, .offset = { -16, -72 },
                         .capture = RC_CAPTURE_PASSTHROUGH }) {
        rcText(hud, .font = F_SMALL, .color = RC_WHITE);
    }
}

void messenger_bench_step(AppState *st, const AppInputSink *in, int frame) {
    /* The frozen scripted scenario. EVERY user action goes through the input sink (B3),
       so the bench exercises the real hit-test / focus / caret cost; only the seeded
       incoming is a direct backend call. At/after MSG_BENCH_WARMUP the app HOLDS - a
       strict no-op - so the bench harness's double-rendered measured frame is byte-identical.

       DETERMINISM AT THE HOLD: RayClay's caret blink + tooltip dwell read rci_input_time()
       (real time on the shipped runner), NOT the injected dt, so the frozen frame must
       carry NO focused text-input and the pointer must rest over NO tooltipped element.
       The pre-hold BLUR + OFF-CANVAS PARK below guarantees that whatever the click
       coordinates are. A mock clock pins them further, and RAYCLAY_FIXED_DT is the
       durable fix.
       The click COORDINATES are a first draft; retune them to your own layout and
       budget (and add a modal open/settle during warmup if you want that measured). */
    if (!in || frame >= MSG_BENCH_WARMUP)
        return;                                        /* the HOLD */

    if (frame == 0) {
        in->move(in->ctx, 700.0f, 300.0f);             /* park the pointer over the thread so the wheel targets it */
    } else if (frame < 12) {
        in->wheel(in->ctx, 0.0f, -3.0f);               /* scroll the thread across the backlog */
    } else if (frame == 18) {
        in->move(in->ctx, 700.0f, 690.0f);             /* focus the composer: press ... */
        in->button(in->ctx, APP_MBTN_LEFT, true);
    } else if (frame == 19) {
        in->button(in->ctx, APP_MBTN_LEFT, false);      /* ... release (a real click edge) */
    } else if (frame >= 22 && frame < 46) {
        static const char TYPED[] = "great work on the trend!";  /* 24 ASCII chars, one/frame */
        in->text(in->ctx, (unsigned int)(unsigned char)TYPED[frame - 22]);
    } else if (frame == 47) {
        msg_inject_incoming(&st->store, 0);            /* the seeded incoming (backend event, not input) */
    } else if (frame == MSG_BENCH_WARMUP - 2) {
        in->move(in->ctx, -100.0f, -100.0f);           /* blur the composer + park off-canvas: press ... */
        in->button(in->ctx, APP_MBTN_LEFT, true);
    } else if (frame == MSG_BENCH_WARMUP - 1) {
        in->button(in->ctx, APP_MBTN_LEFT, false);      /* ... release; pointer stays off-canvas into the hold */
    }
}
