{
  lib,
  stdenv,
  fetchFromGitHub,
  applyPatches,
  cmake,
  ninja,
  pkg-config,
  makeBinaryWrapper,
  python3,
  curl,
  zlib,
  minizip-ng,
  reproc,
  nlohmann_json,
  simdjson,
  sdl3,
  libdecor,
  writeShellScriptBin,
  freetype,
  fontconfig,
  # The short git hash for the 关于 page. The Nix build has no .git, so flake.nix gives it.
  gitCommit ? "",
}:

let
  # nixpkgs has no libzt, so cmake/dependencies.cmake downloads it with FetchContent. The Nix
  # build has no network, so FETCHCONTENT_SOURCE_DIR_LIBZT points at this copy. FetchContent
  # does not run PATCH_COMMAND for a local source dir, so patch it here.
  libzt = applyPatches {
    src = fetchFromGitHub {
      owner = "zerotier";
      repo = "libzt";
      rev = "a707ea6ae0910efdc1125d04758c411e2e9ea4f9";
      fetchSubmodules = true;
      hash = "sha256-7XlMdyyo49fS1mXFll6SM0u5GH+HzxuDYbIpjjRJyvs=";
    };
    nativeBuildInputs = [ cmake ];
    postPatch = ''
      cmake -DSOURCE_DIR=. -DCOMPILER_ID=${if stdenv.cc.isClang then "Clang" else "GNU"} \
        -DCOMPILER_VERSION=${stdenv.cc.version} -P ${../cmake/patch_libzt.cmake}
    '';
  };
in
# FetchContent skips GIT_TAG once FETCHCONTENT_SOURCE_DIR_LIBZT is set.
assert lib.assertMsg (lib.hasInfix libzt.rev (
  builtins.readFile ../cmake/dependencies.cmake
)) "nix/unwrapped.nix: update libzt to the GIT_TAG in cmake/dependencies.cmake";
stdenv.mkDerivation (finalAttrs: {
  pname = "b-launcher-unwrapped";
  version = lib.head (
    builtins.match ".*project\\(b-launcher VERSION ([0-9.]+).*" (builtins.readFile ../CMakeLists.txt)
  );

  src = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.unions [
      ../CMakeLists.txt
      ../cmake
      ../src
      ../assets
      ../third_party
    ];
  };

  __structuredAttrs = true;
  strictDeps = true;

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    (python3.withPackages (ps: [ ps.fonttools ]))
  ]
  ++ lib.optional stdenv.hostPlatform.isDarwin makeBinaryWrapper;

  buildInputs = [
    curl
    zlib
    minizip-ng
    reproc
    nlohmann_json
    simdjson
    finalAttrs.passthru.sdl3'
    freetype
  ]
  # Other builds open fontconfig with dlopen, and macOS uses CoreText.
  ++ lib.optional stdenv.hostPlatform.isLinux fontconfig;

  cmakeFlags = [
    (lib.cmakeBool "FETCHCONTENT_FULLY_DISCONNECTED" true)
    (lib.cmakeFeature "FETCHCONTENT_SOURCE_DIR_LIBZT" "${libzt}")
    (lib.cmakeBool "BLB_SYSTEM_LIBS" true)
    # Nix updates the package.
    (lib.cmakeBool "BLB_UPDATE_CHECK" false)
    (lib.cmakeFeature "BLB_GIT_COMMIT" gitCommit)
  ];

  # CMakeLists.txt installs the app bundle to Applications, which is what Finder and Launchpad
  # run. A wrapper script, not a symlink, because the bundle is found from the executable path.
  postFixup = lib.optionalString stdenv.hostPlatform.isDarwin ''
    mkdir -p $out/bin
    makeWrapper "$out/Applications/B Launcher.app/Contents/MacOS/b-launcher" $out/bin/b-launcher
  '';

  passthru = {
    # The default sdl3 links every backend (SDL_DEPS_SHARED is off) and hardcodes zenity, which
    # pulls in gtk4, libadwaita and GStreamer (about 900 MiB). The launcher opens one window,
    # plays sounds and shows a tray icon and file dialogs:
    # - File dialogs go through xdg-desktop-portal; zenity is a fallback found in PATH.
    # - Audio goes through PulseAudio (pipewire-pulse) or ALSA, not native PipeWire or JACK.
    # The GTK plugin of libdecor pulls in gtk3 (about 200 MiB). The cairo plugin draws the same
    # title bar. package.nix gives this libdecor to the game too.
    libdecor' = libdecor.overrideAttrs (old: {
      mesonFlags = old.mesonFlags ++ [ (lib.mesonEnable "gtk" false) ];
    });
    # On macOS, the sdl3 of Nixpkgs is already small, and its binary cache has it.
    sdl3' =
      if stdenv.hostPlatform.isLinux then
        sdl3.override {
          libdecor = finalAttrs.passthru.libdecor';
          zenity = writeShellScriptBin "zenity" ''exec zenity "$@"'';
          pipewireSupport = false;
          jackSupport = false;
        }
      else
        sdl3;
  };

  meta = {
    description = "Lightweight Minecraft launcher written in C++";
    homepage = "https://github.com/cubk1/b-launcher";
    license = lib.licenses.mit;
    mainProgram = "b-launcher";
    platforms = lib.platforms.linux ++ lib.platforms.darwin;
  };
})
