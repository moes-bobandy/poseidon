/*
 * POSEIDON — shared types, colors, constants.
 */
#pragma once

#include <Arduino.h>
#include <M5Cardputer.h>
#include "display/poseidon_display.h"

/* ---- palette (16-bit 565, via PoseidonDisplay) ---- */
#define COL_BG       0x0000  /* black */
#define COL_FG       0xFFFF  /* white */
#define COL_ACCENT   0x07FF  /* cyan */
#define COL_WARN     0xFFE0  /* yellow */
#define COL_BAD      0xF800  /* red */
#define COL_GOOD     0x07E0  /* green */
#define COL_DIM      0x7BEF  /* grey */
#define COL_MAGENTA  0xF81F

/* ---- display geometry ----
 * Phase-1 dual-screen keeps 240x135 UI layout on the external
 * ILI9341 (top-left). Full 320x240 redesign is later.
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
