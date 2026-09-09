# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

BioSoft is firmware for a two-board embedded system that exposes lab animals to a
controlled, low-frequency electromagnetic field for a bioengineering research
experiment. It is a PlatformIO migration of two former Arduino IDE sketches
(`SoftEsp32`, `SoftMega2560`).

Two independent PlatformIO projects, one per board, connected by a serial link:

- **`esp32/`** — touchscreen HMI (LVGL + TFT_eSPI). Shows system state, lets the
  operator configure experiment parameters, and publishes telemetry over MQTT for
  remote monitoring.
- **`mega2560/`** — physical control. Drives the coil (signal generator + PWM),
  reads field/current/temperature sensors, and decides when to cut the experiment
  (time elapsed, danger detected, or loss of scientific rigor).

There is no shared source directory — `esp32/src/seriallink.hpp` and
`mega2560/src/seriallink.hpp` (and `commands.hpp`, `timer.hpp`, `debugconfig.hpp`)
are **meant to be duplicated, byte-for-byte, in each project**. When changing the
serial protocol, edit both copies and keep them in sync manually. In practice
`seriallink.hpp` has already drifted — the ESP32 copy has extra
`Serial.print`-era logging (ping/pong, connection-state changes) the Mega copy
never had; the framing/CRC/queue logic itself is still identical. Don't assume
the two copies are interchangeable without diffing first.

## Commands

Each folder is built independently with PlatformIO (`pio`, i.e. the PlatformIO
CLI — install via `pip install platformio` or use the VS Code extension).

```
cd esp32 && pio run       # build ESP32 firmware
cd esp32 && pio run -t upload
cd esp32 && pio device monitor

cd mega2560 && pio run    # build Mega2560 firmware
cd mega2560 && pio run -t upload
cd mega2560 && pio device monitor
```

Final verification is manual, on-device (serial monitor logs), since this is
embedded C++ interacting with real sensors and actuators. For the mega2560
project, header-only logic that doesn't need real hardware (detection rules,
state machines, protocol framing) can also be unit-tested on the host,
*before* flashing the board:

```
cd mega2560 && pio test -e native
```

This uses PlatformIO's `native` platform (host gcc/g++, not the AVR
toolchain) with the Unity test framework and the `ArduinoFake` library to
mock `Arduino.h`/`Serial`. See `mega2560/test/test_detector/` for the first
suite and `mega2560/test/support/arduino_fakes.hpp` for the shared
`silenceSerial()` helper — call it from `setUp()` in any suite that touches
code with `Serial.print` tracing (most of it), otherwise ArduinoFake's mock
throws on the first unstubbed call and crashes the test binary.
`test_build_src = no` in the `[env:native]` section keeps this test build
from pulling in the real `.ino` and hardware-facing managers — tests
`#include` the relevant `src/*.hpp` headers directly instead, since
everything is header-only.

### Credentials

`esp32/include/secrets.h` (WiFi + MQTT) is gitignored. Copy
`esp32/include/secrets.example.h` to `secrets.h` and fill in real values before
building — the ESP32 build will fail to compile without it.

## Architecture

### Serial protocol (`seriallink.hpp`)

Both boards talk over a hardware UART (`Serial2` on ESP32 pins 22/27 ↔ `Serial1`
on the Mega) using a custom framed protocol:

```
STX(1) LEN_LO(1) LEN_HI(1) JSON(LEN bytes) CRC8(1) ETX(1)
```

Each frame carries a JSON payload with a `command` string (see `commands.hpp` —
`ping`/`pong`/`start`/`stop`/`reset`/`state_data`/`result_data`/`temp_data`/
`cem_data`/`current_data`/`ack`) and a `params` object. `SerialLink` owns a small
TX queue (drained on a fixed interval) and an RX byte-state-machine parser;
connection liveness is tracked via periodic ping/pong, exposed through
`isConnected()` and the `CommandListener` `onSerialConnected`/`onSerialDisconnected`
callbacks. Consumers implement `CommandListener` and call `sendCommand(...)` /
`getParam(...)`.

### mega2560 — control loop

`Engine` (`mega2560/src/engine.hpp`) is the central orchestrator, composed via
constructor-injected references to all managers/drivers (`Timer`, `SerialLink`,
`MagnetometerManager`, `ThermometerManager`, `CurrentSensorsManager`,
`CoilExcitation`, `CoilChannels`, `FieldController`, `MainPowerSwitch`, `Detector`).
It implements multiple listener interfaces (`EngineStateListener`,
`RuntimeStateListener`, `TimerListener`, `CommandListener`, sensor sample
listeners, `DetectorListener`) and reacts to events rather than polling — sensor
samples, timer ticks, and incoming serial commands all arrive as callbacks into
`Engine::onXxx()`.

Key collaborators:
- `EngineState` / `RuntimeState` — state machines (Ready/Running/Finished, target
  params, health, result) that fire listener callbacks on transitions, which
  `Engine` uses to add/remove periodic `Timer` tasks (see `tasks.hpp` /
  `intervals.hpp`).
