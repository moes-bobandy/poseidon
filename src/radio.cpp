/*
 * radio.cpp — lazy domain switcher.
 */
#include "radio.h"
#include "lora_hw.h"
#include "cc1101_hw.h"
#include "nrf24_hw.h"
#include "gps.h"
#include "heap_budget.h"
#include "display/poseidon_display.h"
#include "screensaver.h"
#include "sd_helper.h"
#include <WiFi.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_bt.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <NimBLEDevice.h>
#include <string.h>

static radio_domain_t s_active = RADIO_NONE;
static bool s_wifi_used = false;
/* Survives esp_restart(), not a power cycle. */
RTC_NOINIT_ATTR static uint32_t s_ble_boot_magic;
RTC_NOINIT_ATTR static uint32_t s_ble_boot_count;
RTC_NOINIT_ATTR static uint32_t s_ble_boot_stage;
static const uint32_t BLE_BOOT_MAGIC = 0xB1E0B007u;
/* High bytes are the signature. The low byte is the stage, 1..4.
 * A power-cycle garbage word does not match. */
static const uint32_t BLE_STAGE_TAG = 0xB1E05100u;
static bool s_in_fresh_boot = false;
static bool s_launch_spam = false;
static bool s_fresh_tried = false;
static char s_ble_diag[192] = "ble idle";
static size_t s_L_ss = 0, s_L_rc = 0, s_L_sd = 0, s_L_wf = 0, s_L_init = 0;
static size_t s_L_after_begin = 0, s_L_before_nim = 0;
static int s_nim_rc = -1, s_bt_st = 0;
static uint32_t s_rb_count = 0;
static uint32_t s_stage_num = 0;
/* RTC_NOINIT is declared with the magic above. */
static int s_death_stage = 0;
static char s_death_msg[40];
static esp_timer_handle_t s_ble_wd = nullptr;
static bool s_ble_wd_armed = false;

struct BleHeapNote {
    char tag[28];
    size_t free_b;
    size_t largest;
    uint32_t stage;
};
static BleHeapNote s_notes[4];
static int s_note_n = 0;

static void ble_diag_publish(void)
{
    snprintf(s_ble_diag, sizeof(s_ble_diag),
             "ss=%u rc=%u\nsd=%u wf=%u\nL=%u n=%d st=%d\nrb=%u sg=%u\nLb=%u La=%u",
             (unsigned)s_L_ss, (unsigned)s_L_rc,
             (unsigned)s_L_sd, (unsigned)s_L_wf,
             (unsigned)s_L_init, s_nim_rc, s_bt_st, (unsigned)s_rb_count,
             (unsigned)s_stage_num,
             (unsigned)s_L_before_nim, (unsigned)s_L_after_begin);
    Serial.printf("[radio] %s\n", s_ble_diag);
    Serial.flush();
}

const char *radio_ble_diag(void) { return s_ble_diag; }

bool radio_ble_launch_pending(void)
{
    bool p = s_launch_spam;
    s_launch_spam = false;
    return p;
}

static bool wifi_was_used(void)
{
    if (s_wifi_used) return true;
    wifi_mode_t mode = WIFI_MODE_NULL;
    return esp_wifi_get_mode(&mode) == ESP_OK;
}

static void ble_request_fresh_boot(const char *why)
{
    /* One auto-reboot per spam entry. The post-reboot attempt sets
     * s_in_fresh_boot and must stay up and show the toast if it fails.
     * Count is capped at 1 so a stuck flag cannot reboot forever. */
    if (s_in_fresh_boot) return;
    if (s_ble_boot_count >= 1) return;
    s_ble_boot_count = 1;
    s_rb_count = s_ble_boot_count;
    ble_diag_publish();
    Serial.printf("[radio] BLE fresh-heap restart (%s) rb=%u\n",
                  why, (unsigned)s_rb_count);
    Serial.flush();
    poseidon_lcd_resume_after_bus();
    s_ble_boot_magic = BLE_BOOT_MAGIC;
    esp_restart();
}

