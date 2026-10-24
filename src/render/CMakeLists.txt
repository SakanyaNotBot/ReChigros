add_library(rechigros_render STATIC
  GameRenderer.cpp
  stb_image.cpp
  stb_image_write.cpp
  gl/Texture.cpp
  gl/Framebuffer.cpp
  gl/ShaderProgram.cpp
  gl/GlDebug.cpp
  asset/TextureCache.cpp
  skin/SkinConfig.cpp
  batch/SpriteBatch.cpp
  text/TextRenderer.cpp
  fx/AmetrineVignette.cpp
  fx/BloomPass.cpp
  fx/FullscreenPass.cpp  fx/FxaaPass.cpp
  fx/BadEffectField.cpp
  fx/BlockCompositePass.cpp
  fx/BlockOverlay.cpp
  fx/BlockedComboField.cpp
  fx/ClickEffectField.cpp
  fx/DsGridPass.cpp
  fx/HitEffect.cpp
  fx/KaleidoBackdropPass.cpp
  fx/PauseBackdropPass.cpp
  fx/PrprChain.cpp
  scene/BackgroundBlur.cpp
  scene/PauseHudBlur.cpp
  scene/StageLayout.cpp
  scene/LinePass.cpp
  scene/NoteBatch.cpp
  scene/ChartDebugOverlay.cpp
  scene/HudPass.cpp
  scene/Transition.cpp
  scene/PauseBackdrop.cpp)

target_include_directories(rechigros_render PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_include_directories(rechigros_render PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/text")
target_link_libraries(rechigros_render PUBLIC rechigros_core rechigros_platform imgui stb_headers nlohmann_json::nlohmann_json)
if(ANDROID)
  target_link_libraries(rechigros_render PUBLIC GLESv3)
  target_compile_definitions(rechigros_render PUBLIC RECHIGROS_GLES=1)
elseif(IOS)
  find_library(OPENGLES_FRAMEWORK OpenGLES REQUIRED)
  target_link_libraries(rechigros_render PUBLIC "${OPENGLES_FRAMEWORK}")
  target_compile_definitions(rechigros_render PUBLIC RECHIGROS_GLES=1)
else()
  target_link_libraries(rechigros_render PUBLIC glad_gl_core_33)
endif()

# Headless smoke path: loads a chart, renders offscreen frames and writes a PNG.
if(NOT ANDROID AND NOT IOS)
  add_executable(rechigros_render_smoke RenderSmoke.cpp)
  target_include_directories(rechigros_render_smoke PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(rechigros_render_smoke PRIVATE
    rechigros_render rechigros_chart rechigros_core rechigros_platform ${RECHIGROS_SDL_TARGET})
  if(TARGET SDL3::SDL3main)
    target_link_libraries(rechigros_render_smoke PRIVATE SDL3::SDL3main)
  endif()
endif()
