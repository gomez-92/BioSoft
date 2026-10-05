# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

BioSoft is firmware for a two-board embedded system that exposes lab animals to a
controlled, low-frequency electromagnetic field for a bioengineering research
experiment. It is a PlatformIO migration of two former Arduino IDE sketches
(`SoftEsp32`, `SoftMega2560`).

Three pieces. Two independent PlatformIO projects, one per board, connected by
a serial link — plus `web/`, which lives on the far side of the MQTT broker and
touches no firmware:

- **`esp32/`** — touchscreen HMI (LVGL + TFT_eSPI). Shows system state, lets the
  operator configure experiment parameters, and publishes telemetry over MQTT for
  remote monitoring.
- **`mega2560/`** — physical control. Drives the coil (signal generator + PWM),
  reads field/current/temperature sensors, and decides when to cut the experiment
  (time elapsed, danger detected, or loss of scientific rigor).
- **`web/`** — remote monitor. Subscribes to the ESP32's telemetry, stores it in
  MongoDB and serves a dashboard (live + history). No remote commands by
  design; its only write paths toward the board are the SD config (card 24,
  below) — plus, locally, deleting runs and managing users, all admin-only.

There is no shared source directory — `esp32/src/seriallink.hpp` and
`mega2560/src/seriallink.hpp` (and `commands.hpp`, `timer.hpp`, `debugconfig.hpp`)
are **meant to be duplicated, byte-for-byte, in each project**. When changing the
serial protocol, edit both copies and keep them in sync manually. In practice
`seriallink.hpp` has already drifted — the ESP32 copy has extra
`Serial.print`-era logging (ping/pong, connection-state changes) the Mega copy
never had; the framing/CRC/queue logic itself is still identical. Don't assume
the two copies are interchangeable without diffing first.
Their sizes (`MAX_TASKS`, `MAX_TASK_NAME`, `TX_BUFFER_SIZE`) are `#ifndef`
defaults and the Mega overrides them in its `platformio.ini` (10 / 24 / 384),
so the files stay identical. That is a RAM fix, not tuning: at the defaults
the Mega had ~200 bytes free and **hung on the first frames it exchanged**
(heap and stack colliding — `[PING] ... | RAM libre -4`, 2026-10-03). A task
name longer than `MAX_TASK_NAME - 1` is truncated and never matches in
`onTimer()`, so a new Mega task name must fit in 23 chars.
**The TX queue holds finished frame bytes, not `JsonDocument`s**:
`sendCommand()` writes `{"command":"…","params":…}` by hand straight into
`_txBuffer` (same bytes the old envelope serialized to — pinned by
`test_seriallink`, which pushes frames through the real parser), and
`update()` just copies one frame out per `TX_INTERVAL_MS`. The old queue kept
a heap copy of every pending message plus an envelope copy and a 256-byte
stack buffer at send time; a critical cut then had 245 bytes left and built
an **empty `result_data`** (ArduinoJson nulls strings on OOM, silently) — the
ESP32 showed a blank Resultado, the monitor never closed the run, and the
`reset` ack came out empty too, so the ESP32 resent `reset` forever. Two
behaviour changes ride along: a frame over `MAX_JSON_SIZE` is now **refused**
(`false`) instead of truncated with a valid CRC, and "full" is measured in
bytes, not messages. The ESP32 also turns an empty `reason` into `refused`
(while Starting) or `unknown` (otherwise), so neither the screen nor the
monitor is left without an ending. `SerialLink` also **no longer keeps a copy
of the last params** (`_params`/`getParam()` are gone, nothing read them): the
listener gets `doc["params"]` from `_processFrame()`'s local document, so a
handler must copy any string it wants past `onCommand()`. One did not:
`SystemData::setState()` stored the pointer it was given, and `state_data`
hands it the frame's `status` string — dangling the moment `onCommand()`
returned, after which state compares returned garbage (`oldstate: <` in the
log) and the screen sat on "Iniciando" through a whole run. It now stores
the matching `StateData` constant, which is also what makes the existing
pointer compares (`getState() == StateData::Starting`) valid.
`config_current`'s `name` is the other close call (copied, then nulled). The Mega also builds
with `ARDUINOJSON_POOL_CAPACITY=8` (48-byte pools instead of 96). With the
first round of these it still hung mid-`config_*` burst with 666 bytes free
at a ping; `Engine::onCommand` alone takes ~245 bytes of stack.

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

The web monitor is a separate npm workspace:

```
cd web && npm install
npm run infra:up      # Mongo + Mosquitto in Docker, for development
npm run dev           # backend :4000, front :5173
npm test              # the three suites
```

Its Mongo-backed tests **skip themselves with a message** when the compose
stack isn't up, rather than failing as if the code were broken. Each test file
uses **its own database** (`biosoft_test_<name>`): vitest runs files in
parallel, and with one shared database the `deleteMany` of one wipes what
another just wrote — a failure that appears and disappears depending on worker
order, and looks like a bug in the run correlation.

The external configuration generator (`tools/`, see below) is plain JS and has
its own host check, which needs nothing installed but node:

```
node tools/test-generador.js
```

The user manual (`docs/manual-de-usuario.html`) is hand-authored HTML, and it is
the **single source** — there is no Markdown copy on purpose, since two
hand-maintained copies of the same manual drift the way the two `seriallink.hpp`
already did. The PDF beside it is generated from that file and is not edited by
hand:

```
chrome --headless=new --disable-gpu --no-sandbox --user-data-dir=<tmp>   --print-to-pdf=docs/manual-de-usuario.pdf docs/manual-de-usuario.html
```

Its print layout lives in the `@media print` block at the end of the file:
cover on its own sheet, a printed table of contents the screen version hides,
and `break-before: page` on every section, because the manual is consulted by
jumping to a section rather than read start to finish.

`python tools/manual-a-word.py` exports the same manual to
`docs/manual-de-usuario.docx`. It writes the OOXML by hand rather than driving
Word: automating Word over COM returned `RPC_E_CALL_REJECTED` persistently when
saving an opened HTML — the Protected View signature — and it tied the export to
having Word installed. Two things it has to fix that the HTML cannot express:
the manual's `<h2>` is the small "Sección N" label while the real title is a
`<p class="h2title">`, which is right on screen but would leave Word's navigation
pane and automatic TOC listing fourteen entries called "Sección N"; and Word maps
its built-in styles **by `w:styleId`**, so the headings must be `Heading1`/
`Heading2`, not custom ids carrying the right `w:name` — with custom ids they
render fine and silently carry no outline level, which is exactly the bug that
makes the navigation pane empty. Child order inside `w:pPr` is schema-fixed too
(`keepNext` before `pageBreakBefore`); out of order, Word drops what follows,
`outlineLvl` included.
The wiring diagrams (sections 3–4, added 2026-09-11) are `<pre
class="diagram">` blocks of box-drawing text, chosen over SVG precisely so
the same figure survives HTML, PDF and the Word export. The exporter turns
each into one `Diagrama`-styled paragraph (Consolas 7pt, `<w:br/>` per line,
spaces preserved — `_Inline` collapses whitespace and would wreck the
alignment). Its block regex matches `<pre` *before* `<p`, and the `<p`
alternative is `<p(\s[^>]*|)>`: with the old `<p[^>]*>`, `<pre class=…>`
matched as a paragraph with attributes `re class=…` and the diagram vanished
without an error. Keep every diagram ≤ 90 columns — at the print size
(8.2pt) that is the A4 text width, and Chrome clips the overflow silently.

`docs/puesta-en-marcha.html` is the bench bring-up checklist (phase 0
preparation → each board alone → link → full runs on synthetic scenarios with
nothing energized → sensors one at a time → actuators unloaded → power, one
coil at a time). Same self-contained rule as the manual; ticks and notes live
in the browser's `localStorage`. When a step's expected log line or behaviour
changes in firmware, this page goes stale silently — update it with the code.

**The `.docx` is a one-way export, not a synchronized copy.** Re-running the
script overwrites it. If the manual is ever edited in Word, that file becomes
the authority and the changes have to come back to the HTML by hand — the same
drift hazard as the two `seriallink.hpp`.

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

Both boards talk over a hardware UART (`Serial2` on ESP32 RX 35 / TX 22 ↔
`Serial1` on the Mega — on the JC2432W328C; the old 2432S028R used RX 22 / TX 27,
but on this board 27 is the backlight and 21 the touch INT, and 22/35 are the
only free GPIOs on its connectors) using a custom framed protocol:

```
STX(1) LEN_LO(1) LEN_HI(1) JSON(LEN bytes) CRC8(1) ETX(1)
```

