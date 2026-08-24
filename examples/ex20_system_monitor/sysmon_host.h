/* ============================================================================
 *  sysmon_host.h - the one file you replace to make ex20 a REAL system monitor
 * ============================================================================
 *
 *  THIS FILE IS THE SEAM, and it is a separate file so that the seam is
 *  impossible to miss. Everything in main.c reads a SysHost and knows nothing
 *  about where the numbers came from; swap the two functions below for /proc,
 *  sysctl or the Windows performance counters and not one line above this file
 *  changes. That separation is the point of the example - a monitor's UI and a
 *  monitor's collector have nothing to say to each other.
 *
 *  WHY THE SHIPPED VERSION IS SIMULATED. RayClay's promise is one source, every
 *  target, and the same picture on all of them. A host's core count, process
 *  table and NIC counters are the least portable numbers in computing: they do
 *  not exist at all in a browser tab, and reading them on three desktops means
 *  three collectors. A deterministic model keeps the example honest about what
 *  it is teaching (the UI) and lets the screenshots match on every platform.
 *  The readings that ARE portable - this process's own CPU and resident memory
 *  - are real, come from rcProcessCpuPercent / rcProcessMemoryBytes, and are
 *  kept visibly separate in main.c so the two are never confused.
 *
 *  DETERMINISM IS LOAD-BEARING, not decoration. The generator is a fixed-width
 *  integer PRNG seeded by the caller, so a given seed draws the same frame on
 *  Linux, macOS, Windows and wasm. That is what makes a cross-platform pixel
 *  comparison meaningful; time(NULL) or rand() would make it impossible.
 *
 *  Fixed-size storage, no allocation, no growth: the model is a plain value
 *  type that a caller can hold by value and memset. A monitor is the archetypal
 *  run-forever app, and a collector that grows a list per sample is how one
 *  dies overnight.
 *
 *  Plain statics rather than the bench suite's stb-style
 *  <app>_backend.h / IMPLEMENTATION pair: an exNN example is a single
 *  translation unit, so there is no second TU to keep the definitions out of.
 *
 *  NO libc HERE EITHER. Examples reach for RayClay and nothing else
 *  (test/check-examples-pure-rc.sh enforces it on the .c, and a model that
 *  smuggled <string.h> in through a header would be dodging the contract, not
 *  meeting it). rayclay.h supplies the fixed-width types; rcStrCopy replaces
 *  strncpy; a zero-initialised const replaces memset. Include rayclay.h first.
 * ========================================================================= */

#ifndef SYSMON_HOST_H
#define SYSMON_HOST_H

enum {
    SYS_CORES     = 8,    /* simulated logical CPUs                            */
    SYS_PROCS     = 128,  /* simulated process table - fixed, never grows      */
    SYS_NAME_MAX  = 20
};

/** How a simulated process presents in the table. Ordered by how much attention
    it deserves, so a sort on this field reads the way an operator expects. */
typedef enum SysProcState {
    SYS_SLEEPING = 0,
    SYS_RUNNING,
    SYS_STOPPED
} SysProcState;

typedef struct SysProc {
    char         name[SYS_NAME_MAX];  /**< NUL-terminated; suffixed in init    */
    int32_t      pid;
    float        cpu;      /**< percent of ONE core, like top - may exceed 100 */
    float        memMiB;
    SysProcState state;    /**< drives the row colour AND the state sort order */
} SysProc;

typedef struct SysHost {
    float    core[SYS_CORES];   /**< per-core load, percent                    */
    float    memUsedMiB;
    float    memTotalMiB;
    float    swapUsedMiB;
    float    swapTotalMiB;
    /* Rates, not running totals. A rate is bounded by construction, so none of
       these can overflow however long the app runs - which is the difference
       between a monitor that survives a week and one that does not. */
    float    netRxKiB;
    float    netTxKiB;
    float    diskRdKiB;
    float    diskWrKiB;
    SysProc  proc[SYS_PROCS];   /**< fixed-size: the model never allocates     */
    int32_t  procCount;
    uint32_t rng;               /**< xorshift state; never zero (see init)     */
} SysHost;

/* xorshift32: three shifts, no multiply, identical on every ABI we target and
   good enough for a display model. Never seeded with 0 - xorshift is stuck
   there - so the seeding path below forces a nonzero state. */
static uint32_t sysmon__rand(SysHost *h)
{
    uint32_t x = h->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    h->rng = x;
    return x;
}

/** Uniform in [0,1). Divides by 2^32 as a double so the conversion is exact and
    the result cannot land on 1.0 through rounding. */
static float sysmon__unit(SysHost *h)
{
    return (float)((double)sysmon__rand(h) / 4294967296.0);
}

/** Pull @p cur a fraction @p k toward @p target and add bounded jitter, then
    clamp to [lo,hi]. One mean-reverting step, which is what keeps a simulated
    series looking like telemetry instead of noise - and, more importantly, is
    what keeps it BOUNDED: a random walk without the pull wanders off the axis
    after a few thousand samples and the charts stop being readable. */
