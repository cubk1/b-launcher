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
  libusb1,
  gamemode,
  xrandr,
  pciutils,
  # The narrator of the game (com.mojang:text2speech) opens libflite. flite adds about 130 MiB.
  textToSpeechSupport ? true,
  # gamemoderun for the wrapper setting of an instance, and the library that it preloads. Off by
  # default: it needs programs.gamemode.enable, and gamemode adds about 90 MiB.
  gamemodeSupport ? false,
  # The launcher finds Java in JAVA_HOME and PATH. The wrapper adds the jdks listed here
  # (e.g. [ jdk8 jdk21 ]) to PATH of the launcher.
  jdks ? [ ],
  # More libraries for LD_LIBRARY_PATH of the game, and more programs for its PATH.
  additionalLibs ? [ ],
  additionalPrograms ? [ ],
}:

let
  # Libraries that the game opens with dlopen. SDL3 (Minecraft 26.3 and later) and GLFW
  # open the windowing and audio libraries, and libusb for some controllers (the sdl3 of the
  # launcher links libusb, so it adds nothing to the closure). Then LWJGL 2 (libxxf86vm), OpenAL (libstdc++),
  # Vulkan, and the AWT fonts of Java 8 (freetype). On NixOS the
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
    b-launcher-unwrapped.libdecor'
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
    libusb1
  ]
  ++ lib.optional textToSpeechSupport flite
  ++ lib.optional gamemodeSupport gamemode.lib
  ++ additionalLibs;

  nixosLibraryPath = "${addDriverRunpath.driverLink}/lib:${lib.makeLibraryPath gameLibs}";
  # LWJGL 2 (Minecraft 1.12.2 and older) runs xrandr. OSHI runs lspci. The game inherits PATH.
  runtimePrograms = [
    xrandr
    pciutils
  ]
  ++ lib.optional gamemodeSupport gamemode
  ++ additionalPrograms;
in
symlinkJoin {
  pname = "b-launcher";
  inherit (b-launcher-unwrapped) version;

  paths = [ b-launcher-unwrapped ];

  nativeBuildInputs = [ makeBinaryWrapper ];

  # The launcher reads BLB_NIX_LIBRARY_PATH on NixOS only. Desktop shortcuts run BLB_EXECUTABLE
  # (this wrapper), because /proc/self/exe is the program in b-launcher-unwrapped.
  postBuild = ''
    wrapProgram $out/bin/b-launcher \
      --set BLB_EXECUTABLE "$out/bin/b-launcher" \
      --set BLB_NIX_LIBRARY_PATH ${lib.escapeShellArg nixosLibraryPath} \
      --prefix PATH : ${lib.escapeShellArg (lib.makeBinPath (jdks ++ runtimePrograms))}
  '';

  passthru = {
    unwrapped = b-launcher-unwrapped;
    inherit
      gameLibs
      jdks
      nixosLibraryPath
      runtimePrograms
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
