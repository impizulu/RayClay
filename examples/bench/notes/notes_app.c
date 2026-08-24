/*
================================================================================
    notes_app.c - the notes app's pure-RC_ GUI + bench hooks
================================================================================

    The single implementation TU. It defines the four-function app contract
    (seed / update / layout / bench_step) + a demo-only chrome overlay, and it
    carries the header-only backend implementation (NOTES_BACKEND_IMPLEMENTATION).

    PURE RC_ API - RayClay types only, no layout-engine call, no <system>
    include (any raw-memory need is routed through notes_backend.h), so this file
    passes examples/check-examples-pure-rc.sh unchanged. The FROZEN core
    (seed/update/layout) calls ZERO rcFormat: every dynamic string (dates, word
    counts, reading time) is precomputed at seed into fixed-width buffers, so a
    formatted string's byte length is machine-invariant and the bench's two measured
    frames do byte-identical work. rcFormat is used ONLY in the demo-only chrome.

    Build target: rayclay_bench_notes
================================================================================
*/
#define NOTES_BACKEND_IMPLEMENTATION
#include "notes_app.h"

/* Nav-rail glyph from the shared example icon set (as ex05 / the messenger do):
   the semantics are approximate - it reads as a clean icon rail. */
#include "icons/rc_icons_settings.h"

/* The frozen bench scenario length: warmup frames, then HOLD. the bench harness tunes this
   + the click coordinates to its measurement budget when it wires the app. */
#define NOTES_BENCH_WARMUP 64

/* "New note" opens this seeded note. The frozen backend has no note_create, so a
   short DRAFT stands in for a fresh note (note 11 = "Weekend backlog", a short draft
   - see notes_backend.h's corpus), which reads as a genuinely different blank-ish
   editor rather than dropping the user back onto whatever note was already open. */
#define NOTE_NEW_TARGET 11

/* Unique element ids for the sidebar note rows (the layout engine needs a stable id per
   interactive element; a static table keeps the core rcFormat-free). */
static const char *const NOTE_IDS[NOTE_MAX_NOTES] = {
    "note00", "note01", "note02", "note03", "note04", "note05", "note06", "note07",
    "note08", "note09", "note10", "note11", "note12", "note13", "note14", "note15",
    "note16", "note17", "note18", "note19", "note20", "note21", "note22", "note23",
    "note24", "note25", "note26", "note27", "note28", "note29", "note30", "note31",
};

/* Category options for the publish + settings combos, surfaced in the Preview
   details table. Fixed literals -> length-invariant, rcFormat-free. */
static const char *const CATEGORIES[] = {
    "Engineering", "Design", "Product", "Personal",
};

/* Status accents: a published note reads emerald, a draft amber (procedural,
   Latin-1-safe - no glyphs, just tinted dots/chips). */
#define NOTE_TINT_PUBLISHED 0x10b981
#define NOTE_TINT_DRAFT     0xf59e0b

/* ── small helpers ───────────────────────────────────────────────────────── */

/* Map the settings "Editor size" slider (px) to the nearest baked ladder id, so the
   slider VISIBLY steps the Write-tab body text instead of being a cosmetic no-op. */
static NoteFont editor_body_font(float px) {
    if (px < 15.0f) return F_SMALL;
    if (px < 17.0f) return F_BODY;
    if (px < 20.0f) return F_MD;
    return F_HEAD;
}

/* Open note `i`: set it as the selection and sync the editor title + body buffers to
   it. NULL-guarded (empty-state safe); does NOT change the tab (callers decide). */
static void notes_open(AppState *st, int i) {
    const Note *n = note_at(&st->store, i);
    if (!n)
        return;
    st->openNote = i;
    rcStrCopy(st->title, n->title, sizeof st->title);
    note_body_text(n, st->body, (int)sizeof st->body);
}

/* A nav-rail icon button. The whole box is the click target; a hand-rolled Box
   button opts into the pointer cursor real RC_ widgets set automatically. */
