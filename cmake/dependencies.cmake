include(FetchContent)

set(FETCHCONTENT_QUIET ON)
set(BLB_DEP_BASE "https://github.com")

function(blb_force name value)
  set(${name} ${value} CACHE INTERNAL "")
endfunction()

set(BLB_SAVED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
blb_force(BUILD_TESTING OFF)
blb_force(SKIP_INSTALL_ALL ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

blb_force(ZLIB_COMPAT ON)
blb_force(ZLIB_ENABLE_TESTS OFF)
blb_force(ZLIBNG_ENABLE_TESTS OFF)
blb_force(WITH_GTEST OFF)
blb_force(WITH_GZFILEOP OFF)
blb_force(WITH_FUZZERS OFF)
blb_force(WITH_BENCHMARKS OFF)
blb_force(WITH_BENCHMARK_APPS OFF)
blb_force(WITH_AVX512 OFF)
blb_force(WITH_AVX512VNNI OFF)
blb_force(WITH_VPCLMULQDQ OFF)
blb_force(WITH_CRC32_CHORBA OFF)
blb_force(INSTALL_UTILS OFF)

FetchContent_Declare(zlib
  URL ${BLB_DEP_BASE}/zlib-ng/zlib-ng/archive/refs/tags/2.3.3.tar.gz
  URL_HASH SHA256=f9c65aa9c852eb8255b636fd9f07ce1c406f061ec19a2e7d508b318ca0c907d1
  OVERRIDE_FIND_PACKAGE
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(zlib)
if(MSVC AND TARGET zlib-ng)
  target_compile_options(zlib-ng PRIVATE $<$<CONFIG:Release>:/O2>)
endif()
if(NOT TARGET ZLIB::ZLIB)
  add_library(ZLIB::ZLIB ALIAS zlib-ng)
endif()
blb_force(ZLIB_INCLUDE_DIR "${zlib_BINARY_DIR};${zlib_SOURCE_DIR}")
blb_force(ZLIB_INCLUDE_DIRS "${zlib_BINARY_DIR};${zlib_SOURCE_DIR}")
blb_force(ZLIB_LIBRARY zlib-ng)
blb_force(ZLIB_LIBRARIES zlib-ng)

blb_force(BUILD_CURL_EXE OFF)
blb_force(BUILD_STATIC_LIBS ON)
blb_force(BUILD_LIBCURL_DOCS OFF)
blb_force(BUILD_MISC_DOCS OFF)
blb_force(ENABLE_CURL_MANUAL OFF)
blb_force(BUILD_EXAMPLES OFF)
blb_force(CURL_DISABLE_INSTALL ON)
blb_force(CURL_ENABLE_EXPORT_TARGET OFF)
blb_force(PICKY_COMPILER OFF)
blb_force(CURL_DROP_UNUSED ON)
blb_force(CURL_USE_PKGCONFIG OFF)
blb_force(CURL_USE_LIBPSL OFF)
blb_force(CURL_USE_LIBSSH2 OFF)
blb_force(CURL_USE_LIBSSH OFF)
blb_force(CURL_USE_GSSAPI OFF)
blb_force(USE_LIBIDN2 OFF)
blb_force(USE_WIN32_IDN OFF)
blb_force(USE_NGTCP2 OFF)
blb_force(USE_NGHTTP2 OFF)
blb_force(CURL_BROTLI OFF)
blb_force(CURL_ZSTD OFF)
blb_force(CURL_ZLIB ON)
blb_force(ENABLE_ARES OFF)
blb_force(ENABLE_UNIX_SOCKETS OFF)
blb_force(USE_WIN32_LDAP OFF)
foreach(feature
    ALTSVC AWS DICT DOH FILE FTP GOPHER IMAP LDAP LDAPS MQTT POP3 RTSP SMTP
    TELNET TFTP IPFS WEBSOCKETS MIME FORM_API NETRC HSTS KERBEROS_AUTH
    NEGOTIATE_AUTH DIGEST_AUTH GETOPTIONS LIBCURL_OPTION PROGRESS_METER
    SHA512_256 BINDLOCAL HEADERS_API COOKIES HTTPSIG)
  blb_force(CURL_DISABLE_${feature} ON)
endforeach()
if(WIN32)
  blb_force(CURL_USE_SCHANNEL ON)
  blb_force(CURL_USE_OPENSSL OFF)
  blb_force(CURL_WINDOWS_SSPI ON)
else()
  blb_force(CURL_USE_OPENSSL ON)
endif()

FetchContent_Declare(curl
  URL ${BLB_DEP_BASE}/curl/curl/releases/download/curl-8_23_0/curl-8.23.0.tar.xz
  URL_HASH SHA256=8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(curl)

blb_force(MZ_COMPAT OFF)
blb_force(MZ_ZLIB ON)
blb_force(MZ_ZLIB_FLAVOR zlib)
blb_force(MZ_BZIP2 OFF)
blb_force(MZ_LZMA OFF)
blb_force(MZ_PPMD OFF)
blb_force(MZ_ZSTD OFF)
blb_force(MZ_LIBCOMP OFF)
blb_force(MZ_FETCH_LIBS OFF)
blb_force(MZ_FORCE_FETCH_LIBS OFF)
blb_force(MZ_PKCRYPT OFF)
blb_force(MZ_WZAES OFF)
blb_force(MZ_OPENSSL OFF)
blb_force(MZ_LIBBSD OFF)
blb_force(MZ_ICONV OFF)
blb_force(MZ_DECOMPRESS_ONLY OFF)
blb_force(MZ_BUILD_TESTS OFF)
blb_force(MZ_BUILD_UNIT_TESTS OFF)
blb_force(MZ_BUILD_FUZZ_TESTS OFF)

FetchContent_Declare(minizip
  URL ${BLB_DEP_BASE}/zlib-ng/minizip-ng/archive/refs/tags/4.2.2.tar.gz
  URL_HASH SHA256=71af7b9799856d8b03619df3949e9c1be9703f8de0795af71399ba283cb27aac
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(minizip)

blb_force(REPROC++ OFF)
blb_force(REPROC_TEST OFF)
blb_force(REPROC_EXAMPLES OFF)
blb_force(REPROC_INSTALL OFF)
blb_force(REPROC_INSTALL_PKGCONFIG OFF)
blb_force(REPROC_OBJECT_LIBRARIES OFF)
blb_force(REPROC_MULTITHREADED ON)

FetchContent_Declare(reproc
  URL ${BLB_DEP_BASE}/DaanDeMeyer/reproc/archive/refs/tags/v14.2.7.tar.gz
  URL_HASH SHA256=77914ba566356d86986db240ecdb1762dfdd55bb1e8e127d016d9c5c4c9300e8
  PATCH_COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=<SOURCE_DIR>
    -P ${CMAKE_CURRENT_LIST_DIR}/patch_reproc.cmake
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(reproc)

blb_force(JSON_BuildTests OFF)
blb_force(JSON_Install OFF)
blb_force(JSON_ImplicitConversions OFF)

FetchContent_Declare(nlohmann_json
  URL ${BLB_DEP_BASE}/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(nlohmann_json)

set(BLB_SIMDJSON_RAW
  "https://raw.githubusercontent.com/simdjson/simdjson/v5.0.5/singleheader")
FetchContent_Declare(simdjson_header
  URL ${BLB_SIMDJSON_RAW}/simdjson.h
  URL_HASH SHA256=869e8a4ff285cec36bc79d4a671a433158391a4f1898bb1c3b1f6e654b9547ae
  DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_Declare(simdjson_source
  URL ${BLB_SIMDJSON_RAW}/simdjson.cpp
  URL_HASH SHA256=97d622441079fae86c7c342508880168bd7bf9e3342b1f4c565077da0155ca79
  DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(simdjson_header simdjson_source)

set(BLB_SIMDJSON_DIR "${CMAKE_BINARY_DIR}/simdjson")
configure_file("${simdjson_header_SOURCE_DIR}/simdjson.h"
  "${BLB_SIMDJSON_DIR}/simdjson.h" COPYONLY)
configure_file("${simdjson_source_SOURCE_DIR}/simdjson.cpp"
  "${BLB_SIMDJSON_DIR}/simdjson.cpp" COPYONLY)
add_library(blb_simdjson STATIC "${BLB_SIMDJSON_DIR}/simdjson.cpp")
target_include_directories(blb_simdjson SYSTEM PUBLIC "${BLB_SIMDJSON_DIR}")
target_compile_features(blb_simdjson PUBLIC cxx_std_20)
target_compile_definitions(blb_simdjson PUBLIC
  SIMDJSON_IMPLEMENTATION_ICELAKE=0
  SIMDJSON_IMPLEMENTATION_WESTMERE=0
  SIMDJSON_DISABLE_DEPRECATED_API=1)

set(BUILD_SHARED_LIBS ${BLB_SAVED_BUILD_SHARED_LIBS})

set(BLB_CURL_TARGET CURL::libcurl_static)
if(NOT TARGET ${BLB_CURL_TARGET})
  set(BLB_CURL_TARGET CURL::libcurl)
endif()

set(BLB_MINIZIP_TARGET minizip)
if(NOT TARGET ${BLB_MINIZIP_TARGET})
  set(BLB_MINIZIP_TARGET minizip-ng)
endif()
