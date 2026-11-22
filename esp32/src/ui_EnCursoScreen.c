// EN CURSO, escrita a mano (antes: export de SquareLine). Arbol del handoff:
//
// ui_EnCursoScreen                    st_screen
// ├─ ui_EnCursoHeader                 (0, 0, 320, 28) st_header + st_hdr_ok | _warn | _crit
// │  ├─ titulo                        FONT_12 "EN CURSO" (en 16 no entra con ADVERTENCIA)
// │  ├─ ui_EnCursoHeaderDivisor       1 x 16, color del texto del encabezado
// │  ├─ ui_EnCursoHeaderSalud         FONT_16, flex_grow 1: NORMAL | ADVERTENCIA | CRITICO
// │  └─ 4 iconos                      ui_EnCursoIcono{Mega,Sd,Wifi,Broker}
// ├─ ui_EnCursoTabMediciones          (8, 34, 96, 40)   activa al entrar
// ├─ ui_EnCursoTabObjetivos           (112, 34, 96, 40)
// ├─ ui_EnCursoModoChip               (216, 34, 96, 40) st_mode_x | st_mode_null
// │  └─ ui_EnCursoModoTexto           FONT_12: LV_SYMBOL_CHARGE " CAMPO X" | "CAMPO NULO"
// ├─ ui_EnCursoPaginaMediciones       (8, 80, 304, 94)
// │  ├─ CAMPO MEDIDO (148 x 48)       ui_EnCursoMedicionesIntensidadAlertas1..4, ui_EnCursoMedicionesIntensidadValor "mT"
// │  ├─ TEMPERATURA  (148 x 48)       ui_EnCursoMedicionesTempAlertas1..4, ui_EnCursoMedicionesTempValor "C"
// │  └─ ui_EnCursoMedicionesBobinas   (0, 52, 304, 42) flex row wrap
// │     ├─ ui_EnCursoMedicionesBobina1..4   fila: Nombre, ControlBar, ControlValor, Corriente
// │     └─ ui_EnCursoMedicionesSinBobinas   "SIN BOBINAS CONECTADAS" (solo con 0)
// ├─ ui_EnCursoPaginaObjetivos        (8, 80, 304, 92) 6 tarjetas de 28 de alto, oculta al entrar
// ├─ progreso                         (8, 180, 184, 48)
// │  ├─ ui_EnCursoProgresoValor       FONT_16 a la derecha: "42 %"
// │  ├─ ui_EnCursoProgresoBarra       (0, 22, 184, 8)
// │  └─ ui_EnCursoTranscurridoValor   FONT_12 a la derecha: "0 h 38 m"
// └─ ui_ButtonDetener                 (200, 180, 112, 40) st_btn_stop
//
// Las dos pestanias se crean al entrar y solo se muestran u ocultan
// (decision B del plan): nada se crea ni se destruye durante una corrida.
// Las cuatro filas de bobina existen siempre; ui_encurso_set_coil_layout()
// ajusta anchos y visibilidad segun cuantas haya.
//
// Nombres: se conservan los que ya existian (Mediciones*, Objetivos*Valor,
// Bobina*, ui_ButtonDetener). ui_Label1 (el % de progreso, sin nombre en
// SquareLine) pasa a ui_EnCursoProgresoValor; los chips de salud, los
// iconos dobles y las horas/minutos sueltos de TRANSCURRIDO no existen en
// el diseno nuevo.

#include "ui.h"
#include "ui_theme.h"
#include "ui_components.h"

lv_obj_t * ui_EnCursoScreen = NULL;
lv_obj_t * ui_EnCursoHeader = NULL;
lv_obj_t * ui_EnCursoTitle = NULL;
lv_obj_t * ui_EnCursoHeaderDivisor = NULL;
lv_obj_t * ui_EnCursoHeaderSalud = NULL;
lv_obj_t * ui_EnCursoIconoMega = NULL;
lv_obj_t * ui_EnCursoIconoSd = NULL;
lv_obj_t * ui_EnCursoIconoWifi = NULL;
lv_obj_t * ui_EnCursoIconoBroker = NULL;
lv_obj_t * ui_EnCursoTabMediciones = NULL;
lv_obj_t * ui_EnCursoTabObjetivos = NULL;
lv_obj_t * ui_EnCursoModoChip = NULL;
lv_obj_t * ui_EnCursoModoTexto = NULL;
lv_obj_t * ui_EnCursoPaginaMediciones = NULL;
lv_obj_t * ui_EnCursoPaginaObjetivos = NULL;

