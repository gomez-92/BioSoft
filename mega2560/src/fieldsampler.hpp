#pragma once

// Valor eficaz (RMS) del campo magnetico a la frecuencia de excitacion.
//
// Logica pura, sin Arduino.h: el bus real (MLX90393) entra por IFieldBus, asi
// que el muestreo, la ventana y el calculo se prueban en host con un reloj y
// un sensor simulados (test_fieldsampler).
//
// Que mide: la amplitud de la FUNDAMENTAL a la frecuencia f del experimento,
// por eje, y devuelve sqrt(sum_ejes A^2 / 2). Es el valor eficaz del campo
// alterno que producen las bobinas. El campo continuo (ambiente, imanes,
// tierra) cae solo -- es el termino constante del ajuste -- y por eso no hace
// falta tara. Lo que no esta a la frecuencia f (ruido, rizado del PWM
// aliasado) no suma.
//
// Como: el MLX90393 tarda ~1.3 ms en convertir y el I2C ~1.3 ms mas, asi que a
// 100 Hz el bus da ~330 muestras/s: 3 por periodo, no 10. Tres muestras por
// periodo es el minimo (3 incognitas: a, b, c), sin un solo grado de libertad
// de sobra, y ademas alinea el 2do armonico con la fundamental. Por eso las
// muestras se agrupan de a N = 10 y NO se espacian T/K: se toman cada
// (m + 0.382) T -- la razon aurea --, de modo que el grupo abarca varios
// periodos (~4 a 100 Hz) y las fases quedan repartidas sin repetirse. Quedan
// 7 grados de libertad y los armonicos 2..7 no caen sobre la fundamental. Es el
// mismo numero de muestras por segundo que antes; se paga con grupos de ~40 ms
// en vez de ~10 ms, que a 2 s de ventana siguen siendo decenas.
// Si T/10 ya alcanza para el bus (hasta ~33 Hz) se muestrea uniforme: 10
// muestras en exactamente un periodo.
// A cada grupo se le ajusta por minimos cuadrados  y = a cos(wt) + b sin(wt) + c
// con el INSTANTE REAL de cada muestra (micros() tomado al disparar). Por eso
// el jitter del loop() no ensucia la medicion: cada muestra entra con su fase
// verdadera, no con la que "deberia" haber tenido. La amplitud del grupo es
// sqrt(a^2 + b^2); la ventana promedia la potencia de todos los grupos que caen
// en W y devuelve su raiz.
//
// Un grupo dura unos pocos periodos, nunca la ventana entera: un error de
// frecuencia (cuarzo del Mega, cuantizacion del AD9833) corre las fases como
// mucho el error relativo por los periodos del grupo (~1e-4 x 4 = 0.04 %), y no
// se acumula durante los 2 s. Ajustar toda la ventana de una vez si lo
// acumularia, y mediria de menos.
//
// Se descarta (y se rearma el grupo) lo que no es confiable: una muestra que
// llega mas de 1.5 intervalos despues de la anterior (loop frenado), un fallo
// del bus, o un grupo mal condicionado (fases amontonadas).

#include <stdint.h>
#include <math.h>

class IFieldBus {
public:
    virtual ~IFieldBus() = default;
    // Reloj en microsegundos, con vuelta a 0 (como micros()).
    virtual uint32_t nowUs() const = 0;
    // Dispara una conversion de los tres ejes. false si el bus fallo.
    virtual bool trigger() = 0;
    // Lee el resultado (uT). Hay que llamarlo despues de conversionUs().
    virtual bool fetch(float& x, float& y, float& z) = 0;
    // Tiempo de conversion del sensor con la configuracion actual.
    virtual uint32_t conversionUs() const = 0;
    // Costo en bus de un trigger + un fetch (para dimensionar el intervalo).
    virtual uint32_t overheadUs() const = 0;
};

