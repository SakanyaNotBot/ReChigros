add_library(rechigros_platform STATIC
  AssetManager.cpp
  Archive.cpp
  FileDialog.cpp
  FileSystem.cpp
  Gif.cpp)
if(IOS)
  # The document picker is Objective-C++ and needs the UIKit view controller SDL owns, so
  # it lives beside the shared dialog sources rather than in a generic branch.
  target_sources(rechigros_platform PRIVATE FileDialogIOS.mm)
  # The picker retains its delegate and security-scoped URL across calls; automatic
  # reference counting keeps those lifetimes correct instead of leaking one object per pick.
  set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/FileDialogIOS.mm"
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  # The Xcode generator reads source options through the target's per-file build flags;
  # this setting covers generators that do not forward the source property.
  set_target_properties(rechigros_platform PROPERTIES
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES")
endif()
target_include_directories(rechigros_platform PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(rechigros_platform PUBLIC ${RECHIGROS_SDL_TARGET} miniz)
target_compile_features(rechigros_platform PUBLIC cxx_std_20)

if(APPLE)
  # FileSystem uses iconv to normalise archive member names. Apple ships libiconv as a
  # separate stub library on both macOS and iOS.
  find_library(RECHIGROS_ICONV_LIBRARY iconv)
  if(RECHIGROS_ICONV_LIBRARY)
    target_link_libraries(rechigros_platform PUBLIC "${RECHIGROS_ICONV_LIBRARY}")
  endif()
endif()
