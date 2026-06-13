# Keebtype

Keebtype is a local C++ tray app that plays mechanical keyboard switch sounds
when the user types. The tray menu exposes an enabled toggle, volume levels,
and quit. The first version ships one soundpack:

```text
soundpacks/cherrymx-brown-abs
```

The app does not store typed text, key history, or aggregate typing stats. A
global keydown event is mapped to a soundpack key ID, queued for playback, and
discarded. Volume is an in-memory tray setting for this MVP.

## Current Platform Scope

| Platform | Status | Notes |
|---|---|---|
| macOS | Implemented | Uses a listen-only Quartz event tap and an AppKit status item. Requires input/accessibility permission. |
| Windows | Implemented | Uses a low-level keyboard hook and Shell notification icon. Input is never suppressed. |
| Linux | Stub | Builds and runs, but global input capture is intentionally deferred. |

Linux is not just a missing implementation detail. Full global key capture has
different behavior across X11 and Wayland, and Wayland generally pushes this
through compositor/portal policy instead of allowing arbitrary key capture.

## Build

Requirements:

- CMake 3.20+
- C++20 compiler
- Git, because CMake fetches pinned dependency revisions

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

On macOS, the default build creates an app bundle:

```sh
open build/Keebtype.app
```

You can also run the executable inside the bundle directly:

```sh
./build/Keebtype.app/Contents/MacOS/Keebtype
```

On Windows and Linux, run the executable from the build directory:

```sh
./build/keebtype
```

You can also pass a soundpack directory explicitly:

```sh
./build/keebtype /absolute/path/to/soundpacks/cherrymx-brown-abs
```

## Packaging

Local macOS DMG build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cpack --config build/CPackConfig.cmake -G DragNDrop
```

Tag releases are built by GitHub Actions. Pushing a tag like `v0.1.0` creates a
GitHub Release with unsigned macOS and Windows installers plus SHA256 checksum
files.

These v1 artifacts are unsigned. macOS users may see Gatekeeper warnings, and
Windows users may see SmartScreen warnings until code signing is added.

## Runtime Shape

```text
global keydown
    |
    v
platform input monitor
    |
    |  maps native key code to soundpack key ID
    v
audio engine
    |
    |  chooses configured sound slice or fallback slice
    v
lock-free request queue
    |
    v
audio callback mixes active voices
```

The soundpack is a sound sprite: `config.json` maps key IDs to
`[offset_ms, duration_ms]` slices inside `sound.ogg`. The OGG is decoded once at
startup into PCM. Playback uses a fixed-size voice pool so the audio callback
does not allocate. The current volume is applied in the audio callback through
an atomic gain value, so changing it from the tray does not block playback.

## Permissions

See [docs/permissions.md](docs/permissions.md).