- `FieldController` — closed-loop control (setpoint from `start` command) driving
  `PwmDriver` based on live magnetometer readings. Its gains are **not tuned
  against real coils yet** — `docs/protocolo-calibracion-intensidad.md` is the
  bench procedure that does it. Two things there are worth knowing before
  touching this class: `kp` cannot be read off the settled duty (at equilibrium
  the error is zero, so every `kp` lands on the same duty — it only changes how
  fast and whether it oscillates); it is computed as `α/K` from the plant gain
  `K` (mT per unit of duty) measured **open-loop**, with α ≈ 0.1–0.3, since
  `kp·error` is a duty step and so carries units of duty/mT. And because there
  is exactly one magnetometer — by design, not by omission — per-coil intensity
  can never be four independent loops; it is one loop plus a per-coil scaling
  factor measured once at calibration.
  Build its `Config` with **`makeFieldControllerConfig(intervalMs)`**, never by
  filling `sampleTime` by hand: that field must match the real cadence of
  `update()` calls or the integral term is scaled wrong, and it used to be a
  literal `0.2f` against a real 500 ms interval — silently off by more than
  half, because the two constants lived in different files and neither
  referenced the other. The `.ino` now derives it from
  `Intervals::MeasureMagneticField`, so changing the measurement cadence retunes
  the controller on its own. The `0.2f` default survives only for `Config`s
  built standalone in tests. The tuning parameters (`kp`, `maxStep`, `deadBand`)
  are deliberately *not* derived — they are set in `SoftMega2560.ino`, which is
  the single place the SD config will override once it exists, and they are
  still uncalibrated header defaults rather than measured values.
- `CoilExcitation` (`coilexcitation.hpp`) — the only place that
  decides *what signal the coils get*. `Engine` no longer touches
  `SignalGenerator` directly: `_start()`/`_stop()` go through
  `start(freq)`/`stop()`, and the experiment mode (`FieldMode::X` / `Null`) is a
  single digital pin driving the analog mux. **`setMode()` returns false and
  changes nothing while running** — flipping a live experiment between field and
  null field changes the experimental condition of animals already being exposed,
  which invalidates the run; the mode is chosen before starting. It arrives as an
  *optional* `mode` param on `start` ("null" ⇒ `Null`, anything else or absent ⇒
  `X`), so an older ESP32 build keeps working; the default is `X` on purpose,
  since a silently-null experiment would look exactly like a normal one. The
  ESP32 does not send it yet — the Configuración selector is its own card.
  `ISignalGenerator` is declared here (same pattern as `IMagnetometer` living in
  `magnetometermanager.hpp`) so it can be host-tested without pulling in
  `AD9833.h`/SPI; `SignalGenerator` implements it.
  Deliberately **not** named `...Manager`: the `*Manager` classes here
  (`MagnetometerManager`, `ThermometerManager`, `CurrentSensorsManager`) are all
  containers of N homogeneous things with `addX`/`xAll` iteration, and this is
  not one — it owns a single generator plus a pin, and earns its place by
  exposing a domain concept (`FieldMode`) and guarding an invariant, not by
  managing a collection. It briefly *was* `CoilExcitationManager`, back when the
  design had two AD9833s to orchestrate; the name outlived that design and was
  actively misleading, so it went. The same reasoning retired `RelayManager`,
  now `MainPowerSwitch` (see below).
- `CoilChannel` / `CoilChannels` (`coilchannel.hpp`) — one channel per coil,
  owning the two signals that are 1:1 with a coil: its intensity PWM and its
  enable line. The enable is a bare `pinMode`+`digitalWrite` with **no
  blanking** — the other end is a digital input to a solid-state stage, not a
  mechanical contact, so reusing `Relay` would only add latency for a
  protection that doesn't apply here.
  **The loop stays single.** With one magnetometer there is no per-coil
  feedback, so "individual intensity" is one `FieldController` producing a
  *common* duty that each channel scales by its own calibration factor
  (`duty_channel = duty_common × factor`). `Engine` calls `writeAll(duty)` once;
  the per-coil differences live in the factors, measured on the bench (see
  `docs/protocolo-calibracion-intensidad.md`). A factor of `1.0` — the default —
  means **not yet measured**, not "measured and found equal".
  When a factor pushes duty past 1.0 the channel clamps *and logs it*: that coil
  is not reaching the setpoint and will run weaker than the others, which
  nothing else would reveal. During calibration that log line is the symptom
  you're looking for.
  **Phase groups are alternating, not contiguous pairs**: coils 1 and 3 take the
  direct signal (fixed group), coils 2 and 4 take the mux output (invertible
  group). That split is wiring, not software, but it is what makes the default
  registration in `SoftMega2560.ino` — only BOB1 and BOB2 — useful: two
  consecutive coils land in *different* groups, so both experiment modes can be
  exercised with two coils mounted. Grouped as 1-2 / 3-4 instead, those same two
  coils would share a group and the null field would not be verifiable until all
  four were built. Coils 3 and 4 are declared and ready; their `addChannel` calls
  are commented out, the same bring-up pattern used for the sensors.
  `CoilChannels` **is** a legitimate container of N, unlike the `RelayManager`
  that became `MainPowerSwitch`: there N was fixed at 1 by design, here there
  are 4 real coils and every experiment-level operation is collective (enable
  all, cut all, hand out the common duty). Same shape as `MagnetometerManager`.
- `MainPowerSwitch` (`mainpowerswitch.hpp`) — the general cutoff for the whole
  power stage (the remote 3-pin GND/VDD/enable connector), distinct from the
  per-coil enable in `CoilChannel`. It wraps **one** `Relay`;
  `Engine` calls `enable()`/`disable()` and no longer reasons about open and
  closed contacts, which are a property of the relay and not of the experiment.
  It replaced `RelayManager` (an `addRelay`/`beginAll`/`openAll` container for up
  to 10 relays) for the reason above: by design there is exactly one relay, so a
  manager-of-N at a fixed N=1 was indirection without a concept. `Relay` itself
  is unchanged — its 200 ms blanking still makes sense for a real mechanical
  relay, **and it still applies underneath**: an `enable()`/`disable()` requested
  within 200 ms of the previous change is a silent no-op with no return value, so
  `isEnabled()` is the only way to confirm what actually happened. `begin()` is
  called once, from `Engine::begin()` — the `.ino` used to call it too, and that
  duplicate is gone.
