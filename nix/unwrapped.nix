{
  lib,
  stdenv,
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
  simdjson,
  sdl3,
  freetype,
  fontconfig,
  # The short git hash for the 关于 page. The Nix build has no .git, so flake.nix gives it.
  gitCommit ? "",
}:

let
  # nixpkgs has no libzt, so cmake/dependencies.cmake downloads it with FetchContent.
  # The Nix build has no network, so we fetch it here and point
  # FETCHCONTENT_SOURCE_DIR_LIBZT at the copy.
  # Keep the commit in sync with cmake/dependencies.cmake.
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
  stale = lib.filter (pin: !lib.hasInfix pin pins) [ libzt.rev ];
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
      ../tests
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
    simdjson
    sdl3
    freetype
    fontconfig
  ];

  postUnpack = ''
    deps=$NIX_BUILD_TOP/deps
    mkdir -p $deps
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
    (lib.cmakeBool "BLB_TESTS" true)
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

  meta = {
    description = "Lightweight Minecraft launcher written in C++";
    homepage = "https://github.com/cubk1/b-launcher";
    license = lib.licenses.mit;
    mainProgram = "b-launcher";
    platforms = lib.platforms.linux;
  };
}
