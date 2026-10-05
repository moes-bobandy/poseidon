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
 * Stock and the menu surface stay 240×135.
 * Dual content (external ILI9341 up, surface = content) is native
 * 320×240. SCR_W / SCR_H follow the active surface in that build
 * only, so single-screen layouts stay compile-time 240×135.
 */
#if POSEIDON_DUAL_SCREEN
#define SCR_W poseidon_view_w()
#define SCR_H poseidon_view_h()
#else
#define SCR_W 240
#define SCR_H 135
#endif
#define STATUS_H 12
#define FOOTER_H 10
#define BODY_Y   (STATUS_H)
#define BODY_H   (SCR_H - STATUS_H - FOOTER_H)
#define FOOTER_Y (SCR_H - FOOTER_H)

/* ---- build info ---- */
/* POSEIDON_VERSION comes from -D in platformio.ini; src/version.h
 * provides an #ifndef-guarded fallback. Defining it here too caused
 * a compiler "redefined" warning on every build. */