Each frame carries a JSON payload with a `command` string (see `commands.hpp` —
`ping`/`pong`/`start`/`stop`/`reset`/`state_data`/`result_data`/`temp_data`/
`cem_data`/`coil_data`/`ack`) and a `params` object. `SerialLink` owns a small
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
  built standalone in tests. CEM1's cadence is now the RMS window (2000 ms,
  see the `MagnetometerMlx90393` bullet), so `kp`/`maxStep` must be retuned
  for a ~2 s loop (four times slower than the 500 ms they were guessed for).
  The tuning parameters (`kp`, `maxStep`, `deadBand`)
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
  ESP32 sends it always and explicitly, from `_sendStart()`
  (`ConfigurationOptions::optionsFieldMode` → `configuration.fieldModeOption`),
  chosen by the operator in the Configuración dropdown
  (`ui_ConfiguracionesModoExposicionOpciones`, wired 2026-09-19).
  `test_start_fits_with_field_mode` (native) pins that the 9-key `start`
  frame still fits, since a truncated `start` would drop `mode` silently and run
  the control group with real field.
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
  **Both cuts are active-HIGH, fixed, and that is a hardware spec, not a
  default** (`docs/coil-excitation.md` §3.5): HIGH energizes; LOW, a floating
  pin or an unpowered Mega leaves the relay open and the coils cut, with a
  pull-down on each driver input. `mainRelay` was `ACTIVE_LOW` until
  2026-10-04 — rejected because an unpowered Mega with the stage powered
  clamps the pin near 0 V and closes an active-low relay. Making polarity
  SD-configurable was considered and **rejected**: the config arrives over
  serial seconds after boot (never, without the ESP32), so until then the
  firmware would write the "inactive" level of a polarity that may not match
  the hardware. Flipping it in firmware without changing the driver closes
  the relay at boot — the two always change together.
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
  CEM1 can be fed by the real `MagnetometerMlx90393` (the compiled default
  since card 22 — it used to be the sim), `MagnetometerVoltageSim` (a plain
  analog voltage on `A0` mapped linearly to mT, which does **not** validate
  `FieldController` against anything physically meaningful), or a scenario
  (below).
