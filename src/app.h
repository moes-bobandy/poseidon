/*
 * POSEIDON — shared types, colors, constants.
 */
#pragma once

#include <Arduino.h>
#include <M5Cardputer.h>
#include "display/poseidon_display.h"

/* ---- palette (16-bit 565). Both dual-screen surfaces use theme.h. ---- */
#define COL_BG       0x0000  /* black */
#define COL_FG       0xFFFF  /* white */
#define COL_ACCENT   0x07FF  /* cyan */
#define COL_WARN     0xFFE0  /* yellow */
#define COL_BAD      0xF800  /* red */
#define COL_GOOD     0x07E0  /* green */
#define COL_DIM      0x7BEF  /* grey */
#define COL_MAGENTA  0xF81F

/* ---- display geometry ----
 * Widget grid is 240×135 on every build. Feature and menu code sizes
 * buffers and row counts from these constants, so they stay literals.
 *
 * Dual content panel is still native 320×240 (see poseidon_view_w/h).
 * Splash and the menu's content card use that full size. Feature entry
 * clears the external panel to the active theme, so the area outside
 * this grid is theme background rather than the Phase-1 black letterbox.
 */
#define SCR_W 240
#define SCR_H 135
#define STATUS_H 12
#define FOOTER_H 10
#define BODY_Y   (STATUS_H)
#define BODY_H   (SCR_H - STATUS_H - FOOTER_H)
#define FOOTER_Y (SCR_H - FOOTER_H)

/* ---- build info ---- */
/* POSEIDON_VERSION comes from -D in platformio.ini; src/version.h
 * provides an #ifndef-guarded fallback. Defining it here too caused
 * a compiler "redefined" warning on every build. */