void wifi_force_clean_sta(void)
{
    /* Probe whether WiFi was ever inited this session. esp_wifi_get_mode
     * returns ESP_ERR_WIFI_NOT_INIT until esp_wifi_init has run; in
     * that case there's nothing to reset and the subsequent IDF calls
     * would themselves return errors and leave the driver in a
     * half-state that breaks Arduino's later WiFi.mode() init. */
    wifi_mode_t cur = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&cur) != ESP_OK) return;
    esp_wifi_set_promiscuous(false);
    esp_wifi_disconnect();
    esp_wifi_set_mode(WIFI_MODE_STA);
    delay(30);
}

bool wifi_lean_sta_init(void)
{
    s_wifi_used = true;
    /* If WiFi is already inited (we got here from another feature in
     * the same session), just ensure mode is STA + started.
     * esp_wifi_get_mode returns ESP_OK only after esp_wifi_init has run.
     *
     * On-device repro fix 2026-06-06: after POS-AUDIT-008 the
     * teardown(RADIO_WIFI) leaves the driver stopped-but-inited.
     * Previous code here just returned true on the inited check —
     * subsequent esp_wifi_set_promiscuous / esp_wifi_set_channel /
     * esp_wifi_80211_tx then all failed with ESP_ERR_WIFI_NOT_STARTED
     * and the WiFi task's coex path eventually panicked, freezing the
     * device and forcing a reset. Symptom seen as "Deauth All freezes
     * and restarts". Now we explicitly esp_wifi_start() — a second
     * start on an already-started driver returns ESP_ERR_WIFI_STATE
     * (harmless), but covers the stopped-but-inited case which is the
     * only one POS-AUDIT-008 introduced. */
    wifi_mode_t cur = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&cur) == ESP_OK) {
        if (cur != WIFI_MODE_STA) {
            esp_wifi_set_promiscuous(false);
            esp_wifi_disconnect();
            esp_wifi_set_mode(WIFI_MODE_STA);
            delay(30);
        }
        (void)esp_wifi_start();
        return true;
    }
    /* Fresh init — raw IDF with shrunk buffers to fit in fragmented
     * DMA RAM (M5GFX framebuffer holds ~60 KB at boot, leaves no
     * room for default 32-buffer Arduino init). */
    esp_netif_init();
    esp_event_loop_create_default();
    /* Repro fix 2026-06-06: a previous AP-mode feature (Portal,
     * Evil Twin, CIW, AP Signal Test, SaltyJack rogue-DHCP) may have
     * left a default AP netif resident. Creating a STA netif on top
     * conflicts and panic-restarts during the next esp_wifi_init.
     * Destroy any leftover AP netif first. */
    esp_netif_t *ap_if = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_if) esp_netif_destroy_default_wifi(ap_if);
    /* The static cache below tracked "we already created the STA netif
     * this boot". After the AP cleanup above, the STA netif may also
     * have been collateral-destroyed by a prior call — always probe
     * the handle directly instead of trusting the cache. */
    if (!esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")) {
        esp_netif_create_default_wifi_sta();
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    /* TIGHT buffer config. Cardputer-Adv has ~52 KB DMA-capable RAM
     * after M5GFX framebuffer at boot. WiFi init grabs most of it for
     * its tx_buf + rx_buf pools (each ~1.7 KB). At 8/8 we hit ~14 KB
     * of TX + ~14 KB of RX = 28 KB consumed, leaving ~1 KB DMA free
     * for runtime allocations — raw 802.11 TX then OOMs after one
     * frame. 4/4 leaves ~14 KB DMA-free which is plenty for sustained
     * raw TX bursts with the 2 ms inter-frame delay. Sacrifice: brief
     * RX bursts may drop occasional frames (capture rate ~halved at
     * very high traffic), but TX reliability is far more important. */
    cfg.static_tx_buf_num  = 0;
    cfg.dynamic_tx_buf_num = 4;
    cfg.tx_buf_type        = 1;
    cfg.cache_tx_buf_num   = 4;
    cfg.static_rx_buf_num  = 4;
    cfg.dynamic_rx_buf_num = 4;
    cfg.ampdu_tx_enable    = 0;
    cfg.ampdu_rx_enable    = 0;
    cfg.amsdu_tx_enable    = 0;
    esp_err_t ie = esp_wifi_init(&cfg);
    if (ie != ESP_OK) return false;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_err_t se = esp_wifi_start();
    if (se != ESP_OK) return false;
    /* #A5 / 2026-06-09: bump TX power once at session entry — 84 is
     * the driver max in 0.25 dBm units (21 dBm ~126 mW). Per the
     * wifi_probe.cpp comment, setting this per-burst destabilises the
     * driver; setting it once after esp_wifi_start is the supported
     * pattern. Benefits every STA-mode feature that goes through
     * wifi_lean_sta_init: Deauth All, Scan, Probe, PMKID, Beacon
     * Spam, Wardrive. */
    esp_wifi_set_max_tx_power(84);
    return true;
}

