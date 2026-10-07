# TODO.md

## Current tip
Initial hello-world: desktop OpenGL 3.3 + optional GLES path, simple house + cubes scene, mouse-look + WASD, side-by-side stereo toggle (`V`).

## Done
- [x] Minimal math (Vec3 / Mat4)
- [x] GLES3 / GL 3.3 renderer (colored triangles)
- [x] Scene: floor, house (base+roof+door), decorative cubes
- [x] First-person camera + stereo side-by-side
- [x] Nix flake for x86_64-linux / aarch64-linux desktop binary + devShell
- [x] SPDX headers, AGENTS.md, this TODO

## Open / next
- [x] Android APK target (SDL2 + NDK, SBS stereo default, sensor look)
  - [x] Per-cube model matrices so the floating cubes can spin independently
- [x] Blinn-Phong lighting (directional + ambient + specular)
- [x] Procedural checkerboard albedo texture with UVs
- [x] Directional shadow map (2048, 3x3 PCF)
- [ ] Simple textured floor or skybox (still keep deps low)
- [x] Daydream path via GVR NDK (Mirage Solo)

## Build / run (desktop)
```bash
nix build
./result/bin/daydrym

# or
nix develop
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/daydrym
```

Controls: WASD move, mouse look, Space/Ctrl up/down, V stereo, Esc release mouse (click regrabs), Ctrl+Q quit.

## Android / Mirage Solo notes
- Plain SDL/2D windows show up in the "2D in VR" virtual display, and a VR-mode
  surface without GVR is black: `vr_flinger` only composites DVR buffers, and
  `libdvr.so` is not a public library. Hence the GVR NDK.
- Device quirk: developer option "Don't keep activities" (`always_finish_activities`)
  destroys the activity on every app switch; the app then restarts from scratch.
- Head pose is GVR rotation + neck model (apparent position is the neck-model offset,
  not true Mirage Solo 6DoF); controller input is not used yet.

## Bundle history
- Base commit: 96e286e48440f72cfdfbb4f9077ced058b5dfdd5 (96e286e)
- Current tip: drop removed SDL_HINT_ANDROID_SEPARATE_MOUSE_AND_TOUCH
- Bundle: daydrym-023.1-fix-sdl-hint-96e286e.bundle
  (supersedes 022.1; full history from base)
