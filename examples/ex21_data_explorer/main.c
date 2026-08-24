/* ============================================================================
 *  ex21 - Data Explorer
 * ============================================================================
 *
 *  An analytics workbench over a 12,000-row exoplanet catalogue: filter it from
 *  the rail on the left, and a year chart, a radius histogram, a period/radius
 *  scatter, a summary and a sortable 12,000-row table all follow. Desktop and
 *  web from this one source, no #ifdef, no asset files.
 *
 *  WHY THIS APP EXISTS, beyond looking like a dashboard. Every serious data app
 *  has the same shape - ONE dataset, several views of it, and controls that
 *  move all of them at once - and three things about that shape have no worked
 *  example anywhere else in this repo:
 *
 *  1. ONE DATASET, MANY VIEWS, THROUGH AN INDEX ARRAY. Filtering and sorting
 *     never touch, copy or reorder a single CatPlanet. They build and permute
 *     an array of int32_t indices, and the table, all three charts and the
 *     summary read that one selection. See `View` and view_rebuild.
 *
 *  2. DERIVED DATA IS CACHED, AND THE APP SHOWS YOU THAT IT IS. Filtering is
 *     O(rows); drawing is O(rows you can see). Recomputing per frame would tie
 *     the two together for no reason, so the aggregates are rebuilt only when a
 *     control moves - and the status bar prints the rebuild count, so the claim
 *     is visible rather than asserted. Drag the window around and watch it not
 *     move. See view_rebuild and the note above table_body.
 *
 *  3. A SCATTER PLOT IS NOT FREE, AND THE FIX IS TO PLOT A SAMPLE. A line of
 *     12,000 points is 12,000 vertices; 12,000 scatter markers are 12,000 small
 *     discs, and each disc is a fan of triangles. That overruns the renderer's
 *     vertex pool, which is a budget, not a bug. plot_scatter draws a strided
 *     sample and says so on the chart - the shape of a cloud is a property of
 *     its density, so a uniform sample of it looks the same.
 *
 *  WHAT IT DELIBERATELY DOES NOT DO. It uses the bundled titlebar rather than a
 *  custom one (ex20 is the branded-window example) and it never animates, so it
 *  parks between interactions: no timer, no easing, nothing asking for frames.
 *  An analytics tool is idle almost all of the time, and that should cost
 *  nothing.
 *
 *  THE DATA IS SYNTHETIC AND THE FILE THAT MAKES IT IS THE SEAM. catalog.h
 *  generates a deterministic catalogue shaped after the real exoplanet record;
 *  swap it for a CSV reader or a database query and nothing below changes.
 *  Read its header for what is modelled and what is not.
 *
 *  Zero-asset: the bundled font and procedural icon headers, nothing to install.
 * ========================================================================= */

#include "rayclay.h"

#include "icons/rc_icons_chart_column.h"

#include "catalog.h"

/* Font slots, in load order into RC_AppOptions.fontSizes; the index is the
   .font value in RC_TextOptions. Baked from the bundled face (zero-asset). */
enum { F_MICRO = 0, F_SMALL, F_HEAD, F_STAT, F_COUNT };

enum {
    HIST_BINS   = 24,    /* radius histogram resolution                       */
    HIST_MAX_R  = 20,    /* Earth radii the histogram spans                   */
    SCATTER_MAX = 1200,  /* markers actually plotted; see plot_scatter        */
    SCATTER_P   = 100,   /* scatter x window, days                            */
    SCATTER_R   = 20,    /* scatter y window, Earth radii                     */
    ROW_H       = 26     /* table row height                                  */
};

/* ------------------------------------------------------------------------ *
 *  Columns. ONE table drives the header row, the sort keys and the cells, so
 *  a column cannot drift out of alignment with its own heading.
 * ------------------------------------------------------------------------ */

typedef enum SortKey {
    K_NAME = 0, K_METHOD, K_YEAR, K_RADIUS, K_PERIOD, K_DISTANCE, K_COUNT
} SortKey;

typedef struct Column {
    const char *label;
    const char *width;
    const char *headerId;  /* a literal: an id must outlive the frame        */
    bool        numeric;   /* right-align, as every table of numbers does    */
} Column;

static const Column COLUMN[K_COUNT] = {
    { "Planet",        "grow",  "h_name",   false },
    { "Method",        "88px",  "h_method", false },
    { "Year",          "64px",  "h_year",   true  },
    { "Radius",        "80px",  "h_radius", true  },
    { "Period (d)",    "100px", "h_period", true  },
    { "Distance (pc)", "128px", "h_dist",   true  }
};

/* ------------------------------------------------------------------------ *
 *  State
 * ------------------------------------------------------------------------ */

typedef struct Filters {
    bool  method[CAT_METHODS];
    float sinceYear;    /* discovered in this year or later                  */
    float maxDistance;  /* parsecs; the slider's top stop means "no limit"   */
    float minRadius;    /* Earth radii                                       */
    float maxRadius;
} Filters;

/** Everything derived from (catalogue, filters, sort). Rebuilt as one unit,
    because a half-updated view is the bug this struct exists to prevent: if
    the chart arrays and the row list can be refreshed separately, one day they
    will be, and the chart will describe a selection the table is not showing. */
