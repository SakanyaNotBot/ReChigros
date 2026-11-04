add_library(rechigros_chart STATIC
  ChartLoader.cpp
  OfficialChartParser.cpp
  PecChartParser.cpp
  PlainChartParser.cpp
  RpeChartParser.cpp
  Md5.cpp)
target_include_directories(rechigros_chart PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(rechigros_chart PUBLIC rechigros_core nlohmann_json::nlohmann_json)
target_compile_features(rechigros_chart PUBLIC cxx_std_20)
