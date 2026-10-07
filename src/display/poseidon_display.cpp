/*
 * Dual-screen Cardputer Adv — menu on the internal ST7789, features
 * on the external ILI9341. Built for real only when POSEIDON_DUAL_SCREEN=1.
 */
#include "display/poseidon_display.h"

#if POSEIDON_DUAL_SCREEN

#include "theme.h"
#include "ui_ambient.h"
#include <string.h>

/* Defined in ui.cpp. Invalidating here keeps the status-bar cache from
 * skipping the first paint after a surface switch. */
extern void ui_status_invalidate(void);
/* Argus skips pushImage when mood/x/y are unchanged. A content clear
 * wipes the face, so the next feature entry must force a fresh push. */
extern void argus_invalidate(void);

LGFX_ILI9341 g_ext_display;
static bool s_ext_ok = false;
static poseidon_surface_t s_surface = POSEIDON_SURFACE_MENU;
static poseidon_surface_t s_surface_restore = POSEIDON_SURFACE_MENU;

/* Companion-card cache. Cleared on return to the menu surface so a
 * feature's last frame is replaced by the themed selection card. */
static const char *s_sel_parent = nullptr;
static const char *s_sel_label  = nullptr;
static const char *s_sel_hint   = nullptr;
static uint8_t     s_sel_theme  = 0xFF;

/* Shared SPI3 with the SD card. depth counts quiesce callers.
 * needs_recover is set when a paint was attempted while the bus was
 * held, or a resume arrived with no matching quiesce. No MISO on the
 * 3-wire panel, so this flag is the health bit. */
static int  s_quiesce_depth = 0;
static bool s_ext_needs_recover = false;
static bool s_ext_force_reinit = false;

/* Background bands left by the blood-sized card. Ambience repaints
 * only these. No full-frame sprite (320×240 RGB565 is 150 KB and the
 * NimBLE gate needs a 48 KB contiguous block). */
static int s_gap_y[8];
static int s_gap_h[8];
static int s_gap_n = 0;

static void selection_cache_clear(void)
{
    s_sel_parent = nullptr;
    s_sel_label  = nullptr;
    s_sel_hint   = nullptr;
    s_sel_theme  = 0xFF;
}

lgfx::LGFX_Device &poseidon_menu(void)
{
    return M5Cardputer.Display;
}

lgfx::LGFX_Device &poseidon_content(void)
{
    if (s_ext_ok) return g_ext_display;
    return M5Cardputer.Display;
}

lgfx::LGFX_Device &poseidon_disp(void)
{
    if (s_surface == POSEIDON_SURFACE_CONTENT) return poseidon_content();
    return poseidon_menu();
}

bool poseidon_dual_ok(void) { return s_ext_ok; }

poseidon_surface_t poseidon_surface(void) { return s_surface; }

void poseidon_set_surface(poseidon_surface_t surface)
{
    if (s_surface == surface) return;
    s_surface = surface;
    if (surface == POSEIDON_SURFACE_MENU) selection_cache_clear();
    ui_status_invalidate();
}

void poseidon_enter_ui(bool menu_chrome)
{
    s_surface_restore = s_surface;
    poseidon_set_surface(menu_chrome ? POSEIDON_SURFACE_MENU
                                     : POSEIDON_SURFACE_CONTENT);
    /* Full native panel, then the feature draws at 320×240. Drop any
     * clip a previous screen left behind so content cannot shrink into
     * a corner. Argus must re-push after this clear. */
    if (!menu_chrome && s_ext_ok && s_surface == POSEIDON_SURFACE_CONTENT) {
        g_ext_display.setTextDatum(top_left);
        g_ext_display.setTextWrap(false, false);
        g_ext_display.setTextSize(1);
        g_ext_display.clearClipRect();
        g_ext_display.fillScreen(theme().bg);
        argus_invalidate();
    }
}

void poseidon_while_content(void (*fn)(void))
{
    if (!fn) return;
    if (!s_ext_ok) { fn(); return; }
    poseidon_surface_t prev = s_surface;
    s_surface = POSEIDON_SURFACE_CONTENT;
    fn();
    s_surface = prev;
}

void poseidon_leave_ui(void)
{
    poseidon_set_surface(s_surface_restore);
}

int poseidon_view_w(void)
{
    if (s_surface == POSEIDON_SURFACE_CONTENT && s_ext_ok) return 320;
    return 240;
}

int poseidon_view_h(void)
{
    if (s_surface == POSEIDON_SURFACE_CONTENT && s_ext_ok) return 240;
    return 135;
}

void poseidon_lcd_quiesce(void)
{
    if (!s_ext_ok) return;
    if (s_quiesce_depth < 0) s_quiesce_depth = 0;
    if (s_quiesce_depth == 0) {
        g_ext_display.endWrite();
        g_ext_display.waitDisplay();
        digitalWrite(POSEIDON_EXT_LCD_CS, HIGH);
    }
    s_quiesce_depth++;
}

