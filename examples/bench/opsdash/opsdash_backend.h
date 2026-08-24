/*
================================================================================
    opsdash_backend.h - the ops-dashboard app's non-GUI model
================================================================================

    A header-only, raylib-style backend for the RayClay `opsdash` benchmark/showcase
    app: a fleet of 48 services with fixed metadata, plus a small live telemetry
    band (a latency sparkline ring, a rolling request counter, an incident timer).
    PURE C99 with ZERO RayClay dependency, deterministic (no wall-clock, no rand(),
    no I/O).

    THE SPLIT IS THE POINT. This app exists to be a PARTIALLY-STATIC scene, so the
    model is deliberately cut in two:

      ops_seed()  fills the INVENTORY - 48 services and every string that describes
                  them. Nothing after seeding ever writes it again.
      ops_tick()  advances ONLY the telemetry band, and ONLY when dt > 0.

    So a frame re-declares a large unchanged subtree next to a small changing one,
    which is the shape the render-only static-island experiment (L1) needs a subject
    for. With ctx->dt == 0 the telemetry stops too and the whole scene is static -
    the same binary is therefore both the experiment and its own ceiling arm, with no
    second app and no build flag to get them apart.

    Every DISPLAYED number is formatted by THIS backend into a fixed buffer, so the
    pure-RC_ GUI never calls rcFormat in its frozen core - a string is identical on
    every machine at any frame, and the arena-less bench core never dereferences a
    NULL arena.

    Usage (stb-style single implementation, in exactly one TU):
        #define OPSDASH_BACKEND_IMPLEMENTATION
        #include "opsdash_backend.h"

    This header owns ops_memzero so the pure-RC_ GUI TU stays free of <system>
    includes.

    Build target: rayclay_bench_opsdash
================================================================================
*/
#ifndef OPSDASH_BACKEND_H
#define OPSDASH_BACKEND_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef OPSDEF
#define OPSDEF static
#endif

#define OPS_SVC_COUNT    48    /* the static inventory - the large unchanged subtree */
#define OPS_GROUP_COUNT   8    /* left-nav groups; every service belongs to exactly one */
#define OPS_SPARK_COUNT  24    /* telemetry ring: the small changing subtree */
#define OPS_NAME_CAP     24
#define OPS_NUM_CAP      12

/* Health is ordered by severity so the GUI can compare, and the roll-up is a max. */
typedef enum { OPS_OK = 0, OPS_WARN, OPS_DOWN, OPS_HEALTH_COUNT } OpsHealth;

typedef struct {
    char     name[OPS_NAME_CAP];
    char     owner[OPS_NAME_CAP];
    char     rps[OPS_NUM_CAP];      /* steady-state request rate, e.g. "1.2k" */
    char     p99[OPS_NUM_CAP];      /* steady-state p99 latency, e.g. "84 ms" */
    uint8_t  group;                 /* 0..OPS_GROUP_COUNT-1 */
    uint8_t  region;                /* index into OPS_REGIONS */
    uint8_t  tier;                  /* 1..3; tier 1 is customer-facing */
    uint8_t  health;                /* OpsHealth - FIXED at seed; never ticks */
} OpsService;

/* The live band. Everything here changes on a dt > 0 tick and nothing else does. */
typedef struct {
    uint8_t  spark[OPS_SPARK_COUNT]; /* 0..100 latency samples, oldest first */
    uint32_t reqTotal;               /* monotonic request counter */
    uint32_t incidentSecs;           /* time the open incident has been running */
    float    accum;                  /* fractional-second carry, so ticks are dt-exact */
    float    pulse;                  /* 0..1 triangle wave for the incident banner */
    bool     pulseUp;
    char     reqText[OPS_NUM_CAP];   /* reqTotal, formatted */
    char     p99Text[OPS_NUM_CAP];   /* newest spark sample, formatted "NN ms" */
    char     upText[OPS_NUM_CAP];    /* incidentSecs, formatted "MM:SS" */
} OpsLive;

typedef struct {
    OpsService svc[OPS_SVC_COUNT];
    OpsLive    live;
    uint8_t    groupHealth[OPS_GROUP_COUNT]; /* max health per group; seeded, static */
    uint16_t   groupCount[OPS_GROUP_COUNT];  /* services per group; seeded, static */
    uint32_t   rng;
} OpsStore;