typedef struct View {
    int32_t sel[CAT_COUNT];   /* catalogue indices that pass, in sort order   */
    int     count;

    float   yearX[CAT_YEARS]; /* shared x for both year series                */
    float   yearAll[CAT_YEARS];
    float   yearSel[CAT_YEARS];

    float   histX[HIST_BINS];
    float   histSel[HIST_BINS];

    float   scatterX[SCATTER_MAX];
    float   scatterY[SCATTER_MAX];
    int     scatterCount;
    int     scatterStride;    /* 1 = every point; >1 = a sample              */
    int     scatterOutside;   /* passed the filter, off the plotted window   */

    int     byMethod[CAT_METHODS];
    float   meanRadius;
    float   meanDistance;
    int     nearest;          /* catalogue index of the closest match, or -1 */

    /* The farthest row in the catalogue, so the distance slider's top stop is
       the data's own maximum. A hard-coded stop is either short - quietly
       excluding rows nobody asked to exclude - or so long that the useful
       range lives in the first tenth of the track. */
    float   distanceMax;

    int     rebuilds;         /* how many times view_rebuild has run         */
} View;

typedef struct AppState {
    Catalog cat;
    View    view;
    Filters filter;
    SortKey sortKey;
    bool    sortDesc;
    int     selected;         /* catalogue index, or -1                      */
    bool    refilter;         /* a filter control moved this frame           */
    bool    resort;           /* a header was clicked this frame             */
} AppState;

/* ------------------------------------------------------------------------ *
 *  Small helpers
 * ------------------------------------------------------------------------ */

/* Per-row element ids: let rcFormat build them.
 *
 *  A dynamic list needs a per-row element id, which is a NUL-terminated
 *  `const char *`. `rcFormat` returns an RC_String - a pointer and a length -
 *  and its result is promised NUL-terminated on every path, so
 *  `.id = rcFormat(mem, "r%d", i).chars` is the supported idiom and no
 *  digit-pushing helper is needed.
 *
 *  It is arena memory, valid for the rest of this frame - which is exactly
 *  the lifetime an id needs (see the note above the row loop).
 */

/** A quantity at a sensible number of significant figures.
 *
 *  Distance runs from 1.3 pc to nine thousand and period from half a day to
 *  three centuries, so NO single printf specifier serves either column: "%.0f"
 *  collapses every nearby planet to the same integer, and "%.2f" puts two
 *  meaningless decimals on a microlensing detection. A column of numbers needs
 *  a rule, not a format string - this is the rule.
 */
static RC_String fmt_sig(RC_Arena *mem, float v)
{
    if (v < 10.0f)
        return rcFormat(mem, "%.2f", (double)v);
    if (v < 100.0f)
        return rcFormat(mem, "%.1f", (double)v);
    return rcFormat(mem, "%.0f", (double)v);
}

/** The one-word size class a radius puts a planet in. Purely presentational -
    the generator's own classes are private to catalog.h - but it is what makes
    the detail card readable to someone who does not think in Earth radii. */
static const char *size_class(float radius)
{
    if (radius < 1.25f)
        return "Earth-sized";
    if (radius < 2.0f)
        return "Super-Earth";
    if (radius < 4.0f)
        return "Sub-Neptune";
    if (radius < 8.0f)
        return "Neptune-like";
    return "Gas giant";
}

/** A distinct colour per detection method, so the same method reads the same
    in the summary bars, the table and the detail card. Derived from the theme
    rather than hard-coded, so it follows a theme swap. */
static RC_Color method_color(const RC_Style *s, int method)
{
    switch (method) {
    case CAT_TRANSIT:   return s->primary;
    case CAT_RADIAL:    return s->success;
    case CAT_MICROLENS: return s->warning;
    default:            return s->danger;
    }
}

/* ------------------------------------------------------------------------ *
 *  The analytics core
 * ------------------------------------------------------------------------ */

/** The value a row sorts on. Returning a float for every key keeps the
    comparator branch-free and, more importantly, keeps the ORDER defined in
    one place: add a column and there is exactly one function to extend.

    Sorting by name means sorting by (survey, host number), which is what the
    displayed designation reads as - and it avoids building 12,000 strings to
    compare. Both parts fit a float exactly: 9 * 10000 + 9999 is well inside
    the 24-bit mantissa. */
static float sort_value(const CatPlanet *p, SortKey key)
{
    switch (key) {
    case K_NAME:     return (float)(p->survey * 10000 + p->host);
    case K_METHOD:   return (float)p->method;
    case K_YEAR:     return (float)p->year;
    case K_RADIUS:   return p->radius;
    case K_PERIOD:   return p->period;
    default:         return p->distance;
    }
}

/** Sift `hole` down a max-heap of `n` indices. Split out of view_sort because
    heapsort calls it from two places and the sift is the whole algorithm. */
static void heap_sift(int32_t *a, int n, int hole, const Catalog *c,
                      SortKey key, bool desc)
{
    int32_t held = a[hole];
    float   heldV = sort_value(&c->row[held], key);

    for (;;) {
        int child = 2 * hole + 1;
        float childV, rightV;

        if (child >= n)
            break;
        childV = sort_value(&c->row[a[child]], key);
        if (child + 1 < n) {
            rightV = sort_value(&c->row[a[child + 1]], key);
            /* Descending sorts build a MIN-heap, so one routine serves both
               directions and the two orders cannot drift apart. */
            if (desc ? rightV < childV : rightV > childV) {
                child++;
                childV = rightV;
            }
        }
        if (desc ? childV >= heldV : childV <= heldV)
            break;
        a[hole] = a[child];
        hole = child;
    }
    a[hole] = held;
}