void poseidon_lcd_resume_after_bus(void)
{
    if (!s_ext_ok) return;
    /* After SD on shared SCK/MOSI: release CS only. No COLMOD / setColorDepth.
     * An extra resume (boot calls this once more after sd_mount) just
     * parks CS. It does not count as a failed wake. */
    if (s_quiesce_depth <= 0) {
        s_quiesce_depth = 0;
        digitalWrite(POSEIDON_EXT_LCD_CS, HIGH);
        return;
    }
    s_quiesce_depth--;
    if (s_quiesce_depth == 0) {
        digitalWrite(POSEIDON_EXT_LCD_CS, HIGH);
        delay(2);
    }
}

/* Fit a C string into max_px pixels of the current font. */
static void print_fit(lgfx::LGFX_Device &d, int x, int y, int max_px,
                      const char *text, uint16_t fg, uint16_t bg)
{
    if (!text || !*text || max_px < 6) return;
    char buf[72];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    while (buf[0] && d.textWidth(buf) > max_px) {
        size_t n = strlen(buf);
        if (n < 2) break;
        buf[n - 1] = '\0';
        if (n >= 2) buf[n - 2] = '.';
    }
    d.setTextColor(fg, bg);
    d.setCursor(x, y);
    d.print(buf);
}

static void ext_mark_bad(void)
{
    s_ext_needs_recover = true;
}

static void gaps_add(int y, int h)
{
    if (h < 4 || s_gap_n >= 8) return;
    if (y < 0) { h += y; y = 0; }
    if (y >= 240 || h < 4) return;
    if (y + h > 240) h = 240 - y;
    s_gap_y[s_gap_n] = y;
    s_gap_h[s_gap_n] = h;
    s_gap_n++;
}

/* Occupied bands are added top-to-bottom. Gaps are whatever is left
 * on the 240-tall panel. */
static void gaps_between(int y0, int y1)
{
    if (y1 - y0 >= 4) gaps_add(y0, y1 - y0);
}

void poseidon_ext_invalidate(void)
{
    selection_cache_clear();
    s_gap_n = 0;
}

void poseidon_ext_bus_idle(void)
{
    if (!s_ext_ok) return;
    g_ext_display.endWrite();
    g_ext_display.clearClipRect();
    digitalWrite(POSEIDON_EXT_LCD_CS, HIGH);
}

bool poseidon_ext_recover_if_needed(void)
{
    if (!s_ext_needs_recover) return false;
    if (s_quiesce_depth != 0) return false;
    s_ext_needs_recover = false;
    s_ext_force_reinit = true;
    bool ok = poseidon_dual_begin();
    selection_cache_clear();
    s_gap_n = 0;
    return ok;
}

void poseidon_ext_ambient_gaps(void)
{
    if (!s_ext_ok || s_gap_n <= 0) return;
    if (s_quiesce_depth != 0) {
        ext_mark_bad();
        return;
    }
    auto &d = g_ext_display;
    poseidon_surface_t prev = s_surface;
    s_surface = POSEIDON_SURFACE_CONTENT;
    for (int i = 0; i < s_gap_n; ++i) {
        int y = s_gap_y[i];
        int h = s_gap_h[i];
        /* ui_matrix_rain paints a T_BG box behind every glyph, so a
         * "skip the chrome" test is not enough. The clip is what keeps
         * those boxes off the text. Cleared before the next band. */
        d.setClipRect(0, y, 320, h);
        d.fillRect(0, y, 320, h, theme().bg);
        ui_ambient_tick(0, 0, 320, 240);
        d.clearClipRect();
    }
    s_surface = prev;
}

