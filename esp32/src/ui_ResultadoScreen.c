// RESULTADO, escrita a mano (antes: export de SquareLine). Arbol del handoff:
//
// ui_ResultadoScreen               st_screen
// ├─ ui_ResultadoHeader            (0, 0, 320, 28)  st_header: "RESULTADO" + 4 iconos
// ├─ ui_ResultadoPanel             (8, 34, 304, 92) st_panel_ok | _warn | _crit | _neutral
// │  ├─ fila titulo                (100%, 22)       simbolo + titulo + chip de modo
// │  │  ├─ ui_ResultadoPanelSimbolo                 LV_SYMBOL_OK | STOP | CLOSE | "?"
// │  │  ├─ ui_ResultadoPanelTitulo                  COMPLETADO | DETENIDO | FALLA | NO INICIO | TERMINO
// │  │  └─ ui_ResultadoPanelModo   (alto 20)        st_mode_x | st_mode_null
// │  │     └─ ui_ResultadoPanelModoTexto            LV_SYMBOL_CHARGE " CAMPO X" | "CAMPO NULO"
// │  ├─ ui_ResultadoPanelTitular   (100%, 30)       FONT_12, 2 lineas con "..."
// │  └─ ui_ResultadoPanelDetalle   (100%, 24)       FONT_10, color del estado, 2 lineas con "..."
// ├─ ui_ResultadoResumen           (8, 132, 304, 40) tres tarjetas de 96 x 40
// │  ├─ PROGRESO      ui_ResultadoProgresoValor          "%"
// │  ├─ TRANSCURRIDO  ui_ResultadoTranscurridoValor      "m"
// │  └─ CAMPO MEDIO   ui_ResultadoCampoMedioUnidadValor  ui_ResultadoCampoMedioUnidad "mT"
// ├─ ui_ButtonResultadoPrincipal   (8, 180, 148, 40) secundario; (8, 180, 304, 40) primario sin REPETIR
// └─ ui_ButtonResultadoRepetir     (164, 180, 148, 40) primario
//
// Un solo panel con cinco variantes (el export tenia tres paneles
// superpuestos): las pinta ResultadoController. Los nombres que ya existian
// se conservan; los nuevos siguen la convencion ui_Resultado<Parte>.
//
// Desvio del handoff: con gap 3, titular de 30 y detalle de 24 el contenido
// del panel no entra en 92 px (borde 2 + pad 4 arriba y abajo) por 2 px, y
// el panel scrolleaba. El gap es 2.

#include "ui.h"
#include "ui_theme.h"
#include "ui_components.h"

lv_obj_t * ui_ResultadoScreen = NULL;
lv_obj_t * ui_ResultadoHeader = NULL;
lv_obj_t * ui_ResultadoTitle = NULL;
lv_obj_t * ui_ResultadoIconoMega = NULL;
lv_obj_t * ui_ResultadoIconoSd = NULL;
lv_obj_t * ui_ResultadoIconoWifi = NULL;
lv_obj_t * ui_ResultadoIconoBroker = NULL;
lv_obj_t * ui_ResultadoPanel = NULL;
lv_obj_t * ui_ResultadoPanelSimbolo = NULL;
lv_obj_t * ui_ResultadoPanelTitulo = NULL;
lv_obj_t * ui_ResultadoPanelModo = NULL;
lv_obj_t * ui_ResultadoPanelModoTexto = NULL;
lv_obj_t * ui_ResultadoPanelTitular = NULL;
lv_obj_t * ui_ResultadoPanelDetalle = NULL;
lv_obj_t * ui_ResultadoResumen = NULL;
lv_obj_t * ui_ResultadoProgresoValor = NULL;
lv_obj_t * ui_ResultadoTranscurridoValor = NULL;
lv_obj_t * ui_ResultadoCampoMedioUnidadValor = NULL;
lv_obj_t * ui_ResultadoCampoMedioUnidad = NULL;
lv_obj_t * ui_ButtonResultadoPrincipal = NULL;
lv_obj_t * ui_ButtonResultadoPrincipalTexto = NULL;
lv_obj_t * ui_ButtonResultadoRepetir = NULL;
lv_obj_t * ui_ButtonResultadoRepetirTexto = NULL;

// Tarjeta del resumen: titulo arriba, valor + unidad abajo alineados por la
// base. Devuelve el label del valor; el de la unidad en *unit.
static lv_obj_t* summary_card_create(lv_obj_t* parent, const char* title, const char* value,
                                     const char* unitText, lv_obj_t** unit)
{
    lv_obj_t* card = ui_card_create(parent);
    lv_obj_set_size(card, 96, 40);
    lv_obj_set_style_pad_ver(card, 2, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 2, 0);

    ui_label_create(card, &st_lbl, title);

    lv_obj_t* row = ui_box_create(card);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 3, 0);

    lv_obj_t* valueLabel = ui_label_create(row, &st_val16, value);
    lv_obj_t* unitLabel = ui_label_create(row, &st_lbl, unitText);
    if (unit) *unit = unitLabel;
    return valueLabel;
}