- **Which driver feeds each source is a `SensorChoice`
  (`Real`/`Sim`/`Scenario`/`None`, `sensorchoice.hpp`)**, parsed per source by
  `SensorChoices::parse()` from `config_source`'s `sensor`: CEM1 takes
  `mlx90393`/`sim`/`scenario`, TEMP1 takes `ds18b20`/`sim`/`scenario`/`none`. Names are **not** interchangeable across sources,
  and an invalid value is rejected whole (returns false, keeps the previous
  driver) — the old code mapped "anything but mlx90393" to sim. **CEM1 refuses
  `none` by design**: it is the `FieldController`'s feedback, and with no
  magnetometer the loop would push duty to the rail chasing a field it can't
  see. TEMP1's two drivers reach Engine through `registerThermometers(real,
  sim)` (the DS18B20 is `new`'d in `setup()`, after Engine exists), and
  `_applySourceSettings()` re-registers the chosen one on every `start` —
  `none` registers nothing, the only safe bring-up state without a DS18B20
  since the DS18B20 conversion takes ~750 ms even on an empty bus. TEMP1
  watched with no thermometer is legal but never cuts; Engine logs it.
  **The DS18B20's ROM code is `detector.sources[TEMP1].address`** (schema
  §7), no longer only the literal in the `.ino` — another unit of the same
  sensor read `-127` forever and cut by silence. Validated whole
  (`onewireaddress.hpp`: family `0x28` + CRC8, mirrored in the generator),
  a bad one keeps the previous; applied on receipt when not Running (so the
  screen reads it without a start), else at the next start, through
  `IThermometer::setAddress()`. `setup()` prints every DS18B20 on the bus
  (`[ONEWIRE]`, own debug flag) in the file's format — that is how you get
  the address of a new sensor. The ESP32 sends it without separators for the
  frame budget. Its error strings are `F()` and `format()` has no hex table
  on purpose: as plain literals they cost ~130 bytes of the Mega's RAM.
  `ThermometerVoltageSim` reads **A1, 0–5 V → 0–50 °C** (10 °C per volt, so
  every menu zone is reachable with a pot). `ThermometerManager::
  addThermometer()` now skips `begin()` on an already-valid sensor, same
  reason as the magnetometer bullet below: it is re-registered every start.
- **Synthetic scenarios (`sensor: "scenario"`, card 22) generate readings
  *inside* the Mega** (`scenario.hpp` + `scenariosensors.hpp`), as one more
  `IMagnetometer`/`IThermometer`, so every sample walks the real path:
  Detector, rules, flags, `_finish()`, `result_data`, screen, web monitor.
  Fabricating data on the ESP32 was the first idea and was rejected — it
  would only test transport, with fake alerts; streaming samples over serial
  was rejected too (9600 baud shared with everything, and a link drop would
  freeze the "sensor", which reads like a sensor fault). Only the signal's
  *definition* travels, once, in `config_scenario` (short keys `b n r sa s
  oa op da d`, budget pinned in `test_serialframes`), stored in the source
  template and copied into the sensor at the next start.
  **The signal is in *levels* relative to the run's ranges, never absolute
  values** (user requirement): 0 = centre of normal, ±1 = normal edge, ±2 =
  critical edge, linear in between and beyond (a flush critical/normal pair
  uses half the normal range as the 1→2 step, or every level > 1 would sit on
  the edge). Engine hands each scenario sensor the ranges it just gave the
  Detector (`setRanges(_scenarioRangesOf(config))`, right after
  `configureSource`), so "temperatura alta" (+2.5) cuts with any range chosen
  on screen. CEM1 has a plant: `setpointDuty` is the common duty that yields
  the target, field = target·duty/setpointDuty + (level value − target), so
  the `FieldController` really regulates — and can legitimately *compensate*
  a slow disturbance; `setpointDuty: 0` drops the plant to force the field
  out of range. Time is `Scenario::startMs`, reset in `_start()`; noise is a
  local xorshift32 (no `random()` in native).
  **With CEM1 not real (`sim` or `scenario`) nothing is energized**:
  `_applySourceSettings()` sets `_syntheticField`, `_start()` calls
  `CoilChannels::setOutputsInhibited()` *before* `enableAll()` (which then
  enables no channel; `writeAll()` still records `appliedDuty()`, so the
  screen shows the duty that would apply) and skips `_mainPowerSwitch.enable()`.
  That is why CEM1's compiled default moved from `sim` to `mlx90393`: with the
  block, a sim default would leave a Mega without SD unable to ever drive the
  coils. On the ESP32, `ConfigLoader::marksRunsAsTest()` (= `runType` test
  **or** any source on `sim`/`scenario`) forces `TEST` and the MODO PRUEBA
  notice — fabricated data never enters the history as an experiment.
  RAM cost on the Mega is ~320 bytes (two signal templates + two sensors),
  leaving it at ~76% (84% after the RMS sampler, below).
  The "sensor caido" profile (`dropAt`) is the bench test of the silence cut
  below.
- **A watched source that goes silent cuts the run** (card 25). Before it,
  the Detector only evaluated samples that arrived, so a DS18B20 unplugged
  mid-run let the experiment finish `completed`. Silence = no valid sample:
  managers already drop invalid readings (DS18B20 `-127` fails the
  plausibility check, a failing MLX goes invalid), so Engine just calls
  `Detector::noteAlive()` on every valid sample — **before** the settling
  gate, so the clock runs from `start` — and `Detector::checkSilence()` from
  `Engine::update()` every loop (cheap, and independent of the `Timer` being
  healthy). `resetSilence()` in `_start()` (after the `configureSource()`
  calls, which reset sources), `armSilence()` when `Tasks::SettlingTime`
  fires. The limit is `maxMissedSamples` (SD, 1–20, default 3) × the source's
  measuring interval (`SourceConfig::sampleIntervalMs`, set by Engine at each
  start from `Intervals`), floored at `MinSilenceMs` = 5 s because the
  DS18B20's ~750 ms conversion makes sample arrival jittery. It emits one
  `EventType::Silence` with `count >= limit`, so the existing `onFlag` path
  cuts it (`type: "silence"`), reported once. It respects
  `detector.enabled` — **except CEM1 with the control loop on**, which
  `Engine::_checkControlSilence()` cuts regardless of watch or master switch,
  because the loop would otherwise keep driving the coils blind on its last
  duty; the decision is the pure `controlSilenceExpired()` in `detector.hpp`
  so native tests can pin it. Bench consequence: MLX unwired + `control.enabled`
  ⇒ every run cuts at ~15 s. On the ESP32 `isCriticalAlertType()` makes
  `silence` count as critical for health and dot colour, and Resultado reads
  "<SRC> SIN LECTURAS". TEMP1 `none` + watched is now a generator **error**.
- **`detector.enabled` is a master switch inside `Detector` itself**
  (`setEnabled()`: `addSample()` drops everything, sources and config stay
  intact), not a gate scattered across Engine's three sample handlers — which
  is what lets `test_detector` pin "disabled never flags". It travels in its
  own `config_detector` frame (sent only if the file carries the key; acked
  with no key, like `config_control`) and, like the sources, Engine stores it
  in `_detectorEnabled` and applies it at the **next `start`**: switching the
  detector off mid-run because the ESP32 reconnected with a different card
  would leave a run that started watched without a watcher.
- `MagnetometerManager::addMagnetometer()` only calls `magnetometer->begin()`
  if the magnetometer is not already `isValid()`. Needed because
  `_applySourceSettings()` re-runs `clearMagnetometers()`/`addMagnetometer()` on every
  `start`, and unconditionally calling `begin()` on `MagnetometerMlx90393`
  re-inits the Adafruit driver (full chip reset) on an already-working sensor
  seconds after its initial `begin()` in `setup()` — reproducible on bench as
  `begin_I2C` failing the *second* time (first always OK), with no actual
  wiring problem. A magnetometer that never connected, or that dropped mid-run,
  still gets its `begin()` retried since it's not valid.
  **That "second time" failure was a library bug, not the chip**:
  `Adafruit_MLX90393::begin_I2C()` (2.0.5) does `delete i2c_dev` without
  nulling it and then uses the freed pointer. With the MLX *absent* it is
  worse than a failure: the detect at the corrupted address 0 is ACKed by the
  ADS1115 (general call), the next write goes through a broken vtable and the
  Mega **reboots in a loop before `Engine::begin()`** — seen 2026-10-04 as
  `[MLX90393] ` followed by NULs, forever, and the ESP32 never linking.
  `MagnetometerMlx90393::begin()` now probes the address first, calls
  `begin_I2C()` at most once, and retries with the public
  `exitMode()`/`reset()`. Never call `begin_I2C()` twice on one object.
- **CEM1 measures the RMS of the fundamental, not a snapshot, and there is no
  ambient tare** (`fieldsampler.hpp`, driven by `MagnetometerMlx90393`).
  A snapshot depended on where in the 1-100 Hz wave it landed, and the ambient
  (~0.025-0.065 mT, DC) could fill a whole 5 % band — so the old tare
  (`FieldTare`/`TareSequencer`/`control.tare`/`tare`/`taretime` refusals) is
  gone, not replaced: per group, a least-squares fit
  `y = a cos(wt) + b sin(wt) + c` per axis, where `c` *is* the ambient and
  drops out. Output is sqrt(mean group power) in µT, ÷1000 → mT (the rest of
  the system works in mT; the log prints both units, since near ambient the mT
  value can round to 0.0000 and the µT figure tells "not reading" from "small
  field"). The value renews every `Intervals::MeasureMagneticField` (**2000 ms**
  now, was 500): the settling window and cadence were the negotiable costs
  accepted for precision; the emergency-stop latency and the field accuracy
  were not.
  Things that bite:
    - **3 samples/period is the minimum, not enough**: 3 unknowns, zero
      redundancy, and the 2nd harmonic aliases exactly onto the fundamental
      (a 20 % harmonic read as ~20 % error). The bus only gives ~165-330
      samples/s at 100 Hz, so a group is **10 samples, uniform T/10 in one
      period up to ~33 Hz, and above that spaced (m+0.382)T or (m+0.618)T**
      (golden ratio, never T/k) over ~5 periods — same samples/s, 7 degrees of
      freedom, harmonics 2-7 rejected to <2 % (`test_fieldsampler`). A group
      lasting a few periods is still fine against crystal/AD9833 error (~1e-4 ×
      periods); fitting the whole window would not be.
    - **`SlotMarginUs` (1000 µs) in the minimum spacing is load-bearing.**
      `poll()` runs once per `loop()`; with a grid tighter than the loop
      jitter the *real* interval drifted to ~0.5T, the harmonics came back and
      half the groups were discarded at 100 Hz. Each sample carries its real
      `micros()` timestamp, so jitter itself does not hurt; slot slippage does.
    - **Frequency is the AD9833's actual one** (`ad9833ActualHz`, 0.0931 Hz
      step — up to 4.7 % off at 1 Hz), passed by Engine, not the requested one.
    - **Non-blocking**: `poll()` does at most one I2C transaction (trigger *or*
      fetch), `Engine::update()` calls `pollAll()` each loop, `updateAll()`
      skips `isAsync()` sensors. The old blocking read held `loop()` and with
      it the emergency button. Five consecutive bus failures make the sensor
      invalid (CEM1 silence cuts the run, card 25); first success recovers.
      OSR_0/FILTER_0 (tconv 1.27 ms) is the shortest conversion; OSR_1 is the
      fallback if bench noise bothers — the plan recomputes itself.
    - RMS has a **noise floor** (never reads 0 with coils off) — it is the
      minimum distinguishable in null field. An AC source at the same frequency
      (mains at 50/60 Hz) *does* add. **All of it unverified on the bench**:
      I2C cadence, noise floor, 100 Hz attenuation, RAM (Mega at ~84 %).
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
- **I2C / blocking hazards, now bounded** — the motivation is the emergency
  button, which `loop()` polls: anything that holds `loop()` delays it.
  `Adafruit_ADS1115::readADC_Differential_*()` used to **hang indefinitely**
  waiting for an ACK that never comes when the ADS1115 isn't wired
  (confirmed on-device: the log cut off mid-tick and `Tasks::Finish` never
  fired). Guards, all in place:
    - `Wire.setWireTimeout(25000, true)` in `SoftMega2560.ino`: any bus hang
      is cut at 25 ms.
    - `CurrentSensorSct013::begin()` probes the address first (a bare
      `beginTransmission`/`endTransmission`) and only then `_ads.begin()`; if
      the chip isn't there the sensor stays disabled and `update()` returns
      before touching the bus. That is what makes a channel on the second
      module (0x49) safe to leave in the SD file before the module exists.
    - The SCT013 integration window (an RMS over tens of ms) can be aborted:
      `CurrentSensorSct013::abortCheck` is set by `Engine::begin()` to
      `EmergencyButton::isPressed()` (the raw pin level, not the debounced
      edge), checked before each `readRaw()`. A pressed button cuts the window
      instead of waiting it out.
    - `ThermometerDS18B20` is async: it `update()` triggers a conversion with
      `setWaitForConversion(false)` and `poll()` collects it ~800 ms later, instead of `requestTemperatures()` blocking `loop()` 750 ms per
      call. Still must not be registered with no DS18B20 wired for a *useful*
      reading, which is the SD setting `detector.sources[TEMP1].sensor: "none"`.
    - `MagnetometerMlx90393` is non-blocking (above).
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
- The phase-inversion circuit itself (op-amp inverter + 4053 mux from
  ADR-001) is drawn in **`docs/etapa-de-fase.html`** — a self-contained HTML
  with an inline SVG schematic, BOM and bench checks, same "opens by
  double-click, no dependencies" rule as the manual and the generator. It is
  the **single source** of the schematic: the ADR and `coil-excitation.md`
  point to it and only summarize the choices (MCP6004 + CD4053B, inversion
  around a buffered VREF = 2.5 V, R2/R3 the only matched pair). Don't
  redraw it in Markdown or box-drawing text next to those summaries. The
  selector polarity drawn there (A = HIGH ⇒ X1 = inverted ⇒ null field)
  matches the `HIGH` passed to `CoilExcitation` in `SoftMega2560.ino`; if the
  board ends up wired the other way, that argument is the one thing to flip.
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
- **`coil_data` carries duty *and* current, one entry per registered coil**
  (`"d1".."d4"` in percent, `"c1".."c4"` in amps, from
  `Engine::_sendCoilData()`). It replaced `current_data`, which sent only the
  currents keyed by sensor name (`"SCT013-1"...`): the Running screen needs
  both magnitudes per coil row, and needs to know how many coils actually
  exist on the other side. Keys are short because the frame budget is 256
  bytes and `serializeJson` truncates silently with a valid CRC —
  `test_coildata_fits_with_four_coils` (native) pins it. Duty is rounded to
  one decimal and current to two on the Mega, both to bound the frame and
  because that is the precision the screen shows. The *set of keys present*
  is itself payload: the ESP32 draws one coil row per coil that reported,
  when it has no `coils` section from the SD. The duty sent is
  `CoilChannel::appliedDuty()` — the common duty times that coil's
  calibration factor, already saturated, and reset to 0 by `disable()` — not
  the `FieldController` setpoint, so a coil running short is visible on
  screen.
- **Map duty/field and null-field mode (card 135)** (`controlmap.hpp`).
  Intensity is no longer found by the
  controller from zero: `ControlMap` holds up to 9 points (3 intensities x 3
  frequencies, the menus' cap) with the duty measured on the bench for each,
  and `start` begins at that duty, the controller only trimming. Points come
  from `control.map` of the SD through `config_map` (one frame per point;
  `n=0,k=0` is the **clear** frame: a card without map must empty what the Mega
  kept from a previous connection, or null field would run on points the card
  does not say). Null field **drives the coils at the mapped current but the
  target is 0**, and the tolerance stays the percentage of the *chosen*
  intensity (user decision), not an absolute one. The residual is corrected by
  the *direction* of the measured vector (which group wins), through a
  per-point `balance` bounded by `control.balanceMax`; the fixed-sigma variant
  was chosen over an adaptive one (documented as future improvement).
  **A start that cannot begin is refused, not run degraded**: null field with
  no point for that intensity/frequency or a balance over the cap.
  `Engine::_refuseStart()` sends `result_data` with `reason: "refused"` +
  `cause` (`nomap`/`balance`) and never goes through
  `_finish()` (nothing ran, Engine stays Ready).
  There is no ambient tare any more (card 135 first had one, with
  `control.tare` thresholds): the RMS measurement removes the ambient by
  construction, see the `MagnetometerMlx90393` bullet. The MLX still reports no
  sign, so the balance direction is found by trial. `config_control` frame
  width is pinned in `test_serialframes`.
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
- **Screens are created and destroyed as you navigate, not all at boot**, and
  that is what makes MQTT over TLS possible at all. With `LV_USE_STDLIB_MALLOC`
  set to `LV_STDLIB_CLIB`, LVGL allocates from the same heap as mbedTLS, and
  the six screens together cost ~176 KB (measured on the board: 253 KB free at
  boot, 77 KB once they exist, and the largest contiguous block dropping from
  110 KB to 45 KB). `mbedtls_ssl_setup()` needs **two ~16.7 KB buffers**
  (`CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384` in the core's SDK, with neither
  asymmetric nor variable buffers, and it cannot be changed without rebuilding
  the IDF), so with all six alive every connect died with `-32512
  MBEDTLS_ERR_SSL_ALLOC_FAILED`.
  `ui_init()` therefore **no longer calls the six `_screen_init()` functions
  nor loads a screen** — the second hand edit to the generated `ui.c`, which
  has to be redone after every SquareLine export.
  Which screens stay alive is `ScreenManager::belongsToGroup()`: **Principal
  and Configuraciones live together** (you go back and forth between them and
  recreating one two seconds later would only add latency); every other screen
  lives alone. Splash is destroyed on reaching Principal and never rebuilt.
  Three things this required, each of which crashed the board when it was
  missing:
    - **No LVGL screen operation may happen inside an LVGL event callback.**
      `show()` only records the request; `ScreenManager::update()` performs it,
      called from `loop()` and not from `lv_timer_handler()`. Tapping INICIAR
      would otherwise destroy Principal while LVGL was still dispatching that
      very button's event. `_currentIndex` still moves inside `show()` so
      `MySystem`'s state machine does not reprocess the same transition;
      `_previousIndex` remembers what to hide.
    - **A blank bridge screen is loaded first** (`_blank`, one `lv_obj`, created
      once and reused). It resolves a genuine contradiction: memory demands
      freeing the old screens *before* creating the new one — creating En curso
      with Principal and Config still alive does not fit and `LV_ASSERT_MALLOC`
      aborts — but deleting the screen LVGL holds as active leaves
      `disp->act_scr` at NULL (`obj_delete_core` in `lv_obj_tree.c`) and the
      next draw dereferences it. `LV_USE_ASSERT_OBJ` is 0, so nothing warns.
      With the bridge loaded, the active screen is none of ours and the order
      becomes **bridge → destroy → create → load**. No `lv_timer_handler()`
      runs in between, so the blank is never actually drawn.
    - **`BaseScreenController` keeps `lv_obj_t**`, not `lv_obj_t*`.** The
      `ui_XxxScreen` global is nulled on destroy and repointed on create, so a
      pointer copied in the constructor dangles the first time a screen is
      recycled. For the same reason `create()` always re-runs the controller's
      `init()`: event callbacks, dropdown lists and the "already painted"
      caches (`StatusIconsCache::applied`, `RunningController::_visibleCoils`,
      `_shownMagneticField`) all describe widgets that no longer exist, and a
      stale cache leaves the export's mock values on screen.
  The transition is visible — En curso is ~120 widgets — but accepted.
  `SoftEsp32.ino` also carries `CONECTAR_ANTES_DE_LA_UI`, which connects WiFi +
  NTP + broker before building the UI (when the heap is untouched: 110 KB
  contiguous). It works, and it is **off**: with screens recycled the ordinary
  connect path succeeds, which also means *reconnections* succeed — something
  connecting early never fixed, since a reconnect always finds the heap already
  fragmented. Keep it as a documented escape hatch, not as the solution.
- `MySystem::_processState()` is the app-level state machine (`idle` → `ready` →
  `starting`/`stopping` → `running`, from `systemdata.hpp`'s `StateData`) that
  decides which screen should be showing and mirrors state received from the Mega
  over serial (`state_data` command) and from the local UI.
- `_sendStart()` builds the `start` command JSON from `SystemData::configuration`,
  resolving each setting through `ConfigurationOptions` lookup tables (intensity,
  frequency, duration, tolerance, temperature ranges) before sending it to the Mega.
- `SdStorage` (`sdstorage.hpp`) is a generic SD-card file read/write module
  (agnostic of content, per its own contract) for the ESP32-2432S028 (CYD)
  board's onboard SD slot. **It does not share the TFT's SPI bus**: the TFT
  is on HSPI (12/13/14), the SD slot is wired to VSPI (SCK=18, MISO=19,
  MOSI=23) with its own CS on pin 5, and `SdStorage` opens its own
  `SPIClass(VSPI)` there. The first version assumed 14/12/13 on HSPI and
  failed on-device with `sdSelectCard(): Select Failed` (the card never
  answers, before any mount is attempted); `tools/test-sd-esp32` is the
  bench that caught it — 39/39 once the pins were fixed. After the tests
  that bench also dumps `/biosoft/config.json` as `ConfigLoader` would read
  it (same path, same 8 KB buffer), and lists `/biosoft` with exact names
  when the file is missing — the usual cause being a Windows-hidden
  extension (`config.json.json`, `config.txt`), which nothing else would
  reveal. It is the way to check a card without an SD reader on the PC. `begin()` opens the SPI bus — the constructor only stores
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
- **The HMI board comes in two incompatible revisions, and `display.hpp` has
  to be told which one** (`DisplayDriver::TOUCH_KIND`). The original
  ESP32-2432S028R (one micro-USB) has an ILI9341 panel and a resistive
  XPT2046 on its own VSPI bus (25/32/39, CS 33, IRQ 36). The replacement
  bought in 2026-09 is a Guition **JC2432W328C** (micro-USB + USB-C, model
  printed on the back): ST7789 panel and a **capacitive CST816S over I2C**
  (SDA 33, SCL 32, RST 25, INT 21, address 0x15) sitting on the same GPIOs
  the R used for the XPT2046. Two symptoms give it away with the R config:
  colours inverted (white→black, blue→orange — fixed by `TFT_INVERSION_ON`
  in `platformio.ini`, which the R must *not* have) and a permanent "touch"
  in one corner, because the XPT2046 library reads a floating MISO
  (`raw=(-4096,-4096)` / `(4095,4095)` in the `[TOUCH]` trace). The CST816S
  driver is hand-written in `display.hpp` (5 registers over `Wire`, no
  library) and disables the chip's auto-sleep, since LVGL polls it instead
  of using INT. Switching boards means flipping `TOUCH_KIND` *and* the
  inversion flag together.
- **MQTT/TLS: the CA was never the problem.** An earlier note here claimed the
  EMQX Cloud *serverless* instance (`*.emqxsl.com:8883`, host in `secrets.h`)
  was signed by Let's Encrypt while `SoftEsp32.ino` embedded *DigiCert Global
  Root G2*, and blamed the `-9984 X509 - Certificate verification failed` on
  that. It was wrong: the certificate downloaded from the EMQX console for
  this instance is byte-identical to the compiled one (same SHA-256, verified
  2026-09-22). The real cause was the **clock** — every certificate carries
  validity dates and an ESP32 boots at 1970, so a perfectly good CA is
  rejected as "not yet valid". `MySystem::_syncTime()` now calls
  `configTime()` from `onWiFiConnected()` and `remoteUpdate()` refuses to let
  the broker even try until `_timeIsValid()` (epoch past 2025), which also
  stops burning a full TLS handshake every 5 s on a connection that cannot
  succeed. Don't re-open this as a certificate problem without comparing
  fingerprints first.
  **Mid-run broker drops were a WiFi scan, not the broker** (bench,
  2026-10-04, visible once telemetry went from 30 s to 5 s). `WiFiManager`
  re-ran `WiFi.scanNetworks()` every 10 s *while connected* — blocking for
  seconds, radio off the AP's channel, on the same `communicationTask` that
  runs MQTT. The publish queue (4 slots) drained once per ~10 s, so a third
  of the telemetry was evicted (`Queue llena` twice a cycle; the published
  order `status, measures, coils, status` is exactly what survives of
  `M C S M C S`), and now and then PubSubClient's 15 s keepalive expired and
  dropped the link. Fixed in three places: the priority rescan now runs at
  most every 10 min, **never while already on network 0** (nothing better to
  find) and never during a run (`MySystem::remoteUpdate()` calls
  `setRescanAllowed(!experimentInProgress())`; reconnecting with WiFi down
  still scans); `BrokerManager` sets keepalive 60 s / socket timeout 5 s
  (`BrokerKeepAliveSeconds`, `BrokerSocketTimeoutSeconds`); and a drop now
  logs `[BROKER] Desconectado: <reason> (state=N) tras X s conectado`, with
  WiFi status, RSSI and heap — before, a drop left no line at all. Don't
  enlarge the publish queue to hide drops: each slot is ~640 bytes of the
  heap the TLS handshake needs.
  The remote monitor is a **dashboard of the user's own**, not Datacake, and
  there are **no remote commands**: that is a deliberate boundary — a remote
  `start` would energize coils with animals in the cabinet and nobody in the
  room, while the physical e-stop only helps someone who is already there.
  Its one write path is the SD config (next bullet), which never acts live.
- **Remote config (card 24, `remoteconfig.hpp`)**: the board's first inbound
  channel — `onMessageReceived()` listens to `biosoft/config/set` and
  `biosoft/ping` (below). Contract in schema §17. Things that bite if forgotten:
  - The MQTT callback runs on the **comms core**; the SD is also used by the
    main loop. So `onMessageReceived()` only copies into a FreeRTOS queue
    (`_configInbox`, 4 × 512) and `MySystem::update()` processes it. Same for
    the "publish current config" trigger: `onBrokerConnected()` sets a
    volatile flag, `update()` adds the timer task (`Timer` isn't core-safe).
  - `BrokerManager::handleMessage` used to truncate inbound payloads to 128
    bytes; a chunk is ~400, so it's `BrokerBufferSize` now.
  - The current config (12-ish chunks + meta) is published **one chunk at a
    time and only with the publish queue empty** (`Output::publishQueueIdle()`).
    `BrokerManager::publish()` never fails on a full queue — it evicts the
    oldest and returns true — so the old "returns false, retry next tick"
    guard never fired: the chunks went into a 4-slot queue at once, the middle
    ones were evicted (seen on the bench 2026-10-03: 5–8 and 10 missing) and
    the monitor dropped the whole config on CRC. The receiver's status acks
    keep using `publishConfigMessage()` ungated: those must not wait.
  - **No new 8 KB statics**: they come out of the same DRAM as the heap TLS
    needs. `ConfigLoader::_currentText` (the text published as "current",
    exactly what was CRC'd) *is* `load()`'s read buffer — `load()` parses with
    `(const char*)` so ArduinoJson copies strings and frees the buffer — and
    the receiver works entirely through SD streams (`openRead`/`openWrite`/
    `copyFile`). A first draft with two extra buffers took static RAM from
    88 KB to 105 KB.
  - The board writes `config.json` **minified**: pretty-printed, a config that
    fit when sent can exceed `ConfigLoader`'s 8 KB once credentials are added
    back (`ConfigLoader::addCredentials()` restores wifi/broker exactly as the
    file had them; they never travel over MQTT).
  - Rejected while `Running`/`Starting`/`Stopping`; applied only on reboot,
    whatever causes it — **no on-screen confirmation and no "restart now"**
    (they need SquareLine; card 26). The EMQX ACL restricting who may publish
    to `config/set` is a manual console step (`docs/despliegue-monitor-web.md`).
  - Its ack *is* the confirmation the operator sees: `aceptada` = written to
    the SD. The monitor tells that apart from **vigente** (the board rebooted
    and reports that same `configId` as current) and shows both in the
    generator's sticky header (`biosoft-status` postMessage), next to Enviar —
    a status list outside the iframe went unseen.
- **Ping (`remoteping.hpp`)**: the monitor publishes `biosoft/ping {r}` every
  `BOARD_PING_SECONDS` (15) and on demand (header button, `POST
  /api/board/ping`); the board answers `biosoft/pong {r, st, cfg, mega, up}`.
  It exists because outside a run the board publishes nothing, so "En linea"
  only ever meant *the monitor* reaches the broker. Read-only by design (not a
  remote command). Same core split as config: `onMessageReceived()` queues
  (`_pingInbox`, 2 entries), `_answerPings()` publishes from `update()`.
  Neither side retains, and the server ignores a retained pong — it would claim
  a board answered that may be off for hours. Not persisted (link state, like
  the broker's). "Sin respuesta" is decided by ping **sequence number**, not
  timestamps: two pings in one millisecond tied by date. The EMQX ACL must
  allow these topics too. `web/tools/simulador-config.js` answers pings
  (`--sin-pong` to play a hung board).
- **`runType` (`"normal"` | `"test"`, top-level in the SD config, schema
  §15) is a declaration, not an operating mode.** It switches nothing on or
  off — every relaxation (detector, sources, sensors, control loop) lives in
  its own section, and there is deliberately no master key gating them. All
  it does is mark runs: `ConfigLoader::marksRunsAsTest()` (runType test, or
  any source on a `sim`/`scenario` sensor) puts `TEST` in both
  `targets` and `result` (in `result` too, so an orphan run built from a
  `result` alone keeps the mark), and `TestRunNotice` (`testrunnotice.hpp`,
  called from `setup()` after `mySystem.begin()`) draws a "MODO PRUEBA"
  strip plus a boot dialog closed with ENTENDIDO. Both live on
  **`lv_layer_top()`**, not on a screen, because screens are destroyed and
  rebuilt on navigation; with `runType: normal` nothing is created at all.
  `TEST` is **always sent, true or false** — its *absence* is what the web
  monitor reads as "firmware older than the mark", so omitting it when false
  would turn every new experiment into `unknown`. Absent or unrecognised
  `runType` ⇒ normal (logged); the generator refuses any other value. The
  emergency stop is **not** configurable in any run type, by design.
- **Each run carries `CFG` and `RLX` in `targets`** (card 23), for
  traceability. `CFG` = `ConfigLoader::configId()`: CRC32 (IEEE, via a
  `Print` that `serializeJson` feeds byte by byte — no text buffer) of the
  parsed document **after removing `wifi` and `broker`**, computed at the end
  of `load()`; `"default"` whenever the compiled defaults are what runs. Only
  the board computes it — the monitor stores it, never recomputes — and it is
  deliberately not written into the file (a hand-edited file would keep a
  stale id). `RLX` = `relaxationMask()`, one bit per relaxation, absent keys
  counted at their factory default; bit 2 (CEM1 unwatched) **is** the factory
  default, so it is recorded but excluded from the warning (`WarningMask`),
  or every experiment would be flagged. The bit order is duplicated in
  `web/client/src/lib/format.ts` (`RELAJACIONES`) — same drift hazard as the
  two `seriallink.hpp`.
- **`requireMega`** (top-level SD key, default `true`) replaced the
  compile-time `BENCH_SIN_MEGA`, which had been left at `true` in the
  production firmware. `MySystem::_benchWithoutMega()` is `!requireMega &&
  !_serial.isConnected()` — both conditions: with the Mega plugged in the
  normal flow rules even if the card says `false`. In bench mode the splash
  timeout forces `Ready`, Iniciar/Repetir go straight to Running without
  sending `start`, and Detener fabricates a local `stopped` result. With
  `requireMega` on, Iniciar/Repetir now **return early when the link is
  down** instead of sending `start` into the void and parking the operator on
  Esperando until the timeout.
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
  the `Topics` bullet below). **Every section of the schema is implemented.**
  `wifi` and `broker` (sections 13-14) are the two that are *not* applied by
  `ConfigLoader` either: it copies them into its own buffers and
  `SoftEsp32.ino` reads them when building `WiFiConfig`/`MqttConfig`, because
  both of those keep `const char*` **pointers, not copies**, so the strings
  have to outlive the `JsonDocument` they came from.
  The two override differently on purpose. `wifi` **replaces** the compiled
  list rather than adding to it — merging would mean a stale network left in
  `secrets.h` could never be removed from the card — while an empty or absent
  list falls back to the compiled ones, since having no network at all is
  worse than ignoring the section. `broker` overrides **key by key**, so the
  host can be changed without repeating user and password. The CA certificate
  deliberately stays compiled: a PEM inside JSON means escaping every newline,
  and it is the one value here that is not a single line of text.
  **This puts WiFi and broker passwords in clear text on a card that travels
  between machines**, where before they lived only inside the binary. That is
  the cost of changing networks without reflashing; `docs/config.example.json`
  therefore carries placeholders only, never real credentials.
  Note `detector.sources[].range.mode` is parsed but *not*
  forwarded: which source derives its ranges from the target and which takes
  them verbatim is fixed by design on the Mega (CEM1 derived, TEMP1 not), so
  only `criticalMultiplier` travels.
- `ConfigurationOptions` and `Intervals` (ESP32) are **no longer `constexpr`** —
  they are fixed-size static buffers plus a real length counter (`countXxx`),
  seeded with the compiled defaults and overwritten by `ConfigLoader`. Option
  `label`s are owned `char` buffers rather than `const char*` because the JSON
  strings die with the `JsonDocument` as soon as parsing ends.
  **`optionsFieldMode` is the one exception** — the experiment mode menu
  (campo X / campo nulo, feeding `start`'s `mode` param) is a fixed 2-entry
  table that the SD file cannot touch. Those two are an experimental condition
  fixed by the design, not numbers an operator calibrates; letting the file
  edit the list would allow deleting "Campo nulo" and making the control group
  unreachable with nothing to show for it. Being compile-fixed is also why its
  `value` is a plain `const char*` while every other menu owns its strings.
  Its dropdown (`ui_ConfiguracionesModoExposicionOpciones`) is wired like the
  other six in `ConfigurationController`; `buildDropdown` overwrites the
  `"NORMAL
