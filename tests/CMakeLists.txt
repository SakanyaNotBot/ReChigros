# Shared test helpers.
set(RECHIGROS_TEST_SUPPORT "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(rechigros_core_tests CoreTests.cpp)
# CoreTests also exercises the PEC parser, so it needs the chart layer when available.
if(TARGET rechigros_chart)
  target_link_libraries(rechigros_core_tests PRIVATE rechigros_core rechigros_chart)
else()
  target_link_libraries(rechigros_core_tests PRIVATE rechigros_core)
endif()
if(NOT ANDROID AND NOT IOS)
  add_test(NAME rechigros_core_tests COMMAND rechigros_core_tests)
endif()

if(TARGET rechigros_chart)
  add_executable(rechigros_chart_tests ChartParserTests.cpp)
  target_link_libraries(rechigros_chart_tests PRIVATE rechigros_chart)
  if(NOT ANDROID AND NOT IOS)
    add_test(NAME rechigros_chart_tests COMMAND rechigros_chart_tests)
  endif()
endif()

if(TARGET rechigros_platform)
  add_executable(rechigros_platform_tests PlatformTests.cpp)
  # The fixture writer compresses entries so the reader's raw-inflate path is exercised.
  target_link_libraries(rechigros_platform_tests PRIVATE rechigros_platform miniz)
  if(NOT ANDROID AND NOT IOS)
    add_test(NAME rechigros_platform_tests COMMAND rechigros_platform_tests)
  endif()
endif()

if(TARGET rechigros_audio)
  add_executable(rechigros_audio_tests AudioTests.cpp)
  target_link_libraries(rechigros_audio_tests PRIVATE rechigros_audio)
  if(NOT ANDROID AND NOT IOS)
    add_test(NAME rechigros_audio_tests COMMAND rechigros_audio_tests)
  endif()
endif()

if(TARGET rechigros_render)
  add_executable(rechigros_transition_tests TransitionTests.cpp)
  target_link_libraries(rechigros_transition_tests PRIVATE rechigros_render)
  if(NOT ANDROID AND NOT IOS)
    add_test(NAME rechigros_transition_tests COMMAND rechigros_transition_tests)
  endif()
endif()

add_executable(rechigros_level_effect_tests LevelEffectTests.cpp)
target_link_libraries(rechigros_level_effect_tests PRIVATE rechigros_core)
if(NOT ANDROID AND NOT IOS)
  add_test(NAME rechigros_level_effect_tests COMMAND rechigros_level_effect_tests)
endif()

# Application-layer data helpers (metadata labels, settings clamping, replay storage).
# This suite compiles the few application sources that are free of interface and GPU
# dependencies, so their behaviour is covered by the normal test run.
if(TARGET rechigros_platform)
  add_executable(rechigros_app_data_tests
    AppDataTests.cpp
    ../src/app/InfoFile.cpp
    ../src/app/ChartLibrary.cpp
    ../src/app/Settings.cpp
    ../src/app/SmoothingClock.cpp
    ../src/app/Replay.cpp)
  target_include_directories(rechigros_app_data_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src/app"
    "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(rechigros_app_data_tests PRIVATE
    rechigros_core rechigros_platform nlohmann_json_headers)
  if(NOT ANDROID AND NOT IOS)
    add_test(NAME rechigros_app_data_tests COMMAND rechigros_app_data_tests)
  endif()
endif()

# Resource-pack / skin material configuration: parsing, defaults and merge rules. The
# module is headless, so the same source compiles into the render library and this suite.
if(TARGET rechigros_platform)
  add_executable(rechigros_skin_tests
    SkinConfigTests.cpp
    ../src/render/skin/SkinConfig.cpp)
  target_include_directories(rechigros_skin_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(rechigros_skin_tests PRIVATE rechigros_platform nlohmann_json_headers)
  if(NOT ANDROID AND NOT IOS)
    add_test(NAME rechigros_skin_tests COMMAND rechigros_skin_tests)
  endif()
endif()

target_include_directories(rechigros_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(rechigros_chart_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(rechigros_platform_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(rechigros_audio_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(rechigros_transition_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(rechigros_level_effect_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")