static bool nav_button(const char *id, RC_IconCallback icon, bool active) {
    RC_Style s = rcGetStyle();
    rcBox(.id = id, .w = "44px", .h = "44px", .align = "cc",
           .bg = active ? s.surfaceAlt : (rcIsHovered(id) ? s.surface : RC_TRANSPARENT),
           .borderRadius = "all-lg") {
        icon(20.0f, active ? s.primary : s.textMuted);
    }
    return rcClicked(id);
}

/* A sidebar folder-filter pill (All / Drafts / Published). */
static bool filter_pill(const char *id, const char *label, bool active) {
    RC_Style s = rcGetStyle();
    rcBox(.id = id, .px = 10, .py = 5, .align = "cc",
           .bg = active ? s.primary : (rcIsHovered(id) ? s.surfaceAlt : RC_TRANSPARENT),
           .borderRadius = "all-full") {
        rcTextC(label, .font = F_SMALL, .color = active ? RC_WHITE : s.textMuted);
    }
    return rcClicked(id);
}

/* A Write/Preview tab: a centered label over an underline that lights when active. */
static bool tab_button(const char *id, const char *label, bool active) {
    RC_Style s = rcGetStyle();
    rcColumn(.id = id, .h = "grow", .px = 8, .align = "cc") {
        rcBox(.h = "grow", .align = "cc") {
            rcTextC(label, .font = F_BODY, .color = active ? s.text : s.textMuted);
        }
        rcBox(.w = "28px", .h = "2px",
               .bg = active ? s.primary : RC_TRANSPARENT, .borderRadius = "all-full") {}
    }
    return rcClicked(id);
}

/* A sidebar note card: title + status dot, snippet, then date + status. The whole
   card is a click target (rcClicked turns the styled column into a button). */
static bool note_row(const char *id, const Note *n, bool active) {
    RC_Style s = rcGetStyle();
    bool pub = (n->status == NOTE_PUBLISHED);
    rcColumn(.id = id, .w = "grow", .p = 10, .gap = 5,
              .bg = active ? s.surfaceAlt : (rcIsHovered(id) ? s.surface : RC_TRANSPARENT),
              .borderRadius = "all-md") {
        rcRow(.w = "grow", .align = "cl", .gap = 6) {
            rcBox(.w = "grow", .overflow = "hidden") {
                rcTextC(n->title, .font = F_BODY, .color = s.text, .wrap = "n");
            }
            rcBox(.w = "8px", .h = "8px", .borderRadius = "all-full",
                   .bg = rcHex(pub ? NOTE_TINT_PUBLISHED : NOTE_TINT_DRAFT)) {}
        }
        rcBox(.w = "grow", .overflow = "hidden") {
            rcTextC(n->snippet, .font = F_SMALL, .color = s.textMuted, .wrap = "n");
        }
        rcRow(.w = "grow", .align = "cl", .gap = 6) {
            rcTextC(n->date, .font = F_SMALL, .color = s.textMuted);
            rcBox(.w = "grow") {}
            rcTextC(pub ? "Published" : "Draft", .font = F_SMALL, .color = s.textMuted);
        }
    }
    return rcClicked(id);
}

/* One label/value row of the Preview "Post details" table (the section-3 grid slot). */
static void details_row(const char *label, const char *value) {
    RC_Style s = rcGetStyle();
    rcRow(.w = "grow", .align = "cl", .gap = 12) {
        rcBox(.w = "110px") {
            rcTextC(label, .font = F_SMALL, .color = s.textMuted);
        }
        rcBox(.w = "grow") {
            rcTextC(value, .font = F_SMALL, .color = s.text);
        }
    }
}

/* ── main-pane tabs ──────────────────────────────────────────────────────── */

/* The Write tab: a fixed-width editorial column - title + tags inputs, then the note
   body as a live multi-line editor (rcTextArea over st->body, seeded from the open
   note in notes_open). The fixed 720px column makes the wrap deterministic
   (machine-invariant). The body font steps with the settings slider (editor_body_font). */
