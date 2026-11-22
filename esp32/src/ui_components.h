// Componentes de la interfaz escrita a mano (handoff, seccion Componentes):
// funciones que crean un grupo de objetos ya estilado y devuelven los
// punteros a sus partes dinamicas. Todos usan los estilos de ui_theme.

#ifndef UI_COMPONENTS_H
#define UI_COMPONENTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

typedef enum {
    UI_BTN_PRIMARY,    // INICIAR, GUARDAR, REPETIR, ENTENDIDO, PRINCIPAL solo
    UI_BTN_SECONDARY,  // CONFIGURAR, VOLVER/DESCARTAR, PRINCIPAL, CANCELAR
    UI_BTN_STOP,       // DETENER: el unico boton rojo solido
} ui_button_kind_t;

// Los cuatro iconos de estado del encabezado, en el orden en que se ven.
typedef struct {
    lv_obj_t* mega;
    lv_obj_t* sd;
    lv_obj_t* wifi;
    lv_obj_t* broker;
} ui_status_icons_t;

// Encabezado de 320 x 28 en (0, 0): st_header, flex row, titulo con
// flex_grow 1. Devuelve el contenedor; el titulo va en *title si no es NULL.
lv_obj_t* ui_header_create(lv_obj_t* parent, const char* text, lv_obj_t** title);

// Contenedor de 92 x 20 con los 4 iconos (MEGA, SD, WIFI, BROKER) al final
// del encabezado. Arrancan en OK.
void ui_header_add_status_icons(lv_obj_t* header, ui_status_icons_t* out);

// OK: simbolo en el color del texto del encabezado, sin fondo.
// FALLA: chip blanco de 20 x 20 con el simbolo en COL_CRIT.
void ui_status_icon_set(lv_obj_t* icon, bool ok);

// Boton con su label centrado. Lleva los estilos normal, presionado y
// deshabilitado (lv_obj_add_state(btn, LV_STATE_DISABLED)).
lv_obj_t* ui_button_create(lv_obj_t* parent, ui_button_kind_t kind, const char* text, lv_obj_t** label);

// Cambia el aspecto de un boton creado con ui_button_create.
void ui_button_set_kind(lv_obj_t* button, ui_button_kind_t kind);

// Tarjeta: contenedor con st_card, sin scroll ni click. El contenido lo
// arma cada pantalla.
lv_obj_t* ui_card_create(lv_obj_t* parent);

// Contenedor sin estilos (ni fondo, ni borde, ni pad), sin scroll ni click:
// para filas y columnas de layout.
lv_obj_t* ui_box_create(lv_obj_t* parent);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
