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
`mega2560/src/seriallink.hpp` (and `commands.hpp`) are **intentionally duplicated,
byte-for-byte, in each project**. When changing the serial protocol, edit both
copies and keep them in sync manually.

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
`SignalGenerator`, `PwmDriver`, `FieldController`, `RelayManager`, `Detector`).
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
  `PwmDriver` based on live magnetometer readings.
- `Detector` — evaluates per-source (`CEM1`, `TEMP1`, ...) rules (critical
  threshold, streak, frequency) from sensor sample history and raises `onFlag`
  events; `Engine::_evaluateHealthData()` turns per-source scores into an overall
  health verdict (`optimal`/`good`/`warning`/`bad`/`critical`).
- Remote commands `start`/`stop`/`reset` drive `Engine::_start()`/`_stop()`/
  `_reset()`, each acknowledged back to the ESP32 via an `ack` command.
- `Engine::_start()` sets `_isSettlingTime = true` and `onStart()` schedules the
  `Tasks::SettlingTime` timer (`Intervals::SettlingTime`, 10s) to clear it; while
  set, sensor samples are recorded but withheld from `_detector.addSample(...)`
  (see `onThermometerSample`/`onMagnetometerSample`/`onCurrentSensorSample`), so
  no flags can fire during that initial window. If this timer task isn't
  scheduled, the flag stays true forever and the Detector silently never
  evaluates anything for the rest of the run — always verify `onStart()` still
  adds `Tasks::SettlingTime` after touching that code path.

Several code paths (`SetTarget`, `ConfigSource`) are present but commented out —
the protocol supports them but the ESP32 side does not yet drive them. Check
before assuming a command name is unused. Per-flag telemetry (`flag_data`,
`Commands::OneFlagsData`) *is* wired end to end: the Mega's `Detector::onFlag`
sends one `flag_data` per event, and the ESP32 renders the last 3 into the
Running screen's Alertas tab (see below).

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
- Commands with retry: `start`/`stop`/`reset` are re-sent on a timer
  (`Tasks::ReSendStart` etc.) until the Mega's `ack` arrives, then the resend task
  is cancelled — see `onCommand`'s handling of `Commands::Ack`.
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

### Style notes specific to this codebase

- Nearly everything is header-only (`.hpp` with `inline` method bodies defined
  right after the class), Arduino-sketch style — there are no corresponding
  `.cpp` files for these classes.
- Heavy use of the listener/observer pattern for decoupling between managers;
  when adding behavior, look for the relevant `*Listener` interface before adding
  direct calls between classes.
- Extensive `Serial.print`/`printf` tracing (especially around `start`/`ack`
  handling) is intentional for on-device debugging over USB serial — match this
  style when touching those code paths rather than stripping it out.
