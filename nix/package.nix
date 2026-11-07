{
  lib,
  stdenv,
  fetchurl,
  fetchFromGitHub,
  cmake,
  ninja,
  pkg-config,
  python3,
  wayland-scanner,
  patchelf,
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
  wayland,
  libxkbcommon,
  libdecor,
  libGL,
  dbus,
  udev,
  alsa-lib,
  libpulseaudio,
  ibus,
  fontconfig,
  freetype,
  zlib,
  addDriverRunpath,
  libxxf86vm,
  vulkan-loader,
  flite,
  xrandr,
  pciutils,
  # NixOS uses local Java. Java homes listed here (e.g. [ jdk21 ]) are found too.
  jdks ? [ ],
}:

let
  # Sources that cmake/dependencies.cmake downloads with FetchContent.
  # The Nix build has no network, so we fetch them here and point
  # FETCHCONTENT_SOURCE_DIR_<NAME> at the unpacked copies.
  # Keep URLs and hashes in sync with cmake/dependencies.cmake.
  deps = {
    zlib = fetchurl {
      url = "https://github.com/zlib-ng/zlib-ng/archive/refs/tags/2.3.3.tar.gz";
      sha256 = "f9c65aa9c852eb8255b636fd9f07ce1c406f061ec19a2e7d508b318ca0c907d1";
    };
    mbedtls = fetchurl {
      url = "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2";
      sha256 = "a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6";
    };
    curl = fetchurl {
      url = "https://github.com/curl/curl/releases/download/curl-8_23_0/curl-8.23.0.tar.xz";
      sha256 = "8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5";
    };
    minizip = fetchurl {
      url = "https://github.com/zlib-ng/minizip-ng/archive/refs/tags/4.2.2.tar.gz";
      sha256 = "71af7b9799856d8b03619df3949e9c1be9703f8de0795af71399ba283cb27aac";
    };
    reproc = fetchurl {
      url = "https://github.com/DaanDeMeyer/reproc/archive/refs/tags/v14.2.7.tar.gz";
      sha256 = "77914ba566356d86986db240ecdb1762dfdd55bb1e8e127d016d9c5c4c9300e8";
    };
    nlohmann_json = fetchurl {
      url = "https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz";
      sha256 = "42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa";
    };
    sdl3 = fetchurl {
      url = "https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz";
      sha256 = "9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3";
    };
  };

  simdjsonHeader = fetchurl {
    url = "https://raw.githubusercontent.com/simdjson/simdjson/v5.0.5/singleheader/simdjson.h";
    sha256 = "869e8a4ff285cec36bc79d4a671a433158391a4f1898bb1c3b1f6e654b9547ae";
  };
  simdjsonSource = fetchurl {
    url = "https://raw.githubusercontent.com/simdjson/simdjson/v5.0.5/singleheader/simdjson.cpp";
    sha256 = "97d622441079fae86c7c342508880168bd7bf9e3342b1f4c565077da0155ca79";
  };

  libzt = fetchFromGitHub {
    owner = "zerotier";
    repo = "libzt";
    rev = "a707ea6ae0910efdc1125d04758c411e2e9ea4f9";
    fetchSubmodules = true;
    hash = "sha256-7XlMdyyo49fS1mXFll6SM0u5GH+HzxuDYbIpjjRJyvs=";
  };

  # SDL3 is linked statically but opens the windowing and audio libraries
  # with dlopen at run time. fontconfig is opened the same way by UiFont.cpp.
  # PipeWire is left out on purpose: its closure is over 600 MiB, and SDL
  # plays the UI sounds through PulseAudio or ALSA, which PipeWire serves too.
  runtimeLibs = [
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
    wayland
    libxkbcommon
    libdecor
    libGL
    dbus
    udev
    alsa-lib
    libpulseaudio
    fontconfig
  ];

  # Libraries that the game opens with dlopen. SDL3 (Minecraft 26.3 and later) and GLFW
  # need the same ones as the launcher. Then LWJGL 2 (libxxf86vm), OpenAL (libstdc++),
  # Vulkan and the narrator (flite). On NixOS the launcher puts them in LD_LIBRARY_PATH
  # of the game, after the GPU driver in /run/opengl-driver/lib.
  gameLibs = runtimeLibs ++ [
    (lib.getLib stdenv.cc.cc)
    libxxf86vm
    vulkan-loader
    flite
    freetype
    zlib
  ];

  nixosJavaHomes = lib.concatMapStringsSep ":" (jdk: jdk.home) jdks;
  nixosLibraryPath = "${addDriverRunpath.driverLink}/lib:${lib.makeLibraryPath gameLibs}";
  nixosPath = lib.makeBinPath [
    xrandr
    pciutils
  ];

  compilerId = if stdenv.cc.isClang then "Clang" else "GNU";

  # FetchContent skips URL_HASH and GIT_TAG once FETCHCONTENT_SOURCE_DIR_<NAME> is set,
  # so stop here when cmake/dependencies.cmake pins other versions.
  pins = builtins.readFile ../cmake/dependencies.cmake;
  stale = lib.filter (pin: !lib.hasInfix pin pins) (
    map (src: src.outputHash) (
      lib.attrValues deps
      ++ [
        simdjsonHeader
        simdjsonSource
      ]
    )
    ++ [ libzt.rev ]
  );
  staleMessage = "nix/package.nix: update the sources to match cmake/dependencies.cmake (${toString stale})";
