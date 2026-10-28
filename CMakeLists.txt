cmake_minimum_required(VERSION 3.25)
project(b-launcher VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif ()

if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    foreach (language C CXX)
        set(CMAKE_${language}_FLAGS_RELEASE "/O1 /Ob1 /Gy /Gw /GL /DNDEBUG")
        set(CMAKE_${language}_FLAGS_MINSIZEREL "/O1 /Ob1 /Gy /Gw /GL /DNDEBUG")
    endforeach ()
    foreach (kind EXE SHARED MODULE)
        set(CMAKE_${kind}_LINKER_FLAGS_RELEASE "/INCREMENTAL:NO /LTCG /OPT:REF /OPT:ICF")
        set(CMAKE_${kind}_LINKER_FLAGS_MINSIZEREL "/INCREMENTAL:NO /LTCG /OPT:REF /OPT:ICF")
    endforeach ()
    set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "/LTCG")
    set(CMAKE_STATIC_LINKER_FLAGS_MINSIZEREL "/LTCG")
endif ()

include(cmake/dependencies.cmake)
include(cmake/embed.cmake)

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp)
add_library(core STATIC ${CORE_SOURCES})
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(core PUBLIC ${BLB_CURL_TARGET} ${BLB_MINIZIP_TARGET} reproc nlohmann_json::nlohmann_json blb_simdjson)
if (WIN32)
    target_compile_definitions(core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00)
endif ()
if (MSVC)
    target_compile_options(core PRIVATE /W4 /utf-8 /permissive- /Zc:__cplusplus)
    set_source_files_properties(
            src/core/base/hash.cpp
            src/core/base/sha1_x86.cpp
            src/core/base/fs.cpp
            src/core/base/thread_pool.cpp
            src/core/network/download_client.cpp
            src/core/installer/integrity.cpp
            src/core/meta/asset_index.cpp
            PROPERTIES COMPILE_OPTIONS $<$<CONFIG:Release>:/O2>)
endif ()

if (MSVC)
    add_compile_options(/utf-8 /W3 /permissive- /Zc:__cplusplus)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif ()

set(FREETYPE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype)
add_library(freetype STATIC
        ${FREETYPE_DIR}/src/base/ftbase.c
        ${FREETYPE_DIR}/src/base/ftbbox.c
        ${FREETYPE_DIR}/src/base/ftbitmap.c
        ${FREETYPE_DIR}/src/base/ftinit.c
        ${FREETYPE_DIR}/src/base/ftmm.c
        ${FREETYPE_DIR}/src/base/ftsynth.c
        ${FREETYPE_DIR}/builds/windows/ftsystem.c
        ${FREETYPE_DIR}/builds/windows/ftdebug.c
        ${FREETYPE_DIR}/src/autofit/autofit.c
        ${FREETYPE_DIR}/src/gzip/ftgzip.c
        ${FREETYPE_DIR}/src/sfnt/sfnt.c
        ${FREETYPE_DIR}/src/smooth/smooth.c
        ${FREETYPE_DIR}/src/truetype/truetype.c)
target_include_directories(freetype PUBLIC ${FREETYPE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype-config)
target_compile_definitions(freetype PRIVATE FT2_BUILD_LIBRARY FT_CONFIG_OPTION_SUBPIXEL_RENDERING
        "FT_CONFIG_MODULES_H=<b-launcher-ftmodule.h>" _CRT_NONSTDC_NO_WARNINGS)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/misc/freetype/imgui_freetype.cpp
        ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_DEMO_WINDOWS IMGUI_DISABLE_DEBUG_TOOLS IMGUI_DISABLE_DEFAULT_FONT IMGUI_ENABLE_FREETYPE)
target_link_libraries(imgui PUBLIC freetype)

file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp)
find_program(PYTHON_ON_PATH NAMES python python3)
find_package(Python3 COMPONENTS Interpreter QUIET)
set(FONT_PYTHON "")
foreach (candidate IN ITEMS "${PYTHON_ON_PATH}" "${Python3_EXECUTABLE}")
    if (candidate AND NOT FONT_PYTHON)
        execute_process(COMMAND ${candidate} -c "import fontTools" RESULT_VARIABLE FONTTOOLS_MISSING OUTPUT_QUIET ERROR_QUIET)
        if (FONTTOOLS_MISSING EQUAL 0)
            set(FONT_PYTHON ${candidate})
        endif ()
    endif ()