static void notes_write(AppState *st) {
    RC_Style s = rcGetStyle();
    NoteFont body_font = editor_body_font(st->editorFontPx);
    rcColumn(.id = "WriteScroll", .w = "grow", .h = "grow", .scroll = "v",
              .p = 24, .align = "tc") {
        rcColumn(.w = "720px", .gap = 14) {
            rcTextInput("title_in", st->title, sizeof st->title, .placeholder = "Untitled note");
            rcTextInput("tags_in", st->tags, sizeof st->tags,
                         .placeholder = "Add tags, comma separated");
            rcBox(.w = "grow", .h = "1px", .bg = s.border) {}
            rcTextArea("body_in", st->body, sizeof st->body,
                        .font = body_font, .rows = 18, .placeholder = "Write your note");
        }
    }
}

/* The Preview tab: the rendered post - display title, byline, the wrapped body (the
   B9 cost), a details table, and tag chips. This is the frozen bench scene. */
static void notes_preview(AppState *st, const Note *n) {
    RC_Style s = rcGetStyle();
    if (!n)
        return;                     /* no open note: render nothing (defensive) */
    bool pub = (n->status == NOTE_PUBLISHED);
    rcColumn(.id = "PreviewScroll", .w = "grow", .h = "grow", .scroll = "v",
              .p = 24, .align = "tc") {
        rcColumn(.w = "720px", .gap = 12) {
            rcTextC(n->title, .font = F_TITLE, .color = s.text);
            /* byline: author, date, status chip (\xc2\xb7 = Latin-1 middot separator) */
            rcRow(.w = "grow", .align = "cl", .gap = 8) {
                rcTextL("By You", .font = F_SMALL, .color = s.textMuted);
                rcTextL("\xc2\xb7", .font = F_SMALL, .color = s.textMuted);
                rcTextC(n->date, .font = F_SMALL, .color = s.textMuted);
                rcTextL("\xc2\xb7", .font = F_SMALL, .color = s.textMuted);
                rcBox(.px = 8, .py = 2, .align = "cc", .borderRadius = "all-full",
                       .bg = pub ? rcHex(NOTE_TINT_PUBLISHED) : s.surfaceAlt) {
                    rcTextC(pub ? "Published" : "Draft", .font = F_SMALL,
                             .color = pub ? RC_WHITE : s.textMuted);
                }
            }
            rcBox(.w = "grow", .h = "1px", .bg = s.border) {}
            for (int p = 0; p < n->body.nParas; p++)   /* the large wrapped body: the B9 cost */
                rcTextC(n->body.paras[p], .font = F_BODY, .color = s.text);
            rcBox(.w = "grow", .h = "1px", .bg = s.border) {}
            rcTextL("Post details", .font = F_MD, .color = s.text);
            details_row("Status",   pub ? "Published" : "Draft");
            details_row("Author",   "You");
            details_row("Date",     n->date);
            details_row("Reading",  n->meta);
            details_row("Category", CATEGORIES[st->category]);
            rcRow(.w = "grow", .gap = 6, .align = "cl") {
                for (int t = 0; t < n->tagCount; t++) {
                    rcBox(.bg = s.surfaceAlt, .px = 8, .py = 3, .borderRadius = "all-full") {
                        rcTextC(n->tags[t], .font = F_SMALL, .color = s.textMuted);
                    }
                }
            }
        }
    }
}

/* ── regions ─────────────────────────────────────────────────────────────── */

/* The custom titlebar (nativeFrame + .titlebar.custom): brand + title + the bundled
   window controls. RC_ID_WINDOW_DRAG makes the whole band draggable, so it carries NO
   app-interactive widget - only the RC_WINDOW_* controls, which are drag-exempt. */