/** Order the selection in place.
 *
 *  Heapsort, because this file cannot call qsort: examples are held to a
 *  no-libc contract (test/check-examples-pure-rc.sh), and YOUR app should just
 *  call qsort - nothing about RayClay asks you to write a sort. Given that one
 *  had to be written, heapsort is the right one to write: it allocates nothing,
 *  never recurses, and its O(n log n) is a worst case rather than an average,
 *  so a user dragging a slider cannot find the input that makes it stall.
 */
static void view_sort(View *v, const Catalog *c, SortKey key, bool desc)
{
    int i;

    for (i = v->count / 2 - 1; i >= 0; i--)
        heap_sift(v->sel, v->count, i, c, key, desc);

    for (i = v->count - 1; i > 0; i--) {
        int32_t top = v->sel[0];

        v->sel[0] = v->sel[i];
        v->sel[i] = top;
        heap_sift(v->sel, i, 0, c, key, desc);
    }
}

/** Take up to SCATTER_MAX points off the selection, evenly strided.
 *
 *  A scatter marker is a disc, and a disc is a triangle fan, so 12,000 of them
 *  is tens of thousands of vertices in one draw - past the renderer's per-frame
 *  pool. On the default renderer that is not a degraded frame, it is no frame:
 *  the packet backend refuses an over-capacity frame whole and logs an error,
 *  deliberately, so you get a blank window rather than a silently clipped scene
 *  you might mistake for the data. (Dropping the overflow and carrying on is the
 *  alternate renderer, RC_GFX_PACKET=0.) The fix is not a bigger pool: a scatter
 *  plot communicates density, and a uniform sample has the same density
 *  everywhere the full cloud does. It just draws.
 *
 *  Strided, not "the first 1,200": the selection is in catalogue order here
 *  (view_sort runs after this), so a stride samples the whole set while a
 *  prefix would show whichever rows happened to be generated first.
 *
 *  Points outside the pinned axis window are counted rather than clamped -
 *  clamping would pile a false ridge of markers onto the plot edge, which is
 *  the one thing a scatter must never do. The count is reported on the chart.
 */
static void plot_scatter(View *v, const Catalog *c)
{
    int i;

    /* CEILING division: a floor would leave ceil(count/stride) above SCATTER_MAX
       for every count that is not an exact multiple, and the plot would then
       drop the tail of the run rather than thin it evenly. */
    v->scatterStride  = v->count > SCATTER_MAX
                      ? (v->count + SCATTER_MAX - 1) / SCATTER_MAX : 1;
    v->scatterCount   = 0;
    v->scatterOutside = 0;

    for (i = 0; i < v->count; i += v->scatterStride) {
        const CatPlanet *p = &c->row[v->sel[i]];

        if (p->period > (float)SCATTER_P || p->radius > (float)SCATTER_R) {
            v->scatterOutside++;
            continue;
        }
        if (v->scatterCount >= SCATTER_MAX)
            break;
        v->scatterX[v->scatterCount] = p->period;
        v->scatterY[v->scatterCount] = p->radius;
        v->scatterCount++;
    }
}

/** Everything derived, in one pass and one place.
 *
 *  Call this when a control moves, NOT every frame. One pass over the rows
 *  fills the selection, both chart series, the per-method tally and the means;
 *  the sample and the sort follow. That ordering is deliberate - see
 *  plot_scatter for why the sample is taken before the sort.
 */
static void view_rebuild(View *v, const Catalog *c, const Filters *f,
                         SortKey key, bool desc)
{
    double sumR = 0.0, sumD = 0.0;
    float  nearestD = 0.0f;
    int    i;

    v->count   = 0;
    v->nearest = -1;
    for (i = 0; i < CAT_YEARS; i++)
        v->yearSel[i] = 0.0f;
    for (i = 0; i < HIST_BINS; i++)
        v->histSel[i] = 0.0f;
    for (i = 0; i < CAT_METHODS; i++)
        v->byMethod[i] = 0;

    for (i = 0; i < CAT_COUNT; i++) {
        const CatPlanet *p = &c->row[i];
        int bin;

        if (!f->method[p->method])
            continue;
        if ((float)p->year < f->sinceYear)
            continue;
        if (p->distance > f->maxDistance)
            continue;
        if (p->radius < f->minRadius || p->radius > f->maxRadius)
            continue;

        v->sel[v->count++] = i;
        v->yearSel[p->year - CAT_YEAR_FIRST] += 1.0f;
        v->byMethod[p->method]++;
        sumR += (double)p->radius;
        sumD += (double)p->distance;
        if (v->nearest < 0 || p->distance < nearestD) {
            v->nearest = i;
            nearestD   = p->distance;
        }

        /* The top bin is half-open upward so the giants above 20 Earth radii
           land somewhere visible instead of being silently dropped. */
        bin = (int)(p->radius * (float)HIST_BINS / (float)HIST_MAX_R);
        if (bin < 0)
            bin = 0;
        if (bin >= HIST_BINS)
            bin = HIST_BINS - 1;
        v->histSel[bin] += 1.0f;
    }

    v->meanRadius   = v->count ? (float)(sumR / v->count) : 0.0f;
    v->meanDistance = v->count ? (float)(sumD / v->count) : 0.0f;

    plot_scatter(v, c);
    view_sort(v, c, key, desc);
    v->rebuilds++;
}

/** The parts of the view that never change: the chart x axes, and the "all
    12,000" context series the year chart draws behind the selection. */
