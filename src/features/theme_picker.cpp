/*
 * theme_picker.cpp — visual theme selector with live preview.
 *
 * Browses POSEIDON / MATRIX / E-INK with arrow keys, previewing each
 * in-RAM only (no NVS thrash). ENTER commits the choice to NVS; ESC
 * restores the original.
 */
#include "../app.h"
#include "../ui.h"
#include "../input.h"
#include "../theme.h"

void feat_theme_picker(void)
{
    auto &d = PoseidonDisplay;
    theme_id_t original = theme_current_id();  /* for ESC-restore */
    int sel = (int)original;

    int prev_sel = -1;
    while (true) {
        /* Preview only — no NVS writes during browsing. */
        theme_preview((theme_id_t)sel);
        if (sel != prev_sel) {
            ui_force_clear_body();
            poseidon_ext_invalidate();
            prev_sel = sel;
        }
        ui_draw_status("theme", "");

        d.setTextColor(T_ACCENT, T_BG);
        d.setCursor(4, BODY_Y + 2); d.print("THEME");
        d.drawFastHLine(4, BODY_Y + 12, SCR_W - 8, T_ACCENT2);

        for (int i = 0; i < THEME__COUNT; i++) {
            int y = BODY_Y + 18 + i * 14;
            /* Flip palette just to render this row's swatches. Still
             * RAM-only — cheap. */
            theme_preview((theme_id_t)i);
            bool s = (i == sel);
            if (s) {
                d.fillRoundRect(2, y - 2, SCR_W - 4, 13, 2, T_SEL_BG);
                d.drawRoundRect(2, y - 2, SCR_W - 4, 13, 2, T_SEL_BD);
            }
            uint16_t bg = s ? T_SEL_BG : theme().bg;
            d.setTextColor(T_ACCENT, bg);
            d.setCursor(8, y);
            d.printf("%s", theme().name);
            /* Color swatches. */
            d.fillRect(140, y, 12, 8, T_ACCENT);
            d.fillRect(154, y, 12, 8, theme().accent2);
            d.fillRect(168, y, 12, 8, T_GOOD);
            d.fillRect(182, y, 12, 8, T_BAD);
            d.fillRect(196, y, 12, 8, T_DIM);
        }
        theme_preview((theme_id_t)sel);   /* back to browsed theme after swatch loop */

        /* Picker chrome stays on the menu panel. EXT gets a full card
         * for this theme, including E-INK (white bg, no ambient). The
         * cache is dropped when the selection changes so a previous
         * solid fill cannot stay up with no chrome. */
        poseidon_ext_bus_idle();
        poseidon_content_show_selection("Theme", theme().name,
                                        "menu and content share this palette");

        ui_draw_footer(";/.=browse  ENTER=apply  ESC=back");

        uint16_t k = input_poll();
        if (k == PK_NONE) { delay(20); continue; }
        if (k == PK_ESC) {
            /* Restore original in RAM — NVS still holds original, nothing
             * to write. Repaint EXT so the restored palette is on glass. */
            theme_preview(original);
            poseidon_ext_bus_idle();
            poseidon_ext_invalidate();
            poseidon_content_show_selection("Theme", theme().name,
                                            "menu and content share this palette");
            return;
        }
        if (k == ';' || k == PK_UP)   sel = (sel - 1 + THEME__COUNT) % THEME__COUNT;
        if (k == '.' || k == PK_DOWN) sel = (sel + 1) % THEME__COUNT;
        if (k == PK_ENTER) {
            /* Park EXT CS before the NVS flash write. A stall mid-RAMWR
             * on SPI3 leaves the ILI9341 backlight-only. */
            poseidon_ext_bus_idle();
            theme_set((theme_id_t)sel);    /* THE ONE NVS write — commit */
            poseidon_ext_invalidate();
            poseidon_content_show_selection("Theme", theme().name,
                                            "menu and content share this palette");
            d.fillScreen(T_BG);
            ui_toast("theme applied", T_GOOD, 600);
            return;
        }
    }
}
