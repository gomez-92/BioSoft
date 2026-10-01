#pragma once

#include <math.h>
#include <stdint.h>

// Tara del campo ambiente del magnetometro.
//
// El MLX90393 mide el campo TOTAL: el de las bobinas mas el ambiente (~25 a
// 65 uT de campo terrestre, mas lo que ponga el equipo del laboratorio). Con
// un objetivo de 1 mT y una tolerancia de 5 % la banda es de +-50 uT, asi que
// el ambiente puede ocuparla entera, y en campo nulo -- donde el objetivo
// medido es 0 -- es justamente lo unico que se lee.
//
// La tara se toma con las bobinas APAGADAS, al arrancar el experimento: se
// promedian N lecturas del VECTOR (x, y, z) y de ahi en adelante el campo es
// |medido - ambiente|. Se resta el vector y no la magnitud: |B + A| - |A| solo
// vale |B| cuando B y A son paralelos; |(B + A) - A| = |B| siempre.
//
// Supone que el ambiente no cambia durante la corrida. Se rechaza (no se
// recorta ni se usa a medias) una tara cuyas lecturas no concuerdan entre si,
// que es lo que pasa si alguien mueve la jaula o hay una fuente de campo
// intermitente cerca: tararia mal y no habria como enterarse despues.
//
// Clase pura y sin Arduino.h para poder probarla en el host. Las unidades las
// pone quien la use (el MLX90393 la alimenta en uT).

enum class TareStatus : uint8_t {
  Idle,        // sin tara: apply() devuelve la magnitud sin descontar nada
  Collecting,  // juntando lecturas
  Ready,       // ambiente medido y aceptado
  Failed       // lecturas inconsistentes o fuera de rango: no se usa
};

struct TareConfig {
  // Lecturas que se promedian. Con la cadencia de 500 ms de CEM1 son 2 s.
  uint8_t samples = 4;
  // Dispersion maxima aceptada (max - min) en CADA eje, en uT. El ruido
  // propio del MLX90393 con la configuracion de begin() es de unos pocos uT;
  // 10 uT deja pasar el ruido y frena un movimiento real. Valor de partida,
  // a verificar en banco con el sensor montado.
  float maxSpread = 10.0f;
  // Magnitud maxima del ambiente aceptada, en uT. Mas que esto no es campo
  // ambiente sino un iman, una fuente o un cable cerca del sensor.
  float maxAmbient = 300.0f;
};

// Rangos aceptados para los umbrales que llegan de la tarjeta SD
// (control.tare). Fuera de rango se rechaza el valor entero y queda el
// anterior: una dispersion de 0 rechazaria toda tara, una de 10000 aceptaria
// cualquiera, y ninguna de las dos es una calibracion.
namespace TareLimits {
  // El tope de lecturas sale del tiempo: settle + (N-1) cadencias de 500 ms
  // tienen que entrar en TareTiming::TimeoutMs (6 -> 3 s de 4 s).
  constexpr long MinSamples = 2;
  constexpr long MaxSamples = 6;
  constexpr float MinSpread = 1.0f;
  constexpr float MaxSpread = 100.0f;
  constexpr float MinAmbient = 50.0f;
  constexpr float MaxAmbient = 1000.0f;

  inline bool isValidSamples(long n) { return n >= MinSamples && n <= MaxSamples; }
  // Las comparaciones son falsas con NaN: un NaN nunca es valido.
  inline bool isValidSpread(float v) { return v >= MinSpread && v <= MaxSpread; }
  inline bool isValidAmbient(float v) { return v >= MinAmbient && v <= MaxAmbient; }
}

namespace TareTiming {
  // Espera entre el start y la primera lectura de la tara: deja decaer
  // cualquier resto de la corrida anterior en las bobinas.
  constexpr unsigned long SettleMs = 500;
  // Tiempo maximo de todo el proceso de tara. Tiene que quedar por debajo
  // del timeout de la pantalla Procesando de la ESP32 (6 s): pasado eso la
  // pantalla ya volvio a Principal.
  constexpr unsigned long TimeoutMs = 4000;
}