static void view_init(View *v, const Catalog *c)
{
    int i;

    for (i = 0; i < CAT_YEARS; i++) {
        v->yearX[i]   = (float)(CAT_YEAR_FIRST + i);
        v->yearAll[i] = 0.0f;
    }
    v->distanceMax = 0.0f;
    for (i = 0; i < CAT_COUNT; i++) {
        v->yearAll[c->row[i].year - CAT_YEAR_FIRST] += 1.0f;
        if (c->row[i].distance > v->distanceMax)
            v->distanceMax = c->row[i].distance;
    }

    /* Bin centres, so a bar sits over the range it counts rather than starting
       at it. */
    for (i = 0; i < HIST_BINS; i++)
        v->histX[i] = ((float)i + 0.5f) * (float)HIST_MAX_R / (float)HIST_BINS;
}

static void filters_reset(Filters *f, float distanceMax)
{
    int i;

    for (i = 0; i < CAT_METHODS; i++)
        f->method[i] = true;
    f->sinceYear   = (float)CAT_YEAR_FIRST;
    f->maxDistance = distanceMax;
    f->minRadius   = 0.0f;
    f->maxRadius   = (float)HIST_MAX_R;
}

/* ------------------------------------------------------------------------ *
 *  Panels
 * ------------------------------------------------------------------------ */

static void panel_heading(const char *text)
{
    rcTextC(text, .font = F_MICRO, .color = rcGetStyle().textMuted);
}

/** A labelled slider: caption and live value on one line, the track under it.
    Returns true on the frames the value changes, which is what drives the
    rebuild - see the note in layout(). */
static bool slider_row(RC_App *app, const char *id, const char *label,
                       const char *fmt, float *value, float min, float max)
{
    RC_Arena *mem = rcAppArena(app);
    RC_Style  s   = rcGetStyle();
    bool      moved;

    rcRow(.w = "grow", .align = "cl") {
        rcTextC(label, .font = F_SMALL, .color = s.textMuted);
        rcBox(.w = "grow") {}
        rcText(rcFormat(mem, fmt, (double)*value), .font = F_SMALL,
                .color = s.text);
    }
    moved = rcSlider(id, value, min, max);
    return moved;
}

static void filter_card(RC_App *app, AppState *st)
{
    RC_Arena *mem = rcAppArena(app);
    RC_Style  s   = rcGetStyle();
    int       i;

    rcColumn(.id = "p_filters", .w = "grow", .bg = s.surface, .p = 14, .gap = 10,
              .borderRadius = "all-lg", .border = { .color = s.border,
                                                    .width = "1px" }) {
        rcRow(.w = "grow", .align = "cl") {
            panel_heading("FILTERS");
            rcBox(.w = "grow") {}
            if (rcButton("f_reset", "Reset", RC_BTN_GHOST)) {
                filters_reset(&st->filter, st->view.distanceMax);
                st->refilter = true;
            }
        }

        rcColumn(.w = "grow", .gap = 2) {
            for (i = 0; i < CAT_METHODS; i++) {
                rcRow(.w = "grow", .gap = 8, .align = "cl") {
                    rcBox(.wType = RC_PX(8), .hType = RC_PX(8),
                           .bg = method_color(&s, i), .borderRadius = "all-sm") {}
                    if (rcCheckbox(rcFormat(mem, "f_m%d", i).chars,
                                    CAT_METHOD_NAME[i], &st->filter.method[i]))
                        st->refilter = true;
                    rcBox(.w = "grow") {}
                    rcText(rcFormat(mem, "%d", st->view.byMethod[i]),
                            .font = F_MICRO, .color = s.textMuted);
                }
            }
        }

        if (slider_row(app, "f_year", "Discovered since", "%.0f",
                        &st->filter.sinceYear, (float)CAT_YEAR_FIRST,
                        (float)CAT_YEAR_LAST))
            st->refilter = true;

        /* The top stop IS the farthest row in the catalogue, so the slider has
           a genuine "no limit" end that does not quietly exclude the
           microlensing detections out in the galactic bulge. */
        if (slider_row(app, "f_dist", "Within (pc)", "%.0f",
                        &st->filter.maxDistance, 10.0f, st->view.distanceMax))
            st->refilter = true;

        if (slider_row(app, "f_rmin", "Radius at least", "%.1f",
                        &st->filter.minRadius, 0.0f, (float)HIST_MAX_R))
            st->refilter = true;
        if (slider_row(app, "f_rmax", "Radius at most", "%.1f",
                        &st->filter.maxRadius, 0.0f, (float)HIST_MAX_R))
            st->refilter = true;
    }
}

