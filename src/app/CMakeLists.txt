set(RECHIGROS_APP_SOURCES
  main.cpp
  Application.cpp
  CommonScreens.cpp
  LibraryScreen.cpp
  DetailScreen.cpp
  SettingsScreen.cpp
  CalibrationScreen.cpp
  ResultScreen.cpp
  GameScreen.cpp
  ScreenCommon.cpp
  UiStyle.cpp
  UiText.cpp
  UiLayout.cpp
  CaptureHarness.cpp
  TriGridBackdrop.cpp
  CoverCache.cpp
  InfoFile.cpp
  Settings.cpp
  ChartLibrary.cpp
  Replay.cpp
  ImportCore.cpp
  ImportDialogs.cpp
  SelfTest.cpp)

if(ANDROID)
  add_library(ReChigros SHARED ${RECHIGROS_APP_SOURCES})
  set_target_properties(ReChigros PROPERTIES OUTPUT_NAME main)
else()
  add_executable(ReChigros ${RECHIGROS_APP_SOURCES})
endif()

target_include_directories(ReChigros PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(ReChigros PRIVATE
  ${RECHIGROS_SDL_TARGET}
  rechigros_core
  rechigros_chart
  rechigros_audio
  rechigros_render
  rechigros_platform
  imgui
  nlohmann_json_headers
  stb_headers
  miniz_headers)
if(TARGET miniz)
  target_link_libraries(ReChigros PRIVATE miniz)
endif()
if(TARGET SDL3::SDL3main)
  target_link_libraries(ReChigros PRIVATE SDL3::SDL3main)
endif()
if(NOT ANDROID AND NOT IOS)
  target_link_libraries(ReChigros PRIVATE glad_gl_core_33)
endif()

target_compile_definitions(ReChigros PRIVATE RECHIGROS_VERSION="${PROJECT_VERSION}")
if(ANDROID OR IOS)
  target_compile_definitions(ReChigros PRIVATE RECHIGROS_GLES=1)
endif()

if(APPLE AND NOT IOS)
  set_target_properties(ReChigros PROPERTIES MACOSX_BUNDLE TRUE MACOSX_BUNDLE_GUI_IDENTIFIER com.wuying.chigros)
endif()
if(IOS)
  set_target_properties(ReChigros PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/platform/ios/Info.plist"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.wuying.chigros"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "13.0"
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon")
  target_sources(ReChigros PRIVATE
    "${PROJECT_SOURCE_DIR}/platform/ios/Assets.xcassets"
    "${PROJECT_SOURCE_DIR}/platform/ios/LaunchScreen.storyboard")
  set_source_files_properties("${PROJECT_SOURCE_DIR}/platform/ios/Assets.xcassets" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_source_files_properties("${PROJECT_SOURCE_DIR}/platform/ios/LaunchScreen.storyboard" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

if(NOT ANDROID)
  add_custom_command(TARGET ReChigros POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:ReChigros>/assets")
endif()

# Batch check for the file import flows. It only links the import logic and the library /
# replay persistence it drives, so it runs without a window or a GL context.
if(NOT ANDROID AND NOT IOS)
  add_executable(rechigros_import_check
    ImportCheck.cpp
    ImportCore.cpp
    ChartLibrary.cpp
    InfoFile.cpp
    Replay.cpp)
  target_include_directories(rechigros_import_check PRIVATE "${PROJECT_SOURCE_DIR}/include")
  target_link_libraries(rechigros_import_check PRIVATE
    rechigros_platform
    nlohmann_json_headers
    miniz_headers)
  if(TARGET miniz)
    target_link_libraries(rechigros_import_check PRIVATE miniz)
  endif()
endif()
