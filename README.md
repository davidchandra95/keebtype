# Keebtype

Keebtype is a local C++ tray app that plays mechanical keyboard switch sounds
when the user types. The tray menu exposes an enabled toggle, volume levels,
soundpack selection/import/delete, and quit. The first version ships one
soundpack:

```text
soundpacks/cherrymx-brown-abs
```

The bundled pack is the Mechvibes
[CherryMX Brown - ABS keycaps](https://mechvibes.com/sound-packs/sound-pack-1200000000005/)
pack. Keebtype's import format is intentionally compatible with extracted
Mechvibes `config-v1` single-sprite soundpacks from
[mechvibes.com/sound-packs](https://mechvibes.com/sound-packs/). Credit for the
soundpack ecosystem and community soundpacks belongs to Mechvibes and the
pack authors.

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

You can also pass a soundpack directory explicitly. This is a one-run override:
it does not import the pack and does not change the persisted tray selection.

```sh
./build/keebtype /absolute/path/to/soundpacks/cherrymx-brown-abs
```

## Soundpacks

Keebtype supports two soundpack locations:

```text
packaged app / repo
  soundpacks/
    cherrymx-brown-abs/
      config.json
      sound.ogg

per-user app data
  Keebtype/
    settings.json
    soundpacks/
      imported-pack-id/
        config.json
        sound.ogg
```

Imported packs are copied into per-user app data:

| Platform | Imported soundpack directory |
|---|---|
| macOS | `~/Library/Application Support/Keebtype/soundpacks` |
| Windows | `%APPDATA%\Keebtype\soundpacks` |
| Linux | `$XDG_CONFIG_HOME/Keebtype/soundpacks` or `~/.config/Keebtype/soundpacks` |

The selected soundpack ID is stored in `settings.json` next to the imported
soundpack directory. Volume is still in-memory for now.

### Supported Import Format

This pass supports extracted Mechvibes `config-v1` single-sprite folders:

```text
some-soundpack/
  config.json
  sound.ogg
```

The `config.json` must use one OGG sound sprite and array-based slice
definitions:

```json
{
  "id": "sound-pack-id",
  "name": "Soundpack Name",
  "version": 1,
  "key_define_type": "single",
  "sound": "sound.ogg",
  "defines": {
    "57": [1000, 120]
  }
}
```

Keebtype rejects unsupported formats before import:

```text
accepted:
  config-v1 single sprite
  sound: "sound.ogg"
  defines: { "keyCode": [offset_ms, duration_ms] }

rejected:
  zip files; extract them first
  config-v2+ packs
  key_define_type: "multi"
  per-key audio files such as { "57": "space.ogg" }
  non-OGG primary sound files
  sound paths that are absolute or escape the pack directory
  invalid OGG files or slices that exceed decoded audio bounds
```

### Tray Management

Use the tray menu:

```text
Keebtype tray
  Enabled
  Volume
  Soundpacks
    CherryMX Brown - ABS keycaps
    Imported Pack
    Import Soundpack Folder...
    Delete Imported Soundpack
      Imported Pack
  Quit
```

Selecting a soundpack swaps audio at runtime and persists the selected
soundpack ID. Importing a folder validates the pack, copies it into app data,
selects it, and persists that selection. Deleting is only available for imported
packs; bundled packs are read-only. If the active imported pack is deleted,
Keebtype switches back to the bundled default.

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
`[offset_ms, duration_ms]` slices inside `sound.ogg`. The OGG is decoded when a
pack is selected and then held in memory as PCM. Playback uses a fixed-size
voice pool so the audio callback does not allocate. The current volume is
applied in the audio callback through an atomic gain value, so changing it from
the tray does not block playback.

Runtime soundpack changes create and validate the next audio engine before it
becomes active:

```text
tray selects/imports pack
    |
    v
load config.json and validate format
    |
    v
decode sound.ogg and resolve slice bounds
    |
    v
start replacement audio engine
    |
    v
atomically publish replacement for key callbacks
```

## Permissions

See [docs/permissions.md](docs/permissions.md).
