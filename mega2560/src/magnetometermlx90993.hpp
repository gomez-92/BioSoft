#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90393.h>

#include "magnetometermanager.hpp"
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_MAGNETOMETER_MLX90393 = true;

class MagnetometerMlx90393 : public IMagnetometer {
private:
    const char* _name;
    uint8_t _address;

    Adafruit_MLX90393 _sensor;

    float _x;
    float _y;
    float _z;
    float _magneticField;

    bool _isValid;

public:
    MagnetometerMlx90393(const char* name, uint8_t address = 0x0C);
    void begin() override;
    void update() override;

    float getMagneticField() const override;
    bool isValid() const override;
    const char* getName() const override;

    float getX() const;
    float getY() const;
    float getZ() const;
};

inline MagnetometerMlx90393::MagnetometerMlx90393(const char* name, uint8_t address) : 
    _name(name),
    _address(address),
    _x(0),
    _y(0),
    _z(0),
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

    _sensor.setOversampling(MLX90393_OSR_3);
    _sensor.setFilter(MLX90393_FILTER_5);
}

inline void MagnetometerMlx90393::update() {
    if (!_isValid)
        return;

    if (_sensor.readData(&_x, &_y, &_z)) {
        // Adafruit_MLX90393::readData() devuelve uT (ver su propio comentario
        // "Convert data to uT and float", Adafruit_MLX90393.cpp) -- el resto
        // del sistema (targets del ESP32, MagnetometerVoltageSim, FieldController,
        // DetectorConfigBuilder) trabaja en mT, asi que se convierte aca antes
        // de que el valor salga de esta clase. Sin esta conversion el sensor
        // real reportaria ~1000x lo que el resto del sistema espera.
        float magneticFieldUt = sqrtf(_x * _x + _y * _y + _z * _z);
        _magneticField = magneticFieldUt / 1000.0f;
        _isValid = true;

        // Se loguean ambas unidades a proposito: con el campo base (sin
        // bobinas excitando) el valor en mT puede ser tan chico que se vea
        // como 0.0000 en el log -- el valor crudo en uT permite distinguir
        // "el sensor no esta leyendo nada" de "el campo ambiente es
        // realmente muy chico en mT".
        DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, F("[MLX90393] "));
        DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, _name);
        DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, F(" = "));
        DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, magneticFieldUt, 4);
        DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, F(" uT / "));
        DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, _magneticField, 6);
        DEBUG_PRINTLN(DEBUG_MAGNETOMETER_MLX90393, F(" mT"));
    }
    else {
        _isValid = false;
        DEBUG_PRINTLN(DEBUG_MAGNETOMETER_MLX90393, F("[MLX90393] readData() fallo"));
    }
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

inline float MagnetometerMlx90393::getX() const {
    return _x;
}

inline float MagnetometerMlx90393::getY() const {
    return _y;
}

inline float MagnetometerMlx90393::getZ() const {
    return _z;
}