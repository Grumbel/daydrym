# Android APK packaging for daydrym (SDL2 + GLES3 + SBS stereo).
# Produces a debug-signed APK for arm64-v8a (and optionally armeabi-v7a).
{ pkgs
, src
, sdl2Src
, version ? "0.1.0"
}:

let
  lib = pkgs.lib;
  jdk = pkgs.jdk17;

  androidComposition = pkgs.androidenv.composeAndroidPackages {
    cmdLineToolsVersion = "11.0";
    toolsVersion = "26.1.1";
    platformToolsVersion = "34.0.5";
    buildToolsVersions = [ "34.0.0" ];
    includeEmulator = false;
    platformVersions = [ "34" ];
    includeSources = false;
    includeSystemImages = false;
    systemImageTypes = [ ];
    abiVersions = [ "arm64-v8a" ];
    includeNDK = true;
    ndkVersions = [ "26.1.10909125" ];
    useGoogleAPIs = false;
    useGoogleTVAddOns = false;
  };

  sdk = androidComposition.androidsdk;
  ndk = "${sdk}/libexec/android-sdk/ndk-bundle";
  # Some nixpkgs layouts use ndk/<version>
  ndkAlt = "${sdk}/libexec/android-sdk/ndk/26.1.10909125";

  buildTools = "${sdk}/libexec/android-sdk/build-tools/34.0.0";
  androidJar = "${sdk}/libexec/android-sdk/platforms/android-34/android.jar";

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

  buildPhase = ''
    set -euo pipefail
    export PATH="${buildTools}:${jdk}/bin:$PATH"

    NDK="${ndk}"
    if [ ! -d "$NDK" ]; then
      NDK="${ndkAlt}"
    fi
    if [ ! -d "$NDK" ]; then
      # Find any NDK under the SDK
      NDK=$(find "$ANDROID_HOME/ndk" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | head -1 || true)
    fi
    echo "Using NDK at $NDK"
    test -d "$NDK"

    HOST_TAG=linux-x86_64
    TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/$HOST_TAG"
    API=24

    SDL_SRC="${sdl2Src}"
    WORK="$PWD/android-build"
    mkdir -p "$WORK"
    cp -a "$SDL_SRC" "$WORK/SDL2"
    chmod -R u+w "$WORK/SDL2"

    # --- Build SDL2 + daydrym for each ABI ---
    for ABI in ${lib.concatStringsSep " " abis}; do
      case "$ABI" in
        arm64-v8a)
          TARGET_TRIPLE=aarch64-linux-android
          ;;
        armeabi-v7a)
          TARGET_TRIPLE=armv7a-linux-androideabi
          ;;
        *)
          echo "unsupported ABI $ABI"; exit 1
          ;;
      esac

      SYSROOT="$TOOLCHAIN/sysroot"
      CC="$TOOLCHAIN/bin/${TARGET_TRIPLE}''${API}-clang"
      CXX="$TOOLCHAIN/bin/${TARGET_TRIPLE}''${API}-clang++"
      AR="$TOOLCHAIN/bin/llvm-ar"
      RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
      STRIP="$TOOLCHAIN/bin/llvm-strip"

      SDL_BUILD="$WORK/sdl-$ABI"
      SDL_INSTALL="$WORK/sdl-install-$ABI"
      mkdir -p "$SDL_BUILD" "$SDL_INSTALL"

      cmake -S "$WORK/SDL2" -B "$SDL_BUILD" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=Android \
        -DCMAKE_ANDROID_ARCH_ABI="$ABI" \
        -DCMAKE_ANDROID_NDK="$NDK" \
        -DCMAKE_ANDROID_API="$API" \
        -DCMAKE_ANDROID_STL_TYPE=c++_shared \
        -DANDROID=ON \
        -DSDL_SHARED=ON \
        -DSDL_STATIC=OFF \
        -DSDL_TEST=OFF \
        -DSDL_ANDROID_JAR=OFF \
        -DCMAKE_INSTALL_PREFIX="$SDL_INSTALL"
      cmake --build "$SDL_BUILD" --target install

      APP_BUILD="$WORK/app-$ABI"
      mkdir -p "$APP_BUILD"
      cmake -S "$src" -B "$APP_BUILD" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=Android \
        -DCMAKE_ANDROID_ARCH_ABI="$ABI" \
        -DCMAKE_ANDROID_NDK="$NDK" \
        -DCMAKE_ANDROID_API="$API" \
        -DCMAKE_ANDROID_STL_TYPE=c++_shared \
        -DANDROID=ON \
        -DSDL2_ANDROID_PREFIX="$SDL_INSTALL"
      cmake --build "$APP_BUILD"

      LIBDIR="$WORK/apk/lib/$ABI"
      mkdir -p "$LIBDIR"
      cp -v "$SDL_INSTALL/lib/"*.so "$LIBDIR/" || true
      # CMake may put libmain.so in app build dir
      find "$APP_BUILD" -name 'libmain.so' -exec cp -v {} "$LIBDIR/" \;
      # libc++_shared
      CPP_SHARED=$(find "$NDK/toolchains/llvm/prebuilt/$HOST_TAG/sysroot/usr/lib" -name 'libc++_shared.so' | grep "$TARGET_TRIPLE\|aarch64\|arm" | head -1 || true)
      if [ -z "$CPP_SHARED" ]; then
        CPP_SHARED=$(find "$NDK" -name 'libc++_shared.so' | grep "$ABI\|aarch64-linux-android" | head -1 || true)
      fi
      if [ -n "$CPP_SHARED" ]; then
        cp -v "$CPP_SHARED" "$LIBDIR/"
      fi
    done

    # --- Java: SDL Android sources + our activity ---
    JAVA_OUT="$WORK/java"
    CLASSES="$WORK/classes"
    mkdir -p "$JAVA_OUT" "$CLASSES"
    cp -a "$WORK/SDL2/android-project/app/src/main/java/org" "$JAVA_OUT/"
    cp -a "$src/mk/android/app/src/main/java/com" "$JAVA_OUT/"

    find "$JAVA_OUT" -name '*.java' > "$WORK/sources.list"
    javac --release 11 -cp "${androidJar}" -d "$CLASSES" @"$WORK/sources.list"

    # jar → dex
    JAR="$WORK/classes.jar"
    jar cf "$JAR" -C "$CLASSES" .
    mkdir -p "$WORK/dex"
    d8 --min-api ${toString 24} --output "$WORK/dex" "$JAR"
    # d8 outputs classes.dex in the output dir
    cp "$WORK/dex/classes.dex" "$WORK/apk/classes.dex"

    # Resources + manifest
    mkdir -p "$WORK/apk/res"
    cp -a "$src/mk/android/app/src/main/res/." "$WORK/apk/res/"
    cp "$src/mk/android/app/src/main/AndroidManifest.xml" "$WORK/apk/AndroidManifest.xml"

    # Package APK with aapt
    AAPT="${buildTools}/aapt"
    UNSIGNED="$WORK/daydrym-unsigned.apk"
    "$AAPT" package -f -M "$WORK/apk/AndroidManifest.xml" \
      -S "$WORK/apk/res" \
      -I "${androidJar}" \
      -F "$UNSIGNED" \
      --min-sdk-version 24 \
      --target-sdk-version 34

    # Add native libs + dex
    cd "$WORK/apk"
    ${pkgs.zip}/bin/zip -u "$UNSIGNED" classes.dex
    find lib -type f -name '*.so' | ${pkgs.zip}/bin/zip -u "$UNSIGNED" -@

    # Align + sign with debug key
    ALIGNED="$WORK/daydrym-aligned.apk"
    "${buildTools}/zipalign" -f 4 "$UNSIGNED" "$ALIGNED"

    KEYSTORE="$WORK/debug.keystore"
    keytool -genkeypair -v -keystore "$KEYSTORE" -storepass android \
      -alias androiddebugkey -keypass android -keyalg RSA -keysize 2048 \
      -validity 10000 -dname "CN=Android Debug,O=Android,C=US" 2>/dev/null || true

    SIGNED="$WORK/daydrym.apk"
    "${buildTools}/apksigner" sign \
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