in
assert lib.assertMsg (stale == [ ]) staleMessage;
stdenv.mkDerivation {
  pname = "b-launcher";
  version = "0.3.0";

  src = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.unions [
      ../CMakeLists.txt
      ../cmake
      ../src
      ../assets
      ../third_party
      ../tests
    ];
  };

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    (python3.withPackages (ps: [ ps.fonttools ]))
    wayland-scanner
    patchelf
  ];

  buildInputs = runtimeLibs ++ [ ibus ];

  postUnpack = ''
    deps=$NIX_BUILD_TOP/deps
    mkdir -p $deps
    ${lib.concatStrings (
      lib.mapAttrsToList (name: tarball: ''
        mkdir -p $deps/${name}
        tar -xf ${tarball} -C $deps/${name} --strip-components=1
      '') deps
    )}
    mkdir -p $deps/simdjson_header $deps/simdjson_source
    cp ${simdjsonHeader} $deps/simdjson_header/simdjson.h
    cp ${simdjsonSource} $deps/simdjson_source/simdjson.cpp
    cp -r ${libzt} $deps/libzt
    chmod -R u+w $deps
  '';

  # FetchContent does not run PATCH_COMMAND for a local source dir,
  # so run the same patch scripts by hand.
  preConfigure = ''
    deps=$NIX_BUILD_TOP/deps
    cmake -DSOURCE_DIR=$deps/reproc -P cmake/patch_reproc.cmake
    cmake -DSOURCE_DIR=$deps/libzt -DCOMPILER_ID=${compilerId} \
      -DCOMPILER_VERSION=${stdenv.cc.version} -P cmake/patch_libzt.cmake
    for name in $(ls $deps); do
      appendToVar cmakeFlags "-DFETCHCONTENT_SOURCE_DIR_''${name^^}=$deps/$name"
    done
  '';

  cmakeFlags = [
    (lib.cmakeBool "FETCHCONTENT_FULLY_DISCONNECTED" true)
    (lib.cmakeBool "BLB_TESTS" true)
    (lib.cmakeFeature "BLB_NIXOS_JAVA" nixosJavaHomes)
    (lib.cmakeFeature "BLB_NIXOS_LIBRARY_PATH" nixosLibraryPath)
    # LWJGL 2 (Minecraft 1.12.2 and older) runs xrandr. OSHI runs lspci.
    (lib.cmakeFeature "BLB_NIXOS_PATH" nixosPath)
  ];

  ninjaFlags = [
    "b-launcher"
    "b-launcher-tests"
  ];

  doCheck = true;
  checkPhase = ''
    runHook preCheck
    ctest --output-on-failure
    runHook postCheck
  '';

  installPhase = ''
    runHook preInstall
    install -Dm755 b-launcher $out/bin/b-launcher
    install -Dm644 $src/assets/images/logo.svg $out/share/icons/hicolor/scalable/apps/b-launcher.svg
    install -Dm644 /dev/stdin $out/share/applications/b-launcher.desktop <<EOF
    [Desktop Entry]
    Type=Application
    Name=B Launcher
    Comment=Minecraft launcher
    Exec=b-launcher
    Icon=b-launcher
    Categories=Game;
    Terminal=false
    EOF
    runHook postInstall
  '';

  # Run after the fixup hooks, which drop RPATH entries that no DT_NEEDED uses.
  postFixup = ''
    patchelf --add-rpath ${lib.makeLibraryPath runtimeLibs} $out/bin/b-launcher
  '';

  passthru = {
    inherit
      runtimeLibs
      gameLibs
      jdks
      nixosJavaHomes
      nixosLibraryPath
      nixosPath
      ;
  };

  meta = {
    description = "Lightweight Minecraft launcher written in C++";
    homepage = "https://github.com/cubk1/b-launcher";
    license = lib.licenses.mit;
    mainProgram = "b-launcher";
    platforms = lib.platforms.linux;
  };
}
