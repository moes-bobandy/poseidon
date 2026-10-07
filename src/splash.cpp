/*
 * splash.cpp — Hokusai Great Wave full-screen splash.
 *
 * Public domain via Wikimedia Commons, converted to 201x135 RGB565.
 * The image fills the full display height; ~20px side margins get
 * matrix-rain animation.
 *
 * Phases:
 *   1. Fade-in from black via per-pixel brightness ramp
 *   2. Magenta scanline sweeps top→bottom (materialization effect)
 *   3. POSEIDON title glows on at the bottom, magenta halo
 *   4. Idle: matrix rain drips in the side gutters until key press
 */
#include "app.h"
#include "ui.h"
#include "input.h"
#include "sfx.h"
#include "version.h"
#include "sprites/splash_sprite.h"
#include <esp_random.h>

#define C_MAG_HI 0xF81F
#define C_MAG_LO 0x9013

static uint16_t blend565(uint16_t a, uint16_t b, uint8_t t)
{
    uint8_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    uint8_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    uint8_t r = (ar * (255 - t) + br * t) / 255;
    uint8_t g = (ag * (255 - t) + bg * t) / 255;
    uint8_t bl = (ab * (255 - t) + bb * t) / 255;
    return (r << 11) | (g << 5) | bl;
}

/* Stock splash is the 240×135 grid. Dual content uses the external
 * panel's native size so the wave is centered on 320×240. */
static int splash_span_w(void)
{
#if POSEIDON_DUAL_SCREEN
    if (poseidon_dual_ok() && poseidon_surface() == POSEIDON_SURFACE_CONTENT)
        return poseidon_view_w();
#endif
    return SCR_W;
}

static int splash_span_h(void)
{
#if POSEIDON_DUAL_SCREEN
    if (poseidon_dual_ok() && poseidon_surface() == POSEIDON_SURFACE_CONTENT)
        return poseidon_view_h();
#endif
    return SCR_H;
}

/* Draw the sprite centered, fade multiplier 0..255. */
static void draw_wave(uint8_t brightness)
{
    auto &d = PoseidonDisplay;
    const int span_w = splash_span_w();
    const int span_h = splash_span_h();
    int ox = (span_w - splash_w) / 2;
    int oy = (span_h - splash_h) / 2;
    for (int y = 0; y < splash_h; ++y) {
        int dy = oy + y;
        if (dy < 0 || dy >= span_h) continue;
        for (int x = 0; x < splash_w; ++x) {
            uint16_t c = splash_data[y * splash_w + x];
            if (c == splash_alpha) continue;
            int dx = ox + x;
            if (dx < 0 || dx >= span_w) continue;
            uint16_t out = (brightness == 255) ? c : blend565(0x0000, c, brightness);
            d.drawPixel(dx, dy, out);
        }
    }
}

void ui_splash(void)
{
    auto &d = PoseidonDisplay;
    d.fillScreen(0x0000);

    /* Boot jingle kicks off alongside the fade-in — sub-bass rumble into
     * glitch sweep into POSEIDON chord. Run from a one-shot FreeRTOS task
     * so it actually runs *in parallel* with the animation below. Direct
     * sfx_boot() call was blocking ~1.1s before phase-1 ever started. */
    xTaskCreate([](void *) {
        extern void sfx_boot(void);
        sfx_boot();
        vTaskDelete(nullptr);
    }, "splash_sfx", 4096, nullptr, 2, nullptr);

    /* ---- Phase 1: fade in with magenta scanline sweep ---- */
    for (int f = 0; f <= 25; ++f) {
        uint32_t frame_start = millis();
        uint8_t bright = (uint8_t)(f * 255 / 25);
        d.fillScreen(0x0000);
        draw_wave(bright);
        /* Scanline — full panel width, moves top→bottom. */
        const int span_w = splash_span_w();
        const int span_h = splash_span_h();
        int sy = f * span_h / 25;
        d.drawFastHLine(0, sy,     span_w, C_MAG_HI);
        d.drawFastHLine(0, sy + 1, span_w, C_MAG_LO);
        if (input_poll() != PK_NONE) goto idle;
        uint32_t e = millis() - frame_start;
        if (e < 30) delay(30 - e);
    }

    /* ---- Phase 2: title glows on at bottom ---- */
    {
        const char *title = "POSEIDON";
        d.setTextSize(2);
        const int span_w = splash_span_w();
        const int span_h = splash_span_h();
        int tw = d.textWidth(title) * 2;
        int tx = (span_w - tw) / 2;
        int ty = span_h - 18;

        /* Clear title band. */
        d.fillRect(0, ty - 2, span_w, 18, 0x0000);

        for (int f = 0; f <= 14; ++f) {
            uint8_t a = (uint8_t)(f * 255 / 14);
            /* Magenta shadow/glow. */
            uint16_t halo = blend565(0x0000, C_MAG_LO, a);
            uint16_t hot  = blend565(0x0000, C_MAG_HI, a);
            /* Redraw title every frame so it brightens. */
            d.fillRect(0, ty - 2, span_w, 18, 0x0000);
            d.setTextColor(halo, 0);
            d.setCursor(tx - 1, ty); d.print(title);
            d.setCursor(tx + 1, ty); d.print(title);
            d.setCursor(tx, ty - 1); d.print(title);
            d.setCursor(tx, ty + 1); d.print(title);
            d.setTextColor(hot, 0);
            d.setCursor(tx, ty); d.print(title);
            if (input_poll() != PK_NONE) goto idle;
            delay(30);
        }
        d.setTextSize(1);

        /* Version tag, dim magenta, bottom-right corner. */
        char vt[32];
        snprintf(vt, sizeof(vt), "v%s", poseidon_version());
        d.setTextColor(C_MAG_LO, 0);
        int vw = d.textWidth(vt);
        d.setCursor(span_w - vw - 3, span_h - 9);
        d.print(vt);
    }

idle:
    /* ---- Phase 3: idle — matrix rain in the side margins ---- */
    while (true) {
        const int span_w = splash_span_w();
        const int span_h = splash_span_h();
        int left_margin  = (span_w - splash_w) / 2;
        int right_start  = left_margin + splash_w;
        if (left_margin > 0)
            ui_matrix_rain(0, 0, left_margin, span_h, C_MAG_HI);
        if (right_start < span_w)
            ui_matrix_rain(right_start, 0, span_w - right_start, span_h, C_MAG_LO);

        /* Occasional magenta twinkle at the title underline. */
        if ((esp_random() & 0xFF) < 20) {
            int x = left_margin + (esp_random() % splash_w);
            d.drawPixel(x, span_h - 2, 0xFFFF);
            delay(40);
            d.drawPixel(x, span_h - 2, C_MAG_HI);
        }

        if (input_poll() != PK_NONE) {
            Serial.println("[SPLASH_EXIT]");
            return;
        }
        delay(80);
    }
}