class FieldTare {
  private:
    TareConfig _config;
    TareStatus _status = TareStatus::Idle;
    uint8_t _count = 0;
    // Mientras junta: suma de lecturas. Al aceptar: el ambiente (promedio).
    float _sum[3] = {0, 0, 0};
    float _min[3] = {0, 0, 0};
    float _max[3] = {0, 0, 0};
    float _worstSpread = 0.0f;

    void _evaluate();

  public:
    // Arranca una tara nueva y descarta la anterior.
    void begin(const TareConfig& config = TareConfig());
    // Vuelve a "sin tara".
    void clear();
    // Suma una lectura mientras se esta juntando; en cualquier otro estado la
    // ignora. Devuelve el estado resultante: al llegar a la ultima lectura
    // decide Ready o Failed.
    TareStatus addSample(float x, float y, float z);
    TareStatus status() const { return _status; }
    bool isReady() const { return _status == TareStatus::Ready; }

    // Magnitud del campo ya descontado el ambiente. Sin tara lista, la
    // magnitud sin descontar.
    float apply(float x, float y, float z) const;

    float ambientX() const { return _status == TareStatus::Ready ? _sum[0] : 0.0f; }
    float ambientY() const { return _status == TareStatus::Ready ? _sum[1] : 0.0f; }
    float ambientZ() const { return _status == TareStatus::Ready ? _sum[2] : 0.0f; }
    float ambientMagnitude() const;
    // Mayor dispersion (max - min) entre ejes de la ultima evaluacion, para el log.
    float worstSpread() const { return _worstSpread; }
    uint8_t sampleCount() const { return _count; }
};

inline void FieldTare::begin(const TareConfig& config) {
  _config = config;
  if (_config.samples < 2) _config.samples = 2;  // la dispersion pide al menos dos
  _status = TareStatus::Collecting;
  _count = 0;
  _worstSpread = 0.0f;
  for (uint8_t i = 0; i < 3; i++) {
    _sum[i] = 0.0f;
    _min[i] = 0.0f;
    _max[i] = 0.0f;
  }
}

inline void FieldTare::clear() {
  _status = TareStatus::Idle;
  _count = 0;
  _worstSpread = 0.0f;
  for (uint8_t i = 0; i < 3; i++) {
    _sum[i] = 0.0f;
    _min[i] = 0.0f;
    _max[i] = 0.0f;
  }
}

inline TareStatus FieldTare::addSample(float x, float y, float z) {
  if (_status != TareStatus::Collecting) return _status;

  // Un NaN/inf envenenaria el promedio y despues el campo de toda la corrida.
  if (!isfinite(x) || !isfinite(y) || !isfinite(z)) {
    _status = TareStatus::Failed;
    return _status;
  }

  const float v[3] = {x, y, z};
  for (uint8_t i = 0; i < 3; i++) {
    if (_count == 0 || v[i] < _min[i]) _min[i] = v[i];
    if (_count == 0 || v[i] > _max[i]) _max[i] = v[i];
    _sum[i] += v[i];
  }
  _count++;

  if (_count >= _config.samples) _evaluate();
  return _status;
}

inline void FieldTare::_evaluate() {
  _worstSpread = 0.0f;
  for (uint8_t i = 0; i < 3; i++) {
    float spread = _max[i] - _min[i];
    if (spread > _worstSpread) _worstSpread = spread;
  }
  for (uint8_t i = 0; i < 3; i++) _sum[i] /= (float)_count;

  float magnitude = sqrtf(_sum[0] * _sum[0] + _sum[1] * _sum[1] + _sum[2] * _sum[2]);
  if (_worstSpread > _config.maxSpread || magnitude > _config.maxAmbient) {
    _status = TareStatus::Failed;
    return;
  }
  _status = TareStatus::Ready;
}

inline float FieldTare::apply(float x, float y, float z) const {
  float dx = x, dy = y, dz = z;
  if (_status == TareStatus::Ready) {
    dx -= _sum[0];
    dy -= _sum[1];
    dz -= _sum[2];
  }
  return sqrtf(dx * dx + dy * dy + dz * dz);
}

inline float FieldTare::ambientMagnitude() const {
  if (_status != TareStatus::Ready) return 0.0f;
  return sqrtf(_sum[0] * _sum[0] + _sum[1] * _sum[1] + _sum[2] * _sum[2]);
}
