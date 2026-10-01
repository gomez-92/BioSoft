#pragma once
#include <Arduino.h>
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_DETECTOR = true;

#define MAX_SOURCES 10

// Forward declarations
class Source;
class SourceListener;
class DetectorListener;

// ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// BUFFER
// ===== ===== ===== ===== ===== ===== ===== ===== ===== =====

class Buffer {
  public:
    Buffer();
    explicit Buffer(unsigned int size);
    ~Buffer();
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    void resize(unsigned int size);
    void clear();
    bool addSample(float value, float& overwritten);
    float getLastSample() const;
    unsigned int getCount() const;
    unsigned int capacity() const;
    unsigned long totalAdded() const;
    bool empty() const;
    bool full() const;
    bool isInitialized() const;
    const float* data() const;
  private:
    float* _samples;
    unsigned int _capacity;
    unsigned int _currentIndex;
    bool _isFull;
    unsigned long _totalAdded;
};

inline Buffer::Buffer()
  : _samples(nullptr),
    _capacity(0),
    _currentIndex(0),
    _isFull(false),
    _totalAdded(0)
{}

inline Buffer::Buffer(unsigned int size) : Buffer() { 
  resize(size); 
}

inline Buffer::~Buffer() {
  clear();
}

inline void Buffer::resize(unsigned int size) {
  clear();
  
  if (size == 0) return;
  
  _samples = static_cast<float*>(calloc(size, sizeof(float)));
  
  if (_samples == nullptr) return;
  
  _capacity = size;
  _currentIndex = 0;
  _isFull = false;
  _totalAdded = 0;
}

inline void Buffer::clear() {
  if(_samples != nullptr) {
    free(_samples);
    _samples = nullptr;
  }

  _capacity = 0;
  _currentIndex = 0;
  _isFull = false;
  _totalAdded = 0;
}

inline bool Buffer::addSample(float value, float& overwritten) {
  if(_samples == nullptr || _capacity == 0) return false;
  
  bool hasOverwritten = _isFull;
  
  if(hasOverwritten) {
    overwritten = _samples[_currentIndex];
  }
  
  _samples[_currentIndex] = value;
  _currentIndex++;
  
  if(_currentIndex >= _capacity) {
    _currentIndex = 0;
    _isFull = true;
  }
  
  _totalAdded++;
  return hasOverwritten;
}

inline float Buffer::getLastSample() const {
  if(empty()) return 0.0f;
  
  unsigned int index;
  
  if(_currentIndex == 0) {
    index = _isFull ? (_capacity - 1) : 0;
  }
  else {
    index = _currentIndex - 1;
  }

  return _samples[index];
}

inline unsigned int Buffer::getCount() const {
  return _isFull ? _capacity : _currentIndex;
}

inline unsigned int Buffer::capacity() const {
  return _capacity;
}

inline unsigned long Buffer::totalAdded() const {
  return _totalAdded;
}

inline bool Buffer::empty() const {
  return getCount() == 0;
}

inline bool Buffer::full() const {
  return _isFull;
}

inline bool Buffer::isInitialized() const {
  return _samples != nullptr;
}

inline const float* Buffer::data() const {
  return _samples;
}

// ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// RULE
// ===== ===== ===== ===== ===== ===== ===== ===== ===== =====

class Rule {
  public:
    Rule();
    void reset();
    bool evaluate(uint16_t value, uint32_t currentSample);
    void updateCooldown(uint16_t samples);
    uint32_t getTotalEvents() const;
    uint32_t getLastTriggerSample() const;
    uint16_t getCooldown() const;
  public:
    uint16_t threshold = 0;
    uint16_t cooldown = 0;
    uint16_t maxEvents = 0;
  private:
    uint32_t _totalEvents;
    uint32_t _lastTriggerSample;
    uint16_t _cooldownCounter;
};

inline Rule::Rule() {
    reset();
}