- `Detector` — evaluates per-source (`CEM1`, `TEMP1`, ...) rules (critical
  threshold, streak, frequency) from sensor sample history and raises `onFlag`
  events. Overall health scoring used to live here (`Engine::_evaluateHealthData()`)
  but was removed in a refactor — it's now computed on the ESP32 from the flags it
  receives, see the esp32 section below.
- Remote commands `start`/`stop`/`reset` drive `Engine::_start()`/`_stop()`/
  `_reset()`, each acknowledged back to the ESP32 via an `ack` command.
- `Engine::_finish(reason, description)` is the single place an experiment ends —
  called from the 3 spots that used to call `_stop(); _reset();` directly: the
  `Tasks::Finish` timer (`reason="completed"`, duration elapsed), `Commands::Stop`
  (`reason="stopped"`, operator hit Detener), and `Detector::onFlag` when
  `event.count >= event.limit` (`reason="critical"`, description built from
  `source->getName()`/type/count/limit — e.g. `"TEMP1: limite de alertas critical
  alcanzado (4/4)"`). It calls `_runtimeState.setResult(reason, description)`
  *before* `_stop()`/`_reset()` — `_reset()` wipes `RuntimeState`'s result and
  `_stop()`+`_reset()` are synchronous, so `Finished` is never observable over
  `state_data` (see `onFinish()`'s comment). The only way `result_data` ever
  reaches the ESP32 with real content is the synchronous send triggered by
  `RuntimeStateListener::onResult()` the instant `setResult()` runs — same
  fire-once reliability as `flag_data`, no ack/retry.
- `Engine::_start()` sets `_isSettlingTime = true` and `onStart()` schedules the
  `Tasks::SettlingTime` timer (`Intervals::SettlingTime`, 10s) to clear it; while
  set, sensor samples are recorded but withheld from `_detector.addSample(...)`
  (see `onThermometerSample`/`onMagnetometerSample`/`onCurrentSensorSample`), so
  no flags can fire during that initial window. If this timer task isn't
  scheduled, the flag stays true forever and the Detector silently never
  evaluates anything for the rest of the run — always verify `onStart()` still
  adds `Tasks::SettlingTime` after touching that code path.

- `DetectorConfigBuilder` (`detectorconfigbuilder.hpp`) builds the `SourceConfig`
  Engine applies to the Detector on `start`, extracted out of
  `Engine::onCommand`/`Start` (which used to build it inline across ~70 lines).
  Engine still validates the incoming JSON params itself; only the "turn
  validated values into detector rules" part moved out. CEM1's
  critical/streak/frequency thresholds are **test placeholders** (same numbers
  as TEMP1's), not calibrated against real field.
  It is deliberately split in two halves, because a `SourceConfig` is now
  assembled from two different sources: `defaultConfig()` gives the compiled
  `bufferSize` + 3 rules (which the SD config overwrites and Engine keeps as a
  per-source *template* across runs), and `applyCemRanges()`/`applyTempRanges()`
  write the ranges from the operator's `start` params onto a **copy** of that
  template. Merging them back into one `build...()` would make every `start`
  overwrite the SD-configured rules with the compiled placeholders — silently.
  `test_detectorconfigbuilder` pins that, plus the section-7 validation
  (`isValidBufferSize`/`isValidRule`/`isValidCriticalMultiplier`). The contract
  is **reject whole, don't clamp**: a rule with any out-of-range field is
  discarded entirely and keeps its compiled default, since half a rule cuts (or
  fails to cut) an experiment on numbers nobody chose.
- `EmergencyButton` (`emergencybutton.hpp`) is the physical e-stop: edge-detected
  (not level), software-debounced, wired to pin 4. `Engine::onEmergencyButtonPressed()`
  only calls `_finish()` while `State::Running` — pressing it while idle is a
  no-op, so it doesn't push a bogus `result_data` (and bounce the ESP32 to
  Resultado) for an experiment that never ran.
- **`testMode` is gone** (was a 1-6 bring-up combo: `testmoderesolver.hpp`,
  `Engine::_applyTestMode()`, a dropdown on the Configuración screen, a `start`
  param). Its three axes moved into the SD config: sim-vs-real sensor and
  CEM1-watched-or-not are `detector.sources[CEM1].sensor`/`.enabled`, and
  "measure but don't drive the coils" is `control.enabled` (which is what
  `Engine::_controlLoopEnabled` now reads, defaulting to `true` so a board with
  no SD card still runs a full experiment). `Engine::_applySourceSettings()`
  replaced `_applyTestMode()` and keeps its ordering constraint: it must run
  *before* the `configureSource(...)` calls in `onCommand`'s `Start` handler,
  because `removeSource`/`addSource` recreate the `Source` unconfigured and
  `Source::addSample()` is a no-op while `_configured` is false — configure
  first and the config is silently dropped.
  CEM1 can still be fed by either `MagnetometerVoltageSim` or the real
  `MagnetometerMlx90393`; the sim reads a plain analog voltage on `A0` and maps
  it linearly to mT, and does **not** validate `FieldController` against
  anything physically meaningful.
- `MagnetometerManager::addMagnetometer()` only calls `magnetometer->begin()`
  if the magnetometer is not already `isValid()`. Needed because
  `_applySourceSettings()` re-runs `clearMagnetometers()`/`addMagnetometer()` on every
  `start`, and unconditionally calling `begin()` on `MagnetometerMlx90393`
  re-inits the Adafruit driver (full chip reset) on an already-working sensor
  seconds after its initial `begin()` in `setup()` — reproducible on bench as
  `begin_I2C` failing the *second* time (first always OK), with no actual
  wiring problem. A magnetometer that never connected, or that dropped mid-run,
  still gets its `begin()` retried since it's not valid.
- `MagnetometerMlx90393::update()` converts the Adafruit driver's raw reading
  (µT) to mT before storing it — the rest of the system (ESP32 targets, the
  sim, `FieldController`, `DetectorConfigBuilder`) works in mT, so without this
  conversion the real sensor reports ~1000x what everything else expects. Logs
  both units on every read (`DEBUG_MAGNETOMETER_MLX90393`) since the mT value
  can round to 0.0000 near ambient field, and the raw µT figure is what
  distinguishes "sensor isn't reading" from "ambient field really is that
  small."
- CEM1 is **not registered** as a Detector source by default, `TEMP1` is —
  that pair is the compiled default, set in `SoftMega2560.ino`'s `setup()` and
  in `Engine`'s constructor, and matches `docs/config.example.json`. The reason
  is `A0`: floating, the sim's noisy reading falls outside the critical band
  and hits the 3-consecutive-reading streak within ~1.5s of the settling window
  ending, firing a false `"critical"` flag that cuts the experiment early.
  Turning CEM1 back on no longer means editing the `.ino` — it's
  `detector.sources[CEM1].enabled` in the SD config, applied at the next
  `start`. (Before this, that decision briefly lived in `testMode`, which made
  the `//detector.addSource("CEM1")` line in the `.ino` dead code; both are
  gone now.)
- `FieldController`'s proportional term (`fieldcontroller.hpp`) is now a real
  continuous P step — `kp * error` clamped to `maxStep` — replacing the old
  coarse/fine two-speed jump (audited as MOD-011 in Trello). The old version
  moved `_output` by the same fixed `fineStep` whether the error was 0.011 or
  0.099, which felt like the duty "bouncing" at the slightest deviation instead
  of easing in; `Config::kp`/`maxStep` replace the old
  `Config::coarseStep`/`fineStep` fields.
- **I2C hang hazard**: `ThermometerDS18B20::update()`
  (`dallasThermometer.requestTemperatures()`) always blocks ~750ms per call
  regardless of whether a sensor is on the bus, and
  `Adafruit_ADS1115::readADC_Differential_*()` **hangs `loop()` indefinitely**
  waiting for an I2C ACK that never comes when the ADS1115 isn't wired — it has
  no timeout. Confirmed on-device: the debug log cut off mid-tick right after
  `"Iniciando: MEASURE_CURRENT"`, with `Tasks::Finish` silently never firing
  because the MCU was frozen well before the configured duration elapsed.
  **The current-sensor half of this is now guarded.**
  `CurrentSensorSct013::begin()` probes the module's address first — a bare
  address transaction (`Wire.beginTransmission`/`endTransmission`), which
  returns a clean error when nobody answers — and only then calls
  `_ads.begin()`. If the chip isn't there the sensor stays disabled, says so in
  the log, and `update()` returns before touching the bus. That is what makes
  it safe to leave a channel on the second module (0x49) configured in the SD
  file while that module doesn't physically exist yet.
  The thermometer has no such guard, and `ThermometerManager::updateAll()` /
  `CurrentSensorsManager::updateAll()` are still safe no-ops with zero sensors
  registered, so for bring-up without a DS18B20 wired it's still better to not
  register it than to leave it registered "because it's probably fine".
- `PWM_PIN` is `44` (Timer5/OC5C), not `A0` — `A0`..`A15` have no hardware timer
  on the Mega2560, so `analogWrite()` there silently degrades to a binary
  `digitalWrite`, not real PWM. `PwmDriver`'s `frequency` constructor param is
  now actually applied via `applyFrequency()`, called from `enable()` and *not*
  the constructor — `pwmDriver` is a global object and its constructor runs
  before Arduino's `init()` configures the timers, which would silently
  overwrite anything touched earlier. It only snaps to the nearest of 5 fixed
  prescaler-derived frequencies, never an arbitrary value.
- **The Mega2560 has hardware PWM on 15 pins (2–13 and 44–46), not 3.** The
  "only 44/45/46" claim that circulated in Trello was a garbled merge of two
  separate facts: `A0`..`A15` have no timer (true), and `PwmDriver` used to
  write `TCCR5B` unconditionally (also true, now fixed). `PwmTiming::timerForPin()`
  (`pwmdriver.hpp`) covers the four 16-bit timers, which share one prescaler
  table: Timer1 (11,12), Timer3 (2,3,5), Timer4 (6,7,8), Timer5 (44,45,46).
  **Timer0 (4,13) and Timer2 (9,10) are excluded on purpose** — Timer0 clocks
  `millis()`/`delay()` (and every periodic `Timer` task with it), and Timer2 has
  7 prescalers instead of 5, so the shared table would map to a different
  divisor. Note the frequency is **per timer, not per pin**: the 3 pins of a
  timer share one prescaler and the last `enable()` wins, silently. So four coil
  channels fit without a DAC or multiplexing (44/45/46 + one of Timer4), but
  Timer3's pins are already taken by OneWire/relay/SPI-CS.
- `PwmTiming::frequencyFor()` divides by `2 * N * top`, and the factor of 2 is
  load-bearing: the Arduino core puts timers 1/3/4/5 in **8-bit phase-correct**
  PWM, where one period is `2*top` ticks. The original code omitted it, which
  reported double the real frequency and picked the wrong prescaler — asking for
  the 490 Hz default selected ÷256 (~122 Hz real) instead of the ÷64 the timer
  already had, quietly degrading a correct configuration. `test_pwmdriver` pins
  the frequency→prescaler table so this can't silently regress; the `TCCRnB`
  write itself is still not observable on host.
- The PWM frequency is now set explicitly to **3906 Hz** (prescaler 8) in
  `SoftMega2560.ino`, not left at the 490 Hz default — it is the output contract
  for coil excitation (`docs/coil-excitation.md`): the highest value reachable
  without changing the timer mode, leaving nearly two decades of headroom over
  the 10–50 Hz sine so the power stage can filter the PWM into a DC level.
  Intensity reaches the coil as the *amplitude* of a continuous sine (PWM → RC →
  DC level → gain), never by chopping the sine itself.
- The 180° phase inversion for the null-field control group is **analog** — an
  op-amp inverter plus a mux picking direct vs. inverted for coils 2 and 4, one
  GPIO — not a second AD9833 with a programmed phase. The reason is written up
  in `docs/adr/001-inversion-de-fase.md` and is worth knowing before anyone
  proposes the software route again: two AD9833 boards each carry their own
  25 MHz oscillator, and since output frequency is `fMCLK × FREQREG/2²⁸`, a few
  ppm of clock mismatch makes the *relative phase rotate continuously* — at
  50 Hz with the usual 50–100 ppm, a full turn every 200–400 s. The null field
  would hold at startup and be summing in phase minutes later, silently
  contaminating the control group (residual field goes as `2·sin(φ/2)`: 1° of
  error ⇒ 1.7%, 18° ⇒ 31%). The AD9833's phase register aligns channels *within
  one chip*, not across chips. Sharing one MCLK fixes it in principle but needs
  the second board's oscillator desoldered, 25 MHz routed between boards, and
  both phase accumulators reset with back-to-back SPI writes — at 50 Hz, 1 ms of
  skew between those two writes is 18° of error, so even a `DEBUG_PRINTLN`
  between them would matter. The analog inverter is exact by construction
  instead, which is the whole argument.
- **An ADS1115 has only 2 differential pairs** (AIN0-AIN1, AIN2-AIN3), not 4
  channels — `CurrentSensorSct013::readRaw()` reads differentially, and its
  `default` branch returns 0, i.e. a sensor that reads 0 A forever while
  looking healthy (`begin()` now rejects `channel > 1` outright). So the
  cabinet's 4 current channels need **two modules**: 0x48 and 0x49. Both are
  declared in `SoftMega2560.ino` along with 4 static `CurrentSensorSct013`
  slots (2 per module), even though the second module doesn't exist yet —
  adding it later is an SD-file edit, not a reflash. Each file entry is matched
  to its slot by `(address, channel)`, never by name, because a static object
  is bound to its ADS for life.
  The slots are static and not `new` on purpose:
  `CurrentSensorsManager::clearCurrentSensors()` doesn't delete, so
  re-instantiating them on every `start` would leak steadily inside 8 KB.
  Note `sampleRate` (and gain) are **per module, not per channel** — two
  sensors on one ADS share those registers and the last one configured wins,
  silently. Same shape of trap as PWM frequency being per timer, not per pin.
- Current (`SCT013-1`) is measured and sent to the ESP32 but is not, and was
  never meant to be, a Detector source — by design the only sources are `TEMP1`
  and `CEM1`. `Engine::onCurrentSensorSample` still calls
  `_detector.addSample("SCT013-1", ...)` but it's a no-op today (source never
  registered in `SoftMega2560.ino`).

