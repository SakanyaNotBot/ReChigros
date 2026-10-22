# Standalone diagnostics for the renderer. These are developer tools, not part of the
# application, and they need a desktop GL context, so the platform packages skip them.
add_executable(rechigros_block_probe block_probe.cpp)
target_include_directories(rechigros_block_probe PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(rechigros_block_probe PRIVATE
  rechigros_render rechigros_core rechigros_platform ${RECHIGROS_SDL_TARGET})
if(TARGET SDL2::SDL2main)
  target_link_libraries(rechigros_block_probe PRIVATE SDL2::SDL2main)
endif()
