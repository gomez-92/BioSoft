// ESPERANDO, escrita a mano (antes: export de SquareLine). Arbol del handoff:
//
// ui_EsperandoScreen       (0, 0, 320, 240)  st_screen
// ├─ ui_EsperandoSpinner   (128, 60, 64, 64) tema + st_spinner / st_spinner_ind
// ├─ ui_EsperandoMensaje   (0, 140, 320, -)  FONT_16 centrado: "Iniciando..." | "Deteniendo..."
// └─ ui_EsperandoSubtitulo (0, 166, 320, -)  st_lbl centrado
//
// El timeout de esta pantalla NO es un lv_timer: sigue en
// BusyController::update() (Intervals::BusyTimeout), con su manejo en
// MySystem. El mensaje lo pone BusyController::show().

#include "ui.h"
#include "ui_theme.h"

lv_obj_t * ui_EsperandoScreen = NULL;
lv_obj_t * ui_EsperandoSpinner = NULL;
lv_obj_t * ui_EsperandoMensaje = NULL;
lv_obj_t * ui_EsperandoSubtitulo = NULL;

void ui_EsperandoScreen_screen_init(void)
{
    ui_EsperandoScreen = ui_screen_create();

    // El spinner se queda con el tema (es el que dibuja los dos arcos y lo
    // anima); solo se le cambian ancho y colores.
    ui_EsperandoSpinner = lv_spinner_create(ui_EsperandoScreen);
    lv_obj_set_pos(ui_EsperandoSpinner, 128, 60);
    lv_obj_set_size(ui_EsperandoSpinner, 64, 64);
    lv_obj_remove_flag(ui_EsperandoSpinner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(ui_EsperandoSpinner, &st_spinner, LV_PART_MAIN);
    lv_obj_add_style(ui_EsperandoSpinner, &st_spinner_ind, LV_PART_INDICATOR);

    ui_EsperandoMensaje = ui_label_create(ui_EsperandoScreen, &st_val16, "Procesando...");
    lv_obj_add_style(ui_EsperandoMensaje, &st_txt_center, 0);
    lv_obj_set_pos(ui_EsperandoMensaje, 0, 140);
    lv_obj_set_width(ui_EsperandoMensaje, PANTALLA_ANCHO);

    ui_EsperandoSubtitulo = ui_label_create(ui_EsperandoScreen, &st_lbl, "ESPERANDO CONFIRMACION DEL MEGA");
    lv_obj_add_style(ui_EsperandoSubtitulo, &st_txt_center, 0);
    lv_obj_set_pos(ui_EsperandoSubtitulo, 0, 166);
    lv_obj_set_width(ui_EsperandoSubtitulo, PANTALLA_ANCHO);
}

void ui_EsperandoScreen_screen_destroy(void)
{
    if(ui_EsperandoScreen) lv_obj_delete(ui_EsperandoScreen);

    ui_EsperandoScreen = NULL;
    ui_EsperandoSpinner = NULL;
    ui_EsperandoMensaje = NULL;
    ui_EsperandoSubtitulo = NULL;
}