static void notes_topbar(void) {
    RC_Style s = rcGetStyle();
    rcRow(.id = RC_ID_WINDOW_DRAG, .w = "grow", .h = "52px", .bg = s.chrome,
           .px = 14, .gap = 10, .align = "cl") {
        rcBox(.w = "26px", .h = "26px", .align = "cc",
               .bg = s.primary, .borderRadius = "all-md") {
            rcTextL("N", .font = F_BODY, .color = RC_WHITE);
        }
        rcTextL("RayClay Notes", .font = F_HEAD, .color = s.text);
        rcBox(.w = "grow") {}
        rcWindowControls();
    }
}

static void notes_navrail(AppState *st) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "64px", .h = "grow", .bg = s.surface,
              .py = 12, .gap = 6, .align = "tc") {
        rcBox(.w = "grow", .h = "grow") {}
        if (nav_button("nav_settings", rcIconSettings, false)) {
            st->modalSettings = true;
            st->modalPublish  = false;
        }
        rcBox(.w = "40px", .h = "40px", .align = "cc",
               .bg = rcHex(0x6366f1), .borderRadius = "all-full") {
            rcTextL("Y", .font = F_SMALL, .color = RC_WHITE);
        }
    }
}

static void notes_sidebar(AppState *st) {
    RC_Style s = rcGetStyle();
    rcColumn(.w = "320px", .h = "grow", .bg = s.surface, .p = 10, .gap = 8) {
        rcRow(.w = "grow", .align = "cl") {
            rcTextL("Notes", .font = F_TITLE, .color = s.text);
            rcBox(.w = "grow") {}
            rcBox(.id = "btn_add", .w = "32px", .h = "32px", .align = "cc",
                   .bg = rcIsHovered("btn_add") ? s.surfaceAlt : RC_TRANSPARENT,
                   .borderRadius = "all-md", .tooltip = "New note") {
                rcTextL("+", .font = F_HEAD, .color = s.textMuted);
            }
            if (rcClicked("btn_add")) {
                notes_open(st, NOTE_NEW_TARGET);
                st->tab = 0;
            }
        }
        rcTextInput("search", st->search, sizeof st->search, .placeholder = "Search notes");
        rcRow(.w = "grow", .gap = 6) {
            if (filter_pill("pill_all",    "All",       st->folderFilter == 0)) st->folderFilter = 0;
            if (filter_pill("pill_drafts", "Drafts",    st->folderFilter == 1)) st->folderFilter = 1;
            if (filter_pill("pill_pub",    "Published", st->folderFilter == 2)) st->folderFilter = 2;
        }
        rcColumn(.id = "NoteList", .w = "grow", .h = "grow", .scroll = "v", .gap = 4, .pr = 12) {
            int n = note_count(&st->store);
            for (int i = 0; i < n; i++) {
                const Note *nt = note_at(&st->store, i);
                if (!nt)                  /* out-of-range query returns NULL; skip the row */
                    continue;
                /* filter by status, but ALWAYS keep the open note's row visible so a
                   filter change can never strand the note shown in the main pane */
                if (i != st->openNote) {
                    if (st->folderFilter == 1 && nt->status != NOTE_DRAFT)     continue;
                    if (st->folderFilter == 2 && nt->status != NOTE_PUBLISHED) continue;
                }
                if (note_row(NOTE_IDS[i], nt, i == st->openNote))
                    notes_open(st, i);
            }
        }
    }
}

static void notes_main(AppState *st) {
    RC_Style s = rcGetStyle();
    const Note *n = note_at(&st->store, st->openNote);
    rcColumn(.w = "grow", .h = "grow", .bg = s.background) {
        /* tab bar: Write | Preview + word-count/read-time + Publish */
        rcRow(.w = "grow", .h = "52px", .bg = s.chrome, .px = 14, .gap = 4, .align = "cl") {
            if (tab_button("tab_write",   "Write",   st->tab == 0)) st->tab = 0;
            if (tab_button("tab_preview", "Preview", st->tab == 1)) st->tab = 1;
            rcBox(.w = "grow") {}
            if (n)
                rcTextC(n->meta, .font = F_SMALL, .color = s.textMuted);
            if (rcButton("btn_publish", "Publish", RC_BTN_PRIMARY)) {
                st->modalPublish  = true;
                st->modalSettings = false;
            }
        }
        if (st->tab == 0)
            notes_write(st);
        else
            notes_preview(st, n);
    }
}

