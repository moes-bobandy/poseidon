/*
 * poseidon_display — dual-screen draw routing (contract v1).
 *
 * Single-screen (POSEIDON_DUAL_SCREEN=0):
 *   poseidon_menu() and poseidon_content() are both M5Cardputer.Display.
 *   PoseidonDisplay is that same panel. Behavior matches stock.
 *
 * Dual (POSEIDON_DUAL_SCREEN=1):
 *   poseidon_menu()    → internal ST7789 240×135 (M5Cardputer.Display)
 *   poseidon_content() → external ILI9341 320×240 when begin() succeeded,
 *                        otherwise the internal ST7789
 *   PoseidonDisplay follows the *active* surface. It does not mean
 *   "send every draw to EXT". Menu, carousel, and theme-picker chrome
 *   run on the menu surface. feat_* screens, splash, and in-feature
 *   HUDs run on the content surface. The other panel keeps its last
 *   frame, so both stay up together.
 *
 * Theme colors come from theme.h on both panels. Content does not
 * carry a second palette.
 *
 * Bus: poseidon_lcd_quiesce / poseidon_lcd_resume_after_bus around SD
 * on the shared SCK/MOSI. RGB565 / COLMOD is boot-only — never
 * mid-run setColorDepth.
 *
 * Pins (locked): SCK 40, MOSI 14, CS 5, DC 6, RST 3, SD_CS 12.
 * Cap LoRa / Hydra claim G3 / G5 / G6. The dual build skips the LoRa
 * RST park in main.cpp; those hats cannot share the panel pins.
 */
#pragma once

#include <M5Cardputer.h>

#ifndef POSEIDON_DUAL_SCREEN
#define POSEIDON_DUAL_SCREEN 0
#endif

/* Which panel PoseidonDisplay currently targets. */
enum poseidon_surface_t {
    POSEIDON_SURFACE_MENU = 0,     /* internal ST7789 */
    POSEIDON_SURFACE_CONTENT = 1,  /* external ILI9341, or ST7789 fallback */
};

#if POSEIDON_DUAL_SCREEN

#include "display/LGFX_ILI9341.h"

lgfx::LGFX_Device &poseidon_menu(void);
lgfx::LGFX_Device &poseidon_content(void);
lgfx::LGFX_Device &poseidon_disp(void);

bool poseidon_dual_begin(void);
bool poseidon_dual_ok(void);
void poseidon_lcd_quiesce(void);
void poseidon_lcd_resume_after_bus(void);

void poseidon_set_surface(poseidon_surface_t surface);
poseidon_surface_t poseidon_surface(void);

/* Bracket a blocking UI. menu_chrome keeps draws on the internal
 * panel (theme picker, layout toggle). Otherwise the feature, splash,
 * or HUD draws on content. Restored by poseidon_leave_ui(). */
void poseidon_enter_ui(bool menu_chrome);
void poseidon_leave_ui(void);

/* While the menu surface is active and the external panel is up,
 * paint the highlighted entry on content at native 320×240 using the
 * active theme. No-op during a feature (content owns that panel). */
void poseidon_content_show_selection(const char *parent,
                                     const char *label,
                                     const char *hint);

/* Width/height of the active surface. Menu is 240×135. Content is
 * 320×240 when the external panel came up, else 240×135. */
int poseidon_view_w(void);
int poseidon_view_h(void);

#define PoseidonDisplay poseidon_disp()

#else /* !POSEIDON_DUAL_SCREEN */

inline M5GFX &poseidon_menu(void) { return M5Cardputer.Display; }
inline M5GFX &poseidon_content(void) { return M5Cardputer.Display; }
inline M5GFX &poseidon_disp(void) { return M5Cardputer.Display; }
inline bool poseidon_dual_begin(void) { return false; }
inline bool poseidon_dual_ok(void) { return false; }
inline void poseidon_lcd_quiesce(void) {}
inline void poseidon_lcd_resume_after_bus(void) {}
inline void poseidon_set_surface(poseidon_surface_t) {}
inline poseidon_surface_t poseidon_surface(void) { return POSEIDON_SURFACE_MENU; }
inline void poseidon_enter_ui(bool) {}
inline void poseidon_leave_ui(void) {}
inline void poseidon_content_show_selection(const char *, const char *, const char *) {}
inline int poseidon_view_w(void) { return 240; }
inline int poseidon_view_h(void) { return 135; }

#define PoseidonDisplay M5Cardputer.Display

#endif /* POSEIDON_DUAL_SCREEN */
