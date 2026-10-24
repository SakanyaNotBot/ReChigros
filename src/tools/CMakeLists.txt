# Standalone diagnostics for the renderer. These are developer tools, not part of the
# application, and they need a desktop GL context, so the platform packages skip them.
add_executable(rechigros_block_probe block_probe.cpp)
target_include_directories(rechigros_block_probe PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(rechigros_block_probe PRIVATE
  rechigros_render rechigros_core rechigros_platform ${RECHIGROS_SDL_TARGET})
if(TARGET SDL3::SDL3main)
  target_link_libraries(rechigros_block_probe PRIVATE SDL3::SDL3main)
endif()

# Frame-time profile: drives a chart through the real renderer and reports the per-frame
# distribution, so performance work has a repeatable before/after gate.
add_executable(rechigros_frame_profile frame_profile.cpp)
target_include_directories(rechigros_frame_profile PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(rechigros_frame_profile PRIVATE
  rechigros_render rechigros_chart rechigros_core rechigros_platform ${RECHIGROS_SDL_TARGET})
if(TARGET SDL3::SDL3main)
  target_link_libraries(rechigros_frame_profile PRIVATE SDL3::SDL3main)
endif()

# Blocked-combo mask probe: builds the failed-challenge readout mask on the CPU and reports
# its cell coverage, so a mask that silently fails to build cannot hide behind a screenshot.
add_executable(rechigros_blocked_combo_probe blocked_combo_probe.cpp)
target_include_directories(rechigros_blocked_combo_probe PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(rechigros_blocked_combo_probe PRIVATE
  rechigros_render rechigros_core rechigros_platform ${RECHIGROS_SDL_TARGET})
if(TARGET SDL3::SDL3main)
  target_link_libraries(rechigros_blocked_combo_probe PRIVATE SDL3::SDL3main)
endif()

# Interface framework smoke test: proves the ObjectivelyMVC runtime (SDL3 GPU device,
# render device, window controller, view hierarchy) comes up before any screen is moved
# onto it. It is C, like the framework, so the bridge does not depend on C++ interop.
if(TARGET objectivelymvc)
  add_executable(rechigros_omvc_smoke omvc_smoke.c)
  set_source_files_properties(omvc_smoke.c PROPERTIES LANGUAGE C)
  set_target_properties(rechigros_omvc_smoke PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
  # The three framework archives reference each other (Theme pulls Objectively, the GPU
  # device pulls Objectively's Object), so all of them are named explicitly and repeated
  # once: a static archive only resolves symbols that are still unresolved when it is
  # reached, and the first pass leaves the lower layers' back-references pending.
  target_link_libraries(rechigros_omvc_smoke PRIVATE
    objectivelymvc objectivelygpu objectively ${RECHIGROS_SDL_TARGET})
endif()

# Recording encoder check: pushes synthetic frames through the video encoder and verifies a
# playable file comes out, so the encode path is gated independently of the renderer.
add_executable(rechigros_record_check record_check.cpp ../app/VideoEncoder.cpp)
target_include_directories(rechigros_record_check PRIVATE "${PROJECT_SOURCE_DIR}/include"
  "${PROJECT_SOURCE_DIR}/src/app")
target_link_libraries(rechigros_record_check PRIVATE
  rechigros_platform ${RECHIGROS_SDL_TARGET})
if(TARGET SDL3::SDL3main)
  target_link_libraries(rechigros_record_check PRIVATE SDL3::SDL3main)
endif()