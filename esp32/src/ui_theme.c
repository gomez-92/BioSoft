// Ver ui_theme.h.

#include "ui_theme.h"

lv_style_t st_screen;
lv_style_t st_bg_surface;
lv_style_t st_header;
lv_style_t st_hdr_ok;
lv_style_t st_hdr_warn;
lv_style_t st_hdr_crit;
lv_style_t st_icon_fail;
lv_style_t st_card;
lv_style_t st_lbl;
lv_style_t st_val16;
lv_style_t st_val24;
lv_style_t st_txt_center;
lv_style_t st_btn_pri;
lv_style_t st_btn_pri_pr;
lv_style_t st_btn_sec;
lv_style_t st_btn_sec_pr;
lv_style_t st_btn_dis;
lv_style_t st_btn_stop;
lv_style_t st_btn_stop_pr;
lv_style_t st_tab;
lv_style_t st_tab_chk;
lv_style_t st_mode_x;
lv_style_t st_mode_null;
lv_style_t st_field_dirty;
lv_style_t st_bar_main;
lv_style_t st_bar_ind;
lv_style_t st_dot_warn;
lv_style_t st_dot_crit;
lv_style_t st_panel_ok;
lv_style_t st_panel_warn;
lv_style_t st_panel_crit;
lv_style_t st_panel_neutral;
lv_style_t st_strip;
lv_style_t st_spinner;
lv_style_t st_spinner_ind;

static bool s_initialized = false;

static void init_bg(lv_style_t* s, lv_color_t color) {
    lv_style_set_bg_color(s, color);
    lv_style_set_bg_opa(s, LV_OPA_COVER);
}

static void init_text(lv_style_t* s, lv_color_t color, const lv_font_t* font) {
    lv_style_set_text_color(s, color);
    if (font) lv_style_set_text_font(s, font);
}

static void init_button(lv_style_t* s, lv_color_t bg, lv_color_t text) {
    lv_style_init(s);
    init_bg(s, bg);
    lv_style_set_radius(s, RADIO_MD);
    lv_style_set_border_width(s, 0);
    lv_style_set_shadow_width(s, 0);
    init_text(s, text, FONT_16);
}

static void init_pressed(lv_style_t* s, lv_color_t bg) {
    lv_style_init(s);
    lv_style_set_bg_color(s, bg);
}

static void init_panel(lv_style_t* s, lv_color_t border, lv_color_t bg, lv_color_t text) {
    lv_style_init(s);
    init_bg(s, bg);
    lv_style_set_radius(s, RADIO_MD);
    lv_style_set_border_width(s, 2);
    lv_style_set_border_color(s, border);
    lv_style_set_text_color(s, text);
}

static void init_dot(lv_style_t* s, lv_color_t color) {
    lv_style_init(s);
    lv_style_set_width(s, 8);
    lv_style_set_height(s, 8);
    lv_style_set_radius(s, 4);
    init_bg(s, color);
}

static void init_header_state(lv_style_t* s, lv_color_t bg, lv_color_t text) {
    lv_style_init(s);
    lv_style_set_bg_color(s, bg);
    lv_style_set_text_color(s, text);
}

