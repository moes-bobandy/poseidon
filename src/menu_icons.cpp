/*
 * menu_icons.cpp — dispatch top-level menu hotkeys to bitmap icons.
 *
 * Bitmap data is auto-generated from assets/icons.jpg by
 * scripts/convert_icons.py. To re-run the conversion (e.g. after
 * tweaking the icon sheet):
 *
 *   python scripts/convert_icons.py
 *
 * which rewrites src/menu_icons_data.h.
 */
#include "menu_icons.h"
#include "menu_icons_data.h"
#include <M5Cardputer.h>
#include "display/poseidon_display.h"

extern const menu_node_t MENU_ROOT;

bool draw_menu_icon(int cx, int cy, uint16_t color,
                    const menu_node_t *parent, const menu_node_t *item)
{
    if (parent != &MENU_ROOT) return false;
    if (!item) return false;

    auto &d = PoseidonDisplay;
    /* drawBitmap origin is top-left; we want the icon centered on
     * (cx, cy) — offset by half the bitmap dimensions. */
    int x = cx - (MENU_ICON_W / 2);
    int y = cy - (MENU_ICON_H / 2);

    const uint8_t *bmp = nullptr;
    switch (item->hotkey) {
    case 'w': bmp = MENU_ICON_WIFI;      break;
    case 'b': bmp = MENU_ICON_BLE;       break;
    case 'i': bmp = MENU_ICON_IR;        break;
    case 't': bmp = MENU_ICON_TRIDENT;   break;
    case 'u': bmp = MENU_ICON_USB;       break;
    case 'n': bmp = MENU_ICON_NETWORK;   break;
    case 'j': bmp = MENU_ICON_SKULL;     break;
    case 'r': bmp = MENU_ICON_RADIO;     break;
    case 'o': bmp = MENU_ICON_TOOLS;     break;
    case 'm': bmp = MENU_ICON_MESH;      break;
    case '5': bmp = MENU_ICON_SATELLITE; break;
    case 'x': bmp = MENU_ICON_EYE;       break;
    case 'p': bmp = MENU_ICON_LAPTOP;    break;
    case 's': bmp = MENU_ICON_GEAR;      break;
    default: return false;
    }
    d.drawBitmap(x, y, bmp, MENU_ICON_W, MENU_ICON_H, color);
    return true;
}