static void summary_card(RC_App *app, AppState *st)
{
    RC_Arena *mem = rcAppArena(app);
    RC_Style  s   = rcGetStyle();
    const View *v = &st->view;
    int       i;

    rcColumn(.id = "p_summary", .w = "grow", .bg = s.surface, .p = 14, .gap = 8,
              .borderRadius = "all-lg", .border = { .color = s.border,
                                                    .width = "1px" }) {
        panel_heading("SELECTION");
        rcRow(.w = "grow", .gap = 6, .align = "bl") {
            rcText(rcFormat(mem, "%d", v->count), .font = F_STAT,
                    .color = s.text);
            rcText(rcFormat(mem, "of %d planets", CAT_COUNT), .font = F_SMALL,
                    .color = s.textMuted);
        }

        /* One bar per method, each a percentage of the WHOLE selection, so the
           bars read as a stacked breakdown laid out flat. */
        rcColumn(.w = "grow", .gap = 3) {
            for (i = 0; i < CAT_METHODS; i++) {
                float pct = v->count ? 100.0f * (float)v->byMethod[i] /
                                       (float)v->count
                                     : 0.0f;

                rcBox(.w = "grow", .hType = RC_PX(6), .bg = s.surfaceAlt,
                       .borderRadius = "all-sm") {
                    rcBox(.wType = RC_PCT(pct), .h = "grow",
                           .bg = method_color(&s, i),
                           .borderRadius = "all-sm") {}
                }
            }
        }

        rcRow(.w = "grow", .align = "cl") {
            rcTextL("Mean radius", .font = F_SMALL, .color = s.textMuted);
            rcBox(.w = "grow") {}
            rcText(rcFormat(mem, "%.2f Earth", (double)v->meanRadius),
                    .font = F_SMALL, .color = s.text);
        }
        rcRow(.w = "grow", .align = "cl") {
            rcTextL("Mean distance", .font = F_SMALL, .color = s.textMuted);
            rcBox(.w = "grow") {}
            rcText(rcFormat(mem, "%.0f pc", (double)v->meanDistance),
                    .font = F_SMALL, .color = s.text);
        }
    }
}

/** One label/value line in the detail card. The unit is a separate, muted run
    rather than part of the formatted value: it keeps the numbers scannable
    down the column, and it means fmt_sig can own the number's precision
    without also having to know what it is a number OF. */
static void detail_row(const char *label, RC_String value, const char *unit)
{
    RC_Style s = rcGetStyle();

    rcRow(.w = "grow", .align = "cl") {
        rcTextC(label, .font = F_SMALL, .color = s.textMuted);
        rcBox(.w = "grow") {}
        rcRow(.gap = 3, .align = "bl") {
            rcText(value, .font = F_SMALL, .color = s.text);
            if (unit)
                rcTextC(unit, .font = F_MICRO, .color = s.textMuted);
        }
    }
}

static void detail_card(RC_App *app, AppState *st)
{
    RC_Arena *mem = rcAppArena(app);
    RC_Style  s   = rcGetStyle();
    /* Fall back to the nearest planet in the selection, so the card is never
       empty and always says something true about what is on screen. */
    int       idx = st->selected >= 0 ? st->selected : st->view.nearest;

    rcColumn(.id = "p_detail", .w = "grow", .h = "grow", .bg = s.surface,
              .p = 14, .gap = 8, .borderRadius = "all-lg",
              .border = { .color = s.border, .width = "1px" }) {
        panel_heading(st->selected >= 0 ? "SELECTED" : "CLOSEST IN SELECTION");

        if (idx < 0) {
            rcTextL("No planet matches these filters.", .font = F_SMALL,
                     .color = s.textMuted);
        } else {
            const CatPlanet *p = &st->cat.row[idx];

            rcText(rcFormat(mem, "%s-%d %s", CAT_SURVEY[p->survey],
                              (int)p->host, catalog_letter(p)),
                    .font = F_HEAD, .color = s.text);
            rcRow(.w = "grow", .gap = 6, .align = "cl") {
                rcBox(.wType = RC_PX(8), .hType = RC_PX(8),
                       .bg = method_color(&s, p->method),
                       .borderRadius = "all-sm") {}
                rcTextC(size_class(p->radius), .font = F_SMALL,
                         .color = s.textMuted);
            }
            rcBox(.w = "grow", .hType = RC_PX(1), .bg = s.border) {}

            detail_row("Method",
                        rcStringFromCStr(CAT_METHOD_NAME[p->method]), NULL);
            detail_row("Discovered", rcFormat(mem, "%d", p->year), NULL);
            detail_row("Radius", fmt_sig(mem, p->radius), "Earth");
            detail_row("Mass", fmt_sig(mem, p->mass), "Earth");
            detail_row("Orbital period", fmt_sig(mem, p->period), "days");
            detail_row("Distance", fmt_sig(mem, p->distance), "pc");
            detail_row("Equilibrium temp", rcFormat(mem, "%d", p->teq), "K");
        }
    }
}

static void chart_years(AppState *st)
{
    RC_Style s = rcGetStyle();
    View    *v = &st->view;

    /* Drawn first, so it sits behind: the whole catalogue as a muted band, the
       current selection as bars in front. Reading a filtered chart without its
       context is how a selection that removed 90% of the data still looks
       like the whole story. */
    RC_Series series[2] = {
        { .y = v->yearAll, .x = v->yearX, .count = CAT_YEARS,
          .kind = RC_SERIES_AREA, .color = rcAlpha(s.textMuted, 60),
          .label = "All discoveries" },
        { .y = v->yearSel, .x = v->yearX, .count = CAT_YEARS,
          .kind = RC_SERIES_BAR, .color = s.primary,
          .label = "Current selection" }
    };

    rcChart("ch_years", series, 2, (RC_ChartOptions){
        .x = { .min = (float)CAT_YEAR_FIRST, .max = (float)CAT_YEAR_LAST,
               .ticks = 8 },
        .y = { .grid = true, .ticks = 4 },
        .legend      = true,
        .fontSize    = 11,
        .tooltip     = RC_CHART_TOOLTIP_NEAREST,
        .hoverGuide  = true,
        .hoverMarkers = true
    });
}

