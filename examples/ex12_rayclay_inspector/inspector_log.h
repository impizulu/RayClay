/*
================================================================================
    inspector_log.h - a fixed-capacity, in-memory log ring for rc_inspector
================================================================================

    One interleaved stream of two sources, both timestamped by a monotonic
    sequence number (no wall clock, so the demo behaves identically on desktop
    and web):

        [APP]  - lines the app itself writes: a click, a resize, a lifecycle
                 event. The app knows the exact widget and outcome.
        [RAY]  - RayClay's OWN diagnostics, delivered by rcSetLogSink. These
                 are the real RC_LOG_WARNING / _ERROR / _INFO messages the
                 library would otherwise print to stderr - captured here so the
                 panel shows them in the app's OWN UI, on every platform. Not a
                 web-only measure: stderr reaches the browser console there too
                 (Emscripten's printErr; see docs/web-build.md).

    Header-only: the push helpers are `static inline`, so the type and the code
    travel together in one header with no separate .c to build. The ring is a
    plain value inside AppState - no allocation, no threads, oldest line drops.
================================================================================
*/

#ifndef RC_INSPECTOR_LOG_H
#define RC_INSPECTOR_LOG_H

/* Capacity is fixed so the whole log is a flat member of AppState - no malloc,
   no lifetime to manage. 256 lines is comfortably more than fits on screen; the
   panel scrolls, and the oldest line is overwritten once the ring is full. */
#define INSP_LOG_CAP   256
#define INSP_LOG_MSG   128   /* per-line body; a longer message is truncated */

typedef enum {
    INSP_SRC_APP = 0,        /* the app narrating its own events */
    INSP_SRC_RAY = 1         /* RayClay, via rcSetLogSink       */
} InspLogSource;

/* Level mirrors RC_LogLevel (INFO=0 / WARNING=1 / ERROR=2) so a [RAY] line keeps
   the library's own severity; an [APP] note is INFO unless it reports a failure. */
typedef struct {
    unsigned char source;    /* InspLogSource */
    unsigned char level;     /* RC_LogLevel   */
    long          seq;       /* monotonic order == a synthetic timestamp */
    char          msg[INSP_LOG_MSG];
} InspLogLine;

typedef struct {
    InspLogLine line[INSP_LOG_CAP];
    long        total;       /* lines ever pushed (drives seq + the ring head) */
} InspectorLog;

/* Copy at most cap-1 bytes and always null-terminate. Written out longhand so
   the example pulls in no <string.h> and cannot walk off the end of a source
   that is not null-terminated (rcFormat returns a length, not a C string). */
static inline void insp_copy(char *dst, int cap, const char *src, int len)
{
    int n = 0;
    if (len > cap - 1)
        len = cap - 1;
    while (n < len && src[n]) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = '\0';
}

/* Push one line from an explicit (chars, length) pair - the shape rcFormat
   hands back. The ring is indexed modulo capacity, so a full ring overwrites its
   oldest entry with no shuffling. */
static inline void insp_log_push_len(InspectorLog *log, InspLogSource source,
                                     int level, const char *chars, int len)
{
    InspLogLine *l = &log->line[(int)(log->total % INSP_LOG_CAP)];
    l->source = (unsigned char)source;
    l->level  = (unsigned char)level;
    l->seq    = log->total;
    insp_copy(l->msg, INSP_LOG_MSG, chars, len);
    log->total++;
}

/* Convenience for a null-terminated C string (the rcSetLogSink `msg` is one).
   Bounds the length itself so a rogue caller cannot over-read. */
static inline void insp_log_push(InspectorLog *log, InspLogSource source,
                                 int level, const char *cstr)
{
    int len = 0;
    while (len < INSP_LOG_MSG && cstr[len])
        len++;
    insp_log_push_len(log, source, level, cstr, len);
}

/* How many lines are currently live in the ring (<= capacity). */
static inline int insp_log_count(const InspectorLog *log)
{
    return log->total < INSP_LOG_CAP ? (int)log->total : INSP_LOG_CAP;
}

/* The i-th live line, 0 == oldest still held. Maps the logical index onto the
   physical ring slot, accounting for wrap once total has passed capacity. */
static inline const InspLogLine *insp_log_at(const InspectorLog *log, int i)
{
    long first = log->total < INSP_LOG_CAP ? 0 : log->total - INSP_LOG_CAP;
    return &log->line[(int)((first + i) % INSP_LOG_CAP)];
}

#endif /* RC_INSPECTOR_LOG_H */
