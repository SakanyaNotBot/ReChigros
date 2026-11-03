set(RECHIGROS_AUDIO_SOURCES
  AudioEngine.cpp
  Decoder.cpp
  SkinSoundEffects.cpp)
if(IOS)
  # AVFoundation and Objective-C runtime types in the iOS backend require the
  # Objective-C front end, which the .m extension selects even when a generator does not
  # honor a per-source LANGUAGE property. The wrapper includes the shared .c file.
  list(APPEND RECHIGROS_AUDIO_SOURCES MiniaudioImpl.m)
else()
  list(APPEND RECHIGROS_AUDIO_SOURCES MiniaudioImpl.c)
endif()
add_library(rechigros_audio STATIC ${RECHIGROS_AUDIO_SOURCES})
target_include_directories(rechigros_audio PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_include_directories(rechigros_audio PRIVATE "${PROJECT_SOURCE_DIR}/src/audio")
target_link_libraries(rechigros_audio PUBLIC miniaudio_headers rechigros_platform)
target_compile_features(rechigros_audio PUBLIC cxx_std_20)

if(APPLE)
  # miniaudio talks to the system audio frameworks directly.
  find_library(RECHIGROS_AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
  find_library(RECHIGROS_COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
  find_library(RECHIGROS_COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
  target_link_libraries(rechigros_audio PUBLIC
    "${RECHIGROS_AUDIOTOOLBOX_FRAMEWORK}"
    "${RECHIGROS_COREAUDIO_FRAMEWORK}"
    "${RECHIGROS_COREFOUNDATION_FRAMEWORK}")
  if(IOS)
    find_library(RECHIGROS_AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
    find_library(RECHIGROS_FOUNDATION_FRAMEWORK Foundation REQUIRED)
    target_link_libraries(rechigros_audio PUBLIC
      "${RECHIGROS_AVFOUNDATION_FRAMEWORK}"
      "${RECHIGROS_FOUNDATION_FRAMEWORK}")
  endif()
endif()

if(MSVC)
  target_compile_options(rechigros_audio PRIVATE /W4 /permissive-)
  # The bundled single-file decoder is not warning-clean.
  set_source_files_properties(MiniaudioImpl.c PROPERTIES COMPILE_OPTIONS "/W0")
else()
  target_compile_options(rechigros_audio PRIVATE -Wall -Wextra)
  set_source_files_properties(MiniaudioImpl.c PROPERTIES COMPILE_OPTIONS "-w")
  if(IOS)
    set_source_files_properties(MiniaudioImpl.m PROPERTIES COMPILE_OPTIONS "-w")
  endif()
endif()
