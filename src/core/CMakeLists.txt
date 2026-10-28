add_library(rechigros_core STATIC
  BpmTimeline.cpp
  ClickEffectItem.cpp
  Easing.cpp
  EventUtils.cpp
  GameplaySession.cpp
  HitOffsetIndicator.cpp
  JudgementEngine.cpp
  JudgeLine.cpp
  MessageParticleField.cpp
  NoteGeometry.cpp
  OfficialBlock.cpp
  OfficialEase.cpp
  OfficialLevelEffects.cpp
  PracticeJudgeWindows.cpp
  PrprEffect.cpp
  RpeNoiseArea.cpp
  RpeTrajectory.cpp)

target_include_directories(rechigros_core PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(rechigros_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(rechigros_core PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(rechigros_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(rechigros_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