const char *radio_name(void)
{
    switch (s_active) {
    case RADIO_WIFI:   return "wifi";
    case RADIO_BLE:    return "ble";
    case RADIO_LORA:   return "lora";
    case RADIO_SUBGHZ: return "subghz";
    case RADIO_NRF24:  return "nrf24";
    default:           return "idle";
    }
}

radio_domain_t radio_current(void) { return s_active; }

static void teardown_current(void)
{
    switch (s_active) {
    case RADIO_WIFI:
        /* POS-AUDIT-008: PORKCHOP pattern. WiFi.mode(WIFI_OFF) is BANNED
         * — combined with esp_wifi_deinit, repeated session churn
         * fragments the heap and the next esp_wifi_init returns
         * ESP_ERR_NO_MEM (257), eventually deadlocking the driver.
         * esp_wifi_stop alone leaves the driver structures resident so
         * the next feature's WiFi.mode(STA/AP) + start is clean off a
         * hot driver. Heap-aware deinit gate (only deinit when heap is
         * healthy AND largest-free > kMinHeapForTls) lands with
         * POS-AUDIT-118. WiFi.disconnect(false,false) just drops the
         * association without driver-state poke. */
        WiFi.disconnect(false, false);
        esp_wifi_stop();
        break;
    case RADIO_BLE:
        /* Only deinit if NimBLE is actually initialized — features that
         * explicitly deinit on exit (ble_hid) leave a dangling state
         * flag, and a second deinit crashes on some NimBLE builds. */
        if (NimBLEDevice::isInitialized()) NimBLEDevice::deinit(true);
        break;
    case RADIO_LORA:
        if (lora_is_up()) lora_end();
        break;
    case RADIO_SUBGHZ:
        cc1101_end();
        gps_begin();  /* re-enable GPS UART on pin 13 after CC1101 releases it */
        break;
    case RADIO_NRF24:
        nrf24_end();
        break;
    default: break;
    }
    s_active = RADIO_NONE;
    delay(100);
}

/* Last-resort heap return for the 48 KB BLE gate. The caller measures
 * first and invokes this only when the largest block is still short, so
 * it is not the every-session deinit POS-AUDIT-008 bans.
 *
 * Branch 1 — Arduino owns the stack (lowLevelInitDone && started).
 * WiFi.getMode() is not NULL. WiFi.mode(WIFI_OFF) → espWiFiStop() →
 * wifiLowLevelDeinit(), which clears lowLevelInitDone and then
 * esp_wifi_deinit(). A raw deinit here would leave that flag set.
 *
 * Branch 2 — raw IDF owns the stack (wifi_lean_sta_init, beacon spam).
 * getMode() is NULL because lowLevelInitDone is already false, but
 * esp_wifi_get_mode() still returns ESP_OK. stop + deinit is safe;
 * Arduino state cannot desync. After either branch, esp_wifi_get_mode()
 * is NOT_INIT, so wifi_lean_sta_init takes the fresh esp_wifi_init at
 * the bottom of this file and the portal calls esp_wifi_init again. */