NULO"` placeholder the SquareLine export ships with.
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
- `Topics` (`topics.hpp`) is no longer constants: telemetry is split into **six
  groups, each with its own MQTT topic**, all mutable globals seeded with the
  compiled defaults and overwritten by `ConfigLoader` from the SD `telemetry`
  section — same "applied locally" pattern as `menus`/`intervals.esp32`,
  nothing here travels to the Mega. Three are periodic and carry an interval
  (`measures`, `coils`, `status`, one `Timer` task each, registered only while
  Running and only when enabled); three are **event-driven and carry no
  interval at all** (`targets` once on entering Running, `alerts` on every
  `flag_data`, `result` from `_applyResult`). An interval would be meaningless
  for something that happens once, so the schema doesn't accept one — `enabled`
  is what turns a group off.
  The split is not cosmetic: everything in one message does not fit, and the
  cadences are genuinely different. It also means **two groups must never share
  a topic** — the Decoder would get two different JSON shapes on one
  subscription with no way to tell them apart; the generator rejects it.
  `fields` (configurable `enabled`+`name`) covers **only the five keys that
  were already the dashboard contract** before the split. Everything the new
  groups publish (`c1`..`d4`, `REASON`, `SRC`, `MODE`...) is structural — part
  of the message shape, not a knob — and is not individually configurable.
  **Topics and those five names are the contract with the dashboard**;
  renaming one without updating it makes that value silently stop arriving.
  Every group carries a `retain` flag too, so a dashboard opened mid-run gets
  the last known state instead of an empty screen — decisive for `targets` and
  `result`, which publish once per experiment. It is `false` for `alerts`
  alone: those are an event stream, and a retained alert would be handed to
  every new subscriber as if it had just happened, long after the run ended.
  Two groups are worth protecting: `targets` is the only thing that says
  whether a run is the treated or the control group (`MODE`), and `result` is
  the only thing that reports *why* an experiment ended — without it a run cut
  short by a critical alert looks, from outside, exactly like one that finished.
  `coils` publishes only the coils that have actually reported, so the
  dashboard never sees zeros from coils that aren't mounted.
  Every group funnels through `_publishTelemetry(group, doc)`, which skips a
  group that is disabled or empty and **refuses to publish a payload that
  doesn't fit `Topics::MaxPayloadLength`** instead of letting `serializeJson`
  truncate it silently (the same ignored-return-value trap as
  `SerialLink::_sendFrame`). Below that, `BrokerManager::begin()` raises
  PubSubClient's packet buffer to 512 — its 256 default was enough while
  telemetry was one 128-byte message but not for `result`, which carries the
  cut description — and `processPublishQueue()` now **drops** a message too big
  for that buffer rather than retrying it: an oversize publish fails every
  time, and leaving it queued would block every message behind it forever. A
  dropped link still keeps its messages queued, which is the case retrying is
  for.
