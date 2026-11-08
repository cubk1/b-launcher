{
  description = "B Launcher, a lightweight Minecraft launcher";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
      # The 关于 page shows the short git hash. A dirty tree shows the hash of HEAD, as a CMake build does.
      gitCommit = self.shortRev or (nixpkgs.lib.removeSuffix "-dirty" (self.dirtyShortRev or ""));
    in
    {
      overlays.default = final: prev: {
        b-launcher-unwrapped = final.callPackage ./nix/unwrapped.nix { inherit gitCommit; };
        b-launcher = final.callPackage ./nix/package.nix { };
      };

      packages = forAllSystems (pkgs: rec {
        b-launcher-unwrapped = pkgs.callPackage ./nix/unwrapped.nix { inherit gitCommit; };
        b-launcher = pkgs.callPackage ./nix/package.nix { inherit b-launcher-unwrapped; };
        default = b-launcher;
      });

      devShells = forAllSystems (
        pkgs:
        let
          b-launcher = self.packages.${pkgs.stdenv.hostPlatform.system}.b-launcher;
        in
        {
          # The dev build uses the libraries of the shell. FetchContent downloads simdjson and libzt:
          #   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
          #   cmake --build build --target b-launcher
          default = pkgs.mkShell {
            inputsFrom = [ b-launcher.unwrapped ];
            packages = [
              pkgs.git
              pkgs.gdb
            ];
            # The default value of the CMake option BLB_SYSTEM_LIBS.
            BLB_SYSTEM_LIBS = "ON";
            # The values that the package wrapper gives to the launcher, for ./build/b-launcher.
            BLB_NIXOS_JAVA = b-launcher.nixosJavaHomes;
            BLB_NIXOS_LIBRARY_PATH = b-launcher.nixosLibraryPath;
            BLB_NIXOS_PATH = b-launcher.nixosPath;
          };
        }
      );

      formatter = forAllSystems (pkgs: pkgs.nixfmt-tree);
    };
}
