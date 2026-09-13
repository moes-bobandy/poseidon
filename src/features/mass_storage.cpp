/*
 * mass_storage — "Mass Storage": expose the microSD to a USB host as a
 * removable drive (USB MSC). Turns the Cardputer into a card reader.
 *
 * Implementation note: arduino-esp32's SD library is NOT the IDF sdmmc
 * driver — it's a self-contained SPI SD implementation (sd_diskio.cpp)
 * with its own bus locking. Trying to claim the card via the IDF
 * sdspi/sdmmc path while Arduino SD owns the bus deadlocks. So the MSC
 * read/write callbacks call Arduino's OWN raw sector helpers
 * (sd_read_raw / sd_write_raw) — no teardown, no second driver. Host and
 * firmware share one driver; Arduino's AcquireSPI lock serialises them.
 *
 * On ARDUINO_USB_MODE=1 the device boots on the hardware USB-Serial/JTAG
 * PHY. USB.begin() switches the single USB PHY over to USB-OTG (TinyUSB)
 * so the MSC interface enumerates — same call BadUSB makes for HID. Side
 * effect: the USB-CDC serial console drops until a reboot.
 *
 * On exit we re-mount the card (SD.end + sd_mount) to discard any FATFS
 * cache the host invalidated with its own writes.
 */
#include "app.h"
#include "../theme.h"
#include "ui.h"
#include "input.h"
#include "sd_helper.h"

#include <USB.h>
#include <USBMSC.h>
#include <SD.h>

/* Raw single-sector helpers exported by arduino-esp32's SD library
 * (libraries/SD/src/sd_diskio.cpp) — declared here so we don't depend on
 * that private header being on the include path. */
bool sd_read_raw(uint8_t pdrv, uint8_t *buffer, uint32_t sector);
bool sd_write_raw(uint8_t pdrv, uint8_t *buffer, uint32_t sector);
/* sd_helper re-mount (flushes FATFS after the host's writes). */
bool sd_remount(void);

/* POSEIDON mounts exactly one SD card, so its Arduino drive index is 0. */
#define MS_PDRV  0

static USBMSC            s_msc;
static uint32_t          s_sector  = 512;
static volatile uint32_t s_rd_sectors = 0;
static volatile uint32_t s_wr_sectors = 0;

/* ---- MSC callbacks: per-sector I/O through Arduino's SD driver ---- */

static int32_t ms_read(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    (void)offset;
    uint32_t count = bufsize / s_sector;
    uint8_t *p = (uint8_t *)buffer;
    for (uint32_t i = 0; i < count; ++i) {
        if (!sd_read_raw(MS_PDRV, p + i * s_sector, lba + i)) return -1;
    }
    s_rd_sectors += count;
    return (int32_t)bufsize;
}

static int32_t ms_write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    (void)offset;
    uint32_t count = bufsize / s_sector;
    for (uint32_t i = 0; i < count; ++i) {
        if (!sd_write_raw(MS_PDRV, buffer + i * s_sector, lba + i)) return -1;
    }
    s_wr_sectors += count;
    return (int32_t)bufsize;
}

static bool ms_start_stop(uint8_t power_condition, bool start, bool load_eject)
{
    (void)power_condition; (void)start; (void)load_eject;
    return true;
}

/* ---- UI ---- */

static bool ms_confirm(void)
{
    auto &d = PoseidonDisplay;
    ui_force_clear_body();
    d.setTextColor(T_ACCENT, T_BG);
    d.setCursor(4, BODY_Y + 2); d.print("MASS STORAGE");
    d.drawFastHLine(4, BODY_Y + 12, SCR_W - 8, T_ACCENT);
    d.setTextColor(T_FG, T_BG);
    d.setCursor(4, BODY_Y + 18); d.print("Expose microSD to USB host as");
    d.setCursor(4, BODY_Y + 28); d.print("a removable drive (card reader).");
    d.setTextColor(T_WARN, T_BG);
    d.setCursor(4, BODY_Y + 44); d.print("SD logging pauses while active.");
    d.setTextColor(T_DIM, T_BG);
    d.setCursor(4, BODY_Y + 56); d.print("Serial drops; reboot to restore.");
    ui_draw_footer("ENTER=start  `=cancel");

    while (true) {
        uint16_t k = input_poll();
        if (k == PK_NONE) { delay(20); continue; }
        if (k == PK_ENTER) return true;
        if (k == PK_ESC)   return false;
    }
}

