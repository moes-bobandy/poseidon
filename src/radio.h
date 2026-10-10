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
 * RTC magic before returning. A normal boot returns false. The result
 * is kept in a static for the call after M5Cardputer.begin. */
bool radio_ble_boot_armed(void);

/* True after radio_ble_boot_armed() accepted this boot. */
bool radio_ble_fresh_active(void);

/* Record free/largest internal heap. Safe before Serial.begin. */
void radio_ble_note_heap(const char *tag);

/* Print notes taken before Serial.begin, then drop them. */
void radio_ble_flush_notes(void);

/* Print free heap, largest block, and the stage marker. Serial is up. */
void radio_ble_log_heap(const char *tag);

/* 8 s one-shot. Call only on the BLE-first path, before NimBLE.
 * Fires from the esp_timer task, not from setup(). */
void radio_ble_watchdog_arm(void);
void radio_ble_watchdog_disarm(void);

/* RTC stage, BLE-first boot only. 1 before NimBLE, 2 after NimBLE,
 * 3 before dual_begin, 4 after dual_begin. */
void radio_ble_mark_stage(uint32_t stage);

/* NimBLE for the restart. Call after M5Cardputer.begin, keyboard
 * begin, and the internal setRotation, and before poseidon_dual_begin.
 * Does not touch GPIO5. Does not reboot when this boot is already
 * the one-shot restart. */
void radio_ble_fresh_boot(void);

/* Clear the stage marker and disarm the watchdog. Call once the
 * panels and ui_init are up, before the splash wait. */
void radio_ble_boot_reached_ui(void);

/* "BLE boot died at stage N" when the previous boot's marker was
 * still set. Null on a clean boot. */
const char *radio_ble_death_msg(void);

/* True once after a fresh-heap reboot so the menu opens BLE spam. */
bool radio_ble_launch_pending(void);

/* The one-shot boot already ran NimBLE and it did not come up.
 * One-shot: the next spam entry tries again. */
bool radio_ble_take_boot_failure(void);
