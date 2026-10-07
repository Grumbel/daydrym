# Android packaging

`nix build .#daydrym-android` builds a debug-signed APK (`arm64-v8a`).

Layout:

- `app/src/main/AndroidManifest.xml` — landscape, GLES3, Daydream + Cardboard intent categories,
  `enableVrMode` pointing at the Google VR Services listener
- `app/src/main/java/com/daydrym/DaydrymActivity.java` — hosts a `GvrLayout` + `GLSurfaceView`
- `src/gvr_app.cpp` — JNI renderer: GVR swap chain, head pose, per-eye views of the shared scene

The Google VR SDK (`sdk-base-1.200.0.aar`: headers, `libgvr.so`, Java classes,
resources) is fetched by `flake.nix`. At runtime the real implementation is loaded
from Google VR Services (VrCore), so it works on Daydream-ready phones and the
Mirage Solo, and in Cardboard mode on other phones.

Launch: from the Daydream home, or
`adb shell am start -n com.daydrym/.DaydrymActivity -a android.intent.action.MAIN -c com.google.intent.category.DAYDREAM`
