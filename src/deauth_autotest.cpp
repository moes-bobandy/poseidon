/*
 * deauth_autotest.cpp — no-UI deauth stress / regression test.
 *
 * Gated on -DPOSEIDON_AUTO_DEAUTH_TEST. Runs at boot before splash.
 *
 * Stages:
 *   A. Basic    — wifi_silent_ap_begin → 4 deauth pairs → end.
 *   B. Burst    — mirror deauth_task: 30 rounds × 16 broadcast pairs
 *                 (64 frames per round) with delay(3) like the real
 *                 attack. This is where the user reported reboot.
 *   C. Triton   — per-hop silent_ap_begin/end × 10 cycles with a
 *                 broadcast pair between, matching Triton's hop_task.
 *
 * Crash trace without serial: at every critical step we write a
 * one-byte marker to NVS (`autotest/step`). If the device reboots
 * mid-test, the next boot reads the marker and displays it on the
 * screen so the user can read the last step reached.
 *
 * Success log (via serial, if CDC works): `[autotest] STAGE n PASS`.
 */
#include <Arduino.h>
#include <Preferences.h>
#include <M5Cardputer.h>
#include "display/poseidon_display.h"
#include "features/wifi_deauth_frame.h"

static Preferences s_pref;

static void mark(uint8_t step, const char *label)
{
    s_pref.begin("autotest", false);
    s_pref.putUChar("step", step);
    s_pref.putString("label", label);
    s_pref.end();
    Serial.printf("[autotest] step=%u %s\n", (unsigned)step, label);
}

/* Called from main.cpp BEFORE c5_begin so we can read the prior-boot
 * crash marker and render it. If step == 0xFF (successful run marker)
 * or not present, do nothing. */
void poseidon_autotest_show_last_crash(void)
{
    s_pref.begin("autotest", true);
    uint8_t last_step = s_pref.getUChar("step", 0xFF);
    String last_label = s_pref.getString("label", String());
    s_pref.end();
    if (last_step == 0xFF || last_step == 0xFE) return;  /* no crash */

    auto &d = PoseidonDisplay;
    d.fillScreen(0x0000);
    d.setTextColor(0xF81F);  /* magenta */
    d.setTextSize(2);
    d.setCursor(6, 10); d.print("AUTOTEST CRASH");
    d.setTextSize(1);
    d.setTextColor(0xFFFF);
    d.setCursor(6, 38); d.printf("last step: %u", (unsigned)last_step);
    d.setCursor(6, 52); d.printf("label: %s", last_label.c_str());
    d.setTextColor(0x7BEF);
    d.setCursor(6, 80); d.print("press any key to continue");
    while (!Serial.available() && M5Cardputer.Keyboard.isPressed() == false) {
        M5Cardputer.update();
        delay(20);
    }
    /* Clear marker so we don't show this again until next crash. */
    s_pref.begin("autotest", false);
    s_pref.putUChar("step", 0xFF);
    s_pref.end();
}

static bool stage_basic(void)
{
    mark(10, "basic.begin");
    const uint8_t bssid[6]  = {0x00, 0x50, 0xC2, 0xFF, 0xFE, 0x00};
    const uint8_t client[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    wifi_silent_ap_set_source_mac(bssid);

    mark(11, "basic.silent_ap_begin");
    if (wifi_silent_ap_begin(1) != ESP_OK) { mark(12, "basic.begin_fail"); return false; }
    mark(13, "basic.bursts");
    uint16_t seq = 0x100;
    for (int i = 0; i < 4; ++i) {
        wifi_deauth_pair(client, bssid, &seq);
        delay(3);
    }
    mark(14, "basic.end");
    wifi_silent_ap_end();
    wifi_silent_ap_set_source_mac(nullptr);
    mark(15, "basic.done");
    return true;
}

static bool stage_burst(void)
{
    /* Exact replica of deauth_task's inner loop: 30 rounds ×
     * 16 broadcast pairs, delay(3). ~30 s of sustained attack. */
    mark(20, "burst.begin");
    const uint8_t bssid[6] = {0x00, 0x50, 0xC2, 0xFF, 0xFE, 0x01};
    wifi_silent_ap_set_source_mac(bssid);
    if (wifi_silent_ap_begin(6) != ESP_OK) { mark(21, "burst.begin_fail"); return false; }

    uint16_t seq = (uint16_t)(esp_random() & 0x0FFF);
    for (int round = 0; round < 30; ++round) {
        mark(22, "burst.round");
        for (int i = 0; i < 16; ++i) {
            wifi_deauth_broadcast(bssid, &seq);
            delay(3);
        }
        delay(30);
    }
    mark(23, "burst.end");
    wifi_silent_ap_end();
    wifi_silent_ap_set_source_mac(nullptr);
    mark(24, "burst.done");
    return true;
}

static bool stage_triton(void)
{
    /* Per-hop begin/end × 10 — Triton's hop_task pattern. */
    mark(30, "triton.begin");
    const uint8_t bssid[6] = {0x00, 0x50, 0xC2, 0xFF, 0xFE, 0x02};
    wifi_silent_ap_set_source_mac(bssid);
    for (int hop = 0; hop < 10; ++hop) {
        mark(31, "triton.hop");
        uint8_t ch = 1 + (hop % 11);
        if (wifi_silent_ap_begin(ch) != ESP_OK) { mark(32, "triton.begin_fail"); return false; }
        uint16_t seq = 0;
        for (int i = 0; i < 4; ++i) {
            wifi_deauth_broadcast(bssid, &seq);
            delay(3);
        }
        wifi_silent_ap_end();
        delay(30);
    }
    wifi_silent_ap_set_source_mac(nullptr);
    mark(33, "triton.done");
    return true;
}

void poseidon_deauth_autotest(void)
{
    Serial.println("[autotest] deauth stress test starting");
    mark(1, "startup");

    if (!stage_basic())  { mark(0xEA, "STAGE A FAIL"); return; }
    mark(0x19, "STAGE A PASS");

    if (!stage_burst())  { mark(0xEB, "STAGE B FAIL"); return; }
    mark(0x29, "STAGE B PASS");

    if (!stage_triton()) { mark(0xEC, "STAGE C FAIL"); return; }
    mark(0x39, "STAGE C PASS");

    /* 0xFE = "ran end-to-end without crashing, skip next-boot warning" */
    mark(0xFE, "ALL PASS");
    Serial.println("[autotest] ALL STAGES PASS");
}
