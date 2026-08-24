/*
================================================================================
    gallery_backend.h - the image-gallery app's non-GUI model + asset generator
================================================================================

    A header-only, raylib-style backend for the RayClay `gallery` benchmark/showcase
    app: a curated set of 12 "photos", each PROCEDURALLY GENERATED as a real,
    valid 32-bit BMP in memory (no asset files - zero-asset), plus per-image
    metadata, a search filter, and the precomputed display strings. PURE C99 with
    ZERO RayClay dependency, deterministic (no wall-clock, no rand(), no I/O).

    The images are hand-encoded BMPs so the GUI can decode them through the PUBLIC
    rcLoadImageFromMemory path - exercising the real stb_image decode + GPU upload,
    the gallery's distinct cost centre. 32-bit BGRA is used deliberately: w*4 is
    always 4-byte aligned, so BMP row padding is structurally impossible (the whole
    stride/overflow bug class is designed out). Every header field is written as
    explicit little-endian bytes (a struct overlay would pad BITMAPFILEHEADER's 14
    bytes to 16). Alpha is 255 on every pixel so the decode is unambiguously opaque.

    Every DISPLAYED number/label is formatted by THIS backend into a fixed buffer
    (dimensions "96 x 72", the result count), so the pure-RC_ GUI never calls
    rcFormat in its frozen core - a string is identical on every machine at any
    frame. Nothing here reads a clock; the app freezes to a byte-identical hold.

    Usage (stb-style single implementation, in exactly one TU):
        #define GALLERY_BACKEND_IMPLEMENTATION
        #include "gallery_backend.h"

    This header owns gallery_memzero so the pure-RC_ GUI TU stays free of <system>
    includes. Image handles (RC_Image) are a RayClay type and live in the app's
    AppState, not here - this backend only produces the encoded BYTES.

    Build target: rayclay_bench_gallery
================================================================================
*/
#ifndef GALLERY_BACKEND_H
#define GALLERY_BACKEND_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef GALDEF
#define GALDEF static
#endif

#define GAL_IMG_COUNT    12               /* MUST stay <= 16: the capture backend's image-handle pool
                                             is a process-global 16-slot static that never reclaims. */
#define GAL_IMG_MAXW     96
#define GAL_IMG_MAXH     96
#define GAL_BMP_CAP      (54 + GAL_IMG_MAXW * 4 * GAL_IMG_MAXH)  /* worst-case encoded size, 36918 */
#define GAL_TITLE_CAP    24
#define GAL_DIM_CAP      16               /* "96 x 72"                */
#define GAL_TAG_CAP      16               /* one tag label            */
#define GAL_COUNT_CAP    16               /* "12 photos" / "3 results" */
#define GAL_CAPTION_CAP  257              /* 256-byte paste cap + NUL  */

/* Per-image record. All strings are precomputed fixed-width at seed. */
typedef struct {
    char     title[GAL_TITLE_CAP];        /* "Sunset Ridge"                          */
    char     dim[GAL_DIM_CAP];            /* precomputed "96 x 72"                   */
    char     tag[GAL_TAG_CAP];            /* one filter/display tag                  */
    char     caption[GAL_CAPTION_CAP];    /* default caption (the editable seed text) */
    int32_t  w, h;                        /* image pixel dimensions                  */
    uint8_t  pattern;                     /* which procedural pattern (0..GAL_PAT_N-1) */
    uint32_t hueA, hueB;                  /* two BGRA endpoints the pattern blends    */
} GalImage;

/* The whole gallery model - a flat, memset-able POD (the app embeds it by value). */
typedef struct {
    GalImage img[GAL_IMG_COUNT];
    int32_t  visible[GAL_IMG_COUNT];      /* compacted indices matching the search   */
    int32_t  visibleCount;
    char     countStr[GAL_COUNT_CAP];     /* "12 photos" / "3 results" (recomputed on filter) */
} GalStore;

/* ============================================================================
   The non-inline API is declared + defined ONLY under GALLERY_BACKEND_IMPLEMENTATION
   (as in trader_backend.h), so a TU that includes this header WITHOUT defining it
   (the thin main.c runner) never sees a static-declared-but-undefined prototype and
   trips -Werror=unused-function. The GUI TU defines IMPLEMENTATION and calls them.
   ============================================================================ */
