/*
 * poseidon_display — UI draw target routing.
 *
 * Default (POSEIDON_DUAL_SCREEN=0): M5Cardputer.Display (internal ST7789).
 * Dual Adv (POSEIDON_DUAL_SCREEN=1): external ILI9341 is the primary UI
 * surface; internal ST7789 is a status stub only.
 *
 * Include via app.h. Prefer PoseidonDisplay over M5Cardputer.Display for
 * all UI drawing so the dual-screen env can redirect without feature churn.
 */
#pragma once

#include <M5Cardputer.h>

#ifndef POSEIDON_DUAL_SCREEN
#define POSEIDON_DUAL_SCREEN 0
#endif

#if POSEIDON_DUAL_SCREEN

#include "display/LGFX_ILI9341.h"

lgfx::LGFX_Device &poseidon_disp(void);
bool poseidon_dual_begin(void);
bool poseidon_dual_ok(void);
void poseidon_dual_status_stub(const char *line1, const char *line2 = nullptr);
void poseidon_lcd_quiesce(void);
void poseidon_lcd_resume_after_bus(void);

#define PoseidonDisplay poseidon_disp()

#else /* !POSEIDON_DUAL_SCREEN */

inline M5GFX &poseidon_disp(void) { return M5Cardputer.Display; }
inline bool poseidon_dual_begin(void) { return false; }
inline bool poseidon_dual_ok(void) { return false; }
inline void poseidon_dual_status_stub(const char *, const char * = nullptr) {}
inline void poseidon_lcd_quiesce(void) {}
inline void poseidon_lcd_resume_after_bus(void) {}

#define PoseidonDisplay M5Cardputer.Display

#endif /* POSEIDON_DUAL_SCREEN */
