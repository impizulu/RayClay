/*
================================================================================
    messenger_backend.h - the messenger app's non-GUI model + logic
================================================================================

    A header-only, raylib-style backend for the RayClay `messenger` benchmark/
    showcase app: the conversation + message model, a seeded PRNG, a fixed-step
    clock, and the incoming-message schedule. PURE C99 with ZERO RayClay
    dependency, so it is reusable on its own and compiles today - before the
    library's deterministic-clock seam lands (it takes a plain `dt`).

    DETERMINISM (the whole point of the benchmark half): the model is seeded once
    (msg_store_seed), advanced by an injected `dt` (msg_store_step), and reads no
    wall-clock, no rand(), no file/network I/O. In demo mode the GUI feeds it the
    real per-frame dt; in bench mode it feeds a fixed dt (and 0 to freeze). Every
    timestamp is a FIXED-length "HH:MM" precomputed at seed time, so a formatted
    string's byte length is machine-invariant (a reflow would be a false Ir step).

    Usage (stb-style single implementation, in exactly one TU):
        #define MESSENGER_BACKEND_IMPLEMENTATION
        #include "messenger_backend.h"

    This header owns the small raw-memory helpers (msg_memzero) so the GUI TU that
    consumes it can stay free of <system> includes (the pure-RC_ examples contract).

    Build target: rayclay_bench_messenger (shared with messenger_app.c / main.c)
================================================================================
*/
#ifndef MESSENGER_BACKEND_H
#define MESSENGER_BACKEND_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* MSGDEF: `static` by default (header-only, one TU). Kept as a seam in case a
   future build wants external linkage. */
#ifndef MSGDEF
#define MSGDEF static
#endif

/* Fixed capacities - the store is a flat value type (memset-seedable, zero
   malloc), sized to honour the RAM tenet (~35 KB). Seed uses a subset. */
#define MSG_MAX_CONVERSATIONS   16
#define MSG_MAX_MSGS_PER_CONV   64
#define MSG_TEXT_POOL_BYTES   4096
#define MSG_NAME_CAP            24
#define MSG_EPOCH_SECONDS    43200   /* the frozen "now" == 12:00:00, no calendar */

typedef enum { MSG_KIND_TEXT = 0, MSG_KIND_SYSTEM } MsgKind;
typedef enum { MSG_DIR_INCOMING = 0, MSG_DIR_OUTGOING } MsgDir;

/* One message. `text` points either into the immutable corpus (seed messages) or
   into the store's textPool (runtime-sent). `ts` is precomputed "HH:MM". */
typedef struct {
    const char *text;
    uint16_t    textLen;
    uint16_t    author;      /* index into the conversation's participant naming */
    uint32_t    clock;       /* seconds-since-midnight this message carries       */
    char        ts[6];       /* "HH:MM" + NUL, ALWAYS 5 glyphs (length-invariant) */
    uint8_t     kind;        /* MsgKind  */
    uint8_t     dir;         /* MsgDir   */
} MsgMessage;

/* One conversation: a contiguous message run (O(1) index, cache-friendly). */
typedef struct {
    char       name[MSG_NAME_CAP];
    char       preview[40];      /* last-message preview for the sidebar row */
    char       badge[4];         /* unread badge text, "" / "1".."9" / "9+"  */
    char       initials[3];      /* procedural avatar glyphs (Latin-1)       */
    uint32_t   accent;           /* avatar tint (0xRRGGBB), seeded per convo  */
    uint16_t   unread;
    uint32_t   lastClock;        /* for the sidebar time column               */
    char       lastTs[6];        /* precomputed "HH:MM" of the last message   */
    int32_t    count;            /* messages in `items`                       */
    MsgMessage items[MSG_MAX_MSGS_PER_CONV];
} MsgConversation;

typedef struct {
    MsgConversation conv[MSG_MAX_CONVERSATIONS];
    int32_t         convCount;
    uint32_t        nowClock;              /* current sim time, seconds-since-midnight */
    float           accum;                 /* dt integrator -> whole-second ticks       */
    char            textPool[MSG_TEXT_POOL_BYTES];
    int32_t         textUsed;
    uint32_t        rng;                   /* xorshift32 state (seed-time only)         */
} MsgStore;