inline void Rule::reset() {
    _totalEvents = 0;
    _lastTriggerSample = 0;
    _cooldownCounter = 0;
}

inline bool Rule::evaluate(uint16_t value, uint32_t currentSample) {
  if (threshold == 0)
    return false;

  if (value < threshold)
    return false;

  if (_cooldownCounter > 0)
    return false;

  if (maxEvents > 0 && _totalEvents >= maxEvents)
    return false;

  _totalEvents++;
  _lastTriggerSample = currentSample;
  _cooldownCounter = cooldown;
  return true;
}

inline void Rule::updateCooldown(uint16_t samples) {
  if (_cooldownCounter == 0)
    return;

  _cooldownCounter =
    (samples >= _cooldownCounter)
    ? 0
    : (_cooldownCounter - samples);
}

inline uint32_t Rule::getTotalEvents() const {
  return _totalEvents;
}

inline uint32_t Rule::getLastTriggerSample() const {
  return _lastTriggerSample;
}

inline uint16_t Rule::getCooldown() const {
  return _cooldownCounter;
}

// ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// SOURCE STATE
// ===== ===== ===== ===== ===== ===== ===== ===== ===== =====

struct SourceConfig {
  size_t bufferSize = 32;
  float normalMin;
  float normalMax;
  float criticalMin;
  float criticalMax;
  Rule critical;
  Rule streak;
  Rule frequency;
};

class SourceState {
  public:
    SourceState();
    void reset();
    // "Out" = fuera del rango normal. Una muestra critica (fuera del
    // rango critico, mas ancho) esta POR DEFINICION tambien fuera del
    // rango normal -- critico es un subconjunto mas severo de "fuera de
    // lo normal", no una categoria aparte. incrementOut/decrementOut se
    // llaman para CUALQUIER muestra fuera de lo normal, sea o no critica
    // ademas.
    void incrementOut();
    void decrementOut();
    // Racha de muestras consecutivas (no relacionado con la ventana del
    // buffer): se incrementa mientras la condicion se sostiene muestra a
    // muestra, y se resetea a 0 apenas llega una muestra que no la
    // cumple. updateOutStreak sigue el mismo criterio de union que
    // incrementOut (una muestra critica tambien avanza la racha "out").
    // Independiente de incrementOut, que cuenta cuantas muestras hay
    // ahora mismo dentro de la ventana del buffer.
    void updateCriticalStreak(bool isCritical);
    void updateOutStreak(bool isOutOfNormal);
    uint16_t getOutCount() const;
    uint16_t getCriticalStreak() const;
    uint16_t getOutStreak() const;
  private:
    uint16_t _outCount;
    uint16_t _criticalStreak;
    uint16_t _outStreak;
};

inline SourceState::SourceState() {
  reset();
}

inline void SourceState::reset() {
  _outCount = 0;
  _criticalStreak = 0;
  _outStreak = 0;
}

inline void SourceState::incrementOut() {
  ++_outCount;
}

inline void SourceState::decrementOut() {
  if (_outCount > 0) {
    --_outCount;
  }
}

inline void SourceState::updateCriticalStreak(bool isCritical) {
  if (!isCritical) {
    _criticalStreak = 0;
    return;
  }
  if (_criticalStreak < 0xFFFF) {
    ++_criticalStreak;
  }
}

inline void SourceState::updateOutStreak(bool isOutOfNormal) {
  if (!isOutOfNormal) {
    _outStreak = 0;
    return;
  }
  if (_outStreak < 0xFFFF) {
    ++_outStreak;
  }
}

inline uint16_t SourceState::getOutCount() const {
  return _outCount;
}

inline uint16_t SourceState::getCriticalStreak() const {
  return _criticalStreak;
}

inline uint16_t SourceState::getOutStreak() const {
  return _outStreak;
}

// ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// SOURCE EVENTS
// ===== ===== ===== ===== ===== ===== ===== ===== ===== =====

enum class EventType {
  Critical,
  Streak,
  Frequency
};

