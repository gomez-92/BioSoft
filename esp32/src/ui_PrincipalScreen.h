// PRINCIPAL, escrita a mano (ver ui_PrincipalScreen.c).

#ifndef UI_PRINCIPALSCREEN_H
#define UI_PRINCIPALSCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

extern void ui_PrincipalScreen_screen_init(void);
extern void ui_PrincipalScreen_screen_destroy(void);
extern lv_obj_t * ui_PrincipalScreen;
extern lv_obj_t * ui_PrincipalHeader;
extern lv_obj_t * ui_PrincipalTitle;
extern lv_obj_t * ui_PrincipalIconoMega;
extern lv_obj_t * ui_PrincipalIconoSd;
extern lv_obj_t * ui_PrincipalIconoWifi;
extern lv_obj_t * ui_PrincipalIconoBroker;
extern lv_obj_t * ui_PrincipalModoBanda;
extern lv_obj_t * ui_PrincipalModoTag;
extern lv_obj_t * ui_PrincipalModoSimbolo;
extern lv_obj_t * ui_PrincipalModoValor;
extern lv_obj_t * ui_PrincipalDatos;
extern lv_obj_t * ui_PrincipalIntensidadCampoValor;
extern lv_obj_t * ui_PrincipalTolIntensidadValor;
extern lv_obj_t * ui_PrincipalFrecuenciaValor;
extern lv_obj_t * ui_PrincipalTempNormalValor;
extern lv_obj_t * ui_PrincipalDuracionHorasValor;
extern lv_obj_t * ui_PrincipalDuracionMinutosValor;
extern lv_obj_t * ui_PrincipalTempCriticaValor;
extern lv_obj_t * ui_ButtonPrincipalConfigurar;
extern lv_obj_t * ui_ButtonPrincipalConfigurarTexto;
extern lv_obj_t * ui_ButtonPrincipalIniciar;
extern lv_obj_t * ui_ButtonPrincipalIniciarTexto;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