static void chart_radius(AppState *st)
{
    RC_Style  s = rcGetStyle();
    RC_Series bars = {
        .y = st->view.histSel, .x = st->view.histX, .count = HIST_BINS,
        .kind = RC_SERIES_BAR, .color = s.success, .label = "Planets"
    };

    rcChart("ch_radius", &bars, 1, (RC_ChartOptions){
        .x = { .min = 0.0f, .max = (float)HIST_MAX_R, .ticks = 5 },
        .y = { .grid = true, .ticks = 4 },
        .fontSize = 11,
        .tooltip  = RC_CHART_TOOLTIP_NEAREST
    });
}

static void chart_scatter(AppState *st)
{
    RC_Style  s = rcGetStyle();
    RC_Series dots = {
        .y = st->view.scatterY, .x = st->view.scatterX,
        .count = st->view.scatterCount, .kind = RC_SERIES_SCATTER,
        .color = rcAlpha(s.warning, 170),
        .thickness = 2.0f   /* SCATTER reads .thickness as the marker radius */
    };

    rcChart("ch_scatter", &dots, 1, (RC_ChartOptions){
        .x = { .min = 0.0f, .max = (float)SCATTER_P, .ticks = 5 },
        .y = { .min = 0.0f, .max = (float)SCATTER_R, .ticks = 5, .grid = true },
        .fontSize = 11,
        .tooltip  = RC_CHART_TOOLTIP_NEAREST
    });
}

/** A titled frame around a chart. Charts GROW both ways, so something has to
    give them a size - that is what the fixed-height row in layout() and the
    grow box in here are between them doing. */
static void chart_card(const char *id, const char *title, const char *note,
                       const char *width, void (*body)(AppState *), AppState *st)
{
    RC_Style s = rcGetStyle();

    rcColumn(.id = id, .w = width, .h = "grow", .bg = s.surface, .p = 12,
              .gap = 6, .borderRadius = "all-lg",
              .border = { .color = s.border, .width = "1px" }) {
        rcRow(.w = "grow", .gap = 8, .align = "cl") {
            rcTextC(title, .font = F_MICRO, .color = s.textMuted);
            rcBox(.w = "grow") {}
            rcTextC(note, .font = F_MICRO, .color = s.textMuted);
        }
        rcBox(.w = "grow", .h = "grow") {
            body(st);
        }
    }
}

/** The sortable header row.
 *
 *  Hand-rolled rather than rcBeginTable's own header, for the same reason the
 *  rows below are hand-rolled: the table's header cells are TEXT, not elements
 *  the caller can give an id to, so there is nothing to hit-test. Sorting is an
 *  explicit v1 non-goal of rcBeginTable - "rows are the caller's array; sort it
 *  yourself" - and this is what taking it up on that looks like.
 *
 *  Direction is shown by the accent colour plus a word, not by an arrow glyph:
 *  the bundled face is Latin-1, so a triangle would draw as a missing-glyph box
 *  on every platform. Check your text against the font, not against your editor.
 */
static void table_header(AppState *st)
{
    RC_Style s = rcGetStyle();
    int      i;

    /* The right inset matches the body's, so the columns stay registered with
       their headings once rcScrollbar takes the last 12 px of the rows. */
    rcRow(.w = "grow", .gap = 0, .pr = 12, .bg = s.surfaceAlt,
           .borderRadius = "t-lg") {
        for (i = 0; i < K_COUNT; i++) {
            bool active = (st->sortKey == (SortKey)i);

            rcColumn(.id = COLUMN[i].headerId, .w = COLUMN[i].width,
                      .px = 8, .py = 7, .gap = 4) {
                rcRow(.w = "grow", .gap = 5, .align = "cl") {
                    /* Numeric columns right-align so the heading sits over its
                       own column of digits. `.align` is a char[3] in the
                       options record, not a const char *, so it cannot be
                       picked with a ternary the way `.w` can - the flex idiom
                       does it instead, and reads better anyway: a growing
                       spacer in front pushes everything after it to the end. */
                    if (COLUMN[i].numeric)
                        rcBox(.w = "grow") {}
                    rcTextC(COLUMN[i].label, .font = F_MICRO,
                             .color = active ? s.primary : s.textMuted);
                    if (active) {
                        rcTextC(st->sortDesc ? "DESC" : "ASC", .font = F_MICRO,
                                 .color = rcAlpha(s.primary, 160));
                    }
                }
                rcBox(.w = "grow", .hType = RC_PX(2),
                       .bg = active ? s.primary : rcAlpha(s.border, 0)) {}
            }
            if (rcClicked(COLUMN[i].headerId)) {
                /* Clicking the active column flips it; a new column starts
                   ascending, except for the columns where "most" is the
                   interesting end and starting ascending wastes a click. */
                if (active) {
                    st->sortDesc = !st->sortDesc;
                } else {
                    st->sortKey  = (SortKey)i;
                    st->sortDesc = (i == K_YEAR || i == K_RADIUS);
                }
                st->resort = true;
            }
        }
    }
}

/** The virtualized, selectable table body.
 *
 *  TWO THINGS HERE ARE THE POINT.
 *
 *  Virtualization: 12,000 rows, about twenty declared. rcVirtualList emits the
 *  visible window plus a row of overscan each side and pads the rest with two
 *  spacers, so the scroll travel, the content height and the wheel all behave
 *  exactly as if every row were there - at a cost that does not depend on the
 *  row count at all. Without it, layout is charged for all 12,000 every frame,
 *  because Clay culls offscreen elements but still lays them out.
 *
 *  Rows as elements: each row is ONE rcRow with its own id, rather than a
 *  rcBeginTable row. rcTableRow() opens a row but takes no id, so a table row
 *  cannot be hovered, clicked or selected as a unit - you would be hit-testing
 *  six cells and hoping they agree. When rows are display-only, use
 *  rcBeginTable (ex10 and ex20 both do); when a row is a control, build it from
 *  a rcRow and share one width table with the header, as here.
 */