endforeach ()
set(GENERATED_FONTS ${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedFonts.cpp)
if (FONT_PYTHON)
    set(GENERATED_FONTS_STAMP ${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedFonts.stamp)
    file(GLOB_RECURSE ICON_SOURCES CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.h)
    add_custom_command(OUTPUT ${GENERATED_FONTS_STAMP}
            BYPRODUCTS ${GENERATED_FONTS}
            COMMAND ${FONT_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_fonts.py ${GENERATED_FONTS} ${GENERATED_FONTS_STAMP}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_fonts.py ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/SSTRg.TTF
                    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/fa-solid-900.ttf ${ICON_SOURCES}
            COMMENT "Cutting the built-in fonts down to what is used"
            VERBATIM)
    list(APPEND UI_SOURCES ${GENERATED_FONTS} ${GENERATED_FONTS_STAMP})
else ()
    message(STATUS "No Python with fontTools (pip install fonttools): the built-in fonts are compiled in whole")
    embed_files(OUTPUT ${GENERATED_FONTS} HEADER EmbeddedFonts.h NAMESPACE EmbeddedFonts FILES
            textFont assets/fonts/SSTRg.TTF
            iconFont assets/fonts/fa-solid-900.ttf)
    list(APPEND UI_SOURCES ${GENERATED_FONTS})
endif ()
add_library(ui STATIC ${UI_SOURCES})
target_include_directories(ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/fonts)
target_include_directories(ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
target_link_libraries(ui PUBLIC imgui)
if (WIN32)
    target_link_libraries(ui PUBLIC winhttp opengl32 shell32 winmm)
endif ()

embed_files(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedIcons.cpp HEADER app/shell/EmbeddedIcons.h NAMESPACE EmbeddedIcons FILES
        logoSvg assets/images/logo.svg
        forge assets/images/forge.png
        vanilla assets/images/vanilla.png
        neoforge assets/images/neoforge.png
        cleanroom assets/images/cleanroom.png
        fabric assets/images/fabric.png
        quilt assets/images/quilt.png
        optifine assets/images/optifine.png
        legacyFabric assets/images/legacy-fabric.png
        liteloader assets/images/liteloader.png
        iris assets/images/iris.png
        canvas assets/images/canvas.png
        hmcl assets/images/hmcl.png
        pcl assets/images/pcl.png
        backgroundSidebar assets/images/background-sidebar.jpg
        backgroundPanel assets/images/background-panel.jpg)
embed_files(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedSounds.cpp HEADER app/shell/EmbeddedSounds.h NAMESPACE EmbeddedSounds FILES
        press assets/sounds/press.wav
        tick assets/sounds/tick.wav
        release assets/sounds/release.wav
        page assets/sounds/page.wav
        pulse assets/sounds/pulse.wav)

embed_files(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedAgent.cpp HEADER app/launch/EmbeddedAgent.h NAMESPACE EmbeddedAgent FILES
        launcherAgent assets/launcher-agent.jar)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.cpp)
add_executable(b-launcher WIN32 src/main.cpp ${APP_SOURCES} src/app.rc
        ${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedIcons.cpp ${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedSounds.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedAgent.cpp)
target_include_directories(b-launcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nanosvg)
target_compile_definitions(b-launcher PRIVATE B_LAUNCHER_VERSION="${PROJECT_VERSION}")
target_link_libraries(b-launcher PRIVATE ui imgui core nlohmann_json::nlohmann_json opengl32 dwmapi shell32 ole32 crypt32 bcrypt ws2_32)
if (MSVC)
    set_target_properties(b-launcher PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
endif ()
find_program(UPX_EXECUTABLE upx)
add_custom_command(TARGET b-launcher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DCONFIG=$<CONFIG> -DUPX=${UPX_EXECUTABLE} -DFILE=$<TARGET_FILE:b-launcher>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/upx.cmake
        VERBATIM)