/* The modals - placed OUTSIDE the root so their scrim covers the whole window. */
static void notes_modals(AppState *st) {
    RC_Style s = rcGetStyle();

    if (rcBeginModal("modal_publish", &st->modalPublish)) {
        rcColumn(.w = "420px", .bg = s.surface, .p = 18, .gap = 12,
                  .borderRadius = "all-xl") {
            rcTextL("Publish note", .font = F_TITLE, .color = s.text);
            rcTextL("Review the details before publishing.", .font = F_SMALL, .color = s.textMuted);
            rcRow(.w = "grow", .align = "cl") {
                rcTextL("Public", .font = F_BODY, .color = s.text);
                rcBox(.w = "grow") {}
                rcToggle("tg_public", &st->isPublic);
            }
            rcColumn(.w = "grow", .gap = 4) {
                rcTextL("Slug", .font = F_SMALL, .color = s.textMuted);
                rcTextInput("slug_in", st->slug, sizeof st->slug, .placeholder = "my-note");
            }
            rcColumn(.w = "grow", .gap = 4) {
                rcTextL("Summary", .font = F_SMALL, .color = s.textMuted);
                rcTextInput("summary_in", st->summary, sizeof st->summary,
                             .placeholder = "A short summary");
            }
            rcRow(.w = "grow", .align = "cl", .gap = 12) {
                rcBox(.w = "120px") {
                    rcTextL("Category", .font = F_BODY, .color = s.text);
                }
                rcBox(.w = "grow") { rcCombo("cb_cat", &st->category, CATEGORIES, 4); }
            }
            rcRow(.gap = 8) {
                if (rcButton("btn_pub_confirm", "Publish", RC_BTN_PRIMARY)) {
                    note_publish(&st->store, st->openNote);
                    st->modalPublish = false;
                }
                if (rcButton("btn_pub_cancel", "Cancel", RC_BTN_DEFAULT))
                    st->modalPublish = false;
            }
        }
        rcEndModal();
    }

    if (rcBeginModal("modal_settings", &st->modalSettings)) {
        rcColumn(.w = "400px", .bg = s.surface, .p = 18, .gap = 14,
                  .borderRadius = "all-xl") {
            rcTextL("Settings", .font = F_TITLE, .color = s.text);
            rcRow(.w = "grow", .align = "cl") {
                rcTextL("Dark mode", .font = F_BODY, .color = s.text);
                rcBox(.w = "grow") {}
                rcToggle("tg_dark_set", &st->darkMode);
            }
            rcRow(.w = "grow", .align = "cl") {
                rcTextL("Spellcheck", .font = F_BODY, .color = s.text);
                rcBox(.w = "grow") {}
                rcToggle("tg_spell", &st->spellcheck);
            }
            rcRow(.w = "grow", .align = "cl") {
                rcTextL("Auto-save", .font = F_BODY, .color = s.text);
                rcBox(.w = "grow") {}
                rcToggle("tg_autosave", &st->autoSave);
            }
            rcRow(.w = "grow", .align = "cl", .gap = 12) {
                rcBox(.w = "150px") {
                    rcTextL("Editor size", .font = F_BODY, .color = s.text);
                }
                rcBox(.w = "grow") { rcSlider("sl_fontsize", &st->editorFontPx, 12.0f, 24.0f); }
            }
            rcRow(.w = "grow", .align = "cl", .gap = 12) {
                rcBox(.w = "150px") {
                    rcTextL("Default category", .font = F_BODY, .color = s.text);
                }
                rcBox(.w = "grow") { rcCombo("cb_cat2", &st->category, CATEGORIES, 4); }
            }
            rcRow(.gap = 8) {
                if (rcButton("btn_set_done", "Done", RC_BTN_PRIMARY))
                    st->modalSettings = false;
            }
        }
        rcEndModal();
    }
}