- `MySystem::_sendMegaConfig()` (called from `onSerialConnected()`, so it
  fires on every connect *and* reconnect) generalizes that same ack/retry
  pattern to an arbitrary set of frames instead of one fixed command: up to
  21 (1 `config_intervals` + 1 `config_control` + 1 `config_detector` + 4
  `config_coil` + 2 `config_source` + 6 `config_rule` + 2 `config_scenario`
  + 4 `config_current`),
  each tracked as an entry in
  `_pendingConfig[]`, with a single `Tasks::ReSendConfig` task (not one per
  frame) sending whatever is still active until `Commands::Ack` clears it.
  Entries are matched by `command` plus a `key` that disambiguates several
  pendings of the same command (coil name, source name, `"<source>/<rule>"`,
  or `"<address>/<channel>"` for a current channel);
  `config_intervals`/`config_control` have no key since
  only one of each can be in flight. Once every entry is inactive,
  `_cancelPendingConfig()` removes the task itself.
  Note `_sendMegaConfig()` does **not** blast all 14 out at once: the
  SerialLink TX buffer (2 KB on the ESP32) fills and drains one frame per 50 ms, so
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
  classes that add behavior on top. Since the 2026-09-19 redesign they are the
  export of SquareLine projects: `ui_*Screen.c/.h` plus `ui.c`/`ui.h`. As of
  2026-09-21 five screens (Splash, Principal, Configuraciones, Resultado, and
  **Esperando**, which replaced Busy) come from the `UiFinalParte1` project
  and EnCurso from `UiFInalParte2` (sic) — `ui.c`/`ui.h` were hand-merged
  (Busy → Esperando) since neither export shipped a `ui.c`. When
  re-exporting, **set SquareLine's LVGL target to 9.1** (`platformio.ini` pins
  `lvgl@9.1.0`): an 8.3 export compiles almost everywhere thanks to
  `lv_api_map_v8.h`, but `lv_spinner_create(parent, t, angle)` and the
  `ui_img_splash_png.c` descriptor (`LV_IMG_CF_TRUE_COLOR`/`always_zero`) do
  not. The hand edit left in `ui.c` is that **`ui_init()` neither creates the
  six screens nor loads one** — both the `_screen_init()` calls and the
  `lv_disp_load_scr()` are removed, because screens are now built on demand
  (see the ScreenManager bullet above). Redo that after every re-export; if
  the calls come back, the board runs out of heap the moment TLS connects. The old `Principal2` project (`ui_Running.c` and its 11
  icons) is gone; the new design has no image assets besides the splash.
  New exports land in `esp32/refactor/` and get copied over `src/`; every
  widget the controllers touch is renamed per export (the `UiFinalParte1`
  ones are prefixed by screen: `ui_PrincipalIntensidadCampoValor`,
  `ui_ConfiguracionTolIntensidadOpciones`, `ui_ButtonResultadoPrincipal`, ...),
  so the build is what tells you which references went stale.
  `ui_ButtonResultadoRepetir` (REPETIR, on Resultado) is **not wired** — the
  layout has it, no controller handles it yet.
  The new Principal splits value and unit into separate labels
  (`ui_PrincipalIntensidadCampoValor` + `...Unidad`, hours/minutes for duration),
  so `PrincipalData` carries both the composite labels (still read by the old
  Running) and per-value strings formatted from the option's *numeric* fields
  — never parsed out of the label, which is free text once it comes from the
  SD. The "componentes" chips (`BOB 1-2`, `CEM 1`, `TEMP 1`, `SCT 1`) are
  static export text: the ESP32 has no way to know what the Mega registered.
  The Running screen is `ui_EnCursoScreen` (`RunningController`): health as
  three mutually-exclusive chips in the header (`ui_EnCursoHeaderEstadoNormal`/
  `Advertencia`/`Critico`), an Objetivos tab with the same split value+unit
  labels as Principal (`ui_EnCursoObjetivos*Valor`, tolerance included), a
  Mediciones tab with field + temperature plus **4 alert panels per
  measurement** (`ui_EnCursoMedicionesIntensidadAlertas1-4` /
  `...TempAlertas1-4`) and a coil table. The alert panels are the **last 4
  events** of that source, oldest on the left: as many shown as there are
  events (none at 0, the 4 newest from the fourth on), the rest hidden with
  `LV_OBJ_FLAG_HIDDEN`, and each one colored by *its own* type — red for
  `critical`, amber for streak/frequency. That needs a per-source event
  history, which `AlertsData` cannot give: it dedupes by source+type (one
  entry per type, carrying the accumulated count) because that is what health
  and Resultado need. So `SystemData::pushAlertEvent()` keeps a **separate,
  non-deduping** `AlertsHistoryData` (`MAX_ALERT_DOTS`=4 events for each of
  `MAX_ALERT_SOURCES`=2 sources), fed from the same `flag_data` handler and
  cleared on entering Running. Don't merge the two registers.
  The coil table shows **one row per coil that exists**, not always 4
  (`RunningController::_resolveVisibleCoils()`): the SD `coils` section wins,
  counting only the enabled ones, and with no card it falls back to the coils
  that reported in a `coil_data` frame — recomputed on the 1 s tick and not
  only in `show()`, since with no SD nothing has reported yet when the screen
  opens. With neither, no rows: how many coils there are is the Mega's fact,
  and hardcoding 2 on the ESP32 because that is what `SoftMega2560.ino`
  usually registers would duplicate it across the cable. Names come from the
  SD entry when present, else `B1`..`B4` (the export ships row 4 named "B3").
  Current and duty both come from `coil_data` and **both start at 0** instead
  of showing dashes — 0 is what the PWM is actually putting out before the
  loop starts. The bottom bar shows progress % (`ui_Label1`, unnamed in
  SquareLine) and time **elapsed** (`SystemData::elapsedSeconds()`), as its
  TRANSCURRIDO title says — the `RefactorPrincipal` layout showed remaining
  instead. Every measurement on this screen **starts at 0 on entering
  Running** (`_processState`'s Running branch resets field, temperature,
  per-coil current and duty, and their timestamps). The two measurement
  labels used to be guarded by `static float` caches initialised to `0.0f`,
  which compared equal to the data's own `0.0f` and so left the export's mock
  values ("1.0" mT, "33.6" C) on screen as if they had been measured; the
  caches are now members reset to `NAN` in `show()`. The old 3-panel Alertas tab is gone;
  `AlertsData` survives for health, the dots and Resultado.
  Resultado now has dedicated widgets (progress %, elapsed h/m, per-reason
  panel with mode + detail line, and a **mean field** the ESP32 computes by
  accumulating every `cem_data` while `Running` — `MeasuresData::
  magneticFieldSum/Samples`, reset on entering Running, frozen into
  `ResultData` with the other snapshots; shows `--` with zero samples). The
  health label has no widget in the new layout. Principal shows only the mode
  ("CAMPO X"/"CAMPO NULO"); the group label and the components chips of the
  `RefactorPrincipal` layout have no widget anymore. The Configuraciones
  "N CAMBIOS SIN GUARDAR" banner is gone from the layout too —
  `ConfigurationController::_pendingChanges()` still counts staging vs
  `_data.configuration`, but only logs it, so leaving via Volver discards
  edits silently on screen.
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
  `ResultadoController::_applyResult()` shows exactly one of the three panels
  based on `reason` (`"completed"`/`"critical"`/`"stopped"`) and **writes the
  detail line itself** from structured fields, not from the Mega's prose.
  `result_data` therefore carries, besides `reason`/`description`, the raw
  facts of the cut: `source`/`type`/`count`/`limit` when critical, and
  `emerg` (physical e-stop vs on-screen Detener) when stopped — both boards
  mirror them in their own `ResultData`, and `RuntimeState::setResult` gained
  a whole-struct overload so there is still exactly one place an experiment
  ends. That fixes three things the fixed export text got wrong: the Falla
  headline was hardcoded to "TEMP1 ALCANZO EL LIMITE CRITICO" although CEM1
  can be enabled from the SD *and* a `streak`/`frequency` limit cuts just
  like a `critical` one (see `Engine::onFlag`); an emergency stop looked
  identical to pressing Detener, because the controller discarded the one
  description that told them apart; and the detail claimed "sin alertas
  activas al detener", which is false of a run that ends with alerts
  standing. `description` still travels, as the fallback the screen uses when
  the raw fields are absent.
  `test_resultdata_fits_with_critical_fields` (native) pins the widest frame.
  **REPETIR** (`ui_ButtonResultadoRepetir`) re-runs the experiment with the
  configuration still in `_data.configuration` — the same path as Principal's
  Iniciar, through `EventName::Repeat` — and is **hidden when the run ended
  in `critical`**: repeating a configuration that just hit a limit would most
  likely hit it again, and the cause needs looking at first. Because `state_data: ready` is only
  sent on a 5s poll (`Tasks::SendState`) and arrives *after* `result_data`,
  `_processState()`'s `Ready` branch treats `ScreenType::RESULT` like
  `PRINCIPAL`/`CONFIG` (early-return, doesn't auto-navigate away) — otherwise
  that stale "ready" broadcast would silently bounce the operator back to
  Principal a few seconds after arriving, before they could read the result.
  A start the Mega **refused** (`reason: "refused"`, `cause`) is shown on the
  same Falla panel retitled **NO INICIO**, with headline/detail written from
  the cause code (`ResultadoController::_buildRefusedTexts`), progress and
  elapsed forced to zero (the getters would read the *previous* run), and
  state closed to Ready if it was Starting. REPETIR is offered only for
  **never** for a refused start (both causes are configuration, so repeating
  would refuse again). **It is not published to MQTT**: with no `targets` before it, the web monitor would build an orphan
  run from an experiment that never existed.
  Leaving Resultado is manual only, via `ui_ResultadoBtnVolver` (already built in
  SquareLine, just needed wiring) → `EventName::Back` → resets `_data.result` and
  shows `PRINCIPAL`.

### `tools/` — external configuration generator

`tools/generador-config.html` is the app that produces `/biosoft/config.json`
for the SD card (Trello card 13). It is **one self-contained HTML file with no
build step, no dependencies and no CDN**: it opens by double-click on any
machine and works offline. That is the whole point — the alternative designs
all needed a toolchain and a distribution story for what is, in the end, a form
that writes a JSON file. `tools/test-generador.js` re-runs its validation rules
in node with a stub DOM (`node tools/test-generador.js`), which is the same
"pin what breaks silently" role the `pio test -e native` suites play for the
Mega.

Two things there are worth knowing before touching it:

- **Integers and floats are not interchangeable in the file, asymmetrically.**
  Verified against the ArduinoJson 7 the firmware compiles: `is<float>()`
  accepts both `1` and `1.0`, but `is<unsigned long>()` accepts `1` and
  **rejects `1.0`**. So a float may be written as an integer, but an
  integer-typed key written with a decimal point is dropped in silence — it
  keeps the compiled default in `intervals`/`bufferSize`, and is **worse in
  `menus`**, where `ConfigLoader` reads `option["value"] | 0` and the `|`
  fallback fires precisely when `is<T>()` is false: the option lands on `0`,
  and the operator gets a menu entry of 0 ms. The generator blocks the download
  on any non-integer in an int-typed field; that check is the main reason to
  generate the file rather than hand-edit it.
- **The scenario profile shown as applied is *deduced*, not remembered**
  (`profileOf()`): it compares the sources against each profile's values, so
  it is right after loading a file or the board's config, and a profile with
  one number edited stops being marked (`custom`). The buttons mark **only
  what is being edited**; embedded, a separate block lists three stages, one
  row each — *En la placa* (current config), *Enviado* (sent, written to the
  SD, waiting for a reboot; `GET /api/config/requests` carries `content` on
  the latest request for this) and *Editando*. A board on `"default"` shows
  *Valores de fabrica* plus **why** — `ConfigLoader::loadStatus()` (`ok`/
  `nosd`/`nofile`/`unreadable`/`invalid`/`schema`) travels as `src` in the
  current-config meta and `cfgst` in the pong. `invalid`/`schema`/`unreadable`
  are warnings, not boots-as-usual: the file *is* on the card and is being
  ignored. The wording lives once, in `web/client/src/lib/format.ts`
  (`motivoConfig`), and is passed to the generator already built. They used to share the
  buttons (board as an underline) and that read as one confusing state.
- **`control.map` is validated here against the same limits as the Mega**
  (`MAX_BALANCE` mirrors `CoilChannels::MaxBalance` — same drift hazard as the two
  `seriallink.hpp`). The web copy (`client/public/`) must stay identical:
  `npm run sync:generador` after every edit.
- **The 128-byte telemetry payload cannot overflow**, so the generator
  deliberately does *not* validate it. `Topics::TelemetryField::name` is a
  `char[16]` and the topic a `char[64]`, so every key is truncated to 15
  characters no matter how long it arrives in the file; the heaviest batch
  (measures, 3 floats) then tops out at 4 + 3×(15+3+12) = 94 bytes. That makes
  `_publishTelemetry()`'s "don't publish a truncated payload" guard genuinely
  defensive — correct, but unreachable while those buffers are 16. A blocking
  error for a condition that cannot happen would be worse than none, so the
  generator only reports the computed size. If `MaxFieldNameLength` ever grows,
  that number is what says how much room is left.

### `web/` — remote monitor

Backend (Express + mqtt.js + Socket.IO + Mongoose) and frontend (React + Vite)
in one npm workspace, deployed as **a single container**: in production the
backend also serves the compiled front, from the same origin. Design and
telemetry contract are in `docs/plan-monitor-web.md`; deployment in
`docs/despliegue-monitor-web.md`.

The whole thing hangs off six MQTT topics the ESP32 already publishes
(`esp32/src/topics.hpp`). The monitor itself needs no firmware support;
the firmware changes made alongside it (telemetry cadence, broker stability,
progress clock — see the bullets below) fix what the monitor exposed.

Four decisions everything else rests on:

- **Retained messages are never persisted or emitted.** Five of the six groups
  publish with `retain`, so the broker redelivers the last one of each on
  *every* subscribe. Treating those as new events would insert duplicate
  samples with the delivery time instead of the measurement time on every
  reconnect, and — far worse — a retained `targets` would open a ghost run with
  the previous experiment's goals while a retained `result` closed the one
  actually running. `TelemetryMessage.retained` is the guard, tested in
  `test/ingesta.test.ts` and `test/corridas.test.ts`.
- **Persist first, then emit.** The handler saves and only afterwards emits the
  same object over Socket.IO, so the dashboard cannot show a measurement that
  isn't in the database. With Mongo down it saves nothing and therefore emits
  nothing.
- **Runs are inferred, not reported.** The board sends no experiment id:
  `targets` opens a run, `result` closes it, and every sample is written with
  its `runId` already set (time-series collections aren't updated afterwards —
  possible because `targets` always precedes the first sample). A run left
  without a `result` is marked `orphan` and closed with the timestamp of its
  **last data**, not of now; a `result` with no open run still creates an
  orphan, because the cut reason is the one fact nobody else reports. An orphan
  is not garbage: its samples are real measurements. See
  `src/domain/runtracker.ts`.
- **Gaps stay gaps.** The board publishes QoS 0 with no queue, so a stretch
  without data is real. The API returns no empty buckets and the client inserts
  an explicit null point so the chart breaks the line instead of drawing a
  tidy straight one across minutes nobody measured.

Other things worth knowing before touching it:

- **`MODE` is `"x"` / `"null"`, not the label.** `_publishTargets()` sends
  `optionsFieldMode[...].value`. The front asked `mode.includes('nulo')`, which
  is **false** for `"null"` — a control-group run would have displayed as
  treated, the one distinction the experiment cannot lose. It went unnoticed
  because the simulator published the label. Now `esCampoNulo()`/`nombreModo()`
  (`client/src/lib/format.ts`) are the single place that interprets it, and
  `tools/simulador.js` sends what the board sends.
- **Runs carry `runType`: `normal` / `test` / `unknown`**, from the `TEST`
  flag of `targets` (or of `result` when `targets` didn't carry one — it
  fills a missing type, never overwrites one). A run without the flag is
  `unknown`, **not** `normal`: old firmware and the old simulator don't say
  whether animals were involved. `GET /api/runs` **excludes test runs unless
  asked** (`type=test|normal|unknown|all`), so an average or comparison made
  from the history can't silently drag bench data in. `tools/simulador.js`
  publishes `TEST: true` by default (`--type normal|none` to change it):
  its data is invented, and entering the history as an experiment is exactly
  what the flag exists to prevent.
- **`targets.configId` / `targets.relaxations`** come from the board's `CFG`
  / `RLX` (card 23); `parseTargets` drops an `RLX` that isn't an integer
  0–255 rather than store it misread. A run declared `normal` with any
  warning bit gets the **CON RELAJACIONES** chip (history, detail, live);
  test runs don't, since relaxations are expected there.
  `relajacionesDe()`/`relajacionesConAviso()` are the single place that
  decodes the mask.
- **Remote config, monitor side** (`domain/configsync.ts`,
  `domain/configchunks.ts`, `api/config.ts`): config topics bypass the
  telemetry path in the ingestor because here **retained messages matter**
  (the current config is published retained; reprocessing is idempotent, keyed
  by `configId`). Reassembled text must CRC32 to the board's id or it's
  dropped — retained chunks of two configs can mix in the broker. Sending is
  one chunk at a time, waiting for the board's `parcial` ack (5 s, 3 retries,
  then `no_entregada`); one request at a time (409 otherwise). **A late ack
  is the normal case, not an edge case** (first real-board send, 2026-10-03:
  18 `parcial` for 12 chunks, then `incompleta` shown over an accepted
  file). So: an ack for another chunk keeps waiting out the same attempt
  without resending (resending cascaded — each copy produced another stray
  ack on the next chunk); an unsolicited final status only lands on a
  request still open (`enviando`/`parcial`/`no_entregada`), never over a
  board verdict; and the board re-answers `aceptada` to a chunk of the
  request it just accepted instead of `incompleta`. CRC32 and
  chunking are reimplemented (no `zlib.crc32`, which needs Node ≥ 22.2) and
  mirror `remoteconfig.hpp` — same drift hazard as the bit order of `RLX`.
  The Configuracion page **embeds the generator** (`postMessage`, same origin
  only) so validation has one source; the container is built from `web/`
  alone, so it ships a copy in `client/public/` (`npm run sync:generador`)
  and `client/src/lib/generador.test.ts` fails when copy and original differ.
  Production's `X-Frame-Options` went from `DENY` to `SAMEORIGIN` for that
  iframe. `tools/simulador-config.js` plays the board's side (current config +
  acks; `--ocupada`, `--perder N`) for testing without hardware.
