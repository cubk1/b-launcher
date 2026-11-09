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

      packages = forAllSystems (
        pkgs:
        let
          pkgs' = pkgs.extend self.overlays.default;
        in
        {
          inherit (pkgs') b-launcher b-launcher-unwrapped;
          default = pkgs'.b-launcher;
        }
      );

      checks = forAllSystems (pkgs: {
        inherit (self.packages.${pkgs.stdenv.hostPlatform.system}) b-launcher b-launcher-unwrapped;
      });

      devShells = forAllSystems (
        pkgs:
        let
          b-launcher = self.packages.${pkgs.stdenv.hostPlatform.system}.b-launcher;
        in
        {
          # The dev build uses the libraries of the shell. FetchContent downloads libzt:
          #   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
          #   cmake --build build --target b-launcher
          default = pkgs.mkShell {
            inputsFrom = [ b-launcher.unwrapped ];
            # The jdks and programs that the package wrapper adds to PATH, for ./build/b-launcher.
            packages = [
              pkgs.git
              pkgs.gdb
            ]
            ++ b-launcher.jdks
            ++ b-launcher.runtimePrograms;
            env = {
              # The default value of the CMake option BLB_SYSTEM_LIBS.
              BLB_SYSTEM_LIBS = "ON";
              # The value that the package wrapper gives to the launcher.
              BLB_NIX_LIBRARY_PATH = b-launcher.nixosLibraryPath;
            };
          };
        }
      );

      formatter = forAllSystems (pkgs: pkgs.nixfmt-tree);
    };
}
