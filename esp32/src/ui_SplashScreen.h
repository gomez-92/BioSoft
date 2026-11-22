// SPLASH, escrita a mano (ver ui_SplashScreen.c).

#ifndef UI_SPLASHSCREEN_H
#define UI_SPLASHSCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

extern void ui_SplashScreen_screen_init(void);
extern void ui_SplashScreen_screen_destroy(void);
extern lv_obj_t * ui_SplashScreen;
extern lv_obj_t * ui_Image1;
extern lv_obj_t * ui_SplashNombre;
extern lv_obj_t * ui_SplashEstado;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
