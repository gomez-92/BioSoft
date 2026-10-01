#pragma once
#include <lvgl.h>
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_TESTRUNNOTICE = true;

// Aviso de que la configuracion cargada declara corridas de PRUEBA
// (`runType: "test"` en la tarjeta SD, ver ConfigLoader::loadRunType()).
//
// Una corrida de prueba se ve en pantalla exactamente igual que un
// experimento, y el operador no tiene otra forma de saber que lo que esta por
// correr va a quedar marcado como banco en el monitor remoto. Por eso dos
// avisos:
//
//  - Una franja "MODO PRUEBA" permanente.
//  - Un cartel al arrancar que hay que cerrar con ENTENDIDO. No ofrece
//    "rechazar": la configuracion no se puede cambiar desde la pantalla, asi
//    que la unica decision posible es enterarse.
//
// Los dos viven en lv_layer_top() y no en una pantalla: las pantallas se
// crean y se destruyen al navegar (ver ScreenManager), y la capa superior se
// dibuja encima de cualquiera y sobrevive a todas. Ponerlo en cada pantalla
// obligaria a repetirlo en seis exports de SquareLine y a rehacerlo en cada
// re-export.
namespace TestRunNotice {

  namespace {
    inline lv_obj_t* _dialog = nullptr;

    // NO se borra el cartel aca adentro: estamos dentro del callback de su
    // propio boton, y borrar el objeto que LVGL esta despachando es el mismo
    // problema que ScreenManager evita con las pantallas. delete_async lo
    // hace en el proximo ciclo de lv_timer_handler().
    inline void _onAcknowledge(lv_event_t* e) {
      (void)e;
      if (_dialog == nullptr) return;
      lv_obj_delete_async(_dialog);
      _dialog = nullptr;
      DEBUG_PRINTLN(DEBUG_TESTRUNNOTICE, F("[PRUEBA] aviso de arranque confirmado"));
    }

    // Arriba al centro, angosta: tapa lo menos posible de los encabezados de
    // las pantallas. Sin clic, para que nunca se coma un toque destinado a
    // un boton que este debajo.
    inline void _createBanner() {
      lv_obj_t* banner = lv_label_create(lv_layer_top());
      lv_label_set_text(banner, "MODO PRUEBA");
      lv_obj_set_style_text_font(banner, &lv_font_montserrat_10, LV_PART_MAIN);
      lv_obj_set_style_text_color(banner, lv_color_hex(0x1A1D24), LV_PART_MAIN);
      lv_obj_set_style_bg_color(banner, lv_color_hex(0xFFB020), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(banner, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_pad_hor(banner, 8, LV_PART_MAIN);
      lv_obj_set_style_pad_ver(banner, 1, LV_PART_MAIN);
      lv_obj_set_style_radius(banner, 3, LV_PART_MAIN);
      lv_obj_remove_flag(banner, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_align(banner, LV_ALIGN_TOP_MID, 0, 0);
    }

    // Fondo a pantalla completa y clickeable: absorbe los toques, asi no se
    // puede iniciar nada hasta cerrar el cartel.
    inline void _createDialog() {
      _dialog = lv_obj_create(lv_layer_top());
      lv_obj_remove_style_all(_dialog);
      lv_obj_set_size(_dialog, LV_PCT(100), LV_PCT(100));
      lv_obj_set_style_bg_color(_dialog, lv_color_hex(0x000000), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(_dialog, LV_OPA_70, LV_PART_MAIN);
      lv_obj_add_flag(_dialog, LV_OBJ_FLAG_CLICKABLE);

      lv_obj_t* panel = lv_obj_create(_dialog);
      lv_obj_set_size(panel, 280, LV_SIZE_CONTENT);
      lv_obj_center(panel);
      lv_obj_set_style_bg_color(panel, lv_color_hex(0x1A1D24), LV_PART_MAIN);
      lv_obj_set_style_border_color(panel, lv_color_hex(0xFFB020), LV_PART_MAIN);
      lv_obj_set_style_border_width(panel, 2, LV_PART_MAIN);
      lv_obj_set_style_pad_all(panel, 12, LV_PART_MAIN);
      lv_obj_set_style_pad_row(panel, 8, LV_PART_MAIN);
      lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

      lv_obj_t* title = lv_label_create(panel);
      lv_label_set_text(title, "CONFIGURACION DE PRUEBA");
      lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN);
      lv_obj_set_style_text_color(title, lv_color_hex(0xFFB020), LV_PART_MAIN);

      lv_obj_t* body = lv_label_create(panel);
      lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(body, LV_PCT(100));
      lv_label_set_text(body,
        "La tarjeta SD declara corridas de prueba. Todo lo que se corra "
        "queda marcado como PRUEBA en el monitor remoto y no se mezcla con "
        "los experimentos.");
      lv_obj_set_style_text_font(body, &lv_font_montserrat_12, LV_PART_MAIN);
      lv_obj_set_style_text_color(body, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

      lv_obj_t* button = lv_button_create(panel);
      lv_obj_set_style_bg_color(button, lv_color_hex(0xFFB020), LV_PART_MAIN);
      lv_obj_add_event_cb(button, _onAcknowledge, LV_EVENT_CLICKED, nullptr);
      lv_obj_t* buttonLabel = lv_label_create(button);
      lv_label_set_text(buttonLabel, "ENTENDIDO");
      lv_obj_set_style_text_color(buttonLabel, lv_color_hex(0x1A1D24), LV_PART_MAIN);
    }
  }

  // Despues de que LVGL este inicializado (MySystem::begin() ya llamo a
  // DisplayDriver::begin()). Con una configuracion normal no crea nada: ni
  // un objeto oculto, para no gastar heap que TLS necesita.
  inline void begin(bool isTestRun) {
    if (!isTestRun) return;
    _createBanner();
    _createDialog();
    DEBUG_PRINTLN(DEBUG_TESTRUNNOTICE, F("[PRUEBA] configuracion de prueba: franja y aviso de arranque en pantalla"));
  }

};
