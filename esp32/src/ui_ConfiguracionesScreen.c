// CONFIGURACION, escrita a mano (antes: export de SquareLine). Arbol del
// handoff:
//
// ui_ConfiguracionesScreen               st_screen
// ├─ ui_ConfiguracionesHeader            (0, 0, 320, 28) "CONFIGURACION" + chip SIN GUARDAR
// │  └─ ui_ConfiguracionSinGuardar                       oculto sin cambios
// ├─ ui_ConfiguracionTabExposicion       (8, 34, 148, 40)   pestania + punto ambar
// ├─ ui_ConfiguracionTabLimites          (164, 34, 148, 40) pestania + punto ambar
// ├─ ui_ConfiguracionPaginaExposicion    (8, 82, 304, 88)   2 x 2 campos
// │  ├─ ui_ConfiguracionModoOpciones             MODO (+ cuadrito de modo)
// │  ├─ ui_ConfiguracionIntensidadCampoOpciones  INTENSIDAD
// │  ├─ ui_ConfiguracionFrecuenciaOpciones       FRECUENCIA
// │  └─ ui_ConfiguracionDuracionOpciones         DURACION
// ├─ ui_ConfiguracionPaginaLimites       (8, 82, 304, 88)   oculta salvo en LIMITES
// │  ├─ ui_ConfiguracionTolIntensidadOpciones    TOL. INTENSIDAD
// │  ├─ ui_ConfiguracionTempNormalOpciones       TEMP. NORMAL
// │  └─ ui_ConfiguracionTempCriticaOpciones      TEMP. CRITICA
// ├─ ui_ButtonConfiguracionVolver        (8, 180, 148, 40)  "VOLVER" | "DESCARTAR"
// └─ ui_ButtonConfiguracionGuardar       (164, 180, 148, 40) DISABLED sin cambios
//
// Cada campo es un lv_dropdown de 148 x 40 que conserva el tema (abre la
// lista y dibuja la flecha) con st_card + st_field encima. Sus hijos, en
// este orden (las ayudas de abajo dependen de el):
//   0: etiqueta del campo, 1: valor, 2: cuadrito de modo (solo MODO).
// El texto propio del dropdown queda vacio: no puede cortar con "...", y las
// etiquetas de los menus vienen de la SD con hasta 15 caracteres, mas de lo
// que entra. El valor va en un label con LV_LABEL_LONG_DOT que pinta
// ui_config_field_refresh(), tambien con "--" cuando un rango de
// temperatura quedo sin opcion compatible.
//
// Las pestanias no son CHECKABLE: tocar la activa no tiene que desmarcarla.
// Las cambia ui_config_show_tab().

#include "ui.h"
#include "ui_theme.h"
#include "ui_components.h"

lv_obj_t * ui_ConfiguracionesScreen = NULL;
lv_obj_t * ui_ConfiguracionesHeader = NULL;
lv_obj_t * ui_ConfiguracionesTitle = NULL;
lv_obj_t * ui_ConfiguracionSinGuardar = NULL;
lv_obj_t * ui_ConfiguracionTabExposicion = NULL;
lv_obj_t * ui_ConfiguracionTabExposicionPunto = NULL;
lv_obj_t * ui_ConfiguracionTabLimites = NULL;
lv_obj_t * ui_ConfiguracionTabLimitesPunto = NULL;
lv_obj_t * ui_ConfiguracionPaginaExposicion = NULL;
lv_obj_t * ui_ConfiguracionPaginaLimites = NULL;
lv_obj_t * ui_ConfiguracionModoOpciones = NULL;
lv_obj_t * ui_ConfiguracionIntensidadCampoOpciones = NULL;
lv_obj_t * ui_ConfiguracionFrecuenciaOpciones = NULL;
lv_obj_t * ui_ConfiguracionDuracionOpciones = NULL;
lv_obj_t * ui_ConfiguracionTolIntensidadOpciones = NULL;
lv_obj_t * ui_ConfiguracionTempNormalOpciones = NULL;
lv_obj_t * ui_ConfiguracionTempCriticaOpciones = NULL;
lv_obj_t * ui_ButtonConfiguracionVolver = NULL;
lv_obj_t * ui_ButtonConfiguracionVolverTexto = NULL;
lv_obj_t * ui_ButtonConfiguracionGuardar = NULL;
lv_obj_t * ui_ButtonConfiguracionGuardarTexto = NULL;

#define CAMPO_HIJO_NOMBRE 0
#define CAMPO_HIJO_VALOR  1
#define CAMPO_HIJO_MODO   2

// Ancho del valor: 148 - pad 6 x 2 - flecha (~20).
#define ANCHO_VALOR 116
// El cuadrito de modo va a la izquierda del valor.
#define ANCHO_CUADRITO 18