namespace FieldSampling {
    constexpr uint8_t GroupSize = 10;        // muestras por grupo (3 incognitas -> 7 gdl)
    constexpr float PhiLow = 0.381966f;      // razon aurea: reparte las fases sin repetirlas
    constexpr float PhiHigh = 0.618034f;     // su espejo
    constexpr float MinFreqHz = 0.5f;
    constexpr float MaxFreqHz = 1000.0f;
    constexpr uint8_t MaxConsecutiveFailures = 5;
    constexpr float MinNormalizedDet = 0.02f;   // ideal (uniforme) = 0.25
    constexpr uint32_t PollMarginUs = 300;      // espera extra antes de leer
    // Holgura del intervalo entre disparos para el jitter del loop(): poll() solo
    // corre una vez por vuelta, asi que disparar y leer llega unos cientos de us
    // tarde. Si la grilla fuera mas ajustada que eso, el intervalo REAL se iria
    // corriendo a ~0.5 T y el 2do armonico volveria a caer sobre la fundamental.
    constexpr uint32_t SlotMarginUs = 1000;

    // Mismo calculo que robtillaart/AD9833::setFrequency (con redondeo, el
    // default de la libreria): el registro es de 28 bits sobre 25 MHz, o sea
    // pasos de 0.093 Hz -- 4.7 % de error posible a 1 Hz, que a la fase le
    // importa. Se muestrea contra esta, no contra la pedida.
    inline float ad9833ActualHz(float requestedHz) {
        const float factor = 268435456.0f / 25000000.0f;
        float reg = roundf(requestedHz * factor);
        if (reg < 1.0f) reg = 1.0f;
        return reg / factor;
    }
}

struct SamplingPlan {
    bool ok = false;
    uint8_t groupSize = 0;        // N
    uint32_t spacingUs = 0;       // Ts entre disparos
    uint16_t extraPeriods = 0;    // m: periodos enteros entre disparos (0 = lo normal)
    bool uniform = false;         // true: N muestras en un periodo exacto (T/N)
    uint32_t spanUs = 0;          // duracion del grupo: (N - 1) Ts
};

// Elige N y Ts para la frecuencia f dado el intervalo minimo que el sensor
// aguanta.
//  - Si 10 muestras entran en un periodo (T/10 >= minimo): uniforme, N = 10.
//  - Si no, N = 10 con Ts = (m + phi) T, phi = 0.382 o 0.618 (razon aurea y su
//    espejo), el menor Ts >= minimo. Ts nunca es T/k, asi que las fases del
//    grupo no se repiten ni se alinean con los armonicos bajos.
inline SamplingPlan planSampling(float freqHz, uint32_t minSpacingUs) {
    SamplingPlan plan;
    if (!(freqHz >= FieldSampling::MinFreqHz) || freqHz > FieldSampling::MaxFreqHz)
        return plan;
    float periodUs = 1000000.0f / freqHz;
    const uint8_t n = FieldSampling::GroupSize;

    plan.ok = true;
    plan.groupSize = n;
    float uniformSpacing = periodUs / (float)n;
    if (uniformSpacing >= (float)minSpacingUs) {
        plan.uniform = true;
        plan.extraPeriods = 0;
        plan.spacingUs = (uint32_t)(uniformSpacing + 0.5f);
    } else {
        float r = (float)minSpacingUs / periodUs;
        float m = floorf(r);
        float frac = r - m;
        float phi;
        if (frac <= FieldSampling::PhiLow) {
            phi = FieldSampling::PhiLow;
        } else if (frac <= FieldSampling::PhiHigh) {
            phi = FieldSampling::PhiHigh;
        } else {
            phi = FieldSampling::PhiLow;
            m += 1.0f;
        }
        plan.extraPeriods = (uint16_t)m;
        plan.spacingUs = (uint32_t)((m + phi) * periodUs + 0.5f);
    }
    plan.spanUs = (uint32_t)(n - 1) * plan.spacingUs;
    return plan;
}

class FieldSampler {
private:
    enum class Phase : uint8_t { WaitSlot, Converting };

    IFieldBus& _bus;

    bool _running = false;
    bool _healthy = true;
    uint8_t _failures = 0;

