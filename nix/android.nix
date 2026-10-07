# Android APK packaging for daydrym (Google VR NDK + GLES3).
# Produces a debug-signed APK for arm64-v8a (and optionally armeabi-v7a).
{ pkgs
, src
, gvrAar
, version ? "0.1.0"
}:

let
  lib = pkgs.lib;
  jdk = pkgs.jdk17;

  androidComposition = pkgs.androidenv.composeAndroidPackages {
    platformToolsVersion = "37.0.1";
    buildToolsVersions = [ "35.0.0" "34.0.0" ];
    includeEmulator = false;
    platformVersions = [ "35" "34" ];
    includeSources = false;
    includeSystemImages = false;
    systemImageTypes = [ ];
    abiVersions = [ "arm64-v8a" ];
    includeNDK = true;
    useGoogleAPIs = false;
    useGoogleTVAddOns = false;
  };

  sdk = androidComposition.androidsdk;

  abis = [ "arm64-v8a" ];
in
pkgs.stdenv.mkDerivation {
  pname = "daydrym-android";
  inherit version src;

  nativeBuildInputs = [
    jdk
    pkgs.cmake
    pkgs.ninja
    pkgs.which
    pkgs.python3
    pkgs.unzip
    pkgs.zip
  ];

  # The SDK is large; reference it rather than copying.
  ANDROID_HOME = "${sdk}/libexec/android-sdk";
  ANDROID_SDK_ROOT = "${sdk}/libexec/android-sdk";
  JAVA_HOME = jdk.home;

  # This derivation is a custom Android pipeline; do not let stdenv/cmake
  # auto-configure the desktop CMakeLists.txt (that path needs pkg-config).
  dontConfigure = true;
  dontInstall = true;

  buildPhase = '' 
    set -euo pipefail
    export PATH="${jdk}/bin:$PATH"

    # Unpacked flake source lives in $PWD after the unpack phase.
    SRC_ROOT="$PWD"
    if [ ! -f "$SRC_ROOT/CMakeLists.txt" ]; then
      # Fallback: src attribute path
      SRC_ROOT="${src}"
    fi
    echo "Source root: $SRC_ROOT"
    test -f "$SRC_ROOT/CMakeLists.txt"

    # Resolve build-tools (prefer highest version directory)
    BUILD_TOOLS=$(ls -d "$ANDROID_HOME/build-tools"/* 2>/dev/null | sort -V | tail -1)
    echo "Using build-tools at $BUILD_TOOLS"
    test -d "$BUILD_TOOLS"
    export PATH="$BUILD_TOOLS:$PATH"

    # Resolve android.jar platform
    ANDROID_JAR=$(ls -d "$ANDROID_HOME/platforms"/android-* 2>/dev/null | sort -V | tail -1)/android.jar
    echo "Using platform jar $ANDROID_JAR"
    test -f "$ANDROID_JAR"

    # Resolve NDK
    NDK=""
    if [ -d "$ANDROID_HOME/ndk-bundle" ]; then
      NDK="$ANDROID_HOME/ndk-bundle"
    fi
    if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
      NDK=$(ls -d "$ANDROID_HOME/ndk"/* 2>/dev/null | sort -V | tail -1 || true)
    fi
    echo "Using NDK at $NDK"
    test -d "$NDK"

    HOST_TAG=linux-x86_64
    TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/$HOST_TAG"
    API=24

    WORK="$PWD/android-build"
    mkdir -p "$WORK"

    # --- Google VR SDK (headers, libgvr.so, Java classes, resources) ---
    GVR_AAR="$WORK/gvr-aar"
    mkdir -p "$GVR_AAR"
    unzip -q "${gvrAar}" -d "$GVR_AAR"
    GVR_PREFIX="$WORK/gvr"
    mkdir -p "$GVR_PREFIX/include" "$GVR_PREFIX/lib"
    cp -a "$GVR_AAR/headers/." "$GVR_PREFIX/include/"

    TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake"
    test -f "$TOOLCHAIN_FILE"
    export ANDROID_NDK_HOME="$NDK" ANDROID_NDK="$NDK" ANDROID_NDK_ROOT="$NDK"

    for ABI in ${lib.concatStringsSep " " abis}; do
      LIBDIR="$WORK/apk/lib/$ABI"
      mkdir -p "$LIBDIR"
      cp "$GVR_AAR/jni/$ABI/libgvr.so" "$GVR_PREFIX/lib/libgvr.so"

      APP_BUILD="$WORK/app-$ABI"
      cmake -S "$SRC_ROOT" -B "$APP_BUILD" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -DANDROID_ABI="$ABI" \
        -DANDROID_PLATFORM="android-$API" \
        -DANDROID_STL=c++_shared \
        -DCMAKE_BUILD_TYPE=Release \
        -DGVR_PREFIX="$GVR_PREFIX"
      cmake --build "$APP_BUILD"

      cp -v "$APP_BUILD/libdaydrym.so" "$LIBDIR/"
      cp -v "$GVR_AAR/jni/$ABI/libgvr.so" "$LIBDIR/"
      # libc++_shared must match the ABI (do not pick armeabi for arm64)
      case "$ABI" in
        arm64-v8a)   CPP_ARCH="aarch64-linux-android" ;;
        armeabi-v7a) CPP_ARCH="arm-linux-androideabi" ;;
        *) echo "unsupported ABI $ABI"; exit 1 ;;
      esac
      cp -v "$TOOLCHAIN/sysroot/usr/lib/$CPP_ARCH/libc++_shared.so" "$LIBDIR/"
    done

    # --- Resources: app + GVR library resources, R.java for both packages ---
    mkdir -p "$WORK/apk/res" "$WORK/gen"
    cp -a "$SRC_ROOT/mk/android/app/src/main/res/." "$WORK/apk/res/"
    cp "$SRC_ROOT/mk/android/app/src/main/AndroidManifest.xml" "$WORK/apk/AndroidManifest.xml"

    AAPT="$BUILD_TOOLS/aapt"
    UNSIGNED="$WORK/daydrym-unsigned.apk"
    "$AAPT" package -f -m --auto-add-overlay \
      -M "$WORK/apk/AndroidManifest.xml" \
      -S "$GVR_AAR/res" -S "$WORK/apk/res" \
      -I "$ANDROID_JAR" \
      -J "$WORK/gen" \
      --extra-packages com.google.vr.cardboard \
      -F "$UNSIGNED" \
      --min-sdk-version 24 \
      --target-sdk-version 26

    # --- Java: our activity + GVR classes + generated R → dex ---
    CLASSES="$WORK/classes"
    mkdir -p "$CLASSES"
    find "$SRC_ROOT/mk/android/app/src/main/java" "$WORK/gen" -name '*.java' > "$WORK/sources.list"
    javac --release 11 -encoding UTF-8 \
      -cp "$ANDROID_JAR:$GVR_AAR/classes.jar" -d "$CLASSES" @"$WORK/sources.list"

    mkdir -p "$WORK/dex"
    jar cf "$WORK/classes.jar" -C "$CLASSES" .
    d8 --min-api 24 --lib "$ANDROID_JAR" --output "$WORK/dex" \
      "$WORK/classes.jar" "$GVR_AAR/classes.jar"
    cp "$WORK/dex"/classes*.dex "$WORK/apk/"

    # Add native libs + dex
    cd "$WORK/apk"
    ${pkgs.zip}/bin/zip -u "$UNSIGNED" classes*.dex
    find lib -type f -name '*.so' | ${pkgs.zip}/bin/zip -u "$UNSIGNED" -@

    # Align + sign with debug key
    ALIGNED="$WORK/daydrym-aligned.apk"
    "$BUILD_TOOLS/zipalign" -f 4 "$UNSIGNED" "$ALIGNED"

    KEYSTORE="$WORK/debug.keystore"
    keytool -genkeypair -v -keystore "$KEYSTORE" -storepass android \
      -alias androiddebugkey -keypass android -keyalg RSA -keysize 2048 \
      -validity 10000 -dname "CN=Android Debug,O=Android,C=US" 2>/dev/null || true

    SIGNED="$WORK/daydrym.apk"
    "$BUILD_TOOLS/apksigner" sign \
      --ks "$KEYSTORE" --ks-pass pass:android \
      --key-pass pass:android \
      --out "$SIGNED" "$ALIGNED"

    mkdir -p $out
    cp "$SIGNED" $out/daydrym.apk
    echo "Built $out/daydrym.apk"
  ''; 

  installPhase = ''
    # already installed in buildPhase
    true
  '';

  # Don't try to fixup Android .so / APK with patchelf
  dontFixup = true;
  dontStrip = true;
}
