/* ============================================================================
 *  ex23 - Issue Inbox   |   one file, desktop + web, no assets, no #ifdef
 *
 *  The rail / list / detail shape every desktop app is built on, over 100,000
 *  issues. Type in the filter and the whole list re-selects; scroll it and
 *  about thirty elements are ever declared. Four ideas carry it:
 *
 *    1. rcVirtualList declares the VIEWPORT, not the dataset - so drawing
 *       costs the same at 100 rows and at 100,000.
 *    2. Filtering permutes an index array. No record is copied or reordered,
 *       so the rail, the list and the detail pane cannot disagree.
 *    3. The scan runs on a CHANGE, not a frame. The rail prints its rebuild
 *       counter: drag the window and watch it not move.
 *    4. A row stores no strings - 20 bytes of indices into shared tables, with
 *       the title composed where it is drawn.
 *
 *  Build target: rayclay_ex23_issue_inbox
 * ========================================================================= */
#include "rayclay.h"
#include "icons/rc_icons_rayclay_logo.h"
#include "icons/rc_icons_folder.h"
#include "icons/rc_icons_panel_left.h"
#include "icons/rc_icons_panel_right.h"
#include "icons/rc_icons_minimize.h"
#include "icons/rc_icons_maximize.h"
#include "icons/rc_icons_shrink.h"   /* the restore glyph, when the window IS maximised */
#include "icons/rc_icons_x.h"

enum { F_MICRO = 0, F_SMALL, F_BODY, F_TITLE, F_COUNT };
enum { ISSUES = 100000, ROW_H = 34, BAR_H = 46, QCAP = 48, VIEWS = 4 };
enum { V_INBOX = 0, V_MINE, V_MENTION, V_CLOSED };

static const char *const VIEW[VIEWS] = { "Inbox", "Assigned to me", "Mentions", "Closed" };
static const char *const PRIO[4]     = { "P0", "P1", "P2", "P3" };
static const char *const ACTION[] = {
    "Crash", "Regression", "Flicker", "Memory leak", "Deadlock", "Race condition", "Wrong colour",
    "Missing focus ring", "Slow scroll", "Stale cache", "Off-by-one", "Dropped keystroke",
    "Broken tab order", "Truncated label", "Silent failure", "Timeout", "Layout jump",
    "Lost selection", "Stuck spinner", "Wrong sort order", "Blank state", "Frozen pointer" };
static const char *const OBJECT[] = {
    "the sidebar", "the command palette", "the search index", "the virtual list",
    "the settings sheet", "the export dialog", "the markdown parser", "the sync queue",
    "the offline cache", "the notification tray", "the keyboard router", "the theme loader",
    "the plugin host", "the undo stack", "the clipboard bridge", "the file watcher",
    "the diff viewer", "the autosave timer", "the login flow", "the emoji picker",
    "the thread view", "the draft store", "the table renderer", "the update checker" };
static const char *const AREA[] = { "Editor", "Sync", "Renderer", "Platform", "Search", "Mobile",
                                    "Desktop", "Web" };
static const char *const WHO[]  = { "amara", "devon", "hiro", "iris", "jonas", "leah", "mateo",
                                    "nadia", "omar", "priya", "quinn", "rafael", "sana", "theo" };
#define N(a) ((int)(sizeof (a) / sizeof (a)[0]))

typedef struct { uint32_t num; uint16_t verb, noun, area, age, comments;
                 uint8_t prio, who, open, flags; } Issue;
enum { F_MINE = 1, F_MENTION = 2 };

typedef struct {
    Issue   issue[ISSUES];
    int32_t sel[ISSUES];                 /* indices that pass, in list order  */
    int     count, perView[VIEWS], rebuilds;
    char    countText[VIEWS][12];
    int     view, selRow, selIssue;
    bool    prio[4], railOpen, dirty;
    char    query[QCAP];
} AppState;

/* ---- strings. rayclay.h pulls in no libc, so the two things this app needs
   that printf would otherwise give it are done by hand.
   Note: element ids are not one of them - rcFormat's result is promised
   NUL-terminated, so `.id = rcFormat(m, "r%d", i).chars` is the supported
   idiom and needs no helper here. ------------------------------------------- */
static int cat(char *b, int cap, int at, const char *s)
{
    while (*s && at < cap - 1) b[at++] = *s++;
    if (cap > 0) b[at] = '\0';
    return at;
}
/* Grouped in threes: 61893 -> "61,893". printf has no portable grouping flag
   (the ' flag is POSIX; MSVC omits it), so it is done here. */
