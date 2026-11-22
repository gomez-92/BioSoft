// CONFIGURACION, escrita a mano (ver ui_ConfiguracionesScreen.c).

#ifndef UI_CONFIGURACIONESSCREEN_H
#define UI_CONFIGURACIONESSCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

extern void ui_ConfiguracionesScreen_screen_init(void);
extern void ui_ConfiguracionesScreen_screen_destroy(void);
extern lv_obj_t * ui_ConfiguracionesScreen;
extern lv_obj_t * ui_ConfiguracionesHeader;
extern lv_obj_t * ui_ConfiguracionesTitle;
extern lv_obj_t * ui_ConfiguracionSinGuardar;
extern lv_obj_t * ui_ConfiguracionTabExposicion;
extern lv_obj_t * ui_ConfiguracionTabExposicionPunto;
extern lv_obj_t * ui_ConfiguracionTabLimites;
extern lv_obj_t * ui_ConfiguracionTabLimitesPunto;
extern lv_obj_t * ui_ConfiguracionPaginaExposicion;
extern lv_obj_t * ui_ConfiguracionPaginaLimites;
extern lv_obj_t * ui_ConfiguracionModoOpciones;
extern lv_obj_t * ui_ConfiguracionIntensidadCampoOpciones;
extern lv_obj_t * ui_ConfiguracionFrecuenciaOpciones;
extern lv_obj_t * ui_ConfiguracionDuracionOpciones;
extern lv_obj_t * ui_ConfiguracionTolIntensidadOpciones;
extern lv_obj_t * ui_ConfiguracionTempNormalOpciones;
extern lv_obj_t * ui_ConfiguracionTempCriticaOpciones;
extern lv_obj_t * ui_ButtonConfiguracionVolver;
extern lv_obj_t * ui_ButtonConfiguracionVolverTexto;
extern lv_obj_t * ui_ButtonConfiguracionGuardar;
extern lv_obj_t * ui_ButtonConfiguracionGuardarTexto;

// Muestra la pagina EXPOSICION (false) o LIMITES (true) y marca su pestania.
void ui_config_show_tab(bool limits);

// Pinta en el campo la opcion elegida del dropdown, o "--" si no hay
// (rango de temperatura sin opcion compatible). En MODO actualiza el
// cuadrito. Llamarla despues de cada set_selected / set_options.
void ui_config_field_refresh(lv_obj_t* field, bool hasSelection);

// Borde ambar y etiqueta en COL_WARN_TEXT si el campo difiere de lo guardado.
void ui_config_field_set_dirty(lv_obj_t* field, bool dirty);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