static lv_obj_t* tab_create(int32_t x, const char* text, lv_obj_t** dot)
{
    lv_obj_t* tab = lv_button_create(ui_ConfiguracionesScreen);
    lv_obj_remove_style_all(tab);
    lv_obj_add_style(tab, &st_tab, 0);
    lv_obj_add_style(tab, &st_tab_chk, LV_STATE_CHECKED);
    lv_obj_remove_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(tab, x, 34);
    lv_obj_set_width(tab, 148);

    lv_obj_t* label = ui_label_create(tab, NULL, text);
    lv_obj_center(label);

    *dot = ui_box_create(tab);
    lv_obj_add_style(*dot, &st_dot_warn, 0);
    lv_obj_align(*dot, LV_ALIGN_RIGHT_MID, -PAD_LG, 0);
    lv_obj_add_flag(*dot, LV_OBJ_FLAG_HIDDEN);
    return tab;
}

static lv_obj_t* page_create(void)
{
    lv_obj_t* page = ui_box_create(ui_ConfiguracionesScreen);
    lv_obj_set_pos(page, MARGEN, 82);
    lv_obj_set_size(page, 304, 88);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(page, PAD_LG, 0);
    lv_obj_set_style_pad_column(page, PAD_LG, 0);
    return page;
}

static lv_obj_t* field_create(lv_obj_t* page, const char* name, bool modeSquare)
{
    lv_obj_t* dd = lv_dropdown_create(page);
    lv_obj_add_style(dd, &st_card, 0);
    lv_obj_add_style(dd, &st_field, 0);
    lv_obj_add_style(dd, &st_field_symbol, LV_PART_INDICATOR);
    lv_obj_remove_flag(dd, LV_OBJ_FLAG_SCROLLABLE);
    lv_dropdown_set_text(dd, "");

    // La lista la crea lv_dropdown_create sobre la pantalla (no es hija del
    // dropdown) y se destruye con ella.
    lv_obj_t* list = lv_dropdown_get_list(dd);
    lv_obj_add_style(list, &st_dd_list, 0);
    lv_obj_add_style(list, &st_dd_sel, LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_add_style(list, &st_dd_sel, LV_PART_SELECTED | LV_STATE_PRESSED);

    // Las posiciones de los hijos son relativas al area de contenido, que
    // empieza en pad_top 16: la etiqueta sube a y 3 del campo.
    lv_obj_t* label = ui_label_create(dd, &st_lbl, name);
    lv_obj_set_pos(label, 0, -13);

    lv_obj_t* value = ui_label_create(dd, &st_val16, "");
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(value, modeSquare ? ANCHO_CUADRITO : 0, 0);
    lv_obj_set_width(value, modeSquare ? ANCHO_VALOR - ANCHO_CUADRITO : ANCHO_VALOR);

    if (modeSquare) {
        lv_obj_t* square = ui_box_create(dd);
        lv_obj_add_style(square, &st_mode_square, 0);
        lv_obj_add_style(square, &st_mode_x, 0);
        lv_obj_set_pos(square, 0, 3);
    }
    return dd;
}

void ui_ConfiguracionesScreen_screen_init(void)
{
    ui_ConfiguracionesScreen = ui_screen_create();

    ui_ConfiguracionesHeader = ui_header_create(ui_ConfiguracionesScreen, "CONFIGURACION", &ui_ConfiguracionesTitle);
    ui_ConfiguracionSinGuardar = ui_label_create(ui_ConfiguracionesHeader, &st_chip_dirty, "SIN GUARDAR");
    lv_obj_add_flag(ui_ConfiguracionSinGuardar, LV_OBJ_FLAG_HIDDEN);

    ui_ConfiguracionTabExposicion = tab_create(MARGEN, "EXPOSICION", &ui_ConfiguracionTabExposicionPunto);
    ui_ConfiguracionTabLimites = tab_create(164, "LIMITES", &ui_ConfiguracionTabLimitesPunto);

    ui_ConfiguracionPaginaExposicion = page_create();
    ui_ConfiguracionModoOpciones = field_create(ui_ConfiguracionPaginaExposicion, "MODO", true);
    ui_ConfiguracionIntensidadCampoOpciones = field_create(ui_ConfiguracionPaginaExposicion, "INTENSIDAD", false);
    ui_ConfiguracionFrecuenciaOpciones = field_create(ui_ConfiguracionPaginaExposicion, "FRECUENCIA", false);
    ui_ConfiguracionDuracionOpciones = field_create(ui_ConfiguracionPaginaExposicion, "DURACION", false);

    ui_ConfiguracionPaginaLimites = page_create();
    ui_ConfiguracionTolIntensidadOpciones = field_create(ui_ConfiguracionPaginaLimites, "TOL. INTENSIDAD", false);
    ui_ConfiguracionTempNormalOpciones = field_create(ui_ConfiguracionPaginaLimites, "TEMP. NORMAL", false);
    ui_ConfiguracionTempCriticaOpciones = field_create(ui_ConfiguracionPaginaLimites, "TEMP. CRITICA", false);

    ui_ButtonConfiguracionVolver = ui_button_create(ui_ConfiguracionesScreen, UI_BTN_SECONDARY, "VOLVER",
                                                    &ui_ButtonConfiguracionVolverTexto);
    lv_obj_set_pos(ui_ButtonConfiguracionVolver, MARGEN, 180);
    lv_obj_set_size(ui_ButtonConfiguracionVolver, 148, ALTO_BOTON);

    ui_ButtonConfiguracionGuardar = ui_button_create(ui_ConfiguracionesScreen, UI_BTN_PRIMARY, "GUARDAR",
                                                     &ui_ButtonConfiguracionGuardarTexto);
    lv_obj_set_pos(ui_ButtonConfiguracionGuardar, 164, 180);
    lv_obj_set_size(ui_ButtonConfiguracionGuardar, 148, ALTO_BOTON);
    lv_obj_add_state(ui_ButtonConfiguracionGuardar, LV_STATE_DISABLED);

    ui_config_show_tab(false);
}

void ui_ConfiguracionesScreen_screen_destroy(void)
{
    if(ui_ConfiguracionesScreen) lv_obj_delete(ui_ConfiguracionesScreen);

    ui_ConfiguracionesScreen = NULL;
    ui_ConfiguracionesHeader = NULL;
    ui_ConfiguracionesTitle = NULL;
    ui_ConfiguracionSinGuardar = NULL;
    ui_ConfiguracionTabExposicion = NULL;
    ui_ConfiguracionTabExposicionPunto = NULL;
    ui_ConfiguracionTabLimites = NULL;
    ui_ConfiguracionTabLimitesPunto = NULL;
    ui_ConfiguracionPaginaExposicion = NULL;
    ui_ConfiguracionPaginaLimites = NULL;
    ui_ConfiguracionModoOpciones = NULL;
    ui_ConfiguracionIntensidadCampoOpciones = NULL;
    ui_ConfiguracionFrecuenciaOpciones = NULL;
    ui_ConfiguracionDuracionOpciones = NULL;
    ui_ConfiguracionTolIntensidadOpciones = NULL;
    ui_ConfiguracionTempNormalOpciones = NULL;
    ui_ConfiguracionTempCriticaOpciones = NULL;
    ui_ButtonConfiguracionVolver = NULL;
    ui_ButtonConfiguracionVolverTexto = NULL;
    ui_ButtonConfiguracionGuardar = NULL;
    ui_ButtonConfiguracionGuardarTexto = NULL;
}

void ui_config_show_tab(bool limits)
{
    if (ui_ConfiguracionesScreen == NULL) return;
    if (limits) {
        lv_obj_remove_state(ui_ConfiguracionTabExposicion, LV_STATE_CHECKED);
        lv_obj_add_state(ui_ConfiguracionTabLimites, LV_STATE_CHECKED);
        lv_obj_add_flag(ui_ConfiguracionPaginaExposicion, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui_ConfiguracionPaginaLimites, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_state(ui_ConfiguracionTabExposicion, LV_STATE_CHECKED);
        lv_obj_remove_state(ui_ConfiguracionTabLimites, LV_STATE_CHECKED);
        lv_obj_remove_flag(ui_ConfiguracionPaginaExposicion, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_ConfiguracionPaginaLimites, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_config_field_refresh(lv_obj_t* field, bool hasSelection)
{
    if (field == NULL) return;
    lv_obj_t* value = lv_obj_get_child(field, CAMPO_HIJO_VALOR);
    if (hasSelection) {
        char text[40];
        lv_dropdown_get_selected_str(field, text, sizeof(text));
        lv_label_set_text(value, text);
    }
    else {
        lv_label_set_text(value, "--");
    }

    // MODO: indice 0 = campo X, 1 = campo nulo (ConfigurationOptions::optionsFieldMode).
    lv_obj_t* square = lv_obj_get_child(field, CAMPO_HIJO_MODO);
    if (square) {
        bool nullField = hasSelection && lv_dropdown_get_selected(field) == 1;
        lv_obj_remove_style(square, &st_mode_x, 0);
        lv_obj_remove_style(square, &st_mode_null, 0);
        lv_obj_add_style(square, nullField ? &st_mode_null : &st_mode_x, 0);
    }
}

void ui_config_field_set_dirty(lv_obj_t* field, bool dirty)
{
    if (field == NULL) return;
    lv_obj_t* name = lv_obj_get_child(field, CAMPO_HIJO_NOMBRE);
    lv_obj_remove_style(field, &st_field_dirty, 0);
    lv_obj_remove_style(name, &st_lbl_dirty, 0);
    if (dirty) {
        lv_obj_add_style(field, &st_field_dirty, 0);
        lv_obj_add_style(name, &st_lbl_dirty, 0);
    }
}
