include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Do not update dependencies after first download")

set(RECHIGROS_SDL_SOURCE "" CACHE PATH "Optional local SDL3 source tree")
if(NOT RECHIGROS_BUILD_APP)
  # Core and chart layers only need header-only dependencies.
  set(RECHIGROS_SDL_TARGET "")
elseif(ANDROID)
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(RECHIGROS_SDL_TARGET SDL3::SDL3)
else()
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(RECHIGROS_SDL_TARGET SDL3::SDL3-static)
endif()
set(SDL_TEST OFF CACHE BOOL "" FORCE)

if(NOT RECHIGROS_BUILD_APP)
  # Nothing to fetch.
elseif(RECHIGROS_SDL_SOURCE AND EXISTS "${RECHIGROS_SDL_SOURCE}/CMakeLists.txt")
  add_subdirectory("${RECHIGROS_SDL_SOURCE}" "${CMAKE_BINARY_DIR}/third_party/SDL3")
elseif(RECHIGROS_USE_SYSTEM_SDL)
  find_package(SDL3 CONFIG REQUIRED)
  if(NOT ANDROID AND TARGET SDL3::SDL3-static)
    set(RECHIGROS_SDL_TARGET SDL3::SDL3-static)
  else()
    set(RECHIGROS_SDL_TARGET SDL3::SDL3)
  endif()
else()
  FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    URL_HASH SHA256=9C75CF16330322C217DEDD2E0609F1124F1B54B8633E763467B4684D0F4334A3)
  FetchContent_MakeAvailable(SDL3)
endif()

set(RECHIGROS_DOWNLOAD_DIR "${PROJECT_SOURCE_DIR}/third_party/cache" CACHE PATH "Shared verified dependency download cache")
function(rechigros_download relative_path url sha256)
  set(output "${RECHIGROS_DOWNLOAD_DIR}/${relative_path}")
  get_filename_component(output_dir "${output}" DIRECTORY)
  file(MAKE_DIRECTORY "${output_dir}")
  set(needs_download TRUE)
  if(EXISTS "${output}")
    file(SHA256 "${output}" current_hash)
    if("${current_hash}" STREQUAL "${sha256}")
      set(needs_download FALSE)
    endif()
  endif()
  if(needs_download)
    file(DOWNLOAD "${url}" "${output}"
      EXPECTED_HASH "SHA256=${sha256}"
      TLS_VERIFY ON
      STATUS download_status)
    list(GET download_status 0 status_code)
    list(GET download_status 1 status_message)
    if(NOT status_code EQUAL 0)
      file(REMOVE "${output}")
      message(FATAL_ERROR "Failed to download ${url}: ${status_message}")
    endif()
  endif()
endfunction()

set(JSDELIVR "https://cdn.jsdelivr.net/gh")
rechigros_download("nlohmann/json.hpp" "${JSDELIVR}/nlohmann/json@v3.11.3/single_include/nlohmann/json.hpp" "9BEA4C8066EF4A1C206B2BE5A36302F8926F7FDC6087AF5D20B417D0CF103EA6")
add_library(nlohmann_json_headers INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json_headers)
target_include_directories(nlohmann_json_headers INTERFACE "${RECHIGROS_DOWNLOAD_DIR}")

rechigros_download("miniaudio/miniaudio.h" "${JSDELIVR}/mackron/miniaudio@0.11.21/miniaudio.h" "6B2029714F8634C4D7C70CC042F45074E0565766113FC064F20CD27C986BE9C9")
add_library(miniaudio_headers INTERFACE)
target_include_directories(miniaudio_headers INTERFACE "${RECHIGROS_DOWNLOAD_DIR}/miniaudio")

rechigros_download("stb/stb_image.h" "${JSDELIVR}/nothings/stb@master/stb_image.h" "594C2FE35D49488B4382DBFAEC8F98366DEFCA819D916AC95BECF3E75F4200B3")
rechigros_download("stb/stb_image_write.h" "${JSDELIVR}/nothings/stb@master/stb_image_write.h" "CBD5F0AD7A9CF4468AFFB36354A1D2338034F2C12473CF1A8E32053CB6914A05")
add_library(stb_headers INTERFACE)
target_include_directories(stb_headers INTERFACE "${RECHIGROS_DOWNLOAD_DIR}/stb")