static void release_wifi_driver_for_ble(void)
{
    if (WiFi.getMode() != WIFI_MODE_NULL) {
        WiFi.mode(WIFI_OFF);
        return;
    }
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK) return;
    esp_wifi_stop();
    esp_wifi_deinit();
}

/* A failed enable leaves the controller INITED while NimBLE's flag
 * stays false. The next init then dies in esp_bt_controller_init
 * with INVALID_STATE and Bluetooth never comes up. */
static void ble_controller_idle(void)
{
    esp_bt_controller_status_t st = esp_bt_controller_get_status();
    if (st == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        esp_bt_controller_disable();
        st = esp_bt_controller_get_status();
    }
    if (st == ESP_BT_CONTROLLER_STATUS_INITED) {
        esp_bt_controller_deinit();
        delay(100);
    }
}

bool radio_switch(radio_domain_t target)
{
    if (target == s_active) return true;
    teardown_current();
    if (target == RADIO_NONE) return true;

    switch (target) {
    case RADIO_WIFI:
        s_wifi_used = true;
        /* State-only switch — don't touch WiFi here. Calling WiFi.mode(WIFI_STA)
         * left the driver in a half-init state ("STA not started!" warning
         * on disconnect), then any later WiFi.mode(WIFI_AP) crashed in
         * ieee80211_hostap_attach (+0x2c null deref) because the AP-side
         * driver structures were never allocated. Features now bring up
         * WiFi themselves from a clean state (WiFi.mode(WIFI_STA) for
         * scan/wardrive, WiFi.mode(WIFI_AP) for portal/spam). Country
         * code is still applied lazily on first esp_wifi_init by the
         * driver default, and esp_wifi_set_country can be called by
         * features that hop ch12-14. */
        break;
    case RADIO_BLE:
        Serial.printf("[radio] enter RADIO_BLE setup. bt_ctrl_status=%d\n",
                      (int)esp_bt_controller_get_status());
        Serial.flush();
        if (!NimBLEDevice::isInitialized()) {
            bool wifi_used = wifi_was_used();
            Serial.printf("[radio] wifi_used=%d (does not gate the reboot)\n",
                          (int)wifi_used);
            Serial.flush();
            /* Heap inventory, in order. ui.cpp's canvas is allocated
             * only inside an overlay and is already gone by the time a
             * menu item runs. What can be freed: screensaver sprite
             * (~154 KB, only if the saver was up), Argus's 96x96 sprite
             * (~18 KB) via heap_reclaim_all, the SD FAT buffers, then
             * leftover Wi-Fi / lwIP. */
            bool ss_freed = screensaver_free_sprite();
            s_L_ss = heap_largest_internal();
            Serial.printf("[radio] screensaver sprite freed=%d Lss=%u\n",
                          (int)ss_freed, (unsigned)s_L_ss);
            Serial.flush();
            heap_reclaim_all();
            s_L_rc = heap_largest_internal();
            /* EXT CS idle HIGH before any SPI release or the controller
             * allocation. dma_channel stays 0. */
            poseidon_lcd_quiesce();
            s_L_sd = s_L_rc;
            if (s_L_sd < 48 * 1024) {
                sd_drop_for_ble();
                heap_reclaim_all();
                s_L_sd = heap_largest_internal();
            }
            ble_controller_idle();
            s_L_wf = s_L_sd;
            if (s_L_wf < 48 * 1024) {
                release_wifi_driver_for_ble();
                delay(20);
                heap_reclaim_all();
                s_L_wf = heap_largest_internal();
            }
            s_L_init = s_L_wf;
            s_L_before_nim = s_L_init;
            s_nim_rc = -1;
            s_bt_st = (int)esp_bt_controller_get_status();
            Serial.printf("[boot] NimBLE gate free=%u largest=%u stage=%u marker=0x%08x\n",
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                          (unsigned)s_L_before_nim, (unsigned)s_stage_num,
                          (unsigned)s_ble_boot_stage);
            Serial.flush();
            /* NimBLE asserts when the largest internal block is too
             * small. One clean-heap reboot, whether or not Wi-Fi was
             * used. The one-shot boot itself must not reboot again. */
            if (s_L_init < 48 * 1024) {
                poseidon_lcd_resume_after_bus();
                ble_request_fresh_boot("heap");
                ble_diag_publish();
                if (s_in_fresh_boot) {
                    Serial.printf("[boot] NimBLE not started, largest=%u under 48KB, no reboot\n",
                                  (unsigned)s_L_before_nim);
                    Serial.flush();
                }
                return false;
            }
            int st_before = s_bt_st;
            bool ok = NimBLEDevice::init("");
            int st_after = (int)esp_bt_controller_get_status();
            poseidon_lcd_resume_after_bus();
            /* NimBLE swallows esp_bt_controller_enable's esp_err_t.
             * st is the observable result: 0 IDLE (init rejected),
             * 1 INITED (enable failed), 2 ENABLED. n is NimBLE's bool. */
            s_L_init = heap_largest_internal();
            s_nim_rc = (int)ok;
            s_bt_st = st_after;
            ble_diag_publish();
            Serial.printf("[radio] enable-stage before=%d after=%d nim=%d\n",
                          st_before, st_after, (int)ok);
            Serial.flush();
            if (!ok || st_after != (int)ESP_BT_CONTROLLER_STATUS_ENABLED) {
                ble_controller_idle();
                ble_request_fresh_boot("init");
                ble_diag_publish();
                return false;
            }
        } else {
            Serial.println("[radio] NimBLE already initialized"); Serial.flush();
        }
        break;
    case RADIO_LORA:
        break;
    case RADIO_SUBGHZ:
        gps_end();  /* release pin 13 — GPS UART TX shares with CC1101 CS */
        break;
    case RADIO_NRF24:
        break;
    default: break;
    }
    s_active = target;
    return true;
}

