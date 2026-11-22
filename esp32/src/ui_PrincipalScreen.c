// PRINCIPAL, escrita a mano (antes: export de SquareLine). Arbol del handoff:
//
// ui_PrincipalScreen                st_screen
// ├─ ui_PrincipalHeader             (0, 0, 320, 28)   "PRINCIPAL" + 4 iconos
// ├─ ui_PrincipalModoBanda          (8, 34, 304, 32)  st_mode_x | st_mode_null
// │  ├─ ui_PrincipalModoTag                           "MODO" (COL_MODE_X_SUB | COL_TEXT_2)
// │  ├─ ui_PrincipalModoSimbolo                       LV_SYMBOL_CHARGE (oculto en campo nulo)
// │  └─ ui_PrincipalModoValor                         "CAMPO X" | "CAMPO NULO"
// ├─ datos                          (8, 72, 304, 102) flex row wrap: tarjetas de 136 / 160 x 30
// │  ├─ INTENSIDAD      ui_PrincipalIntensidadCampoValor   mT
// │  ├─ TOL. INTENSIDAD ui_PrincipalTolIntensidadValor     %
// │  ├─ FRECUENCIA      ui_PrincipalFrecuenciaValor        Hz
// │  ├─ TEMP. NORMAL    ui_PrincipalTempNormalValor        C
// │  ├─ DURACION        ui_PrincipalDuracionHorasValor h   ui_PrincipalDuracionMinutosValor m
// │  └─ TEMP. CRITICA   ui_PrincipalTempCriticaValor       C
// ├─ ui_ButtonPrincipalConfigurar   (8, 180, 148, 40)  secundario
// └─ ui_ButtonPrincipalIniciar      (164, 180, 148, 40) primario; DISABLED + "SIN CONEXION"
//                                                       si se exige el Mega y no esta
//
// Los nombres que ya existian se conservan; los nuevos siguen la convencion
// ui_Principal<Parte>. Lo dinamico (modo, valores, iconos, INICIAR) lo pinta
// PrincipalController.

#include "ui.h"
#include "ui_theme.h"
#include "ui_components.h"

lv_obj_t * ui_PrincipalScreen = NULL;
lv_obj_t * ui_PrincipalHeader = NULL;
lv_obj_t * ui_PrincipalTitle = NULL;
lv_obj_t * ui_PrincipalIconoMega = NULL;
lv_obj_t * ui_PrincipalIconoSd = NULL;
lv_obj_t * ui_PrincipalIconoWifi = NULL;
lv_obj_t * ui_PrincipalIconoBroker = NULL;
lv_obj_t * ui_PrincipalModoBanda = NULL;
lv_obj_t * ui_PrincipalModoTag = NULL;
lv_obj_t * ui_PrincipalModoSimbolo = NULL;
lv_obj_t * ui_PrincipalModoValor = NULL;
lv_obj_t * ui_PrincipalDatos = NULL;
lv_obj_t * ui_PrincipalIntensidadCampoValor = NULL;
lv_obj_t * ui_PrincipalTolIntensidadValor = NULL;
lv_obj_t * ui_PrincipalFrecuenciaValor = NULL;
lv_obj_t * ui_PrincipalTempNormalValor = NULL;
lv_obj_t * ui_PrincipalDuracionHorasValor = NULL;
lv_obj_t * ui_PrincipalDuracionMinutosValor = NULL;
lv_obj_t * ui_PrincipalTempCriticaValor = NULL;
lv_obj_t * ui_ButtonPrincipalConfigurar = NULL;
lv_obj_t * ui_ButtonPrincipalConfigurarTexto = NULL;
lv_obj_t * ui_ButtonPrincipalIniciar = NULL;
lv_obj_t * ui_ButtonPrincipalIniciarTexto = NULL;

// Columna izquierda 136, derecha 160: 136 + 8 + 160 = 304.
#define ANCHO_IZQ 136
#define ANCHO_DER 160
#define ALTO_DATO 30