/* ── the app contract ────────────────────────────────────────────────────── */

void notes_seed(AppState *st, unsigned seed) {
    note_memzero(st, sizeof *st);       /* B2: zero all (incl. padding) THEN set fields */
    note_store_seed(&st->store, seed);
    st->openNote     = 2;                /* default-open note 2: the long-body DRAFT (robust bench freeze + meaningful publish) */
    st->tab          = 0;
    st->folderFilter = 0;
    st->category     = 0;
    st->darkMode     = true;
    st->spellcheck   = true;
    st->autoSave     = true;
    st->isPublic     = false;
    st->editorFontPx = 16.0f;
    st->seeded       = true;
    notes_open(st, 2);                  /* sync the editor title buffer to the open note */
}

void notes_update(AppState *st, const AppCtx *ctx) {
    note_store_step(&st->store, ctx->dt);   /* dt <= 0 (freeze) => a no-op */
    /* Keep the open note's title in sync with the editor buffer (the Write->Preview
       round-trip). Idempotent: the backend clamps to its title cap, so re-pushing the
       same buffer every frame - even at the dt=0 freeze - never grows or drifts. */
    int len = 0;
    while (st->title[len])
        len++;
    note_set_title(&st->store, st->openNote, st->title, len);
}

void notes_layout(AppState *st, const AppCtx *ctx) {
    (void)ctx;
    rcSetStyle(st->darkMode ? rcStyleDark() : rcStyleLight());
    RC_Style s = rcGetStyle();

    rcColumn(.id = "Root", .w = "grow", .h = "grow", .bg = s.background) {
        notes_topbar();
        rcRow(.id = "Body", .w = "grow", .h = "grow") {
            notes_navrail(st);
            notes_sidebar(st);
            notes_main(st);
        }
    }
    notes_modals(st);               /* modals sit outside Root (full-window scrim) */

    rcScrollbar("NoteList");
    rcScrollbar("WriteScroll");    /* one tab renders per frame; rcScrollbar no-ops on */
    rcScrollbar("PreviewScroll");  /* the inactive id, so the two tabs scroll independently */
}

void notes_demo_chrome(AppState *st, const AppCtx *ctx) {
    if (ctx->mode != APP_DEMO || !ctx->arena)
        return;
    /* A floating perf/status HUD - demo-only, floats over the UI so it can never
       reflow the measured core. A PASSIVE readout: RC_CAPTURE_PASSTHROUGH lets clicks
       fall through (never blocking the titlebar controls). rcFormat is fine here
       (never in the bench path); the \xc2\xb7 separators are the Latin-1 middot. */
    RC_String hud = rcFormat(ctx->arena, "%.0f fps \xc2\xb7 %d notes \xc2\xb7 %d published",
                                ctx->dt > 0.0f ? 1.0f / ctx->dt : 0.0f,
                                note_count(&st->store),
                                note_published_count(&st->store));
    rcBox(.id = "demo_hud", .bg = rcAlpha(RC_BLACK, 150), .px = 10, .py = 5,
           .borderRadius = "all-full",
           .floating = { .to = RC_ATTACH_ROOT, .parent = RC_ANCHOR_BOTTOM_RIGHT,
                         .element = RC_ANCHOR_BOTTOM_RIGHT, .offset = { -16, -16 },
                         .capture = RC_CAPTURE_PASSTHROUGH }) {
        rcText(hud, .font = F_SMALL, .color = RC_WHITE);
    }
}

