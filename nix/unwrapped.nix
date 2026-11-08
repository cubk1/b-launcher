{
  lib,
  stdenv,
  fetchFromGitHub,
  applyPatches,
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
      ../tests
    ];
  };

  __structuredAttrs = true;
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

  cmakeFlags = [
    (lib.cmakeBool "FETCHCONTENT_FULLY_DISCONNECTED" true)
    (lib.cmakeFeature "FETCHCONTENT_SOURCE_DIR_LIBZT" "${libzt}")
    (lib.cmakeBool "BLB_SYSTEM_LIBS" true)
    # Nix updates the package.
    (lib.cmakeBool "BLB_UPDATE_CHECK" false)
    (lib.cmakeFeature "BLB_GIT_COMMIT" gitCommit)
    (lib.cmakeBool "BLB_TESTS" finalAttrs.doCheck)
  ];

  doCheck = true;

  meta = {
    description = "Lightweight Minecraft launcher written in C++";
    homepage = "https://github.com/cubk1/b-launcher";
    license = lib.licenses.mit;
    mainProgram = "b-launcher";
    platforms = lib.platforms.linux;
  };
})
