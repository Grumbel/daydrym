{
  description = "daydrym — GLES3/OpenGL VR-style hello world (house + cubes). Desktop + Android APK.";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    sdl2-src = {
      url = "https://github.com/libsdl-org/SDL/releases/download/release-2.32.8/SDL2-2.32.8.tar.gz";
      flake = false;
    };
    cardboard-src = {
      url = "github:googlevr/cardboard";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, flake-utils, sdl2-src, cardboard-src }:
    flake-utils.lib.eachSystem [ "x86_64-linux" "aarch64-linux" ] (system:
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
          buildInputs = [ pkgs.SDL2 pkgs.libGL ];
          cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" ];
        };

        daydrym-gles = daydrym.overrideAttrs (old: {
          pname = "daydrym-gles";
          cmakeFlags = old.cmakeFlags ++ [ "-DUSE_GLES=ON" ];
        });

        daydrym-android = import ./nix/android.nix {
          inherit pkgs src;
          sdl2Src = sdl2-src;
          cardboardSrc = cardboard-src;
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

        apps.default = {
          type = "app";
          program = "${daydrym}/bin/daydrym";
        };

        devShells.default = pkgs.mkShell {
          packages = [
            pkgs.cmake
            pkgs.pkg-config
            pkgs.SDL2
            pkgs.libGL
            pkgs.gdb
            pkgs.clang-tools
            pkgs.jdk17
          ];
          shellHook = ''
            echo "daydrym dev shell"
            echo "  nix build .#daydrym"
            echo "  nix build .#daydrym-android   # APK (arm64-v8a, debug-signed)"
          '';
        };
      });
}