struct SourceEvent {
  EventType type;
  const Source* source;
  uint16_t count;
  uint16_t limit;
};

// Listener de Source
class SourceListener {
  public:
    virtual void onSourceEvent(SourceEvent& event) = 0;
};

// Listener de Detector
class DetectorListener {
  public:
    virtual void onFlag(SourceEvent& event) = 0;
};

struct SourceStatistics {
  uint16_t criticalEvents;
  uint16_t criticalLimit;
  uint16_t streakEvents;
  uint16_t streakLimit;
  uint16_t frequencyEvents;
  uint16_t frequencyLimit;
};

// ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// SOURCE
// ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
class Source {
  public:
    explicit Source(const char* name);
    const char* getName() const;
    bool isConfigured() const;
    void setConfig(const SourceConfig& config);
    const SourceConfig& getConfig() const;
    void reset();
    void addSample(float sample);
    void publish(JsonDocument& document);
    SourceStatistics statistics() const;
    bool hasCriticalFlags() const;
    float score() const;
    void setListener(SourceListener* listener);
  private:
    void evaluate();
    void evaluateRule(Rule& rule, EventType type, uint16_t value);
    bool isCritical(float value) const;
    bool isOutOfNormal(float value) const;
  private:
    const char* _name;
    bool _configured;
    unsigned long _processedSamples;
    Buffer _buffer;
    SourceConfig _config;
    SourceState _state;
    SourceListener* _listener;
};

inline Source::Source(const char* name) : _name(name), _configured(false), _processedSamples(0), _listener(nullptr) {}

inline const char* Source::getName() const {
  return _name;
}

inline bool Source::isConfigured() const {
  return _configured;
}

inline void Source::setConfig(const SourceConfig& config) {
  _config = config;
  _configured = true;
  reset();
}

inline const SourceConfig& Source::getConfig() const {
  return _config;
}

inline void Source::reset() {
  _buffer.resize(_config.bufferSize);
  _state.reset();
  _processedSamples = 0;
}

inline void Source::addSample(float sample) {
  if (!_configured) return;
  float overwritten = 0.0f;
  bool hasOld = _buffer.addSample(sample, overwritten);

  // "out de normal" es un superconjunto de "critico" (rangos anidados:
  // criticalMin<=normalMin<normalMax<=criticalMax) -- una muestra critica
  // tambien esta, por definicion, fuera de lo normal. decrementOut se
  // llama para CUALQUIER muestra evictada que estuviera fuera de lo
  // normal, sea o no ademas critica.
  if (hasOld && isOutOfNormal(overwritten)) {
    _state.decrementOut();
  }

  bool critical = isCritical(sample);
  bool outOfNormal = isOutOfNormal(sample);

  if (outOfNormal) {
    _state.incrementOut();
  }

  // Racha consecutiva de la muestra que se acaba de agregar -- no se ve
  // afectada por lo que sale del buffer (a diferencia de incrementOut,
  // que si depende de la ventana). updateOutStreak sigue el mismo
  // criterio de union: una muestra critica tambien avanza esta racha.
  _state.updateCriticalStreak(critical);
  _state.updateOutStreak(outOfNormal);

  evaluate();
}

// Unico caller es Engine::_sendFlagsData(), cuyo cuerpo esta comentado hoy
// (dump periodico de todas las sources, reemplazado por el push individual
// por evento via Detector::onFlag/flag_data -- ver CLAUDE.md). En la
// practica esta funcion no se ejecuta.
inline void Source::publish(JsonDocument& doc) {
  SourceStatistics stats = statistics();
  doc["crit"]    = stats.criticalEvents;
  doc["mcrit"]   = stats.criticalLimit;
  doc["streak"]  = stats.streakEvents;
  doc["mstreak"] = stats.streakLimit;
  doc["freq"]    = stats.frequencyEvents;
  doc["mfreq"]   = stats.frequencyLimit;
}