void ui_ResultadoScreen_screen_init(void)
{
    ui_ResultadoScreen = ui_screen_create();

    ui_ResultadoHeader = ui_header_create(ui_ResultadoScreen, "RESULTADO", &ui_ResultadoTitle);
    ui_status_icons_t icons;
    ui_header_add_status_icons(ui_ResultadoHeader, &icons);
    ui_ResultadoIconoMega = icons.mega;
    ui_ResultadoIconoSd = icons.sd;
    ui_ResultadoIconoWifi = icons.wifi;
    ui_ResultadoIconoBroker = icons.broker;

    // ---- Panel de motivo ----
    ui_ResultadoPanel = ui_box_create(ui_ResultadoScreen);
    lv_obj_add_style(ui_ResultadoPanel, &st_panel_ok, 0);
    lv_obj_set_pos(ui_ResultadoPanel, MARGEN, 34);
    lv_obj_set_size(ui_ResultadoPanel, 304, 92);
    lv_obj_set_style_pad_ver(ui_ResultadoPanel, PAD_SM, 0);
    lv_obj_set_style_pad_hor(ui_ResultadoPanel, PAD_LG, 0);
    lv_obj_set_flex_flow(ui_ResultadoPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui_ResultadoPanel, 2, 0);

    lv_obj_t* titleRow = ui_box_create(ui_ResultadoPanel);
    lv_obj_set_size(titleRow, lv_pct(100), 22);
    lv_obj_set_flex_flow(titleRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(titleRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(titleRow, PAD_MD, 0);

    ui_ResultadoPanelSimbolo = ui_label_create(titleRow, &st_f16, LV_SYMBOL_OK);
    ui_ResultadoPanelTitulo = ui_label_create(titleRow, &st_f16, "COMPLETADO");
    lv_obj_set_flex_grow(ui_ResultadoPanelTitulo, 1);

    ui_ResultadoPanelModo = ui_box_create(titleRow);
    lv_obj_add_style(ui_ResultadoPanelModo, &st_chip_sm, 0);
    lv_obj_add_style(ui_ResultadoPanelModo, &st_mode_x, 0);
    lv_obj_set_width(ui_ResultadoPanelModo, LV_SIZE_CONTENT);
    ui_ResultadoPanelModoTexto = ui_label_create(ui_ResultadoPanelModo, &st_f10, LV_SYMBOL_CHARGE " CAMPO X");
    lv_obj_center(ui_ResultadoPanelModoTexto);

    ui_ResultadoPanelTitular = ui_label_create(ui_ResultadoPanel, &st_txt12, "");
    lv_obj_set_size(ui_ResultadoPanelTitular, lv_pct(100), 30);
    lv_label_set_long_mode(ui_ResultadoPanelTitular, LV_LABEL_LONG_DOT);

    ui_ResultadoPanelDetalle = ui_label_create(ui_ResultadoPanel, &st_f10, "");
    lv_obj_set_size(ui_ResultadoPanelDetalle, lv_pct(100), 24);
    lv_label_set_long_mode(ui_ResultadoPanelDetalle, LV_LABEL_LONG_DOT);

    // ---- Resumen ----
    ui_ResultadoResumen = ui_box_create(ui_ResultadoScreen);
    lv_obj_set_pos(ui_ResultadoResumen, MARGEN, 132);
    lv_obj_set_size(ui_ResultadoResumen, 304, 40);
    lv_obj_set_flex_flow(ui_ResultadoResumen, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(ui_ResultadoResumen, PAD_LG, 0);

    ui_ResultadoProgresoValor = summary_card_create(ui_ResultadoResumen, "PROGRESO", "0", "%", NULL);
    ui_ResultadoTranscurridoValor = summary_card_create(ui_ResultadoResumen, "TRANSCURRIDO", "0 h 00", "m", NULL);
    ui_ResultadoCampoMedioUnidadValor = summary_card_create(ui_ResultadoResumen, "CAMPO MEDIO", "--", "mT",
                                                            &ui_ResultadoCampoMedioUnidad);

    // ---- Botones ----
    ui_ButtonResultadoPrincipal = ui_button_create(ui_ResultadoScreen, UI_BTN_SECONDARY, "PRINCIPAL",
                                                   &ui_ButtonResultadoPrincipalTexto);
    lv_obj_set_pos(ui_ButtonResultadoPrincipal, MARGEN, 180);
    lv_obj_set_size(ui_ButtonResultadoPrincipal, 148, ALTO_BOTON);

    ui_ButtonResultadoRepetir = ui_button_create(ui_ResultadoScreen, UI_BTN_PRIMARY, "REPETIR",
                                                 &ui_ButtonResultadoRepetirTexto);
    lv_obj_set_pos(ui_ButtonResultadoRepetir, 164, 180);
    lv_obj_set_size(ui_ButtonResultadoRepetir, 148, ALTO_BOTON);
}

void ui_ResultadoScreen_screen_destroy(void)
{
    if(ui_ResultadoScreen) lv_obj_delete(ui_ResultadoScreen);

    ui_ResultadoScreen = NULL;
    ui_ResultadoHeader = NULL;
    ui_ResultadoTitle = NULL;
    ui_ResultadoIconoMega = NULL;
    ui_ResultadoIconoSd = NULL;
    ui_ResultadoIconoWifi = NULL;
    ui_ResultadoIconoBroker = NULL;
    ui_ResultadoPanel = NULL;
    ui_ResultadoPanelSimbolo = NULL;
    ui_ResultadoPanelTitulo = NULL;
    ui_ResultadoPanelModo = NULL;
    ui_ResultadoPanelModoTexto = NULL;
    ui_ResultadoPanelTitular = NULL;
    ui_ResultadoPanelDetalle = NULL;
    ui_ResultadoResumen = NULL;
    ui_ResultadoProgresoValor = NULL;
    ui_ResultadoTranscurridoValor = NULL;
    ui_ResultadoCampoMedioUnidadValor = NULL;
    ui_ResultadoCampoMedioUnidad = NULL;
    ui_ButtonResultadoPrincipal = NULL;
    ui_ButtonResultadoPrincipalTexto = NULL;
    ui_ButtonResultadoRepetir = NULL;
    ui_ButtonResultadoRepetirTexto = NULL;
}