static int cat_grouped(char *b, int cap, int at, unsigned v)
{
    char t[16];
    int  n = 0;
    do {
        if (n && n % 4 == 3) t[n++] = ',';
        t[n++] = (char)('0' + v % 10u);
    } while ((v /= 10u) && n < (int)sizeof t);
    while (n > 0 && at < cap - 1) b[at++] = t[--n];
    if (cap > 0) b[at] = '\0';
    return at;
}
static RC_String num(RC_Arena *m, int v, const char *suffix)
{
    char b[16];
    cat_grouped(b, (int)sizeof b, 0, (unsigned)(v < 0 ? 0 : v));
    return rcFormat(m, "%s%s", b, suffix);
}
static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

/* `needle` is pre-folded once per rebuild, not once per row - the difference
   between a filter that keeps up with typing and one that does not. */
static bool has(const char *hay, const char *needle)
{
    int i, j;
    if (!needle[0]) return true;
    for (i = 0; hay[i]; i++) {
        for (j = 0; needle[j] && lower(hay[i + j]) == needle[j]; j++) ;
        if (!needle[j]) return true;
    }
    return false;
}
static void title_of(const Issue *it, char *b, int cap)
{
    cat(b, cap, cat(b, cap, cat(b, cap, 0, ACTION[it->verb]), " in "), OBJECT[it->noun]);
}

/* ---- data. One seed -> the same 100,000 issues on every machine, which is
   what makes a screenshot comparable across platforms.
   Note: returns the high 16 bits. An LCG's low bits have a very short period, so
   `rand() % n` on one visibly repeats - two fields drawn from the same word
   come out correlated, and the list fills with adjacent duplicate titles. */
static uint32_t nxt(uint32_t *s)
{
    return (*s = *s * 1664525u + 1013904223u) >> 16;
}
static void seed(AppState *st, uint32_t s)
{
    int i;
    for (i = 0; i < ISSUES; i++) {
        Issue   *it = &st->issue[i];
        uint32_t r;

        it->num      = (uint32_t)(ISSUES - i) + 1000u;
        it->verb     = (uint16_t)(nxt(&s) % (unsigned)N(ACTION));
        it->noun     = (uint16_t)(nxt(&s) % (unsigned)N(OBJECT));
        it->area     = (uint16_t)(nxt(&s) % (unsigned)N(AREA));
        it->who      = (uint8_t)(nxt(&s) % (unsigned)N(WHO));
        it->age      = (uint16_t)(nxt(&s) % 900u);
        it->comments = (uint16_t)(nxt(&s) % 64u);
        /* Skewed, as a real backlog is: a uniform draw would put 25,000 fires
           in the inbox and make the priority filter meaningless. */
        r = nxt(&s) % 100u;
        it->prio  = (uint8_t)(r < 3u ? 0 : r < 15u ? 1 : r < 55u ? 2 : 3);
        it->open  = (uint8_t)(nxt(&s) % 100u < 62u);
        it->flags = (uint8_t)((nxt(&s) % 100u < 9u ? F_MINE : 0u) |
                              (nxt(&s) % 100u < 6u ? F_MENTION : 0u));
    }
}
static bool in_view(const Issue *it, int v)
{
    if (v == V_CLOSED)  return !it->open;
    if (v == V_MINE)    return it->open && (it->flags & F_MINE);
    if (v == V_MENTION) return it->open && (it->flags & F_MENTION);
    return it->open != 0;
}

/* The ONE O(issues) pass, and it runs on a keystroke - never on a frame. It
   also carries the selection across BY IDENTITY, so the row you were reading
   keeps its highlight if it survives the new filter. */