    float _freqHz = 0;
    SamplingPlan _plan;
    uint32_t _windowUs = 0;
    uint32_t _convWaitUs = 0;
    uint32_t _gapMaxUs = 0;

    Phase _phase = Phase::WaitSlot;
    uint32_t _nextTriggerUs = 0;
    uint32_t _triggeredAtUs = 0;

    // Grupo en curso. Las sumas son del ajuste por minimos cuadrados; los
    // valores se guardan restando la primera muestra del grupo para que el
    // continuo (hasta cientos de uT) no coma la precision de un float.
    uint8_t _k = 0;
    uint32_t _groupStartUs = 0;
    uint32_t _lastSampleUs = 0;
    float _ref[3] = {0, 0, 0};
    float _scc = 0, _scs = 0, _sss = 0, _sc = 0, _ss = 0;
    float _sy[3] = {0, 0, 0}, _syc[3] = {0, 0, 0}, _sys[3] = {0, 0, 0};

    // Ventana en curso y ultimo resultado.
    uint32_t _windowStartUs = 0;
    float _powerSum = 0;
    uint16_t _groups = 0;
    float _lastRmsUt = 0;
    uint16_t _lastGroups = 0;
    uint16_t _discarded = 0;
    uint16_t _lastDiscarded = 0;

    static bool _due(uint32_t now, uint32_t target) { return (int32_t)(now - target) >= 0; }

    void _resetGroup() {
        _k = 0;
        _scc = _scs = _sss = _sc = _ss = 0;
        for (uint8_t i = 0; i < 3; i++) _sy[i] = _syc[i] = _sys[i] = 0;
    }

    void _fail() {
        _resetGroup();
        if (_failures < 255) _failures++;
        if (_failures >= FieldSampling::MaxConsecutiveFailures) _healthy = false;
    }

    void _discardGroup() {
        if (_k > 0) _discarded++;
        _resetGroup();
    }

    // Cierra el grupo: resuelve el ajuste y suma la potencia de los 3 ejes.
    // false si el grupo no sirve (muestras amontonadas).
    bool _finishGroup() {
        float n = (float)_k;
        float A = _scc, B = _scs, C = _sc, D = _sss, E = _ss, F = n;
        float i00 = D * F - E * E;
        float i01 = C * E - B * F;
        float i02 = B * E - C * D;
        float i11 = A * F - C * C;
        float i12 = B * C - A * E;
        float i22 = A * D - B * B;
        float det = A * i00 + B * i01 + C * i02;
        (void)i22;
        if (!(det / (n * n * n) >= FieldSampling::MinNormalizedDet)) return false;

        float power = 0;
        for (uint8_t axis = 0; axis < 3; axis++) {
            float r0 = _syc[axis], r1 = _sys[axis];
            float r2 = _sy[axis];
            float a = (i00 * r0 + i01 * r1 + i02 * r2) / det;
            float b = (i01 * r0 + i11 * r1 + i12 * r2) / det;
            power += 0.5f * (a * a + b * b);
        }
        _powerSum += power;
        _groups++;
        return true;
    }

    // true cuando esta muestra cerro una ventana con resultado nuevo.
    bool _addSample(uint32_t t, float x, float y, float z) {
        if (_k > 0 && (uint32_t)(t - _lastSampleUs) > _gapMaxUs)
            _discardGroup();

        float v[3] = {x, y, z};
        if (_k == 0) {
            _groupStartUs = t;
            for (uint8_t i = 0; i < 3; i++) _ref[i] = v[i];
        }

        float cycles = _freqHz * (float)(uint32_t)(t - _groupStartUs) * 1e-6f;
        float phase = 6.2831853f * cycles;
        float c = cosf(phase), s = sinf(phase);
        _scc += c * c;
        _scs += c * s;
        _sss += s * s;
        _sc += c;
        _ss += s;
        for (uint8_t i = 0; i < 3; i++) {
            float d = v[i] - _ref[i];
            _sy[i] += d;
            _syc[i] += d * c;
            _sys[i] += d * s;
        }
        _k++;
        _lastSampleUs = t;

        if (_k < _plan.groupSize) return false;

        bool used = _finishGroup();
        if (!used) _discarded++;
        _resetGroup();

        if (_groups == 0 || !_due(t, _windowStartUs + _windowUs)) return false;

        _lastRmsUt = sqrtf(_powerSum / (float)_groups);
        _lastGroups = _groups;
        _lastDiscarded = _discarded;
        _powerSum = 0;
        _groups = 0;
        _discarded = 0;
        _windowStartUs = t;
        return true;
    }

public:
    explicit FieldSampler(IFieldBus& bus) : _bus(bus) {}

