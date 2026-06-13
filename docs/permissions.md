# Keebtype Permissions

Keebtype needs global keyboard events so it can play sounds while another app is
focused. That is a sensitive permission class. The app intentionally keeps the
privacy boundary narrow:

- no typed text is stored
- no ordered key history is stored
- no aggregate typing stats are stored
- key events are only converted into sound requests

## macOS

The macOS build uses a listen-only Quartz event tap. It does not modify,
suppress, or repost input.

If the app cannot start the event tap, grant permission in System Settings:

```text
System Settings
  -> Privacy & Security
  -> Accessibility
  -> enable Keebtype or the terminal/runner used to launch it
```

Depending on macOS version and launch method, you may also need:

```text
System Settings
  -> Privacy & Security
  -> Input Monitoring
  -> enable Keebtype or the terminal/runner used to launch it
```

For local development, macOS often associates the permission with the terminal
application or IDE that launched the binary, not with the unsigned binary alone.

## Windows

The Windows build uses a low-level keyboard hook. It returns every event to the
next hook with `CallNextHookEx`; it does not suppress input.

## Linux

Linux global input is stubbed in v1. The app can build and run, but it will not
capture global typing. This avoids pretending that X11 and Wayland have the
same security and permission model.