static void rebuild(AppState *st)
{
    char q[QCAP], t[96];
    bool anyPrio = st->prio[0] || st->prio[1] || st->prio[2] || st->prio[3];
    int  i, v;

    for (i = 0; i < QCAP - 1 && st->query[i]; i++) q[i] = lower(st->query[i]);
    q[i] = '\0';
    for (v = 0; v < VIEWS; v++) st->perView[v] = 0;

    st->count = 0;
    for (i = 0; i < ISSUES; i++) {
        const Issue *it = &st->issue[i];

        for (v = 0; v < VIEWS; v++) if (in_view(it, v)) st->perView[v]++;
        if (!in_view(it, st->view) || (anyPrio && !st->prio[it->prio])) continue;
        if (q[0]) {
            title_of(it, t, (int)sizeof t);
            if (!has(t, q) && !has(WHO[it->who], q) && !has(AREA[it->area], q)) continue;
        }
        st->sel[st->count++] = (int32_t)i;
    }
    st->rebuilds++;
    for (v = 0; v < VIEWS; v++) cat_grouped(st->countText[v], 12, 0, (unsigned)st->perView[v]);

    st->selRow = -1;
    for (i = 0; st->selIssue >= 0 && i < st->count; i++)
        if (st->sel[i] == st->selIssue) { st->selRow = i; break; }
    if (st->selRow < 0) {
        st->selRow   = st->count ? 0 : -1;
        st->selIssue = st->count ? (int)st->sel[0] : -1;
    }
    st->dirty = false;
}
/* Keep the keyboard selection on screen.

   Load-bearing: with a virtual list this is not cosmetic. rcVirtualList only
   declares the rows in the visible window, so a selection that walks out of it
   is not merely off-centre - nothing draws it at all, while the detail pane
   keeps changing for a row the user can no longer see. Arrow keys move the
   selection; only this moves the viewport.

   The viewport height is not directly readable, but it falls out of the
   travel: maxOffsetY is content-minus-viewport, so viewport = content - travel.
   When the content fits, maxOffsetY is 0 and this collapses to "never scroll",
   which is the right answer. */
static void reveal_selected(AppState *st)
{
    RC_ScrollInfo si = rcGetScrollInfo("list");
    float contentH, viewH, top, bottom;

    if (!si.found || st->selRow < 0) return;

    contentH = (float)st->count * (float)ROW_H;
    viewH    = contentH - si.maxOffsetY;
    top      = (float)st->selRow * (float)ROW_H;
    bottom   = top + (float)ROW_H;

    /* Minimum movement, so a row that is already visible never lurches the list.
       rcScrollBy is positive-DOWN, like the DOM's element.scrollBy. */
    if (top < si.offsetY)
        rcScrollBy("list", 0.0f, top - si.offsetY);
    else if (bottom > si.offsetY + viewH)
        rcScrollBy("list", 0.0f, bottom - (si.offsetY + viewH));
}

static void move(AppState *st, int step)
{
    int n = st->selRow < 0 ? 0 : st->selRow + step;
    if (!st->count) return;
    st->selRow   = n < 0 ? 0 : n > st->count - 1 ? st->count - 1 : n;
    st->selIssue = (int)st->sel[st->selRow];
    reveal_selected(st);
}
static RC_Color prio_color(int p)
{
    RC_Style s = rcGetStyle();
    return p == 0 ? s.danger : p == 1 ? s.warning : p == 2 ? s.primary : s.textMuted;
}

/* ---- chrome. .titlebar.custom means the runner draws nothing and this band
   IS the titlebar; RC_ID_WINDOW_DRAG moves the window, and an interactive
   child opts back out (the desktop twin of -webkit-app-region: no-drag). --- */
static void titlebar(RC_App *app, AppState *st)
{
    RC_Style s = rcGetStyle();
    RC_Color tint = rcIsHovered("panel") ? s.text : s.textMuted;

    /* Chrome, not content: RC_AppOptions.titlebarHeight freezes the OS drag
       strip in physical px, so a band that grew with the content zoom would
       stop matching the strip the OS lets you drag. Measured before this
       existed: at 2x zoom the drawn band was exactly twice the draggable one. */
    rcUnzoomed() {
        rcRow(.id = RC_ID_WINDOW_DRAG, .w = "grow", .hType = RC_PX(BAR_H), .bg = s.chrome,
               .px = 12, .gap = 10, .align = "cl") {
            rcIconRayClayLogo(22.0f);
            rcColumn(.gap = 0) {
                rcTextL("Inbox", .font = F_BODY, .color = s.text);
                rcTextL("RayClay " RC_VERSION " \xc2\xb7 one source, every target",
                         .font = F_MICRO, .color = s.textMuted);
            }
            rcBox(.w = "grow") {}
            rcText(num(rcAppArena(app), st->perView[V_INBOX], " open"), .font = F_SMALL,
                    .color = s.textMuted);
            rcRow(.id = RC_ID_WINDOW_NODRAG, .gap = 6, .align = "cc") {
                /* The icon story in one control: the SAME callback takes a colour
                   at draw time, so hover is a different argument next frame. */
                rcBox(.id = "panel", .w = "30px", .h = "26px", .align = "cc", .borderRadius = "all-md",
                       .bg = rcAlpha(s.border, rcIsHovered("panel") ? 120 : 0)) {
                    if (st->railOpen) rcIconPanelLeft(15.0f, tint); else rcIconPanelRight(15.0f, tint);
                }
                if (rcClicked("panel")) st->railOpen = !st->railOpen;
            }
            rcRow(.gap = 2, .align = "cc") {
                rcWindowControlButton(RC_WINCTL_MINIMIZE, rcIconMinimize, 14.0f);
                /* The middle chip does maximise OR restore - one id, two actions - so
                   the glyph has to say which. rcIsWindowMaximized answers false on
                   web, where nothing maximises, so this needs no #ifdef. */
                rcWindowControlButton(RC_WINCTL_MAXIMIZE,
                                      rcIsWindowMaximized() ? rcIconShrink : rcIconMaximize,
                                      14.0f);
                rcWindowControlButton(RC_WINCTL_CLOSE,    rcIconX,        14.0f);
            }
        }
    }
}