# WebP decoding for packs that carry a WebP payload under a .png name. Wuffs' release is a
# single C file and covers VP8/VP8L, so no platform image framework is involved.
rechigros_download("wuffs/wuffs-v0.4.c"
  "${JSDELIVR}/google/wuffs@main/release/c/wuffs-v0.4.c"
  "458066457347BD6D9BC3C906AA8292BEA4488577727569DDD9436FDD89D5F04D")
add_library(wuffs_headers INTERFACE)
target_include_directories(wuffs_headers INTERFACE "${RECHIGROS_DOWNLOAD_DIR}/wuffs")

set(IMGUI_BASE "${JSDELIVR}/ocornut/imgui@v1.91.8")
rechigros_download("imgui/imconfig.h" "${IMGUI_BASE}/imconfig.h" "BD89B32DE6A8A286700383BAE47B4F4BBA753BA13058D1A350770C5232B2E4E8")
rechigros_download("imgui/imgui.h" "${IMGUI_BASE}/imgui.h" "C54743F229027D3A9C057343073C6C6EFA37A907306AAFE94C79D706B8BF832A")
rechigros_download("imgui/imgui_internal.h" "${IMGUI_BASE}/imgui_internal.h" "2BBBFB8DD39F287455E42F3350AD99C377906F366CF6E136C5E7F6438683647C")
rechigros_download("imgui/imgui.cpp" "${IMGUI_BASE}/imgui.cpp" "75DEF72D9FF61B057445EE01C7BE3B5A0CC9D07158173AAB1B67202D2C342367")
rechigros_download("imgui/imgui_draw.cpp" "${IMGUI_BASE}/imgui_draw.cpp" "138849E6FB4CF8C4AB31F0CA23EA722DF235E29C5496901CC7C14AEC8C90C770")
rechigros_download("imgui/imgui_tables.cpp" "${IMGUI_BASE}/imgui_tables.cpp" "B7F7DE78D8CD7F5B6757F75499B6DF0B2C42F85CDD276BF3016C2AB77EAC6AD9")
rechigros_download("imgui/imgui_widgets.cpp" "${IMGUI_BASE}/imgui_widgets.cpp" "6C7CFF645D1A317E82FD9FD2BFD636CCCBB84388EA0178554FF4A6B253F10F43")
rechigros_download("imgui/imstb_rectpack.h" "${IMGUI_BASE}/imstb_rectpack.h" "2EFA3D5F7D003C19743B15155DAD9F46D9F9FC783A18893D703B431D3C990972")
rechigros_download("imgui/imstb_textedit.h" "${IMGUI_BASE}/imstb_textedit.h" "77010E494CAF72C178B573569612AEC409D24380409676325A31D02DE0EC64B7")
rechigros_download("imgui/imstb_truetype.h" "${IMGUI_BASE}/imstb_truetype.h" "87F08919AE8E7223451D0E9D06B62829075D3E920C9E6AD436DB9D7E7427FFCE")
rechigros_download("imgui/backends/imgui_impl_sdl3.h" "${IMGUI_BASE}/backends/imgui_impl_sdl3.h" "A6866CB9F9D9EB69AD64D49075FBCC247CADB9C872FCDFB0569F4B93CCB802A6")
rechigros_download("imgui/backends/imgui_impl_sdl3.cpp" "${IMGUI_BASE}/backends/imgui_impl_sdl3.cpp" "72EF28868E34EE88A65C9AF368E4D3E643B8B1847407E5961011758AA411AD7C")
rechigros_download("imgui/backends/imgui_impl_opengl3.h" "${IMGUI_BASE}/backends/imgui_impl_opengl3.h" "8239B72A4325F370C275D34B655B3FCAF2660CE9D7ACFDEA494A303DD51F9E5C")
rechigros_download("imgui/backends/imgui_impl_opengl3.cpp" "${IMGUI_BASE}/backends/imgui_impl_opengl3.cpp" "166C4FB64FA7A1D744D51CEB4DBA29AC670F76FD921EE47CE10977633DA1AE00")
rechigros_download("imgui/backends/imgui_impl_opengl3_loader.h" "${IMGUI_BASE}/backends/imgui_impl_opengl3_loader.h" "9D17B1679DC876E0CA693631030E70656C372C808C6767644C3B545834483E24")
set(imgui_SOURCE_DIR "${RECHIGROS_DOWNLOAD_DIR}/imgui")
# The UI toolkit needs SDL and OpenGL, so it only exists for application builds.
# Core-only builds must not see it in the default target set.
if(RECHIGROS_BUILD_APP)
  add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp")
  target_include_directories(imgui PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
  target_link_libraries(imgui PUBLIC ${RECHIGROS_SDL_TARGET})
  if(IOS)
    # The desktop loader embedded in the backend opens OpenGL.framework, which does not
    # exist on iOS. Compiling the ES3 branch uses the OpenGLES headers and entry points
    # directly, matching the renderer's own GLES build.
    target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3=1)
  endif()