inline SourceStatistics Source::statistics() const {
  SourceStatistics stats;
  stats.criticalEvents  = _config.critical.getTotalEvents();
  stats.criticalLimit   = _config.critical.maxEvents;
  stats.streakEvents    = _config.streak.getTotalEvents();
  stats.streakLimit     = _config.streak.maxEvents;
  stats.frequencyEvents = _config.frequency.getTotalEvents();
  stats.frequencyLimit  = _config.frequency.maxEvents;
  return stats;
}

// SIN USO hoy (grep confirma que nadie la llama) -- resto del scoring de
// salud de un source individual, de cuando esa logica vivia en el Detector.
// Se removio de aca en un refactor: la salud general ahora la calcula la
// ESP32 a partir de las flags que recibe (SystemData::evaluateHealth(),
// ver CLAUDE.md). Se deja el metodo por si se retoma, pero no forma parte
// del camino activo.
inline bool Source::hasCriticalFlags() const {
  SourceStatistics stats = statistics();
  return stats.criticalEvents > 0;
}

// SIN USO hoy, mismo origen que hasCriticalFlags() arriba -- pesos 50/10
// sin calibrar ni referenciados desde ningun lado activo.
inline float Source::score() const {
  SourceStatistics stats = statistics();

  float frequencyRatio = 0.0f;
  if (stats.frequencyLimit > 0) {
    frequencyRatio = static_cast<float>(stats.frequencyEvents) / stats.frequencyLimit;
  }

  float streakRatio = 0.0f;
  if (stats.streakLimit > 0) {
    streakRatio = static_cast<float>(stats.streakEvents) / stats.streakLimit;
  }

  return 50.0f * frequencyRatio + 10.0f * streakRatio;
}

inline void Source::setListener(SourceListener* listener) {
  _listener = listener;
}

inline void Source::evaluate() {
  unsigned long totalAdded = _buffer.totalAdded();
  unsigned long newSamplesUL = totalAdded - _processedSamples;

  if (newSamplesUL == 0) return;

  uint16_t newSamples =
    (newSamplesUL > 0xFFFFUL)
    ? 0xFFFF
    : static_cast<uint16_t>(newSamplesUL);

  _processedSamples = totalAdded;

  _config.critical.updateCooldown(newSamples);
  _config.streak.updateCooldown(newSamples);
  _config.frequency.updateCooldown(newSamples);

  // critical/streak: racha de muestras consecutivas (se resetea con
  // cualquier muestra que no cumpla la condicion).
  evaluateRule(
    _config.critical,
    EventType::Critical,
    _state.getCriticalStreak());

  evaluateRule(
    _config.streak,
    EventType::Streak,
    _state.getOutStreak());

  // frequency: a proposito sigue basada en la ventana del buffer (cuantas
  // muestras "no normales" hay ahora mismo, esten o no seguidas) -- mide
  // inestabilidad general en el tiempo, no un episodio puntual. getOutCount()
  // ya incluye las criticas (ver incrementOut en addSample), asi que no
  // hay que sumarlas aparte -- sumarlas de nuevo las contaria dos veces.
  evaluateRule(
    _config.frequency,
    EventType::Frequency,
    _state.getOutCount());
}

inline void Source::evaluateRule(Rule& rule, EventType type, uint16_t value) {
  if (!rule.evaluate(value, _buffer.totalAdded()))
    return;

  if (_listener == nullptr)
    return;

  SourceEvent event;

  event.type = type;
  event.source = this;
  event.count = rule.getTotalEvents();
  event.limit = rule.maxEvents;

  
  _listener->onSourceEvent(event);
}

inline bool Source::isCritical(float value) const {
  return value < _config.criticalMin || value > _config.criticalMax;
}

inline bool Source::isOutOfNormal(float value) const {
  return value < _config.normalMin || value > _config.normalMax;
}

// ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// DETECTOR
// ===== ===== ===== ===== ===== ===== ===== ===== ===== =====

