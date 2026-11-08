{
  lib,
  symlinkJoin,
  makeBinaryWrapper,
  b-launcher-unwrapped,
  addDriverRunpath,
  libx11,
  libxext,
  libxcursor,
  libxi,
  libxfixes,
  libxrandr,
  libxrender,
  libxscrnsaver,
  libxtst,
  libxcb,
  libxxf86vm,
  wayland,
  libxkbcommon,
  libdecor,
  libGL,
  dbus,
  udev,
  alsa-lib,
  libpulseaudio,
  fontconfig,
  freetype,
  zlib,
  stdenv,
  vulkan-loader,
  flite,
  xrandr,
  pciutils,
  # The launcher finds Java in JAVA_HOME and PATH. Java homes listed here (e.g. [ jdk8 jdk21 ])
  # are found too. For the same Java version, the launcher uses them before JAVA_HOME and PATH.
  jdks ? [ ],
  # More libraries for LD_LIBRARY_PATH of the game, and more programs for its PATH.
  additionalLibs ? [ ],
  additionalPrograms ? [ ],
}:

let
  # Libraries that the game opens with dlopen. SDL3 (Minecraft 26.3 and later) and GLFW
  # open the windowing and audio libraries. Then LWJGL 2 (libxxf86vm), OpenAL (libstdc++),
  # Vulkan, the narrator (flite), and the AWT fonts of Java 8 (freetype). On NixOS the
  # launcher puts them in LD_LIBRARY_PATH of the game, after the GPU driver.
  gameLibs = [
    libx11
    libxext
    libxcursor
    libxi
    libxfixes
    libxrandr
    libxrender
    libxscrnsaver
    libxtst
    libxcb
    libxxf86vm
    wayland
    libxkbcommon
    libdecor
    libGL
    dbus
    udev
    alsa-lib
    libpulseaudio
    fontconfig
    freetype
    zlib
    (lib.getLib stdenv.cc.cc)
    vulkan-loader
    flite
  ]
  ++ additionalLibs;

  nixosJavaHomes = lib.concatMapStringsSep ":" (jdk: jdk.home) jdks;
  nixosLibraryPath = "${addDriverRunpath.driverLink}/lib:${lib.makeLibraryPath gameLibs}";
  # LWJGL 2 (Minecraft 1.12.2 and older) runs xrandr. OSHI runs lspci.
  nixosPath = lib.makeBinPath (
    [
      xrandr
      pciutils
    ]
    ++ additionalPrograms
  );
in
symlinkJoin {
  pname = "b-launcher";
  inherit (b-launcher-unwrapped) version;

  paths = [ b-launcher-unwrapped ];

  nativeBuildInputs = [ makeBinaryWrapper ];

  # The launcher reads BLB_NIXOS_* on NixOS only. Desktop shortcuts run BLB_EXECUTABLE (this
  # wrapper), because /proc/self/exe is the program in b-launcher-unwrapped.
  postBuild = ''
    wrapProgram $out/bin/b-launcher \
      --set BLB_EXECUTABLE "$out/bin/b-launcher" \
      --set BLB_NIXOS_JAVA ${lib.escapeShellArg nixosJavaHomes} \
      --set BLB_NIXOS_LIBRARY_PATH ${lib.escapeShellArg nixosLibraryPath} \
      --set BLB_NIXOS_PATH ${lib.escapeShellArg nixosPath}
  '';

  passthru = {
    unwrapped = b-launcher-unwrapped;
    inherit
      gameLibs
      jdks
      nixosJavaHomes
      nixosLibraryPath
      nixosPath
      ;
  };

  meta = {
    inherit (b-launcher-unwrapped.meta)
      description
      homepage
      license
      mainProgram
      platforms
      ;
  };
}
