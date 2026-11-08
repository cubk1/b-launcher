{
  lib,
  stdenv,
  fetchurl,
  fetchFromGitHub,
  cmake,
  ninja,
  pkg-config,
  python3,
  curl,
  zlib,
  minizip-ng,
  reproc,
  nlohmann_json,
  sdl3,
  freetype,
  fontconfig,
  # The short git hash for the 关于 page. The Nix build has no .git, so flake.nix gives it.
  gitCommit ? "",
}:

let
  # nixpkgs has no simdjson 5 and no libzt, so cmake/dependencies.cmake downloads them
  # with FetchContent. The Nix build has no network, so we fetch them here and point
  # FETCHCONTENT_SOURCE_DIR_<NAME> at the copies.
  # Keep URLs and hashes in sync with cmake/dependencies.cmake.
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

  compilerId = if stdenv.cc.isClang then "Clang" else "GNU";

  # FetchContent skips URL_HASH and GIT_TAG once FETCHCONTENT_SOURCE_DIR_<NAME> is set,
  # so stop here when cmake/dependencies.cmake pins other versions.
  pins = builtins.readFile ../cmake/dependencies.cmake;
  stale = lib.filter (pin: !lib.hasInfix pin pins) [
    simdjsonHeader.outputHash
    simdjsonSource.outputHash
    libzt.rev
  ];
  staleMessage = "nix/unwrapped.nix: update the sources to match cmake/dependencies.cmake (${toString stale})";

  version = lib.head (
    builtins.match ".*project\\(b-launcher VERSION ([0-9.]+).*" (builtins.readFile ../CMakeLists.txt)
  );
in
assert lib.assertMsg (stale == [ ]) staleMessage;
stdenv.mkDerivation {
  pname = "b-launcher-unwrapped";
  inherit version;

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

  strictDeps = true;

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    (python3.withPackages (ps: [ ps.fonttools ]))
  ];

  buildInputs = [
    curl
    zlib
    minizip-ng
    reproc
    nlohmann_json
    sdl3
    freetype
    fontconfig
  ];

  postUnpack = ''
    deps=$NIX_BUILD_TOP/deps
    mkdir -p $deps/simdjson_header $deps/simdjson_source
    cp ${simdjsonHeader} $deps/simdjson_header/simdjson.h
    cp ${simdjsonSource} $deps/simdjson_source/simdjson.cpp
    cp -r ${libzt} $deps/libzt
    chmod -R u+w $deps
  '';

  # FetchContent does not run PATCH_COMMAND for a local source dir,
  # so run the same patch script by hand.
  preConfigure = ''
    deps=$NIX_BUILD_TOP/deps
    cmake -DSOURCE_DIR=$deps/libzt -DCOMPILER_ID=${compilerId} \
      -DCOMPILER_VERSION=${stdenv.cc.version} -P cmake/patch_libzt.cmake
    for name in $(ls $deps); do
      appendToVar cmakeFlags "-DFETCHCONTENT_SOURCE_DIR_''${name^^}=$deps/$name"
    done
  '';

  cmakeFlags = [
    (lib.cmakeBool "FETCHCONTENT_FULLY_DISCONNECTED" true)
    (lib.cmakeBool "BLB_SYSTEM_LIBS" true)
    # Nix updates the package.
    (lib.cmakeBool "BLB_UPDATE_CHECK" false)
    (lib.cmakeFeature "BLB_GIT_COMMIT" gitCommit)
  ];

  ninjaFlags = [
    "b-launcher"
  ];

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

  meta = {
    description = "Lightweight Minecraft launcher written in C++";
    homepage = "https://github.com/cubk1/b-launcher";
    license = lib.licenses.mit;
    mainProgram = "b-launcher";
    platforms = lib.platforms.linux;
  };
}