endif()

# SDL3_ttf and SDL3_image: the interface framework renders text through SDL_ttf and decodes
# its bitmap resources through SDL_image. They are built from source next to SDL3 so the
# application never depends on a system copy that may be a different SDL3 major version.
if(RECHIGROS_BUILD_APP)
  # SDL3 can come from a local source tree, a system package or FetchContent, and each of
  # those spells its targets differently. SDL_ttf/SDL_image require the canonical
  # SDL3::SDL3 and SDL3::Headers names, so they are aliased onto whatever this tree got.
  if(NOT TARGET SDL3::Headers)
    add_library(SDL3::Headers INTERFACE)
    target_link_libraries(SDL3::Headers INTERFACE ${RECHIGROS_SDL_TARGET})
  endif()
  if(NOT TARGET SDL3::SDL3)
    add_library(SDL3::SDL3 INTERFACE)
    target_link_libraries(SDL3::SDL3 INTERFACE ${RECHIGROS_SDL_TARGET})
  endif()

  # Unpacked once into the shared cache; the extract directory is the archive's own stem.
  rechigros_download("sdl3_ttf/SDL3_ttf-3.2.2.tar.gz"
    "https://github.com/libsdl-org/SDL_ttf/releases/download/release-3.2.2/SDL3_ttf-3.2.2.tar.gz"
    "63547D58D0185C833213885B635A2C0548201CC8F301E6587C0BE1A67E1E045D")
  rechigros_download("sdl3_image/SDL3_image-3.2.4.tar.gz"
    "https://github.com/libsdl-org/SDL_image/releases/download/release-3.2.4/SDL3_image-3.2.4.tar.gz"
    "A725BD6D04261FDDA0DD8D950659E1DC15A8065D025275EF460D32AE7DCFC182")
  # Each archive is expanded only once. Extracting on every configure costs seconds and, on
  # Windows, reports the Xcode framework symlinks as warnings.
  if(NOT EXISTS "${RECHIGROS_DOWNLOAD_DIR}/sdl3_ttf/SDL3_ttf-3.2.2/CMakeLists.txt")
    file(ARCHIVE_EXTRACT INPUT "${RECHIGROS_DOWNLOAD_DIR}/sdl3_ttf/SDL3_ttf-3.2.2.tar.gz"
      DESTINATION "${RECHIGROS_DOWNLOAD_DIR}/sdl3_ttf")
  endif()
  if(NOT EXISTS "${RECHIGROS_DOWNLOAD_DIR}/sdl3_image/SDL3_image-3.2.4/CMakeLists.txt")
    file(ARCHIVE_EXTRACT INPUT "${RECHIGROS_DOWNLOAD_DIR}/sdl3_image/SDL3_image-3.2.4.tar.gz"
      DESTINATION "${RECHIGROS_DOWNLOAD_DIR}/sdl3_image")
  endif()

  # The release archive does not contain the freetype submodule. SDL_ttf builds its own
  # freetype when SdlTtf is configured with vendored support, so a clean tree needs the
  # pinned SDL fork unpacked next to it. The upstream download script clones the branch,
  # which would make the dependency float; the archive is pinned to the submodule commit.
  set(rechigros_freetype_dir "${RECHIGROS_DOWNLOAD_DIR}/sdl3_ttf/SDL3_ttf-3.2.2/external/freetype")
  if(NOT EXISTS "${rechigros_freetype_dir}/CMakeLists.txt")
    set(rechigros_freetype_commit "9973564cfa63763a3e4ac67c09147899539b1e07")
    set(rechigros_freetype_archive
      "${RECHIGROS_DOWNLOAD_DIR}/sdl3_ttf/freetype-${rechigros_freetype_commit}.tar.gz")
    rechigros_download(
      "sdl3_ttf/freetype-${rechigros_freetype_commit}.tar.gz"
      "https://github.com/libsdl-org/freetype/archive/${rechigros_freetype_commit}.tar.gz"
      "026A05A49D114A1235D2926F4C03A9330E4B1A6EFE7C217EC9607904C32907D4")
    set(rechigros_freetype_extract "${RECHIGROS_DOWNLOAD_DIR}/sdl3_ttf/freetype-extract")
    file(REMOVE_RECURSE "${rechigros_freetype_extract}")
    file(ARCHIVE_EXTRACT INPUT "${rechigros_freetype_archive}"
      DESTINATION "${rechigros_freetype_extract}")
    file(GLOB rechigros_freetype_roots LIST_DIRECTORIES true "${rechigros_freetype_extract}/*")
    list(LENGTH rechigros_freetype_roots rechigros_freetype_root_count)
    if(NOT rechigros_freetype_root_count EQUAL 1)
      message(FATAL_ERROR "The freetype archive has an unexpected layout.")
    endif()
    list(GET rechigros_freetype_roots 0 rechigros_freetype_root)
    file(RENAME "${rechigros_freetype_root}" "${rechigros_freetype_dir}")
    file(REMOVE_RECURSE "${rechigros_freetype_extract}")
  endif()

  # SDL_ttf/SDL_image key their target names off BUILD_SHARED_LIBS, which this project never
  # sets; without an explicit value they would look for SDL3::SDL3-shared and re-run
  # find_package against a system install.
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(SDLTTF_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(SDLIMAGE_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(SDL_TTF_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_TTF_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TTF_INSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_TTF_SAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_TTF_TESTS OFF CACHE BOOL "" FORCE)
  set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
  # Harfbuzz (complex text shaping) and plutosvg (colour emoji) are optional: this interface
  # draws CJK and Latin labels from one bundled face with no shaping or emoji, so both are
  # switched off rather than vendoring their sources.
  set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
  set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
  # Freetype is not optional for SDL_ttf. Vendoring it avoids requiring a system copy on
  # every host, which the packaged platforms cannot provide.
  set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
  set(SDLTTF_STRICT OFF CACHE BOOL "" FORCE)
  add_subdirectory("${RECHIGROS_DOWNLOAD_DIR}/sdl3_ttf/SDL3_ttf-3.2.2"
    "${CMAKE_BINARY_DIR}/third_party/SDL3_ttf")

  set(SDL_IMAGE_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_IMAGE_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_IMAGE_INSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_IMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_IMAGE_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_IMAGE_VENDORED OFF CACHE BOOL "" FORCE)
  set(SDLIMAGE_VENDORED OFF CACHE BOOL "" FORCE)
  set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
  set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
  add_subdirectory("${RECHIGROS_DOWNLOAD_DIR}/sdl3_image/SDL3_image-3.2.4"
    "${CMAKE_BINARY_DIR}/third_party/SDL3_image")
endif()

if(NOT ANDROID AND NOT IOS)
  set(GLAD_BASE "${JSDELIVR}/JoeyDeVries/LearnOpenGL@master")
  rechigros_download("glad/include/glad/glad.h" "${GLAD_BASE}/includes/glad/glad.h" "83AC58D5E5C25042ED2512418F0920F2BFF74C3B9561EA8A77CA614490CF81C2")
  rechigros_download("glad/include/KHR/khrplatform.h" "${GLAD_BASE}/includes/KHR/khrplatform.h" "FE7725728DC40D46134D9838D29E3BBBC95A70E77A1182A2BE9DED6470A2378D")
  rechigros_download("glad/src/glad.c" "${GLAD_BASE}/src/glad.c" "E56BE82DB3FAC82D62626ACD47B2DD0AA0BA8ECF2C95B3EF8E3465B67A42E5DF")
  add_library(glad_gl_core_33 STATIC "${RECHIGROS_DOWNLOAD_DIR}/glad/src/glad.c")
  target_include_directories(glad_gl_core_33 PUBLIC "${RECHIGROS_DOWNLOAD_DIR}/glad/include")
endif()

# Deflate codec for the ZIP package importer. The upstream release is split
# across several translation units; the inflate path and the archive reader are
# the only parts this project consumes.
set(MINIZ_BASE "${JSDELIVR}/richgel999/miniz@3.0.2")
rechigros_download("miniz/miniz.h"        "${MINIZ_BASE}/miniz.h"        "8033197E77C9567DE66425939AB6164405E54AA3C60ACF9312905D32C8CDDC03")
rechigros_download("miniz/miniz.c"        "${MINIZ_BASE}/miniz.c"        "D8B837863CC6536A270751893A9778C4241467D8906719AE6CB787A62FA797BC")
rechigros_download("miniz/miniz_common.h" "${MINIZ_BASE}/miniz_common.h" "8B60E76E726DFCF41A338C3A4C54C70038041A21E6314AA54FD3B7F729EA3412")
rechigros_download("miniz/miniz_tdef.h"   "${MINIZ_BASE}/miniz_tdef.h"   "12B09B189C27AA7D0B0EBE0BE2F3021A3BAB26FE7B19E9B3B6F5C4DF7C21E9DF")
rechigros_download("miniz/miniz_tinfl.h"  "${MINIZ_BASE}/miniz_tinfl.h"  "040AAD2D9CA23C487652C150277D950BE12E72C5453AE1F7C6713ED2EFD16B4E")
rechigros_download("miniz/miniz_zip.h"    "${MINIZ_BASE}/miniz_zip.h"    "C9343F0D251D92F3730D5FE684D4AF67AA0BED25D2D1D39D5FD4746A3C63F796")
rechigros_download("miniz/miniz_tdef.c"   "${MINIZ_BASE}/miniz_tdef.c"   "69A1D22A01E698286A7D2A8DF90884AA33B4F073E080749E1FCE76C5117A5A29")
rechigros_download("miniz/miniz_tinfl.c"  "${MINIZ_BASE}/miniz_tinfl.c"  "9B7EDC1FAB708166E10F5E90A618A91C95A91496C4692E38160FDAE810C1149A")
rechigros_download("miniz/miniz_zip.c"    "${MINIZ_BASE}/miniz_zip.c"    "7179062FDAA868D18E86AE9391FE47316EF4632C5A24FE3D3CE73D6535C7DD5C")

# miniz_export.h is produced by the upstream build system and is not part of
# the published sources. The codec is linked statically here, so the export
# attribute expands to nothing and the header can be generated directly.
set(MINIZ_DIR "${RECHIGROS_DOWNLOAD_DIR}/miniz")
set(MINIZ_EXPORT_HEADER "${MINIZ_DIR}/miniz_export.h")
if(NOT EXISTS "${MINIZ_EXPORT_HEADER}")
  file(WRITE "${MINIZ_EXPORT_HEADER}"
    "#ifndef MINIZ_EXPORT_H\n"
    "#define MINIZ_EXPORT_H\n"
    "\n"
    "#define MINIZ_EXPORT\n"
    "#define MINIZ_NO_EXPORT\n"
    "\n"
    "#endif\n")
endif()

# The archive importer lives in the platform layer, which is application-only, so the
# codec is compiled only for application builds.
if(RECHIGROS_BUILD_APP)
  add_library(miniz STATIC
    "${MINIZ_DIR}/miniz.c"
    "${MINIZ_DIR}/miniz_tdef.c"
    "${MINIZ_DIR}/miniz_tinfl.c"
    "${MINIZ_DIR}/miniz_zip.c")
  target_include_directories(miniz PUBLIC "${MINIZ_DIR}")
  # The public declarations in miniz.h are shaped by these macros, so consumers
  # must see the same definitions as the compiled translation units.
  target_compile_definitions(miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
  if(MSVC)
    # miniz_zip.c redefines the Win32 lean-imports macro that every target in this
    # project already receives; the redefinition is harmless.
    target_compile_options(miniz PRIVATE /wd4005)
  endif()
endif()

add_library(miniz_headers INTERFACE)
target_include_directories(miniz_headers INTERFACE "${MINIZ_DIR}")
target_compile_definitions(miniz_headers INTERFACE MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
