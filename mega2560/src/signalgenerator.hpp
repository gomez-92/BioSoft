#pragma once
#include <AD9833.h>

class SignalGenerator {
  private:
    AD9833* _gen;
    uint8_t _cs;
    bool _initialized;
    float _freq;
    void _ensureInitialized();
  public:
    SignalGenerator(SPIClass& spi, uint8_t cs);
    ~SignalGenerator();
    void begin();
    void setSineWave(float freq = 0);
    void setTriangleWave(float freq = 0);
    void setSquareWave(float freq = 0);
    void off();
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