void notes_bench_step(AppState *st, const AppInputSink *in, int frame) {
    (void)st;   /* notes has no external/backend event: EVERY action is synthetic input */
    /* The frozen scripted scenario. EVERY user action goes through the input sink (B3),
       so the bench exercises the real hit-test / focus / caret cost. At/after
       NOTES_BENCH_WARMUP the app HOLDS - a strict no-op - so the bench harness's double-rendered
       measured frame is byte-identical.

       DETERMINISM AT THE HOLD: RayClay's caret blink + tooltip dwell read rci_input_time()
       (real time on the shipped runner), NOT the injected dt, so the frozen frame must
       carry NO focused text-input and the pointer must rest over NO tooltipped element.
       The pre-hold BLUR + OFF-CANVAS PARK below guarantees that regardless of the click
       coordinates. A mock clock pins them further, and RAYCLAY_FIXED_DT is the durable
       library fix.

       COORDS ARE A FIRST DRAFT for 1280x720; retune them to your own layout and
       budget. The y-bands ARE panel-correct: the topbar/titlebar is y 0..52, so the
       tab-bar controls (Preview, Publish) are clicked at y~78 (notes_main's tab bar,
       y 52..104) - NOT inside the titlebar. B9 = the large wrapped body: note 2 (a long
       ~281-word DRAFT) is the DEFAULT-open note (notes_seed), so the freeze is ROBUSTLY
       openNote==2 with NO coord-fragile click-select - the frame-6 move only parks the
       pointer over NoteList so the wheel scrolls it (the mock pointer starts at 0,0;
       the engine scrolls the container under the pointer). The scripted publish then flips
       note 2 draft->published, so the frozen Preview is a finished long-body post. */
    if (!in || frame >= NOTES_BENCH_WARMUP)
        return;                                        /* the HOLD */

    if (frame == 6) {
        in->move(in->ctx, 200.0f, 300.0f);            /* park the pointer over NoteList so the wheel targets it. Note 2
                                                          (the long-body DRAFT) is the DEFAULT-open note, so the freeze is
                                                          robustly openNote==2 with NO coord-fragile click-select. */
    } else if (frame >= 10 && frame < 16) {
        in->wheel(in->ctx, 0.0f, -2.0f);              /* scroll the note list (pointer is over it) */
    } else if (frame == 20) {
        in->move(in->ctx, 1150.0f, 150.0f);           /* focus the title input; FAR-RIGHT x clamps the caret */
        in->button(in->ctx, APP_MBTN_LEFT, true);      /* to end-of-text so the typed chars append cleanly */
    } else if (frame == 21) {
        in->button(in->ctx, APP_MBTN_LEFT, false);
    } else if (frame >= 24 && frame < 36) {
        static const char TYPED[] = " looks great";    /* 12 ASCII chars, one/frame -> a clean end-of-title append */
        in->text(in->ctx, (unsigned int)(unsigned char)TYPED[frame - 24]);
    } else if (frame == 40) {
        in->move(in->ctx, 1200.0f, 78.0f);            /* open the Publish modal (btn_publish, tab bar y~52..104) */
        in->button(in->ctx, APP_MBTN_LEFT, true);
    } else if (frame == 41) {
        in->button(in->ctx, APP_MBTN_LEFT, false);
    } else if (frame == 46) {
        in->move(in->ctx, 490.0f, 490.0f);            /* confirm Publish (note 2 -> published post at the freeze) */
        in->button(in->ctx, APP_MBTN_LEFT, true);
    } else if (frame == 47) {
        in->button(in->ctx, APP_MBTN_LEFT, false);
    } else if (frame == 52) {
        in->move(in->ctx, 490.0f, 78.0f);             /* switch to the Preview tab (tab bar, NOT the y=26 titlebar) */
        in->button(in->ctx, APP_MBTN_LEFT, true);
    } else if (frame == 53) {
        in->button(in->ctx, APP_MBTN_LEFT, false);
    } else if (frame == NOTES_BENCH_WARMUP - 2) {
        in->move(in->ctx, -100.0f, -100.0f);          /* blur the title + park off-canvas: press ... */
        in->button(in->ctx, APP_MBTN_LEFT, true);
    } else if (frame == NOTES_BENCH_WARMUP - 1) {
        in->button(in->ctx, APP_MBTN_LEFT, false);     /* ... release; pointer stays off-canvas into the hold */
    }
}