/* -- queries (const, pure, per-frame; always available, header-inline) ------- */

/* Case-insensitive ASCII substring test - the sidebar search filter. An empty or
   NULL query matches everything. Self-contained (no libc) so the pure-RC GUI can
   call it directly, like the accessors below. */
static inline bool msg_name_matches(const char *name, const char *query) {
    if (!query || !query[0])
        return true;
    for (const char *base = name; *base; base++) {
        const char *a = base, *b = query;
        while (*a && *b) {
            char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb)
                break;
            a++;
            b++;
        }
        if (!*b)
            return true;
    }
    return false;
}

static inline int msg_conversation_count(const MsgStore *st) { return st->convCount; }
static inline const MsgConversation *msg_conversation_at(const MsgStore *st, int i) {
    return (i >= 0 && i < st->convCount) ? &st->conv[i] : NULL;
}
static inline int msg_thread_count(const MsgStore *st, int convo) {
    return (convo >= 0 && convo < st->convCount) ? st->conv[convo].count : 0;
}
static inline const MsgMessage *msg_thread_at(const MsgStore *st, int convo, int i) {
    if (convo < 0 || convo >= st->convCount)
        return NULL;
    const MsgConversation *c = &st->conv[convo];
    return (i >= 0 && i < c->count) ? &c->items[i] : NULL;
}
static inline int msg_unread_total(const MsgStore *st) {
    int total = 0;
    for (int i = 0; i < st->convCount; i++)
        total += st->conv[i].unread;
    return total;
}

/* ========================================================================== */
#ifdef MESSENGER_BACKEND_IMPLEMENTATION

#include <string.h>

/* The non-inline API is declared AND defined only under IMPLEMENTATION, so a TU
   that needs just the types + queries (the demo's main.c, the bench harness)
   never gets a bare `static` prototype (which -Werror=unused-function rejects).
   The one TU that #defines MESSENGER_BACKEND_IMPLEMENTATION (messenger_app.c) gets
   the full API. Forward decls first, for the mutual reference in step(). */
MSGDEF void msg_memzero(void *p, size_t n);
MSGDEF void msg_store_seed(MsgStore *st, unsigned seed);
MSGDEF void msg_store_step(MsgStore *st, float dt);   /* dt <= 0 => no-op (freeze) */
MSGDEF int  msg_send_text(MsgStore *st, int convo, const char *text, int len);
MSGDEF void msg_mark_read(MsgStore *st, int convo);
MSGDEF void msg_inject_incoming(MsgStore *st, int convo);
MSGDEF int  msg_format_clock(uint32_t secondsOfDay, char *out, int cap);  /* -> "HH:MM" */

MSGDEF void msg_memzero(void *p, size_t n) { memset(p, 0, n); }

/* xorshift32 - a fast, deterministic PRNG used ONLY at seed time (never on a
   measured frame), so per-frame Ir carries zero RNG cost. */
static uint32_t msg__rng_next(MsgStore *st) {
    uint32_t x = st->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    st->rng = x;
    return x;
}
static uint32_t msg__rng_range(MsgStore *st, uint32_t lo, uint32_t hi) {
    return lo + msg__rng_next(st) % (hi - lo + 1u);
}

MSGDEF int msg_format_clock(uint32_t s, char *out, int cap) {
    if (cap < 6)
        return 0;
    unsigned hh = (s / 3600u) % 24u, mm = (s / 60u) % 60u;
    out[0] = (char)('0' + hh / 10u);
    out[1] = (char)('0' + hh % 10u);
    out[2] = ':';
    out[3] = (char)('0' + mm / 10u);
    out[4] = (char)('0' + mm % 10u);
    out[5] = '\0';
    return 5;
}

/* Seed corpus - immutable, ASCII/Latin-1 only (the bundled face is Latin-1; a
   non-Latin-1 byte renders blank AND shifts run widths, breaking determinism). */
