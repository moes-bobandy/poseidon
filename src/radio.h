/*
 * radio — lazy domain management.
 *
 * Only one radio stack runs at a time. Switching domains tears down
 * the old one first to free heap. Copied from Davey Jones's proven
 * architecture — keeps BLE init from starving out of RAM when WiFi
 * is already eating 100KB.
 */
#pragma once

#include <Arduino.h>

enum radio_domain_t {
    RADIO_NONE = 0,
    RADIO_WIFI,
    RADIO_BLE,
    RADIO_LORA,
    RADIO_SUBGHZ,
    RADIO_NRF24,
};

/* Switch domains. Tears down the current one, brings up the new one.
 * Call with RADIO_NONE to drop all radios and return heap. */
bool radio_switch(radio_domain_t target);
radio_domain_t radio_current(void);

/* Short name for status bar ("wifi", "ble", "idle"). */
const char *radio_name(void);

/* Force the WiFi driver to a clean STA + non-promiscuous state, but
 * ONLY if it was previously inited. Calling esp_wifi_disconnect /
 * esp_wifi_set_mode on a fresh boot where WiFi has never been inited
 * returns ESP_ERR_WIFI_NOT_INIT and leaves Bruce's libs in a
 * half-state that makes subsequent WiFi.mode(WIFI_STA) init fail
 * NO_MEM with "Expected to init 4 rx buffer, actual is 3". Use this
 * everywhere instead of inlining the raw IDF calls. */
void wifi_force_clean_sta(void);

/* Idempotent raw-IDF lean WiFi init in STA mode. Bypasses Arduino's
 * WiFi.mode(WIFI_STA) which uses DEFAULT buffer counts that won't fit
 * in DMA-capable RAM after the M5GFX framebuffer takes its ~60 KB.
 * Returns true if WiFi is up (either already, or successfully started
 * by this call). Every WiFi-feature should call this BEFORE
 * esp_wifi_set_promiscuous / scan_start / etc. */
bool wifi_lean_sta_init(void);

/* Last BLE attempt: largest block, NimBLE init rc, controller status.
 * Shown on the spam toast when init does not come up. */
const char *radio_ble_diag(void);

/* True only when this boot is the one-shot spam restart. Clears the
 * RTC magic before returning. A normal boot returns false. */
bool radio_ble_boot_armed(void);

/* NimBLE init for that restart. Call only after radio_ble_boot_armed
 * returned true, and before M5Cardputer.begin. Does not touch GPIO5. */
void radio_ble_fresh_boot(void);

/* True once after a fresh-heap reboot so the menu opens BLE spam. */
bool radio_ble_launch_pending(void);
