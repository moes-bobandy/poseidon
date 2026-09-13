/* wifi_pmf_warn.cpp — see header. */
#include "wifi_pmf_warn.h"
#include "app.h"
#include "../theme.h"
#include "ui.h"
#include "input.h"
#include <M5Cardputer.h>

bool wifi_pmf_warning(void)
{
    auto &d = PoseidonDisplay;
    ui_clear_body();
    d.setTextColor(T_WARN, T_BG);
    d.setCursor(4, BODY_Y + 4); d.print("PMF / 802.11w warning");
    d.drawFastHLine(4, BODY_Y + 14, SCR_W - 8, T_WARN);
    d.setTextColor(T_FG, T_BG);
    d.setCursor(4, BODY_Y + 20); d.print("target uses WPA3 or");
    d.setCursor(4, BODY_Y + 30); d.print("WPA2-Enterprise.");
    d.setCursor(4, BODY_Y + 44); d.print("deauth will be dropped");
    d.setCursor(4, BODY_Y + 54); d.print("cryptographically.");
    d.setTextColor(T_DIM, T_BG);
    d.setCursor(4, BODY_Y + 70); d.print("ENTER = proceed anyway");
    d.setCursor(4, BODY_Y + 80); d.print("ESC   = back");
    ui_draw_footer("PMF warn");
    while (true) {
        uint16_t k = input_poll();
        if (k == PK_ENTER) return true;
        if (k == PK_ESC)   return false;
        delay(20);
    }
}
