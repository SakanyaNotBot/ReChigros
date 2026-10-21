add_library(rechigros_platform STATIC
  AssetManager.cpp
  Archive.cpp
  FileSystem.cpp
  Gif.cpp)
target_include_directories(rechigros_platform PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(rechigros_platform PUBLIC ${RECHIGROS_SDL_TARGET} miniz)
target_compile_features(rechigros_platform PUBLIC cxx_std_20)
