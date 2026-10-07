# Android packaging

`nix build .#daydrym-android` builds a debug-signed APK (`arm64-v8a`).

Layout:

- `app/src/main/AndroidManifest.xml` — landscape, GLES3, Cardboard/Daydream intent categories
- `app/src/main/java/com/daydrym/DaydrymActivity.java` — thin `SDLActivity` subclass
- Native code is the same C++ tree; CMake builds `libmain.so` when `ANDROID` is set

There is **no** archived Google VR / Daydream SDK dependency. Stereo is application-side SBS; head look uses the accelerometer via SDL.