    // freqHz: la que REALMENTE sale del generador (ad9833ActualHz).
    // windowMs: cada cuanto se quiere un valor nuevo.
    bool begin(float freqHz, uint32_t windowMs) {
        uint32_t conv = _bus.conversionUs();
        // 10 % de margen sobre el tiempo de conversion + lo que cuesta el bus
        // + holgura, o el intervalo no alcanzaria para disparar y leer.
        uint32_t minSpacing = conv + conv / 10 + _bus.overheadUs() + FieldSampling::SlotMarginUs;
        _plan = planSampling(freqHz, minSpacing);
        if (!_plan.ok || windowMs == 0) {
            _running = false;
            return false;
        }
        _freqHz = freqHz;
        _windowUs = windowMs * 1000UL;
        _convWaitUs = conv + conv / 10 + FieldSampling::PollMarginUs;
        // Un hueco mayor que esto es un loop() frenado, no jitter. Las muestras
        // llevan su instante real, asi que un hueco moderado no ensucia el ajuste.
        uint32_t gap = _plan.spacingUs + _plan.spacingUs / 2;
        if (gap < 2 * minSpacing) gap = 2 * minSpacing;
        _gapMaxUs = gap;

        _resetGroup();
        _powerSum = 0;
        _groups = 0;
        _discarded = 0;
        _lastRmsUt = 0;
        _failures = 0;
        _healthy = true;
        _phase = Phase::WaitSlot;
        _nextTriggerUs = _bus.nowUs();
        _windowStartUs = _nextTriggerUs;
        _running = true;
        return true;
    }

    void stop() { _running = false; _resetGroup(); }

    bool running() const { return _running; }
    bool healthy() const { return _healthy; }
    float rmsUt() const { return _lastRmsUt; }
    uint16_t lastGroups() const { return _lastGroups; }
    uint16_t lastDiscarded() const { return _lastDiscarded; }
    const SamplingPlan& plan() const { return _plan; }

    // Un paso acotado (a lo sumo una transaccion de bus). true = hay un valor
    // de ventana nuevo en rmsUt().
    bool poll() {
        if (!_running) return false;
        uint32_t now = _bus.nowUs();

        if (_phase == Phase::WaitSlot) {
            if (!_due(now, _nextTriggerUs)) return false;
            if (!_bus.trigger()) {
                _fail();
                _advanceSlot(_bus.nowUs());
                return false;
            }
            _triggeredAtUs = _bus.nowUs();
            _phase = Phase::Converting;
            return false;
        }

        if ((uint32_t)(now - _triggeredAtUs) < _convWaitUs) return false;

        float x = 0, y = 0, z = 0;
        bool ok = _bus.fetch(x, y, z);
        _phase = Phase::WaitSlot;
        uint32_t after = _bus.nowUs();
        _advanceSlot(after);
        if (!ok) {
            _fail();
            return false;
        }
        _failures = 0;
        _healthy = true;
        return _addSample(_triggeredAtUs, x, y, z);
    }

private:
    // La grilla de disparos es absoluta (no se corre con el jitter), salvo que
    // se haya perdido mas de un intervalo entero: ahi se re-ancla en vez de
    // disparar en rafaga para "recuperar".
    void _advanceSlot(uint32_t now) {
        _nextTriggerUs += _plan.spacingUs;
        if ((int32_t)(now - _nextTriggerUs) > (int32_t)_plan.spacingUs)
            _nextTriggerUs = now;
    }
};
