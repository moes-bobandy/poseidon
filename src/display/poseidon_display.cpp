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
    g_ext_display.endWrite();
    g_ext_display.waitDisplay();
    digitalWrite(POSEIDON_EXT_LCD_CS, HIGH);
}

void poseidon_lcd_resume_after_bus(void)
{
    if (!s_ext_ok) return;
    /* After SD on shared SCK/MOSI: release CS only. No COLMOD / setColorDepth. */
    digitalWrite(POSEIDON_EXT_LCD_CS, HIGH);
    delay(2);
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

void poseidon_content_show_selection(const char *parent,
                                     const char *label,
                                     const char *hint)
{
    if (!s_ext_ok) return;
    /* A feature owns the external panel. Leave its frame alone. */
    if (s_surface == POSEIDON_SURFACE_CONTENT) return;

    uint8_t theme_id = (uint8_t)theme_current_id();
    if (parent == s_sel_parent && label == s_sel_label &&
        hint == s_sel_hint && theme_id == s_sel_theme) {
        return;
    }
    s_sel_parent = parent;
    s_sel_label  = label;
    s_sel_hint   = hint;
    s_sel_theme  = theme_id;

    /* Moving themes paint the external panel themselves. A static card
     * would cover that motion. */
    if (ui_ambient_paints()) {
        poseidon_content_ambient_tick();
        return;
    }

    auto &d = g_ext_display;
    const int W = 320;
    const int H = 240;
    const uint16_t bg = theme().bg;
    const uint16_t fg = theme().fg;
    const uint16_t accent = theme().accent;
    const uint16_t accent2 = theme().accent2;
    const uint16_t dim = theme().dim;
    const uint16_t status_bg = theme().status_bg;
    const uint16_t footer_bg = theme().footer_bg;

    d.setTextDatum(top_left);
    d.setTextWrap(false, false);
    d.fillScreen(bg);

    /* Same palette as the menu — status, accent rule, footer. */
    d.fillRect(0, 0, W, 26, status_bg);
    d.fillRect(0, 26, W, 2, accent2);
    d.setTextSize(2);
    d.setTextColor(accent, status_bg);
    d.setCursor(10, 6);
    d.print("POSEIDON");
    d.setTextSize(1);
    print_fit(d, 150, 9, W - 160, parent ? parent : "", accent2, status_bg);

    d.setTextSize(1);
    d.setTextColor(dim, bg);
    d.setCursor(12, 48);
    d.print("CONTENT");

    d.setTextSize(2);
    print_fit(d, 12, 78, W - 24, label ? label : "", fg, bg);

    d.setTextSize(1);
    d.setTextColor(accent, bg);
    d.drawFastHLine(12, 108, W - 24, accent);
    print_fit(d, 12, 120, W - 24, hint ? hint : "", dim, bg);

    d.fillRect(0, H - 22, W, 22, footer_bg);
    d.drawFastHLine(0, H - 22, W, theme().rule);
    d.setTextColor(dim, footer_bg);
    d.setCursor(10, H - 14);
    d.print("menu on internal    feature opens here");
}

static void content_caption(lgfx::LGFX_Device &d, int W, int H)
{
    const uint16_t bg = theme().footer_bg;
    const uint16_t dim = theme().dim;
    d.fillRect(0, H - 22, W, 22, bg);
    d.drawFastHLine(0, H - 22, W, theme().rule);
    d.setTextSize(1);
    d.setTextDatum(top_left);
    print_fit(d, 8, H - 14, W / 2 - 12, s_sel_label ? s_sel_label : "POSEIDON",
              theme().fg, bg);
    print_fit(d, W / 2, H - 14, W / 2 - 8, s_sel_hint ? s_sel_hint : "",
              dim, bg);
}

void poseidon_content_ambient_tick(void)
{
    if (!s_ext_ok) return;
    if (s_surface == POSEIDON_SURFACE_CONTENT) return;
    if (!ui_ambient_paints()) return;

    poseidon_surface_t prev = s_surface;
    s_surface = POSEIDON_SURFACE_CONTENT;

    auto &d = g_ext_display;
    const int W = poseidon_view_w();
    const int H = poseidon_view_h();
    d.clearClipRect();
    d.setTextWrap(false, false);
    d.setTextSize(1);
    d.fillScreen(theme().bg);
    ui_ambient_tick(0, 0, W, H - 22);
    content_caption(d, W, H);

    s_surface = prev;
}

bool poseidon_dual_begin(void)
{
    if (s_ext_ok) return true;

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