#ifdef GALLERY_BACKEND_IMPLEMENTATION

/* memzero for the pure-RC_ GUI TU (keeps <string.h> out of the GUI). */
GALDEF void gallery_memzero(void *p, size_t n);
/* Build the store: metadata + precomputed strings. Deterministic; `seed` is accepted
   for contract symmetry but the curated content is fixed. */
GALDEF void gallery_backend_seed(GalStore *s, unsigned seed);
/* Encode image `i` as a 32-bit BGRA BMP into `dst`. Returns the byte length, or 0 on
   any bounds/argument failure (the caller treats 0 / a NULL decode as "degrade to a
   placeholder", never a crash). */
GALDEF size_t gallery_encode_bmp(const GalStore *s, int i, unsigned char *dst, size_t cap);
/* Recompute the visible-index list for a search string (case-insensitive substring over
   the title; empty search matches all) + the "n photos/results" count string. */
GALDEF void gallery_filter(GalStore *s, const char *search);
/* Copy image `i`'s default caption into `dst` (bounded). Lets the pure-RC_ GUI TU load
   a caption without a libc string call. Out-of-range `i` writes an empty string. */
GALDEF void gallery_load_caption(const GalStore *s, int i, char *dst, int cap);

/* ------------------------------------------------------------------ utilities */

GALDEF void gallery_memzero(void *p, size_t n) {
    unsigned char *b = (unsigned char *)p;
    for (size_t i = 0; i < n; i++) b[i] = 0;
}

/* Bounded ASCII copy (dst always NUL-terminated); no libc. */
static void gal__str_copy(char *dst, const char *src, int cap) {
    int i = 0;
    if (cap <= 0) return;
    for (; src && src[i] && i < cap - 1; i++) dst[i] = src[i];
    dst[i] = '\0';
}

/* Append an unsigned int as decimal to dst[*pos], bounded. */
static void gal__put_uint(char *dst, int *pos, int cap, unsigned v) {
    char tmp[12];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 11);
    while (n > 0 && *pos < cap - 1) dst[(*pos)++] = tmp[--n];
    dst[*pos] = '\0';
}

/* "96 x 72" into a fixed buffer. */
static void gal__dim_str(char *dst, int cap, int w, int h) {
    int pos = 0;
    gal__put_uint(dst, &pos, cap, (unsigned)(w < 0 ? 0 : w));
    if (pos < cap - 3) { dst[pos++] = ' '; dst[pos++] = 'x'; dst[pos++] = ' '; dst[pos] = '\0'; }
    gal__put_uint(dst, &pos, cap, (unsigned)(h < 0 ? 0 : h));
}

static char gal__lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

/* Case-insensitive: does `hay` contain `needle`? Empty needle matches. No libc. */
static bool gal__contains_ci(const char *hay, const char *needle) {
    if (!needle || !needle[0]) return true;
    for (int i = 0; hay[i]; i++) {
        int j = 0;
        while (needle[j] && hay[i + j] &&
               gal__lower(hay[i + j]) == gal__lower(needle[j])) j++;
        if (!needle[j]) return true;
    }
    return false;
}

/* ------------------------------------------------------ little-endian BMP bytes */

