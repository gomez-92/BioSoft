// Tema propio de la interfaz: tokens (colores, fuentes, medidas) y los
// estilos compartidos del handoff de diseño
// (https://claude.ai/artifact/W23df1TQhAL4tFxQ8QX4yX).
//
// Por que estilos compartidos: SquareLine le pone a cada objeto sus propios
// estilos locales, y cada uno es una reserva en el mismo heap del que sale
// el handshake TLS (LV_USE_STDLIB_MALLOC en CLIB). Estos se inicializan UNA
// vez al arrancar -- antes de que el heap se fragmente -- y todas las
// pantallas los reusan, asi que crear un objeto ya no reserva estilos.
//
// Regla para las pantallas escritas a mano: cada objeto propio se crea, se le
// hace lv_obj_remove_style_all() y se le agregan estos estilos (asi no carga
// los del tema por defecto). Spinner, dropdown y su lista son la excepcion:
// toman el tema como base y se les agrega lo nuestro encima.
//
// Un estado distinto (presionado, deshabilitado, checked) necesita su propio
// lv_style_t agregado con ese selector: por eso st_btn_pri tiene st_btn_pri_pr.

#ifndef UI_THEME_H
#define UI_THEME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* ---- Colores (23) ---- */
#define COL_BG            lv_color_hex(0xEEF1F4)
#define COL_SURFACE       lv_color_hex(0xFFFFFF)
#define COL_BORDER        lv_color_hex(0xC9CED4)
#define COL_TEXT          lv_color_hex(0x1D1F20)
#define COL_TEXT_2        lv_color_hex(0x5F6670)
#define COL_PRIMARY       lv_color_hex(0x2E628F)
#define COL_PRIMARY_DARK  lv_color_hex(0x0A3358)
#define COL_PRIMARY_LIGHT lv_color_hex(0xCFE3FA)
#define COL_DISABLED_BG   lv_color_hex(0xE3E6EA)
#define COL_DISABLED_TEXT lv_color_hex(0x9AA0A8)
#define COL_OK            lv_color_hex(0x2E7D32)
#define COL_OK_BG         lv_color_hex(0xDCEFDD)
#define COL_OK_TEXT       lv_color_hex(0x1E5A22)
#define COL_WARN          lv_color_hex(0xC47F08)
#define COL_WARN_BG       lv_color_hex(0xFCEBC9)
#define COL_WARN_TEXT     lv_color_hex(0x7A4F00)
#define COL_CRIT          lv_color_hex(0xC1121F)
#define COL_CRIT_BG       lv_color_hex(0xFBD5D2)
#define COL_CRIT_TEXT     lv_color_hex(0x9B0E19)
#define COL_NEUTRAL_BG    lv_color_hex(0xE6E8EB)
#define COL_NEUTRAL_TEXT  lv_color_hex(0x3F454C)
#define COL_MODE_X        lv_color_hex(0x5E2B97)
#define COL_MODE_X_SUB    lv_color_hex(0xD9C8EE)

/* ---- Fuentes (4). Montserrat de LVGL: solo ASCII, sin tildes ni ° ---- */
#define FONT_10 (&lv_font_montserrat_10)
#define FONT_12 (&lv_font_montserrat_12)
#define FONT_16 (&lv_font_montserrat_16)
#define FONT_24 (&lv_font_montserrat_24)

/* ---- Medidas ---- */
#define RADIO_SM        4
#define RADIO_MD        6
#define PAD_SM          4
#define PAD_MD          6
#define PAD_LG          8
#define MARGEN          8
#define GAP_BOTONES     8
#define ALTO_BOTON      40
#define ALTO_ENCABEZADO 28
#define PANTALLA_ANCHO  320
#define PANTALLA_ALTO   240
// Ninguna pantalla pone contenido en y 228-240: ahi va la franja MODO PRUEBA.
#define ZONA_RESERVADA_Y 228

/* ---- Estilos compartidos ---- */
extern lv_style_t st_screen;        // raiz de pantalla: bg COL_BG
extern lv_style_t st_bg_surface;    // encima de st_screen: bg blanco (SPLASH)
extern lv_style_t st_header;
extern lv_style_t st_hdr_ok;
extern lv_style_t st_hdr_warn;
extern lv_style_t st_hdr_crit;
extern lv_style_t st_icon_fail;
extern lv_style_t st_card;
extern lv_style_t st_lbl;           // FONT_10, COL_TEXT_2
extern lv_style_t st_val16;         // FONT_16, COL_TEXT
extern lv_style_t st_val24;         // FONT_24, COL_TEXT
extern lv_style_t st_txt_center;    // solo text_align center
extern lv_style_t st_btn_pri;
extern lv_style_t st_btn_pri_pr;    // LV_STATE_PRESSED
extern lv_style_t st_btn_sec;
extern lv_style_t st_btn_sec_pr;    // LV_STATE_PRESSED
extern lv_style_t st_btn_dis;       // LV_STATE_DISABLED
extern lv_style_t st_btn_stop;
extern lv_style_t st_btn_stop_pr;   // LV_STATE_PRESSED
extern lv_style_t st_tab;
extern lv_style_t st_tab_chk;       // LV_STATE_CHECKED
extern lv_style_t st_mode_x;
extern lv_style_t st_mode_null;
extern lv_style_t st_field_dirty;
extern lv_style_t st_bar_main;      // LV_PART_MAIN
extern lv_style_t st_bar_ind;       // LV_PART_INDICATOR
extern lv_style_t st_dot_warn;
extern lv_style_t st_dot_crit;
extern lv_style_t st_panel_ok;
extern lv_style_t st_panel_warn;
extern lv_style_t st_panel_crit;
extern lv_style_t st_panel_neutral;
extern lv_style_t st_strip;
extern lv_style_t st_spinner;       // LV_PART_MAIN (encima del tema)
extern lv_style_t st_spinner_ind;   // LV_PART_INDICATOR (encima del tema)

// Una sola vez, despues de lv_init() y antes de crear cualquier pantalla.
// Llamarla dos veces no hace nada.
void ui_theme_init(void);

/* ---- Ayudas para crear objetos propios ---- */

// Pantalla raiz sin estilos del tema, con st_screen y sin scroll.
lv_obj_t* ui_screen_create(void);

// Label sin estilos del tema, con `style` (puede ser NULL) y `text`.
lv_obj_t* ui_label_create(lv_obj_t* parent, lv_style_t* style, const char* text);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