void ui_theme_init(void) {
    if (s_initialized) return;
    s_initialized = true;

    lv_style_init(&st_screen);
    init_bg(&st_screen, COL_BG);
    lv_style_set_pad_all(&st_screen, 0);

    lv_style_init(&st_bg_surface);
    lv_style_set_bg_color(&st_bg_surface, COL_SURFACE);

    lv_style_init(&st_header);
    lv_style_set_width(&st_header, PANTALLA_ANCHO);
    lv_style_set_height(&st_header, ALTO_ENCABEZADO);
    init_bg(&st_header, COL_PRIMARY_DARK);
    lv_style_set_pad_left(&st_header, PAD_LG);
    lv_style_set_pad_right(&st_header, PAD_LG);
    lv_style_set_pad_top(&st_header, 0);
    lv_style_set_pad_bottom(&st_header, 0);
    lv_style_set_layout(&st_header, LV_LAYOUT_FLEX);
    lv_style_set_flex_flow(&st_header, LV_FLEX_FLOW_ROW);
    lv_style_set_flex_cross_place(&st_header, LV_FLEX_ALIGN_CENTER);
    lv_style_set_flex_track_place(&st_header, LV_FLEX_ALIGN_CENTER);
    lv_style_set_pad_column(&st_header, PAD_LG);
    init_text(&st_header, lv_color_white(), FONT_16);

    init_header_state(&st_hdr_ok, COL_OK, lv_color_white());
    // Sobre el ambar el blanco no llega a contraste legible: texto oscuro.
    init_header_state(&st_hdr_warn, COL_WARN, COL_TEXT);
    init_header_state(&st_hdr_crit, COL_CRIT, lv_color_white());

    lv_style_init(&st_icon_fail);
    init_bg(&st_icon_fail, lv_color_white());
    lv_style_set_radius(&st_icon_fail, RADIO_SM);
    lv_style_set_text_color(&st_icon_fail, COL_CRIT);

    lv_style_init(&st_card);
    init_bg(&st_card, COL_SURFACE);
    lv_style_set_border_width(&st_card, 1);
    lv_style_set_border_color(&st_card, COL_BORDER);
    lv_style_set_radius(&st_card, RADIO_SM);
    lv_style_set_pad_left(&st_card, PAD_MD);
    lv_style_set_pad_right(&st_card, PAD_MD);

    lv_style_init(&st_lbl);
    init_text(&st_lbl, COL_TEXT_2, FONT_10);

    lv_style_init(&st_val16);
    init_text(&st_val16, COL_TEXT, FONT_16);

    lv_style_init(&st_val24);
    init_text(&st_val24, COL_TEXT, FONT_24);

    lv_style_init(&st_txt_center);
    lv_style_set_text_align(&st_txt_center, LV_TEXT_ALIGN_CENTER);

    init_button(&st_btn_pri, COL_PRIMARY, lv_color_white());
    init_pressed(&st_btn_pri_pr, COL_PRIMARY_DARK);

    init_button(&st_btn_sec, lv_color_white(), COL_PRIMARY);
    lv_style_set_border_width(&st_btn_sec, 2);
    lv_style_set_border_color(&st_btn_sec, COL_PRIMARY);
    init_pressed(&st_btn_sec_pr, COL_PRIMARY_LIGHT);

    lv_style_init(&st_btn_dis);
    lv_style_set_bg_color(&st_btn_dis, COL_DISABLED_BG);
    lv_style_set_border_width(&st_btn_dis, 0);
    lv_style_set_text_color(&st_btn_dis, COL_DISABLED_TEXT);

    init_button(&st_btn_stop, COL_CRIT, lv_color_white());
    init_pressed(&st_btn_stop_pr, COL_CRIT_TEXT);

    lv_style_init(&st_tab);
    lv_style_set_height(&st_tab, ALTO_BOTON);
    lv_style_set_radius(&st_tab, RADIO_MD);
    init_bg(&st_tab, COL_PRIMARY_LIGHT);
    init_text(&st_tab, COL_PRIMARY_DARK, FONT_12);
    lv_style_init(&st_tab_chk);
    lv_style_set_bg_color(&st_tab_chk, COL_PRIMARY);
    lv_style_set_text_color(&st_tab_chk, lv_color_white());

    lv_style_init(&st_mode_x);
    init_bg(&st_mode_x, COL_MODE_X);
    lv_style_set_border_width(&st_mode_x, 2);
    lv_style_set_border_color(&st_mode_x, COL_MODE_X);
    lv_style_set_text_color(&st_mode_x, lv_color_white());

    lv_style_init(&st_mode_null);
    init_bg(&st_mode_null, lv_color_white());
    lv_style_set_border_width(&st_mode_null, 2);
    lv_style_set_border_color(&st_mode_null, COL_TEXT_2);
    lv_style_set_text_color(&st_mode_null, COL_TEXT);

    // Con borde de 2 px el contenido baja 1 px: el pad lo compensa. El color
    // de la etiqueta del campo se aplica sobre la etiqueta misma (fase 3).
    lv_style_init(&st_field_dirty);
    lv_style_set_border_width(&st_field_dirty, 2);
    lv_style_set_border_color(&st_field_dirty, COL_WARN);
    lv_style_set_pad_left(&st_field_dirty, 5);
    lv_style_set_pad_right(&st_field_dirty, 5);
    lv_style_set_pad_bottom(&st_field_dirty, 2);
    lv_style_set_pad_top(&st_field_dirty, 15);

    lv_style_init(&st_bar_main);
    init_bg(&st_bar_main, COL_PRIMARY_LIGHT);
    lv_style_set_radius(&st_bar_main, RADIO_SM);
    lv_style_init(&st_bar_ind);
    init_bg(&st_bar_ind, COL_PRIMARY);
    lv_style_set_radius(&st_bar_ind, RADIO_SM);

    init_dot(&st_dot_warn, COL_WARN);
    init_dot(&st_dot_crit, COL_CRIT);

    init_panel(&st_panel_ok, COL_OK, COL_OK_BG, COL_OK_TEXT);
    init_panel(&st_panel_warn, COL_WARN, COL_WARN_BG, COL_WARN_TEXT);
    init_panel(&st_panel_crit, COL_CRIT, COL_CRIT_BG, COL_CRIT_TEXT);
    init_panel(&st_panel_neutral, COL_NEUTRAL_TEXT, COL_NEUTRAL_BG, COL_NEUTRAL_TEXT);

    lv_style_init(&st_strip);
    lv_style_set_width(&st_strip, PANTALLA_ANCHO);
    lv_style_set_height(&st_strip, PANTALLA_ALTO - ZONA_RESERVADA_Y);
    init_bg(&st_strip, COL_WARN);
    init_text(&st_strip, COL_TEXT, FONT_10);
    lv_style_set_text_align(&st_strip, LV_TEXT_ALIGN_CENTER);

    lv_style_init(&st_spinner);
    lv_style_set_arc_width(&st_spinner, 6);
    lv_style_set_arc_color(&st_spinner, COL_PRIMARY_LIGHT);
    lv_style_init(&st_spinner_ind);
    lv_style_set_arc_width(&st_spinner_ind, 6);
    lv_style_set_arc_color(&st_spinner_ind, COL_PRIMARY);
}

lv_obj_t* ui_screen_create(void) {
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_add_style(screen, &st_screen, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    return screen;
}

lv_obj_t* ui_label_create(lv_obj_t* parent, lv_style_t* style, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    if (style) lv_obj_add_style(label, style, 0);
    lv_label_set_text(label, text);
    return label;
}