void poseidon_ext_paint_card(const char *parent, const char *title,
                             const char *hint, const char *body)
{
    if (!s_ext_ok) return;
    if (s_quiesce_depth != 0) {
        ext_mark_bad();
        return;
    }

    auto &d = g_ext_display;
    const int W = 320;
    const uint16_t bg = theme().bg;
    const uint16_t fg = theme().fg;
    const uint16_t accent = theme().accent;
    const uint16_t accent2 = theme().accent2;
    const uint16_t dim = theme().dim;
    const uint16_t status_bg = theme().status_bg;

    /* Close a stuck CPU-SPI transaction (dma is off) so this paint
     * re-asserts CS. A CS rise mid-RAMWR leaves the ILI9341 ignoring
     * later writes while the backlight stays on. */
    d.endWrite();
    d.clearClipRect();
    d.startWrite();
    d.setTextDatum(top_left);
    d.setTextWrap(false, false);
    d.setTextSize(1);
    d.fillScreen(bg);

    d.fillRect(0, 0, W, 26, status_bg);
    d.fillRect(0, 26, W, 2, accent2);
    d.setTextSize(2);
    d.setTextColor(accent, status_bg);
    d.setCursor(10, 6);
    d.print("POSEIDON");
    d.setTextSize(1);
    print_fit(d, 150, 9, W - 160, parent ? parent : "", accent2, status_bg);

    d.setTextColor(dim, bg);
    d.setCursor(12, 48);
    d.print("CONTENT");

    d.setTextSize(2);
    print_fit(d, 12, 78, W - 24, title ? title : "", fg, bg);

    d.setTextSize(1);
    d.setTextColor(accent, bg);
    d.drawFastHLine(12, 108, W - 24, accent);
    print_fit(d, 12, 120, W - 24, hint ? hint : "", dim, bg);

    /* Chrome bands, top to bottom. No footer line. */
    s_gap_n = 0;
    int covered = 136;
    gaps_between(28, 44);
    gaps_between(60, 74);
    gaps_between(100, 104);

    if (body && *body) {
        const char *p = body;
        int y = 140;
        while (*p && y < 232) {
            int take = 0, last_space = -1;
            while (p[take] && take < 48) {
                if (p[take] == ' ') last_space = take;
                take++;
            }
            if (p[take] && last_space > 0) take = last_space;
            char line[52];
            if (take > 48) take = 48;
            strncpy(line, p, take);
            line[take] = '\0';
            d.fillRect(0, y - 1, W, 12, bg);
            d.setTextColor(fg, bg);
            d.setCursor(12, y);
            d.print(line);
            gaps_between(covered, y - 1);
            covered = y + 11;
            y += 12;
            p += take;
            if (*p == ' ') p++;
        }
    }
    gaps_between(covered, 240);
    d.setTextSize(1);
    d.endWrite();
}

void poseidon_content_show_selection(const char *parent,
                                     const char *label,
                                     const char *hint)
{
    if (!s_ext_ok) return;
    /* A feature owns the external panel. Leave its frame alone. */
    if (s_surface == POSEIDON_SURFACE_CONTENT) return;

    if (poseidon_ext_recover_if_needed())
        selection_cache_clear();
    if (!s_ext_ok) return;

    uint8_t theme_id = (uint8_t)theme_current_id();
    if (parent == s_sel_parent && label == s_sel_label &&
        hint == s_sel_hint && theme_id == s_sel_theme) {
        return;
    }
    s_sel_parent = parent;
    s_sel_label  = label;
    s_sel_hint   = hint;
    s_sel_theme  = theme_id;

    /* Every theme, including E-INK (index 2, ambient no-op, bg 0xFFFF)
     * and BLOOD (index 5, ambient no-op). The old branch sent painting
     * themes through a full-frame tick plus a 22 px caption and sent
     * no-op themes through this card only, so E-INK's idle path never
     * touched the panel. */
    poseidon_ext_paint_card(parent, label, hint, nullptr);
}

void poseidon_content_ambient_tick(void)
{
    if (!s_ext_ok) return;
    if (s_surface == POSEIDON_SURFACE_CONTENT) return;
    /* Do not fillScreen and do not redraw the card. Gaps only. */
    poseidon_ext_ambient_gaps();
}

bool poseidon_dual_begin(void)
{
    if (s_ext_ok && !s_ext_force_reinit) return true;
    s_ext_force_reinit = false;

    /* Power rail settle after M5Cardputer.begin(). */
    delay(100);

    /* Boot-only color depth BEFORE begin — never mid-run. */
    g_ext_display.setColorDepth(16);
    if (!g_ext_display.begin()) {
        Serial.println("[dual] ILI9341 begin() failed — content falls back to ST7789");
        s_ext_ok = false;
        pinMode(POSEIDON_EXT_LCD_CS, OUTPUT);
        digitalWrite(POSEIDON_EXT_LCD_CS, HIGH);
        return false;
    }
    delay(50);
    /* Landscape 320×240 (panel memory is 240×320, offset_rotation 4). */
    g_ext_display.setRotation(3);
    /* Leave _swapBytes false, matching the internal ST7789. Argus and
     * the other RGB565 sprites are already byte-swapped for that path.
     * setSwapBytes(true) here double-swaps them and the face turns to
     * color noise. */
    g_ext_display.setTextDatum(top_left);
    g_ext_display.setTextWrap(false, false);
    g_ext_display.setTextSize(1);
    g_ext_display.fillScreen(0x0000);

    s_ext_ok = true;
    Serial.printf("[dual] ILI9341 OK %dx%d — content on EXT, menu on INT "
                  "(SPI3 CS5 DC6 RST3)\n",
                  (int)g_ext_display.width(), (int)g_ext_display.height());
    return true;
}

#endif /* POSEIDON_DUAL_SCREEN */

#if !POSEIDON_DUAL_SCREEN
/* Dual-screen bodies live behind POSEIDON_DUAL_SCREEN; header provides inlines. */
void poseidon_display_anchor(void) {}
#endif
