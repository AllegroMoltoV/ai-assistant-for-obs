cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version} LANGUAGES C CXX)

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

# OpenAI の要求と応答の JSON を扱う。obs_data は文字列の配列を扱えないため使わない
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
  GIT_SHALLOW TRUE
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(nlohmann_json)

# libobs と libcurl に依存させない中核。プラグインと単体テストの両方から使う
file(
  GLOB AIA_CORE_SOURCES
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.h"
)
add_library(aia-core STATIC ${AIA_CORE_SOURCES})
target_compile_features(aia-core PUBLIC cxx_std_17)
target_include_directories(aia-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(aia-core PUBLIC nlohmann_json::nlohmann_json)
# API キーの暗号化に DPAPI を使う。Windows 以外では未対応を返す実装のためリンクしない
if(WIN32)
  target_link_libraries(aia-core PUBLIC crypt32)
endif()
# Linux で MODULE へ静的ライブラリをリンクするために必要。テンプレートの plugin-support と同じ扱い
set_target_properties(aia-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(${CMAKE_PROJECT_NAME} MODULE)

target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)

find_package(libobs REQUIRED)
# Windows では obs-deps の import ライブラリにリンクし、実行時は OBS 同梱の libcurl.dll を使う
find_package(CURL REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs CURL::libcurl aia-core)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 REQUIRED COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

# ソースと翻訳ファイルを追加のたびに CMakeLists.txt を編集せずに拾う
file(
  GLOB PLUGIN_SOURCES
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

file(GLOB_RECURSE PLUGIN_RESOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/locale/*.ini")

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${PLUGIN_SOURCES} ${PLUGIN_RESOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${PLUGIN_SOURCES} ${PLUGIN_RESOURCES})

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# 外部 API を使わない単体テスト。CI の Windows のビルドでも ctest で実行する
option(AIA_BUILD_TESTS "Build unit tests for the core layer" OFF)
if(AIA_BUILD_TESTS)
  enable_testing()
  add_executable(aia-core-tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/core-tests.cpp")
  target_link_libraries(aia-core-tests PRIVATE aia-core)
  add_test(NAME aia-core-tests COMMAND aia-core-tests)
  # API キーの保存は DPAPI を使うため Windows だけで確かめる
  if(WIN32)
    add_executable(aia-api-key-tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/api-key-tests.cpp")
    target_link_libraries(aia-api-key-tests PRIVATE aia-core)
    add_test(NAME aia-api-key-tests COMMAND aia-api-key-tests)
  endif()
endif()

# モデルを比べる開発用の道具。OBS を使わずに、模した道具の結果で Responses API と多段の会話をする。CI ではビルドしない
option(AIA_BUILD_MODEL_EVAL "Build the model comparison tool" OFF)
if(AIA_BUILD_MODEL_EVAL)
  add_executable(
    aia-model-eval
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/model-eval/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/openai-client.cpp"
  )
  target_link_libraries(aia-model-eval PRIVATE aia-core CURL::libcurl)
endif()

# ビルドの直後に OBS のプラグインのフォルダーへ配置する。
# 導入先はほかのプラグインと共有のため既定は OFF とし、実機で確かめるときだけ ON にする
option(ENABLE_POST_BUILD_INSTALL "Run cmake --install after build" OFF)

if(WIN32 AND ENABLE_POST_BUILD_INSTALL)
  add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config "$<CONFIG>"
    COMMENT "Installing OBS plugin (cmake --install)"
    VERBATIM
  )
endif()