class Detector : public SourceListener {
  public:
    Detector();
    ~Detector();
    uint8_t getCount() const;
    void clear();
    void reset();
    bool addSource(const char* name);
    bool removeSource(const char* name);
    Source* findSource(const char* name);
    Source* getSource(uint8_t index);
    bool configureSource(const char* name, const SourceConfig& config);
    bool addSample(const char* name, float sample);
    bool publishSource(const char* name, JsonDocument& document);
    void setListener(DetectorListener* listener);
    // Interruptor general (`detector.enabled` de la tarjeta SD). Apagado,
    // addSample() descarta todo: ninguna fuente acumula muestras ni levanta
    // flags, asi que ninguna corta el experimento. Las fuentes NO se borran
    // ni se reconfiguran -- volver a encenderlo las encuentra como estaban.
    void setEnabled(bool enabled);
    bool isEnabled() const;
  public:
    void onSourceEvent(SourceEvent& event) override;
  private:
    DetectorListener* _listener;
    bool _enabled;
    Source* _sources[MAX_SOURCES];
    uint8_t _count;
};

inline Detector::Detector() : _listener(nullptr), _enabled(true), _count(0) {
  for (uint8_t i = 0; i < MAX_SOURCES; i++) {
    _sources[i] = nullptr;
  }
}

inline Detector::~Detector() {
  clear();
}

inline uint8_t Detector::getCount() const {
  return _count;
}

inline void Detector::clear() {
  for (uint8_t i = 0; i < _count; i++) {
    delete _sources[i];
    _sources[i] = nullptr;
  }

  _count = 0;
}

inline void Detector::reset() {
  for (uint8_t i = 0; i < _count; i++) {
    _sources[i]->reset();
  }
} 

inline bool Detector::addSource(const char* name) {
  if (_count >= MAX_SOURCES) 
    return false;

  if (findSource(name) != nullptr)
    return false;

  Source* source = new Source(name);
  source->setListener(this);
  _sources[_count++] = source;
  return true;
}

inline bool Detector::removeSource(const char* name) {
  for (uint8_t i = 0; i < _count; i++) {
    if (strcmp(_sources[i]->getName(), name) == 0) {
      delete _sources[i];
      for (uint8_t j = i; j < _count - 1; j++) {
        _sources[j] = _sources[j + 1];
      }
      _sources[_count - 1] = nullptr;
      _count--;
      return true;
    }
  }
  return false;
}

inline Source* Detector::findSource(const char* name) {
  for (uint8_t i = 0; i < _count; i++) {
    if (strcmp(_sources[i]->getName(), name) == 0)
      return _sources[i];
    }
  return nullptr;
}

inline Source* Detector::getSource(uint8_t index) {
    return (index < _count) ? _sources[index] : nullptr;
}

inline bool Detector::configureSource(const char* name, const SourceConfig& config) {
  Source* source = findSource(name);
  if (source == nullptr) return false;
  source->setConfig(config);
  return true;
}

inline bool Detector::addSample(const char* name, float sample) {
  if (!_enabled) return false;
  Source* source = findSource(name);
  if (source == nullptr) return false;
  source->addSample(sample);
  return true;
}

inline void Detector::setEnabled(bool enabled) {
  _enabled = enabled;
}

inline bool Detector::isEnabled() const {
  return _enabled;
}

inline bool Detector::publishSource(const char* name, JsonDocument& document) {
  Source* source = findSource(name);
  if (source == nullptr) return false;
  source->publish(document);
  return true;
}

inline void Detector::setListener(DetectorListener* listener){
  _listener = listener;
}

inline void Detector::onSourceEvent(SourceEvent& event) {
  if (_listener == nullptr) return;
  DEBUG_PRINT(DEBUG_DETECTOR, F("Source event! Flag: "));
  DEBUG_PRINTLN(DEBUG_DETECTOR, event.source->getName());
  _listener->onFlag(event);
}