OPSDEF const char *const OPS_REGIONS[4] = { "us-east", "us-west", "eu-west", "ap-south" };
OPSDEF const char *const OPS_GROUPS[OPS_GROUP_COUNT] = {
    "Edge",  "Identity", "Payments", "Catalog",
    "Search", "Media",   "Analytics", "Platform",
};

/* ============================================================================
   The non-inline API is declared + defined ONLY under OPSDASH_BACKEND_IMPLEMENTATION,
   so the TU that merely INCLUDES this header (the thin main.c runner) never sees a
   static-declared-but-undefined prototype and trips -Werror=unused-function. The GUI
   TU defines IMPLEMENTATION and calls them. Warning: moving these three lines above
   the guard compiles fine and warns three times in main.c - measured, not assumed.
   ============================================================================ */
#ifdef OPSDASH_BACKEND_IMPLEMENTATION

#include <string.h>

OPSDEF void ops_memzero(void *p, size_t n);
OPSDEF void ops_seed(OpsStore *s, unsigned seed);
OPSDEF void ops_tick(OpsStore *s, float dt);

OPSDEF void ops_memzero(void *p, size_t n) { memset(p, 0, n); }

/* xorshift32 - a deterministic stream, so a re-seed reproduces the fleet exactly.
   Seed 0 would latch the generator at 0, so it is folded to a non-zero constant. */
static uint32_t ops__rand(uint32_t *st) {
    uint32_t x = *st ? *st : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *st = x;
    return x;
}

/* Append a decimal u32 at `at`, returning the new end. Caller guarantees the room:
   every call site below writes into an OPS_NUM_CAP buffer, and a u32 is at most 10
   digits, so the widest string this can produce ("4294967295") still fits. */
static size_t ops__u32(char *dst, size_t at, uint32_t v) {
    char tmp[10];
    size_t n = 0;
    do { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; } while (v);
    while (n) dst[at++] = tmp[--n];
    return at;
}

/* "1.2k" above a thousand, plain digits below - the readout a dashboard would show. */
static void ops__rate(char *dst, size_t cap, uint32_t v) {
    size_t at = 0;
    if (v >= 1000u) {
        at = ops__u32(dst, at, v / 1000u);
        dst[at++] = '.';
        dst[at++] = (char)('0' + ((v / 100u) % 10u));
        dst[at++] = 'k';
    } else {
        at = ops__u32(dst, at, v);
    }
    dst[at < cap ? at : cap - 1] = '\0';
}

static void ops__ms(char *dst, size_t cap, uint32_t v) {
    size_t at = ops__u32(dst, 0, v);
    dst[at++] = ' ';
    dst[at++] = 'm';
    dst[at++] = 's';
    dst[at < cap ? at : cap - 1] = '\0';
}

/* "MM:SS", zero-padded, so the string WIDTH is stable and the row never reflows. */
static void ops__clock(char *dst, size_t cap, uint32_t secs) {
    uint32_t m = (secs / 60u) % 100u, s = secs % 60u;
    size_t at = 0;
    dst[at++] = (char)('0' + (m / 10u));
    dst[at++] = (char)('0' + (m % 10u));
    dst[at++] = ':';
    dst[at++] = (char)('0' + (s / 10u));
    dst[at++] = (char)('0' + (s % 10u));
    dst[at < cap ? at : cap - 1] = '\0';
}

