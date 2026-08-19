#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90393.h>

#include "magnetometermanager.hpp"

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
        _magneticField = sqrtf(_x * _x + _y * _y + _z * _z);
        _isValid = true;
    }
    else {
        _isValid = false;
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