static bool ble_stage_number(uint32_t word, uint32_t *n_out)
{
    if ((word & 0xFFFFFF00u) != BLE_STAGE_TAG) return false;
    uint32_t n = word & 0xFFu;
    if (n < 1 || n > 4) return false;
    *n_out = n;
    return true;
}

static void ble_clear_rtc(void)
{
    s_ble_boot_magic = 0;
    s_ble_boot_count = 0;
    s_ble_boot_stage = 0;
}

static void ble_wd_fire(void *)
{
    /* Leave the stage marker. The magic was cleared before the timer
     * was armed, so the next boot cannot enter BLE-first. */
    esp_restart();
}

void radio_ble_note_heap(const char *tag)
{
    if (s_note_n >= 4) return;
    BleHeapNote &n = s_notes[s_note_n++];
    snprintf(n.tag, sizeof(n.tag), "%s", tag ? tag : "?");
    n.free_b = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    n.largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    n.stage = s_stage_num;
    if (tag && strcmp(tag, "after M5Cardputer.begin") == 0)
        s_L_after_begin = n.largest;
}

void radio_ble_flush_notes(void)
{
    for (int i = 0; i < s_note_n; i++) {
        Serial.printf("[boot] %s free=%u largest=%u stage=%u\n",
                      s_notes[i].tag, (unsigned)s_notes[i].free_b,
                      (unsigned)s_notes[i].largest, (unsigned)s_notes[i].stage);
    }
    s_note_n = 0;
    if (s_death_stage)
        Serial.printf("[boot] %s\n", s_death_msg);
    Serial.flush();
}