static void gal__u16le(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
}
static void gal__u32le(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
    p[2] = (unsigned char)((v >> 16) & 0xFFu);
    p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

/* Blend two BGRA endpoints by t in [0,255] (per-channel, alpha forced opaque). */
static uint32_t gal__lerp_bgra(uint32_t a, uint32_t b, int t) {
    if (t < 0) t = 0; else if (t > 255) t = 255;
    uint32_t out = 0xFFu << 24;                       /* opaque alpha */
    for (int sh = 0; sh <= 16; sh += 8) {
        int ca = (int)((a >> sh) & 0xFFu);
        int cb = (int)((b >> sh) & 0xFFu);
        int c  = ca + (cb - ca) * t / 255;
        out |= (uint32_t)(c & 0xFF) << sh;
    }
    return out;
}

/* The procedural pattern: BGRA at pixel (x,y) of a w*h image. Deterministic. Every
   divisor is floored to >= 1 (a 1px image would otherwise divide by zero). */
static uint32_t gal__pixel(const GalImage *m, int x, int y, int w, int h) {
    int wm = w > 1 ? w - 1 : 1;
    int hm = h > 1 ? h - 1 : 1;
    int cx = w / 2, cy = h / 2;
    int t;
    switch (m->pattern % 8) {
    case 0:  t = y * 255 / hm; break;                         /* vertical gradient (asymmetric) */
    case 1:  t = x * 255 / wm; break;                         /* horizontal gradient            */
    case 2:  t = (x + y) * 255 / (wm + hm); break;            /* diagonal gradient              */
    case 3:  t = ((x / 12 + y / 12) & 1) ? 230 : 25; break;   /* checkerboard                   */
    case 4: {                                                 /* concentric rings               */
        int dx = x - cx, dy = y - cy;
        int d = dx * dx + dy * dy;
        t = ((d / 90) & 1) ? 40 : 215;
        break;
    }
    case 5:  t = ((x / 8) & 1) ? 210 : 45; break;             /* vertical stripes               */
    case 6: {                                                 /* radial (bright centre)         */
        int dx = x - cx, dy = y - cy;
        int r2 = dx * dx + dy * dy;
        int rmax = cx * cx + cy * cy; if (rmax < 1) rmax = 1;
        t = 255 - r2 * 255 / rmax;
        break;
    }
    default: t = (y > h / 2) ? 60 : 200; break;               /* two-tone top/bottom split      */
    }
    return gal__lerp_bgra(m->hueA, m->hueB, t);
}

GALDEF size_t gallery_encode_bmp(const GalStore *s, int i, unsigned char *dst, size_t cap) {
    if (!s || !dst || i < 0 || i >= GAL_IMG_COUNT) return 0;
    const GalImage *m = &s->img[i];
    int w = (int)m->w, h = (int)m->h;
    if (w <= 0 || h <= 0 || w > GAL_IMG_MAXW || h > GAL_IMG_MAXH) return 0;

    size_t pixels = (size_t)w * (size_t)h * 4u;               /* 32-bit: no row padding, ever */
    size_t total  = 54u + pixels;
    if (total > cap) return 0;

    for (size_t k = 0; k < 54; k++) dst[k] = 0;               /* zero both headers first */

    /* BITMAPFILEHEADER (14 bytes) */
    dst[0] = 'B'; dst[1] = 'M';
    gal__u32le(dst + 2,  (unsigned)total);                    /* bfSize                 */
    gal__u32le(dst + 10, 54u);                                /* bfOffBits              */
    /* BITMAPINFOHEADER (40 bytes) */
    gal__u32le(dst + 14, 40u);                                /* biSize                 */
    gal__u32le(dst + 18, (unsigned)w);                        /* biWidth                */
    gal__u32le(dst + 22, (unsigned)h);                        /* biHeight (+ = bottom-up) */
    gal__u16le(dst + 26, 1u);                                 /* biPlanes               */
    gal__u16le(dst + 28, 32u);                                /* biBitCount             */
    gal__u32le(dst + 30, 0u);                                 /* biCompression = BI_RGB */
    gal__u32le(dst + 34, (unsigned)pixels);                   /* biSizeImage            */
    /* 38..53 (ppm x/y, clrUsed, clrImportant) stay zero. */

    /* Pixels: bottom-up BGRA rows. File row r (0 = bottom) is image y = h-1-r. */
    unsigned char *px = dst + 54;
    for (int r = 0; r < h; r++) {
        int y = h - 1 - r;
        for (int x = 0; x < w; x++) {
            uint32_t c = gal__pixel(m, x, y, w, h);
            unsigned char *o = px + ((size_t)r * (size_t)w + (size_t)x) * 4u;
            o[0] = (unsigned char)(c & 0xFFu);                /* B */
            o[1] = (unsigned char)((c >> 8) & 0xFFu);         /* G */
            o[2] = (unsigned char)((c >> 16) & 0xFFu);        /* R */
            o[3] = 0xFFu;                                     /* A - opaque */
        }
    }
    return total;
}

/* ------------------------------------------------------------ seed + filter */

/* Curated content, indexed by image. Kept fixed so the app is byte-deterministic. */
typedef struct { const char *title, *tag, *caption; int w, h; uint8_t pat; uint32_t a, b; } GalSeed;

static const GalSeed GAL__SEEDS[GAL_IMG_COUNT] = {
    /* title            tag           caption                                w   h  pat  hueA(BGRA)  hueB(BGRA)  */
    { "Sunset Ridge",  "landscape",  "Golden light over the far ridge.",    96, 64, 0, 0xFF3A6EFFu, 0xFF3AC8FFu },
    { "Ocean Drift",   "landscape",  "",                                    96, 64, 6, 0xFFC8A050u, 0xFF603010u },
    { "City Lights",   "night",      "Downtown after the rain.",            96, 64, 5, 0xFF202020u, 0xFFF0C020u },
    { "Forest Path",   "nature",     "",                                    64, 96, 2, 0xFF204028u, 0xFF80C060u },
    { "Desert Dune",   "landscape",  "Endless ridgelines of sand.",         96, 64, 0, 0xFF3060C0u, 0xFF80D0F0u },
    { "Aurora Sky",    "night",      "",                                    96, 64, 6, 0xFF402020u, 0xFF80F040u },
    { "Still Water",   "nature",     "A quiet morning reflection.",         80, 80, 1, 0xFFB08040u, 0xFFF0E0C0u },
    { "Autumn Leaf",   "macro",      "",                                    80, 80, 3, 0xFF2040A0u, 0xFF4080E0u },
    { "Blue Hour",     "landscape",  "The last blue before dark.",          96, 64, 0, 0xFF603010u, 0xFFF0A040u },
    { "Marble",        "abstract",   "",                                    80, 80, 4, 0xFF303030u, 0xFFF0F0F0u },
    { "Neon Grid",     "abstract",   "Synth grid, endless horizon.",        96, 64, 5, 0xFFF040A0u, 0xFF40F0F0u },
    { "Soft Focus",    "macro",      "",                                    64, 96, 7, 0xFFA0C0E0u, 0xFF204060u },
};

GALDEF void gallery_backend_seed(GalStore *s, unsigned seed) {
    (void)seed;                                               /* content is curated + fixed */
    gallery_memzero(s, sizeof *s);
    for (int i = 0; i < GAL_IMG_COUNT; i++) {
        const GalSeed *g = &GAL__SEEDS[i];
        GalImage *m = &s->img[i];
        gal__str_copy(m->title, g->title, GAL_TITLE_CAP);
        gal__str_copy(m->tag, g->tag, GAL_TAG_CAP);
        gal__str_copy(m->caption, g->caption, GAL_CAPTION_CAP);
        m->w = g->w; m->h = g->h; m->pattern = g->pat;
        m->hueA = g->a; m->hueB = g->b;
        gal__dim_str(m->dim, GAL_DIM_CAP, g->w, g->h);
    }
    gallery_filter(s, "");                                    /* all visible initially */
}

GALDEF void gallery_filter(GalStore *s, const char *search) {
    s->visibleCount = 0;
    for (int i = 0; i < GAL_IMG_COUNT; i++) {
        if (gal__contains_ci(s->img[i].title, search))
            s->visible[s->visibleCount++] = i;
    }
    /* "<n> photos" (no search) or "<n> results" (filtering). */
    int pos = 0;
    gal__put_uint(s->countStr, &pos, GAL_COUNT_CAP, (unsigned)s->visibleCount);
    const char *word = (search && search[0]) ? " results" : " photos";
    for (int k = 0; word[k] && pos < GAL_COUNT_CAP - 1; k++) s->countStr[pos++] = word[k];
    s->countStr[pos] = '\0';
}

GALDEF void gallery_load_caption(const GalStore *s, int i, char *dst, int cap) {
    if (!dst || cap <= 0) return;
    if (!s || i < 0 || i >= GAL_IMG_COUNT) { dst[0] = '\0'; return; }
    gal__str_copy(dst, s->img[i].caption, cap);
}

#endif /* GALLERY_BACKEND_IMPLEMENTATION */
#endif /* GALLERY_BACKEND_H */
