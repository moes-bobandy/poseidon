/*
 * ui_subghz — shared visual widgets for SubGHz features.
 *
 * Goal: make every SubGHz screen look like a piece of RF lab kit.
 * All widgets draw to PoseidonDisplay directly; caller is
 * responsible for any framing / titles outside the widget bounds.
 */
#pragma once
#include <stdint.h>

/* Pulse waveform — oscilloscope-style view of an OOK pulse sequence.
 *
 *   - Each pulse is drawn as a vertical bar above (HIGH) or below (LOW)
 *     the midline. Bar width is proportional to duration relative to
 *     total; height is proportional to duration relative to max.
 *   - Bars are gradient-filled (lighter edge, darker core) so the trace
 *     reads as volumetric, not flat rectangles.
 *   - Color buckets: bars get a HSV hue shift based on which quartile
 *     their duration falls in — gives a visual "rainbow" along the
 *     signal so the user can see timing distribution at a glance.
 *   - Phosphor glow: a thin 1px halo above/below the bar cluster.
 *   - Playhead: when idx >= 0, draws a vertical WARN cursor at the
 *     position corresponding to the cumulative duration up to that
 *     pulse (used by replay to animate TX progress).
 */
void ui_draw_pulse_wave(int x, int y, int w, int h,
                        const int16_t *pulses, int n_pulses,
                        int playhead_idx);

/* Band-aware frequency picker.
 *
 *   - 280..930 MHz horizontal axis.
 *   - Three ISM bands highlighted with their own color.
 *   - Current-freq cursor: triangle + vertical gradient line.
 *   - Freq text below. */
void ui_draw_freq_band(int x, int y, int w, int h, float freq_mhz);

/* Scrolling RSSI oscilloscope — time-series view, NEWEST sample on
 * right, history scrolling left. Call once per frame with the current
 * RSSI; the widget keeps its own internal history ring. Width MUST
 * stay the same across calls (history ring is sized to `w`).
 *
 * Height divided into 10 tiers, colored by strength band (weak red,
 * mid amber, strong green). Midline + floor annotations. */
void ui_draw_rssi_scope(int x, int y, int w, int h, int rssi_dbm);

/* Reset the RSSI scope ring — call on feature entry / exit so we don't
 * carry stale history into a new session. */
void ui_rssi_scope_reset(void);

/* Full-screen "LIVE TX" splash — concentric expanding wave rings radiating
 * from the center, freq + hex payload ticker across the middle. Takes
 * over the whole body area for the duration of the call (`duration_ms`).
 * Returns after the animation completes. Blocking. */
void ui_subghz_live_tx_splash(float freq_mhz,
                              const char *protocol,
                              uint32_t payload_hex,
                              uint32_t duration_ms);
