# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>
{
  description = "daydrym — GLES3/OpenGL VR-style hello world (desktop + optional OpenXR + Android/GVR)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config = {
            allowUnfree = true;
            android_sdk.accept_license = true;
          };
        };
        lib = pkgs.lib;

        src = lib.cleanSourceWith {
          src = ./.;
          filter = path: type:
            let base = baseNameOf path; in
            !(base == ".git" || base == "result" || base == "build"
              || lib.hasSuffix ".bundle" base);
        };

        daydrym = pkgs.stdenv.mkDerivation {
          pname = "daydrym";
          version = "0.1.0";
          inherit src;
          nativeBuildInputs = [ pkgs.cmake pkgs.pkg-config ];
          buildInputs = [
            pkgs.SDL2
            pkgs.libGL
            pkgs.libx11
            pkgs.openxr-loader
          ];
          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
            "-DDAYDRYM_OPENXR=ON"
          ];
        };

        daydrym-gles = daydrym.overrideAttrs (old: {
          pname = "daydrym-gles";
          cmakeFlags = old.cmakeFlags ++ [ "-DUSE_GLES=ON" ];
        });

        # Archived Google VR SDK (headers, libgvr.so, Java classes), pinned.
        gvrAar = pkgs.fetchurl {
          url = "https://raw.githubusercontent.com/googlevr/gvr-android-sdk/f6b00dea8dc6a6f8b5e8da8897ade615b170b088/libraries/sdk-base-1.200.0.aar";
          hash = "sha256-86aWE/1BSQPqEWZLgu5CW6ETQiveFTwwtQTXbrBHpbU=";
        };

        daydrym-android = import ./nix/android.nix {
          inherit pkgs src;
          inherit gvrAar;
          version = "0.1.0";
        };
      in
      {
        packages = {
          default = daydrym;
          daydrym = daydrym;
          daydrym-gles = daydrym-gles;
          daydrym-android = daydrym-android;
        };

        apps = rec {
          default = daydrym;
          daydrym = {
            type = "app";
            program = "${self.packages.${system}.daydrym}/bin/daydrym";
          };
          # Same program with an OpenGL ES 3.0 context (the Android render path).
          daydrym-gles = {
            type = "app";
            program = "${self.packages.${system}.daydrym-gles}/bin/daydrym";
          };
        };

        devShells.default = pkgs.mkShell {
          packages = [
            pkgs.cmake
            pkgs.pkg-config
            pkgs.SDL2
            pkgs.libGL
            pkgs.libx11
            pkgs.openxr-loader
            pkgs.monado  # optional runtime for local --openxr testing
            pkgs.gdb
            pkgs.clang-tools
            pkgs.jdk17
          ];
          shellHook = ''
            echo "daydrym dev shell"
            echo "  nix build .#daydrym"
            echo "  ./result/bin/daydrym           # desktop window"
            echo "  ./result/bin/daydrym --openxr  # OpenXR (needs runtime)"
            echo "  # Monado example:"
            echo "  #   monado-service &"
            echo "  #   export XR_RUNTIME_JSON=\$(dirname \$(dirname \$(which monado-service)))/share/openxr/1/openxr_monado.json"
            echo "  #   ./result/bin/daydrym --openxr"
            echo "  nix build .#daydrym-android   # APK (arm64-v8a, debug-signed)"
          '';
        };
      });
}
