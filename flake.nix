{
  description = "Simple GLES3 VR-style hello world (house + cubes). Desktop Linux + Android APK.";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachSystem [ "x86_64-linux" "aarch64-linux" ] (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config.allowUnfree = true;
        };
        lib = pkgs.lib;

        # Desktop binary (OpenGL 3.3 core)
        gles-vr-hello = pkgs.stdenv.mkDerivation {
          pname = "gles-vr-hello";
          version = "0.1.0";
          src = lib.cleanSourceWith {
            src = ./.;
            filter = path: type:
              let base = baseNameOf path; in
              !(base == ".git" || base == "result" || base == "build"
                || lib.hasSuffix ".bundle" base);
          };

          nativeBuildInputs = [ pkgs.cmake pkgs.pkg-config ];
          buildInputs = [ pkgs.SDL2 pkgs.libGL ];

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
          ];

          # Ensure we can find SDL2 via pkg-config / cmake
          postPatch = ''
            # nothing needed
          '';
        };

        # Optional pure-GLES desktop build (useful for testing the ES path)
        gles-vr-hello-gles = gles-vr-hello.overrideAttrs (old: {
          pname = "gles-vr-hello-gles";
          cmakeFlags = old.cmakeFlags ++ [ "-DUSE_GLES=ON" ];
          # Mesa provides GLES
          buildInputs = old.buildInputs ++ [ pkgs.libGLU /* often pulls GLES bits */ ];
        });
      in
      {
        packages = {
          default = gles-vr-hello;
          gles-vr-hello = gles-vr-hello;
          gles-vr-hello-gles = gles-vr-hello-gles;
        };

        apps.default = {
          type = "app";
          program = "${gles-vr-hello}/bin/gles-vr-hello";
        };

        devShells.default = pkgs.mkShell {
          packages = [
            pkgs.cmake
            pkgs.pkg-config
            pkgs.SDL2
            pkgs.libGL
            pkgs.gdb
            pkgs.clang-tools
          ];
          shellHook = ''
            echo "gles-vr-hello dev shell"
            echo "  cmake -B build && cmake --build build"
            echo "  ./build/gles-vr-hello"
            echo "  nix run .   # or nix build && ./result/bin/gles-vr-hello"
          '';
        };
      });
}