lv_obj_t * ui_EnCursoMedicionesIntensidadValor = NULL;
lv_obj_t * ui_EnCursoMedicionesIntensidadAlertas1 = NULL;
lv_obj_t * ui_EnCursoMedicionesIntensidadAlertas2 = NULL;
lv_obj_t * ui_EnCursoMedicionesIntensidadAlertas3 = NULL;
lv_obj_t * ui_EnCursoMedicionesIntensidadAlertas4 = NULL;
lv_obj_t * ui_EnCursoMedicionesTempValor = NULL;
lv_obj_t * ui_EnCursoMedicionesTempAlertas1 = NULL;
lv_obj_t * ui_EnCursoMedicionesTempAlertas2 = NULL;
lv_obj_t * ui_EnCursoMedicionesTempAlertas3 = NULL;
lv_obj_t * ui_EnCursoMedicionesTempAlertas4 = NULL;

lv_obj_t * ui_EnCursoMedicionesBobinas = NULL;
lv_obj_t * ui_EnCursoMedicionesSinBobinas = NULL;
lv_obj_t * ui_EnCursoMedicionesBobina1 = NULL;
lv_obj_t * ui_EnCursoMedicionesBobina1Nombre = NULL;
lv_obj_t * ui_EnCursoMedicionesBobina1ControlBar = NULL;
lv_obj_t * ui_EnCursoMedicionesBobina1ControlValor = NULL;
lv_obj_t * ui_EnCursoMedicionesBobina1Corriente = NULL;
lv_obj_t * ui_EnCursoMedicionesBobina2 = NULL;
lv_obj_t * ui_EnCursoMedicionesBobina2Nombre = NULL;
lv_obj_t * ui_EnCursoMedicionesBobina2ControlBar = NULL;
lv_obj_t * ui_EnCursoMedicionesBobina2ControlValor = NULL;
lv_obj_t * ui_EnCursoMedicionesBobina2Corriente = NULL;
lv_obj_t * ui_EnCursoMedicionesBobina3 = NULL;
lv_obj_t * ui_EnCursoMedicionesBobina3Nombre = NULL;
lv_obj_t * ui_EnCursoMedicionesBobina3ControlBar = NULL;
lv_obj_t * ui_EnCursoMedicionesBobina3ControlValor = NULL;
lv_obj_t * ui_EnCursoMedicionesBobina3Corriente = NULL;
lv_obj_t * ui_EnCursoMedicionesBobina4 = NULL;
lv_obj_t * ui_EnCursoMedicionesBobina4Nombre = NULL;
lv_obj_t * ui_EnCursoMedicionesBobina4ControlBar = NULL;
lv_obj_t * ui_EnCursoMedicionesBobina4ControlValor = NULL;
lv_obj_t * ui_EnCursoMedicionesBobina4Corriente = NULL;

lv_obj_t * ui_EnCursoObjetivosIntensidadValor = NULL;
lv_obj_t * ui_EnCursoObjetivosTolIntensidadValor = NULL;
lv_obj_t * ui_EnCursoObjetivosFrecuenciaValor = NULL;
lv_obj_t * ui_EnCursoObjetivosTempNormalValor = NULL;
lv_obj_t * ui_EnCursoObjetivosDuracionHorasValor = NULL;
lv_obj_t * ui_EnCursoObjetivosDuracionMinutosValor = NULL;
lv_obj_t * ui_EnCursoObjetivosTempCriticaValor = NULL;

lv_obj_t * ui_EnCursoProgresoValor = NULL;
lv_obj_t * ui_EnCursoProgresoBarra = NULL;
lv_obj_t * ui_EnCursoTranscurridoValor = NULL;
lv_obj_t * ui_ButtonDetener = NULL;
lv_obj_t * ui_ButtonDetenerTexto = NULL;