static void rail(RC_App *app, AppState *st)
{
    static const char *const ID[VIEWS] = { "v0", "v1", "v2", "v3" };
    static const char *const PID[4]    = { "p0", "p1", "p2", "p3" };
    RC_Style s = rcGetStyle();
    char     total[16];
    int      i;

    rcColumn(.id = "rail", .wType = RC_PX(228), .h = "grow", .p = 12, .gap = 6, .bg = s.surface) {
        rcTextL("VIEWS", .font = F_MICRO, .color = s.textMuted);
        for (i = 0; i < VIEWS; i++) {
            bool     on = st->view == i;
            RC_Color fg = on ? s.text : s.textMuted;

            rcRow(.id = ID[i], .w = "grow", .hType = RC_PX(32), .px = 10, .gap = 9,
                   .align = "cl", .borderRadius = "all-md",
                   .bg = on ? rcAlpha(s.primary, 60) : rcAlpha(s.border, rcIsHovered(ID[i]) ? 90 : 0)) {
                if (i == V_INBOX) rcIconFolder(15.0f, fg);
                else rcBox(.w = "9px", .h = "9px", .borderRadius = "all-full",
                            .bg = on ? s.primary : s.border) {}
                rcTextC(VIEW[i], .font = F_SMALL, .color = fg);
                rcBox(.w = "grow") {}
                rcTextC(st->countText[i], .font = F_MICRO, .color = s.textMuted);
            }
            if (rcClicked(ID[i]) && st->view != i) { st->view = i; st->dirty = true; }
        }
        rcBox(.h = "10px") {}
        rcTextL("PRIORITY", .font = F_MICRO, .color = s.textMuted);
        for (i = 0; i < 4; i++) {
            rcRow(.w = "grow", .gap = 8, .align = "cl") {
                rcBox(.w = "8px", .h = "8px", .borderRadius = "all-full", .bg = prio_color(i)) {}
                if (rcCheckbox(PID[i], PRIO[i], &st->prio[i])) st->dirty = true;
            }
        }
        rcBox(.h = "grow") {}
        cat_grouped(total, (int)sizeof total, 0, (unsigned)ISSUES);
        rcText(rcFormat(rcAppArena(app), "%s issues \xc2\xb7 rebuild #%d", total, st->rebuilds),
                .font = F_MICRO, .color = s.textMuted);
        rcTextL("drag the window - it does not move", .font = F_MICRO, .color = s.textMuted);
    }
}

