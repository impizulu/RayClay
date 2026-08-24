/* ============================================================================
   demo_image.h - the gallery's in-memory fallback for the one demo PNG
   ============================================================================

   WHY THIS EXISTS. The IMAGE section loads a real file with rcLoadImage, which
   is the call an app actually makes. That path resolves against the PROCESS
   WORKING DIRECTORY, so it only finds the PNG when the gallery is launched from
   the repository root - and a developer who double-clicks the executable, or
   runs it out of a build directory, gets a warning and an empty frame instead
   of a picture. RayClay has no "where is my executable" helper to resolve that
   properly (filed), so rather than teach an idiom that only works in one
   directory, the section demonstrates BOTH public entry points:

       rcLoadImage(path)                    - decode a file  (preferred; real)
       rcLoadImageFromMemory(bytes, len)    - decode bytes you already hold

   and falls back to the second when the first cannot find the file. That is
   also the honest production pattern: an app that must not depend on its
   working directory ships its pixels inside the binary.

   WHY A BMP AND NOT A PNG. The bytes have to be a format the library's decoder
   accepts, and a 32-bit BMP is the only one that can be WRITTEN in a few lines
   with no compressor and no libc - which keeps the gallery zero-asset and
   inside the pure-RC_ contract (test/check-examples-pure-rc.sh). The same
   trick backs examples/bench/gallery's twelve procedural photos.

   The card is deliberately not a logo: a gradient with a hard border and a
   diagonal band shows filtering, tinting and non-uniform scaling far more
   legibly than artwork would, which is what the section is demonstrating.
   ========================================================================== */

#ifndef RC_EX10_DEMO_IMAGE_H
#define RC_EX10_DEMO_IMAGE_H

/* 96x96 keeps the encoded card under 37 KB, so it fits a stack buffer in the
   one-time seed path without a static allocation living for the whole run.
   32-bit rows are exactly 4-byte aligned, so BMP's row padding never applies. */
enum {
    DEMO_CARD_W   = 96,
    DEMO_CARD_H   = 96,
    DEMO_CARD_CAP = 54 + DEMO_CARD_W * DEMO_CARD_H * 4
};

static void demo__u32le(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)( v        & 0xFFu);
    p[1] = (unsigned char)((v >>  8) & 0xFFu);
    p[2] = (unsigned char)((v >> 16) & 0xFFu);
    p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

static void demo__u16le(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)( v       & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
}

/** Encode the fallback card as a 32-bit BMP into `dst`.
 *
 *  Returns the byte length written, or 0 if `cap` is too small - the caller
 *  treats 0 the same as a failed file load, so neither path can hand
 *  rcLoadImageFromMemory a short buffer.
 */
static int demo_card_bmp(unsigned char *dst, int cap) {
    const int w = DEMO_CARD_W, h = DEMO_CARD_H;
    const int pixels = w * h * 4;
    const int total  = 54 + pixels;
    int r, x, k;

    if (!dst || cap < total)
        return 0;

    for (k = 0; k < 54; k++)
        dst[k] = 0;

    /* BITMAPFILEHEADER (14 bytes), then BITMAPINFOHEADER (40). Every field the
       decoder does not need stays zero, which is what the loop above is for. */
    dst[0] = 'B';
    dst[1] = 'M';
    demo__u32le(dst + 2,  (unsigned)total);    /* bfSize                     */
    demo__u32le(dst + 10, 54u);                /* bfOffBits                  */
    demo__u32le(dst + 14, 40u);                /* biSize                     */
    demo__u32le(dst + 18, (unsigned)w);        /* biWidth                    */
    demo__u32le(dst + 22, (unsigned)h);        /* biHeight, + = bottom-up    */
    demo__u16le(dst + 26, 1u);                 /* biPlanes                   */
    demo__u16le(dst + 28, 32u);                /* biBitCount                 */
    demo__u32le(dst + 30, 0u);                 /* biCompression = BI_RGB     */
    demo__u32le(dst + 34, (unsigned)pixels);   /* biSizeImage                */

    /* Bottom-up BGRA rows: file row r is image row h-1-r. */
    for (r = 0; r < h; r++) {
        const int y = h - 1 - r;
        for (x = 0; x < w; x++) {
            unsigned char *o = dst + 54 + (r * w + x) * 4;
            int edge   = (x < 3 || y < 3 || x >= w - 3 || y >= h - 3);
            /* A 45-degree band with a 7-px period along the diagonal, a pitch
               that divides none of the scale factors this section draws at, so
               nearest-neighbour and linear filtering look visibly different. */
            int band   = (((x + y) / 7) & 1);
            int red    = 40  + (x * 150) / w;
            int green  = 90  + (y * 120) / h;
            int blue   = 200 - (x * 90)  / w;

            /* The bands differ in LUMINANCE, not just hue. The third copy in the
               section draws this same image under a 78%-alpha tint, and a purely
               chromatic pattern disappears completely under that - leaving a flat
               rectangle where the demo is trying to show a tinted picture. */
            if (band) {
                red   = (red   * 45) / 100;
                green = (green * 45) / 100;
                blue  = (blue  * 45) / 100;
            }
            if (edge)
                red = green = blue = 235;

            o[0] = (unsigned char)blue;    /* B */
            o[1] = (unsigned char)green;   /* G */
            o[2] = (unsigned char)red;     /* R */
            o[3] = 0xFFu;                  /* A - opaque */
        }
    }
    return total;
}

#endif /* RC_EX10_DEMO_IMAGE_H */
