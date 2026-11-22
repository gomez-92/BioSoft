// ESPERANDO, escrita a mano (ver ui_EsperandoScreen.c).

#ifndef UI_ESPERANDOSCREEN_H
#define UI_ESPERANDOSCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

extern void ui_EsperandoScreen_screen_init(void);
extern void ui_EsperandoScreen_screen_destroy(void);
extern lv_obj_t * ui_EsperandoScreen;
extern lv_obj_t * ui_EsperandoSpinner;
extern lv_obj_t * ui_EsperandoMensaje;
extern lv_obj_t * ui_EsperandoSubtitulo;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