/* ---- the list. 100,000 rows, about thirty declared. ---------------------- */
static void list(RC_App *app, AppState *st)
{
    RC_Style  s = rcGetStyle();
    RC_Arena *m = rcAppArena(app);

    rcRow(.w = "grow", .hType = RC_PX(52), .px = 14, .gap = 10, .align = "cl", .bg = s.surface) {
        rcBox(.w = "grow") {
            /* Note: ASCII "..." not U+2026 - the bundled face bakes ASCII + Latin-1
               (RC_FONT_LAST_CODEPOINT defaults to 255), so an ellipsis or an em
               dash draws as a replacement glyph. Widen the range or stay in it -
               but decide, because the failure is silent. */
            if (rcTextInput("q", st->query, sizeof st->query, .font = F_SMALL,
                             .placeholder = "Filter 100,000 issues..."))
                st->dirty = true;
        }
        if (st->query[0]) {
            rcBox(.id = "clr", .w = "26px", .h = "26px", .align = "cc", .borderRadius = "all-md",
                   .bg = rcAlpha(s.border, rcIsHovered("clr") ? 120 : 0)) {
                rcIconX(13.0f, rcIsHovered("clr") ? s.text : s.textMuted);
            }
            if (rcClicked("clr")) { st->query[0] = '\0'; st->dirty = true; }
        }
        rcText(num(m, st->count, " shown"), .font = F_SMALL, .color = s.textMuted);
    }
    rcBox(.w = "grow", .hType = RC_PX(1), .bg = s.border) {}

    rcColumn(.id = "list", .w = "grow", .h = "grow", .scroll = "v", .bg = s.background) {
        if (!st->count)
            rcColumn(.w = "grow", .p = 40, .gap = 6, .align = "tc") {
                rcTextL("Nothing matches that filter.", .font = F_BODY, .color = s.textMuted);
            }
        rcVirtualList(row, "list", st->count, (float)ROW_H) {
            const Issue *it = &st->issue[st->sel[row.index]];
            /* rcFormat's result is NUL-terminated, so .chars is a
               valid const char * and is legal as an id. Arena memory: good for
               an id, which is hashed as it is used, and only for this frame. */
            const char  *id = rcFormat(m, "r%d", row.index).chars;
            bool         on = row.index == st->selRow;

            rcRow(.id = id, .w = "grow", .hType = RC_PX(ROW_H), .px = 14, .gap = 10, .align = "cl",
                   .bg = on ? rcAlpha(s.primary, 70)
                            : rcAlpha(s.border, rcIsHovered(id) ? 80 : (row.index & 1) ? 24 : 0)) {
                rcBox(.w = "8px", .h = "8px", .borderRadius = "all-full",
                       .bg = prio_color(it->prio)) {}
                rcText(rcFormat(m, "#%u", it->num), .font = F_MICRO, .color = s.textMuted);
                rcText(rcFormat(m, "%s in %s", ACTION[it->verb], OBJECT[it->noun]),
                        .font = F_SMALL, .color = it->open ? s.text : s.textMuted);
                rcBox(.w = "grow") {}
                rcBox(.px = 7, .py = 2, .borderRadius = "all-full", .bg = rcAlpha(s.border, 110)) {
                    rcTextC(AREA[it->area], .font = F_MICRO, .color = s.textMuted);
                }
                rcBox(.w = "84px") { rcText(rcFormat(m, "@%s", WHO[it->who]), .font = F_MICRO,
                                             .color = s.textMuted); }
                rcBox(.w = "56px") { rcText(rcFormat(m, "%ud", (unsigned)it->age),
                                             .font = F_MICRO, .color = s.textMuted); }
            }
            if (rcClicked(id)) { st->selRow = row.index; st->selIssue = (int)st->sel[row.index]; }
        }
    }
}

static void field(const char *label, RC_String value, RC_Color fg)
{
    RC_Style s = rcGetStyle();
    rcRow(.w = "grow", .gap = 10, .align = "cl") {
        rcBox(.w = "92px") { rcTextC(label, .font = F_MICRO, .color = s.textMuted); }
        rcText(value, .font = F_SMALL, .color = fg);
    }
}

/* Do not `return` early out of a container body: the brace body is a
   macro-generated loop, so a return skips the close and you get a completed
   frame that is the wrong frame. The empty state is an else branch. */
static void detail(RC_App *app, AppState *st)
{
    RC_Style  s = rcGetStyle();
    RC_Arena *m = rcAppArena(app);
    Issue    *it;

    rcColumn(.id = "detail", .wType = RC_PX(360), .h = "grow", .p = 18, .gap = 12,
              .scroll = "v", .bg = s.surface) {
        if (st->selIssue < 0) {
            rcTextL("No issue selected.", .font = F_BODY, .color = s.textMuted);
        } else {
            it = &st->issue[st->selIssue];
            rcRow(.w = "grow", .gap = 8, .align = "cl") {
                rcBox(.px = 8, .py = 3, .borderRadius = "all-full",
                       .bg = it->open ? rcAlpha(s.success, 70) : rcAlpha(s.border, 120)) {
                    rcTextC(it->open ? "Open" : "Closed", .font = F_MICRO,
                             .color = it->open ? s.success : s.textMuted);
                }
                rcText(rcFormat(m, "#%u", it->num), .font = F_MICRO, .color = s.textMuted);
            }
            rcText(rcFormat(m, "%s in %s", ACTION[it->verb], OBJECT[it->noun]),
                    .font = F_TITLE, .color = s.text);
            rcBox(.w = "grow", .hType = RC_PX(1), .bg = s.border) {}
            field("Priority", rcStringFromCStr(PRIO[it->prio]), prio_color(it->prio));
            field("Area",     rcStringFromCStr(AREA[it->area]), s.text);
            field("Assignee", rcStringFromCStr(WHO[it->who]),   s.text);
            field("Age",      rcFormat(m, "%u days", (unsigned)it->age), s.text);
            field("Comments", rcFormat(m, "%u", (unsigned)it->comments), s.text);
            rcBox(.w = "grow", .hType = RC_PX(1), .bg = s.border) {}
            rcText(rcFormat(m, "Reported against %s. Reproduced on desktop and in the browser "
                                "from one source.", AREA[it->area]),
                    .font = F_SMALL, .color = s.textMuted);
            rcBox(.h = "grow") {}
            /* One mutation, one invalidation - which is why the rail counts,
               the list and this pane cannot disagree about what just changed. */
            if (rcButton("act", it->open ? "Close issue" : "Reopen",
                          it->open ? RC_BTN_PRIMARY : RC_BTN_DEFAULT)) {
                it->open  = (uint8_t)!it->open;
                st->dirty  = true;
            }
        }
    }
}