static lv_obj_t* tab_create(int32_t x, const char* text)
{
    lv_obj_t* tab = lv_button_create(ui_EnCursoScreen);
    lv_obj_remove_style_all(tab);
    lv_obj_add_style(tab, &st_tab, 0);
    lv_obj_add_style(tab, &st_tab_chk, LV_STATE_CHECKED);
    lv_obj_remove_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(tab, x, 34);
    lv_obj_set_width(tab, 96);
    lv_obj_t* label = ui_label_create(tab, NULL, text);
    lv_obj_center(label);
    return tab;
}

// Tarjeta de medicion de 148 x 48: etiqueta arriba a la izquierda, 4 puntos
// de alerta arriba a la derecha, valor grande + unidad abajo.
static lv_obj_t* measure_create(lv_obj_t* page, int32_t x, const char* text, const char* unit,
                                lv_obj_t** dots)
{
    lv_obj_t* card = ui_card_create(page);
    lv_obj_set_pos(card, x, 0);
    lv_obj_set_size(card, 148, 48);
    lv_obj_set_style_pad_ver(card, 3, 0);

    ui_label_create(card, &st_lbl, text);

    // Alineado al final: con 2 eventos se ven los 2 de la derecha (los
    // ocultos no ocupan lugar en el flex).
    lv_obj_t* dotBox = ui_box_create(card);
    lv_obj_set_size(dotBox, 44, 8);
    lv_obj_align(dotBox, LV_ALIGN_TOP_RIGHT, 0, 2);
    lv_obj_set_flex_flow(dotBox, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dotBox, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dotBox, PAD_SM, 0);
    for (int i = 0; i < 4; i++) {
        dots[i] = ui_box_create(dotBox);
        lv_obj_add_style(dots[i], &st_dot_warn, 0);
        lv_obj_add_flag(dots[i], LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t* row = ui_box_create(card);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(row, 0, 13);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, PAD_SM, 0);
    lv_obj_t* value = ui_label_create(row, &st_val24, "0.00");
    ui_label_create(row, &st_lbl12, unit);
    return value;
}

// Fila de bobina: nombre, barra de duty, duty y corriente. Los anchos los
// ajusta ui_encurso_set_coil_layout().
static lv_obj_t* coil_row_create(lv_obj_t* parent, const char* name, lv_obj_t** nameLabel, lv_obj_t** bar,
                                 lv_obj_t** duty, lv_obj_t** current)
{
    lv_obj_t* row = ui_box_create(parent);
    lv_obj_set_size(row, 292, 16);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, PAD_SM, 0);

    *nameLabel = ui_label_create(row, &st_txt12, name);
    lv_obj_set_width(*nameLabel, 18);
    lv_label_set_long_mode(*nameLabel, LV_LABEL_LONG_CLIP);

    *bar = lv_bar_create(row);
    lv_obj_remove_style_all(*bar);
    lv_obj_add_style(*bar, &st_bar_main, LV_PART_MAIN);
    lv_obj_add_style(*bar, &st_bar_ind, LV_PART_INDICATOR);
    lv_obj_set_size(*bar, 174, 8);
    lv_bar_set_range(*bar, 0, 100);

    *duty = ui_label_create(row, &st_txt12, "0.0 %");
    lv_obj_add_style(*duty, &st_txt_right, 0);
    lv_obj_set_width(*duty, 44);

    *current = ui_label_create(row, &st_lbl12, "0.00 A");
    lv_obj_add_style(*current, &st_txt_right, 0);
    lv_obj_set_width(*current, 44);
    return row;
}