static void ops__copy(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    while (src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* Service names are built from a noun x suffix table rather than a 48-entry literal
   list: it keeps the fleet legible and lets OPS_SVC_COUNT move without a second edit. */
static const char *const OPS__NOUN[12] = {
    "gateway", "auth",    "ledger",  "catalog",
    "search",  "media",   "metrics", "scheduler",
    "session", "billing", "inventory", "notify",
};
static const char *const OPS__SUFFIX[4] = { "-api", "-worker", "-cache", "-sync" };
static const char *const OPS__OWNER[6] = {
    "team-atlas", "team-borealis", "team-cinder",
    "team-delta", "team-ember",    "team-flux",
};

OPSDEF void ops_seed(OpsStore *s, unsigned seed) {
    ops_memzero(s, sizeof *s);
    s->rng = (uint32_t)seed;

    for (int i = 0; i < OPS_SVC_COUNT; i++) {
        OpsService *sv = &s->svc[i];
        size_t at = 0;
        const char *noun = OPS__NOUN[i % 12];
        const char *sfx  = OPS__SUFFIX[(i / 12) % 4];
        while (*noun && at + 1 < OPS_NAME_CAP) sv->name[at++] = *noun++;
        while (*sfx  && at + 1 < OPS_NAME_CAP) sv->name[at++] = *sfx++;
        sv->name[at] = '\0';

        ops__copy(sv->owner, OPS_NAME_CAP, OPS__OWNER[ops__rand(&s->rng) % 6u]);
        sv->group  = (uint8_t)(i % OPS_GROUP_COUNT);
        sv->region = (uint8_t)(ops__rand(&s->rng) % 4u);
        sv->tier   = (uint8_t)(1u + (ops__rand(&s->rng) % 3u));

        /* Mostly healthy, a few warnings, two hard downs - a real fleet's shape, and
           it gives the card grid three visually distinct states to draw. */
        uint32_t r = ops__rand(&s->rng) % 100u;
        sv->health = (uint8_t)(r < 78u ? OPS_OK : (r < 96u ? OPS_WARN : OPS_DOWN));

        ops__rate(sv->rps, OPS_NUM_CAP, 40u + (ops__rand(&s->rng) % 9000u));
        ops__ms  (sv->p99, OPS_NUM_CAP, 8u + (ops__rand(&s->rng) % 240u));

        s->groupCount[sv->group]++;
        if (sv->health > s->groupHealth[sv->group])
            s->groupHealth[sv->group] = sv->health;
    }

    for (int i = 0; i < OPS_SPARK_COUNT; i++)
        s->live.spark[i] = (uint8_t)(30u + (ops__rand(&s->rng) % 40u));

    s->live.reqTotal = 1000u;
    s->live.pulseUp  = true;
    ops__rate (s->live.reqText, OPS_NUM_CAP, s->live.reqTotal);
    ops__ms   (s->live.p99Text, OPS_NUM_CAP, s->live.spark[OPS_SPARK_COUNT - 1]);
    ops__clock(s->live.upText,  OPS_NUM_CAP, 0u);
}

/* Advance ONLY the live band. dt == 0 is the freeze: it returns before touching
   anything, so a dt-0 run re-declares a byte-identical scene every frame. That is
   what makes this app its own static-island ceiling arm. */
OPSDEF void ops_tick(OpsStore *s, float dt) {
    if (dt <= 0.0f) return;

    /* The pulse is a triangle wave rather than a sine: no libm, and it is exactly
       reproducible from dt alone, which the two-frame-difference bench needs. */
    s->live.pulse += (s->live.pulseUp ? dt : -dt);
    if (s->live.pulse >= 1.0f) { s->live.pulse = 1.0f; s->live.pulseUp = false; }
    if (s->live.pulse <= 0.0f) { s->live.pulse = 0.0f; s->live.pulseUp = true;  }

    /* Roll the sparkline and the counters on a whole-second boundary, carrying the
       remainder, so the readout advances at the same rate under any frame pacing. */
    s->live.accum += dt;
    while (s->live.accum >= 1.0f) {
        s->live.accum -= 1.0f;

        for (int i = 0; i + 1 < OPS_SPARK_COUNT; i++)
            s->live.spark[i] = s->live.spark[i + 1];
        uint8_t prev = s->live.spark[OPS_SPARK_COUNT - 2];
        int32_t next = (int32_t)prev + (int32_t)(ops__rand(&s->rng) % 21u) - 10;
        if (next < 5)   next = 5;
        if (next > 100) next = 100;
        s->live.spark[OPS_SPARK_COUNT - 1] = (uint8_t)next;

        s->live.reqTotal    += 7u + (ops__rand(&s->rng) % 40u);
        s->live.incidentSecs += 1u;

        ops__rate (s->live.reqText, OPS_NUM_CAP, s->live.reqTotal);
        ops__ms   (s->live.p99Text, OPS_NUM_CAP, s->live.spark[OPS_SPARK_COUNT - 1]);
        ops__clock(s->live.upText,  OPS_NUM_CAP, s->live.incidentSecs);
    }
}

#endif /* OPSDASH_BACKEND_IMPLEMENTATION */
#endif /* OPSDASH_BACKEND_H */