static void table_body(RC_App *app, AppState *st)
{
    RC_Arena *mem   = rcAppArena(app);
    RC_Style  s     = rcGetStyle();
    /* Ids must outlive the frame: the id is hashed as the element opens, so a
       local buffer would behave correctly, but the debug inspector and the
       duplicate-id diagnostic keep the POINTER and would report garbage.
       The frame arena is therefore the right storage - it lives until the next
       rcArenaReset, which is after everything that reads the pointer. */
    rcColumn(.id = "tbl", .w = "grow", .h = "grow", .pr = 12, .scroll = "v") {
        rcVirtualList(row, "tbl", st->view.count, (float)ROW_H) {
            const CatPlanet *p = &st->cat.row[st->view.sel[row.index]];
            bool  chosen = (st->view.sel[row.index] == st->selected);
            const char *id = rcFormat(mem, "r%d", row.index).chars;
            RC_Color bg;
            int   i;

            bg = chosen        ? rcAlpha(s.primary, 55)
               : rcIsHovered(id) ? rcAlpha(s.border, 90)
               : (row.index & 1) ? rcAlpha(s.border, 26)
                                 : rcAlpha(s.border, 0);

            rcRow(.id = id, .w = "grow", .hType = RC_PX(ROW_H), .bg = bg) {
                for (i = 0; i < K_COUNT; i++) {
                    RC_Color fg = i == K_NAME ? s.text : s.textMuted;

                    rcRow(.w = COLUMN[i].width, .h = "grow", .px = 8,
                           .align = "cl") {
                        if (COLUMN[i].numeric)
                            rcBox(.w = "grow") {}
                        switch (i) {
                        case K_NAME:
                            /* Formatted into the FRAME ARENA, not a local
                               buffer: RayClay keeps the pointer until the
                               frame is drawn, so a stack string is dangling
                               by then - silently, with no diagnostic and no
                               crash, just an empty column. */
                            rcText(rcFormat(mem, "%s-%d %s",
                                              CAT_SURVEY[p->survey],
                                              (int)p->host, catalog_letter(p)),
                                    .font = F_SMALL, .color = fg);
                            rcBox(.w = "grow") {}
                            rcTextC(size_class(p->radius), .font = F_MICRO,
                                     .color = rcAlpha(s.textMuted, 150));
                            break;
                        case K_METHOD:
                            rcTextC(CAT_METHOD_ABBR[p->method], .font = F_SMALL,
                                     .color = method_color(&s, p->method));
                            break;
                        case K_YEAR:
                            rcText(rcFormat(mem, "%d", p->year),
                                    .font = F_SMALL, .color = fg);
                            break;
                        case K_RADIUS:
                            rcText(rcFormat(mem, "%.2f", (double)p->radius),
                                    .font = F_SMALL, .color = fg);
                            break;
                        case K_PERIOD:
                            rcText(fmt_sig(mem, p->period), .font = F_SMALL,
                                    .color = fg);
                            break;
                        default:
                            rcText(fmt_sig(mem, p->distance), .font = F_SMALL,
                                    .color = fg);
                            break;
                        }
                    }
                }
            }

            if (rcClicked(id))
                st->selected = st->view.sel[row.index];
        }
    }
    rcScrollbar("tbl");
    /* The rail only overflows once zoom shrinks the viewport past its cards, so
       this bar auto-hides at rest and appears exactly when it is needed. */
    rcScrollbar("rail");
}

static void status_bar(RC_App *app, AppState *st)
{
    RC_Arena *mem = rcAppArena(app);
    RC_Style  s   = rcGetStyle();
    const View *v = &st->view;

    rcRow(.w = "grow", .hType = RC_PX(26), .px = 12, .gap = 16, .align = "cl",
           .bg = s.surface, .borderRadius = "all-md") {
        /* The cache, made visible. This counter moves when a control moves and
           at no other time - resize the window, scroll the table, hover a
           chart, and it stays put. That is the claim in point 2 of the file
           header, printed rather than argued. */
        rcText(rcFormat(mem, "%d rows scanned per rebuild \xc2\xb7 rebuilds so "
                              "far: %d", CAT_COUNT, v->rebuilds),
                .font = F_MICRO, .color = s.textMuted);
        rcBox(.w = "grow") {}
        rcText(rcFormat(mem, "scatter: %d of %d plotted (every %d), %d outside "
                              "the window",
                          v->scatterCount, v->count, v->scatterStride,
                          v->scatterOutside),
                .font = F_MICRO, .color = s.textMuted);
    }
}

/* ------------------------------------------------------------------------ *
 *  Frame
 * ------------------------------------------------------------------------ */