static float sysmon__drift(SysHost *h, float cur, float target, float k,
                           float jitter, float lo, float hi)
{
    float v = cur + (target - cur) * k + (sysmon__unit(h) - 0.5f) * jitter;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

/** Seed the model. Zeroes first, so a caller may hold SysHost by value and
    re-seed it to a known state at any time (the example's "reseed" action). */
static void sysmon_host_init(SysHost *h, uint32_t seed)
{
    static const char *stem[] = {
        "kernel_task", "init", "logind", "netd", "audiod", "indexer",
        "compositor", "renderer", "webview", "backup", "sync-agent",
        "db-writer", "cache", "scheduler", "telemetry", "updater"
    };
    static const SysHost blank;   /* static storage => zero-initialised */
    int i;

    *h = blank;                   /* struct assignment, so no memset needed */
    h->rng = seed ? seed : 0x9E3779B9u;   /* xorshift cannot start at zero */

    h->memTotalMiB  = 16384.0f;
    h->memUsedMiB   = 6200.0f;
    h->swapTotalMiB = 4096.0f;
    h->swapUsedMiB  = 320.0f;

    for (i = 0; i < SYS_CORES; i++)
        h->core[i] = 4.0f + sysmon__unit(h) * 20.0f;

    h->procCount = SYS_PROCS;
    for (i = 0; i < SYS_PROCS; i++) {
        SysProc *p = &h->proc[i];
        int n = 0;

        /* rcStrCopy rather than strncpy: examples use the RC_ API, and it always
           terminates. It leaves room for the suffix below by being handed a
           shortened size, which is cheaper than truncating afterwards. */
        rcStrCopy(p->name, stem[i & 15], SYS_NAME_MAX - 4);
        while (p->name[n]) n++;
        /* Suffix the repeats so 128 rows out of 16 stems stay distinguishable -
           a table whose rows share a label teaches nothing about sorting. */
        if (i >= 16) {
            p->name[n++] = '-';
            p->name[n++] = (char)('0' + (i / 100) % 10);
            p->name[n++] = (char)('0' + (i / 10) % 10);
            p->name[n++] = (char)('0' + i % 10);
            p->name[n]   = '\0';
        }

        p->pid    = 100 + i * 7 + (int32_t)(sysmon__unit(h) * 5.0f);
        p->cpu    = sysmon__unit(h) * sysmon__unit(h) * 60.0f;  /* long tail */
        p->memMiB = 4.0f + sysmon__unit(h) * sysmon__unit(h) * 900.0f;
        p->state  = (i % 11 == 0) ? SYS_RUNNING
                  : (i % 37 == 0) ? SYS_STOPPED : SYS_SLEEPING;
    }
}

/** Advance the model by one sample interval.

    Takes no dt on purpose. The caller samples on a fixed cadence it owns (ex20
    re-arms rcAppRequestFrameAfter every tick), so "one step" is the only unit
    this model needs, and taking a dt it would only ever be handed the same
    value for is an invitation to pass a frame delta by mistake. */
static void sysmon_host_sample(SysHost *h)
{
    float busiest = 0.0f;
    int i;

    for (i = 0; i < SYS_CORES; i++) {
        /* Each core reverts to its own plateau, so the strip shows a spread
           rather than eight copies of one number. */
        float plateau = 12.0f + (float)(i * 9);
        h->core[i] = sysmon__drift(h, h->core[i], plateau, 0.25f, 34.0f, 0.0f, 100.0f);
        if (h->core[i] > busiest) busiest = h->core[i];
    }

    h->memUsedMiB  = sysmon__drift(h, h->memUsedMiB, 7000.0f, 0.05f, 180.0f,
                                   512.0f, h->memTotalMiB);
    h->swapUsedMiB = sysmon__drift(h, h->swapUsedMiB, 380.0f, 0.04f, 24.0f,
                                   0.0f, h->swapTotalMiB);

    /* Non-zero floors: a rate that keeps bottoming out at exactly 0 reads as a
       broken widget rather than an idle link, and an idle NIC is never silent. */
    h->netRxKiB  = sysmon__drift(h, h->netRxKiB,  420.0f, 0.30f, 480.0f, 6.0f, 12000.0f);
    h->netTxKiB  = sysmon__drift(h, h->netTxKiB,   90.0f, 0.30f, 150.0f, 2.0f,  6000.0f);
    h->diskRdKiB = sysmon__drift(h, h->diskRdKiB, 150.0f, 0.35f, 600.0f, 0.0f, 24000.0f);
    h->diskWrKiB = sysmon__drift(h, h->diskWrKiB, 240.0f, 0.35f, 600.0f, 0.0f, 24000.0f);

    for (i = 0; i < h->procCount; i++) {
        SysProc *p = &h->proc[i];

        /* A stopped process burns nothing. Modelling that rather than letting
           every row jitter is what makes the state column worth sorting on. */
        if (p->state == SYS_STOPPED) {
            p->cpu = 0.0f;
            continue;
        }
        p->cpu    = sysmon__drift(h, p->cpu, busiest * 0.22f, 0.20f, 18.0f, 0.0f, 240.0f);
        p->memMiB = sysmon__drift(h, p->memMiB, p->memMiB, 0.0f, 6.0f, 2.0f, 4096.0f);
        p->state  = (p->cpu > 12.0f) ? SYS_RUNNING : SYS_SLEEPING;
    }
}

#endif /* SYSMON_HOST_H */
