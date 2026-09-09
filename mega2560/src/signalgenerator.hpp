#pragma once
#include <AD9833.h>
#include "coilexcitationmanager.hpp"

// Implementa ISignalGenerator (declarada en coilexcitationmanager.hpp, mismo
// patron que IMagnetometer en magnetometermanager.hpp) para que
// CoilExcitationManager pueda testearse en host con un doble.
class SignalGenerator : public ISignalGenerator {
  private:
    AD9833* _gen;
    uint8_t _cs;
    bool _initialized;
    float _freq;
    void _ensureInitialized();
  public:
    SignalGenerator(SPIClass& spi, uint8_t cs);
    ~SignalGenerator() override;
    void begin() override;
    // freq=0 (el default) no significa "0 Hz" -- significa "reusar la
    // ultima frecuencia aplicada" (_freq no se pisa si freq no es > 0).
    void setSineWave(float freq = 0) override;
    void setTriangleWave(float freq = 0);
    void setSquareWave(float freq = 0);
    void off() override;
};

inline SignalGenerator::SignalGenerator(SPIClass& spi, uint8_t cs) : _cs(cs), _initialized(false), _freq(50) {
  _gen = new AD9833(cs, &spi);  
}

inline SignalGenerator::~SignalGenerator() {
    delete _gen;
}

inline void SignalGenerator::_ensureInitialized() {
    if (!_initialized) {
        _gen->begin();
        _initialized = true;
    }
}

inline void SignalGenerator::begin() {
  _ensureInitialized();
}

inline void SignalGenerator::off() {
    _ensureInitialized();
    _gen->setWave(AD9833_OFF);
}

// Las 3 formas de onda siguen el mismo patron: guardar la frecuencia
// (si se paso una nueva), apagar la salida, reconfigurar frecuencia+forma,
// y recien ahi reactivar via setWave(). El off() previo evita que el AD9833
// saque un pico/glitch transitorio con la config vieja mientras se escriben
// los registros nuevos.
inline void SignalGenerator::setSineWave(float freq) {
    _ensureInitialized();
    if (freq > 0)
        _freq = freq;
    off();
    _gen->setFrequency(_freq);
    _gen->setWave(AD9833_SINE);
}

inline void SignalGenerator::setTriangleWave(float freq) {
    _ensureInitialized();
    if (freq > 0)
        _freq = freq;
    off();
    _gen->setFrequency(_freq);
    _gen->setWave(AD9833_TRIANGLE);
}

inline void SignalGenerator::setSquareWave(float freq) {
    _ensureInitialized();
    if (freq > 0)
        _freq = freq;
    off();
    _gen->setFrequency(_freq);
    _gen->setWave(AD9833_SQUARE1);
}