static void update(RC_App *app, void *userData)
{
    AppState *st = (AppState *)userData;
    (void)app;
    if (rcKeyPressed(RC_KEY_DOWN)) move(st, 1);
    if (rcKeyPressed(RC_KEY_UP))   move(st, -1);
    if (rcKeyPressed(RC_KEY_ESCAPE) && st->query[0]) { st->query[0] = '\0'; st->dirty = true; }
    if (st->dirty) rebuild(st);
}

static void layout(RC_App *app, void *userData)
{
    AppState *st = (AppState *)userData;
    RC_Style s = rcGetStyle();

    rcColumn(.id = "root", .w = "grow", .h = "grow", .bg = s.background) {
        titlebar(app, st);
        /* Pane rules are 1px boxes: RC_Border takes ONE all-sides width, so
           "a line down this edge only" is a sibling, not a property. */
        rcRow(.w = "grow", .h = "grow") {
            if (st->railOpen) {
                rail(app, st);
                rcBox(.wType = RC_PX(1), .h = "grow", .bg = s.border) {}
            }
            rcColumn(.w = "grow", .h = "grow") { list(app, st); }
            rcBox(.wType = RC_PX(1), .h = "grow", .bg = s.border) {}
            detail(app, st);
        }
    }

    /* A scroll container gets no bar unless you ask for one - rayclay.h calls
       that a requirement, not a suggestion. Both of these needed it: the list is
       100,000 rows tall and the detail pane clips long text, and without a bar
       neither shows a position or accepts a drag, only the wheel. Declared
       outside the root so they float over it, as ex03/ex04/ex05 do, and after
       every rcScrollBy on the same container, which is the order rayclay.h
       requires. */
    rcScrollbar("list");
    rcScrollbar("detail");

    /* Every st->dirty site is inside LAYOUT, and rebuild() runs from update(),
       which the runner calls BEFORE layout (rc_app.c:2097 against :2129). So a
       click here is serviced on the NEXT frame - and this app is
       RC_RENDER_ON_DEMAND, so without this request there may BE no next frame
       until unrelated input arrives, leaving the rail counts, the list and the
       detail pane disagreeing with the control that just changed them. One
       request at the end covers all five sites, and any added later. */
    if (st->dirty)
        rcAppRequestFrame(app);
}

int main(void)
{
    static AppState state;
    float sizes[F_COUNT];

    sizes[F_MICRO] = 11.0f; sizes[F_SMALL] = 13.0f; sizes[F_BODY] = 15.0f; sizes[F_TITLE] = 21.0f;
    rcSetStyle(rcStyleDark());
    seed(&state, 0x1B0C51EDu);
    state.railOpen = true;
    state.selIssue = state.selRow = -1;
    rebuild(&state);

    RC_AppOptions opts = {
        .width = 1280, .height = 800, .title = "RayClay Issue Inbox",
        .clearColor = rcGetStyle().background,
        .fontSizes = sizes, .fontCount = F_COUNT,
        .scratchArenaBytes = 32768,          /* backs every rcFormat in a frame */
        .nativeFrame = true, .titlebarHeight = BAR_H,
        .titlebar = { .custom = true },      /* the band above IS the titlebar  */
        .updateCallback = update, .layoutCallback = layout, .userData = &state,
        .renderMode = RC_RENDER_ON_DEMAND,   /* parks between keystrokes        */
    };
    return rcRunApp(&opts);
}
