/*
 * subghz_bruteforce.cpp — protocol-specific code brute force.
 *
 * Supports fixed-code protocols: Came 12bit, Nice 12bit, Linear 10bit,
 * Chamberlain 9bit, Holtek 12bit, Ansonic 12bit.
 */
#include "../app.h"
#include "../theme.h"
#include "../ui.h"
#include "../input.h"
#include "../radio.h"
#include "../cc1101_hw.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include <esp_task_wdt.h>

struct brute_proto_t {
    const char *name;
    int bits;
    int rc_proto;     /* RCSwitch protocol number */
    float freq;
    int pulse_len;
};

static const brute_proto_t PROTOS[] = {
    { "Came 12bit",       12, 1, 433.92f, 320 },
    { "Nice 12bit",       12, 1, 433.92f, 700 },
    { "Chamberlain 9bit",  9, 6, 315.00f, 1500 },
    { "Linear 10bit",     10, 6, 300.00f, 500 },
    { "Holtek 12bit",     12, 5, 433.92f, 500 },
    { "Ansonic 12bit",    12, 1, 433.92f, 380 },
};
#define PROTO_COUNT (sizeof(PROTOS)/sizeof(PROTOS[0]))

void feat_subghz_bruteforce(void)
{
    auto &d = PoseidonDisplay;
    int sel = 0;

    /* Static chrome once; rows repaint per-row only when the cursor
     * moves so a selection change never blanks the whole body. */
    ui_clear_body();
    d.setTextColor(T_ACCENT2, T_BG);
    d.setCursor(4, BODY_Y + 2); d.print("BRUTE FORCE");
    d.drawFastHLine(4, BODY_Y + 12, SCR_W - 8, T_ACCENT2);
    ui_draw_footer(";/.=sel  ENTER=start  ESC=back");

    int last_sel = -1;
    while (true) {
        if (sel != last_sel) {
            for (int i = 0; i < (int)PROTO_COUNT; ++i) {
                int y = BODY_Y + 18 + i * 13;
                bool s = (i == sel);
                if (s) d.fillRoundRect(2, y - 1, SCR_W - 4, 12, 2, 0x3007);
                else   d.fillRect(2, y - 1, SCR_W - 4, 12, T_BG);
                d.setTextColor(s ? T_ACCENT : T_FG, s ? 0x3007 : T_BG);
                d.setCursor(8, y); d.printf("%s (%d bit)", PROTOS[i].name, PROTOS[i].bits);
            }
            last_sel = sel;
        }
        uint16_t k = input_poll();
        if (k == PK_NONE) { delay(20); continue; }
        if (k == PK_ESC) return;
        if (k == ';' || k == PK_UP)   sel = (sel - 1 + PROTO_COUNT) % PROTO_COUNT;
        if (k == '.' || k == PK_DOWN) sel = (sel + 1) % PROTO_COUNT;
        if (k == PK_ENTER) break;
    }

    const brute_proto_t &p = PROTOS[sel];
    radio_switch(RADIO_SUBGHZ);
    if (!cc1101_begin(p.freq)) {
        ui_toast("CC1101 not found", T_BAD, 1500);
        radio_switch(RADIO_NONE);
        return;
    }

    RCSwitch tx;
    tx.enableTransmit(CC1101_GDO0);
    tx.setProtocol(p.rc_proto);
    tx.setPulseLength(p.pulse_len);
    tx.setRepeatTransmit(3);

    uint32_t total = 1UL << p.bits;
    uint32_t code = 0;
    bool running = true;
    uint32_t last_draw = 0;
    /* POS-AUDIT-240 / rf-009: full sweep at 16+ bits is hundreds of
     * thousands of TXs; with a typical per-code period of ~380 µs that's
     * minutes of nonstop TX with no yield. Feed the task watchdog once
     * per second so loopTask's TWDT subscription doesn't trip. */
    uint32_t last_wdt_reset = 0;

    /* Paint the TX screen chrome (title, rule, progress-bar frame) once;
     * the per-200ms redraw then only overwrites the live fields + the
     * growing bar fill instead of blanking the whole body each pass. */
    ui_clear_body();
    ui_draw_status(radio_name(), "brute");
    d.setTextColor(T_BAD, T_BG);
    d.setCursor(4, BODY_Y + 2); d.printf("BRUTE: %s", p.name);
    d.drawFastHLine(4, BODY_Y + 12, SCR_W - 8, T_BAD);
    d.drawRect(4, BODY_Y + 36, SCR_W - 8, 12, T_ACCENT);
    ui_draw_footer("ESC=abort");
    int last_bw = -1;

    while (running && code < total) {
        /* Transmit current code. */
        cc1101_set_tx();
        tx.send(code, p.bits);
        cc1101_set_idle();
        code++;

        uint32_t now = millis();
        if (now - last_wdt_reset > 1000) {
            (void)esp_task_wdt_reset();
            last_wdt_reset = now;
        }
        if (now - last_draw > 200) {
            last_draw = now;
            ui_draw_status(radio_name(), "brute");
            ui_text_w(4, BODY_Y + 20, SCR_W - 8, T_FG,
                      "code %lu / %lu", (unsigned long)code, (unsigned long)total);
            /* Progress bar — extend only the newly filled segment so the
             * bar grows smoothly without re-blanking. */
            int bw = (int)((code * (SCR_W - 16)) / total);
            if (bw > last_bw) {
                d.fillRect(6 + (last_bw < 0 ? 0 : last_bw), BODY_Y + 38,
                           bw - (last_bw < 0 ? 0 : last_bw), 8, T_ACCENT2);
                last_bw = bw;
            }
            ui_text_w(4, BODY_Y + 54, SCR_W - 8, T_FG,
                      "%.1f%% complete", (code * 100.0f) / total);
        }

        uint16_t k = input_poll();
        if (k == PK_ESC) running = false;
        delay(2);
    }

    tx.disableTransmit();
    cc1101_end();
    radio_switch(RADIO_NONE);
    ui_toast(running ? "done" : "aborted", running ? T_GOOD : T_WARN, 1000);
}
