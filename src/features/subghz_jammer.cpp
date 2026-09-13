/*
 * subghz_jammer.cpp — intermittent + continuous carrier jammer.
 *
 * Two modes:
 *   Intermittent: random-width pulses to disrupt without saturating
 *   Full carrier: constant TX for maximum denial (20s safety cap)
 */
#include "../app.h"
#include "../theme.h"
#include "../ui.h"
#include "../input.h"
#include "../radio.h"
#include "../cc1101_hw.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <esp_task_wdt.h>

#define JAM_MAX_MS 20000

void feat_subghz_jammer(void)
{
    radio_switch(RADIO_SUBGHZ);
    if (!cc1101_begin(433.92f)) {
        ui_toast("CC1101 not found", T_BAD, 1500);
        radio_switch(RADIO_NONE);
        return;
    }

    auto &d = PoseidonDisplay;
    float freq = 433.92f;
    int mode = 0;  /* 0=intermittent, 1=full carrier */
    bool active = false;
    uint32_t jam_start = 0;

    /* POS-AUDIT-240 / rf-009: feed the task watchdog once per second
     * while active. Mode-1 full-carrier in particular sits in a tight
     * delay loop with no yield; under TWDT_TIMEOUT=5s the loop trips
     * if the cap is ever extended past 5s. Mode-0 intermittent already
     * yields via delayMicroseconds + the UI redraw cadence, but the
     * cheap reset keeps both modes safe under future cap raises. */
    uint32_t last_wdt_reset = 0;

    /* Paint static chrome once; repaint only on state change (arm/cap)
     * and per-field for the live readouts. Blanking the whole body every
     * loop tick pulsed the panel even while sitting idle. */
    bool need_chrome = true;
    bool last_active = !active;
    float last_freq = freq + 1.0f;
    int   last_mode = -1;
    uint32_t last_elapsed = 0xFFFFFFFFu;

    while (true) {
        uint32_t now = millis();
        if (active && now - last_wdt_reset > 1000) {
            (void)esp_task_wdt_reset();
            last_wdt_reset = now;
        }
        if (active && now - jam_start > JAM_MAX_MS) {
            /* POS-AUDIT-239 / rf-003: cap path used to only call
             * cc1101_set_idle() — chip stayed armed and the radio slot
             * stayed claimed. Toast said "20 s limit" but a re-ENTER
             * on the feature would silently inherit the previous TX
             * state. Drop the slot fully so the next attempt starts
             * clean. radio_switch back to RADIO_NONE on cap, then the
             * user can re-arm via PK_SPACE. */
            cc1101_set_idle();
            cc1101_end();
            radio_switch(RADIO_NONE);
            active = false;
            ui_toast("20s limit", T_WARN, 800);
        }

        if (active && mode == 0) {
            /* Intermittent: random on/off bursts. */
            cc1101_set_tx();
            delayMicroseconds(esp_random() % 2000 + 500);
            cc1101_set_idle();
            delayMicroseconds(esp_random() % 1000 + 200);
        }

        ui_draw_status(radio_name(), "jammer");

        /* Title + rule + footer repaint only when the armed/active state
         * flips — their color depends on it, and a flip also has to wipe
         * the cap toast / stale TX rows, so the full clear is correct
         * here (it runs on transitions only, not per tick). */
        if (need_chrome || active != last_active) {
            ui_clear_body();
            ui_text_w(4, BODY_Y + 2, 130, active ? T_BAD : T_ACCENT2,
                      "JAMMER %s", active ? "ACTIVE" : "ARMED");
            d.drawFastHLine(4, BODY_Y + 12, SCR_W - 8, active ? T_BAD : T_ACCENT2);
            ui_draw_footer(active ? "ESC=STOP" : "ENTER=start  M=mode  +-=freq  ESC=quit");
            /* Force the static fields to repaint after the body clear. */
            last_freq = freq + 1.0f;
            last_mode = -1;
            last_elapsed = 0xFFFFFFFFu;
            need_chrome = false;
        }

        if (freq != last_freq) {
            ui_text_w(4, BODY_Y + 20, SCR_W - 8, T_FG, "freq: %.3f MHz", freq);
            last_freq = freq;
        }
        if (mode != last_mode) {
            ui_text_w(4, BODY_Y + 32, SCR_W - 8, T_FG,
                      "mode: %s", mode == 0 ? "INTERMITTENT" : "FULL CARRIER");
            last_mode = mode;
        }

        if (active) {
            uint32_t elapsed = (now - jam_start) / 1000;
            if (elapsed != last_elapsed) {
                ui_text_w(4, BODY_Y + 48, SCR_W - 8, T_BAD,
                          "TX  %lus / %ds max",
                          (unsigned long)elapsed, JAM_MAX_MS / 1000);
                last_elapsed = elapsed;
            }

            /* Animated TX bars — confined to their own strip, cleared
             * only within that strip so the rest of the body stays put. */
            d.fillRect(10, BODY_Y + 70, 8 * 28, 25, T_BG);
            for (int i = 0; i < 8; ++i) {
                int h = (esp_random() % 20) + 5;
                int x = 10 + i * 28;
                d.fillRect(x, BODY_Y + 70, 20, h, T_BAD);
            }
        }
        last_active = active;

        uint16_t k = input_poll();
        if (k == PK_NONE) { delay(active ? 2 : 20); continue; }
        if (k == PK_ESC) {
            if (active) { cc1101_set_idle(); active = false; }
            else break;
        }
        if (!active) {
            if (k == 'm' || k == 'M') mode = 1 - mode;
            if (k == '+' || k == '=') { freq += 0.5f; cc1101_set_freq(freq); }
            if (k == '-')             { freq -= 0.5f; cc1101_set_freq(freq); }
            if (k == PK_ENTER) {
                /* POS-AUDIT-239 follow-through: cap path drops slot to
                 * RADIO_NONE; re-arm needs a fresh begin. cc1101_begin
                 * is idempotent against s_up — a no-op when we never
                 * capped, full re-init when we did. */
                radio_switch(RADIO_SUBGHZ);
                cc1101_begin(freq);
                active = true;
                jam_start = millis();
                cc1101_set_freq(freq);
                if (mode == 1) cc1101_set_tx();
            }
        }
    }

    cc1101_end();
    radio_switch(RADIO_NONE);
}
