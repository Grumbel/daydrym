# daydrym

Tiny first-person GLES3 / OpenGL 3.3 demo: a house built from cubes, a table
piled with random cubes, and a few spinning cubes on a green floor, with
Blinn-Phong lighting, a shadow map and a procedural checkerboard texture.

A "hello world" starting point for VR experiments. It runs on the desktop
(SDL2) and on the **Lenovo Mirage Solo** (Google Daydream) through the archived
Google VR NDK; the same GVR build also works in Cardboard mode on phones.

![daydrym on the desktop](docs/screenshot.png)

*Desktop build (the headset renders the same scene, once per eye).*

## Desktop (Nix)

```bash
nix run github:…/daydrym          # once published
# or from a checkout:
nix build
./result/bin/daydrym
```

`nix build` (or `cmake --install`) also installs the desktop integration:
`share/applications/daydrym.desktop`, `share/metainfo/daydrym.metainfo.xml`
and the scalable icon `share/icons/hicolor/scalable/apps/daydrym.svg`
(sources in `data/`). The same icon is rendered into the Android launcher icons.

Controls:

- **WASD** — move
- **Mouse** — look
- **Space / Ctrl** — up / down
- **V** — cycle stereo modes (mono → SBS → SBS swapped → anaglyph red/cyan)
- **Esc** — release the mouse (click the window to grab it again)
- **Q** — quit (or close the window)

## Android / Daydream (Mirage Solo)

```bash
nix build .#daydrym-android        # → result/daydrym.apk (debug-signed, arm64-v8a)
adb install -r result/daydrym.apk  # uninstall first if the debug signature changed
adb shell am start -n com.daydrym/.DaydrymActivity \
    -a android.intent.action.MAIN -c com.google.intent.category.DAYDREAM
```

It also appears in the Daydream home. Requires Android 7.0+ (API 24) and
OpenGL ES 3.0.

Why GVR: the headset display belongs to the VR compositor, which only takes
buffers submitted through GVR. A plain SDL window ends up in the small
"2D in VR" panel, and a VR-mode surface without GVR is black. The SDK
(`sdk-base-1.200.0.aar`) is pinned and fetched by `flake.nix`; at runtime the
real implementation comes from Google VR Services.

Daydream controller:

- **Pointer** — a laser ray follows the controller (arm model)
- **Click / trigger** — teleport to where the ray meets the floor
- **Touchpad drag** — walk relative to where you are looking
- **App button** — toggle night / day lighting

The controller model lights up the touchpad (with a dot where you touch) and
buttons as you use them. See `mk/android/README.md` for packaging details.

## Status

- Desktop Linux works.
- Android (Mirage Solo, Android 8.0) works: stereo rendering, head tracking,
  controller.
- Not done: Cardboard tap-to-teleport fallback (no controller on phones),
  true 6DoF position, a textured floor / skybox. See `TODO.md`.

## License

GPL-3.0-or-later. See `LICENSES/` and the SPDX headers in source files.

## OpenXR (Linux desktop)

Optional path for a real HMD via any OpenXR runtime (Monado, SteamVR, …).
The Android/Daydream build is unchanged (Google VR NDK).

```bash
nix build
./result/bin/daydrym --openxr
```

Requirements at **runtime** (not linked into the binary):

1. An OpenXR runtime and its JSON manifest
2. Typically X11 or XWayland (graphics binding is `XrGraphicsBindingOpenGLXlibKHR`)

**Monado on NixOS:**

```nix
services.monado = {
  enable = true;
  defaultRuntime = true;
};
```

Or ad-hoc:

```bash
monado-service &
export XR_RUNTIME_JSON=/path/to/openxr_monado.json
./result/bin/daydrym --openxr
```

Build without OpenXR: `cmake -DDAYDRYM_OPENXR=OFF …`.

### SteamVR + Nix (`libuuid` / runtime load failures)

`nix run` / Nix-built binaries use the Nix dynamic linker. SteamVR’s OpenXR
runtime (`vrclient.so`) is a non-Nix library and often fails with errors like
`libuuid.so.1: cannot open shared object file` when the loader tries to activate
`~/.config/openxr/1/active_runtime.json`.

**Preferred on NixOS:** use Monado as the active runtime (see above).

**Keep SteamVR:** run inside an FHS environment so Steam’s dependencies resolve:

```bash
steam-run $(nix build -o result .#daydrym && echo result/bin/daydrym) --openxr
# or
nix run .#daydrym-steamvr -- --openxr
# equivalent:
#   nix build .#daydrym && steam-run ./result/bin/daydrym --openxr
```

`XR_RUNTIME_JSON` always overrides the active_runtime.json selection if you want
Monado without changing the SteamVR default for other apps.

**SteamVR must be running** before `xrCreateInstance` succeeds. Start SteamVR
(and wake the headset / streaming client), then:

```bash
# Official runtime manifest (recommended over a hand-written JSON):
ln -sf ~/.steam/steam/steamapps/common/SteamVR/steamxr_linux64.json \
  ~/.config/openxr/1/active_runtime.json

nix run .#daydrym-steamvr -- --openxr
# equivalent:
#   nix build .#daydrym && steam-run ./result/bin/daydrym --openxr
```

If `xrCreateInstance` still returns `XR_ERROR_RUNTIME_FAILURE` (-2), check
`~/.steam/steam/logs/vrserver.txt`. A line like
`sRuntimePathFromServer … mismatches sRuntimePathFromClient …/bin/` means the
active runtime JSON is wrong — use `steamxr_linux64.json` as above.

Also try the full Steam FHS explicitly:

```bash
nix build .#daydrym
XR_RUNTIME_JSON=…/SteamVR/steamxr_linux64.json steam-run ./result/bin/daydrym --openxr
```

`daydrym-steamvr` is a thin wrapper around `steam-run` for the same effect.