static const char *const MSG__NAMES[] = {
    "Ada Lovelace", "Grace Hopper", "Alan Turing", "Katherine J.",
    "Linus T.", "Margaret H.", "Dennis R.", "Barbara L.",
    "Ken Thompson", "Radia P.", "Guido v. R.", "Anita B.",
    "Design Team", "RayClay CI", "Release Bot", "Mom",
};
static const char *const MSG__INITIALS[] = {
    "AL", "GH", "AT", "KJ", "LT", "MH", "DR", "BL",
    "KT", "RP", "GR", "AB", "DT", "CI", "RB", "MO",
};
static const uint32_t MSG__ACCENTS[] = {
    0x6366f1, 0x10b981, 0xf59e0b, 0xec4899, 0x8b5cf6, 0x06b6d4, 0xef4444, 0x84cc16,
    0x3b82f6, 0xf97316, 0x14b8a6, 0xa855f7, 0x64748b, 0x22c55e, 0x0ea5e9, 0xe11d48,
};
static const char *const MSG__PREVIEWS[] = {
    "See you at the standup!", "The build is green.", "Ship it when ready.",
    "Nice work on the demo.", "Can you review the PR?", "Lunch at noon?",
    "Pushed the fix.", "Thanks, that helps.", "On my way.", "Let's sync later.",
    "Deploy succeeded.", "Call me back.",
};
/* Thread bodies of varied length so wrap + many-small-runs are exercised. */
static const char *const MSG__BODIES[] = {
    "Hey! Are you free to look at the layout benchmark this afternoon?",
    "Sure - give me ten minutes to finish this pass.",
    "The messenger renders every widget under a real load now.",
    "Nice.",
    "One source builds the desktop window and the web canvas.",
    "How is the frame time holding up at 60 conversations?",
    "Flat. The scissor stack stays shallow with the virtualized list.",
    "Great, that was the risk.",
    "I pushed the seeded scenario so the trend is reproducible.",
    "Let me pull it.",
    "Remember timestamps are a frozen epoch - no wall clock.",
    "Right, otherwise the diff would drift.",
    "Sending you the numbers.",
    "Got them. The Ir is byte-identical across two runs.",
    "That is exactly what we wanted.",
    "Standardised and shippable at the same time.",
};

static void msg__set_badge(MsgConversation *c) {
    if (c->unread == 0)
        c->badge[0] = '\0';
    else if (c->unread < 10) {
        c->badge[0] = (char)('0' + c->unread);
        c->badge[1] = '\0';
    } else {
        c->badge[0] = '9'; c->badge[1] = '+'; c->badge[2] = '\0';
    }
}

/* Copy a fixed-cap, NUL-terminated preview of the last message into the sidebar row. */
static void msg__set_preview(MsgConversation *c, const char *text) {
    size_t pl = strlen(text);
    if (pl >= sizeof c->preview)
        pl = sizeof c->preview - 1;
    memcpy(c->preview, text, pl);
    c->preview[pl] = '\0';
}

/* Append a message to a conversation, clamped to capacity. Returns its index or
   -1 if the conversation is full. `clock` is seconds-since-midnight. */
static int msg__append(MsgConversation *c, const char *text, int len,
                       MsgDir dir, uint32_t clock) {
    if (c->count >= MSG_MAX_MSGS_PER_CONV)
        return -1;
    MsgMessage *m = &c->items[c->count];
    m->text    = text;
    m->textLen = (uint16_t)len;
    m->author  = 0;
    m->clock   = clock;
    m->kind    = MSG_KIND_TEXT;
    m->dir     = (uint8_t)dir;
    msg_format_clock(clock, m->ts, sizeof m->ts);
    c->lastClock = clock;
    memcpy(c->lastTs, m->ts, sizeof c->lastTs);
    return c->count++;
}