- **Aggregated points carry min and max, not just the average.** A five-minute
  bucket swallows the fifteen-second temperature spike that cut the
  experiment — which is exactly what someone opens the chart to find.
- **Express 4 does not catch async handler rejections**, and Node kills the
  process on an unhandled one. With Mongo unreachable, one login attempt was
  enough to kill the server, and the platform restarting it turned that into a
  reboot loop whose cause appeared nowhere. `src/api/asincrono.ts` wraps the
  handlers, answers 503 when the database is down, and a process-level guard
  logs instead of exiting.
- **`/api/ping` is public and reports both links** (`mongo`, `mqtt` booleans,
  nothing else). `/api/health` says more but sits behind the login — and with
  the database down **you cannot log in**, so the only endpoint able to explain
  why nothing works was unreachable exactly when needed.
- **The MQTT ingestor starts independently of Mongo.** It used to wait for the
  database so it could restore the open run first; that coupling meant a
  database problem also silently stopped telemetry ingestion.
- **A PEM in an environment variable arrives with escaped newlines.** `.env`
  files hold no multi-line values, so `MQTT_CA` carries `\n` literals and
  `pemDesdeEntorno()` converts them back. Without that, TLS rejects the
  certificate with the same error as if it were absent — so it looks like the
  secret never arrived when it arrived wrong.