Several code paths (`SetTarget`, `ConfigSource`) are present but commented out —
the protocol supports them but the ESP32 side does not yet drive them. Check
before assuming a command name is unused. Note that `Commands::ConfigSource`
does not actually exist: `Engine::_configureSource()` is commented out in
`engine.hpp`, but the command name was never declared in either copy of
`commands.hpp`, so reviving it means adding the constant to both, not just
uncommenting the handler. This is a *different* command from `config_source`
in the SD-config schema (`docs/config-schema.md` §7, for `detector.sources`,
still unimplemented) — don't conflate the two when that section gets built.
`Engine::_readRule()` *is* live code (not commented out) and already parses
the exact `{cooldown, maxEvents, threshold}` shape the
SD config schema uses for the Detector's rules. Per-flag telemetry (`flag_data`,
`Commands::OneFlagsData`) *is* wired end to end: the Mega's `Detector::onFlag`
sends one `flag_data` per event, and the ESP32 renders the last 3 into the
Running screen's Alertas tab (see below).

`config_intervals`/`config_control`/`config_coil` (`Commands::ConfigIntervals`
etc., both copies of `commands.hpp`) *are* live — they're the first tramo of
the SD-config schema that actually crosses the serial link, added on top of
the phase-1 work below. `Engine::onCommand` applies each one key-by-key
(`params["x"].is<T>()`, same idiom `Start` already uses) and **always acks
once the frame parses**, regardless of whether individual keys were valid —
an invalid/missing key is logged and leaves that one value unchanged, it
doesn't block the ack or the rest of the frame. `config_coil` identifies its
target by `name` via the new `CoilChannels::findByName()`, not by index (the
ack echoes `name` back too, since up to 4 `config_coil` frames can be in
flight for the ESP32 to disambiguate — see below). `FieldController` gained
`setKp`/`setMaxStep`/`setDeadBand` (+ getters) so `control` can be applied
after construction; `CoilChannel::setCalibrationFactor()`'s clamp widened
from "reject `<=0`" to the schema's `0.1`–`5.0` range. `enabled:false` on a
coil calls `disable()` — it does **not** remove the channel from
`CoilChannels` (channels are wired fixed in the `.ino`, there's no runtime
add/remove), so it's a weaker guarantee than the schema's "not registered"
wording, just with the same practical effect (no PWM, no field).
`mega2560/src/intervals.hpp` is `inline`/mutable now too (was `constexpr`),
mirroring the ESP32 side, so `config_intervals` has something to write into.

