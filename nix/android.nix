# Android APK packaging for daydrym (SDL2 + GLES3 + SBS stereo).
# Produces a debug-signed APK for arm64-v8a (and optionally armeabi-v7a).
{ pkgs
, src
, sdl2Src
, cardboardSrc
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

    SDL_SRC="${sdl2Src}"
    WORK="$PWD/android-build"
    mkdir -p "$WORK"
    cp -a "$SDL_SRC" "$WORK/SDL2"
    chmod -R u+w "$WORK/SDL2"

    # Newer NDKs mark ALooper_pollAll as unavailable; prefer pollOnce (SDL 2.32+
    # already has this, but keep a safety rewrite for older trees).
    find "$WORK/SDL2" -type f -name '*.c' -print0 | xargs -0 sed -i       's/ALooper_pollAll(/ALooper_pollOnce(/g' || true

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
      CC="$TOOLCHAIN/bin/''${TARGET_TRIPLE}''${API}-clang"
      CXX="$TOOLCHAIN/bin/''${TARGET_TRIPLE}''${API}-clang++"
      AR="$TOOLCHAIN/bin/llvm-ar"
      RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
      STRIP="$TOOLCHAIN/bin/llvm-strip"

      SDL_BUILD="$WORK/sdl-$ABI"
      SDL_INSTALL="$WORK/sdl-install-$ABI"
      mkdir -p "$SDL_BUILD" "$SDL_INSTALL"

      # Use the NDK-provided toolchain file so ANDROID_NDK / cpu-features resolve.
      TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake"
      test -f "$TOOLCHAIN_FILE"

      export ANDROID_NDK_HOME="$NDK"
      export ANDROID_NDK="$NDK"
      export ANDROID_NDK_ROOT="$NDK"

      cmake -S "$WORK/SDL2" -B "$SDL_BUILD" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -DANDROID_ABI="$ABI" \
        -DANDROID_PLATFORM="android-$API" \
        -DANDROID_STL=c++_shared \
        -DCMAKE_BUILD_TYPE=Release \
        -DSDL_SHARED=ON \
        -DSDL_STATIC=OFF \
        -DSDL_TEST=OFF \
        -DSDL_ANDROID_JAR=OFF \
        -DCMAKE_INSTALL_PREFIX="$SDL_INSTALL"
      cmake --build "$SDL_BUILD" --target install

      # --- Google Cardboard open-source SDK ---
      CB_SRC="$WORK/cardboard"
      if [ ! -d "$CB_SRC" ]; then
        cp -a "${cardboardSrc}" "$CB_SRC"
        chmod -R u+w "$CB_SRC"
      fi
      # Version script exports Unity/Vulkan plugin entry points that are only
      # defined when those optional sources are compiled. Strip them so a
      # pure GLES Cardboard build can link.
      if [ -f "$CB_SRC/sdk/cardboard_api.lds" ]; then
        sed -i \
          -e '/JNI_OnLoad/d' \
          -e '/RenderAPI_Vulkan_OnPluginLoad/d' \
          -e '/\*Unity\*/d' \
          "$CB_SRC/sdk/cardboard_api.lds"
      fi
      CB_BUILD="$WORK/cardboard-$ABI"
      CB_INSTALL="$WORK/cardboard-install-$ABI"
      mkdir -p "$CB_BUILD" "$CB_INSTALL/include" "$CB_INSTALL/lib"
      cmake -S "$CB_SRC/sdk" -B "$CB_BUILD" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -DANDROID_ABI="$ABI" \
        -DANDROID_PLATFORM="android-$API" \
        -DANDROID_STL=c++_shared \
        -DCMAKE_BUILD_TYPE=Release \
        -DCARDBOARDSDK_RENDERING_VULKAN=OFF \
        -DCARDBOARDSDK_UNITY_PLUGIN=OFF \
        -DCARDBOARDSDK_RENDERING_GLESv3=ON
      cmake --build "$CB_BUILD" --target GfxPluginCardboard
      # Install headers + library (target name varies)
      cp -a "$CB_SRC/sdk/include/." "$CB_INSTALL/include/" 2>/dev/null || true
      # Public header often at sdk/include/cardboard.h or packaging
      find "$CB_SRC" -name 'cardboard.h' -exec cp -v {} "$CB_INSTALL/include/" \;
      find "$CB_BUILD" -name '*.so' -exec cp -v {} "$CB_INSTALL/lib/" \;
      find "$CB_BUILD" -name '*.a' -exec cp -v {} "$CB_INSTALL/lib/" \;

      APP_BUILD="$WORK/app-$ABI"
      mkdir -p "$APP_BUILD"
      cmake -S "$SRC_ROOT" -B "$APP_BUILD" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -DANDROID_ABI="$ABI" \
        -DANDROID_PLATFORM="android-$API" \
        -DANDROID_STL=c++_shared \
        -DCMAKE_BUILD_TYPE=Release \
        -DSDL2_ANDROID_PREFIX="$SDL_INSTALL" \
        -DDAYDRYM_USE_CARDBOARD=ON \
        -DCARDBOARD_PREFIX="$CB_INSTALL"
      cmake --build "$APP_BUILD"

      LIBDIR="$WORK/apk/lib/$ABI"
      mkdir -p "$LIBDIR"
      cp -v "$SDL_INSTALL/lib/"*.so "$LIBDIR/" || true
      # CMake may put libmain.so in app build dir
      find "$APP_BUILD" -name 'libmain.so' -exec cp -v {} "$LIBDIR/" \;
      find "$CB_INSTALL/lib" -name '*.so' -exec cp -v {} "$LIBDIR/" \; || true
      # libc++_shared must match the ABI (do not pick armeabi for arm64)
      case "$ABI" in
        arm64-v8a)   CPP_ARCH="aarch64-linux-android" ;;
        armeabi-v7a) CPP_ARCH="arm-linux-androideabi" ;;
        x86_64)      CPP_ARCH="x86_64-linux-android" ;;
        *)           CPP_ARCH="$TARGET_TRIPLE" ;;
      esac
      CPP_SHARED=$(find "$NDK/toolchains/llvm/prebuilt/$HOST_TAG/sysroot/usr/lib/$CPP_ARCH" -name 'libc++_shared.so' 2>/dev/null | head -1 || true)
      if [ -z "$CPP_SHARED" ]; then
        CPP_SHARED=$(find "$NDK" -path "*$CPP_ARCH*" -name 'libc++_shared.so' 2>/dev/null | head -1 || true)
      fi
      if [ -n "$CPP_SHARED" ]; then
        cp -v "$CPP_SHARED" "$LIBDIR/"
      else
        echo "WARNING: libc++_shared.so not found for $CPP_ARCH"
      fi
    done

    # --- Java: SDL Android sources + our activity ---
    JAVA_OUT="$WORK/java"
    CLASSES="$WORK/classes"
    mkdir -p "$JAVA_OUT" "$CLASSES"
    cp -a "$WORK/SDL2/android-project/app/src/main/java/org" "$JAVA_OUT/"
    cp -a "$SRC_ROOT/mk/android/app/src/main/java/com" "$JAVA_OUT/"

    find "$JAVA_OUT" -name '*.java' > "$WORK/sources.list"
    javac --release 11 -encoding UTF-8 -cp "$ANDROID_JAR" -d "$CLASSES" @"$WORK/sources.list"

    # jar → dex
    JAR="$WORK/classes.jar"
    jar cf "$JAR" -C "$CLASSES" .
    mkdir -p "$WORK/dex"
    d8 --min-api ${toString 24} --output "$WORK/dex" "$JAR"
    # d8 outputs classes.dex in the output dir
    cp "$WORK/dex/classes.dex" "$WORK/apk/classes.dex"

    # Resources + manifest
    mkdir -p "$WORK/apk/res"
    cp -a "$SRC_ROOT/mk/android/app/src/main/res/." "$WORK/apk/res/"
    cp "$SRC_ROOT/mk/android/app/src/main/AndroidManifest.xml" "$WORK/apk/AndroidManifest.xml"

    # Package APK with aapt
    AAPT="$BUILD_TOOLS/aapt"
    UNSIGNED="$WORK/daydrym-unsigned.apk"
    "$AAPT" package -f -M "$WORK/apk/AndroidManifest.xml" \
      -S "$WORK/apk/res" \
      -I "$ANDROID_JAR" \
      -F "$UNSIGNED" \
      --min-sdk-version 24 \
      --target-sdk-version 34

    # Add native libs + dex
    cd "$WORK/apk"
    ${pkgs.zip}/bin/zip -u "$UNSIGNED" classes.dex
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
