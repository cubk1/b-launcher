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
    in
    {
      overlays.default = final: prev: {
        b-launcher = final.callPackage ./nix/package.nix { };
      };

      packages = forAllSystems (pkgs: rec {
        b-launcher = pkgs.callPackage ./nix/package.nix { };
        default = b-launcher;
      });

      devShells = forAllSystems (
        pkgs:
        let
          b-launcher = self.packages.${pkgs.stdenv.hostPlatform.system}.b-launcher;
        in
        {
          # The dev build downloads its dependencies with FetchContent as usual:
          #   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
          #   cmake --build build --target b-launcher
          default = pkgs.mkShell {
            inputsFrom = [ b-launcher ];
            packages = [
              pkgs.git
              pkgs.gdb
            ];
            # SDL3 and fontconfig are opened with dlopen, so ./build/b-launcher needs these.
            LD_LIBRARY_PATH = "${pkgs.addDriverRunpath.driverLink}/lib:${pkgs.lib.makeLibraryPath b-launcher.runtimeLibs}";
            BLB_NIXOS_JAVA = b-launcher.nixosJavaHomes;
            BLB_NIXOS_LIBRARY_PATH = b-launcher.nixosLibraryPath;
            BLB_NIXOS_PATH = b-launcher.nixosPath;
          };
        }
      );

      formatter = forAllSystems (pkgs: pkgs.nixfmt-tree);
    };
}