static void layout(RC_App *app, void *userData)
{
    AppState *st = (AppState *)userData;
    RC_Style  s  = rcGetStyle();
    RC_Arena *mem = rcAppArena(app);
    int       before = st->selected;

    st->refilter = false;
    st->resort   = false;

    rcBox(.id = "root", .w = "grow", .h = "grow", .bg = s.background, .p = 12,
           .gap = 12) {
        rcRow(.w = "grow", .h = "grow", .gap = 12) {

            /* The rail scrolls, and that is a zoom fix rather than a cosmetic
               one. Its cards are sized by their content, so past about 175%
               zoom - where the logical viewport is 860/1.75 = 491px - the rail
               is taller than the window. Without `.scroll` it forced the whole
               content ROW to its own height, the table's `grow` share resolved
               against that instead of the viewport, and rcVirtualList was
               handed a viewport taller than the layout: it cannot tell that
               from a list reporting its own content back, so it clamped and
               warned. Scrolling is also simply what a sidebar taller than the
               window does in a browser, which is the out-of-the-box contract.
               A sibling of the table's scroll container, never an ancestor -
               a scroll container inside another one is the one shape that
               genuinely cannot bound a virtual list. */
            rcColumn(.id = "rail", .wType = RC_PX(276), .h = "grow", .gap = 12,
                      .scroll = "v") {
                rcRow(.w = "grow", .gap = 8, .align = "cl") {
                    rcIconChartColumn(17.0f, s.primary);
                    rcTextL("Exoplanet Explorer", .font = F_HEAD,
                             .color = s.text);
                }
                filter_card(app, st);
                summary_card(app, st);
                detail_card(app, st);
            }

            /* The rebuild happens HERE, between the two columns, and that
               position is load-bearing: the rail's controls have just been
               polled, and everything that reads the view is still ahead of us,
               so the charts and the table below cannot draw a frame older than
               the slider the user is dragging. Deferring it to the next frame
               would show a chart that disagrees with its own control. */
            if (st->refilter) {
                view_rebuild(&st->view, &st->cat, &st->filter, st->sortKey,
                              st->sortDesc);
                st->selected = -1;
            }

            rcColumn(.w = "grow", .h = "grow", .gap = 12) {
                rcRow(.w = "grow", .hType = RC_PX(228), .gap = 12) {
                    chart_card("c_years", "DISCOVERIES BY YEAR",
                                "selection over the full catalogue", "grow",
                                chart_years, st);
                    chart_card("c_radius", "RADIUS DISTRIBUTION",
                                "Earth radii", "300px", chart_radius, st);
                }

                rcRow(.w = "grow", .h = "grow", .gap = 12) {
                    rcColumn(.id = "p_table", .w = "grow", .h = "grow",
                              .bg = s.surface, .gap = 0,
                              .borderRadius = "all-lg",
                              .border = { .color = s.border,
                                          .width = "1px" }) {
                        table_header(st);
                        /* Re-sorting reorders the SAME selection, so it does
                           not touch the charts and does not need the full
                           rebuild - but it does have to land before the rows
                           below are declared. */
                        if (st->resort)
                            view_sort(&st->view, &st->cat, st->sortKey,
                                       st->sortDesc);
                        table_body(app, st);
                    }

                    chart_card("c_scatter", "PERIOD vs RADIUS",
                                "days vs Earth radii, sampled", "340px",
                                chart_scatter, st);
                }

                status_bar(app, st);
            }
        }

        rcRow(.w = "grow", .gap = 6, .align = "cl") {
            rcTextL("Synthetic catalogue, shaped after the real record \xc2\xb7 "
                     "see catalog.h", .font = F_MICRO, .color = s.textMuted);
            rcBox(.w = "grow") {}
            rcText(rcFormat(mem, "sorted by %s", COLUMN[st->sortKey].label),
                    .font = F_MICRO, .color = s.textMuted);
        }
    }

    /* The detail card is drawn in the rail, BEFORE the table that changes the
       selection, so a click would otherwise sit on screen for a frame showing
       the previous planet. In an on-demand app nothing else is coming to fix
       that, so ask for the frame - this is the "if RayClay cannot see it
       change, ask for a frame" rule applied to ordering rather than to time. */
    if (st->selected != before)
        rcAppRequestFrame(app);
}

int main(void)
{
    static AppState state;
    float fontSizes[F_COUNT];

    fontSizes[F_MICRO] = 11.0f;
    fontSizes[F_SMALL] = 12.5f;
    fontSizes[F_HEAD]  = 17.0f;
    fontSizes[F_STAT]  = 26.0f;

    rcSetStyle(rcStyleDark());

    catalog_init(&state.cat, 0x45585021u);
    view_init(&state.view, &state.cat);
    filters_reset(&state.filter, state.view.distanceMax);
    /* Nearest first: the most interesting question a catalogue answers on
       opening, and unlike "newest first" it gives a first screen where every
       column varies. */
    state.sortKey  = K_DISTANCE;
    state.sortDesc = false;
    state.selected = -1;
    view_rebuild(&state.view, &state.cat, &state.filter, state.sortKey,
                  state.sortDesc);

    RC_AppOptions opts = {
        .width  = 1320,
        .height = 860,
        .title  = "RayClay Data Explorer",
        .clearColor = rcGetStyle().background,
        .fontSizes = fontSizes,
        .fontCount = F_COUNT,
        /* Backs every rcFormat in a frame: ~20 virtualized rows x 4 formatted
           cells, plus the rail, the detail card and the status bar. */
        .scratchArenaBytes = 32768,
        /* The bundled titlebar, not a custom one: ex20 is the branded-window
           example, and this app is here to show what you get for free. */
        .nativeFrame = true,
        .layoutCallback  = layout,
        .userData  = &state,
        /* The default, spelled out because it is what an analytics tool should
           do: nothing animates here, so the window parks between interactions
           and costs nothing while you read it. */
        .renderMode = RC_RENDER_ON_DEMAND
    };

    return rcRunApp(&opts);
}