void feat_mass_storage(void)
{
    if (!ms_confirm()) return;

    if (!sd_mount()) {
        ui_toast("no SD card", T_BAD, 1500);
        return;
    }

    uint32_t blocks = (uint32_t)SD.numSectors();
    s_sector = (uint32_t)SD.sectorSize();
    if (s_sector == 0) s_sector = 512;
    if (blocks == 0) {
        ui_toast("SD size read failed", T_BAD, 1500);
        return;
    }
    uint64_t bytes = (uint64_t)blocks * s_sector;

    s_rd_sectors = 0;
    s_wr_sectors = 0;

    s_msc.vendorID("POSEIDON");
    s_msc.productID("SD Reader");
    s_msc.productRevision("1.0");
    s_msc.onRead(ms_read);
    s_msc.onWrite(ms_write);
    s_msc.onStartStop(ms_start_stop);
    s_msc.mediaPresent(true);
    s_msc.begin(blocks, s_sector);     /* register the MSC interface... */

    /* ...then bring up TinyUSB so it actually enumerates (PHY switch).
     * Serial console drops here. */
    Serial.printf("[msc] %u-byte sectors x %u — starting TinyUSB\n",
                  (unsigned)s_sector, (unsigned)blocks);
    Serial.flush();
    USB.begin();

    /* ---- live status screen ---- */
    auto &d = PoseidonDisplay;
    ui_force_clear_body();
    ui_status_invalidate();
    d.setTextColor(T_GOOD, T_BG);
    d.setCursor(4, BODY_Y + 2); d.print("MASS STORAGE  ACTIVE");
    d.drawFastHLine(4, BODY_Y + 12, SCR_W - 8, T_GOOD);
    d.setTextColor(T_FG, T_BG);
    d.setCursor(4, BODY_Y + 18);
    d.printf("size: %llu MB", (unsigned long long)(bytes / (1024ULL * 1024ULL)));
    d.setTextColor(T_DIM, T_BG);
    d.setCursor(4, BODY_Y + 30); d.print("host has the card mounted");
    ui_draw_footer("`=eject + return to device");

    uint32_t last = 0;
    uint32_t last_rd = 0xFFFFFFFF, last_wr = 0xFFFFFFFF;
    while (true) {
        ui_draw_status("usb", "msc");
        if (millis() - last > 250) {
            last = millis();
            uint32_t rd = s_rd_sectors, wr = s_wr_sectors;
            if (rd != last_rd) {
                ui_text_w(4, BODY_Y + 46, SCR_W - 8, T_ACCENT2,
                          "read : %lu KB", (unsigned long)((uint64_t)rd * s_sector / 1024));
                last_rd = rd;
            }
            if (wr != last_wr) {
                ui_text_w(4, BODY_Y + 58, SCR_W - 8, T_WARN,
                          "write: %lu KB", (unsigned long)((uint64_t)wr * s_sector / 1024));
                last_wr = wr;
            }
        }
        uint16_t k = input_poll();
        if (k == PK_NONE) { delay(20); continue; }
        if (k == PK_ESC) break;
    }

    /* ---- exit: drop the drive, flush FATFS for the firmware ---- */
    ui_toast("ejecting...", T_WARN, 0);
    s_msc.mediaPresent(false);
    delay(50);
    s_msc.end();
    delay(50);
    sd_remount();                 /* discard cache the host may have dirtied */
    ui_toast("SD back on device", T_GOOD, 1000);
}
