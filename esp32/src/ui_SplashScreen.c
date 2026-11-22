// SPLASH, escrita a mano (antes: export de SquareLine). Arbol:
//
// ui_SplashScreen      (0, 0, 320, 240)   st_screen + st_bg_surface (blanco)
// ├─ ui_Image1         (0, -40, 320, 240) logo de la facultad
// ├─ ui_SplashNombre   (0, 132, 320, -)   FONT_24, COL_PRIMARY_DARK, centrado: "BioSoft"
// └─ ui_SplashEstado   (0, 176, 320, -)   st_lbl centrado: "INICIANDO..."
//
// El handoff pide un logo de 160 x 96 en (80, 40). El asset que hay
// (ui_img_fiuner3201_png) es la pantalla entera, 320 x 240 en blanco con el
// logo centrado (y ~95-145): se usa tal cual, corrido 40 px hacia arriba,
// en lugar de agregar otro asset o escalarlo. Esta en flash, no en heap.
//
// El estado no dice "CONECTANDO CON EL MEGA": en modo banco (requireMega
// false) el splash termina por timeout sin Mega. El timeout sigue en
// SplashController::update().

#include "ui.h"
#include "ui_theme.h"

lv_obj_t * ui_SplashScreen = NULL;
lv_obj_t * ui_Image1 = NULL;
lv_obj_t * ui_SplashNombre = NULL;
lv_obj_t * ui_SplashEstado = NULL;

void ui_SplashScreen_screen_init(void)
{
    ui_SplashScreen = ui_screen_create();
    lv_obj_add_style(ui_SplashScreen, &st_bg_surface, 0);

    ui_Image1 = lv_image_create(ui_SplashScreen);
    lv_obj_remove_style_all(ui_Image1);
    lv_image_set_src(ui_Image1, &ui_img_fiuner3201_png);
    lv_obj_set_pos(ui_Image1, 0, -40);

    ui_SplashNombre = ui_label_create(ui_SplashScreen, &st_val24, "BioSoft");
    lv_obj_add_style(ui_SplashNombre, &st_txt_center, 0);
    // Unico texto en este color: no amerita un estilo compartido mas.
    lv_obj_set_style_text_color(ui_SplashNombre, COL_PRIMARY_DARK, 0);
    lv_obj_set_pos(ui_SplashNombre, 0, 132);
    lv_obj_set_width(ui_SplashNombre, PANTALLA_ANCHO);

    ui_SplashEstado = ui_label_create(ui_SplashScreen, &st_lbl, "INICIANDO...");
    lv_obj_add_style(ui_SplashEstado, &st_txt_center, 0);
    lv_obj_set_pos(ui_SplashEstado, 0, 176);
    lv_obj_set_width(ui_SplashEstado, PANTALLA_ANCHO);
}

void ui_SplashScreen_screen_destroy(void)
{
    if(ui_SplashScreen) lv_obj_delete(ui_SplashScreen);

    ui_SplashScreen = NULL;
    ui_Image1 = NULL;
    ui_SplashNombre = NULL;
    ui_SplashEstado = NULL;
}