void radio_ble_log_heap(const char *tag)
{
    Serial.printf("[boot] %s free=%u largest=%u stage=%u marker=0x%08x\n",
                  tag ? tag : "?",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)s_stage_num, (unsigned)s_ble_boot_stage);
    Serial.flush();
}

void radio_ble_watchdog_arm(void)
{
    if (!s_in_fresh_boot || s_ble_wd_armed) return;
    esp_timer_create_args_t args = {};
    args.callback = ble_wd_fire;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "ble_boot";
    if (esp_timer_create(&args, &s_ble_wd) != ESP_OK) {
        Serial.println("[boot] esp_timer_create failed");
        Serial.flush();
        return;
    }
    if (esp_timer_start_once(s_ble_wd, 8000000ULL) != ESP_OK) {
        esp_timer_delete(s_ble_wd);
        s_ble_wd = nullptr;
        Serial.println("[boot] esp_timer_start_once failed");
        Serial.flush();
        return;
    }
    s_ble_wd_armed = true;
    Serial.println("[boot] watchdog armed 8s on esp_timer task");
    Serial.flush();
}

void radio_ble_watchdog_disarm(void)
{
    if (!s_ble_wd_armed) return;
    esp_timer_stop(s_ble_wd);
    esp_timer_delete(s_ble_wd);
    s_ble_wd = nullptr;
    s_ble_wd_armed = false;
}

void radio_ble_mark_stage(uint32_t stage)
{
    if (!s_in_fresh_boot) return;
    if (stage < 1 || stage > 4) return;
    s_stage_num = stage;
    s_ble_boot_stage = BLE_STAGE_TAG | stage;
}

bool radio_ble_fresh_active(void) { return s_in_fresh_boot; }

const char *radio_ble_death_msg(void)
{
    return s_death_stage ? s_death_msg : nullptr;
}

bool radio_ble_boot_armed(void)
{
    /* First sample. Serial is not up yet; flushed after begin. */
    radio_ble_note_heap("before armed check");

    uint32_t died = 0;
    if (ble_stage_number(s_ble_boot_stage, &died)) {
        /* Previous BLE-first boot did not reach the UI. Whatever the
         * magic and counter are, this boot is a normal one. */
        s_death_stage = (int)died;
        snprintf(s_death_msg, sizeof(s_death_msg),
                 "BLE boot died at stage %u", (unsigned)died);
        ble_clear_rtc();
        return false;
    }
    if (s_ble_boot_stage != 0) {
        /* Power-cycle garbage. Not a death toast. */
        s_ble_boot_stage = 0;
    }
    if (s_ble_boot_magic != BLE_BOOT_MAGIC) {
        s_ble_boot_count = 0;
        return false;
    }
    /* Clear before M5Cardputer.begin, NimBLE, or anything that panics. */
    s_ble_boot_magic = 0;
    if (s_ble_boot_count != 1) {
        s_ble_boot_count = 0;
        return false;
    }
    s_rb_count = s_ble_boot_count;
    s_in_fresh_boot = true;
    s_launch_spam = true;
    return true;
}

void radio_ble_fresh_boot(void)
{
    if (!s_in_fresh_boot) return;
    /* GPIO5 is not driven here. poseidon_lcd_quiesce() does nothing
     * until poseidon_dual_begin has brought the external panel up. */
    (void)radio_switch(RADIO_BLE);
    s_fresh_tried = true;
}

bool radio_ble_take_boot_failure(void)
{
    if (!s_fresh_tried || NimBLEDevice::isInitialized()) return false;
    s_fresh_tried = false;
    return true;
}

void radio_ble_boot_reached_ui(void)
{
    if (s_in_fresh_boot)
        s_ble_boot_stage = 0;
    radio_ble_watchdog_disarm();
}
