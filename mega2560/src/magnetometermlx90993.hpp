#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90393.h>

#include "magnetometermanager.hpp"
#include "fieldsampler.hpp"
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_MAGNETOMETER_MLX90393 = true;

// Configuracion de conversion del sensor. Es un compromiso medido en papel,
// no en banco:
//  - OSR_0 + FILTER_0 es la conversion mas corta (tconv = 1.27 ms, ver
//    mlx90393_tconv). A 100 Hz es lo que deja ~330 muestras/s,
//    y cuanto menos integra el sensor menos atenua una senal rapida.
//  - Lo que pierde en ruido por muestra lo recupera el ajuste: cada ventana de
//    2 s promedia decenas de grupos.
// Si en banco el piso de ruido molesta, OSR_1 (1.84 ms) es el siguiente paso;
// FieldSampler recalcula el plan solo, porque pregunta conversionUs().
constexpr mlx90393_oversampling MLX90393_SAMPLING_OSR = MLX90393_OSR_0;
constexpr mlx90393_filter MLX90393_SAMPLING_FILTER = MLX90393_FILTER_0;

// Lo que cuestan en bus un trigger (SM) + una lectura (RM) a 100 kHz: ~10
// bytes de I2C. Con 400 kHz se podria bajar, pero no hay Wire.setClock hoy.
constexpr uint32_t MLX90393_BUS_OVERHEAD_US = 1300;

// Mide el valor eficaz (RMS) del campo, no una instantanea: ver
// fieldsampler.hpp. begin() solo inicializa el chip; el muestreo arranca con
// beginSampling() cuando Engine sabe la frecuencia del experimento.
class MagnetometerMlx90393 : public IMagnetometer, private IFieldBus {
private:
    const char* _name;
    uint8_t _address;

    Adafruit_MLX90393 _sensor;
    FieldSampler _sampler;

    float _magneticField;   // mT (el RMS del ultimo ventaneo)
    bool _isValid;

    // IFieldBus
    uint32_t nowUs() const override { return micros(); }
    bool trigger() override { return _sensor.startSingleMeasurement(); }
    bool fetch(float& x, float& y, float& z) override { return _sensor.readMeasurement(&x, &y, &z); }
    uint32_t conversionUs() const override {
        return (uint32_t)(mlx90393_tconv[MLX90393_SAMPLING_FILTER][MLX90393_SAMPLING_OSR] * 1000.0f + 0.5f);
    }
    uint32_t overheadUs() const override { return MLX90393_BUS_OVERHEAD_US; }

public:
    MagnetometerMlx90393(const char* name, uint8_t address = 0x0C);
    void begin() override;

    // No hace nada: un MLX real se lee por poll(), sin bloquear.
    void update() override {}
    bool isAsync() const override { return true; }
    bool poll() override;
    bool beginSampling(float freqHz, uint32_t windowMs) override;
    void endSampling() override;

    float getMagneticField() const override;
    bool isValid() const override;
    const char* getName() const override;
};

inline MagnetometerMlx90393::MagnetometerMlx90393(const char* name, uint8_t address) :
    _name(name),
    _address(address),
    _sampler(*this),
    _magneticField(0),
    _isValid(false)
{}

inline void MagnetometerMlx90393::begin() {
    _isValid = _sensor.begin_I2C(_address, &Wire);

    DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, F("[MLX90393] begin_I2C addr=0x"));
    DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, _address, HEX);
    DEBUG_PRINTLN(DEBUG_MAGNETOMETER_MLX90393, _isValid ? F(" -> OK") : F(" -> FALLO (sensor no detectado en el bus)"));

    if (!_isValid)
        return;

    _sensor.setGain(MLX90393_GAIN_1X);

    _sensor.setResolution(MLX90393_X, MLX90393_RES_17);
    _sensor.setResolution(MLX90393_Y, MLX90393_RES_17);
    _sensor.setResolution(MLX90393_Z, MLX90393_RES_16);

    _sensor.setOversampling(MLX90393_SAMPLING_OSR);
    _sensor.setFilter(MLX90393_SAMPLING_FILTER);
}

inline bool MagnetometerMlx90393::beginSampling(float freqHz, uint32_t windowMs) {
    if (!_isValid) return false;
    bool ok = _sampler.begin(freqHz, windowMs);

    DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, F("[MLX90393] muestreo f="));
    DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, freqHz, 3);
    DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, F(" Hz ventana="));
    DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, windowMs);
    DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, F(" ms -> "));
    if (ok) {
        DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, _sampler.plan().groupSize);
        DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, F(" muestras/grupo cada "));
        DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, _sampler.plan().spacingUs);
        DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, F(" us, grupo de "));
        DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, _sampler.plan().spanUs);
        DEBUG_PRINTLN(DEBUG_MAGNETOMETER_MLX90393, _sampler.plan().uniform ? F(" us (uniforme)") : F(" us (fases repartidas)"));
    } else {
        DEBUG_PRINTLN(DEBUG_MAGNETOMETER_MLX90393, F("RECHAZADO (frecuencia fuera de rango)"));
    }
    return ok;
}

inline void MagnetometerMlx90393::endSampling() {
    _sampler.stop();
}

inline bool MagnetometerMlx90393::poll() {
    if (!_isValid && !_sampler.running()) return false;

    bool fresh = _sampler.poll();

    // Un bus que falla cinco veces seguidas invalida el sensor (silencio del
    // CEM1 corta el experimento, ver Engine); el primer acierto lo recupera.
    // begin() no se reintenta aca: re-inicializar el chip en caliente es lo
    // que ya fallaba en banco (ver MagnetometerManager::addMagnetometer).
    _isValid = _sampler.healthy();

    if (!fresh) return false;

    _magneticField = _sampler.rmsUt() / 1000.0f;

    DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, F("[MLX90393] "));
    DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, _name);
    DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, F(" RMS = "));
    DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, _sampler.rmsUt(), 3);
    DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, F(" uT / "));
    DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, _magneticField, 6);
    DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, F(" mT (grupos="));
    DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, _sampler.lastGroups());
    DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, F(" descartados="));
    DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, _sampler.lastDiscarded());
    DEBUG_PRINTLN(DEBUG_MAGNETOMETER_MLX90393, F(")"));
    return true;
}

inline float MagnetometerMlx90393::getMagneticField() const {
    return _magneticField;
}

inline bool MagnetometerMlx90393::isValid() const {
    return _isValid;
}

inline const char* MagnetometerMlx90393::getName() const {
    return _name;
}