MSGDEF void msg_store_seed(MsgStore *st, unsigned seed) {
    msg_memzero(st, sizeof *st);
    st->rng      = seed ? seed : 0x9e3779b9u;
    st->nowClock = MSG_EPOCH_SECONDS;
    st->accum    = 0.0f;

    const int nNames  = (int)(sizeof MSG__NAMES  / sizeof *MSG__NAMES);
    const int nBodies = (int)(sizeof MSG__BODIES / sizeof *MSG__BODIES);
    const int nPrev   = (int)(sizeof MSG__PREVIEWS / sizeof *MSG__PREVIEWS);

    st->convCount = 12;
    for (int i = 0; i < st->convCount; i++) {
        MsgConversation *c = &st->conv[i];
        int who = i % nNames;
        /* fixed-cap copies (no <string.h> reach from the caller; done here) */
        size_t nl = strlen(MSG__NAMES[who]);
        if (nl >= sizeof c->name) nl = sizeof c->name - 1;
        memcpy(c->name, MSG__NAMES[who], nl);
        c->name[nl] = '\0';
        c->initials[0] = MSG__INITIALS[who][0];
        c->initials[1] = MSG__INITIALS[who][1];
        c->initials[2] = '\0';
        c->accent = MSG__ACCENTS[who];

        msg__set_preview(c, MSG__PREVIEWS[i % nPrev]);

        c->unread = (uint16_t)((i == 0) ? 0 : msg__rng_range(st, 0, (i % 3 == 0) ? 12 : 4));
        msg__set_badge(c);

        /* The first conversation is the open thread with a full backlog. */
        int backlog = (i == 0) ? 48 : (int)msg__rng_range(st, 2, 6);
        if (backlog > MSG_MAX_MSGS_PER_CONV) backlog = MSG_MAX_MSGS_PER_CONV;
        uint32_t clock = MSG_EPOCH_SECONDS - (uint32_t)backlog * 73u;  /* spaced ~1.2 min */
        for (int m = 0; m < backlog; m++) {
            const char *body = MSG__BODIES[(i * 7 + m) % nBodies];
            MsgDir dir = (m & 1) ? MSG_DIR_OUTGOING : MSG_DIR_INCOMING;
            msg__append(c, body, (int)strlen(body), dir, clock);
            clock += 73u;
        }
    }
    /* No time-scheduled incoming: the bench's deterministic incoming is a direct,
       frame-precise msg_inject_incoming from messenger_bench_step (a network event).
       The demo simply showcases sending via the composer. */
}

MSGDEF void msg_store_step(MsgStore *st, float dt) {
    if (dt <= 0.0f)                      /* the freeze: a strict no-op */
        return;
    st->accum += dt;
    while (st->accum >= 1.0f) {          /* advance whole sim-seconds deterministically */
        st->accum -= 1.0f;
        st->nowClock++;                 /* drives the timestamp on any message sent now */
    }
}

MSGDEF int msg_send_text(MsgStore *st, int convo, const char *text, int len) {
    if (convo < 0 || convo >= st->convCount || len <= 0)
        return -1;
    if (st->textUsed + len + 1 > MSG_TEXT_POOL_BYTES)
        return -1;
    char *dst = &st->textPool[st->textUsed];
    memcpy(dst, text, (size_t)len);
    dst[len] = '\0';
    st->textUsed += len + 1;
    int idx = msg__append(&st->conv[convo], dst, len, MSG_DIR_OUTGOING, st->nowClock);
    msg__set_preview(&st->conv[convo], dst);   /* keep the sidebar preview == last message */
    return idx;
}

MSGDEF void msg_mark_read(MsgStore *st, int convo) {
    if (convo < 0 || convo >= st->convCount)
        return;
    st->conv[convo].unread = 0;
    msg__set_badge(&st->conv[convo]);
}

MSGDEF void msg_inject_incoming(MsgStore *st, int convo) {
    if (convo < 0 || convo >= st->convCount)
        return;
    static const char INCOMING[] = "Just saw the trend row land - looks great.";
    MsgConversation *c = &st->conv[convo];
    msg__append(c, INCOMING, (int)(sizeof INCOMING - 1), MSG_DIR_INCOMING, st->nowClock);
    msg__set_preview(c, INCOMING);
    c->unread++;
    msg__set_badge(c);
}

#endif /* MESSENGER_BACKEND_IMPLEMENTATION */
#endif /* MESSENGER_BACKEND_H */
