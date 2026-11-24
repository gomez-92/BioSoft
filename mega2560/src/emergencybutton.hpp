#pragma once
#include <Arduino.h>

class EmergencyButtonListener {
  public:
    virtual void onEmergencyButtonPressed() = 0;
};

class EmergencyButton {
  public:
    // Tipo de contacto del pulsador, que es lo que fija que nivel significa
    // "presionado":
    //   NormallyOpen   -- en reposo abierto, cierra al apretar.
    //   NormallyClosed -- en reposo cerrado, abre al apretar. Es el que esta
    //                     cableado en el gabinete (banco 2026-11-24: el pin 28
    //                     leia LOW en reposo y HIGH apretado). Ademas es el
    //                     contacto seguro: un cable cortado o un borne flojo
    //                     se lee como pulsador apretado y corta la corrida.
    // Equivocarlo no da error: el pulsador corta al SOLTARLO en vez de al
    // apretarlo, y la corriente (SCT013) no se mide nunca en reposo.
    enum class Contact : uint8_t { NormallyOpen, NormallyClosed };

    explicit EmergencyButton(uint8_t pin, Contact contact = Contact::NormallyOpen,
                             bool useInternalPullup = true, unsigned long debounceMs = 30);
    void begin();
    void update();
    void setListener(EmergencyButtonListener* listener);
    // Nivel crudo, sin debounce ni flanco ni callback: lo usan las esperas
    // largas para abandonar lo que hacen. No consume el flanco que update()
    // despues convierte en onEmergencyButtonPressed().
    bool isPressed() const { return _readPressed(); }

  private:
    uint8_t _pin;
    Contact _contact;
    bool _useInternalPullup;
    unsigned long _debounceMs;
    bool _rawPressed;          // ultima lectura cruda
    unsigned long _rawSince;   // desde cuando la lectura cruda no cambia
    bool _stablePressed;       // estado aceptado (estable >= _debounceMs)
    EmergencyButtonListener* _listener;
    bool _readPressed() const;
};

inline EmergencyButton::EmergencyButton(uint8_t pin, Contact contact, bool useInternalPullup, unsigned long debounceMs)
  : _pin(pin), _contact(contact), _useInternalPullup(useInternalPullup), _debounceMs(debounceMs),
    _rawPressed(false), _rawSince(0), _stablePressed(false), _listener(nullptr) {}

inline void EmergencyButton::begin() {
  pinMode(_pin, _useInternalPullup ? INPUT_PULLUP : INPUT);
  _rawPressed = _stablePressed = _readPressed();
  _rawSince = millis();
}

// Con pull-up interno el contacto va a GND: un NA apretado da LOW, un NC
// apretado (contacto abierto) deja subir el pin a HIGH. Sin pull-up interno
// se asume la logica inversa (el contacto lleva el pin a VCC).
inline bool EmergencyButton::_readPressed() const {
  int level = digitalRead(_pin);
  int closedLevel = _useInternalPullup ? LOW : HIGH;   // contacto cerrado
  bool contactClosed = (level == closedLevel);
  return _contact == Contact::NormallyOpen ? contactClosed : !contactClosed;
}

inline void EmergencyButton::update() {
  bool raw = _readPressed();
  unsigned long now = millis();

  // Antirrebote por NIVEL ESTABLE: un cambio se acepta recien cuando la
  // lectura se sostiene _debounceMs sin cambiar. El anterior solo espaciaba
  // los flancos y dejaba pasar el rebote al SOLTAR (el contacto NC rebota al
  // volver a cerrar y cada rebote leia "apretado"): banco 2026-11-24, 5
  // disparos para 2 pulsaciones. Cuesta _debounceMs de latencia al apretar.
  if (raw != _rawPressed) {
    _rawPressed = raw;
    _rawSince = now;
    return;
  }
  if (raw == _stablePressed || now - _rawSince < _debounceMs) return;

  _stablePressed = raw;
  if (raw && _listener != nullptr) {
    _listener->onEmergencyButtonPressed();
  }
}

inline void EmergencyButton::setListener(EmergencyButtonListener* listener) {
  _listener = listener;
}