void ui_PrincipalScreen_screen_init(void)
{
    ui_PrincipalScreen = ui_screen_create();

    ui_PrincipalHeader = ui_header_create(ui_PrincipalScreen, "PRINCIPAL", &ui_PrincipalTitle);
    ui_status_icons_t icons;
    ui_header_add_status_icons(ui_PrincipalHeader, &icons);
    ui_PrincipalIconoMega = icons.mega;
    ui_PrincipalIconoSd = icons.sd;
    ui_PrincipalIconoWifi = icons.wifi;
    ui_PrincipalIconoBroker = icons.broker;

    // ---- Banda de modo ----
    ui_PrincipalModoBanda = ui_box_create(ui_PrincipalScreen);
    lv_obj_add_style(ui_PrincipalModoBanda, &st_mode_x, 0);
    lv_obj_set_style_radius(ui_PrincipalModoBanda, RADIO_MD, 0);
    lv_obj_set_pos(ui_PrincipalModoBanda, MARGEN, 34);
    lv_obj_set_size(ui_PrincipalModoBanda, 304, 32);
    lv_obj_set_style_pad_hor(ui_PrincipalModoBanda, 10, 0);
    lv_obj_set_flex_flow(ui_PrincipalModoBanda, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_PrincipalModoBanda, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ui_PrincipalModoBanda, PAD_LG, 0);

    ui_PrincipalModoTag = ui_label_create(ui_PrincipalModoBanda, &st_lbl, "MODO");
    lv_obj_add_style(ui_PrincipalModoTag, &st_mode_sub, 0);
    ui_PrincipalModoSimbolo = ui_label_create(ui_PrincipalModoBanda, &st_f16, LV_SYMBOL_CHARGE);
    ui_PrincipalModoValor = ui_label_create(ui_PrincipalModoBanda, &st_f16, "CAMPO X");

    // ---- Datos de la configuracion ----
    ui_PrincipalDatos = ui_box_create(ui_PrincipalScreen);
    lv_obj_set_pos(ui_PrincipalDatos, MARGEN, 72);
    lv_obj_set_size(ui_PrincipalDatos, 304, 102);
    lv_obj_set_flex_flow(ui_PrincipalDatos, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(ui_PrincipalDatos, PAD_MD, 0);
    lv_obj_set_style_pad_column(ui_PrincipalDatos, PAD_LG, 0);

    lv_obj_t* values;
    ui_data_card_create(ui_PrincipalDatos, ANCHO_IZQ, ALTO_DATO, "INTENSIDAD", &values);
    ui_PrincipalIntensidadCampoValor = ui_data_card_add_value(values, "-", "mT");
    ui_data_card_create(ui_PrincipalDatos, ANCHO_DER, ALTO_DATO, "TOL. INTENSIDAD", &values);
    ui_PrincipalTolIntensidadValor = ui_data_card_add_value(values, "-", "%");
    ui_data_card_create(ui_PrincipalDatos, ANCHO_IZQ, ALTO_DATO, "FRECUENCIA", &values);
    ui_PrincipalFrecuenciaValor = ui_data_card_add_value(values, "-", "Hz");
    ui_data_card_create(ui_PrincipalDatos, ANCHO_DER, ALTO_DATO, "TEMP. NORMAL", &values);
    ui_PrincipalTempNormalValor = ui_data_card_add_value(values, "-", "C");
    ui_data_card_create(ui_PrincipalDatos, ANCHO_IZQ, ALTO_DATO, "DURACION", &values);
    ui_PrincipalDuracionHorasValor = ui_data_card_add_value(values, "-", "h");
    ui_PrincipalDuracionMinutosValor = ui_data_card_add_value(values, "-", "m");
    ui_data_card_create(ui_PrincipalDatos, ANCHO_DER, ALTO_DATO, "TEMP. CRITICA", &values);
    ui_PrincipalTempCriticaValor = ui_data_card_add_value(values, "-", "C");

    // ---- Botones ----
    ui_ButtonPrincipalConfigurar = ui_button_create(ui_PrincipalScreen, UI_BTN_SECONDARY, "CONFIGURAR",
                                                    &ui_ButtonPrincipalConfigurarTexto);
    lv_obj_set_pos(ui_ButtonPrincipalConfigurar, MARGEN, 180);
    lv_obj_set_size(ui_ButtonPrincipalConfigurar, 148, ALTO_BOTON);

    ui_ButtonPrincipalIniciar = ui_button_create(ui_PrincipalScreen, UI_BTN_PRIMARY, "INICIAR",
                                                 &ui_ButtonPrincipalIniciarTexto);
    lv_obj_set_pos(ui_ButtonPrincipalIniciar, 164, 180);
    lv_obj_set_size(ui_ButtonPrincipalIniciar, 148, ALTO_BOTON);
}

void ui_PrincipalScreen_screen_destroy(void)
{
    if(ui_PrincipalScreen) lv_obj_delete(ui_PrincipalScreen);

    ui_PrincipalScreen = NULL;
    ui_PrincipalHeader = NULL;
    ui_PrincipalTitle = NULL;
    ui_PrincipalIconoMega = NULL;
    ui_PrincipalIconoSd = NULL;
    ui_PrincipalIconoWifi = NULL;
    ui_PrincipalIconoBroker = NULL;
    ui_PrincipalModoBanda = NULL;
    ui_PrincipalModoTag = NULL;
    ui_PrincipalModoSimbolo = NULL;
    ui_PrincipalModoValor = NULL;
    ui_PrincipalDatos = NULL;
    ui_PrincipalIntensidadCampoValor = NULL;
    ui_PrincipalTolIntensidadValor = NULL;
    ui_PrincipalFrecuenciaValor = NULL;
    ui_PrincipalTempNormalValor = NULL;
    ui_PrincipalDuracionHorasValor = NULL;
    ui_PrincipalDuracionMinutosValor = NULL;
    ui_PrincipalTempCriticaValor = NULL;
    ui_ButtonPrincipalConfigurar = NULL;
    ui_ButtonPrincipalConfigurarTexto = NULL;
    ui_ButtonPrincipalIniciar = NULL;
    ui_ButtonPrincipalIniciarTexto = NULL;
}
