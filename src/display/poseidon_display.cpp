/*
 * Dual-screen Cardputer Adv — external ILI9341 primary UI.
 * Built only when POSEIDON_DUAL_SCREEN=1.
 */
#include "display/poseidon_display.h"

#if POSEIDON_DUAL_SCREEN

#include "sd_helper.h"

LGFX_ILI9341 g_ext_display;
static bool s_ext_ok = false;

lgfx::LGFX_Device &poseidon_disp(void)
{
    if (s_ext_ok) return g_ext_display;
    return M5Cardputer.Display;
}

bool poseidon_dual_ok(void) { return s_ext_ok; }

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

void poseidon_dual_status_stub(const char *line1, const char *line2)
{
    auto &d = M5Cardputer.Display;
    d.setRotation(1);
    d.fillScreen(0x0000);
    d.setTextDatum(middle_center);
    d.setTextColor(0x07FF, 0x0000);
    d.setTextSize(1);
    d.drawString("POSEIDON Dual", d.width() / 2, 28);
    d.setTextColor(0xFFFF, 0x0000);
    d.drawString("EXT = primary UI", d.width() / 2, 52);
    d.drawString("INT = status", d.width() / 2, 70);
    if (line1 && *line1) {
        d.setTextColor(0x7BEF, 0x0000);
        d.drawString(line1, d.width() / 2, 96);
    }
    if (line2 && *line2) {
        d.setTextColor(0x7BEF, 0x0000);
        d.drawString(line2, d.width() / 2, 112);
    }
}

bool poseidon_dual_begin(void)
{
    if (s_ext_ok) return true;

    /* Power rail settle after M5Cardputer.begin(). */
    delay(100);

    /* Boot-only color depth BEFORE begin — never mid-run. */
    g_ext_display.setColorDepth(16);
    if (!g_ext_display.begin()) {
        Serial.println("[dual] ILI9341 begin() failed — internal ST7789 only");
        s_ext_ok = false;
        pinMode(POSEIDON_EXT_LCD_CS, OUTPUT);
        digitalWrite(POSEIDON_EXT_LCD_CS, HIGH);
        return false;
    }
    delay(50);
    g_ext_display.setRotation(3); /* landscape 320x240 with offset_rotation 4 */
    g_ext_display.setSwapBytes(true);
    g_ext_display.fillScreen(0x0000);

    /* Keep Phase-1 UI geometry at 240x135 (top-left). Letterbox remainder. */
    g_ext_display.fillRect(0, 135, 320, 240 - 135, 0x0000);
    g_ext_display.fillRect(240, 0, 320 - 240, 135, 0x0000);

    s_ext_ok = true;
    Serial.println("[dual] ILI9341 OK — primary UI on EXT (SPI3 CS5 DC6 RST3)");
    poseidon_dual_status_stub("ILI9341 ready", "UI on external");
    return true;
}

#endif /* POSEIDON_DUAL_SCREEN */

#if !POSEIDON_DUAL_SCREEN
/* Dual-screen bodies live behind POSEIDON_DUAL_SCREEN; header provides inlines. */
void poseidon_display_anchor(void) {}
#endif
