// EN CURSO, escrita a mano (ver ui_EnCursoScreen.c).

#ifndef UI_ENCURSOSCREEN_H
#define UI_ENCURSOSCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

extern void ui_EnCursoScreen_screen_init(void);
extern void ui_EnCursoScreen_screen_destroy(void);
extern lv_obj_t * ui_EnCursoScreen;
extern lv_obj_t * ui_EnCursoHeader;
extern lv_obj_t * ui_EnCursoTitle;
extern lv_obj_t * ui_EnCursoHeaderDivisor;
extern lv_obj_t * ui_EnCursoHeaderSalud;
extern lv_obj_t * ui_EnCursoIconoMega;
extern lv_obj_t * ui_EnCursoIconoSd;
extern lv_obj_t * ui_EnCursoIconoWifi;
extern lv_obj_t * ui_EnCursoIconoBroker;
extern lv_obj_t * ui_EnCursoTabMediciones;
extern lv_obj_t * ui_EnCursoTabObjetivos;
extern lv_obj_t * ui_EnCursoModoChip;
extern lv_obj_t * ui_EnCursoModoTexto;
extern lv_obj_t * ui_EnCursoPaginaMediciones;
extern lv_obj_t * ui_EnCursoPaginaObjetivos;
extern lv_obj_t * ui_EnCursoMedicionesIntensidadValor;
extern lv_obj_t * ui_EnCursoMedicionesIntensidadAlertas1;
extern lv_obj_t * ui_EnCursoMedicionesIntensidadAlertas2;
extern lv_obj_t * ui_EnCursoMedicionesIntensidadAlertas3;
extern lv_obj_t * ui_EnCursoMedicionesIntensidadAlertas4;
extern lv_obj_t * ui_EnCursoMedicionesTempValor;
extern lv_obj_t * ui_EnCursoMedicionesTempAlertas1;
extern lv_obj_t * ui_EnCursoMedicionesTempAlertas2;
extern lv_obj_t * ui_EnCursoMedicionesTempAlertas3;
extern lv_obj_t * ui_EnCursoMedicionesTempAlertas4;
extern lv_obj_t * ui_EnCursoMedicionesBobinas;
extern lv_obj_t * ui_EnCursoMedicionesSinBobinas;
extern lv_obj_t * ui_EnCursoMedicionesBobina1;
extern lv_obj_t * ui_EnCursoMedicionesBobina1Nombre;
extern lv_obj_t * ui_EnCursoMedicionesBobina1ControlBar;
extern lv_obj_t * ui_EnCursoMedicionesBobina1ControlValor;
extern lv_obj_t * ui_EnCursoMedicionesBobina1Corriente;
extern lv_obj_t * ui_EnCursoMedicionesBobina2;
extern lv_obj_t * ui_EnCursoMedicionesBobina2Nombre;
extern lv_obj_t * ui_EnCursoMedicionesBobina2ControlBar;
extern lv_obj_t * ui_EnCursoMedicionesBobina2ControlValor;
extern lv_obj_t * ui_EnCursoMedicionesBobina2Corriente;
extern lv_obj_t * ui_EnCursoMedicionesBobina3;
extern lv_obj_t * ui_EnCursoMedicionesBobina3Nombre;
extern lv_obj_t * ui_EnCursoMedicionesBobina3ControlBar;
extern lv_obj_t * ui_EnCursoMedicionesBobina3ControlValor;
extern lv_obj_t * ui_EnCursoMedicionesBobina3Corriente;
extern lv_obj_t * ui_EnCursoMedicionesBobina4;
extern lv_obj_t * ui_EnCursoMedicionesBobina4Nombre;
extern lv_obj_t * ui_EnCursoMedicionesBobina4ControlBar;
extern lv_obj_t * ui_EnCursoMedicionesBobina4ControlValor;
extern lv_obj_t * ui_EnCursoMedicionesBobina4Corriente;
extern lv_obj_t * ui_EnCursoObjetivosIntensidadValor;
extern lv_obj_t * ui_EnCursoObjetivosTolIntensidadValor;
extern lv_obj_t * ui_EnCursoObjetivosFrecuenciaValor;
extern lv_obj_t * ui_EnCursoObjetivosTempNormalValor;
extern lv_obj_t * ui_EnCursoObjetivosDuracionHorasValor;
extern lv_obj_t * ui_EnCursoObjetivosDuracionMinutosValor;
extern lv_obj_t * ui_EnCursoObjetivosTempCriticaValor;
extern lv_obj_t * ui_EnCursoProgresoValor;
extern lv_obj_t * ui_EnCursoProgresoBarra;
extern lv_obj_t * ui_EnCursoTranscurridoValor;
extern lv_obj_t * ui_ButtonDetener;
extern lv_obj_t * ui_ButtonDetenerTexto;

// MEDICIONES (false) u OBJETIVOS (true): marca la pestania y muestra su
// pagina. Las dos paginas existen siempre; solo se ocultan.
void ui_encurso_show_tab(bool targets);

// Cuantas filas de bobina se ven (0..4) y con que anchos: hasta 2, una por
// renglon; con 3 o 4, dos por renglon. Con 0, "SIN BOBINAS CONECTADAS".
void ui_encurso_set_coil_layout(uint8_t count);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