void ui_EnCursoScreen_screen_init(void)
{
    ui_EnCursoScreen = ui_screen_create();

    // ---- Encabezado con salud ----
    ui_EnCursoHeader = ui_header_create(ui_EnCursoScreen, "EN CURSO", &ui_EnCursoTitle);
    lv_obj_add_style(ui_EnCursoHeader, &st_hdr_ok, 0);
    lv_obj_set_flex_grow(ui_EnCursoTitle, 0);
    lv_obj_add_style(ui_EnCursoTitle, &st_f12, 0);

    ui_EnCursoHeaderDivisor = ui_box_create(ui_EnCursoHeader);
    lv_obj_set_size(ui_EnCursoHeaderDivisor, 1, 16);
    lv_obj_set_style_bg_opa(ui_EnCursoHeaderDivisor, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ui_EnCursoHeaderDivisor, lv_color_white(), 0);

    ui_EnCursoHeaderSalud = ui_label_create(ui_EnCursoHeader, NULL, "NORMAL");
    lv_obj_set_flex_grow(ui_EnCursoHeaderSalud, 1);

    ui_status_icons_t icons;
    ui_header_add_status_icons(ui_EnCursoHeader, &icons);
    ui_EnCursoIconoMega = icons.mega;
    ui_EnCursoIconoSd = icons.sd;
    ui_EnCursoIconoWifi = icons.wifi;
    ui_EnCursoIconoBroker = icons.broker;

    // ---- Pestanias y chip de modo ----
    ui_EnCursoTabMediciones = tab_create(MARGEN, "MEDICIONES");
    ui_EnCursoTabObjetivos = tab_create(112, "OBJETIVOS");

    ui_EnCursoModoChip = ui_box_create(ui_EnCursoScreen);
    lv_obj_add_style(ui_EnCursoModoChip, &st_mode_x, 0);
    lv_obj_set_style_radius(ui_EnCursoModoChip, RADIO_MD, 0);
    lv_obj_set_pos(ui_EnCursoModoChip, 216, 34);
    lv_obj_set_size(ui_EnCursoModoChip, 96, ALTO_BOTON);
    ui_EnCursoModoTexto = ui_label_create(ui_EnCursoModoChip, &st_f12, LV_SYMBOL_CHARGE " CAMPO X");
    lv_obj_center(ui_EnCursoModoTexto);

    // ---- Pagina MEDICIONES ----
    ui_EnCursoPaginaMediciones = ui_box_create(ui_EnCursoScreen);
    lv_obj_set_pos(ui_EnCursoPaginaMediciones, MARGEN, 80);
    lv_obj_set_size(ui_EnCursoPaginaMediciones, 304, 94);

    lv_obj_t* dots[4];
    ui_EnCursoMedicionesIntensidadValor = measure_create(ui_EnCursoPaginaMediciones, 0, "CAMPO MEDIDO", "mT", dots);
    ui_EnCursoMedicionesIntensidadAlertas1 = dots[0];
    ui_EnCursoMedicionesIntensidadAlertas2 = dots[1];
    ui_EnCursoMedicionesIntensidadAlertas3 = dots[2];
    ui_EnCursoMedicionesIntensidadAlertas4 = dots[3];
    ui_EnCursoMedicionesTempValor = measure_create(ui_EnCursoPaginaMediciones, 156, "TEMPERATURA", "C", dots);
    lv_label_set_text(ui_EnCursoMedicionesTempValor, "0.0");
    ui_EnCursoMedicionesTempAlertas1 = dots[0];
    ui_EnCursoMedicionesTempAlertas2 = dots[1];
    ui_EnCursoMedicionesTempAlertas3 = dots[2];
    ui_EnCursoMedicionesTempAlertas4 = dots[3];

    ui_EnCursoMedicionesBobinas = ui_card_create(ui_EnCursoPaginaMediciones);
    lv_obj_set_pos(ui_EnCursoMedicionesBobinas, 0, 52);
    lv_obj_set_size(ui_EnCursoMedicionesBobinas, 304, 42);
    lv_obj_set_style_pad_ver(ui_EnCursoMedicionesBobinas, 3, 0);
    lv_obj_set_style_pad_hor(ui_EnCursoMedicionesBobinas, 5, 0);
    lv_obj_set_flex_flow(ui_EnCursoMedicionesBobinas, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(ui_EnCursoMedicionesBobinas, 2, 0);
    lv_obj_set_style_pad_column(ui_EnCursoMedicionesBobinas, PAD_LG, 0);

    ui_EnCursoMedicionesBobina1 = coil_row_create(ui_EnCursoMedicionesBobinas, "B1",
        &ui_EnCursoMedicionesBobina1Nombre, &ui_EnCursoMedicionesBobina1ControlBar,
        &ui_EnCursoMedicionesBobina1ControlValor, &ui_EnCursoMedicionesBobina1Corriente);
    ui_EnCursoMedicionesBobina2 = coil_row_create(ui_EnCursoMedicionesBobinas, "B2",
        &ui_EnCursoMedicionesBobina2Nombre, &ui_EnCursoMedicionesBobina2ControlBar,
        &ui_EnCursoMedicionesBobina2ControlValor, &ui_EnCursoMedicionesBobina2Corriente);
    ui_EnCursoMedicionesBobina3 = coil_row_create(ui_EnCursoMedicionesBobinas, "B3",
        &ui_EnCursoMedicionesBobina3Nombre, &ui_EnCursoMedicionesBobina3ControlBar,
        &ui_EnCursoMedicionesBobina3ControlValor, &ui_EnCursoMedicionesBobina3Corriente);
    ui_EnCursoMedicionesBobina4 = coil_row_create(ui_EnCursoMedicionesBobinas, "B4",
        &ui_EnCursoMedicionesBobina4Nombre, &ui_EnCursoMedicionesBobina4ControlBar,
        &ui_EnCursoMedicionesBobina4ControlValor, &ui_EnCursoMedicionesBobina4Corriente);

    ui_EnCursoMedicionesSinBobinas = ui_label_create(ui_EnCursoMedicionesBobinas, &st_lbl12, "SIN BOBINAS CONECTADAS");
    lv_obj_add_style(ui_EnCursoMedicionesSinBobinas, &st_txt_center, 0);
    lv_obj_set_width(ui_EnCursoMedicionesSinBobinas, lv_pct(100));

    // ---- Pagina OBJETIVOS (mismos textos que PRINCIPAL) ----
    ui_EnCursoPaginaObjetivos = ui_box_create(ui_EnCursoScreen);
    lv_obj_set_pos(ui_EnCursoPaginaObjetivos, MARGEN, 80);
    lv_obj_set_size(ui_EnCursoPaginaObjetivos, 304, 92);
    lv_obj_set_flex_flow(ui_EnCursoPaginaObjetivos, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(ui_EnCursoPaginaObjetivos, PAD_SM, 0);
    lv_obj_set_style_pad_column(ui_EnCursoPaginaObjetivos, PAD_LG, 0);

    lv_obj_t* values;
    ui_data_card_create(ui_EnCursoPaginaObjetivos, 136, 28, "INTENSIDAD", &values);
    ui_EnCursoObjetivosIntensidadValor = ui_data_card_add_value(values, "-", "mT");
    ui_data_card_create(ui_EnCursoPaginaObjetivos, 160, 28, "TOL. INTENSIDAD", &values);
    ui_EnCursoObjetivosTolIntensidadValor = ui_data_card_add_value(values, "-", "%");
    ui_data_card_create(ui_EnCursoPaginaObjetivos, 136, 28, "FRECUENCIA", &values);
    ui_EnCursoObjetivosFrecuenciaValor = ui_data_card_add_value(values, "-", "Hz");
    ui_data_card_create(ui_EnCursoPaginaObjetivos, 160, 28, "TEMP. NORMAL", &values);
    ui_EnCursoObjetivosTempNormalValor = ui_data_card_add_value(values, "-", "C");
    ui_data_card_create(ui_EnCursoPaginaObjetivos, 136, 28, "DURACION", &values);
    ui_EnCursoObjetivosDuracionHorasValor = ui_data_card_add_value(values, "-", "h");
    ui_EnCursoObjetivosDuracionMinutosValor = ui_data_card_add_value(values, "-", "m");
    ui_data_card_create(ui_EnCursoPaginaObjetivos, 160, 28, "TEMP. CRITICA", &values);
    ui_EnCursoObjetivosTempCriticaValor = ui_data_card_add_value(values, "-", "C");

    // ---- Progreso ----
    lv_obj_t* progress = ui_box_create(ui_EnCursoScreen);
    lv_obj_set_pos(progress, MARGEN, 180);
    lv_obj_set_size(progress, 184, 48);

    lv_obj_t* label = ui_label_create(progress, &st_lbl, "PROGRESO");
    lv_obj_set_pos(label, 0, 4);
    ui_EnCursoProgresoValor = ui_label_create(progress, &st_val16, "0 %");
    lv_obj_add_style(ui_EnCursoProgresoValor, &st_txt_right, 0);
    lv_obj_set_pos(ui_EnCursoProgresoValor, 84, 0);
    lv_obj_set_width(ui_EnCursoProgresoValor, 100);

    ui_EnCursoProgresoBarra = lv_bar_create(progress);
    lv_obj_remove_style_all(ui_EnCursoProgresoBarra);
    lv_obj_add_style(ui_EnCursoProgresoBarra, &st_bar_main, LV_PART_MAIN);
    lv_obj_add_style(ui_EnCursoProgresoBarra, &st_bar_ind, LV_PART_INDICATOR);
    lv_obj_set_pos(ui_EnCursoProgresoBarra, 0, 22);
    lv_obj_set_size(ui_EnCursoProgresoBarra, 184, 8);
    lv_bar_set_range(ui_EnCursoProgresoBarra, 0, 100);

    label = ui_label_create(progress, &st_lbl, "TRANSCURRIDO");
    lv_obj_set_pos(label, 0, 34);
    ui_EnCursoTranscurridoValor = ui_label_create(progress, &st_txt12, "0 h 00 m");
    lv_obj_add_style(ui_EnCursoTranscurridoValor, &st_txt_right, 0);
    lv_obj_set_pos(ui_EnCursoTranscurridoValor, 84, 32);
    lv_obj_set_width(ui_EnCursoTranscurridoValor, 100);

    // ---- DETENER ----
    ui_ButtonDetener = ui_button_create(ui_EnCursoScreen, UI_BTN_STOP, LV_SYMBOL_STOP " DETENER",
                                       &ui_ButtonDetenerTexto);
    lv_obj_set_pos(ui_ButtonDetener, 200, 180);
    lv_obj_set_size(ui_ButtonDetener, 112, ALTO_BOTON);

    ui_encurso_show_tab(false);
    ui_encurso_set_coil_layout(0);
}

void ui_EnCursoScreen_screen_destroy(void)
{
    if(ui_EnCursoScreen) lv_obj_delete(ui_EnCursoScreen);

    ui_EnCursoScreen = NULL;
    ui_EnCursoHeader = NULL;
    ui_EnCursoTitle = NULL;
    ui_EnCursoHeaderDivisor = NULL;
    ui_EnCursoHeaderSalud = NULL;
    ui_EnCursoIconoMega = NULL;
    ui_EnCursoIconoSd = NULL;
    ui_EnCursoIconoWifi = NULL;
    ui_EnCursoIconoBroker = NULL;
    ui_EnCursoTabMediciones = NULL;
    ui_EnCursoTabObjetivos = NULL;
    ui_EnCursoModoChip = NULL;
    ui_EnCursoModoTexto = NULL;
    ui_EnCursoPaginaMediciones = NULL;
    ui_EnCursoPaginaObjetivos = NULL;
    ui_EnCursoMedicionesIntensidadValor = NULL;
    ui_EnCursoMedicionesIntensidadAlertas1 = NULL;
    ui_EnCursoMedicionesIntensidadAlertas2 = NULL;
    ui_EnCursoMedicionesIntensidadAlertas3 = NULL;
    ui_EnCursoMedicionesIntensidadAlertas4 = NULL;
    ui_EnCursoMedicionesTempValor = NULL;
    ui_EnCursoMedicionesTempAlertas1 = NULL;
    ui_EnCursoMedicionesTempAlertas2 = NULL;
    ui_EnCursoMedicionesTempAlertas3 = NULL;
    ui_EnCursoMedicionesTempAlertas4 = NULL;
    ui_EnCursoMedicionesBobinas = NULL;
    ui_EnCursoMedicionesSinBobinas = NULL;
    ui_EnCursoMedicionesBobina1 = NULL;
    ui_EnCursoMedicionesBobina1Nombre = NULL;
    ui_EnCursoMedicionesBobina1ControlBar = NULL;
    ui_EnCursoMedicionesBobina1ControlValor = NULL;
    ui_EnCursoMedicionesBobina1Corriente = NULL;
    ui_EnCursoMedicionesBobina2 = NULL;
    ui_EnCursoMedicionesBobina2Nombre = NULL;
    ui_EnCursoMedicionesBobina2ControlBar = NULL;
    ui_EnCursoMedicionesBobina2ControlValor = NULL;
    ui_EnCursoMedicionesBobina2Corriente = NULL;
    ui_EnCursoMedicionesBobina3 = NULL;
    ui_EnCursoMedicionesBobina3Nombre = NULL;
    ui_EnCursoMedicionesBobina3ControlBar = NULL;
    ui_EnCursoMedicionesBobina3ControlValor = NULL;
    ui_EnCursoMedicionesBobina3Corriente = NULL;
    ui_EnCursoMedicionesBobina4 = NULL;
    ui_EnCursoMedicionesBobina4Nombre = NULL;
    ui_EnCursoMedicionesBobina4ControlBar = NULL;
    ui_EnCursoMedicionesBobina4ControlValor = NULL;
    ui_EnCursoMedicionesBobina4Corriente = NULL;
    ui_EnCursoObjetivosIntensidadValor = NULL;
    ui_EnCursoObjetivosTolIntensidadValor = NULL;
    ui_EnCursoObjetivosFrecuenciaValor = NULL;
    ui_EnCursoObjetivosTempNormalValor = NULL;
    ui_EnCursoObjetivosDuracionHorasValor = NULL;
    ui_EnCursoObjetivosDuracionMinutosValor = NULL;
    ui_EnCursoObjetivosTempCriticaValor = NULL;
    ui_EnCursoProgresoValor = NULL;
    ui_EnCursoProgresoBarra = NULL;
    ui_EnCursoTranscurridoValor = NULL;
    ui_ButtonDetener = NULL;
    ui_ButtonDetenerTexto = NULL;
}

void ui_encurso_show_tab(bool targets)
{
    if (ui_EnCursoScreen == NULL) return;
    if (targets) {
        lv_obj_remove_state(ui_EnCursoTabMediciones, LV_STATE_CHECKED);
        lv_obj_add_state(ui_EnCursoTabObjetivos, LV_STATE_CHECKED);
        lv_obj_add_flag(ui_EnCursoPaginaMediciones, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui_EnCursoPaginaObjetivos, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_state(ui_EnCursoTabMediciones, LV_STATE_CHECKED);
        lv_obj_remove_state(ui_EnCursoTabObjetivos, LV_STATE_CHECKED);
        lv_obj_remove_flag(ui_EnCursoPaginaMediciones, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_EnCursoPaginaObjetivos, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_encurso_set_coil_layout(uint8_t count)
{
    if (ui_EnCursoScreen == NULL) return;
    lv_obj_t* rows[4] = { ui_EnCursoMedicionesBobina1, ui_EnCursoMedicionesBobina2,
                          ui_EnCursoMedicionesBobina3, ui_EnCursoMedicionesBobina4 };
    lv_obj_t* bars[4] = { ui_EnCursoMedicionesBobina1ControlBar, ui_EnCursoMedicionesBobina2ControlBar,
                          ui_EnCursoMedicionesBobina3ControlBar, ui_EnCursoMedicionesBobina4ControlBar };
    lv_obj_t* duties[4] = { ui_EnCursoMedicionesBobina1ControlValor, ui_EnCursoMedicionesBobina2ControlValor,
                            ui_EnCursoMedicionesBobina3ControlValor, ui_EnCursoMedicionesBobina4ControlValor };
    lv_obj_t* currents[4] = { ui_EnCursoMedicionesBobina1Corriente, ui_EnCursoMedicionesBobina2Corriente,
                              ui_EnCursoMedicionesBobina3Corriente, ui_EnCursoMedicionesBobina4Corriente };

    // Hasta 2 bobinas: una fila por renglon (292 / 174 / 44). Con 3 o 4: dos
    // por renglon (142 / 32 / 40).
    bool wide = count <= 2;
    for (uint8_t i = 0; i < 4; i++) {
        lv_obj_set_width(rows[i], wide ? 292 : 142);
        lv_obj_set_width(bars[i], wide ? 174 : 32);
        lv_obj_set_width(duties[i], wide ? 44 : 40);
        lv_obj_set_width(currents[i], wide ? 44 : 40);
        if (i < count) lv_obj_remove_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_add_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (count == 0) lv_obj_remove_flag(ui_EnCursoMedicionesSinBobinas, LV_OBJ_FLAG_HIDDEN);
    else            lv_obj_add_flag(ui_EnCursoMedicionesSinBobinas, LV_OBJ_FLAG_HIDDEN);
}
