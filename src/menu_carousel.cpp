/*
 * menu_carousel.cpp — see menu_carousel.h.
 *
 * Layout (per card):
 *
 *  [STATUS BAR ... 12 px ........... magenta divider ........]
 *  parent-name                                     N / TOTAL
 *  ===================== magenta double rule ===================
 *  +--                                                       --+
 *  |                                                           |
 *  |   ((W))   WiFi                                            |
 *  |    ===    recon + attacks                                 |
 *  |                                                           |
 *  |                                                  MENU >   |
 *  +--                                                       --+
 *   <                                                        >
 *  [FOOTER ... ;/.=swipe  ENTER=open  letter=jump  `=back ...]
 *
 * Corner brackets in T_ACCENT (cyan), small magenta accent dots inside
 * the corners, big hotkey badge in a 2-ring circle (outer ring pulses
 * on a 1.5 s sin so the focus has a heartbeat), size-2 label + size-1
 * description.
 *
 * Slide animation: when ;/. flips siblings, the new card slides in
 * from the corresponding edge over 200 ms (linear ease-out). Letter
 * mnemonics jump instantly without animation so power-users don't pay
 * for the visual cost.
 */
#include "menu_carousel.h"
#include "menu_icons.h"
#include "ui.h"
#include "ui_ambient.h"
#include "input.h"
#include "radio.h"
#include "theme.h"
#include "app.h"
#include "screensaver.h"
#include <M5Cardputer.h>
#include <Arduino.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* g_current_feature_item is owned by menu.cpp — the carousel sets it
 * the same way the terminal renderer does so feature ?-help still
 * resolves to the right node when invoked from a card. */
extern const menu_node_t *g_current_feature_item;
extern void               ui_show_current_help(void);

#define CAROUSEL_FOOTER ";/.=swipe  ENTER=open  =help  letter=jump  `=back"

static int count_children(const menu_node_t *parent)
{
    int n = 0;
    for (const menu_node_t *c = parent->children; c && c->hotkey; ++c) ++n;
    return n;
}

static int index_of(const menu_node_t *parent, char hotkey)
{
    int i = 0;
    char want = (char)tolower((int)hotkey);
    for (const menu_node_t *c = parent->children; c && c->hotkey; ++c, ++i) {
        if (c->hotkey == want) return i;
    }
    return -1;
}

/* Linear interpolation 8-bit channel blend used for the badge ring
 * pulse. Returns RGB565 where `a` and `b` are blended by t in [0..255]. */
