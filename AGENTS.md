# AGENTS.md — daydrym

## Project
GLES3/OpenGL 3.3 VR-style hello world with Blinn-Phong lighting, shadow maps, and textured geometry: first-person view of a house
made of cubes plus a few colored cubes on a green floor. Desktop Linux is the
primary, easily testable target. Side-by-side stereo (key `V`) approximates
phone VR / Cardboard / Daydream-style viewing for testing.

Google Daydream itself is discontinued (SDK archived 2019), but the Lenovo Mirage
Solo still ships working Google VR Services. A plain 2D/SDL window is *not*
shown on the headset's lenses (the VR compositor only takes buffers submitted
through GVR), so the Android build uses the archived Google VR NDK
(`sdk-base-1.200.0.aar`, pinned and fetched by `flake.nix`). The desktop build
stays SDL2-only.

## Goals
- Compile and run under NixOS / Linux with a normal desktop GL context.
- Same codebase path ready for GLES3 (Android / pure ES).
- Minimal dependencies: SDL2 + system OpenGL / GLES (desktop); GVR NDK (Android).
- GPLv3+, REUSE-friendly SPDX headers.

## Layout
- `src/` — main (desktop/SDL), gvr_app (Android/GVR JNI), renderer, scene, math
- `include/` — headers
- `flake.nix` — desktop package + devShell
- Android: `nix build .#daydrym-android` → `result/daydrym.apk` (see `nix/android.nix`)

## Conventions
- Author: Ingo Ruhnke <grumbel@gmail.com>
- Co-authored-by: Grok <grok@x.ai> on agent commits
- Deliverables: git bundles only (see session rules)
- Prefer correct design over quick hacks
