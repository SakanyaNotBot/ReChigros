add_library(rechigros_audio STATIC
  AudioEngine.cpp
  Decoder.cpp
  SkinSoundEffects.cpp
  MiniaudioImpl.c)
target_include_directories(rechigros_audio PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_include_directories(rechigros_audio PRIVATE "${PROJECT_SOURCE_DIR}/src/audio")
target_link_libraries(rechigros_audio PUBLIC miniaudio_headers rechigros_platform)
target_compile_features(rechigros_audio PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(rechigros_audio PRIVATE /W4 /permissive-)
  # The bundled single-file decoder is not warning-clean.
  set_source_files_properties(MiniaudioImpl.c PROPERTIES COMPILE_OPTIONS "/W0")
else()
  target_compile_options(rechigros_audio PRIVATE -Wall -Wextra)
  set_source_files_properties(MiniaudioImpl.c PROPERTIES COMPILE_OPTIONS "-w")
endif()
