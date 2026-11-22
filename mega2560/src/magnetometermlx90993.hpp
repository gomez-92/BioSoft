#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90393.h>

#include "magnetometermanager.hpp"
#include "fieldsampler.hpp"
#include "log.hpp"

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
    // begin_I2C() ya se llamo una vez (ver begin(): no se puede llamar dos).
    bool _driverCreated;
    // Ultimo resultado de begin() (-1 = ninguno): se loguea solo si cambia,
    // porque begin() se reintenta en cada start con el sensor ausente.
    int8_t _beginState = -1;

    // IFieldBus
    uint32_t nowUs() const override { return micros(); }
    bool trigger() override { return _sensor.startSingleMeasurement(); }
    bool fetch(float& x, float& y, float& z) override { return _sensor.readMeasurement(&x, &y, &z); }
    uint32_t conversionUs() const override {
        // La tabla esta en flash (copia local de la libreria, lib/Adafruit_MLX90393).
        return (uint32_t)(pgm_read_float(&mlx90393_tconv[MLX90393_SAMPLING_FILTER][MLX90393_SAMPLING_OSR]) * 1000.0f + 0.5f);
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
    _isValid(false),
    _driverCreated(false)
{}

// begin_I2C() de Adafruit_MLX90393 2.0.5 NO se puede llamar dos veces: hace
// `delete i2c_dev` sin ponerlo en NULL y despues usa el puntero liberado. El
// free() de avr-libc escribe su lista libre encima (direccion 0, _wire
// corrupto), el detect a la direccion 0 lo contesta el ADS1115 (general
// call) y la primera escritura llama un metodo virtual por un puntero roto:
// el Mega se reinicia. Con el MLX ausente pasaba en cada arranque, porque
// addMagnetometer() reintenta begin() de un sensor invalido -- bucle de
// reinicios antes de llegar a Engine::begin(). Con el MLX presente, el
// mismo bug era el "begin_I2C falla la segunda vez" del bring-up INT-001.
// Por eso: begin_I2C() una sola vez y solo si el chip contesta; los
// reintentos repiten la inicializacion con los metodos publicos.
inline void MagnetometerMlx90393::begin() {
    Wire.beginTransmission(_address);
    bool present = (Wire.endTransmission() == 0);

    if (!present) {
        _isValid = false;
    } else if (!_driverCreated) {
        _driverCreated = true;
        _isValid = _sensor.begin_I2C(_address, &Wire);
    } else {
        _isValid = _sensor.exitMode() && _sensor.reset();
    }

    int8_t state = !present ? 0 : !_isValid ? 1 : 2;
    if (state != _beginState) {
        _beginState = state;
        if (state == 0) {
            LOG_W(CAMPO, "MLX90393 no responde en 0x%02X", _address);
        } else if (state == 1) {
            LOG_W(CAMPO, "MLX90393 contesta en 0x%02X pero no inicializa", _address);
        } else {
            LOG_I(CAMPO, "MLX90393 listo en 0x%02X", _address);
        }
    }
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
    if (!_sampler.begin(freqHz, windowMs)) {
        LOG_W(CAMPO, "muestreo rechazado: frecuencia %.3f Hz fuera de rango", freqHz);
        return false;
    }
    LOG_D(CAMPO, "muestreo f=%.3f Hz ventana=%lu ms grupo=%u muestras cada %lu us (%s)", freqHz,
          (unsigned long)windowMs, (unsigned)_sampler.plan().groupSize, (unsigned long)_sampler.plan().spacingUs,
          _sampler.plan().uniform ? "uniforme" : "fases repartidas");
    return true;
}

inline void MagnetometerMlx90393::endSampling() {
    _sampler.stop();
}

inline bool MagnetometerMlx90393::poll() {
    if (!_isValid && !_sampler.running()) return false;

    bool fresh = _sampler.poll();
    bool wasValid = _isValid;

    // Un bus que falla cinco veces seguidas invalida el sensor (silencio del
    // CEM1 corta el experimento, ver Engine); el primer acierto lo recupera.
    // begin() no se reintenta aca: re-inicializar el chip en caliente es lo
    // que ya fallaba en banco (ver MagnetometerManager::addMagnetometer).
    _isValid = _sampler.healthy();
    if (wasValid && !_isValid) {
        LOG_W(CAMPO, "MLX90393 sin respuesta del bus: sensor invalido");
    } else if (!wasValid && _isValid) {
        LOG_I(CAMPO, "MLX90393 vuelve a responder");
    }

    if (!fresh) return false;

    _magneticField = _sampler.rmsUt() / 1000.0f;
    // En uT tambien: cerca del ambiente el valor en mT redondea a 0.0000.
    LOG_D(CAMPO, "%s rms=%.3f uT (%.6f mT) grupos=%u descartados=%u", _name, _sampler.rmsUt(), _magneticField,
          (unsigned)_sampler.lastGroups(), (unsigned)_sampler.lastDiscarded());

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