`config_source`/`config_rule` carry `detector.sources`, and they are split
that way — one header frame plus one frame per rule — because **a whole source
does not fit in a frame**. Beware the numbers in `seriallink.hpp`: the
`StaticJsonDocument<128>` in `TxMessage` and the `<256>` envelope constrain
nothing, since ArduinoJson 7 makes that class an elastic `JsonDocument` whose
`capacity()` just returns N (see the library's `compatibility.hpp`). The one
real limit is the `char json[MAX_JSON_SIZE]` (256) that `_sendFrame()`
serializes into, and **overflow is silent**: `serializeJson` truncates, the
CRC is computed over the already-truncated text, so the frame arrives with a
valid CRC and fewer keys, indistinguishable from one that never carried them.
`test_serialframes` (native) pins the budget of every `config_*` payload,
including a test asserting that a whole source in one frame does *not* fit, so
nobody merges them back. `config_rule`'s ack echoes `source` + `rule`, which
the ESP32 rejoins as `"<source>/<rule>"` to match its pending entry.
Unlike the other three, `config_source`/`config_rule` are **not applied on
receipt**: Engine stores them in a per-source template and applies them at the
next `start` (`_applySourceSettings()`), because `enabled` implies
`removeSource`/`addSource`, and doing that mid-experiment on a serial
reconnect would leave the Detector silently not watching that source for the
rest of the run.

### esp32 — HMI, connectivity, and screen state machine

`MySystem` (`esp32/src/mysystem.hpp`) is the equivalent orchestrator on this side,
implementing `CommandListener` (serial from the Mega), `TimerListener`,
`WiFiListener`, `BrokerListener` (MQTT), `ScreenManagerListener`, `IScreenListener`,
and `StateListener` (own app state, from `SystemData`).

- `ScreenManager` holds an array of `IScreen*` (Splash, Principal, Config,
  Running, Resultado, Busy — one controller class each, in
  `esp32/src/ui_*.c/.h` + matching `*controller` files), switches the active one
  via `show(ScreenType)`, and forwards `ScreenEvent`s (button taps, etc., tagged
  with a screen type and event name from `eventsname.hpp`) up to `MySystem`.
- `MySystem::_processState()` is the app-level state machine (`idle` → `ready` →
  `starting`/`stopping` → `running`, from `systemdata.hpp`'s `StateData`) that
  decides which screen should be showing and mirrors state received from the Mega
  over serial (`state_data` command) and from the local UI.
- `_sendStart()` builds the `start` command JSON from `SystemData::configuration`,
  resolving each setting through `ConfigurationOptions` lookup tables (intensity,
  frequency, duration, tolerance, temperature ranges) before sending it to the Mega.
- `SdStorage` (`sdstorage.hpp`) is a generic SD-card file read/write module
  (agnostic of content, per its own contract) for the ESP32-2432S028 (CYD)
  board's onboard SD slot — shares the TFT's SPI bus (pins 12/13/14) with its
  own CS on pin 5. `begin()` opens the SPI bus — the constructor only stores
  pins, so this global's constructor touches no hardware.
  **`readFile()` returns false when the content does not fit the buffer**, and
  that is the whole point: it used to truncate silently and return true, which
  for a config file is the worst possible failure. The JSON would parse-fail,
  the loader would fall back to compiled defaults, and the log would say
  "invalid JSON" while the actual cause was an undersized buffer — sending you
  to inspect the file instead of the code.
  Its contract is `docs/config-schema.md`: `/biosoft/config.json` (menus, task
  intervals, control gains, coil channels, Detector sources + rules,
  current-sensor channels, telemetry), with `docs/config.example.json` holding
  values identical to today's compiled defaults. Two rules constrain every
  consumer: compiled defaults always survive (the file only overrides keys it
  carries; a corrupt file or an unsupported `schemaVersion` is discarded whole,
  never applied half-way), and the Mega's share is pushed **fragmented** — one
  frame per logical unit, each under `MAX_JSON_SIZE`, sent from
  `onSerialConnected` — since the full config does not fit in one 256-byte frame
  and widening that limit would double buffers inside the Mega's 8 KB of RAM.
- `ConfigLoader` (`configloader.hpp`) reads that file at boot and applies it.
  It runs in `setup()` **before** `MySystem` registers its tasks and before the
  Configuración screen builds its dropdowns, because it overwrites what both of
  them read. `load()` returning false is *not* an error to handle — it means the
  compiled defaults stayed in place, which is a perfectly valid boot.
  Its 8 KB read buffer is `static` on purpose: on the stack it would overflow
  Arduino's `loopTask` (8192 bytes total).
  **Phase 1** (`menus`, `intervals.esp32`) is applied locally, by `ConfigLoader`
  itself. Everything Mega-bound (`intervals.mega`, `control`, `coils`,
  `detector.sources`) is parsed and held here too, but `ConfigLoader` never
  applies it — it only stores it (each value tagged with its own `has` flag,
  since a missing/invalid key must forward nothing and let the Mega keep its
  own compiled default — there's no ESP32-side default to fall back to for
  values that aren't its own) for `MySystem` to read when building the
  `config_*` frames (see below). Range checks for those sections deliberately
  live on the Mega, which is the authority over its own Detector and coils;
  the ESP32 only type-checks. `telemetry` is applied locally like phase 1 (see
  the `Topics` bullet below). **All six sections of the schema are now
  implemented.**
  Note `detector.sources[].range.mode` is parsed but *not*
  forwarded: which source derives its ranges from the target and which takes
  them verbatim is fixed by design on the Mega (CEM1 derived, TEMP1 not), so
  only `criticalMultiplier` travels.
- `ConfigurationOptions` and `Intervals` (ESP32) are **no longer `constexpr`** —
  they are fixed-size static buffers plus a real length counter (`countXxx`),
  seeded with the compiled defaults and overwritten by `ConfigLoader`. Option
  `label`s are owned `char` buffers rather than `const char*` because the JSON
  strings die with the `JsonDocument` as soon as parsing ends.
- `buildDropdown()` (`screencontroller.hpp`) now takes `ConfigurationOptions::countXxx`
  instead of a hardcoded literal per call site. That literal had already bitten
  once — the CEM tolerance list went from `{1%, 5%, 10%}` to `{5%, 10%}` and the
  call site kept passing `3` — and with the lists now coming from the SD card,
  where the length is only known at runtime, a literal could not work at all.
  The critical/normal multiplier that derives CEM1's
  critical range from this tolerance lives in `mega2560/src/safetymargins.hpp`
  (`SafetyMargins::CemCriticalMultiplier`), not hardcoded inline in `engine.hpp`
  anymore, but it's still firmware-fixed, not sent from the ESP32.
- Commands with retry: `start`/`stop`/`reset` are re-sent on a timer
  (`Tasks::ReSendStart` etc.) until the Mega's `ack` arrives, then the resend task
  is cancelled — see `onCommand`'s handling of `Commands::Ack`. The `ack` itself
  only cancels the resend; it never drives a screen transition. That only
  happens when `state_data` arrives (`_processState()`), which on the Mega side
  is a periodic broadcast (`Tasks::SendState`, added in `onReady()`), so after a
  `stop` the ESP32 can wait up to ~5s for the confirming `"ready"` before the
  Busy screen's own 6s timeout (`BusyController::update()`,
  `screencontroller.hpp`) fires first.
- `Topics` (`topics.hpp`) is no longer constants: the MQTT topic and the six
  telemetry field names/enables are mutable globals seeded with the compiled
  defaults and overwritten by `ConfigLoader` from the SD `telemetry` section —
  same "applied locally" pattern as `menus`/`intervals.esp32`, nothing here
  travels to the Mega. The field *set* is closed (six values the firmware knows
  how to produce); what's configurable is which of them publish, under what key
  name, at what cadence, and to which topic. **Those default names are the
  contract with the Datacake Decoder** — renaming one without updating the
  Decoder makes that value silently stop arriving.
  `_publishMeasures()`/`_publishStatus()` now funnel through
  `_publishTelemetry()`, which skips the batch when no field is enabled and,
  more importantly, **refuses to publish a payload that doesn't fit the 128-byte
  buffer** instead of letting `serializeJson` truncate it silently (the same
  ignored-return-value trap as `SerialLink::_sendFrame`, and now reachable
  because field names come from a file). A truncated JSON is unparseable to the
  Decoder anyway, so publishing it would trade one missing field for a whole
  lost batch, without a log line to explain it.
- `MySystem::_sendMegaConfig()` (called from `onSerialConnected()`, so it
  fires on every connect *and* reconnect) generalizes that same ack/retry
  pattern to an arbitrary set of frames instead of one fixed command: up to
  18 (1 `config_intervals` + 1 `config_control` + 4 `config_coil` + 2
  `config_source` + 6 `config_rule` + 4 `config_current`), each tracked as an entry in
  `_pendingConfig[]`, with a single `Tasks::ReSendConfig` task (not one per
  frame) sending whatever is still active until `Commands::Ack` clears it.
  Entries are matched by `command` plus a `key` that disambiguates several
  pendings of the same command (coil name, source name, `"<source>/<rule>"`,
  or `"<address>/<channel>"` for a current channel);
  `config_intervals`/`config_control` have no key since
  only one of each can be in flight. Once every entry is inactive,
  `_cancelPendingConfig()` removes the task itself.
  Note `_sendMegaConfig()` does **not** blast all 14 out at once: the
  SerialLink TX queue holds 9 usable messages and drains one per 50 ms, so
  `_sendConfigFrame()` stops as soon as `sendCommand()` returns false and the
  rest go out on the following `ReSendConfig` ticks. That `false` used to be
  ignored, which was harmless at 6 frames and would have silently dropped
  frames at 14.
- That Busy-timeout race has a fallback in `MySystem::onScreenEvent()`
  (`ScreenType::BUSY` + `EventName::Timeout`): for `StateData::Starting` it
  calls `_processState(StateData::Ready)` (back to Principal); for
  `StateData::Stopping` it used to call `_processState(StateData::Running)` —
  wrong, since the Mega had already reached `Ready` with nothing running, and
  that call resets `progress.startTime`/`progress.duration` and shows a
  "ghost" Running screen with a freshly-reset counter that will never receive
  a finish signal. Fixed (2026-08-29) to call `_processState(StateData::Ready)`
  for the Stopping case too, matching the Starting case.
- `loop()` runs the LVGL display + `MySystem::update()` on the main core; a
  separate FreeRTOS task pinned to core 1 (`communicationTask`) drives
  `MySystem::remoteUpdate()` (WiFi + MQTT) independently, so blocking network I/O
  doesn't stall the UI.
- The `ui_*.c/.h` files are LVGL-generated screen layouts (SquareLine
  Studio-style output, including `ui_img_*_png.c` embedded image assets) — treat
  them as generated UI code, distinct from the hand-written `*controller`/manager
  classes that add behavior on top.
- `RunningController` (`screencontroller.hpp`)'s Alertas tab shows up to 3 flags
  in `SystemData::alerts` (`AlertsData`, `systemdata.hpp`), filled by
  `MySystem::onCommand` on every `flag_data` via `SystemData::pushAlert(type,
  source, count, limit)`. `pushAlert` dedupes by `source`+`type`: a repeat flag
  for the same source/type updates that slot's count/limit/timestamp in place
  and moves it to the front, instead of adding a duplicate entry — so the panel
  always shows the latest accumulated count for a given alert, not every single
  event. Slots are most-recent-first (`items[0]` → `ui_Alert1`, top of the tab);
  `RunningController::_applyAlertPanel()` hides a panel entirely
  (`LV_OBJ_FLAG_HIDDEN`) when its slot has no alert yet, so with fewer than 3
  distinct alerts the unused panels disappear instead of showing an empty/dashed
  state. `_data.alerts` is reset (`AlertsData()`) each time the state machine
  enters `Running`, so a new experiment starts with a clean tab.
- Overall experiment health (`SystemData::evaluateHealth()`, `systemdata.hpp`) is
  computed entirely on the ESP32 from `_data.alerts` — it is *not* something the
  Mega reports (a prior refactor removed health scoring from `Engine`). Rule: no
  active alerts → `normal`; any active alert with `type == "critical"` → `critical`
  (even if others are streak/frequency); otherwise → `warning`. It is recomputed at
  exactly two triggers, deliberately not on a timer/poll: when `onCommand` handles
  a `flag_data` (right after `pushAlert`), and reset to `normal` when
  `_processState()` transitions into `Running`. `RunningController::_updateHealthData()`
  reads `_data.progress.health` every screen tick and drives both `ui_HealthEstado`
  (label) and the matching one of `ui_HealthNormalImg`/`WarningImg`/`CriticalImg`
  (mutually exclusive visibility toggle) — same panel used for the icons that used
  to be hardcoded to "always Normal". There used to be a `Tasks::UpdateHealth`
  5s-poll timer wired for this; it was removed as redundant once the event-driven
  recompute was added — if health ever needs a periodic fallback again, don't just
  re-add a poll without checking whether the event triggers already cover it.
- The Resultado screen (`ScreenType::RESULT`, `ui_Resultado.c/.h`) is reached only
  by receiving `result_data`, never through `_processState()`'s normal state
  machine — the Mega's `Finished` state is never observable (see `_finish()`
  above), so there's no `state_data` value to route on. `onCommand`'s
  `Commands::ResultData` branch stores `reason`/`description` from the Mega into
  `SystemData::result` and calls `_screenManager.show(ScreenType::RESULT)`
  directly, the same pattern already used by Config's Back/Save. Progress %,
  elapsed time and health are *not* sent by the Mega — the ESP32 already tracks
  them live (`_data.progressPercent()`, `elapsedTime()`, `progress.health`), so
  they're snapshotted into `_data.result` at the moment `result_data` arrives
  (frozen — they must stop advancing once the experiment is over, unlike the
  Running screen where the same getters are read live every tick).
  `ResultadoController::_applyResult()` shows exactly one of
  `ui_ResultadoExitoso`/`Falla`/`Detenido` based on `reason`
  (`"completed"`/`"critical"`/`"stopped"`), and folds the snapshotted
  progress/elapsed/health into `ui_ResultadoDetalleText` alongside the Mega's
  `description` — the SquareLine layout only has that one free-text field, no
  dedicated widgets for those three values. Because `state_data: ready` is only
  sent on a 5s poll (`Tasks::SendState`) and arrives *after* `result_data`,
  `_processState()`'s `Ready` branch treats `ScreenType::RESULT` like
  `PRINCIPAL`/`CONFIG` (early-return, doesn't auto-navigate away) — otherwise
  that stale "ready" broadcast would silently bounce the operator back to
  Principal a few seconds after arriving, before they could read the result.
  Leaving Resultado is manual only, via `ui_ResultadoBtnVolver` (already built in
  SquareLine, just needed wiring) → `EventName::Back` → resets `_data.result` and
  shows `PRINCIPAL`.

### Style notes specific to this codebase

- Nearly everything is header-only (`.hpp` with `inline` method bodies defined
  right after the class), Arduino-sketch style — there are no corresponding
  `.cpp` files for these classes.
- Heavy use of the listener/observer pattern for decoupling between managers;
  when adding behavior, look for the relevant `*Listener` interface before adding
  direct calls between classes.
- Extensive debug tracing (especially around `start`/`ack` handling) is
  intentional for on-device debugging over USB serial — match this style when
  touching those code paths rather than stripping it out. It goes through
  `debugconfig.hpp`'s `DEBUG_PRINT`/`DEBUG_PRINTLN`/`DEBUG_PRINTF` macros now,
  not raw `Serial.print`/`println`/`printf` — every `.hpp`/`.ino` that logs
  declares its own `constexpr bool DEBUG_<MODULE>` near its includes, gated by
  the global `DEBUG_ENABLED` in `debugconfig.hpp` (duplicated per board like the
  files above). Both flags are compile-time constants, so a disabled log is
  dead-code-eliminated, not just skipped at runtime. New logging in an existing
  file should reuse its `DEBUG_<MODULE>` flag; a new file needs to declare one.