static uint16_t blend565(uint16_t a, uint16_t b, uint8_t t)
{
    uint8_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    uint8_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    uint8_t r  = (uint8_t)((ar * (255 - t) + br * t) / 255);
    uint8_t g  = (uint8_t)((ag * (255 - t) + bg * t) / 255);
    uint8_t bl = (uint8_t)((ab * (255 - t) + bb * t) / 255);
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

/* Compute the pulse color for the badge ring at the current millis().
 * Used by both the full-paint and the lightweight idle path. */
static uint16_t pulse_color_now(void)
{
    uint32_t now   = millis();
    float    phase = (float)(now % 1500) / 1500.0f;
    float    cosw  = (1.0f - cosf(phase * 2.0f * 3.14159f)) * 0.5f;
    uint8_t  blend = (uint8_t)(cosw * 255.0f);
    return blend565(T_ACCENT, T_ACCENT2, blend);
}

/* Full card paint — every layer, every glyph, every bracket. Called on
 * cursor change, on slide animation frames, and on initial entry. */
static void draw_card_full(const menu_node_t *parent, int cursor, int slide_x)
{
    auto &d = PoseidonDisplay;
    int n = count_children(parent);
    if (n <= 0 || cursor < 0 || cursor >= n) return;
    const menu_node_t *item = &parent->children[cursor];

    /* Body background + ambient layer underneath. The carousel doesn't
     * piggyback on draw_menu's hook, so we wire ambient ourselves. */
    d.fillRect(0, BODY_Y, SCR_W, BODY_H, T_BG);
    ui_ambient_tick(0, BODY_Y, SCR_W, BODY_H);

    /* Title bar: parent name on the left, "N / TOTAL" position on the
     * right in magenta. Matches terminal-mode aesthetics. */
    d.setTextColor(T_ACCENT, T_BG);
    d.setCursor(4, BODY_Y + 2);
    d.print(parent->label);
    char pos[16];
    snprintf(pos, sizeof(pos), "%d / %d", cursor + 1, n);
    int pw = d.textWidth(pos);
    d.setTextColor(T_ACCENT2, T_BG);
    d.setCursor(SCR_W - pw - 4, BODY_Y + 2);
    d.print(pos);

    /* Magenta double-divider directly under the title — same splash
     * the terminal mode uses, full body width, 2 px thick. */
    d.drawFastHLine(4, BODY_Y + 12, SCR_W - 8, T_ACCENT2);
    d.drawFastHLine(4, BODY_Y + 13, SCR_W - 8, T_ACCENT2);

    /* Card frame box. cx/cy/cw/ch are the bracket-frame coordinates;
     * bracket arms are 8 px L-shapes at each corner. */
    int cx = 6 + slide_x;
    int cy = BODY_Y + 18;
    int cw = SCR_W - 12;
    int ch = BODY_H - 24;
    int ck = 8;

    /* 4 corner brackets in cyan. */
    d.drawFastHLine(cx,            cy,             ck, T_ACCENT);
    d.drawFastVLine(cx,            cy,             ck, T_ACCENT);
    d.drawFastHLine(cx + cw - ck,  cy,             ck, T_ACCENT);
    d.drawFastVLine(cx + cw - 1,   cy,             ck, T_ACCENT);
    d.drawFastHLine(cx,            cy + ch - 1,    ck, T_ACCENT);
    d.drawFastVLine(cx,            cy + ch - ck,   ck, T_ACCENT);
    d.drawFastHLine(cx + cw - ck,  cy + ch - 1,    ck, T_ACCENT);
    d.drawFastVLine(cx + cw - 1,   cy + ch - ck,   ck, T_ACCENT);

    /* Tiny magenta accent dots tucked just inside each corner — gives
     * the frame the "registration mark" / cyberpunk-spec-sheet feel. */
    d.drawPixel(cx + 2,        cy + 2,        T_ACCENT2);
    d.drawPixel(cx + cw - 3,   cy + 2,        T_ACCENT2);
    d.drawPixel(cx + 2,        cy + ch - 3,   T_ACCENT2);
    d.drawPixel(cx + cw - 3,   cy + ch - 3,   T_ACCENT2);

    /* Big hotkey badge — 2-ring circle with the letter centered.
     * Outer ring pulses cyan -> magenta -> cyan over 1.5 s as a focus
     * heartbeat. Inner ring stays solid cyan. */
    int  bx = cx + 22;
    int  by = cy + ch / 2;
    int  br = 14;
    uint16_t pulse = pulse_color_now();
    d.fillCircle(bx, by, br, T_SEL_BG);
    d.drawCircle(bx, by, br,     pulse);
    d.drawCircle(bx, by, br - 1, T_ACCENT);
    /* Try the icon dispatcher first — top-level POSEIDON entries get
     * their pictograph (cyan in POSEIDON theme, green in MATRIX, black
     * in E-INK because drawBitmap inherits the passed color). Submenu
     * items fall through to the big-letter rendering. */
    if (!draw_menu_icon(bx, by, T_FG, parent, item)) {
        d.setTextColor(T_FG, T_SEL_BG);
        d.setTextSize(2);
        /* Letter is 12x16 at size 2. Center it in the 28-px-diameter circle. */
        d.setCursor(bx - 6, by - 7);
        d.printf("%c", toupper(item->hotkey));
        d.setTextSize(1);
    }

    /* Big label: size 2, to the right of the badge. Truncate to fit. */
    int lx = bx + br + 10;
    int ly = cy + (ch / 2) - 14;
    d.setTextSize(2);
    d.setTextColor(T_FG, T_BG);
    d.setCursor(lx, ly);
    {
        char buf[24];
        int  max_chars = (cx + cw - 8 - lx) / 12;   /* size-2 char = 12 px wide */
        if (max_chars < 1)  max_chars = 1;
        if (max_chars > 23) max_chars = 23;
        strncpy(buf, item->label, max_chars);
        buf[max_chars] = 0;
        d.print(buf);
    }
    d.setTextSize(1);

    /* Hint: size 1, just below the label. */
    if (item->hint) {
        d.setTextColor(T_DIM, T_BG);
        d.setCursor(lx, ly + 18);
        char hbuf[64];
        int  max_hint = (cx + cw - 8 - lx) / 6;     /* size-1 char = 6 px wide */
        if (max_hint < 1)  max_hint = 1;
        if (max_hint > 63) max_hint = 63;
        strncpy(hbuf, item->hint, max_hint);
        hbuf[max_hint] = 0;
        d.print(hbuf);
    }

    /* Type indicator at bottom-right inside the card. */
    {
        const char *type = item->action ? "OPEN" : "MENU >";
        d.setTextColor(T_ACCENT, T_BG);
        int tw = d.textWidth(type);
        d.setCursor(cx + cw - tw - 6, cy + ch - 11);
        d.print(type);
    }

    /* Side scroll arrows — bright magenta when scrollable, dim otherwise. */
    int amid = cy + ch / 2 - 4;
    d.setTextColor(cursor > 0     ? T_ACCENT2 : T_DIM, T_BG);
    d.setCursor(0, amid);
    d.print("<");
    d.setTextColor(cursor < n - 1 ? T_ACCENT2 : T_DIM, T_BG);
    d.setCursor(SCR_W - 6, amid);
    d.print(">");
}

/* Lightweight idle paint — only touches the parts of the card that
 * actually change frame-to-frame: two ambient strips (above and below
 * the text band) and the pulsing outer ring of the badge. The label,
 * hint, brackets, dividers, type indicator, and scroll arrows are NOT
 * touched, so they don't strobe.
 *
 * Layout (matching draw_card_full):
 *
 *  +-                               -+    <- top corner brackets at cy
 *  | [ambient zone above text]       |    <- amb_top strip
 *  |                                  |
 *  |  ((W))   WIFI                   |    <- TEXT BAND — never repainted
 *  |   ===    recon + attacks        |       in idle path
 *  |                                  |
 *  | [ambient zone below text]       |    <- amb_bot strip
 *  |                       MENU >    |
 *  +-                               -+    <- bottom corner brackets
 */
static void draw_card_anim(const menu_node_t *parent, int cursor)
{
    auto &d = PoseidonDisplay;
    int n = count_children(parent);
    if (n <= 0 || cursor < 0 || cursor >= n) return;

    /* Card geometry — must match draw_card_full with slide_x = 0. */
    const int cx = 6;
    const int cy = BODY_Y + 18;
    const int cw = SCR_W - 12;
    const int ch = BODY_H - 24;
    const int bx = cx + 22;
    const int by = cy + ch / 2;
    const int br = 14;

    /* Ambient strips — inset 8 px from card edges (so corner brackets
     * + magenta accent dots are never erased), starting below the top
     * brackets and ending above the type indicator / bottom brackets. */
    const int amb_x      = cx + 8;
    const int amb_w      = cw - 16;
    const int amb_top_y  = cy + 4;
    const int amb_top_h  = (ch / 2) - 20;
    const int amb_bot_y  = cy + (ch / 2) + 14;
    const int amb_bot_h  = (ch / 2) - 27;

    /* Clear both strips back to T_BG, then paint ambient — clipped per
     * strip so the ambient mote/grid/packet positions stay computed
     * against the FULL card bounds (otherwise motes would loop in a
     * strip-sized box and the animation would feel cramped). */
    d.fillRect(amb_x, amb_top_y, amb_w, amb_top_h, T_BG);
    d.setClipRect(amb_x, amb_top_y, amb_w, amb_top_h);
    ui_ambient_tick(0, BODY_Y, SCR_W, BODY_H);

    d.fillRect(amb_x, amb_bot_y, amb_w, amb_bot_h, T_BG);
    d.setClipRect(amb_x, amb_bot_y, amb_w, amb_bot_h);
    ui_ambient_tick(0, BODY_Y, SCR_W, BODY_H);

    d.clearClipRect();

    /* Repaint just the pulsing outer ring of the badge. The fill,
     * inner ring, and icon stay where they are. */
    d.drawCircle(bx, by, br, pulse_color_now());
}

void carousel_run_submenu(const menu_node_t *parent)
{
    int      cursor       = 0;
    int      n            = count_children(parent);
    if (n <= 0) return;

    int      slide_dir    = 0;       /* -1 / 0 / +1 */
    uint32_t slide_start  = 0;
    bool     animating    = false;

    ui_status_invalidate();
    ui_draw_status(radio_name(), "");
    ui_draw_footer(CAROUSEL_FOOTER);

    /* Initial paint. */
    draw_card_full(parent, cursor, 0);

    while (true) {
        uint32_t now = millis();

        /* Animation tick — re-paint the card with a decaying x-offset
         * each frame until the 200 ms window expires. */
        if (animating) {
            uint32_t elapsed = now - slide_start;
            if (elapsed >= 200) {
                animating = false;
                draw_card_full(parent, cursor, 0);
            } else {
                int slide_x = (int)((int64_t)slide_dir * (200 - (int)elapsed) * SCR_W / 200);
                draw_card_full(parent, cursor, slide_x);
            }
        } else {
            /* Idle: only repaint the parts that actually animate — the
             * two ambient strips (above + below the text band) and the
             * pulsing outer ring of the badge. Text / brackets / divider
             * / type indicator / arrows stay put, so they don't strobe. */
            static uint32_t last_idle_paint = 0;
            if (now - last_idle_paint > 33) {
                last_idle_paint = now;
                draw_card_anim(parent, cursor);
            }
        }

        uint16_t k = input_poll();
        if (k == PK_NONE) {
            /* Screensaver takeover on idle. After it returns, force a
             * full card repaint (menu state was clobbered). */
            if (screensaver_check_idle()) {
                ui_status_invalidate();
                ui_draw_status(radio_name(), "");
                ui_draw_footer(CAROUSEL_FOOTER);
                draw_card_full(parent, cursor, 0);
            }
            delay(8);
            continue;
        }

        if (k == PK_ESC || k == '`') return;

        /* Help — same key as terminal mode. Delegates to ui_show_current_help. */
        if (k == '=' || k == '?') {
            const menu_node_t *sel = &parent->children[cursor];
            g_current_feature_item = sel;
            ui_show_current_help();
            g_current_feature_item = nullptr;
            ui_draw_status(radio_name(), "");
            ui_draw_footer(CAROUSEL_FOOTER);
            draw_card_full(parent, cursor, 0);
            continue;
        }

        if (k == ';' || k == PK_LEFT) {
            cursor = (cursor - 1 + n) % n;
            slide_dir   = -1;
            slide_start = now;
            animating   = true;
            continue;
        }
        if (k == '.' || k == PK_RIGHT) {
            cursor = (cursor + 1) % n;
            slide_dir   = +1;
            slide_start = now;
            animating   = true;
            continue;
        }

        if (k == PK_ENTER) {
            const menu_node_t *sel = &parent->children[cursor];
            if (sel->action) {
                Serial.printf("[FEAT_ENTER] %s\n", sel->label);
                g_current_feature_item = sel;
                sel->action();
                g_current_feature_item = nullptr;
                /* Defensive IR park — see menu.cpp comment. */
                pinMode(44, OUTPUT); digitalWrite(44, HIGH);
                Serial.printf("[FEAT_EXIT] %s\n", sel->label);
                ui_draw_status(radio_name(), "");
                ui_draw_footer(CAROUSEL_FOOTER);
                draw_card_full(parent, cursor, 0);
            } else if (sel->children) {
                carousel_run_submenu(sel);
                ui_draw_status(radio_name(), "");
                ui_draw_footer(CAROUSEL_FOOTER);
                draw_card_full(parent, cursor, 0);
            }
            continue;
        }

        /* Letter mnemonic — jump straight to that card and execute. No
         * slide animation here; jumps should feel instant. */
        if (k >= 0x20 && k < 0x7F) {
            int i = index_of(parent, (char)k);
            if (i >= 0) {
                cursor = i;
                draw_card_full(parent, cursor, 0);
                const menu_node_t *sel = &parent->children[cursor];
                if (sel->action) {
                    Serial.printf("[FEAT_ENTER] %s\n", sel->label);
                    g_current_feature_item = sel;
                    sel->action();
                    g_current_feature_item = nullptr;
                    /* Defensive IR park — same as regular path. */
                    pinMode(44, OUTPUT); digitalWrite(44, HIGH);
                    Serial.printf("[FEAT_EXIT] %s\n", sel->label);
                    ui_draw_status(radio_name(), "");
                    ui_draw_footer(CAROUSEL_FOOTER);
                    draw_card_full(parent, cursor, 0);
                } else if (sel->children) {
                    carousel_run_submenu(sel);
                    ui_draw_status(radio_name(), "");
                    ui_draw_footer(CAROUSEL_FOOTER);
                    draw_card_full(parent, cursor, 0);
                }
            }
        }
    }
}
