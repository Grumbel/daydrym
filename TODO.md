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
- [ ] Android APK target (SDL2 + NDK, reuse patterns from pingus `nix/android.nix`)
  - Side-by-side already present; add Android.mk / manifest / APK packaging
  - Optional: open-source Cardboard SDK for distortion + head tracking
- [ ] Per-cube model matrices so the floating cubes can spin independently
- [ ] Simple textured floor or skybox (still keep deps low)
- [ ] True Daydream path only if someone still has working hardware + GVR Services

## Build / run (desktop)
```bash
nix build
./result/bin/gles-vr-hello

# or
nix develop
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/gles-vr-hello
```

Controls: WASD move, mouse look, Space/Ctrl up/down, V stereo, Esc quit.

## Bundle history
- Base commit: 96e286e48440f72cfdfbb4f9077ced058b5dfdd5 (96e286e)
- Current tip: same (initial)
- Bundle: gles-vr-hello-001.1-initial-desktop-96e286e.bundle
