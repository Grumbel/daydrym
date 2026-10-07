# gles-vr-hello

Tiny first-person GLES3 / OpenGL 3.3 demo: stand in front of a simple house
built from cubes, with a few colored cubes on a green floor.

Intended as a “hello world” starting point for phone-VR style experiments
(side-by-side stereo). Google Daydream is discontinued; this project does
**not** depend on the archived GVR SDK.

## Desktop (Nix)

```bash
nix run github:…/gles-vr-hello          # once published
# or from a checkout:
nix build
./result/bin/gles-vr-hello
```

Controls:

- **WASD** — move
- **Mouse** — look
- **Space / Ctrl** — up / down
- **V** — toggle side-by-side stereo
- **Esc** — quit

## Status

Desktop Linux works. Android APK packaging and real Cardboard/Daydream
integration are tracked in `TODO.md`.

## License

GPL-3.0-or-later. See `LICENSE` and SPDX headers in source files.
