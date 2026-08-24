#pragma once

// ================= LIBRERÍAS =================
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include "ui.h"
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_DISPLAY = true;


// ========================================================
// ================= CLASE DISPLAY DRIVER =================
// ========================================================
class DisplayDriver {
public:
    void begin();   // Inicialización completa del sistema gráfico
    void update();  // Actualización periódica de LVGL

private:

    // ====================================================
    // ================= CONFIGURACIÓN =====================
    // ====================================================
    static constexpr uint16_t SCREEN_WIDTH  = 320;
    static constexpr uint16_t SCREEN_HEIGHT = 240;

    // Valores de calibración del touch (crudos)
    static constexpr uint16_t TOUCH_MIN_X = 200;
    static constexpr uint16_t TOUCH_MAX_X = 3700;
    static constexpr uint16_t TOUCH_MIN_Y = 240;
    static constexpr uint16_t TOUCH_MAX_Y = 3800;


    // ====================================================
    // ================= PINES TOUCH =======================
    // ====================================================
    static constexpr uint8_t XPT2046_IRQ  = 36;
    static constexpr uint8_t XPT2046_MOSI = 32;
    static constexpr uint8_t XPT2046_MISO = 39;
    static constexpr uint8_t XPT2046_CLK  = 25;
    static constexpr uint8_t XPT2046_CS   = 33;


    // ====================================================
    // ================= BUFFER LVGL =======================
    // ====================================================
    // Buffer parcial (1/10 de pantalla para ahorrar RAM)
    static lv_color_t _buf[SCREEN_WIDTH * SCREEN_HEIGHT / 10];


    // ====================================================
    // ================= HARDWARE ==========================
    // ====================================================
    TFT_eSPI _tft;                                 // Driver de pantalla
    SPIClass _tsSPI = SPIClass(VSPI);               // SPI dedicado para touch
    XPT2046_Touchscreen _ts = XPT2046_Touchscreen(XPT2046_CS, XPT2046_IRQ);


    // ====================================================
    // ================= SINGLETON =========================
    // ====================================================
    // Necesario porque LVGL usa callbacks estáticos
    static DisplayDriver* _instance;


    // ====================================================
    // ================= CALLBACKS LVGL ====================
    // ====================================================
    static void dispFlush(lv_display_t* disp,
                          const lv_area_t* area,
                          uint8_t* px_map);

    static void touchRead(lv_indev_t* drv,
                          lv_indev_data_t* data);
};



// ========================================================
// ================= VARIABLES ESTÁTICAS ==================
// ========================================================
lv_color_t DisplayDriver::_buf[SCREEN_WIDTH * SCREEN_HEIGHT / 10];
DisplayDriver* DisplayDriver::_instance = nullptr;



// ========================================================
// ================= INICIALIZACIÓN =======================
// ========================================================
inline void DisplayDriver::begin()
{
    // Guardar instancia para callbacks
    _instance = this;

    // ---------- Inicializar LVGL ----------
    lv_init();

    // ---------- Inicializar TFT ----------
    _tft.begin();
    _tft.setRotation(3);


    // ---------- Inicializar Touch ----------
    DEBUG_PRINTLN(DEBUG_DISPLAY, "[TOUCH] Inicializando XPT2046...");
    _tsSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    _ts.begin(_tsSPI);
    _ts.setRotation(3);
    DEBUG_PRINTLN(DEBUG_DISPLAY, "[TOUCH] XPT2046 inicializado");

    // ---------- Configurar display en LVGL ----------
    lv_display_t* disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);

    lv_display_set_buffers(
        disp,
        _buf,
        nullptr,
        sizeof(_buf),
        LV_DISPLAY_RENDER_MODE_PARTIAL
    );

    lv_display_set_flush_cb(disp, dispFlush);

    // ---------- Configurar input (touch) ----------
    lv_indev_t* indev = lv_indev_create();

    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchRead);

    // ---------- Inicializar UI (SquareLine) ----------
    ui_init();
}



// ========================================================
// ================= LOOP DE ACTUALIZACIÓN ================
// ========================================================
inline void DisplayDriver::update() {
    lv_tick_inc(10);
    lv_timer_handler();
}



// ========================================================
// ================= CALLBACK DE RENDER ===================
// ========================================================
inline void DisplayDriver::dispFlush(lv_display_t* disp,
                                     const lv_area_t* area,
                                     uint8_t* px_map)
{
    uint32_t width  = (area->x2 - area->x1 + 1);
    uint32_t height = (area->y2 - area->y1 + 1);

    _instance->_tft.startWrite();

    // Define la ventana de escritura en pantalla
    _instance->_tft.setAddrWindow(area->x1, area->y1, width, height);

    // Envía los píxeles al display
    _instance->_tft.pushColors((uint16_t*)px_map, width * height, true);

    _instance->_tft.endWrite();

    // Notifica a LVGL que terminó el render
    lv_display_flush_ready(disp);
}



// ========================================================
// ================= CALLBACK DE TOUCH ====================
// ========================================================
inline void DisplayDriver::touchRead(lv_indev_t*,
                                     lv_indev_data_t* data)
{
    // Si no hay toque
    if (!_instance->_ts.touched())
    {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    // Leer punto crudo
    TS_Point p = _instance->_ts.getPoint();

    // ---------- 1) Limitar valores crudos ----------
    int32_t rawX = constrain(p.x, TOUCH_MIN_X, TOUCH_MAX_X);
    int32_t rawY = constrain(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y);

    // ---------- 2) Mapear a coordenadas pantalla ----------
    int32_t x = map(rawX, TOUCH_MIN_X, TOUCH_MAX_X, 0, SCREEN_WIDTH  - 1);
    int32_t y = map(rawY, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, SCREEN_HEIGHT - 1);

    // ---------- 3) Clamp final (seguridad) ----------
    x = constrain(x, 0, SCREEN_WIDTH  - 1);
    y = constrain(y, 0, SCREEN_HEIGHT - 1);

    // ---------- 4) Cargar datos en LVGL ----------
    data->point.x = static_cast<int16_t>(x);
    data->point.y = static_cast<int16_t>(y);
    data->state   = LV_INDEV_STATE_PRESSED;
}