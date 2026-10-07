# AGENTS.md — gles-vr-hello

## Project
Simple GLES3 / OpenGL 3.3 “VR-style” hello world: first-person view of a house
made of cubes plus a few colored cubes on a green floor. Desktop Linux is the
primary, easily testable target. Side-by-side stereo (key `V`) approximates
phone VR / Cardboard / Daydream-style viewing for testing.

Google Daydream itself is discontinued (SDK archived 2019, services gone for
new users). This project deliberately stays free of the archived GVR SDK so it
stays buildable. A real Daydream APK would need the old NDK SDK + services.

## Goals
- Compile and run under NixOS / Linux with a normal desktop GL context.
- Same codebase path ready for GLES3 (Android / pure ES).
- Minimal dependencies: SDL2 + system OpenGL / GLES.
- GPLv3+, REUSE-friendly SPDX headers.

## Layout
- `src/` — main, renderer, scene, math
- `include/` — headers
- `flake.nix` — desktop package + devShell
- Android packaging is intentionally minimal / future work (see TODO.md)

## Conventions
- Author: Ingo Ruhnke <grumbel@gmail.com>
- Co-authored-by: Grok <grok@x.ai> on agent commits
- Deliverables: git bundles only (see session rules)
- Prefer correct design over quick hacks
