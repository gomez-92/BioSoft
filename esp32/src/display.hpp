#pragma once

// ================= LIBRERÍAS =================
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <Wire.h>
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
    // ================= TIPO DE TACTIL ====================
    // ====================================================
    // Las placas "CYD" de 2.8" vienen con dos tactiles distintos segun la
    // revision, y no se distinguen por software:
    //  - ESP32-2432S028R (un micro-USB): resistivo XPT2046 por SPI propio
    //    (VSPI, pines de arriba).
    //  - JC2432W328C (Guition, micro-USB + USB-C, panel ST7789): capacitivo
    //    CST816S por I2C, en los mismos GPIO que la R usaba para el XPT2046.
    //    Con la config de la R en esta placa el XPT2046 "lee" un MISO
    //    flotante (raw=(-4096,-4096) o (4095,4095) en la traza), o sea
    //    tocado permanente en una esquina.
    // Hay que elegir a mano al cambiar de placa. La C tambien necesita
    // TFT_INVERSION_ON en platformio.ini.
    enum class TouchKind { Xpt2046, Cst816s };
    static constexpr TouchKind TOUCH_KIND = TouchKind::Cst816s;

    // Pines del CST816S (JC2432W328C)
    static constexpr uint8_t CST816S_SDA  = 33;
    static constexpr uint8_t CST816S_SCL  = 32;
    static constexpr uint8_t CST816S_RST  = 25;
    static constexpr uint8_t CST816S_INT  = 21;
    static constexpr uint8_t CST816S_ADDR = 0x15;


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

    // Un lector por tipo de tactil; ambos entregan coordenadas de pantalla
    // (ya rotadas) y devuelven false si no hay toque.
    bool _readXpt2046(int32_t& x, int32_t& y);
    bool _readCst816s(int32_t& x, int32_t& y);
    void _beginCst816s();
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
    if (TOUCH_KIND == TouchKind::Xpt2046) {
        DEBUG_PRINTLN(DEBUG_DISPLAY, "[TOUCH] Inicializando XPT2046...");
        _tsSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
        _ts.begin(_tsSPI);
        _ts.setRotation(3);
        DEBUG_PRINTLN(DEBUG_DISPLAY, "[TOUCH] XPT2046 inicializado");
    } else {
        _beginCst816s();
    }

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
    int32_t x = 0, y = 0;
    bool touched = (TOUCH_KIND == TouchKind::Xpt2046)
        ? _instance->_readXpt2046(x, y)
        : _instance->_readCst816s(x, y);

    if (!touched)
    {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    // Clamp final (seguridad)
    x = constrain(x, 0, SCREEN_WIDTH  - 1);
    y = constrain(y, 0, SCREEN_HEIGHT - 1);

    data->point.x = static_cast<int16_t>(x);
    data->point.y = static_cast<int16_t>(y);
    data->state   = LV_INDEV_STATE_PRESSED;
}

// ---------- XPT2046 (resistivo, SPI) ----------
inline bool DisplayDriver::_readXpt2046(int32_t& x, int32_t& y)
{
    if (!_ts.touched()) return false;

    TS_Point p = _ts.getPoint();

    int32_t rawX = constrain(p.x, TOUCH_MIN_X, TOUCH_MAX_X);
    int32_t rawY = constrain(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y);
    x = map(rawX, TOUCH_MIN_X, TOUCH_MAX_X, 0, SCREEN_WIDTH  - 1);
    y = map(rawY, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, SCREEN_HEIGHT - 1);

    // Traza cruda -> mapeada, para calibrar/diagnosticar el tactil en una
    // placa nueva (las revisiones de la CYD difieren en espejado de ejes).
    DEBUG_PRINTF(DEBUG_DISPLAY, "[TOUCH] raw=(%d,%d) -> (%ld,%ld)\n", p.x, p.y, (long)x, (long)y);
    return true;
}

// ---------- CST816S (capacitivo, I2C) ----------
// Mapa de registros: 0x02 cantidad de dedos, 0x03/0x04 X (4 bits altos +
// 8 bajos), 0x05/0x06 Y idem. Las coordenadas son del panel en vertical
// (240x320); se rotan aca a la orientacion 3 de la pantalla (320x240).
inline void DisplayDriver::_beginCst816s()
{
    DEBUG_PRINTLN(DEBUG_DISPLAY, "[TOUCH] Inicializando CST816S (I2C)...");
    pinMode(CST816S_INT, INPUT);
    pinMode(CST816S_RST, OUTPUT);
    digitalWrite(CST816S_RST, LOW);
    delay(10);
    digitalWrite(CST816S_RST, HIGH);
    delay(50);

    Wire.begin(CST816S_SDA, CST816S_SCL);

    Wire.beginTransmission(CST816S_ADDR);
    uint8_t err = Wire.endTransmission();
    if (err != 0) {
        DEBUG_PRINTF(DEBUG_DISPLAY, "[TOUCH] CST816S no responde en 0x%02X (err=%u)\n", CST816S_ADDR, err);
        return;
    }

    // Deshabilitar el auto-sleep: sin esto el chip se duerme tras unos
    // segundos sin toques y deja de contestar hasta el proximo INT, y aca
    // se lee por polling desde LVGL, no por interrupcion.
    Wire.beginTransmission(CST816S_ADDR);
    Wire.write(0xFE);
    Wire.write(0x01);
    Wire.endTransmission();

    DEBUG_PRINTLN(DEBUG_DISPLAY, "[TOUCH] CST816S inicializado");
}

inline bool DisplayDriver::_readCst816s(int32_t& x, int32_t& y)
{
    Wire.beginTransmission(CST816S_ADDR);
    Wire.write(0x02);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(CST816S_ADDR, (uint8_t)5) != 5) return false;

    uint8_t fingers = Wire.read();
    uint8_t xh = Wire.read();
    uint8_t xl = Wire.read();
    uint8_t yh = Wire.read();
    uint8_t yl = Wire.read();
    if (fingers == 0) return false;

    int32_t rawX = ((xh & 0x0F) << 8) | xl;   // 0..239 (panel vertical)
    int32_t rawY = ((yh & 0x0F) << 8) | yl;   // 0..319

    // Rotacion 3 del TFT (misma que _tft.setRotation(3)).
    x = (SCREEN_WIDTH  - 1) - rawY;
    y = rawX;

    DEBUG_PRINTF(DEBUG_DISPLAY, "[TOUCH] raw=(%ld,%ld) -> (%ld,%ld)\n", (long)rawX, (long)rawY, (long)x, (long)y);
    return true;
}