- `tools/simulador.js` publishes a whole run (topics, keys and retain flags
  identical to the board's) and is what makes the critical-alert cut
  reproducible without provoking it on real hardware. `tools/probar-mongo.js`
  tests a connection URI — DNS, connection, credential and **write
  permission** — before a deploy: with a read-only user the monitor starts,
  logs no error and stores nothing.

- **Roles: `admin` / `viewer`** (`models/user.ts`). The role only guards the
  three write paths — `POST /api/config/requests`, run deletion
  (`DELETE /api/runs/:id`, `POST /api/runs/delete`) and `/api/users` — via
  `requireAdmin`, which reads the role **from the DB on every request**, not
  from the JWT (demotion must not wait a week for the token to expire).
  Accounts predating roles are migrated to admin at startup
  (`cargarSesiones()`); the API keeps at least one admin and refuses
  self-demotion/self-deletion.
- **JWTs are revocable** without making `requireAuth` hit Mongo: an in-memory
  `tokensValidAfter` table (`auth/sesiones.ts`), loaded on connect and updated
  on every password change/deletion done *by this server*. `ahoraParaRevocar()`
  floors to the second because `iat` has 1 s resolution — with the exact
  instant, the token issued in the same second as the change is born revoked.
  A user missing from the table is let through (the CLI script runs in another
  process); a deleted one is marked `Infinity`. Socket.IO's handshake applies
  the same check.
- **Password links** (invitation 72 h / recovery 2 h) store only a sha256 of a
  256-bit token. Email is optional (`SMTP_URL` + `PUBLIC_URL`); the link is
  built from `PUBLIC_URL`, never from `Host` (reset poisoning). `/auth/forgot`
  answers identically whether the account exists and sends mail in background
  so timing doesn't leak it either. Without SMTP, an admin generates the link.
- **Run deletion** removes the run's measures/coils/statuses/alerts first, then
  the run; the open run (the tracker's `currentRunId`) is refused with 409.
- **En curso charts** fetch `bucket=raw` series and append socket samples;
  alerts carry `value`/`valueAt` = the last *published* measure of their source
  before the alert (`domain/alertas.ts`) — the board's alert has no value, so
  it is labelled as an approximation. Each alert is drawn only on its own
  source's chart. `client/src/lib/rangos.ts` mirrors
  `detectorconfigbuilder.hpp` (null mode centres the CEM band on 0) — same
  drift hazard as the two `seriallink.hpp`.
- **The live charts use a sliding time window** (1/5/15/60 min or the whole
  run, default 5, remembered per browser), right edge = *now*, not the last
  sample: a stalled feed shows as a growing gap instead of an axis that
  quietly stops. It is a time window, not N samples, for the same reason.
  `recortarVentana()` keeps the last point *before* the window so the line
  reaches the left edge; that only works because the X axis has
  `allowDataOverflow` — without it recharts widens the domain to fit the
  point and the window stops being fixed. The run detail page still shows
  the whole run.
- **Coil rows are filtered by the run's config** (`lib/bobinas.ts`, via the
  stored `ConfigSnapshot` of `targets.configId`): the Mega reports every
  *registered* channel, including ones the SD disables. A coil with duty but
  ~0 A is flagged "sin corriente", not hidden.
- **`targets.dur` is milliseconds** — what the board publishes
  (`optionsDuration[].duration`). The simulator used to send minutes, so the
  "N min" label looked right on simulated runs and showed `300000 min` on
  real ones. The simulator now sends ms, `normalizarDuraciones()` converts old
  minute-valued runs at startup (`dur < 1000` is never ms: the shortest menu
  entry is 60000), and the front formats it with `duracionPedida()`.
- **A `completed` run ends at 100 % and the full duration**, on both sides.
  The ESP32 used to start its progress clock on the periodic `state_data:
  running` (up to 5 s late), so a full run closed at 99 % / 4m58s. It now
  starts the clock at the start `ack` (`_startAckAt`; the Mega sends it in the
  same `loop()` pass as `_start()`), and `_applyResult()` snaps the clock to
  the full duration on `completed` before the snapshot. The monitor applies
  the same rule (`avanceFinal()`) so runs stored by older firmware read right.
- Default telemetry cadence is now **2 s / 5 s / 5 s** (measures / coils /
  status; was 30 s everywhere) — `topics.hpp`, `config.example.json`, the
  generator and the schema all carry it. The generator's errors/warnings no
  longer live in its sticky header (only a summary line does).

**Nothing here has been fed by the real board yet.** Everything verified so far
came from the simulator, which reproduces the contract as read from the
firmware. The `MODE` bug above is what that gap looks like when it bites.

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
